#include "configuration.h"
#include "module_modem.h"
#include "module_journal.h"
#include "module_systeme.h"

namespace {

HardwareSerial      liaisonModem(1);
SemaphoreHandle_t   verrouDuModem      = nullptr;
EventGroupHandle_t  evenementsDuModem  = nullptr;
const EventBits_t   BIT_MODEM_PRET     = BIT0;

volatile bool       modemTrouve        = false;
String              ligneEnCoursDeLecture;
IdentiteDuModem     identite = { "", "", "", false };

struct Abonnement {
    const char*                     debutDeLigne;
    FonctionAppeleeSurNotification  fonction;
};
const int  NOMBRE_MAXIMUM_D_ABONNEMENTS = 20;
Abonnement abonnements[NOMBRE_MAXIMUM_D_ABONNEMENTS];
int        nombreDAbonnements = 0;

// ─────────────────────────────── Lecture des lignes ───────────────────────────────

// Lit les caractères disponibles. Renvoie true quand une ligne complète est prête.
bool lireUneLigne(String& ligne, uint32_t attenteMaximumMs) {
    const uint32_t debut = millis();
    do {
        while (liaisonModem.available()) {
            const char caractere = (char)liaisonModem.read();
            if (caractere == '\r') continue;
            if (caractere == '\n') {
                ligne = ligneEnCoursDeLecture;
                ligneEnCoursDeLecture = "";
                return true;
            }
            ligneEnCoursDeLecture += caractere;
        }
        if (attenteMaximumMs > 0) vTaskDelay(pdMS_TO_TICKS(5));
    } while (millis() - debut < attenteMaximumMs);
    return false;
}

// Renvoie true si la ligne est une notification connue, et la transmet aux modules abonnés.
bool distribuerSiCEstUneNotification(const String& ligne) {
    bool estUneNotification = false;
    for (int numero = 0; numero < nombreDAbonnements; numero++) {
        if (ligne.startsWith(abonnements[numero].debutDeLigne)) {
            abonnements[numero].fonction(ligne);
            estUneNotification = true;
        }
    }
    return estUneNotification;
}

void traiterLesLignesEnAttente() {
    String ligne;
    while (lireUneLigne(ligne, 0)) {
        if (!ligne.isEmpty()) distribuerSiCEstUneNotification(ligne);
    }
}

bool estUneFinDeReponse(const String& ligne, ResultatAT& resultat) {
    if (ligne == "OK") { resultat = ResultatAT::OK; return true; }
    if (ligne == "ERROR" || ligne.startsWith("+CME ERROR") || ligne.startsWith("+CMS ERROR")) {
        resultat = ResultatAT::ERREUR;
        return true;
    }
    return false;
}

ResultatAT attendreLaReponse(const String& commande, String* reponse, uint32_t delaiMaximumMs) {
    const uint32_t debut = millis();
    String ligne;
    while (millis() - debut < delaiMaximumMs) {
        if (!lireUneLigne(ligne, 20)) continue;
        if (ligne.isEmpty() || ligne == commande) continue;    // ligne vide ou écho de la commande
        ResultatAT resultat;
        if (estUneFinDeReponse(ligne, resultat)) {
            if (resultat == ResultatAT::ERREUR && reponse != nullptr) *reponse += ligne;
            return resultat;
        }
        if (distribuerSiCEstUneNotification(ligne)) continue;
        if (reponse != nullptr) {
            *reponse += ligne;
            *reponse += '\n';
        }
    }
    return ResultatAT::DELAI_DEPASSE;
}

ResultatAT envoyerSansVerifierQueLeModemEstPret(const String& commande, String* reponse, uint32_t delaiMaximumMs) {
    if (!prendreLeModem(delaiMaximumMs + 5000)) return ResultatAT::DELAI_DEPASSE;
    traiterLesLignesEnAttente();
    if (reponse != nullptr) *reponse = "";
    liaisonModem.print(commande);
    liaisonModem.print("\r");
    const ResultatAT resultat = attendreLaReponse(commande, reponse, delaiMaximumMs);
    rendreLeModem();
    return resultat;
}

// ─────────────────────────────── Démarrage du modem ───────────────────────────────

bool leModemRepondSur(int brocheReception, int brocheEmission) {
    liaisonModem.end();
    liaisonModem.begin(VITESSE_UART_MODEM, SERIAL_8N1, brocheReception, brocheEmission);
    for (int essai = 0; essai < 3; essai++) {
        if (envoyerSansVerifierQueLeModemEstPret("AT", nullptr, 500) == ResultatAT::OK) return true;
    }
    return false;
}

void appuyerSurLeBoutonDAllumageDuModem() {
    if (BROCHE_ALLUMAGE_MODEM < 0) return;
    pinMode(BROCHE_ALLUMAGE_MODEM, OUTPUT);
    digitalWrite(BROCHE_ALLUMAGE_MODEM, HIGH);
    vTaskDelay(pdMS_TO_TICKS(1000));
    digitalWrite(BROCHE_ALLUMAGE_MODEM, LOW);
}

void chercherLeModemJusquAuSucces() {
    uint32_t nombreDEssais = 0;
    for (;;) {
        // Le sens des fils n'est pas documenté par Waveshare : on essaie les deux.
        if (leModemRepondSur(BROCHE_MODEM_A, BROCHE_MODEM_B)) {
            ecrireDansLeJournal("[MODEM] Le modem répond (réception GPIO %d, émission GPIO %d).", BROCHE_MODEM_A, BROCHE_MODEM_B);
            return;
        }
        if (leModemRepondSur(BROCHE_MODEM_B, BROCHE_MODEM_A)) {
            ecrireDansLeJournal("[MODEM] Le modem répond (réception GPIO %d, émission GPIO %d).", BROCHE_MODEM_B, BROCHE_MODEM_A);
            return;
        }
        if (nombreDEssais % 6 == 0) {
            ecrireDansLeJournal("[MODEM] Pas de réponse du modem. Interrupteur DIP « 4G » sur ON ? Nouvel essai...");
        }
        if (nombreDEssais == 2) appuyerSurLeBoutonDAllumageDuModem();
        nombreDEssais++;
        vTaskDelay(pdMS_TO_TICKS(5 * UNE_SECONDE_MS));
    }
}

String premiereLigne(const String& texte) {
    String ligne = texte.substring(0, texte.indexOf('\n') >= 0 ? texte.indexOf('\n') : texte.length());
    ligne.trim();
    return ligne;
}

void lireLIdentiteDuModem() {
    String reponse;
    if (envoyerSansVerifierQueLeModemEstPret("AT+CGMM", &reponse, 2000) == ResultatAT::OK) identite.modele = premiereLigne(reponse);
    if (envoyerSansVerifierQueLeModemEstPret("AT+CGMR", &reponse, 2000) == ResultatAT::OK) {
        identite.versionLogiciel = premiereLigne(reponse);
        identite.versionLogiciel.replace("+CGMR: ", "");
    }
    if (envoyerSansVerifierQueLeModemEstPret("AT+CGSN", &reponse, 2000) == ResultatAT::OK) identite.imei = premiereLigne(reponse);
    // Les variantes sans voix refusent les commandes audio.
    identite.voixDisponible = envoyerSansVerifierQueLeModemEstPret("AT+CLVL?", nullptr, 2000) == ResultatAT::OK;
    ecrireDansLeJournal("[MODEM] Modèle %s, logiciel %s. Voix et haut-parleur : %s.", identite.modele.c_str(),
                        identite.versionLogiciel.c_str(), identite.voixDisponible ? "disponibles" : "NON disponibles sur ce modèle");
}

void attendreLaCarteSIM() {
    for (int essai = 0; essai < 20; essai++) {
        String reponse;
        envoyerSansVerifierQueLeModemEstPret("AT+CPIN?", &reponse, 2000);
        if (reponse.indexOf("READY") >= 0) return;
        if (reponse.indexOf("SIM PIN") >= 0) {
            ecrireDansLeJournal("[MODEM] La carte SIM demande un code PIN : le désactiver avec un téléphone.");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ecrireDansLeJournal("[MODEM] Carte SIM absente ou illisible.");
}

void reglerLeModem() {
    envoyerSansVerifierQueLeModemEstPret("ATE0", nullptr, 1000);                    // pas d'écho des commandes
    envoyerSansVerifierQueLeModemEstPret("AT+CMEE=2", nullptr, 1000);               // erreurs en texte clair
    lireLIdentiteDuModem();
    attendreLaCarteSIM();
    envoyerSansVerifierQueLeModemEstPret("AT+CMGF=1", nullptr, 1000);               // SMS en mode texte
    envoyerSansVerifierQueLeModemEstPret("AT+CSCS=\"UCS2\"", nullptr, 1000);        // textes en UCS2 (accents)
    envoyerSansVerifierQueLeModemEstPret("AT+CSMP=17,167,0,8", nullptr, 1000);      // SMS envoyés en UCS2
    envoyerSansVerifierQueLeModemEstPret("AT+CNMI=2,1,0,0,0", nullptr, 1000);       // prévenir à l'arrivée d'un SMS (+CMTI)
    envoyerSansVerifierQueLeModemEstPret("AT+CLIP=1", nullptr, 1000);               // numéro de l'appelant (+CLIP)
}

void tacheModem(void*) {
    chercherLeModemJusquAuSucces();
    reglerLeModem();
    modemTrouve = true;
    xEventGroupSetBits(evenementsDuModem, BIT_MODEM_PRET);
    ecrireDansLeJournal("[MODEM] Prêt.");

    for (;;) {
        // Quand aucune commande n'est en cours, on lit les notifications spontanées du modem.
        if (liaisonModem.available() && prendreLeModem(0)) {
            traiterLesLignesEnAttente();
            rendreLeModem();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

} // namespace

// ─────────────────────────────── API publique ───────────────────────────────

bool prendreLeModem(uint32_t attenteMaximumMs) {
    return xSemaphoreTakeRecursive(verrouDuModem, pdMS_TO_TICKS(attenteMaximumMs)) == pdTRUE;
}

void rendreLeModem() { xSemaphoreGiveRecursive(verrouDuModem); }

bool leModemEstPret() { return modemTrouve; }

bool attendreQueLeModemSoitPret(TickType_t attenteMaximum) {
    if (evenementsDuModem == nullptr) return false;
    return (xEventGroupWaitBits(evenementsDuModem, BIT_MODEM_PRET, pdFALSE, pdTRUE, attenteMaximum) & BIT_MODEM_PRET) != 0;
}

ResultatAT envoyerCommandeAT(const String& commande, String* reponse, uint32_t delaiMaximumMs) {
    if (!modemTrouve) return ResultatAT::MODEM_ABSENT;
    return envoyerSansVerifierQueLeModemEstPret(commande, reponse, delaiMaximumMs);
}

ResultatAT envoyerCommandeAT(const char* commande, String* reponse, uint32_t delaiMaximumMs) {
    return envoyerCommandeAT(String(commande), reponse, delaiMaximumMs);
}

ResultatAT envoyerCommandeATAvecDonnees(const String& commande, const uint8_t* donnees, size_t taille,
                                        bool terminerParCtrlZ, String* reponse, uint32_t delaiMaximumMs) {
    if (!modemTrouve) return ResultatAT::MODEM_ABSENT;
    if (!prendreLeModem(delaiMaximumMs)) return ResultatAT::DELAI_DEPASSE;
    traiterLesLignesEnAttente();
    if (reponse != nullptr) *reponse = "";
    liaisonModem.print(commande);
    liaisonModem.print("\r");

    // Attente de l'invite « > » (elle n'est pas suivie d'un retour à la ligne).
    bool inviteRecue = false;
    const uint32_t debut = millis();
    while (!inviteRecue && millis() - debut < 5000) {
        while (liaisonModem.available()) {
            const char caractere = (char)liaisonModem.read();
            if (caractere == '>') { inviteRecue = true; break; }
            if (caractere == '\n') {
                ResultatAT resultat;
                String ligne = ligneEnCoursDeLecture;
                ligneEnCoursDeLecture = "";
                if (estUneFinDeReponse(ligne, resultat) && resultat == ResultatAT::ERREUR) {
                    rendreLeModem();
                    return ResultatAT::ERREUR;
                }
                if (!ligne.isEmpty()) distribuerSiCEstUneNotification(ligne);
            } else if (caractere != '\r') {
                ligneEnCoursDeLecture += caractere;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (!inviteRecue) {
        liaisonModem.write((uint8_t)0x1B);   // Échap : annule la saisie
        rendreLeModem();
        return ResultatAT::DELAI_DEPASSE;
    }
    ligneEnCoursDeLecture = "";
    liaisonModem.write(donnees, taille);
    if (terminerParCtrlZ) liaisonModem.write((uint8_t)0x1A);
    const ResultatAT resultat = attendreLaReponse(commande, reponse, delaiMaximumMs);
    rendreLeModem();
    return resultat;
}

void ecouterLesNotificationsDuModem(const char* debutDeLigne, FonctionAppeleeSurNotification fonction) {
    if (nombreDAbonnements < NOMBRE_MAXIMUM_D_ABONNEMENTS) {
        abonnements[nombreDAbonnements++] = { debutDeLigne, fonction };
    }
}

const IdentiteDuModem& identiteDuModem() { return identite; }

bool demarrerModuleModem() {
    verrouDuModem     = xSemaphoreCreateRecursiveMutex();
    evenementsDuModem = xEventGroupCreate();
    if (verrouDuModem == nullptr || evenementsDuModem == nullptr) return false;
    liaisonModem.setRxBufferSize(4096);   // les listes de SMS peuvent être longues
    return creerTacheSurveillee(tacheModem, "Modem", 6144, PRIORITE_HAUTE, COEUR_COMMUNICATIONS);
}
