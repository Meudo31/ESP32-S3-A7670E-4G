#include "configuration.h"
#include "module_reseau_4g.h"
#include "module_modem.h"
#include "module_journal.h"
#include "module_systeme.h"
#include "outils_texte.h"

namespace {

EtatDuReseau4G    etatActuel = {};
SemaphoreHandle_t verrouDeLEtat = nullptr;
TaskHandle_t      tacheDuReseau = nullptr;

// Renvoie ce qui suit « prefixe » dans la réponse, jusqu'à la fin de la ligne.
String valeurApres(const String& reponse, const char* prefixe) {
    const int debut = reponse.indexOf(prefixe);
    if (debut < 0) return "";
    const int debutValeur = debut + strlen(prefixe);
    int fin = reponse.indexOf('\n', debutValeur);
    if (fin < 0) fin = reponse.length();
    String valeur = reponse.substring(debutValeur, fin);
    valeur.trim();
    return valeur;
}

String premiereLigne(const String& reponse) {
    int fin = reponse.indexOf('\n');
    String ligne = reponse.substring(0, fin < 0 ? reponse.length() : fin);
    ligne.trim();
    return ligne;
}

const char* nomDeLaTechnologie(int code) {
    switch (code) {
        case 0: case 1: case 3: return "GSM";
        case 2: case 4: case 5: case 6: return "WCDMA";
        case 7: return "LTE";
        default: return "?";
    }
}

void lireLaCarteSim(EtatDuReseau4G& etat) {
    String reponse;
    envoyerCommandeAT("AT+CPIN?", &reponse);
    etat.etatSim  = valeurApres(reponse, "+CPIN:");
    if (etat.etatSim.isEmpty()) etat.etatSim = "absente";
    etat.simPrete = etat.etatSim == "READY";
    if (!etat.simPrete) return;

    if (etat.iccid.isEmpty() && envoyerCommandeAT("AT+CICCID", &reponse) == ResultatAT::OK) etat.iccid = valeurApres(reponse, "+ICCID:");
    if (etat.imsi.isEmpty()  && envoyerCommandeAT("AT+CIMI", &reponse) == ResultatAT::OK)   etat.imsi  = premiereLigne(reponse);
    if (etat.numeroDeTelephone.isEmpty() && envoyerCommandeAT("AT+CNUM", &reponse) == ResultatAT::OK) {
        String champs[4];
        if (decouperLesChamps(valeurApres(reponse, "+CNUM:"), champs, 4) >= 2) etat.numeroDeTelephone = decoderSiCEstDeLUcs2(champs[1]);
    }
}

void lireLeSignal(EtatDuReseau4G& etat) {
    String reponse;
    if (envoyerCommandeAT("AT+CSQ", &reponse) != ResultatAT::OK) return;
    etat.qualiteBrute = valeurApres(reponse, "+CSQ:").toInt();
    if (etat.qualiteBrute >= 0 && etat.qualiteBrute <= 31) {
        etat.signalDbm      = -113 + 2 * etat.qualiteBrute;
        etat.signalPourcent = etat.qualiteBrute * 100 / 31;
    } else {
        etat.signalDbm      = 0;
        etat.signalPourcent = 0;
    }
}

void lireLInscriptionSurLeReseau(EtatDuReseau4G& etat) {
    String reponse;
    String champs[6];
    if (envoyerCommandeAT("AT+CEREG?", &reponse) == ResultatAT::OK
        && decouperLesChamps(valeurApres(reponse, "+CEREG:"), champs, 6) >= 2) {
        etat.enregistrement = champs[1].toInt();
    }
    if (etat.enregistrement != 1 && etat.enregistrement != 5
        && envoyerCommandeAT("AT+CREG?", &reponse) == ResultatAT::OK
        && decouperLesChamps(valeurApres(reponse, "+CREG:"), champs, 6) >= 2) {
        etat.enregistrement = champs[1].toInt();   // inscription 2G si pas de 4G
    }
    if (envoyerCommandeAT("AT+COPS?", &reponse) == ResultatAT::OK) {
        const int nombre = decouperLesChamps(valeurApres(reponse, "+COPS:"), champs, 6);
        etat.operateur   = nombre >= 3 ? decoderSiCEstDeLUcs2(champs[2]) : "";
        etat.technologie = nombre >= 4 ? nomDeLaTechnologie(champs[3].toInt()) : "";
    }
}

// +CPSI: LTE,Online,208-01,0x1A2B,12345678,123,EUTRAN-BAND20,6300,5,5,-94,-1048,-750,13
void lireLaCellule(EtatDuReseau4G& etat) {
    String reponse;
    if (envoyerCommandeAT("AT+CPSI?", &reponse) != ResultatAT::OK) return;
    String champs[16];
    const int nombre = decouperLesChamps(valeurApres(reponse, "+CPSI:"), champs, 16);
    if (nombre < 3) return;
    if (!champs[0].isEmpty() && champs[0] != "NO SERVICE") etat.technologie = champs[0];
    etat.codePays = champs[2];
    if (champs[0] == "LTE" && nombre >= 14) {
        etat.zone     = champs[3];
        etat.cellule  = champs[4];
        etat.bande    = champs[6];
        etat.canal    = champs[7].toInt();
        etat.rsrqDb   = champs[10].toFloat() / 10.0f;
        etat.rsrpDbm  = champs[11].toFloat() / 10.0f;
        etat.rssiDbm  = champs[12].toFloat() / 10.0f;
        etat.snrDb    = champs[13].toFloat();
    } else if (nombre >= 5) {
        etat.zone     = champs[3];
        etat.cellule  = champs[4];
    }
}

void lireLaConnexionDeDonnees(EtatDuReseau4G& etat) {
    String reponse;
    String champs[6];
    if (envoyerCommandeAT("AT+CGPADDR=1", &reponse) == ResultatAT::OK
        && decouperLesChamps(valeurApres(reponse, "+CGPADDR:"), champs, 6) >= 2) {
        etat.adresseIP = champs[1];
    }
    if (etat.pointDAcces.isEmpty() && envoyerCommandeAT("AT+CGDCONT?", &reponse) == ResultatAT::OK
        && decouperLesChamps(valeurApres(reponse, "+CGDCONT:"), champs, 6) >= 3) {
        etat.pointDAcces = decoderSiCEstDeLUcs2(champs[2]);
    }
}

void lireLaTensionVueParLeModem(EtatDuReseau4G& etat) {
    String reponse;
    if (envoyerCommandeAT("AT+CBC", &reponse) == ResultatAT::OK) {
        // Selon la version du logiciel : "+CBC: 3.950V" ou "+CBC: 0,80,3950"
        String valeur = valeurApres(reponse, "+CBC:");
        if (valeur.endsWith("V")) etat.tensionVueParLeModem = valeur.toFloat();
        else {
            String champs[3];
            if (decouperLesChamps(valeur, champs, 3) == 3) etat.tensionVueParLeModem = champs[2].toFloat() / 1000.0f;
        }
    }
}

void tacheReseau4G(void*) {
    attendreQueLeModemSoitPret(portMAX_DELAY);
    int ancienEnregistrement = -1;
    for (;;) {
        EtatDuReseau4G nouvelEtat = etatDuReseau4G();
        lireLaCarteSim(nouvelEtat);
        lireLeSignal(nouvelEtat);
        lireLInscriptionSurLeReseau(nouvelEtat);
        lireLaCellule(nouvelEtat);
        lireLaConnexionDeDonnees(nouvelEtat);
        lireLaTensionVueParLeModem(nouvelEtat);
        nouvelEtat.miseAJourMs = millis();

        if (nouvelEtat.enregistrement != ancienEnregistrement) {
            ancienEnregistrement = nouvelEtat.enregistrement;
            ecrireDansLeJournal("[RESEAU] Inscription : %d (%s %s, signal %d %%).", nouvelEtat.enregistrement,
                                nouvelEtat.operateur.c_str(), nouvelEtat.technologie.c_str(), nouvelEtat.signalPourcent);
        }

        xSemaphoreTake(verrouDeLEtat, portMAX_DELAY);
        etatActuel = nouvelEtat;
        xSemaphoreGive(verrouDeLEtat);

        // Attente de 10 s, écourtée si quelqu'un demande une actualisation.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10 * UNE_SECONDE_MS));
    }
}

} // namespace

EtatDuReseau4G etatDuReseau4G() {
    xSemaphoreTake(verrouDeLEtat, portMAX_DELAY);
    EtatDuReseau4G copie = etatActuel;
    xSemaphoreGive(verrouDeLEtat);
    return copie;
}

void actualiserLeReseau4G() {
    if (tacheDuReseau != nullptr) xTaskNotifyGive(tacheDuReseau);
}

void decrireLeReseau4G(JsonDocument& document) {
    const EtatDuReseau4G etat = etatDuReseau4G();
    document["t"]       = "4g";
    document["pret"]    = leModemEstPret() ? 1 : 0;
    document["inscr"]   = etat.enregistrement;
    document["op"]      = etat.operateur;
    document["techno"]  = etat.technologie;
    document["csq"]     = etat.qualiteBrute;
    document["dbm"]     = etat.signalDbm;
    document["sig"]     = etat.signalPourcent;
    document["mccmnc"]  = etat.codePays;
    document["tac"]     = etat.zone;
    document["cell"]    = etat.cellule;
    document["bande"]   = etat.bande;
    document["earfcn"]  = etat.canal;
    document["rsrp"]    = etat.rsrpDbm;
    document["rsrq"]    = etat.rsrqDb;
    document["rssi"]    = etat.rssiDbm;
    document["snr"]     = etat.snrDb;
    document["ip"]      = etat.adresseIP;
    document["apn"]     = etat.pointDAcces;
}

void decrireLaCarteSIMEtLeModem(JsonDocument& document) {
    const EtatDuReseau4G etat = etatDuReseau4G();
    const IdentiteDuModem& modem = identiteDuModem();
    document["t"]        = "sim";
    document["modele"]   = modem.modele;
    document["logiciel"] = modem.versionLogiciel;
    document["imei"]     = modem.imei;
    document["voix"]     = modem.voixDisponible ? 1 : 0;
    document["etat"]     = etat.etatSim;
    document["iccid"]    = etat.iccid;
    document["imsi"]     = etat.imsi;
    document["num"]      = etat.numeroDeTelephone;
    document["vmodem"]   = etat.tensionVueParLeModem;
}

bool demarrerModuleReseau4G() {
    verrouDeLEtat = xSemaphoreCreateMutex();
    if (verrouDeLEtat == nullptr) return false;
    return creerTacheSurveillee(tacheReseau4G, "Reseau 4G", 6144, PRIORITE_BASSE, COEUR_COMMUNICATIONS, &tacheDuReseau);
}
