#include <BluetoothSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "module_Bluetooth.h"

/*Comment lire les logs sur votre smartphone désormais ?Les applications de terminaux série classiques ne voient pas le BLE. 
Vous devez utiliser une application compatible BLE UART :
Téléchargez l'application gratuite Serial Bluetooth Terminal (sur Android) ou LightBlue / nRF Connect (sur iOS).
Dans l'application Serial Bluetooth Terminal, ouvrez le menu latéral ➡️ Devices ➡️ Onglet BLE.
Cliquez sur ESP32-S3-LIVE-LOGS pour vous connecter.
Vos flux de logs en direct s'afficheront immédiatement à l'écran, portés de manière transparente par le contrôleur BLE natif de votre ESP32-S3.*/



// UUIDs standards du protocole de port série virtuel "Nordic UART Service" (NUS)
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *pTxCharacteristic;
bool appareilConnecte = false;

// Callbacks pour surveiller la connexion de votre smartphone
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        appareilConnecte = true;
        Serial.println("🔵 [BLE] Smartphone connecté !");
    };
    void onDisconnect(BLEServer* pServer) {
        appareilConnecte = false;
        Serial.println("🔵 [BLE] Smartphone déconnecté.");
        // Relance la visibilité pour permettre une nouvelle connexion
        pServer->getAdvertising()->start(); 
    }
};

#define TAILLE_MAX_LOG 128
extern QueueHandle_t fileAttenteLogs;

bool initialiserBluetooth() {
    Serial.println("🔵 [BLE] Initialisation du Bluetooth Low Energy (NUS)...");

    // 1. Initialisation du périphérique BLE avec le nom de diffusion
    BLEDevice::init("Moto_ESP32");

    // 2. Création du serveur BLE et attachement des écouteurs
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // 3. Création du service UART
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // 4. Création de la caractéristique d'émission (TX) pour envoyer les logs
    pTxCharacteristic = pService->createCharacteristic(
                            CHARACTERISTIC_UUID_TX,
                            BLECharacteristic::PROPERTY_NOTIFY
                        );
    pTxCharacteristic->addDescriptor(new BLE2902());

    // 5. Démarrage du service et de la diffusion radio
    pService->start();
    pServer->getAdvertising()->start();
    
    Serial.println("✅ [BLE] Service UART actif. En attente de connexion...");
    return true;
}

void tacheLogsBluetooth(void * pvParameters) {
    char logMessage[TAILLE_MAX_LOG];

    while(1) {
        // On vérifie s'il y a un log dans la Queue et si un smartphone est connecté
        if (appareilConnecte) {
            if (xQueuePeek(queueLogs, &logMessage, pdMS_TO_TICKS(10)) == pdPASS) {
                
                // Envoi de la ligne de log en BLE
                String messageStr = String(logMessage) + "\n";
                pTxCharacteristic->setValue((uint8_t*)messageStr.c_str(), messageStr.length());
                pTxCharacteristic->notify(); // Envoie une notification "Push" instantanée au téléphone
                
                // Petit délai pour ne pas saturer la bande passante BLE
                vTaskDelay(pdMS_TO_TICKS(20)); 
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
