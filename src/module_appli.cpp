#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <Update.h>
#include <esp_gap_ble_api.h>
#include <esp_ota_ops.h>

#include "module_appli.h"
#include "module_batterie.h"
#include "module_camera.h"
#include "module_CANbus.h"
#include "module_gps.h"
#include "module_gyroscope.h"
#include "module_sms.h"

/* ──────────────────────────────────────────────────────────────────────────────
   Liaison BLE avec l'application Android « Moto ESP32 » (dossier android/ du dépôt).

   Sécurité : tout le service exige un appairage avec code PIN (6 chiffres).
   Le code se règle dans include/secrets.h :
       #define SECRET_BLE_PIN 123456
   Au premier branchement, Android affiche une fenêtre « Demande d'association » : saisir ce code.

   Fonctionnement :
     - les écritures de l'application (commandes, OTA) arrivent dans les callbacks BLE ;
     - les commandes sont confiées à tacheAppliBLE (pour ne pas bloquer la pile Bluetooth) ;
     - tacheAppliBLE envoie aussi les données en direct et les logs.
   ────────────────────────────────────────────────────────────────────────────── */

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef SECRET_BLE_PIN
#warning "SECRET_BLE_PIN absent de include/secrets.h : code PIN Bluetooth par défaut 123456 (à changer !)"
#define SECRET_BLE_PIN 123456
#endif

#define TAILLE_MAX_COMMANDE   64
#define CAPACITE_FILE_LOGS_BLE 20
#define CAPACITE_FILE_CMD_BLE   5

// Cadence d'envoi des données en direct (millisecondes)
#define PERIODE_MOTO_MS     500     // bus CAN + accéléromètre
#define PERIODE_GPS_MS     1000
#define PERIODE_SYSTEME_MS 2000     // système + 4G
#define PERIODE_BATTERIE_MS 5000

namespace {

BLEServer*         serveur         = nullptr;
BLECharacteristic* carTelemetrie   = nullptr;
BLECharacteristic* carReponse      = nullptr;
BLECharacteristic* carLogs         = nullptr;
BLECharacteristic* carOtaCtrl      = nullptr;

volatile bool      connecte        = false;
volatile bool      authentifie     = false;
volatile bool      logsActifs      = true;
volatile uint16_t  idConnexion     = 0;

QueueHandle_t      fileLogsBLE     = nullptr;   // éléments : char[TAILLE_MAX_LOG]
QueueHandle_t      fileCommandes   = nullptr;   // éléments : char[TAILLE_MAX_COMMANDE]

// État de la mise à jour OTA (modifié uniquement depuis les callbacks BLE, qui s'exécutent l'un après l'autre)
volatile bool      otaEnCours      = false;
size_t             otaTailleAttendue = 0;
size_t             otaRecu         = 0;
volatile uint32_t  redemarrageA    = 0;         // millis() auquel redémarrer (0 = pas de redémarrage prévu)

// ─── Envoi d'une notification (ignoré si aucun téléphone authentifié) ───
void notifier(BLECharacteristic* car, const char* texte) {
    if (!connecte || !authentifie || car == nullptr) return;
    car->setValue((uint8_t*)texte, strlen(texte));
    car->notify();
}

void repondre(const char* commande, bool ok, const char* message) {
    char json[200];
    snprintf(json, sizeof(json), "{\"t\":\"rep\",\"cmd\":\"%s\",\"ok\":%s,\"msg\":\"%s\"}",
             commande, ok ? "true" : "false", message);
    notifier(carReponse, json);
}

// ─── Copie du contenu écrit par le téléphone dans un tampon texte terminé par \0 ───
size_t lireTexte(BLECharacteristic* car, char* tampon, size_t taille) {
    size_t n = car->getLength();
    if (n > taille - 1) n = taille - 1;
    memcpy(tampon, car->getData(), n);
    tampon[n] = '\0';
    // On retire les fins de ligne éventuelles
    while (n > 0 && (tampon[n - 1] == '\n' || tampon[n - 1] == '\r' || tampon[n - 1] == ' ')) tampon[--n] = '\0';
    return n;
}

// ─────────────────────────────── Données en direct ───────────────────────────────

void envoyerGPS() {
    char json[220];
    snprintf(json, sizeof(json),
        "{\"t\":\"gps\",\"fix\":%d,\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"vit\":%.1f,\"cap\":%.0f,\"sat\":%d,\"hdop\":%.1f,\"maj\":%ld}",
        GPS_Data.fixValide ? 1 : 0, GPS_Data.latitude, GPS_Data.longitude, GPS_Data.altitude,
        GPS_Data.vitesse, GPS_Data.Direction.degres, GPS_Data.NbSatelites,
        GPS_Data.Precision_Position.HDOP_Horizontal_Dilution_Of_Precision, (long)GPS_Data.MaJ_TimeStamp);
    notifier(carTelemetrie, json);
}

void envoyerBatterie() {
    char json[100];
    if (jaugeBatterie.isDeviceReady()) {
        snprintf(json, sizeof(json), "{\"t\":\"bat\",\"ok\":1,\"v\":%.2f,\"pct\":%.1f}",
                 jaugeBatterie.cellVoltage(), jaugeBatterie.cellPercent());
    } else {
        snprintf(json, sizeof(json), "{\"t\":\"bat\",\"ok\":0}");
    }
    notifier(carTelemetrie, json);
}

void envoyerMoto() {
    const CANbusData& c = lastCANbusData;
    char json[300];
    snprintf(json, sizeof(json),
        "{\"t\":\"moto\",\"vit\":%.0f,\"rpm\":%d,\"rap\":%d,\"temp\":%.0f,\"ess\":%d,\"contact\":%d,\"beq\":%d,"
        "\"frein\":%d,\"embr\":%d,\"abs\":%d,\"clg\":%d,\"cld\":%d,\"route\":%d,\"code\":%d,\"warn\":%d}",
        c.indianVitesseKMH, c.indianRPM, c.indianRapportEngage, c.indianTemperatureMoteur, c.indianNiveauEssence,
        c.indianContactActif, c.kickstandSwitchOut || c.indianBequilleSortie,
        c.brakeLightsActive || c.indianFreinActif, c.clutchSwitchActive || c.indianEmbrayageEnfonce,
        c.antiLockBrakeActive, c.turnSignalLeft || c.indianClignotantGauche,
        c.turnSignalRight || c.indianClignotantDroit, c.highBeamActive || c.indianFeuxDeRoute,
        c.lowBeamActive, c.hazardLightsActive);
    notifier(carTelemetrie, json);
}

void envoyerAccelerometre() {
    const GyroData& g = lastGyroscopeData;
    char json[160];
    snprintf(json, sizeof(json),
        "{\"t\":\"acc\",\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"roll\":%.1f,\"pitch\":%.1f,\"g\":%.2f}",
        g.accelerationX, g.accelerationY, g.accelerationZ, g.roll, g.pitch, g.forceGTotal);
    notifier(carTelemetrie, json);
}

void envoyerSysteme() {
    const esp_partition_t* partition = esp_ota_get_running_partition();
    char json[220];
    snprintf(json, sizeof(json),
        "{\"t\":\"sys\",\"fw\":\"%s\",\"build\":\"%s %s\",\"up\":%lu,\"heap\":%u,\"psram\":%u,\"part\":\"%s\"}",
        APPLI_VERSION_FIRMWARE, __DATE__, __TIME__, (unsigned long)(millis() / 1000),
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), partition ? partition->label : "?");
    notifier(carTelemetrie, json);

    snprintf(json, sizeof(json),
        "{\"t\":\"4g\",\"etat\":%d,\"sig\":%d,\"conso\":%lu,\"reste\":%d}",
        SIM_data.SIM_status, SIM_data.SIM_qualite_signal,
        (unsigned long)SIM_data.dataConsommeeCeMoisBytes, SIM_data.pourcentageDataRestant);
    notifier(carTelemetrie, json);
}

void envoyerTout() {
    envoyerSysteme();
    envoyerBatterie();
    envoyerGPS();
    envoyerMoto();
    envoyerAccelerometre();
}

// ─────────────────────────────── Commandes ───────────────────────────────

void traiterCommande(char* commande) {
    for (char* p = commande; *p; ++p) {
        if (*p == '"' || *p == '\\') *p = '_';            // garde un JSON valide dans la réponse
        else *p = (char)toupper((unsigned char)*p);
    }
    my_log_printf("[📱 APPLI] Commande reçue : %s", commande);

    if (strcmp(commande, "PING") == 0) {
        repondre(commande, true, "pong");
    }
    else if (strcmp(commande, "STATUS") == 0) {
        envoyerTout();
        repondre(commande, true, "Données envoyées");
    }
    else if (strcmp(commande, "GPS") == 0) {
        if (xTacheGPSHandle == NULL) { repondre(commande, false, "Tâche GPS non démarrée"); return; }
        xTaskNotify(xTacheGPSHandle, static_cast<uint32_t>(QueFaireDeLObjet_Values::STOCKAGE_SD), eSetValueWithOverwrite);
        repondre(commande, true, "Mise à jour de la position demandée");
    }
    else if (strcmp(commande, "PHOTO") == 0) {
        if (xTacheCameraHandle == NULL) { repondre(commande, false, "Tâche caméra non démarrée"); return; }
        xTaskNotify(xTacheCameraHandle, static_cast<uint32_t>(QueFaireDeLObjet_Values::STOCKAGE_SD), eSetValueWithOverwrite);
        repondre(commande, true, "Photo demandée (enregistrée sur la carte SD)");
    }
    else if (strcmp(commande, "LOGS ON") == 0) {
        logsActifs = true;
        repondre(commande, true, "Logs activés");
    }
    else if (strcmp(commande, "LOGS OFF") == 0) {
        logsActifs = false;
        xQueueReset(fileLogsBLE);
        repondre(commande, true, "Logs désactivés");
    }
    else if (strcmp(commande, "REBOOT") == 0) {
        repondre(commande, true, "Redémarrage dans 1 s");
        redemarrageA = millis() + 1000;
    }
    else {
        repondre(commande, false, "Commande inconnue");
    }
}

// ─────────────────────────────── Mise à jour OTA ───────────────────────────────

void notifierOta(const char* texte) { notifier(carOtaCtrl, texte); }

void erreurOta(const char* contexte) {
    char texte[120];
    snprintf(texte, sizeof(texte), "ERR %s : %s", contexte, Update.errorString());
    my_log_printf("[📱 OTA] %s", texte);
    if (otaEnCours) Update.abort();
    otaEnCours = false;
    notifierOta(texte);
}

void traiterOtaControle(const char* texte) {
    if (strncmp(texte, "START", 5) == 0) {
        unsigned long taille = 0;
        char md5[33] = {0};
        int n = sscanf(texte + 5, "%lu %32s", &taille, md5);
        if (n < 1 || taille == 0) { notifierOta("ERR format : START <taille> <md5>"); return; }

        if (otaEnCours) Update.abort();
        if (!Update.begin(taille, U_FLASH)) { erreurOta("début"); return; }
        if (n == 2 && strlen(md5) == 32 && !Update.setMD5(md5)) { erreurOta("md5"); return; }

        otaEnCours        = true;
        otaTailleAttendue = taille;
        otaRecu           = 0;
        xQueueReset(fileLogsBLE);
        my_log_printf("[📱 OTA] Début de la mise à jour : %lu octets", taille);
        notifierOta("OK START");
    }
    else if (strcmp(texte, "END") == 0) {
        if (!otaEnCours) { notifierOta("ERR aucune mise à jour en cours"); return; }
        if (otaRecu != otaTailleAttendue) {
            char msg[80];
            snprintf(msg, sizeof(msg), "ERR taille : %u reçus sur %u", (unsigned)otaRecu, (unsigned)otaTailleAttendue);
            Update.abort();
            otaEnCours = false;
            notifierOta(msg);
            return;
        }
        if (!Update.end()) { erreurOta("fin"); return; }   // vérifie aussi le MD5
        otaEnCours = false;
        my_log_printf("[📱 OTA] Mise à jour réussie, redémarrage...");
        notifierOta("OK END");
        redemarrageA = millis() + 1500;
    }
    else if (strcmp(texte, "ABORT") == 0) {
        if (otaEnCours) Update.abort();
        otaEnCours = false;
        notifierOta("OK ABORT");
    }
    else {
        notifierOta("ERR commande OTA inconnue");
    }
}

// ─────────────────────────────── Callbacks BLE ───────────────────────────────

class RappelsServeur : public BLEServerCallbacks {
    void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* param) override {
        connecte    = true;
        authentifie = false;
        idConnexion = param->connect.conn_id;
        // Demande le chiffrement tout de suite : le téléphone affiche la saisie du code PIN s'il n'est pas encore appairé.
        esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
        Serial.println("🔵 [APPLI] Téléphone connecté, authentification...");
    }
    void onDisconnect(BLEServer* s) override {
        connecte    = false;
        authentifie = false;
        if (otaEnCours) {
            Update.abort();
            otaEnCours = false;
            Serial.println("🔵 [APPLI] Connexion perdue : mise à jour annulée.");
        }
        Serial.println("🔵 [APPLI] Téléphone déconnecté.");
        BLEDevice::startAdvertising();
    }
};

class RappelsSecurite : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return SECRET_BLE_PIN; }
    void onPassKeyNotify(uint32_t) override {}
    bool onConfirmPIN(uint32_t) override { return false; }     // pas de comparaison numérique : code PIN obligatoire
    bool onSecurityRequest() override { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t resultat) override {
        authentifie = resultat.success;
        if (resultat.success) {
            Serial.println("🔵 [APPLI] Téléphone authentifié.");
        } else {
            Serial.printf("🔵 [APPLI] Échec d'authentification (raison 0x%x), déconnexion.\n", resultat.fail_reason);
            if (serveur) serveur->disconnect(idConnexion);
        }
    }
};

class RappelsCommande : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* car) override {
        if (!authentifie) return;
        char commande[TAILLE_MAX_COMMANDE];
        if (lireTexte(car, commande, sizeof(commande)) == 0) return;
        if (xQueueSend(fileCommandes, commande, 0) != pdPASS) repondre("?", false, "Occupé, réessayer");
    }
};

class RappelsOtaControle : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* car) override {
        if (!authentifie) return;
        char texte[TAILLE_MAX_COMMANDE];
        lireTexte(car, texte, sizeof(texte));
        traiterOtaControle(texte);
    }
};

class RappelsOtaDonnees : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* car) override {
        if (!authentifie || !otaEnCours) return;
        size_t longueur = car->getLength();
        if (otaRecu + longueur > otaTailleAttendue) { notifierOta("ERR trop de données"); Update.abort(); otaEnCours = false; return; }
        if (Update.write(car->getData(), longueur) != longueur) { erreurOta("écriture"); return; }
        otaRecu += longueur;
    }
};

// Crée une caractéristique accessible uniquement après appairage (chiffrement + PIN)
BLECharacteristic* creerCaracteristique(BLEService* service, const char* uuid, uint32_t proprietes) {
    BLECharacteristic* car = service->createCharacteristic(uuid, proprietes);
    car->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
    if (proprietes & BLECharacteristic::PROPERTY_NOTIFY) {
        BLE2902* cccd = new BLE2902();
        cccd->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
        car->addDescriptor(cccd);
    }
    return car;
}

} // namespace

// ─────────────────────────────── API publique ───────────────────────────────

bool appliBLE_connectee() { return connecte && authentifie; }

void appliBLE_transmettreLog(const char* ligne) {
    if (fileLogsBLE == nullptr || !connecte || !authentifie || !logsActifs || otaEnCours) return;
    char copie[TAILLE_MAX_LOG];
    strncpy(copie, ligne, sizeof(copie) - 1);
    copie[sizeof(copie) - 1] = '\0';
    xQueueSend(fileLogsBLE, copie, 0);   // si la file est pleine, la ligne est perdue (elle reste sur le port série / la SD)
}

bool initialiserAppliBLE() {
    Serial.println("🔵 [APPLI] Initialisation du Bluetooth pour l'application Android...");

    fileLogsBLE   = xQueueCreate(CAPACITE_FILE_LOGS_BLE, TAILLE_MAX_LOG);
    fileCommandes = xQueueCreate(CAPACITE_FILE_CMD_BLE, TAILLE_MAX_COMMANDE);
    if (fileLogsBLE == nullptr || fileCommandes == nullptr) return false;

    BLEDevice::init(APPLI_NOM_BLE);
    BLEDevice::setMTU(517);   // gros paquets : mise à jour plus rapide

    // Sécurité : appairage avec code PIN fixe, liaison mémorisée (bonding)
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(new RappelsSecurite());
    BLESecurity* securite = new BLESecurity();
    securite->setStaticPIN(SECRET_BLE_PIN);
    securite->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);

    serveur = BLEDevice::createServer();
    serveur->setCallbacks(new RappelsServeur());

    BLEService* service = serveur->createService(BLEUUID(APPLI_UUID_SERVICE), 40);

    carTelemetrie = creerCaracteristique(service, APPLI_UUID_TELEMETRIE,
                        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic* carCommande = creerCaracteristique(service, APPLI_UUID_COMMANDE,
                        BLECharacteristic::PROPERTY_WRITE);
    carCommande->setCallbacks(new RappelsCommande());
    carReponse    = creerCaracteristique(service, APPLI_UUID_REPONSE, BLECharacteristic::PROPERTY_NOTIFY);
    carLogs       = creerCaracteristique(service, APPLI_UUID_LOGS,    BLECharacteristic::PROPERTY_NOTIFY);
    carOtaCtrl    = creerCaracteristique(service, APPLI_UUID_OTA_CTRL,
                        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    carOtaCtrl->setCallbacks(new RappelsOtaControle());
    BLECharacteristic* carOtaDonnees = creerCaracteristique(service, APPLI_UUID_OTA_DATA,
                        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    carOtaDonnees->setCallbacks(new RappelsOtaDonnees());

    service->start();

    BLEAdvertising* publicite = BLEDevice::getAdvertising();
    publicite->addServiceUUID(APPLI_UUID_SERVICE);
    publicite->setScanResponse(true);   // le nom part dans la réponse au scan (l'UUID 128 bits remplit déjà l'annonce)
    BLEDevice::startAdvertising();

    if (xTaskCreatePinnedToCore(tacheAppliBLE, "Appli BLE", 6144, NULL, PRIORITE_1, NULL, COEUR_RESEAU_0) != pdPASS) {
        return false;
    }

    Serial.println("✅ [APPLI] Service BLE actif. En attente du téléphone...");
    return true;
}

void tacheAppliBLE(void * pvParameters) {
    uint32_t dernierMoto = 0, dernierGPS = 0, dernierSysteme = 0, dernierBatterie = 0;
    char commande[TAILLE_MAX_COMMANDE];
    char ligne[TAILLE_MAX_LOG];

    for (;;) {
        // 1. Commandes reçues
        while (xQueueReceive(fileCommandes, commande, 0) == pdPASS) {
            traiterCommande(commande);
        }

        // 2. Redémarrage demandé (commande REBOOT ou fin de mise à jour)
        if (redemarrageA != 0 && (int32_t)(millis() - redemarrageA) >= 0) {
            Serial.println("🔵 [APPLI] Redémarrage.");
            delay(100);
            ESP.restart();
        }

        // 3. Données en direct et logs (en pause pendant une mise à jour pour laisser toute la bande passante à l'OTA)
        if (appliBLE_connectee() && !otaEnCours) {
            uint32_t maintenant = millis();
            if (maintenant - dernierMoto     >= PERIODE_MOTO_MS)     { dernierMoto     = maintenant; envoyerMoto(); envoyerAccelerometre(); }
            if (maintenant - dernierGPS      >= PERIODE_GPS_MS)      { dernierGPS      = maintenant; envoyerGPS(); }
            if (maintenant - dernierSysteme  >= PERIODE_SYSTEME_MS)  { dernierSysteme  = maintenant; envoyerSysteme(); }
            if (maintenant - dernierBatterie >= PERIODE_BATTERIE_MS) { dernierBatterie = maintenant; envoyerBatterie(); }

            for (int i = 0; i < 5 && xQueueReceive(fileLogsBLE, ligne, 0) == pdPASS; i++) {
                notifier(carLogs, ligne);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
