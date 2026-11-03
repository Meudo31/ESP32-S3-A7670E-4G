#ifndef MODULE_CARTE_H
#define MODULE_CARTE_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE CARTE : reconnaît la version de la carte Waveshare (V1 ou V2)
   et donne les broches qui changent entre les deux versions.

   Différences documentées par Waveshare (page « Arduino » de la carte) :

        Élément                     V1          V2
        Jauge batterie I2C SDA      GPIO 3      GPIO 15
        Jauge batterie I2C SCL      GPIO 2      GPIO 16
        Caméra XCLK                 GPIO 34     GPIO 39
        Caméra VSYNC                GPIO 36     GPIO 42
        Caméra HREF                 GPIO 35     GPIO 41   ⚠️ même broche que la bande NeoPixel !
        Caméra PCLK                 GPIO 37     GPIO 46

   Méthode de détection, dans l'ordre :
     1. On cherche la jauge de batterie MAX17048 (adresse I2C 0x36) sur GPIO 15/16 → V2.
     2. Sinon on la cherche sur GPIO 3/2 → V1.
     3. Sinon on regarde la PSRAM : la V2 utilise une puce ESP32-S3R8 (8 Mo, qui occupe
        les GPIO 33 à 37) ; la V1 utilise ces GPIO pour la caméra, donc une puce à 2 Mo.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

enum class VersionDeCarte { INCONNUE, V1, V2 };

struct BrochesDeLaCarte {
    int jaugeSDA, jaugeSCL;
    int cameraXCLK, cameraSIOD, cameraSIOC;
    int cameraY2, cameraY3, cameraY4, cameraY5, cameraY6, cameraY7, cameraY8, cameraY9;
    int cameraVSYNC, cameraHREF, cameraPCLK;
    bool cameraPartageLeBusI2CDeLaJauge;   // V2 : caméra et jauge sur les mêmes fils
};

// Détecte la version de la carte. À appeler une seule fois, au tout début du setup().
VersionDeCarte detecterLaVersionDeLaCarte();

VersionDeCarte versionDeLaCarte();
const char* nomDeLaVersionDeCarte();          // "V1", "V2" ou "inconnue"
const char* methodeDeDetectionDeLaCarte();    // explication pour l'application
const BrochesDeLaCarte& brochesDeLaCarte();

#endif // MODULE_CARTE_H
