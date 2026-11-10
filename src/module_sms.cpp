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

enum class TravailSMS : uint8_t { ENVOYER, LIRE_UN_NOUVEAU_SMS };

struct DemandeSMS {
    TravailSMS travail;
    int        numeroDansLaSim;
    char       numero[24];
    char       texte[480];
};

QueueHandle_t fileDuModuleSMS = nullptr;

const int CARACTERES_PAR_SMS_AVEC_ACCENTS = 70;
const int CARACTERES_PAR_SMS_SANS_ACCENT  = 160;

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

String decoderLeTexteDuSMS(const String& texte) {
    String nettoye = texte;
    nettoye.trim();
    bool toutEnHexadecimal = nettoye.length() > 0 && nettoye.length() % 4 == 0;
    for (size_t position = 0; toutEnHexadecimal && position < nettoye.length(); position++) {
        toutEnHexadecimal = isxdigit((unsigned char)nettoye[position]);
    }
    return toutEnHexadecimal ? convertirUcs2HexadecimalEnTexte(nettoye) : nettoye;
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

bool lireUnSMS(int numeroDansLaSim, MessageSMS& sms) {
    String reponse;
    if (envoyerCommandeAT(String("AT+CMGR=") + numeroDansLaSim, &reponse, 5000) != ResultatAT::OK) return false;
    const int debutEntete = reponse.indexOf("+CMGR:");
    if (debutEntete < 0) return false;
    const int finEntete = reponse.indexOf('\n', debutEntete);
    sms.numeroDansLaSim = numeroDansLaSim;
    lireLEntete(reponse.substring(debutEntete + 6, finEntete), false, sms);
    sms.texte = decoderLeTexteDuSMS(reponse.substring(finEntete + 1));
    return true;
}

void enregistrerLeSMSSurLaCarteSD(const MessageSMS& sms) {
    const String chemin = String("/sms/") + (lHeureEstConnue() ? dateDuJourPourNomDeFichier() : String("sans-date")) + ".txt";
    ajouterALaFinDUnFichier(chemin.c_str(), sms.date + "  " + sms.expediteur + " : " + sms.texte + "\n");
}

void traiterUnNouveauSMS(int numeroDansLaSim) {
    MessageSMS sms;
    if (!lireUnSMS(numeroDansLaSim, sms)) {
        ecrireDansLeJournal("[SMS] Nouveau SMS n°%d illisible.", numeroDansLaSim);
        return;
    }
    ecrireDansLeJournal("[SMS] Reçu de %s : %s", sms.expediteur.c_str(), sms.texte.c_str());
    jouerLeSonDUnNouveauSMS();
    enregistrerLeSMSSurLaCarteSD(sms);

    JsonDocument evenement;
    evenement["t"]    = "sms_recu";
    evenement["i"]    = sms.numeroDansLaSim;
    evenement["de"]   = sms.expediteur;
    evenement["date"] = sms.date;
    evenement["txt"]  = sms.texte;
    publierUnEvenementVersLAppli(evenement);

    // « Stop » / « Marche » envoyé depuis le numéro des alertes : coupe ou remet les SMS automatiques.
    traiterUnSMSDeCommandeDesAlertes(sms.expediteur, sms.texte);
}

// Appelée par le module modem pour « +CMTI: "SM",3 » (ne doit pas envoyer de commande AT).
void quandUnSMSArrive(const String& ligne) {
    DemandeSMS demande = {};
    demande.travail         = TravailSMS::LIRE_UN_NOUVEAU_SMS;
    demande.numeroDansLaSim = ligne.substring(ligne.lastIndexOf(',') + 1).toInt();
    xQueueSend(fileDuModuleSMS, &demande, 0);
}

void tacheSMS(void*) {
    attendreQueLeModemSoitPret(portMAX_DELAY);
    static DemandeSMS demande;   // grande structure : hors de la pile
    for (;;) {
        if (xQueueReceive(fileDuModuleSMS, &demande, portMAX_DELAY) != pdPASS) continue;
        if (demande.travail == TravailSMS::ENVOYER) envoyerLeSMS(demande);
        else traiterUnNouveauSMS(demande.numeroDansLaSim);
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
    String reponse;
    const String commande = String("AT+CMGL=\"") + convertirTexteEnUcs2Hexadecimal("ALL") + "\"";
    if (envoyerCommandeAT(commande, &reponse, 15000) != ResultatAT::OK) return false;
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

bool supprimerUnSMS(int numeroDansLaSim) {
    return envoyerCommandeAT(String("AT+CMGD=") + numeroDansLaSim, nullptr, 5000) == ResultatAT::OK;
}

bool demarrerModuleSMS() {
    fileDuModuleSMS = xQueueCreate(4, sizeof(DemandeSMS));
    if (fileDuModuleSMS == nullptr) return false;
    ecouterLesNotificationsDuModem("+CMTI:", quandUnSMSArrive);
    return creerTacheSurveillee(tacheSMS, "SMS", 6144, PRIORITE_NORMALE, COEUR_COMMUNICATIONS);
}
