#include <Preferences.h>

#include "configuration.h"
#include "module_alertes_sms.h"
#include "module_appels.h"
#include "module_appli.h"
#include "module_haut_parleur.h"
#include "module_journal.h"
#include "module_modem.h"
#include "module_systeme.h"
#include "outils_texte.h"

namespace {

enum class ActionDAppel : uint8_t { APPELER, DECROCHER, RACCROCHER, LE_MODEM_A_SIGNALE_QUELQUE_CHOSE,
                                   REGLER_LE_MICRO, TOUCHE_DTMF, TESTER_LE_MICRO };

struct DemandeDAppel {
    ActionDAppel action;
    char         numero[24];
};

QueueHandle_t     fileDesAppels  = nullptr;
SemaphoreHandle_t verrouDeLAppel = nullptr;
AppelEnCours      appelActuel    = { EtatDUnAppel::AUCUN, "", 0 };
String            numeroDeLAppelant;   // donné par +CLIP, avant même que l'état soit vérifié

// Réglages du micro (mémorisés en flash, appliqués au démarrage du modem et à chaque appel)
volatile int  gainDuMicro          = GAIN_DU_MICRO_PAR_DEFAUT;   // AT+CMICGAIN : 0 (faible) à 8 (fort)
volatile bool microCoupe           = false;                      // AT+CMUT : le correspondant ne nous entend plus
volatile bool decrochageAutomatique = false;                     // appels du numéro des alertes décrochés seuls
uint32_t      debutDeLaSonnerieMs  = 0;
bool          decrochageDejaDemande = false;

void appliquerLesReglagesDuMicro();   // plus bas

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

    if (nouvelEtat == EtatDUnAppel::SONNERIE) {
        debutDeLaSonnerieMs   = millis();
        decrochageDejaDemande = false;
        demarrerLaSonnerie();
    }
    // Le modem remet parfois le micro à zéro entre deux appels : on réapplique les réglages.
    if (nouvelEtat == EtatDUnAppel::EN_COMMUNICATION) appliquerLesReglagesDuMicro();
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

String nettoyerLaReponse(String reponse) {
    reponse.replace("\r", " "); reponse.replace("\n", " "); reponse.replace("OK", ""); reponse.trim();
    return reponse;
}

// Le micro de la carte (MEMS, à côté du connecteur) est câblé sur l'entrée MIC_P/MIC_N du modem :
// l'ESP32 n'y a pas accès, tout se règle par commandes AT.
void appliquerLesReglagesDuMicro() {
    const bool gainAccepte = envoyerCommandeAT(String("AT+CMICGAIN=") + gainDuMicro, nullptr, 2000) == ResultatAT::OK;
    const bool muetAccepte = envoyerCommandeAT(String("AT+CMUT=") + (microCoupe ? 1 : 0), nullptr, 2000) == ResultatAT::OK;
    ecrireDansLeJournal("[APPELS] Micro : gain %d (%s), %s (%s).", gainDuMicro, gainAccepte ? "OK" : "refusé",
                        microCoupe ? "coupé" : "ouvert", muetAccepte ? "OK" : "refusé");
}

// Diagnostic : ce que le modem dit de son micro et de sa voie audio, renvoyé à l'application (MICRO_TEST).
void testerLeMicro() {
    String compteRendu;
    const char* questions[][2] = {
        { "AT+CSDVC?",   "Voie audio" },
        { "AT+CMICGAIN?", "Gain micro" },
        { "AT+CMUT?",    "Micro coupé" },
        { "AT+CLVL?",    "Volume" },
    };
    bool auMoinsUneReponse = false;
    for (const auto& question : questions) {
        String reponse;
        const bool ok = envoyerCommandeAT(question[0], &reponse, 2000) == ResultatAT::OK;
        auMoinsUneReponse |= ok;
        compteRendu += String(question[1]) + " : " + (ok ? nettoyerLaReponse(reponse) : String("refusé")) + ". ";
    }
    ecrireDansLeJournal("[APPELS] Test du micro : %s", compteRendu.c_str());
    repondreALAppli("MICRO_TEST", auMoinsUneReponse, compteRendu.c_str());
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
        case ActionDAppel::REGLER_LE_MICRO:
            appliquerLesReglagesDuMicro();
            publierLAppelVersLAppli();
            break;
        case ActionDAppel::TESTER_LE_MICRO:
            testerLeMicro();
            break;
        case ActionDAppel::TOUCHE_DTMF:
            // Touche envoyée au correspondant pendant l'appel (serveur vocal, messagerie…)
            if (envoyerCommandeAT(String("AT+VTS=") + demande.numero, nullptr, 3000) != ResultatAT::OK) {
                ecrireDansLeJournal("[APPELS] Le modem refuse la touche %s.", demande.numero);
            }
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

// Mêmes 9 derniers chiffres : « +33 6 12… » et « 06 12… » sont le même numéro.
bool cEstLeNumeroDesAlertes(const String& numero) {
    auto fin = [](const String& brut) {
        String chiffres;
        for (size_t i = 0; i < brut.length(); i++) if (isDigit(brut[i])) chiffres += brut[i];
        return chiffres.length() > 9 ? chiffres.substring(chiffres.length() - 9) : chiffres;
    };
    const String alertes = numeroDesAlertes();
    return alertes.length() >= 4 && numero.length() >= 4 && fin(numero) == fin(alertes);
}

// Décrochage automatique : seulement pour le numéro des alertes, après quelques sonneries.
void decrocherSeulSiCEstLeNumeroDesAlertes() {
    if (!decrochageAutomatique || decrochageDejaDemande) return;
    const AppelEnCours appel = appelEnCours();
    if (appel.etat != EtatDUnAppel::SONNERIE) return;
    if (millis() - debutDeLaSonnerieMs < DELAI_AVANT_DECROCHAGE_AUTOMATIQUE_MS) return;
    if (!cEstLeNumeroDesAlertes(appel.numero)) return;
    decrochageDejaDemande = true;
    ecrireDansLeJournal("[APPELS] Décrochage automatique (numéro des alertes).");
    if (envoyerCommandeAT("ATA", nullptr, 10000) == ResultatAT::OK) changerDEtat(EtatDUnAppel::EN_COMMUNICATION, "");
}

void tacheAppels(void*) {
    attendreQueLeModemSoitPret(portMAX_DELAY);
    appliquerLesReglagesDuMicro();
    DemandeDAppel demande;
    for (;;) {
        const TickType_t attente = appelEnCours().etat == EtatDUnAppel::SONNERIE ? pdMS_TO_TICKS(500) : pdMS_TO_TICKS(2000);
        if (xQueueReceive(fileDesAppels, &demande, attente) == pdPASS) {
            executer(demande);
        } else if (appelEnCours().etat != EtatDUnAppel::AUCUN) {
            verifierLEtatAupresDuModem();   // sécurité : une notification a pu être manquée
        }
        decrocherSeulSiCEstLeNumeroDesAlertes();
    }
}

void memoriser(const char* cle, int valeur) {
    Preferences memoire;
    memoire.begin("appels", false);
    memoire.putInt(cle, valeur);
    memoire.end();
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

bool rappelerLeNumeroDesAlertes() {
    const String numero = numeroDesAlertes();
    if (numero.length() < 4 || appelEnCours().etat != EtatDUnAppel::AUCUN) return false;
    return appeler(numero);
}

bool envoyerUneToucheDTMF(const String& touches) {
    if (appelEnCours().etat != EtatDUnAppel::EN_COMMUNICATION || touches.isEmpty()) return false;
    for (size_t i = 0; i < touches.length(); i++) {
        const char c = touches[i];
        if (!(isDigit(c) || c == '*' || c == '#' || (c >= 'A' && c <= 'D'))) return false;
    }
    envoyerLaDemande(ActionDAppel::TOUCHE_DTMF, touches);
    return true;
}

bool reglerLeGainDuMicro(int gain) {
    if (gain < 0 || gain > 8) return false;
    gainDuMicro = gain;
    memoriser("gain_micro", gain);
    envoyerLaDemande(ActionDAppel::REGLER_LE_MICRO);
    return true;
}
int gainDuMicroActuel() { return gainDuMicro; }

void couperLeMicro(bool coupe) {
    microCoupe = coupe;   // pas mémorisé : au redémarrage le micro est toujours ouvert
    envoyerLaDemande(ActionDAppel::REGLER_LE_MICRO);
}
bool leMicroEstCoupe() { return microCoupe; }

void activerLeDecrochageAutomatique(bool actif) {
    decrochageAutomatique = actif;
    memoriser("auto", actif ? 1 : 0);
    publierLAppelVersLAppli();
}
bool leDecrochageAutomatiqueEstActif() { return decrochageAutomatique; }

void testerLeMicroDuModule() { envoyerLaDemande(ActionDAppel::TESTER_LE_MICRO); }

void decrireLAppelEnCours(JsonDocument& document) {
    const AppelEnCours appel = appelEnCours();
    document["t"]     = "appel";
    document["etat"]  = (int)appel.etat;   // 0 aucun, 1 sonnerie, 2 numérotation, 3 en communication
    document["num"]   = appel.numero;
    document["duree"] = appel.etat == EtatDUnAppel::EN_COMMUNICATION ? (millis() - appel.debutMs) / 1000 : 0;
    document["micg"]  = gainDuMicro;                     // gain du micro 0..8
    document["muet"]  = microCoupe ? 1 : 0;
    document["auto"]  = decrochageAutomatique ? 1 : 0;   // décrochage automatique du numéro des alertes
}

bool demarrerModuleAppels() {
    Preferences memoire;
    memoire.begin("appels", true);
    gainDuMicro           = constrain(memoire.getInt("gain_micro", GAIN_DU_MICRO_PAR_DEFAUT), 0, 8);
    decrochageAutomatique = memoire.getInt("auto", 0) == 1;
    memoire.end();

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
