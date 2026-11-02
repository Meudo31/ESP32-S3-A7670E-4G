#ifndef CONFIG_MICRO_H
#define CONFIG_MICRO_H
#include "main.h"

/*📋 Connexions physiques Micro (INMP441 ➡️ ESP32-S3)
    VDD ➡️ 3V3 (Alimentation 3.3 Volts)
    GND ➡️ GND (Masse)
    SD (Serial Data) ➡️ GPIO 42 (Ligne de données audio entrantes)
    SCK / BCK (Bit Clock) ➡️ GPIO 1 (Horloge de cadencement des bits)
    WS (Word Select) ➡️ GPIO 2 (Horloge de synchronisation des mots)
    L/R (Left/Right) ➡️ GND (Sélectionne le canal gauche pour forcer le micro en mode Mono)*/


/*💡 Rappel important pour votre montage :
    La broche L/R du microphone doit obligatoirement être reliée à la masse (GND). 
    C'est ce qui indique au micro qu'il travaille seul en mode Mono (Canal Gauche / Left). 
    Dans le cas contraire, le pilote I2S configuré sur I2S_CHANNEL_FMT_ONLY_LEFT lira un signal vide.*/
    
#define I2S_PIN_BCK 1
#define I2S_PIN_WS  2
#define I2S_PIN_SD  42
#define I2S_PORT    I2S_NUM_0

enum CommandesVocales {
    PRENDRE_PHOTO = 1,
    DONNER_HEURE  = 2,
    ENVOYER_ALERT_GPS = 3
};



bool initialiserMicrophone();
void tacheEnregistrementMicro(void * pvParameters);

#endif