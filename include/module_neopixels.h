#ifndef MODULE_NEOPIXELS_H
#define MODULE_NEOPIXELS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE NEOPIXELS : bande de 14 LED WS2812B branchée sur GPIO 2 (BROCHE_BANDE_NEOPIXELS).
   Treize animations au choix, dont « État des modules » (un voyant par module)
   et « Toutes LED éteintes ». On change d'animation avec le bouton (GPIO 21) ou
   depuis l'application. À chaque changement :
     - le nom de l'animation est prononcé sur le haut-parleur ;
     - l'application reçoit le nom de l'animation en cours.
   L'animation choisie et la luminosité sont mémorisées en flash.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

#define NOMBRE_D_ANIMATIONS 13

int  numeroDeLAnimationEnCours();
const char* nomDeLAnimation(int numero);

void passerALAnimationSuivante();
void choisirLAnimation(int numero);

// Luminosité de 0 (éteint) à 255.
void reglerLaLuminositeDesNeopixels(int luminosite);
void allumerOuEteindreLesNeopixels();

void decrireLesNeopixels(JsonDocument& document);

bool demarrerModuleNeopixels();

#endif // MODULE_NEOPIXELS_H
