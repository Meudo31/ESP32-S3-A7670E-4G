#ifndef CONFIGURATION_H
#define CONFIGURATION_H

/* ══════════════════════════════════════════════════════════════════════════════
   CONFIGURATION GÉNÉRALE DU MODULE MOTO
   Tous les réglages qu'on peut avoir envie de changer sont rassemblés ici.
   Les broches qui changent entre la carte V1 et la carte V2 sont dans module_carte.cpp.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

// ─── Version du firmware (affichée dans l'application). À augmenter à chaque nouvelle version. ───
#define VERSION_DU_FIRMWARE "0.4.6"

// ─── Nom du module dans les fichiers (photos) et en Bluetooth ───
#define NOM_DU_MODULE_DANS_LES_FICHIERS "ESP32-A7670"

// ─── Broches ajoutées par Meudo (identiques sur V1 et V2) ───
#define BROCHE_BANDE_NEOPIXELS           2      // bande de 14 LED WS2812B. Pas 41 (HREF de la caméra V2),
                                                // pas 40 (sortie RI du modem A7670E, d'après le brochage Waveshare).
#define NOMBRE_DE_LED_DE_LA_BANDE       14
#define BROCHE_BOUTON_ANIMATIONS        21      // bouton entre la broche et la masse (GND)

// ─── Broches fixes de la carte Waveshare (identiques sur V1 et V2) ───
#define BROCHE_LED_RGB_DE_LA_CARTE      38
#define BROCHE_CARTE_SD_HORLOGE          5      // SDMMC 1 bit
#define BROCHE_CARTE_SD_COMMANDE         4
#define BROCHE_CARTE_SD_DONNEES          6
#define BROCHE_MODEM_A                  17      // UART du modem (le sens TX/RX est détecté au démarrage)
#define BROCHE_MODEM_B                  18
#define VITESSE_UART_MODEM          115200

// ─── Broches facultatives (-1 = non câblée) ───
// Commande d'allumage du modem (PWRKEY). Non documentée : on compte sur l'interrupteur DIP « 4G » sur ON.
#define BROCHE_ALLUMAGE_MODEM           -1
// Mesure de la tension 12 V de la moto par un pont diviseur (ex. 100 kΩ / 22 kΩ) vers une entrée analogique.
#define BROCHE_MESURE_12V               -1
#define RAPPORT_PONT_DIVISEUR_12V       (122.0f / 22.0f)
// Mesure de la présence du 5 V USB par un pont diviseur (ex. 10 kΩ / 10 kΩ).
#define BROCHE_MESURE_USB               -1
#define RAPPORT_PONT_DIVISEUR_USB       2.0f

// ─── Fuseau horaire (France, heure d'été automatique) ───
#define FUSEAU_HORAIRE "CET-1CEST,M3.5.0,M10.5.0/3"

// ─── Répartition des tâches FreeRTOS sur les deux cœurs ───
#define COEUR_COMMUNICATIONS    0       // modem, Bluetooth
#define COEUR_TRAITEMENTS       1       // caméra, carte SD, LED, capteurs

#define PRIORITE_BASSE          1
#define PRIORITE_NORMALE        2
#define PRIORITE_HAUTE          3

// ─── Durées pratiques ───
#define UNE_SECONDE_MS          1000UL
#define UNE_MINUTE_MS           (60UL * UNE_SECONDE_MS)

#endif // CONFIGURATION_H
