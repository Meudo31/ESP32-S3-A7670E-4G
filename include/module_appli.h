#ifndef MODULE_APPLI_H
#define MODULE_APPLI_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE APPLI : liaison Bluetooth Low Energy avec l'application Android.
   Un seul service, protégé par un code PIN (SECRET_BLE_PIN dans include/secrets.h).
   Le protocole complet est décrit dans docs/protocole_ble.md.

   Caractéristiques :
     Données      (notify)  messages JSON : états réguliers et événements
     Commande     (write)   commandes texte de l'application, ex. « PHOTO »
     Réponse      (notify)  réponse à chaque commande
     Journal      (notify)  lignes du journal en direct
     OTA contrôle (write + notify) et OTA données (write) : mise à jour du firmware
     Fichier      (notify)  contenu brut d'un fichier téléchargé

   La tâche de ce module exécute les commandes reçues et envoie les données
   régulières (GPS chaque seconde, batterie, réseau, système...).
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

#define NOM_BLUETOOTH_DU_MODULE "Moto_ESP32"

#define UUID_SERVICE_APPLI        "7a3b0001-5c1e-4b8a-9f2d-6d6f746f3031"
#define UUID_DONNEES              "7a3b0002-5c1e-4b8a-9f2d-6d6f746f3031"
#define UUID_COMMANDE             "7a3b0003-5c1e-4b8a-9f2d-6d6f746f3031"
#define UUID_REPONSE              "7a3b0004-5c1e-4b8a-9f2d-6d6f746f3031"
#define UUID_JOURNAL              "7a3b0005-5c1e-4b8a-9f2d-6d6f746f3031"
#define UUID_OTA_CONTROLE         "7a3b0006-5c1e-4b8a-9f2d-6d6f746f3031"
#define UUID_OTA_DONNEES          "7a3b0007-5c1e-4b8a-9f2d-6d6f746f3031"
#define UUID_FICHIER              "7a3b0008-5c1e-4b8a-9f2d-6d6f746f3031"
#define UUID_FICHIER_ENTRANT      "7a3b0009-5c1e-4b8a-9f2d-6d6f746f3031"

// true si un téléphone est connecté ET authentifié par le code PIN.
bool appliConnectee();

// Envoie un message JSON (état ou événement) sur la caractéristique Données.
void publierUnEvenementVersLAppli(JsonDocument& message);

// Répond à une commande : {"t":"rep","cmd":"PHOTO","ok":true,"msg":"..."}
void repondreALAppli(const char* commande, bool reussi, const char* message);

// Lignes du journal, réponses de la mise à jour, octets d'un fichier téléchargé.
void transmettreUneLigneDuJournalALAppli(const char* ligne);
void envoyerUneReponseDeMiseAJourALAppli(const char* texte);
void envoyerDesOctetsALAppli(const uint8_t* octets, size_t taille);

// Taille utile d'une notification (dépend de ce que le téléphone a négocié).
size_t tailleMaximumDUneNotification();

bool demarrerModuleAppli();

#endif // MODULE_APPLI_H
