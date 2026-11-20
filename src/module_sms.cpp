#include <ArduinoJson.h>

#include "configuration.h"
#include "module_sms.h"
#include "module_alertes_sms.h"
#include "module_appli.h"
#include "module_carte_sd.h"
#include "module_haut_parleur.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_modem.h"
#include "module_systeme.h"
#include "outils_texte.h"

namespace {

enum class TravailSMS : uint8_t { ENVOYER, LIRE_UN_NOUVEAU_SMS, LISTER };

struct DemandeSMS {
    TravailSMS travail;
    int        numeroDansLaSim;
    char       numero[24];        // ENVOYER : destinataire ; LIRE_UN_NOUVEAU_SMS : mémoire (« SM », « ME »)
    char       texte[480];
};

QueueHandle_t fileDuModuleSMS = nullptr;

const int CARACTERES_PAR_SMS_AVEC_ACCENTS = 70;
const int CARACTERES_PAR_SMS_SANS_ACCENT  = 160;

String premiereLigneDe(const String& texte) {
    const int fin = texte.indexOf('\n');
    String ligne = fin >= 0 ? texte.substring(0, fin) : texte;
    ligne.trim();
    return ligne;
}

// ─────────────────────────────── Envoi ───────────────────────────────

bool envoyerUnMorceauSansAccent(const String& numero, const String& morceau) {
    const String commande = String("AT+CMGS=\"") + numero + "\"";
    return envoyerCommandeATAvecDonnees(commande, (const uint8_t*)morceau.c_str(), morceau.length(), true, nullptr, 60000)
           == ResultatAT::OK;
}

bool envoyerUnMorceauEnUcs2(const String& numeroEnUcs2, const String& morceauEnUcs2) {
    const String commande = String("AT+CMGS=\"") + numeroEnUcs2 + "\"";
    return envoyerCommandeATAvecDonnees(commande, (const uint8_t*)morceauEnUcs2.c_str(), morceauEnUcs2.length(), true,
                                        nullptr, 60000) == ResultatAT::OK;
}

bool envoyerSansAccent(const String& numero, const String& texte) {
    // Le modem passe en jeu de caractères GSM le temps de l'envoi (160 caractères par SMS).
    envoyerCommandeAT("AT+CSCS=\"GSM\"");
    envoyerCommandeAT("AT+CSMP=17,167,0,0");
    bool toutEnvoye = true;
    for (size_t debut = 0; debut < texte.length() && toutEnvoye; debut += CARACTERES_PAR_SMS_SANS_ACCENT) {
        toutEnvoye = envoyerUnMorceauSansAccent(numero, texte.substring(debut, debut + CARACTERES_PAR_SMS_SANS_ACCENT));
    }
    envoyerCommandeAT("AT+CSCS=\"UCS2\"");
    envoyerCommandeAT("AT+CSMP=17,167,0,8");
    return toutEnvoye;
}

bool envoyerAvecAccents(const String& numero, const String& texte) {
    const String numeroEnUcs2 = convertirTexteEnUcs2Hexadecimal(numero);
    const String texteEnUcs2  = convertirTexteEnUcs2Hexadecimal(texte);
    const size_t chiffresParSMS = CARACTERES_PAR_SMS_AVEC_ACCENTS * 4;
    size_t debut = 0;
    while (debut < texteEnUcs2.length()) {
        size_t fin = min(debut + chiffresParSMS, (size_t)texteEnUcs2.length());
        // Ne pas couper un émoji en deux (paire de substitution D800..DBFF suivie de DC00..DFFF).
        if (fin < texteEnUcs2.length()) {
            const String dernierCaractere = texteEnUcs2.substring(fin - 4, fin);
            if (dernierCaractere.startsWith("D8") || dernierCaractere.startsWith("D9")
                || dernierCaractere.startsWith("DA") || dernierCaractere.startsWith("DB")) {
                fin -= 4;
            }
        }
        if (!envoyerUnMorceauEnUcs2(numeroEnUcs2, texteEnUcs2.substring(debut, fin))) return false;
        debut = fin;
    }
    return true;
}

void envoyerLeSMS(const DemandeSMS& demande) {
    const String numero(demande.numero);
    const String texte(demande.texte);
    bool reussi = false;
    if (prendreLeModem(30000)) {   // personne ne doit changer le jeu de caractères pendant l'envoi
        reussi = texteEnAsciiSimple(texte) ? envoyerSansAccent(numero, texte) : envoyerAvecAccents(numero, texte);
        rendreLeModem();
    }
    ecrireDansLeJournal("[SMS] Envoi à %s : %s.", numero.c_str(), reussi ? "réussi" : "ÉCHEC");

    JsonDocument evenement;
    evenement["t"]   = "sms_envoi";
    evenement["num"] = numero;
    evenement["ok"]  = reussi;
    publierUnEvenementVersLAppli(evenement);
}

// ─────────────────────────────── Lecture ───────────────────────────────

const uint32_t PERIODE_DE_VERIFICATION_DES_SMS_NON_LUS_MS = 60 * UNE_SECONDE_MS;
const size_t   TEXTE_MAXIMUM_VERS_L_APPLI = 300;   // octets : le message Bluetooth doit tenir dans une notification

String decoderLeTexteDuSMS(const String& texte) {
    String nettoye = texte;
    nettoye.trim();
    bool toutEnHexadecimal = nettoye.length() > 0 && nettoye.length() % 4 == 0;
    for (size_t position = 0; toutEnHexadecimal && position < nettoye.length(); position++) {
        toutEnHexadecimal = isxdigit((unsigned char)nettoye[position]);
    }
    return toutEnHexadecimal ? convertirUcs2HexadecimalEnTexte(nettoye) : nettoye;
}

// Coupe un texte UTF-8 sans couper un caractère accentué en deux.
String raccourcir(const String& texte, size_t tailleMaximum) {
    if (texte.length() <= tailleMaximum) return texte;
    size_t fin = tailleMaximum;
    while (fin > 0 && (((uint8_t)texte[fin]) & 0xC0) == 0x80) fin--;
    return texte.substring(0, fin) + "…";
}

// Entête : <index>,"<état>","<expéditeur>","<nom>","<date>"  (pour +CMGL)
//          "<état>","<expéditeur>","<nom>","<date>"           (pour +CMGR)
void lireLEntete(const String& entete, bool commenceParLIndex, MessageSMS& sms) {
    String champs[8];
    const int nombre = decouperLesChamps(entete, champs, 8);
    const int decalage = commenceParLIndex ? 1 : 0;
    if (commenceParLIndex && nombre > 0) sms.numeroDansLaSim = champs[0].toInt();
    const String etat = nombre > decalage ? decoderSiCEstDeLUcs2(champs[decalage]) : "";
    sms.lu         = etat.indexOf("UNREAD") < 0;
    sms.expediteur = nombre > decalage + 1 ? decoderSiCEstDeLUcs2(champs[decalage + 1]) : "";
    // La date est entre guillemets : sa virgule ne la coupe pas en deux.
    sms.date       = nombre > decalage + 3 ? decoderSiCEstDeLUcs2(champs[decalage + 3]) : "";
}

// Choisit la mémoire où lire les SMS (« SM » = carte SIM, « ME » = modem). Le modem range les SMS
// reçus là où il veut (+CMTI l'indique) : sans ce choix, AT+CMGR lirait dans la mauvaise mémoire.
// En jeu de caractères UCS2, le nom de la mémoire s'écrit lui aussi en UCS2.
void choisirLaMemoireDeLecture(const String& memoire) {
    if (memoire.isEmpty()) return;
    envoyerCommandeAT(String("AT+CPMS=\"") + convertirTexteEnUcs2Hexadecimal(memoire) + "\"", nullptr, 3000);
}

bool lireUnSMS(int numeroDansLaSim, const String& memoire, MessageSMS& sms) {
    if (!prendreLeModem(20000)) return false;
    choisirLaMemoireDeLecture(memoire);
    String reponse;
    bool lu = envoyerCommandeAT(String("AT+CMGR=") + numeroDansLaSim, &reponse, 5000) == ResultatAT::OK;
    rendreLeModem();
    if (!lu) return false;
    const int debutEntete = reponse.indexOf("+CMGR:");
    if (debutEntete < 0) return false;
    int finEntete = reponse.indexOf('\n', debutEntete);
    if (finEntete < 0) finEntete = reponse.length();
    sms.numeroDansLaSim = numeroDansLaSim;
    lireLEntete(reponse.substring(debutEntete + 6, finEntete), false, sms);
    sms.texte = decoderLeTexteDuSMS(reponse.substring(min((int)reponse.length(), finEntete + 1)));
    return true;
}

// Liste les SMS d'un état donné (« ALL », « REC UNREAD »...). Le modem attend normalement l'état
// écrit en UCS2 ; certains logiciels du modem le veulent en clair : on essaie les deux.
// En cas d'échec, « raison » explique pourquoi.
bool listerLesSMSDansLEtat(const char* etat, const std::function<void(const MessageSMS&)>& pourChaqueSMS, String* raison) {
    if (!leModemEstPret()) { if (raison) *raison = "Le modem n'a pas encore démarré"; return false; }
    if (!prendreLeModem(30000)) { if (raison) *raison = "Le modem est occupé (envoi de SMS ou appel en cours)"; return false; }
    String reponse;
    ResultatAT resultat = envoyerCommandeAT(String("AT+CMGL=\"") + convertirTexteEnUcs2Hexadecimal(etat) + "\"", &reponse, 20000);
    if (resultat == ResultatAT::ERREUR) {
        resultat = envoyerCommandeAT(String("AT+CMGL=\"") + etat + "\"", &reponse, 20000);
    }
    rendreLeModem();
    if (resultat != ResultatAT::OK) {
        if (raison) {
            *raison = resultat == ResultatAT::ERREUR ? String("Le modem refuse : ") + premiereLigneDe(reponse)
                                                     : String("Le modem ne répond pas");
        }
        ecrireDansLeJournal("[SMS] Lecture de la liste (%s) impossible : %s", etat, reponse.c_str());
        return false;
    }
    int position = 0;
    while (true) {
        const int debutEntete = reponse.indexOf("+CMGL:", position);
        if (debutEntete < 0) break;
        const int finEntete = reponse.indexOf('\n', debutEntete);
        if (finEntete < 0) break;
        int finTexte = reponse.indexOf('\n', finEntete + 1);
        if (finTexte < 0) finTexte = reponse.length();
        MessageSMS sms;
        lireLEntete(reponse.substring(debutEntete + 6, finEntete), true, sms);
        sms.texte = decoderLeTexteDuSMS(reponse.substring(finEntete + 1, finTexte));
        pourChaqueSMS(sms);
        position = finTexte;
    }
    return true;
}

// La mémoire des SMS presque pleine empêche d'en recevoir d'autres (le modem ne dit rien).
// Les SMS déjà lus sont enregistrés sur la carte SD (/sms/) : on les efface de la SIM.
void libererDeLaPlaceSiBesoin() {
    String reponse;
    if (envoyerCommandeAT("AT+CPMS?", &reponse, 3000) != ResultatAT::OK) return;
    String champs[9];
    const int nombre = decouperLesChamps(reponse.substring(reponse.indexOf(':') + 1), champs, 9);
    if (nombre < 3) return;
    const int utilises = champs[1].toInt(), capacite = champs[2].toInt();
    if (capacite <= 0 || utilises < capacite - 3) return;
    const bool efface = envoyerCommandeAT("AT+CMGD=1,1", nullptr, 25000) == ResultatAT::OK;   // 1,1 = tous les SMS lus
    ecrireDansLeJournal("[SMS] Mémoire des SMS presque pleine (%d/%d) : SMS déjà lus effacés de la SIM %s.",
                        utilises, capacite, efface ? "(copie sur la carte SD)" : "— ÉCHEC");
}

void enregistrerLeSMSSurLaCarteSD(const MessageSMS& sms) {
    const String chemin = String("/sms/") + (lHeureEstConnue() ? dateDuJourPourNomDeFichier() : String("sans-date")) + ".txt";
    ajouterALaFinDUnFichier(chemin.c_str(), sms.date + "  " + sms.expediteur + " : " + sms.texte + "\n");
}

void annoncerUnNouveauSMS(const MessageSMS& sms) {
    ecrireDansLeJournal("[SMS] Reçu de %s : %s", sms.expediteur.c_str(), sms.texte.c_str());
    jouerLeSonDUnNouveauSMS();
    enregistrerLeSMSSurLaCarteSD(sms);

    JsonDocument evenement;
    evenement["t"]    = "sms_recu";
    evenement["i"]    = sms.numeroDansLaSim;
    evenement["de"]   = sms.expediteur;
    evenement["date"] = sms.date;
    evenement["txt"]  = raccourcir(sms.texte, TEXTE_MAXIMUM_VERS_L_APPLI);
    publierUnEvenementVersLAppli(evenement);

    // « Stop » / « Marche » envoyé depuis le numéro des alertes : coupe ou remet les SMS automatiques.
    traiterUnSMSDeCommandeDesAlertes(sms.expediteur, sms.texte);
}

void traiterUnNouveauSMS(int numeroDansLaSim, const String& memoire) {
    MessageSMS sms;
    bool lu = lireUnSMS(numeroDansLaSim, memoire, sms);
    if (!lu) {   // le modem finit parfois d'enregistrer le SMS juste après l'avoir annoncé
        vTaskDelay(pdMS_TO_TICKS(1500));
        lu = lireUnSMS(numeroDansLaSim, memoire, sms);
    }
    if (!lu) {
        ecrireDansLeJournal("[SMS] Nouveau SMS n°%d (%s) illisible : il sera repris par la vérification des SMS non lus.",
                            numeroDansLaSim, memoire.c_str());
        return;
    }
    annoncerUnNouveauSMS(sms);
    libererDeLaPlaceSiBesoin();
}

// Filet de sécurité : un SMS arrivé pendant un redémarrage de l'ESP32, ou dont l'annonce +CMTI
// s'est perdue, reste « non lu » dans la SIM. On les cherche au démarrage puis chaque minute.
// AT+CMGL les marque comme lus : chacun n'est annoncé qu'une fois.
void traiterLesSMSNonLus() {
    int nombre = 0;
    listerLesSMSDansLEtat("REC UNREAD", [&](const MessageSMS& sms) {
        annoncerUnNouveauSMS(sms);
        nombre++;
    }, nullptr);
    if (nombre > 0) {
        ecrireDansLeJournal("[SMS] %d SMS non lu(s) retrouvé(s) dans la SIM.", nombre);
        libererDeLaPlaceSiBesoin();
    }
}

// Liste de tous les SMS pour l'application (commande SMS_LISTE). Faite par la tâche SMS et pas par
// la tâche Bluetooth : la lecture peut prendre plusieurs secondes et ne doit pas bloquer la liaison.
// Chaque SMS part dans un message : {"t":"sms","i":3,"de":"+336...","date":"...","txt":"...","lu":1}
void envoyerLaListeALAppli() {
    JsonDocument debut;
    debut["t"] = "sms_liste_debut";
    publierUnEvenementVersLAppli(debut);
    int nombre = 0;
    String raison;
    const bool reussi = listerLesSMSDansLEtat("ALL", [&](const MessageSMS& sms) {
        JsonDocument d;
        d["t"]    = "sms";
        d["i"]    = sms.numeroDansLaSim;
        d["de"]   = sms.expediteur;
        d["date"] = sms.date;
        d["txt"]  = raccourcir(sms.texte, TEXTE_MAXIMUM_VERS_L_APPLI);
        d["lu"]   = sms.lu ? 1 : 0;
        publierUnEvenementVersLAppli(d);
        nombre++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }, &raison);
    repondreALAppli("SMS_LISTE", reussi, reussi ? (String(nombre) + " SMS").c_str() : raison.c_str());
}

// Appelée par le module modem pour « +CMTI: "SM",3 » (ne doit pas envoyer de commande AT).
void quandUnSMSArrive(const String& ligne) {
    DemandeSMS demande = {};
    demande.travail         = TravailSMS::LIRE_UN_NOUVEAU_SMS;
    demande.numeroDansLaSim = ligne.substring(ligne.lastIndexOf(',') + 1).toInt();
    // Mémoire entre les premiers guillemets : "SM", "ME", ou son écriture UCS2 "0053004D".
    const int guillemet = ligne.indexOf('"');
    const int finGuillemet = guillemet >= 0 ? ligne.indexOf('"', guillemet + 1) : -1;
    if (finGuillemet > guillemet) {
        strlcpy(demande.numero, decoderSiCEstDeLUcs2(ligne.substring(guillemet + 1, finGuillemet)).c_str(), sizeof(demande.numero));
    }
    if (fileDuModuleSMS != nullptr) xQueueSend(fileDuModuleSMS, &demande, 0);
}

void tacheSMS(void*) {
    attendreQueLeModemSoitPret(portMAX_DELAY);
    static DemandeSMS demande;   // grande structure : hors de la pile
    vTaskDelay(pdMS_TO_TICKS(5 * UNE_SECONDE_MS));
    traiterLesSMSNonLus();
    uint32_t derniereVerification = millis();
    for (;;) {
        if (xQueueReceive(fileDuModuleSMS, &demande, pdMS_TO_TICKS(5 * UNE_SECONDE_MS)) == pdPASS) {
            switch (demande.travail) {
                case TravailSMS::ENVOYER:             envoyerLeSMS(demande); break;
                case TravailSMS::LIRE_UN_NOUVEAU_SMS: traiterUnNouveauSMS(demande.numeroDansLaSim, String(demande.numero)); break;
                case TravailSMS::LISTER:              envoyerLaListeALAppli(); break;
            }
        }
        if (millis() - derniereVerification >= PERIODE_DE_VERIFICATION_DES_SMS_NON_LUS_MS) {
            derniereVerification = millis();
            traiterLesSMSNonLus();
        }
    }
}

} // namespace

bool demanderLEnvoiDUnSMS(const String& numero, const String& texte) {
    if (numero.isEmpty() || texte.isEmpty() || fileDuModuleSMS == nullptr) return false;
    // La demande est grande (texte de 480 octets) : elle est préparée dans le tas plutôt que sur la pile.
    DemandeSMS* demande = (DemandeSMS*)calloc(1, sizeof(DemandeSMS));
    if (demande == nullptr) return false;
    demande->travail = TravailSMS::ENVOYER;
    strlcpy(demande->numero, numero.c_str(), sizeof(demande->numero));
    strlcpy(demande->texte, texte.c_str(), sizeof(demande->texte));
    const bool ajoutee = xQueueSend(fileDuModuleSMS, demande, 0) == pdPASS;
    free(demande);
    return ajoutee;
}

bool listerLesSMS(const std::function<void(const MessageSMS&)>& pourChaqueSMS) {
    return listerLesSMSDansLEtat("ALL", pourChaqueSMS, nullptr);
}

bool demanderLaListeDesSMS() {
    if (fileDuModuleSMS == nullptr) return false;
    DemandeSMS* demande = (DemandeSMS*)calloc(1, sizeof(DemandeSMS));
    if (demande == nullptr) return false;
    demande->travail = TravailSMS::LISTER;
    const bool ajoutee = xQueueSend(fileDuModuleSMS, demande, 0) == pdPASS;
    free(demande);
    return ajoutee;
}

bool supprimerUnSMS(int numeroDansLaSim) {
    return envoyerCommandeAT(String("AT+CMGD=") + numeroDansLaSim, nullptr, 5000) == ResultatAT::OK;
}

bool demarrerModuleSMS() {
    fileDuModuleSMS = xQueueCreate(4, sizeof(DemandeSMS));
    if (fileDuModuleSMS == nullptr) return false;
    ecouterLesNotificationsDuModem("+CMTI:", quandUnSMSArrive);
    return creerTacheSurveillee(tacheSMS, "SMS", 6144, PRIORITE_NORMALE, COEUR_COMMUNICATIONS);
}
