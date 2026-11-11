#include "configuration.h"
#include "module_appels.h"
#include "module_appli.h"
#include "module_haut_parleur.h"
#include "module_journal.h"
#include "module_modem.h"
#include "module_systeme.h"
#include "outils_texte.h"

namespace {

enum class ActionDAppel : uint8_t { APPELER, DECROCHER, RACCROCHER, LE_MODEM_A_SIGNALE_QUELQUE_CHOSE };

struct DemandeDAppel {
    ActionDAppel action;
    char         numero[24];
};

QueueHandle_t     fileDesAppels  = nullptr;
SemaphoreHandle_t verrouDeLAppel = nullptr;
AppelEnCours      appelActuel    = { EtatDUnAppel::AUCUN, "", 0 };
String            numeroDeLAppelant;   // donné par +CLIP, avant même que l'état soit vérifié

void envoyerLaDemande(ActionDAppel action, const String& numero = "") {
    DemandeDAppel demande = {};
    demande.action = action;
    strlcpy(demande.numero, numero.c_str(), sizeof(demande.numero));
    if (fileDesAppels != nullptr) xQueueSend(fileDesAppels, &demande, 0);
}

void publierLAppelVersLAppli() {
    JsonDocument evenement;
    decrireLAppelEnCours(evenement);
    publierUnEvenementVersLAppli(evenement);
}

void changerDEtat(EtatDUnAppel nouvelEtat, const String& numero) {
    xSemaphoreTake(verrouDeLAppel, portMAX_DELAY);
    const EtatDUnAppel ancienEtat = appelActuel.etat;
    const uint32_t dureeMs = millis() - appelActuel.debutMs;
    appelActuel.etat = nouvelEtat;
    if (!numero.isEmpty()) appelActuel.numero = numero;
    if (nouvelEtat == EtatDUnAppel::EN_COMMUNICATION && ancienEtat != EtatDUnAppel::EN_COMMUNICATION) appelActuel.debutMs = millis();
    const String numeroAffiche = appelActuel.numero;
    if (nouvelEtat == EtatDUnAppel::AUCUN) appelActuel.numero = "";
    xSemaphoreGive(verrouDeLAppel);

    if (ancienEtat == nouvelEtat) return;

    if (nouvelEtat == EtatDUnAppel::SONNERIE) demarrerLaSonnerie();
    if (ancienEtat == EtatDUnAppel::SONNERIE) arreterLaSonnerie();

    if (nouvelEtat == EtatDUnAppel::AUCUN && ancienEtat == EtatDUnAppel::EN_COMMUNICATION) {
        ecrireDansLeJournal("[APPELS] Appel terminé avec %s (%u s).", numeroAffiche.c_str(), (unsigned)(dureeMs / 1000));
    } else if (nouvelEtat == EtatDUnAppel::AUCUN && ancienEtat == EtatDUnAppel::SONNERIE) {
        ecrireDansLeJournal("[APPELS] Appel manqué de %s.", numeroAffiche.c_str());
    } else {
        ecrireDansLeJournal("[APPELS] %s %s.", nomDeLEtatDUnAppel(nouvelEtat), numeroAffiche.c_str());
    }
    publierLAppelVersLAppli();
}

// Demande au modem la liste des appels (AT+CLCC) pour connaître l'état exact.
// +CLCC: 1,1,4,0,0,"0612345678",129   (sens 1 = entrant ; état 4 = sonne, 0 = en communication, 2/3 = numérotation)
void verifierLEtatAupresDuModem() {
    String reponse;
    if (envoyerCommandeAT("AT+CLCC", &reponse, 2000) != ResultatAT::OK) return;
    const int debut = reponse.indexOf("+CLCC:");
    if (debut < 0) {
        changerDEtat(EtatDUnAppel::AUCUN, "");
        return;
    }
    int fin = reponse.indexOf('\n', debut);
    if (fin < 0) fin = reponse.length();
    String champs[8];
    const int nombre = decouperLesChamps(reponse.substring(debut + 6, fin), champs, 8);
    if (nombre < 3) return;
    const String numero = nombre >= 6 ? decoderSiCEstDeLUcs2(champs[5]) : "";
    switch (champs[2].toInt()) {
        case 0: case 1: changerDEtat(EtatDUnAppel::EN_COMMUNICATION, numero); break;
        case 2: case 3: changerDEtat(EtatDUnAppel::NUMEROTATION, numero);     break;
        case 4: case 5: changerDEtat(EtatDUnAppel::SONNERIE, numero);         break;
    }
}

void executer(const DemandeDAppel& demande) {
    switch (demande.action) {
        case ActionDAppel::APPELER: {
            String numero(demande.numero);
            if (envoyerCommandeAT(String("ATD") + numero + ";", nullptr, 10000) == ResultatAT::OK) {
                changerDEtat(EtatDUnAppel::NUMEROTATION, numero);
            } else {
                ecrireDansLeJournal("[APPELS] Le modem refuse d'appeler %s.", demande.numero);
            }
            break;
        }
        case ActionDAppel::DECROCHER:
            if (envoyerCommandeAT("ATA", nullptr, 10000) == ResultatAT::OK) changerDEtat(EtatDUnAppel::EN_COMMUNICATION, "");
            break;
        case ActionDAppel::RACCROCHER:
            envoyerCommandeAT("AT+CHUP", nullptr, 5000);
            changerDEtat(EtatDUnAppel::AUCUN, "");
            break;
        case ActionDAppel::LE_MODEM_A_SIGNALE_QUELQUE_CHOSE:
        {
            xSemaphoreTake(verrouDeLAppel, portMAX_DELAY);
            const String numeroAnnonce = numeroDeLAppelant;
            numeroDeLAppelant = "";
            xSemaphoreGive(verrouDeLAppel);
            if (!numeroAnnonce.isEmpty()) changerDEtat(EtatDUnAppel::SONNERIE, numeroAnnonce);
            verifierLEtatAupresDuModem();
        }
            break;
    }
}

// ─── Notifications du modem (ne pas envoyer de commande AT ici) ───

// +CLIP: "0612345678",129,,,,0
void quandLeNumeroDeLAppelantArrive(const String& ligne) {
    String champs[6];
    if (verrouDeLAppel != nullptr && decouperLesChamps(ligne.substring(6), champs, 6) >= 1) {
        xSemaphoreTake(verrouDeLAppel, portMAX_DELAY);
        numeroDeLAppelant = decoderSiCEstDeLUcs2(champs[0]);
        xSemaphoreGive(verrouDeLAppel);
    }
    envoyerLaDemande(ActionDAppel::LE_MODEM_A_SIGNALE_QUELQUE_CHOSE);
}

void quandLeModemSignaleUnChangement(const String&) {
    envoyerLaDemande(ActionDAppel::LE_MODEM_A_SIGNALE_QUELQUE_CHOSE);
}

void tacheAppels(void*) {
    attendreQueLeModemSoitPret(portMAX_DELAY);
    DemandeDAppel demande;
    for (;;) {
        if (xQueueReceive(fileDesAppels, &demande, pdMS_TO_TICKS(2000)) == pdPASS) {
            executer(demande);
        } else if (appelEnCours().etat != EtatDUnAppel::AUCUN) {
            verifierLEtatAupresDuModem();   // sécurité : une notification a pu être manquée
        }
    }
}

} // namespace

AppelEnCours appelEnCours() {
    if (verrouDeLAppel == nullptr) return appelActuel;   // module pas encore démarré
    xSemaphoreTake(verrouDeLAppel, portMAX_DELAY);
    AppelEnCours copie = appelActuel;
    xSemaphoreGive(verrouDeLAppel);
    return copie;
}

const char* nomDeLEtatDUnAppel(EtatDUnAppel etat) {
    switch (etat) {
        case EtatDUnAppel::SONNERIE:         return "Appel entrant";
        case EtatDUnAppel::NUMEROTATION:     return "Appel en cours vers";
        case EtatDUnAppel::EN_COMMUNICATION: return "En communication avec";
        default:                             return "Aucun appel";
    }
}

bool appeler(const String& numero) {
    if (numero.isEmpty() || !identiteDuModem().voixDisponible) return false;
    envoyerLaDemande(ActionDAppel::APPELER, numero);
    return true;
}

bool decrocher() {
    if (appelEnCours().etat != EtatDUnAppel::SONNERIE) return false;
    envoyerLaDemande(ActionDAppel::DECROCHER);
    return true;
}

bool raccrocher() {
    envoyerLaDemande(ActionDAppel::RACCROCHER);
    return true;
}

void decrireLAppelEnCours(JsonDocument& document) {
    const AppelEnCours appel = appelEnCours();
    document["t"]     = "appel";
    document["etat"]  = (int)appel.etat;   // 0 aucun, 1 sonnerie, 2 numérotation, 3 en communication
    document["num"]   = appel.numero;
    document["duree"] = appel.etat == EtatDUnAppel::EN_COMMUNICATION ? (millis() - appel.debutMs) / 1000 : 0;
}

bool demarrerModuleAppels() {
    fileDesAppels  = xQueueCreate(6, sizeof(DemandeDAppel));
    verrouDeLAppel = xSemaphoreCreateMutex();
    if (fileDesAppels == nullptr || verrouDeLAppel == nullptr) return false;
    ecouterLesNotificationsDuModem("+CLIP:", quandLeNumeroDeLAppelantArrive);
    ecouterLesNotificationsDuModem("RING", quandLeModemSignaleUnChangement);
    ecouterLesNotificationsDuModem("VOICE CALL:", quandLeModemSignaleUnChangement);
    ecouterLesNotificationsDuModem("NO CARRIER", quandLeModemSignaleUnChangement);
    ecouterLesNotificationsDuModem("BUSY", quandLeModemSignaleUnChangement);
    ecouterLesNotificationsDuModem("NO ANSWER", quandLeModemSignaleUnChangement);
    ecouterLesNotificationsDuModem("MISSED_CALL", quandLeModemSignaleUnChangement);
    return creerTacheSurveillee(tacheAppels, "Appels", 4096, PRIORITE_NORMALE, COEUR_COMMUNICATIONS);
}
