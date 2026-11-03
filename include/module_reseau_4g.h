#ifndef MODULE_RESEAU_4G_H
#define MODULE_RESEAU_4G_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE RÉSEAU 4G : toutes les informations sur la carte SIM et le réseau mobile.
   Sa tâche interroge le modem toutes les 10 secondes.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

struct EtatDuReseau4G {
    bool   simPrete;
    String etatSim;              // "READY", "SIM PIN", "absente"...
    String iccid;                // numéro de la carte SIM
    String imsi;
    String numeroDeTelephone;    // souvent vide : toutes les cartes SIM ne le connaissent pas
    int    enregistrement;       // 0 non inscrit, 1 inscrit, 2 recherche, 3 refusé, 5 itinérance
    String operateur;
    String technologie;          // "LTE", "GSM", "WCDMA"...
    int    qualiteBrute;         // AT+CSQ : 0..31 (99 = inconnu)
    int    signalDbm;
    int    signalPourcent;
    // Détails de la cellule (AT+CPSI?)
    String codePays;             // MCC-MNC, ex. "208-01"
    String zone;                 // TAC
    String cellule;              // identifiant de cellule
    String bande;                // ex. "EUTRAN-BAND20"
    int    canal;                // EARFCN
    float  rsrpDbm, rsrqDb, rssiDbm, snrDb;
    String adresseIP;
    String pointDAcces;          // APN
    float  tensionVueParLeModem; // AT+CBC (volts)
    uint32_t miseAJourMs;
};

EtatDuReseau4G etatDuReseau4G();

// Demande une mise à jour immédiate (au lieu d'attendre 10 s).
void actualiserLeReseau4G();

void decrireLeReseau4G(JsonDocument& document);           // signal, opérateur, cellule
void decrireLaCarteSIMEtLeModem(JsonDocument& document);  // identité de la SIM et du modem

bool demarrerModuleReseau4G();

#endif // MODULE_RESEAU_4G_H
