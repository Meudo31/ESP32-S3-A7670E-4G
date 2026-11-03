#ifndef CONFIG_NEOPIXEL_H
#define CONFIG_NEOPIXEL_H
#include "main.h"

/* ───────────── PANNEAU DE NEOPIXELS (WS2812B, disque à anneaux concentriques) ─────────────
   Câblage :
     DIN  ➡️ GPIO 14 (via une résistance de 330 Ω en série, au plus près de la première LED)
     GND  ➡️ GND de la carte ET GND de l'alimentation du panneau (masses reliées ensemble)
     5V   ➡️ alimentation 5 V DÉDIÉE (pas la carte !). 133 LED peuvent tirer jusqu'à 8 A à pleine puissance.
             Ajouter un condensateur de 1000 µF entre 5V et GND à l'entrée du panneau.
   Signal : le GPIO sort du 3,3 V alors que les LED sont alimentées en 5 V. Ça fonctionne souvent tel quel,
            mais un adaptateur de niveau (74AHCT125 ou 74HCT245) rend la liaison fiable.

   Le code limite lui-même la consommation (NEOPIXEL_COURANT_MAX_MA), quelle que soit l'animation. */

#define NEOPIXEL_PANNEAU_PIN     14

// Nombre de LED par anneau, du centre vers l'extérieur, dans l'ordre de chaînage des données.
// À VÉRIFIER sur ton panneau : le code s'adapte tout seul à cette liste (nombre d'anneaux et de LED).
static constexpr uint8_t NEOPIXEL_TAILLE_ANNEAUX[] = { 1, 8, 12, 16, 24, 32, 40 };
static constexpr uint8_t NEOPIXEL_NB_ANNEAUX       = sizeof(NEOPIXEL_TAILLE_ANNEAUX);
constexpr uint16_t neopixelSommeLED(uint8_t i = 0) {
    return (i >= NEOPIXEL_NB_ANNEAUX) ? 0 : (uint16_t)(NEOPIXEL_TAILLE_ANNEAUX[i] + neopixelSommeLED(i + 1));
}
static constexpr uint16_t NEOPIXEL_NB_LED = neopixelSommeLED();

// Orientation : angle de la première LED de chaque anneau (0° = en haut) et sens de numérotation.
#define NEOPIXEL_DECALAGE_DEG    0.0f
#define NEOPIXEL_SENS_HORAIRE    true

// Réglages d'intensité.
#define NEOPIXEL_NIVEAU_MAX      160     // plafond global de luminosité (0-255)
#define NEOPIXEL_COURANT_MAX_MA  1500    // budget de courant estimé : l'éclairage est réduit au-delà
#define NEOPIXEL_REAGIR_VOIX     true    // en veille, le panneau devient un VU-mètre quand une voix est détectée

enum class AnimationNeoPixel : uint8_t {
    ETEINT,              // tout éteint
    VEILLE,              // respiration bleue lente, plus vive au centre
    ARC_EN_CIEL,         // arc-en-ciel qui tourne
    RADAR,               // faisceau vert qui balaie le disque
    ONDES,               // ondes cyan qui partent du centre
    VU_METRE,            // anneaux allumés selon le volume capté par le micro INMP441
    CLIGNOTANT_GAUCHE,   // balayage orange vers la gauche
    CLIGNOTANT_DROIT,    // balayage orange vers la droite
    ALERTE,              // flash rouge alterné entre anneaux pairs et impairs
    APPEL_ENTRANT,       // ondes vertes rapides + cœur qui pulse
    APPEL_EN_COURS,      // anneau extérieur vert qui respire
    NB_ANIMATIONS
};

bool              initialiserNeopixelPanneau();
void              neopixelDefinirAnimation(AnimationNeoPixel animation);
AnimationNeoPixel neopixelAnimationCourante();
const char*       neopixelNomAnimation(AnimationNeoPixel animation);

void taskNeopixelPanneau(void * pvParameters);

#endif // CONFIG_NEOPIXEL_H
