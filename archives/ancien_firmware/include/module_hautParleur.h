#ifndef CONFIG_HAUT_PARLEUR_H
#define CONFIG_HAUT_PARLEUR_H
// ⚠️ MODULE NON UTILISÉ : le haut-parleur fourni est analogique et branché sur la borne SPK du modem (voir module_appel.h).
// Ce code I2S (ampli MAX98357A) est conservé tel quel, mais ses broches ne correspondent plus au câblage actuel.
#include "main.h"
#include "audio_data.h"

/*La ligne #define I2S_PORT I2S_NUM_0 signifie que le microphone et le haut-parleur utilisent 
    le même contrôleur matériel interne de l’ESP32-S3 pour gérer l'audio numérique.*
    L'ESP32-S3 possède deux périphériques I2S indépendants intégrés dans son silicium : I2S_NUM_0 et I2S_NUM_1. 
    En choisissant I2S_NUM_0 pour vos deux modules, vous configurez un mode de fonctionnement crucial pour votre projet :
    1. Le partage du même "moteur" audio
        Considérer I2S_NUM_0 comme un moteur unique permet à     l'ESP32-S3 de synchroniser parfaitement l'entrée (le micro) et la sortie (le haut-parleur).
        Cela permet d'utiliser les deux mêmes broches d'horloge 
        (GPIO 4 pour le rythme des bits et GPIO 5 pour le rythme des mots) pour les deux composants.
        Sans ce partage sur le même port, vous auriez dû utiliser deux broches d'horloge supplémentaires pour le haut-parleur, 
        ce qui aurait gaspillé des GPIOs précieux.
    2. Le fonctionnement en mode "Duplex"
        Le périphérique I2S_NUM_0 est capable de travailler en transmission (TX) et en réception (RX)
         en même temps ou de manière alternée.
         La Tâche Micro écoute le canal RX de I2S_NUM_0 sur le GPIO 3.
         La Tâche Haut-Parleur alimente le canal TX de I2S_NUM_0 sur le GPIO 42.
    ⚠️ Le piège FreeRTOS à éviter (La colocation)Puisque le micro et le haut-parleur partagent le même port physique 
        I2S_NUM_0, ils ne peuvent pas utiliser des configurations différentes 
        (par exemple, si le micro exigeait du 16 kHz et le haut-parleur du 44 kHz, le moteur planterait).
        C'est pour cela que nous avons configuré une architecture propre : 
            lorsque le haut-parleur doit parler (Tâche 6), il prend le contrôle du port I2S_NUM_0, 
            joue ses fichiers d'un coup sec, puis redonne la priorité à la tâche d'analyse du micro (Tâche 5).*/

            
#define I2S_PIN_BCK         41 // (ou 26) Horloge interne BCK
#define I2S_PIN_WS          40 // Horloge interne WS (LRCLK)
#define I2S_PIN_DATA_OUT    42 // Broche DIN de l'amplificateur
#define I2S_PORT_HP         I2S_NUM_0 // Partage le même périphérique I2S_NUM_0 que le micro


// Liste des messages vocaux que l'ESP32 peut prononcer
enum class MessageVocal {
    HELLO,
    TELL_TIME,
    LOW_BATTERY,
    SMS_RECEIVED
};

// --- SIMULATION DES TABLEAUX AUDIO EN FLASH (PROGMEM) ---
// Remplacer plus tard par vos vrais tableaux de sons convertis depuis des fichiers .wav
const uint8_t audio_hello[] PROGMEM = {0x00, 0x01}; 
const uint8_t audio_time[] PROGMEM  = {0x00, 0x02};
const uint8_t audio_lowbat[] PROGMEM = {0x00, 0x03};
const uint8_t audio_sms[] PROGMEM   = {0x00, 0x04};

extern QueueHandle_t queueAudioOut;

// Initialisation de la sortie Audio
bool initialiserHautParleur();

// Tâche FreeRTOS de gestion de la voix
void tacheHautParleur(void * pvParameters);

// Fonction rapide pour demander à l'ESP32 de parler depuis n'importe quel module
void faireParlerESP32(MessageVocal message);

#endif