#ifndef CONFIG_TEST_MICRO_H
#define CONFIG_TEST_MICRO_H
#include "main.h"
#include "freertos/stream_buffer.h"

/* ───────────── MICRO NUMÉRIQUE INMP441 (I2S) ─────────────
   Sert à la reconnaissance vocale et aux traitements audio à venir.
   (Le micro analogique, lui, est branché sur la borne MIC du modem : il ne sert qu'aux appels, voir module_appel.)

   Câblage :
     VDD ➡️ 3V3          GND ➡️ GND
     L/R ➡️ GND          (canal gauche, mode mono)
     SCK ➡️ GPIO 1       (horloge de bits)
     WS  ➡️ GPIO 2       (horloge de mots)
     SD  ➡️ GPIO 42      (données audio, sortie du micro)
   Ces trois GPIO étaient pris par le gyroscope et le bus CAN, retirés du programme principal.
   Le GPIO 45 (utilisé avant pour SD) est une broche de démarrage de l'ESP32-S3 : on l'évite. */
#define I2S_PIN_BCK 1
#define I2S_PIN_WS  2
#define I2S_PIN_SD  42

#define MICRO_FREQ_ECHANTILLONNAGE  16000   // 16 kHz mono, format attendu par la plupart des moteurs de reconnaissance
#define MICRO_DECALAGE_BITS         14      // 32 bits ➡️ 16 bits : valeur plus faible = micro plus sensible (saturation écrêtée)
#define MICRO_SEUIL_MIN_RMS         80.0f   // niveau minimal (échelle 16 bits) pour parler de voix
#define MICRO_FACTEUR_VOIX          3.0f    // la voix doit dépasser le bruit de fond d'autant de fois
#define MICRO_TAILLE_FLUX_OCTETS    16384   // tampon audio disponible pour les consommateurs (~0,5 s)
#define MICRO_VUMETRE_SERIE         0       // 1 : affiche un VU-mètre dans le moniteur série (debug)

bool initialiserTestMicrophone();
void tacheTestMicro(void * pvParameters);

// ─── API pour les autres modules ───
float  micNumeriqueNiveau();                 // volume lissé de 0.0 (silence) à 1.0 (très fort), échelle logarithmique
bool   micNumeriqueVoixDetectee();           // true tant qu'une voix est détectée (avec maintien d'environ 0,5 s)
void   micNumeriqueActiverFlux(bool actif);  // active/désactive la copie de l'audio dans le flux ci-dessous
size_t micNumeriqueLire(int16_t* destination, size_t nbEchantillons, TickType_t attente);   // lit des échantillons 16 bits mono

#endif
