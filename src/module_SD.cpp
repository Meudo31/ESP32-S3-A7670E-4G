#include <HardwareSerial.h>
#include <FS.h>
#include <SD.h>
#include "esp_camera.h"

#include "module_SD.h"
#include "module_DateHeure.h"
#include "module_camera.h"


QueueHandle_t queueStockageSD                   = NULL;
TaskHandle_t xTacheVerifierEspaceCarteSDHandle  = NULL;

VolumeCarteSD volumeActuelCarteSD;


bool envoyerAuStockage(TypeDonneeSD type, String texte, PhotoComplete* fb) {
    if (queueStockageSD == NULL) return false;

    MessageStockage msg;
    msg.type   = type;
    msg.texte  = texte;
    msg.fb     = fb;

    // Envoi dans la queue. On attend maximum 10 ticks si la queue est temporairement pleine
    if (!xQueueSend(queueStockageSD, &msg, pdMS_TO_TICKS(10)) == pdPASS) {
        // Sécurité : si l'envoi en queue échoue pour une photo, il faut libérer la RAM ici
        if (type == TypeDonneeSD::PHOTO && msg.fb->buf != NULL) {
            free(msg.fb->buf);
        }

        my_log_printf("[💾 ❌] L'envoie dans la Queue queueStockageSD a échoué pour : Type->%d Message->%s!", msg.type, msg.texte.c_str());
        return false;
    }

    return true;
}


void TaskStockageDansSD(void * pvParameters) {  // Déclenché par queueStockageSD
    MessageStockage msg;
    Serial.println("[💾 ✅]  Gestionnaire de stockage SD démarré.");

    while(1) {
        // Attente d'un message (bloquant, ne consomme pas de CPU si la queue est vide)
        if (xQueueReceive(queueStockageSD, &msg, portMAX_DELAY) == pdPASS) {
            
            // 1. Création dynamique des chemins de dossiers
            String dossierJour = "/" + Date_formatteeUSFichier();
            String dossierPhotos = dossierJour + "/photos";
            String cheminFichier = "";

            // 2. Vérification et création automatique des répertoires
            if (!SD.exists(dossierJour)) {
                SD.mkdir(dossierJour);
                log_printf("[SD] Création du dossier jour : %s", dossierJour.c_str());
            }

            // 3. Traitement selon le type de données
            if (msg.type == TypeDonneeSD::PHOTO) {
                // Pour les photos, on s'assure que le sous-dossier /photos existe
                if (!SD.exists(dossierPhotos)) {
                    SD.mkdir(dossierPhotos);
                }
                
                // Le nom de la photo est basé sur l'heure (ex: /2026-09-04/photos/2026-09-04_10-14-32_ESP32_Moto.jpg)
                cheminFichier = dossierPhotos + "/" + Date_formatteeUSFichier() + "_" + Heure_formatteeFichier() + "_ESP32_Moto.jpg";

                if (msg.fb->buf != NULL) {
                    File file = SD.open(cheminFichier, FILE_WRITE);  // Ne serai-ce pas FILE_APPEND
                    if (file) {
                        file.write(msg.fb->buf, msg.fb->len);
                        file.close();
                        log_printf("[💾 ✅] Photo sauvegardée : %s (%d octets)", cheminFichier.c_str(), msg.fb->len);
                    } else {
                        log_printf("[💾 ❌] Impossible de créer le fichier photo %s (%d octets)!", cheminFichier.c_str(), msg.fb->len);
                    }
                    
                    // LIBÉRATION CRITIQUE de la mémoire RAM de la caméra
                    free(msg.fb->buf);
                }
            } 
            else {
                // Pour le texte (LOG, GPS, GYRO)
                // Aiguillage vers le bon fichier
                cheminFichier = dossierJour + "/" + Date_formatteeUSFichier();
                switch (msg.type)
                {
                    case TypeDonneeSD::LOG:   cheminFichier += "_logs_ESP32_Moto.txt"; break;
                    case TypeDonneeSD::GPS:   cheminFichier += "_gps_ESP32_Moto.txt";  break;
                    case TypeDonneeSD::GYRO:  cheminFichier += "_gyro_ESP32_Moto.txt"; break;

                    default: log_printf("[💾 ❌] Type de fichier %d inconnu !", msg.type); break;
                }


                File file = SD.open(cheminFichier, FILE_APPEND);
                if (file) {
                    // Format d'écriture : [HH:MM:SS] Données...
                    file.printf("[%s] %s\n", Heure_formatteeStandard(), msg.texte.c_str());
                    file.close();
                    my_log_printf("[💾 ✅] Message ajouté au fichier %s", cheminFichier.c_str());

                } else {
                    Serial.printf("[💾 ❌] Impossible d'ouvrir le fichier texte : %s\n", cheminFichier.c_str());
                }
            }

            xTaskNotifyGive(xTacheVerifierEspaceCarteSDHandle);
        }
    }
}


void taskVerifierEspaceCarteSD(void * pvParameters) { // Déclenché par xTacheVerifierEspaceCarteSDHandle
    while(1){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);        // La tâche s'endort ici (0% CPU) en attendant le signal
        Serial.println("\n💾 --- ANALYSE DE LA CARTE SD ---");

        VolumeCarteSD volume;
        volumeActuelCarteSD.totalBytes = SD.totalBytes();
        volumeActuelCarteSD.usedBytes  = SD.usedBytes();
        volumeActuelCarteSD.freeBytes  = volumeActuelCarteSD.totalBytes - volumeActuelCarteSD.usedBytes;
        volumeActuelCarteSD.usedPercentage = (volumeActuelCarteSD.totalBytes > 0) ? (static_cast<float>(volumeActuelCarteSD.usedBytes) / static_cast<float>(volumeActuelCarteSD.totalBytes)) * 100.0f : 0.0f;

        // 4. Affichage des résultats avec vos émojis de statut
        my_log_printf("💾 Espace Total   : %u Mo\n", volumeActuelCarteSD.totalBytes / (1024 * 1024));
        my_log_printf("📊 Espace Utilisé : %u Mo\n", volumeActuelCarteSD.usedBytes / (1024 * 1024));
        
        // Alerte de sécurité si l'espace libre est trop faible (ex: moins de 100 Mo)
        if (volumeActuelCarteSD.freeBytes / (1024 * 1024) < 100) {
            my_log_printf("[💾⚠️] ALERTE : Espace Libre Critique : %.2f%% !\n", volumeActuelCarteSD.usedPercentage);
        } else {
            my_log_printf("[💾 ✅] Espace Libre : %.2f%%\n", volumeActuelCarteSD.usedPercentage);
        }
        Serial.println("---------------------------------\n");
    }
}