#include <Adafruit_NeoPixel.h>

#include "module_logs.h"
#include "module_SD.h"

// --- CONFIGURATION ---
#define TAILLE_TAMPON_SD 512    // Taille du tampon RAM avant écriture sur la SD (en octets)

// --- STRUCTURES & GLOBALS ---
QueueHandle_t queueLogs = NULL;
String tamponRAM_SD = ""; // Tampon pour accumuler les logs en RAM


// --- FONCTION PRODUCTRICE : À utiliser à la place de Serial.printf ---
void my_log_printf(const char *format, ...) {
    char messageFormate[TAILLE_MAX_LOG];
    
    // Récupération et formatage des arguments (comme un printf classique)
    va_list args;
    va_start(args, format);
    vsnprintf(messageFormate, sizeof(messageFormate), format, args);
    va_end(args);

    // Si la file d'attente existe, on y envoie le message de manière non-bloquante (0 tick d'attente)
    if (queueLogs != NULL) {
        if (xQueueSend(queueLogs, &messageFormate, 0) != pdPASS) {
            // Si la file est pleine, on affiche directement sur le port série physique pour ne pas perdre l'info
            Serial.println(" [⏰ SYSTEM] Erreur : File d'attente des logs saturée !");
            Serial.println(messageFormate);
        }
    } else {Serial.println(messageFormate);}
}


// --- TÂCHE CONSOMMATRICE (ARRIÈRE-PLAN) ---
void tacheGestionLogs(void * pvParameters) {  // Déclenché par queueLogs
    char messageRecu[TAILLE_MAX_LOG];
    
    while(1) {
        // Attente d'un message dans la file (Bloquant au sens FreeRTOS : ne consomme pas de CPU s'il n'y a rien)
        if (xQueueReceive(queueLogs, &messageRecu, portMAX_DELAY) == pdPASS) {
            
            // 1. Sortie immédiate sur le port Série physique pour le debug en direct
            Serial.println(messageRecu);

            // 2. Ajout du message au tampon RAM avec un saut de ligne
            tamponRAM_SD += String(messageRecu) + "\n";

            // 3. Si le tampon RAM dépasse la taille limite, on écrit physiquement sur la SD
            if (tamponRAM_SD.length() >= TAILLE_TAMPON_SD) {
                envoyerAuStockage(TypeDonneeSD::LOG, tamponRAM_SD);
            }
        }
    }
}


// Comment l'appeler depuis une autre tâche ou fonction : log_printf("Message de log : %d", valeur);