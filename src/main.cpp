
#include "main.h"
#include "module_batterie.h"
#include "module_camera.h"
#include "module_DateHeure.h"
#include "module_gps.h"
#include "module_LED_ESP32.h"
#include "module_neopixel.h"
#include "module_appel.h"
#include "module_SD.h"
#include "module_sms.h"
#include "module_logs.h"
#include "module_wifi.h"
#include "module_bluetooth.h"
#include "module_test_micro.h"
// #include "module_micro.h"
// #include "module_hautParleur.h"

#include "esp_camera.h"
#include <SD.h>


/*
  [💬 Cœur 0] calculs
  [🛰️ Cœur 1] réseaux
  [⚙️ SETUP]
  [🔋 MONITOR]
  [⏰ RTC]
  [⏰ SYSTEM]
  [📟 MMS]
  [📊 DATA]
  [📩 Cœur 0]
*/

// Pour mémoire : proxy git éventuel : git config --global http.proxy http://<utilisateur>:<mot_de_passe>@<hote>:<port>  (ne jamais écrire les vrais identifiants ici)
// https://docs.waveshare.com/ESP32-S3-A7670E-4G
// https://spotpear.com/wiki/ESP32-S3-A7670E-GSM-GPRS-GNSS-LTE-Cat-1-2G.html

/* Modules désactivés (leur code reste dans src/ et include/) : bus CAN, gyroscope, écran.
   Pour en réactiver un : remettre son #include, son initialiser...() dans verifierInit(...) et sa tâche xTaskCreatePinnedToCore :
     - Gyroscope : module_gyroscope.h  | initialiserGyroscope() | tacheLectureGyro (4096, PRIORITE_1, COEUR_TRAITEMENT_1)
     - Bus CAN   : module_CANbus.h     | initialiserCANbus()    | tacheEcouteCAN   (4096, PRIORITE_3, COEUR_RESEAU_0)
     - Écran     : module_display.h    | initialiserEcran()     | tacheGestionEcran (4096, PRIORITE_1, COEUR_TRAITEMENT_1)
   ⚠️ Les anciennes broches de ces modules ont été réattribuées : le gyroscope (SDA 42, INT 1) entre en conflit avec le micro INMP441 (SD 42, SCK 1),
      et le bus CAN (TX 14) avec le panneau NeoPixel (DIN 14). Il faudra leur choisir d'autres GPIO.
   Le serveur Web lit encore lastGyroscopeData (module_gyroscope.cpp reste compilé) : sans la tâche, ces valeurs restent à 0. */

HardwareSerial modemSerial(1);	// On réutilise l'UART 1 partagé pour communiquer avec le modem.
Adafruit_NeoPixel pixel(NUM_PIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

void setup() {

    pixel.begin(); // Initialise le NeoPixel
    pixel.setBrightness(50); // Luminosité à 50% (pour économiser la batterie)

    Serial.begin(115200);

    for (int i=0; i<3; i++) { // en attendant l'ouverture du Serial
        pixel.setPixelColor(0, pixel.Color(255, 255, 255)); // R=0, G=255, B=0
        pixel.show();
        delay(250);

        pixel.clear();
        pixel.show();
        delay(750);
    }

    if (!Serial) {
        my_log_printf("[⚙️ SETUP] Attention : Le moniteur série ne fonctionne pas.");
    }
    else {
        String monMessageEnteteLog;
        String thisFilePath = (__FILE__);
        monMessageEnteteLog += String("----- " + thisFilePath.substring(thisFilePath.lastIndexOf("\\")+1) + " Compilé le " + __DATE__ + " à " + __TIME__ + " -----");
        monMessageEnteteLog += (F("------------------------  MEUDO ------------------------"));
        my_log_printf(monMessageEnteteLog.c_str());
    }
    

    // Initialisation matérielle de la carte SD Waveshare
    const bool carteSD_ok = SD.begin();
    if(!carteSD_ok){
        // On continue : les files d'attente et les tâches doivent démarrer quand même (la tâche SD ignorera les écritures impossibles).
        my_log_printf("[⚙️ SETUP] Attention : carte SD introuvable, fonctionnement sans stockage.");
    }
    int nbModulesEnEchec = 0;

    auto verifierInit = [&nbModulesEnEchec](bool ok, const char* nomModule) {
        if (!ok) {
            nbModulesEnEchec++;
            my_log_printf("[⚙️ SETUP] Échec d'initialisation : %s (le reste du système continue).", nomModule);
        }
    };

    verifierInit(initialisationJaugeBatterie(),     "Jauge batterie");
    verifierInit(initialisationModem4G(),           "Modem 4G");
    verifierInit(initialiserAppelsVocaux(),         "Appels vocaux");
    verifierInit(initialisationHardwareCamera(),    "Caméra");
    verifierInit(initialisationLEDIntegree(),       "LED intégrée");
    verifierInit(initialisationNeopixelIntegree(),  "NeoPixel");
    verifierInit(initialiserNeopixelPanneau(),      "Panneau NeoPixel");
    verifierInit(initialiserWifi(),                 "Wi-Fi");
    verifierInit(initialiserBluetooth(),            "Bluetooth");
    verifierInit(initialiserTestMicrophone(),       "Micro (test)");
    // verifierInit(initialiserHautParleur(),     "Haut-parleur");


    // queuePhotoMMS  = xQueueCreate(1, sizeof(camera_fb_t*));
    // if (queuePhotoMMS == NULL) {
    //     my_log_printf("[⚙️ SETUP] Erreur critique : Échec de création de la queuePhotoMMS !");
    //     delay(1000);
    //     ESP.restart();
    // }
    // queuePhotoSD   = xQueueCreate(1, sizeof(camera_fb_t*));
    // if (queuePhotoSD == NULL) {
    //     my_log_printf("[⚙️ SETUP] Erreur critique : Échec de création de la queuePhotoSD !");
    //     delay(1000);
    //     ESP.restart();
    // }
    // queuePhotoBT   = xQueueCreate(1, sizeof(camera_fb_t*));
    // if (queuePhotoBT == NULL) {
    //     my_log_printf("[⚙️ SETUP] Erreur critique : Échec de création de la queuePhotoBT !");
    //     delay(1000);
    //     ESP.restart();
    // }
    // queuePhotoWifi = xQueueCreate(1, sizeof(camera_fb_t*));
    // if (queuePhotoWifi == NULL) {
    //     my_log_printf("[⚙️ SETUP] Erreur critique : Échec de création de la queuePhotoWifi !");
    //     delay(1000);
    //     ESP.restart();
    // }
    // queueStockageSD = xQueueCreate(CAPACITE_QUEUE_SD, sizeof(MessageStockage));
    // if (queueStockageSD == NULL) {
    //     my_log_printf("[⚙️ SETUP] Erreur critique : Échec de création de la queueStockageSD !");
    //     delay(1000);
    //     ESP.restart();
    // }
    // queueLogs = xQueueCreate(CAPACITE_QUEUE, TAILLE_MAX_LOG);
    // if (queueLogs == NULL) {
    //     my_log_printf("[⚙️ SETUP] Erreur critique : Échec de la création de la file queueLogs !");
    //     return;
    // }
    // // queueAudioOut = xQueueCreate(5, sizeof(MessageVocal));
    // // if (queueAudioOut == NULL) {
    // //     my_log_printf("[⚙️ SETUP] Erreur critique : Échec de la création de la file queueAudioOut !");
    // //     return;
    // // }

    // /*Paramètres de xTaskCreatePinnedToCore
    //     1. La Fonction de la tâche
    //     2. Le Nom textuel
    //     3. La Taille de la pile (Stack)
    //     4. Le Paramètre d'entrée
    //     5. La Priorité
    //     6. Le Handle (Pointeur)
    //     7. L'ID du Cœur (Core ID)*/
    // xTaskCreatePinnedToCore(taskBatterieESP32AfficheInfos,  "Batt",     2000, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(taskClignotementLED,            "LED",      3000, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // // xTaskCreatePinnedToCore(taskNeopixelAnimationSimple,    "NeoPixel", 3000, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(taskNeopixelPanneau,            "NeoPanel", 4096, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(taskAppels,                     "Appels",   4096, NULL, PRIORITE_2, NULL, COEUR_RESEAU_0);
    // // xTaskCreatePinnedToCore(taskHeartBeatLED,            "LED",      3000, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(taskReceptionSMS,               "SMS RX",   5000, NULL, PRIORITE_2, NULL, COEUR_RESEAU_0);
    // xTaskCreatePinnedToCore(tacheGestionLogs,               "Logs",     4096, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(tache_ServeurWeb,               "Web",      4096, NULL, PRIORITE_1, NULL, COEUR_RESEAU_0);
    // xTaskCreatePinnedToCore(tacheLogsBluetooth,             "BT",       3072, NULL, PRIORITE_1, NULL, COEUR_RESEAU_0);
    // xTaskCreatePinnedToCore(tacheTestMicro,                 "MicroTest",4096, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // // xTaskCreatePinnedToCore(tacheEnregistrementMicro,       "Micro",    4096, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // // xTaskCreatePinnedToCore(tacheHautParleur,               "HP",       4096, NULL, PRIORITE_2, NULL, COEUR_TRAITEMENT_1);


    // xTaskCreatePinnedToCore(taskPriseDePhoto,               "Cam",       8000, NULL, PRIORITE_2, &xTacheCameraHandle, COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(taskEnvoiPhotoSMS,              "SMS Photo", 4000, NULL, PRIORITE_1, NULL, COEUR_RESEAU_0); // Cœur 0 pour le modem

    // xTaskCreatePinnedToCore(taskGPSUpdateAllData,           "GPS",       6000, NULL, PRIORITE_1, &xTacheGPSHandle,              COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(taskEnvoiDonneesGPSSMS,         "SMS GPS",   4000, NULL, PRIORITE_1, &xTacheDonneesGPS_SMS_Handle,  COEUR_RESEAU_0);
    // xTaskCreatePinnedToCore(taskStockageDonneesGPSSD,       "SD GPS",    4000, NULL, PRIORITE_1, &xTacheDonneesGPS_SD_Handle,   COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(taskEnvoiDonneesGPSBluetooth,   "BT GPS",    4000, NULL, PRIORITE_1, &xTacheDonneesGPS_BT_Handle,   COEUR_RESEAU_0);
    // xTaskCreatePinnedToCore(taskEnvoiDonneesGPSWifi,        "Wifi GPS",  4000, NULL, PRIORITE_1, &xTacheDonneesGPS_Wifi_Handle, COEUR_RESEAU_0);

    // xTaskCreatePinnedToCore(TaskStockageDansSD,             "SD",        4096, NULL, PRIORITE_1, NULL, COEUR_TRAITEMENT_1);
    // xTaskCreatePinnedToCore(taskVerifierEspaceCarteSD,      "SD Verif",  4000, NULL, PRIORITE_1, &xTacheVerifierEspaceCarteSDHandle,    COEUR_TRAITEMENT_1);

    my_log_printf("[⚙️ SETUP] Initialisation terminée (%d module(s) en échec). Lancement des boucles FreeRTOS.", nbModulesEnEchec);

    // miseAJour_ConsommationData4G();

    // faireParlerESP32(MessageVocal::HELLO);
}

void loop() {
    // xTaskNotify(xTacheGPSHandle, static_cast<uint32_t>(QueFaireDeLObjet_Values::STOCKAGE_SD), eSetValueWithOverwrite);
     
    // vTaskDelay(pdMS_TO_TICKS(xDelai_UneMinute)); // Dodo 1minute pour ne pas saturer le CPU inutilement 

    // Ajouter le test des modules ou SD, Wifi, Bluetooth ... s'ils ont plantés.

    
    delay(1000);
}