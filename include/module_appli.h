#ifndef CONFIG_APPLI_H
#define CONFIG_APPLI_H

#include "main.h"

/* ──────────────────────────────────────────────────────────────────────────────
   Module « Appli » : liaison Bluetooth Low Energy avec l'application Android.

   Ce module remplace module_bluetooth.cpp (un seul serveur BLE possible sur l'ESP32).
   Il offre, dans un seul service GATT protégé par code PIN :
     - les données en direct (GPS, batterie, 4G, bus CAN, accéléromètre, système) ;
     - les commandes (photo, position GPS, redémarrage...) et leurs réponses ;
     - les logs en direct ;
     - la mise à jour du firmware (OTA) en Bluetooth.

   Le protocole complet est décrit dans docs/protocole_ble.md.
   ────────────────────────────────────────────────────────────────────────────── */

// Version affichée dans l'application. À incrémenter à chaque nouveau firmware.
#define APPLI_VERSION_FIRMWARE "0.1.0"

// Nom diffusé en Bluetooth (celui que l'application recherche).
#define APPLI_NOM_BLE "Moto_ESP32"

// UUID du service et des caractéristiques (doivent être identiques côté Android).
#define APPLI_UUID_SERVICE     "7a3b0001-5c1e-4b8a-9f2d-6d6f746f3031"
#define APPLI_UUID_TELEMETRIE  "7a3b0002-5c1e-4b8a-9f2d-6d6f746f3031"  // NOTIFY : données en direct (JSON)
#define APPLI_UUID_COMMANDE    "7a3b0003-5c1e-4b8a-9f2d-6d6f746f3031"  // WRITE  : commande texte
#define APPLI_UUID_REPONSE     "7a3b0004-5c1e-4b8a-9f2d-6d6f746f3031"  // NOTIFY : réponse aux commandes (JSON)
#define APPLI_UUID_LOGS        "7a3b0005-5c1e-4b8a-9f2d-6d6f746f3031"  // NOTIFY : lignes de log
#define APPLI_UUID_OTA_CTRL    "7a3b0006-5c1e-4b8a-9f2d-6d6f746f3031"  // WRITE + NOTIFY : pilotage de la mise à jour
#define APPLI_UUID_OTA_DATA    "7a3b0007-5c1e-4b8a-9f2d-6d6f746f3031"  // WRITE  : morceaux du firmware

// Initialise le BLE, le service de l'application et lance la tâche d'envoi des données.
bool initialiserAppliBLE();

// À appeler pour chaque ligne de log (fait par my_log_printf). Non bloquant, sans effet si personne n'est connecté.
void appliBLE_transmettreLog(const char* ligne);

// true si un téléphone est connecté et authentifié.
bool appliBLE_connectee();

// Tâche FreeRTOS lancée par initialiserAppliBLE() (cœur réseau).
void tacheAppliBLE(void * pvParameters);

#endif // CONFIG_APPLI_H
