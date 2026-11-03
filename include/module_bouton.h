#ifndef MODULE_BOUTON_H
#define MODULE_BOUTON_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE BOUTON : bouton branché entre GPIO 21 et la masse (GND).
   - Appui court : animation NeoPixel suivante.
   - Appui long (plus de 1,5 s) : allume ou éteint la bande.
   L'interruption du bouton réveille la tâche, qui filtre les rebonds du contact.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

bool demarrerModuleBouton();

#endif // MODULE_BOUTON_H
