#include "driver/i2s.h"
#include "module_hautParleur.h"

/* EXPLICATIONS
Pour ajouter un haut-parleur et lui faire dire des phrases en fonction des événements du système, 
la meilleure approche sur l'ESP32-S3 (qui s'intègre parfaitement avec notre architecture de micro I2S existante) est d'utiliser un amplificateur audio I2S (comme le très populaire et bon marché MAX98357A).
Le protocole I2S permet d'envoyer le flux audio numérique directement à l'amplificateur. 
Pour générer la voix de manière 100% locale, gratuite, hors-ligne (sans Internet) et extensible, 
nous allons utiliser une technique de synthèse vocale par dictionnaire de phonèmes/mots pré-enregistrés, 
ou une bibliothèque légère de Text-To-Speech (TTS) locale (comme XTronical_Talking_ESP ou l'émulation de puces de synthèse vocale).
Puisque vous souhaitez pouvoir rajouter facilement d'autres choses à dire, nous allons centraliser cette gestion dans une nouvelle tâche FreeRTOS : 
la Tâche 6 (Haut-Parleur / Audio Out). 
Les autres tâches lui enverront simplement un code d'alerte, 
et elle se chargera de lire le bon son.*/

/*Le Câblage Matériel (MAX98357A ➡️ ESP32-S3)
L'amplificateur I2S partage les mêmes lignes d'horloge (BCK et WS) que votre microphone INMP441. 
C'est un énorme avantage de l'I2S : les périphériques d'entrée et de sortie partagent les mêmes fils d'horloge, 
seule la broche de données change !
VIN ➡️ 3V3 ou 5V (Le 5V offre plus de puissance au haut-parleur)
GND ➡️ GND
LRCLK / WS (Word Select) ➡️ GPIO 5 (Partagé avec le micro)
BCLK / SCK (Bit Clock) ➡️ GPIO 4 (Partagé avec le micro)
DIN (Data In) ➡️ GPIO 18 (Broche dédiée à la sortie audio)
GAIN ➡️ Non connecté (laisse le gain par défaut)*/

/* FONCTIONNEMENT
Le code utilise une Queue FreeRTOS dédiée pour recevoir les demandes de parole. 
Dès qu'un message arrive, la tâche désactive temporairement l'écoute du micro (pour éviter que l'ESP32 ne s'entende parler lui-même), 
joue le son binaire stocké en Flash (PROGMEM), puis réactive le micro.
(Note : Pour cet exemple, nous simulons les données binaires des fichiers audio audio_hello, audio_time etc. 
En production, vous convertissez de courts fichiers .wav 16kHz Mono en tableaux d'octets C++ à l'aide d'outils gratuits comme xxd ou des convertisseurs en ligne).*/



/*Comment l'appeler depuis vos autres modules (La magie de l'architecture)
Grâce à la fonction globale faireParlerESP32(), 
déclencher la voix depuis n'importe quel endroit de votre projet devient extrêmement simple :
    Dans le setup() de votre main.cpp (Au démarrage) :
        faireParlerESP32(MessageVocal::HELLO);*/


QueueHandle_t queueAudioOut = NULL;


bool initialiserHautParleur() {
    Serial.println("🔊 [AUDIO_OUT] Configuration de l'amplificateur MAX98357A...");
    
    // Le pilote I2S_NUM_0 est déjà installé par le module micro.
    // Il nous suffit d'ajouter la broche de sortie DATA_OUT (data_out_num) au routage existant.
    i2s_pin_config_t pin_config = {
        .bck_io_num   = I2S_PIN_BCK,
        .ws_io_num    = I2S_PIN_WS,
        .data_out_num = I2S_PIN_DATA_OUT,   // On active le fil de sortie
        .data_in_num  = 3                   // Conserve le fil d'entrée du micro (GPIO 3)
    };

    if (i2s_set_pin(I2S_PORT_HP, &pin_config) != ESP_OK) {
        Serial.println("❌ [AUDIO_OUT_ERR] Impossible d'associer la broche du Haut-Parleur !");
        return false;
    }

    Serial.println("✅ [AUDIO_OUT] Haut-Parleur configuré et prêt.");
    return true;
}

void faireParlerESP32(MessageVocal message) {
    if (queueAudioOut != NULL) {
        xQueueSend(queueAudioOut, &message, 0);
    }
}

void rejouerBufferAudio(const uint8_t* audioPtr, size_t taille) {
    size_t octetsEcrit = 0;
    // Envoi par paquets DMA au haut-parleur
    i2s_write(I2S_PORT_HP, audioPtr, taille, &octetsEcrit, portMAX_DELAY);
}

void tacheHautParleur(void * pvParameters) {
    MessageVocal msg;
    Serial.println("🔊 [HP_TASK] Tâche vocale en attente d'événements.");

    while(1) {
        // Attente d'un ordre de parole (0% CPU en veille)
        if (xQueueReceive(queueAudioOut, &msg, portMAX_DELAY) == pdPASS) {
            
            Serial.println("🔊 [HP] Diffusion d'une alerte vocale...");
            
            switch(msg) {
                case MessageVocal::HELLO:
                    Serial.println("🗣️ -> 'Hello'");
                    rejouerBufferAudio(HelloDominique, sizeof(HelloDominique));
                    break;
                    
                case MessageVocal::TELL_TIME:
                    Serial.println("🗣️ -> 'The time is...'");
                    rejouerBufferAudio(audio_time, sizeof(audio_time));
                    // Ici vous enchaîneriez les sons des chiffres correspondants à l'heure courante
                    break;
                    
                case MessageVocal::LOW_BATTERY:
                    Serial.println("🗣️ -> 'Low Battery'");
                    rejouerBufferAudio(audio_lowbat, sizeof(audio_lowbat));
                    break;
                    
                case MessageVocal::SMS_RECEIVED:
                    Serial.println("🗣️ -> 'SMS Received'");
                    rejouerBufferAudio(audio_sms, sizeof(audio_sms));
                    break;
            }
            
            vTaskDelay(pdMS_TO_TICKS(500)); // Pause de transition
        }
    }
}
