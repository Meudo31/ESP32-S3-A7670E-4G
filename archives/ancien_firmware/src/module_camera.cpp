#include "esp_camera.h"

#include "module_camera.h"
#include "module_gps.h"
#include "module_SD.h"

#define EXIF_DYNAMIC_DATA_SIZE 43
#define EXIF_BASE_HEADER_SIZE  12
#define EXIF_DATA_TOTAL_SIZE EXIF_BASE_HEADER_SIZE+EXIF_DYNAMIC_DATA_SIZE


const uint8_t EXIF_BASE_HEADER[12] = {  // size = 12
    0xFF, 0xD8, // SOI (Start of Image)
    0xFF, 0xE1, // APP1 (Marqueur EXIF)
    0x00, 0x33, // Longueur du Payload = 51 octets (2 de taille + 6 de signature + 43 de texte)
    0x45, 0x78, 0x69, 0x66, 0x00, 0x00 // Signature "Exif\0\0"
};

QueueHandle_t queuePhotoMMS  = NULL;
QueueHandle_t queuePhotoSD   = NULL;
QueueHandle_t queuePhotoBT   = NULL;
QueueHandle_t queuePhotoWifi = NULL;

TaskHandle_t xTacheCameraHandle = NULL;


bool initialisationHardwareCamera() {
    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;         // 20MHz
    config.pixel_format = PIXFORMAT_JPEG;   // Format requis pour les MMS
    config.frame_size   = FRAMESIZE_SVGA;   // Réglage de la résolution : SVGA (800x600) est le compromis idéal pour un MMS (< 60 Ko)
    config.jpeg_quality = 12;   // Qualité de 0 (excellente) à 63 (médiocre). 12 offre une super compression
    config.fb_count     = 1;    // Nombre de tampons d'image en mémoire

    esp_err_t err = esp_camera_init(&config);   // Initialisation physique du capteur
    if (err != ESP_OK) {
        my_log_printf("[📷 CAM] ❌ Échec de l'initialisation de la caméra (Code: 0x%x)\n", err);
        return false;
    }

    my_log_printf("[📷 CAM] ✅ Matériel de la caméra prêt.");
    return true;
}

void taskPriseDePhoto(void * pvParameters) {  // Déclenché par xTacheCameraHandle
    my_log_printf("[📷 CAM Cœur 1] Tâche Camera en attente de notification directe sur le cœur : %s", xPortGetCoreID());

    for(;;) {
        /* 
           🚀 LA TÂCHE DORT ICI (0% CPU) tant que la Queue est vide.
           Dès qu'une fonction appelle xQueueSend(), cette ligne intercepte 
           les données, les copie dans 'numeroEtMessageATraiter' et réveille la tâche.
        */
        QueFaireDeLObjet_Values modeChoisi;
        uint32_t valeurNotification;


       if (xTaskNotifyWait(0, 0, &valeurNotification, portMAX_DELAY) == pdTRUE) {
            modeChoisi = static_cast<QueFaireDeLObjet_Values>(valeurNotification);
            
            my_log_printf("[📷 CAM Cœur 1] Réveil ! Déclenchement de la capture photo...");
            
            camera_fb_t * fb = esp_camera_fb_get();     // 1. Prise de la photo (Stockage dans le Frame Buffer "fb")
            
            if (!fb) {
                my_log_printf("[📷 CAM Cœur 1] ❌ Erreur : Impossible de capturer l'image.");
                continue; // Retour en haut de la boucle pour se rendormir
            }

            my_log_printf("[📷 CAM Cœur 1] Photo capturée avec succès !\n");
            // my_log_printf("         ↳ Adresse RAM : %p\n", (void*)adresseMemoirePhoto);
            // my_log_printf("         ↳ Taille      : %d octets (%.1f Ko)\n", tailleExactePhotoBytes, (float)tailleExactePhotoBytes / 1024.0);

            

            // 1. Préparation du texte dynamique à taille strictement fixe (43 caractères + '\0')
            // Heure GPS : HHMMSS.ms 141522.0       Date GPS : DDMMYY 260826
            // Format données EXIF :DT:2026-09-09 11:42:00|GPS:+48.85661,+002.35222
            char donneesDynamiques[EXIF_DYNAMIC_DATA_SIZE+1];
            snprintf(donneesDynamiques, sizeof(donneesDynamiques), 
                     "DT:20%.2s-%.2s-%.2s %.2s:%.2s:%.2s|GPS:%+09.5f,%+010.5f", 
                     &GPS_Data.date[4], 
                     &GPS_Data.date[2], 
                     &GPS_Data.date[0], 
                     &GPS_Data.heure[0], 
                     &GPS_Data.heure[2], 
                     &GPS_Data.heure[4], 
                     GPS_Data.latitude, GPS_Data.longitude);

            // 2. Calcul des tailles pour le nouveau fichier
            // On retire 2 octets à la photo originale car on remplace son marqueur SOI (0xFFD8) de début.
            // Allocation d'un bloc de mémoire RAM unifié (dans la PSRAM de votre ESP32-S3)
            uint8_t* photoBuffer = (uint8_t*)malloc(EXIF_DATA_TOTAL_SIZE + (fb->len - 2));

            // uint8_t* adresseMemoirePhoto    = fb->buf; // fb->buf = pointeur vers le premier octet de la photo (uint8_t*)
            // size_t   tailleExactePhotoBytes = fb->len; // fb->len = taille exacte de l'image compressée en octets (size_t)

            if (photoBuffer != NULL) {
                // Étape A : Copie des 12 octets constants de l'en-tête
                memcpy(photoBuffer, EXIF_BASE_HEADER, EXIF_BASE_HEADER_SIZE);
                
                // Étape B : Copie des 43 octets du texte GPS/Date/Heure juste après
                memcpy(photoBuffer + EXIF_BASE_HEADER_SIZE, donneesDynamiques, EXIF_DYNAMIC_DATA_SIZE);
                
                // Étape C : Copie de tout le reste de la photo (en sautant ses 2 premiers octets)
                memcpy(photoBuffer + EXIF_DATA_TOTAL_SIZE, fb->buf + 2, fb->len - 2);

                // 4. On crée le paquet cadeau à distribuer
                PhotoComplete photoPourEnvoi;
                photoPourEnvoi.buf = photoBuffer;
                photoPourEnvoi.len = EXIF_DATA_TOTAL_SIZE + (fb->len - 2);

                // 5. Libération immédiate du matériel de la caméra (le capteur est prêt pour la suite)
                esp_camera_fb_return(fb);




                switch (modeChoisi){
                    case QueFaireDeLObjet_Values::ENVOIE_SMS:
                        my_log_printf("[📷 CAM Cœur 1] Photo prise pour être envoyée par MMS");
                        xQueueSend(queuePhotoMMS, &photoPourEnvoi, portMAX_DELAY);
                    break;
                    
                    case QueFaireDeLObjet_Values::STOCKAGE_SD:
                        my_log_printf("[📷 CAM Cœur 1] Photo prise pour être stockée dans la carte SD");
                        xQueueSend(queuePhotoSD, &photoPourEnvoi, portMAX_DELAY);
                        envoyerAuStockage(TypeDonneeSD::PHOTO, "", &photoPourEnvoi);
                        break;    

                    case QueFaireDeLObjet_Values::ENVOIE_BLUETOOTH:
                        my_log_printf("[📷 CAM Cœur 1] Photo prise pour être envoyée par Bluetooth");
                        xQueueSend(queuePhotoBT, &photoPourEnvoi, portMAX_DELAY);
                        break;
                    
                    case QueFaireDeLObjet_Values::ENVOIE_WIFI:
                        my_log_printf("[📷 CAM Cœur 1] Photo prise pour être envoyée par WiFi");
                        xQueueSend(queuePhotoWifi, &photoPourEnvoi, portMAX_DELAY);
                        break;
                    
                    default: my_log_printf("[📷 CAM Cœur 1] La valeur %d de valeurNotification est inconnue.\n", modeChoisi); break;
                }
            }            
        }
    }
}







/* Comment déclencher la prise de photo depuis une autre tâche ou fonction :
    // Cas 2 : Prise de photo puis stockage dans la SD card
    void declencherScenarioSD() {
        xTaskNotify(xTacheCameraHandle, static_cast<uint32_t>(ModeEnvoi::STOCKAGE_SD), eSetValueWithOverwrite);
    }

    // Cas 3 : Prise de photo puis envoi par bluetooth
    void declencherScenarioBluetooth() {
        xTaskNotify(xTacheCameraHandle, static_cast<uint32_t>(ModeEnvoi::ENVOIE_BLUETOOTH), eSetValueWithOverwrite);
    }

    // Cas 4 : Prise de photo puis envoi par Wifi
    void declencherScenarioWifi() {
        xTaskNotify(xTacheCameraHandle, static_cast<uint32_t>(ModeEnvoi::ENVOIE_WIFI), eSetValueWithOverwrite);
    }
*/



/*
void taskStockagePhotoSD(void * pvParameters) {  // Déclenché par queuePhotoSD
    camera_fb_t * fbRecu = NULL;
    while(1) {
        if (xQueueReceive(queuePhotoSD, &fbRecu, portMAX_DELAY) == pdPASS) {
            my_log_printf("[Tâche 3] Écriture sur la carte SD...");
            // Insérer le code d'écriture binaire du buffer (fbRecu->buf)
            
            // LIBÉRATION CRITIQUE : C'est la tâche finale qui nettoie la RAM
            esp_camera_fb_return(fbRecu);  // Libère le Frame Buffer pour que la RAM de l'ESP32-S3 ne sature pas au prochain déclenchement
            my_log_printf("[📷 CAM Cœur 1] Mémoire de la photo libérée. Retour en veille.");
        }
    }
}

void taskEnvoiPhotoBluetooth(void * pvParameters) {  // Déclenché par queuePhotoBT
    camera_fb_t * fbRecu = NULL;
    while(1) {
        if (xQueueReceive(queuePhotoBT, &fbRecu, portMAX_DELAY) == pdPASS) {
            my_log_printf("[Tâche 4] Envoi via Bluetooth BLE/Classic...");
            
            // LIBÉRATION CRITIQUE : C'est la tâche finale qui nettoie la RAM
            esp_camera_fb_return(fbRecu);  // Libère le Frame Buffer pour que la RAM de l'ESP32-S3 ne sature pas au prochain déclenchement
            my_log_printf("[📷 CAM Cœur 1] Mémoire de la photo libérée. Retour en veille.");
        }
    }
}

void taskEnvoiPhotoWifi(void * pvParameters) {  // Déclenché par queuePhotoWifi
    camera_fb_t * fbRecu = NULL;
    while(1) {
        if (xQueueReceive(queuePhotoWifi, &fbRecu, portMAX_DELAY) == pdPASS) {
            my_log_printf("[Tâche 5] Téléversement via Wi-Fi...");
            
            // LIBÉRATION CRITIQUE : C'est la tâche finale qui nettoie la RAM
            esp_camera_fb_return(fbRecu);  // Libère le Frame Buffer pour que la RAM de l'ESP32-S3 ne sature pas au prochain déclenchement
            my_log_printf("[📷 CAM Cœur 1] Mémoire de la photo libérée. Retour en veille.");
        }
    }
}*/
