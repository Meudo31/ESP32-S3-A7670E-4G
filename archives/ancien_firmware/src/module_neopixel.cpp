#include <math.h>

#include "module_neopixel.h"
#include "module_test_micro.h"   // niveau sonore et détection de voix pour l'animation VU-mètre

/* Les animations sont dessinées dans un tampon (trame), puis envoyées d'un coup aux LED par afficher(),
   qui applique le plafond de luminosité et le budget de courant. */

static Adafruit_NeoPixel panneau(NEOPIXEL_NB_LED, NEOPIXEL_PANNEAU_PIN, NEO_GRB + NEO_KHZ800);

struct Couleur { uint8_t r, g, b; };

static Couleur trame[NEOPIXEL_NB_LED];
static uint8_t indiceAnneau[NEOPIXEL_NB_LED];   // 0 = centre ... NB_ANNEAUX-1 = bord
static float   angleDeg[NEOPIXEL_NB_LED];       // 0° = haut, croissant dans le sens choisi
static float   posX[NEOPIXEL_NB_LED];           // position horizontale normalisée : -1 (gauche) ... +1 (droite)

static volatile AnimationNeoPixel animationCourante = AnimationNeoPixel::VEILLE;

static const float PI_F = 3.14159265f;

// ─── Outils de dessin ───
static inline void poser(uint16_t i, uint8_t r, uint8_t g, uint8_t b) { trame[i].r = r; trame[i].g = g; trame[i].b = b; }

static void toutEteindre() { for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) poser(i, 0, 0, 0); }

static float modulo360(float a) {
    a = fmodf(a, 360.0f);
    return (a < 0) ? a + 360.0f : a;
}

static void poserTeinte(uint16_t i, uint16_t teinte16, uint8_t valeur) {
    uint32_t c = Adafruit_NeoPixel::ColorHSV(teinte16, 255, valeur);
    poser(i, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

static inline float maxAnneau() { return (NEOPIXEL_NB_ANNEAUX > 1) ? (float)(NEOPIXEL_NB_ANNEAUX - 1) : 1.0f; }

// ─── Animations : chacune remplit la trame en fonction de l'instant t (millisecondes) ───
static void animVeille(uint32_t t) {
    float respiration = 0.5f + 0.5f * sinf(t * 2.0f * PI_F / 4000.0f);       // un cycle toutes les 4 s
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        float poids = 1.0f - 0.8f * indiceAnneau[i] / maxAnneau();           // plus faible vers l'extérieur
        uint8_t v = (uint8_t)(6 + 70 * respiration * poids);
        poser(i, 0, v / 3, v);
    }
}

static void animArcEnCiel(uint32_t t) {
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        uint16_t teinte = (uint16_t)(angleDeg[i] / 360.0f * 65535.0f) + (uint16_t)(t * 10) + (uint16_t)(indiceAnneau[i] * 3000);
        poserTeinte(i, teinte, 70);
    }
}

static void animRadar(uint32_t t) {
    float balayage = fmodf(t * 0.12f, 360.0f);                               // un tour en 3 s
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        float retard = modulo360(balayage - angleDeg[i]);                    // distance derrière le faisceau
        float lum = (retard < 120.0f) ? 1.0f - retard / 120.0f : 0.0f;
        uint8_t v = (uint8_t)(lum * lum * 200.0f);
        poser(i, 0, v, v / 6);
    }
    poser(0, 0, 25, 4);                                                      // le centre reste faiblement allumé
}

static void animOndes(uint32_t t) {
    float front = fmodf(t / 110.0f, NEOPIXEL_NB_ANNEAUX + 2.0f);             // anneau atteint par l'onde
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        float d = fabsf((float)indiceAnneau[i] - front);
        float lum = (d < 1.5f) ? 1.0f - d / 1.5f : 0.0f;
        uint8_t v = (uint8_t)(lum * 180.0f);
        poser(i, 0, (uint8_t)(v * 0.6f), v);
    }
}

static void animVuMetre() {
    float niveau = micNumeriqueNiveau();                                     // 0.0 ... 1.0
    float anneauxAllumes = niveau * NEOPIXEL_NB_ANNEAUX;
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        float reste = anneauxAllumes - indiceAnneau[i];
        if (reste <= 0.0f) { poser(i, 0, 0, 2); continue; }
        float lum = (reste > 1.0f) ? 1.0f : reste;
        uint16_t teinte = (uint16_t)(21845.0f * (1.0f - indiceAnneau[i] / maxAnneau()));   // vert au centre, rouge au bord
        poserTeinte(i, teinte, (uint8_t)(130.0f * lum));
    }
}

static void animClignotant(uint32_t t, bool droite) {
    uint32_t phase = t % 1000;
    float p = (phase < 650) ? phase / 650.0f : ((phase < 850) ? 1.1f : -1.0f);   // balayage, plein, éteint
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        float d = droite ? posX[i] : -posX[i];
        bool allume = (p >= 0.0f) && (d >= 0.0f) && (d <= p);
        if (allume) poser(i, 255, 90, 0); else poser(i, 0, 0, 0);
    }
}

static void animAlerte(uint32_t t) {
    uint8_t phase = (t / 150) % 2;
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        if ((indiceAnneau[i] % 2) == phase) poser(i, 255, 0, 0); else poser(i, 0, 0, 0);
    }
}

static void animAppelEntrant(uint32_t t) {
    float front = fmodf(t / 70.0f, NEOPIXEL_NB_ANNEAUX + 1.0f);              // ondes plus rapides que ONDES
    float pouls = 0.5f + 0.5f * sinf(t * 2.0f * PI_F / 500.0f);
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        float d = fabsf((float)indiceAnneau[i] - front);
        float lum = (d < 1.5f) ? 1.0f - d / 1.5f : 0.0f;
        uint8_t v = (uint8_t)(lum * 200.0f);
        poser(i, 0, v, v / 8);
    }
    poser(0, 0, (uint8_t)(80 + 150 * pouls), 20);                            // le cœur pulse
}

static void animAppelEnCours(uint32_t t) {
    float respiration = 0.5f + 0.5f * sinf(t * 2.0f * PI_F / 2000.0f);
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        if (indiceAnneau[i] >= NEOPIXEL_NB_ANNEAUX - 2) poser(i, 0, (uint8_t)(30 + 50 * respiration), 4);
        else poser(i, 0, 0, 0);
    }
}

// ─── Envoi aux LED, avec plafond de luminosité et budget de courant ───
static void afficher() {
    uint32_t somme = 0;
    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) somme += trame[i].r + trame[i].g + trame[i].b;

    float echelle  = NEOPIXEL_NIVEAU_MAX / 255.0f;
    float repos_mA = NEOPIXEL_NB_LED * 1.0f;                                  // environ 1 mA par LED éteinte
    float conso_mA = somme * (20.0f / 255.0f) * echelle;                      // 20 mA par canal à pleine intensité
    float budget   = NEOPIXEL_COURANT_MAX_MA - repos_mA;
    if (conso_mA > budget && conso_mA > 0.0f) echelle *= budget / conso_mA;

    for (uint16_t i = 0; i < NEOPIXEL_NB_LED; i++) {
        panneau.setPixelColor(i, (uint8_t)(trame[i].r * echelle + 0.5f),
                                 (uint8_t)(trame[i].g * echelle + 0.5f),
                                 (uint8_t)(trame[i].b * echelle + 0.5f));
    }
    panneau.show();
}

// ─── API publique ───
bool initialiserNeopixelPanneau() {
    // Position de chaque LED : anneau, angle et coordonnée horizontale.
    uint16_t k = 0;
    for (uint8_t a = 0; a < NEOPIXEL_NB_ANNEAUX; a++) {
        uint8_t n = NEOPIXEL_TAILLE_ANNEAUX[a];
        float rayon = (NEOPIXEL_NB_ANNEAUX > 1) ? (float)a / (NEOPIXEL_NB_ANNEAUX - 1) : 0.0f;
        for (uint8_t j = 0; j < n; j++, k++) {
            float ang = (n == 1) ? 0.0f : 360.0f * j / n;
            ang += NEOPIXEL_DECALAGE_DEG;
            if (!NEOPIXEL_SENS_HORAIRE) ang = 360.0f - ang;
            ang = modulo360(ang);
            indiceAnneau[k] = a;
            angleDeg[k]     = ang;
            posX[k]         = sinf(ang * PI_F / 180.0f) * rayon;
        }
    }

    panneau.begin();
    panneau.setBrightness(255);          // la luminosité est gérée par afficher()
    toutEteindre();
    afficher();
    my_log_printf("[⚙️ SETUP] Panneau NeoPixel prêt : %d LED sur %d anneaux (GPIO %d).", NEOPIXEL_NB_LED, NEOPIXEL_NB_ANNEAUX, NEOPIXEL_PANNEAU_PIN);
    return true;
}

void neopixelDefinirAnimation(AnimationNeoPixel animation) { animationCourante = animation; }

AnimationNeoPixel neopixelAnimationCourante() { return animationCourante; }

const char* neopixelNomAnimation(AnimationNeoPixel animation) {
    switch (animation) {
        case AnimationNeoPixel::ETEINT:             return "éteint";
        case AnimationNeoPixel::VEILLE:             return "veille";
        case AnimationNeoPixel::ARC_EN_CIEL:        return "arc-en-ciel";
        case AnimationNeoPixel::RADAR:              return "radar";
        case AnimationNeoPixel::ONDES:              return "ondes";
        case AnimationNeoPixel::VU_METRE:           return "vu-mètre";
        case AnimationNeoPixel::CLIGNOTANT_GAUCHE:  return "clignotant gauche";
        case AnimationNeoPixel::CLIGNOTANT_DROIT:   return "clignotant droit";
        case AnimationNeoPixel::ALERTE:             return "alerte";
        case AnimationNeoPixel::APPEL_ENTRANT:      return "appel entrant";
        case AnimationNeoPixel::APPEL_EN_COURS:     return "appel en cours";
        default:                                    return "?";
    }
}

void taskNeopixelPanneau(void * pvParameters) {
    AnimationNeoPixel derniere = AnimationNeoPixel::NB_ANIMATIONS;

    for (;;) {
        AnimationNeoPixel a = animationCourante;
        if (NEOPIXEL_REAGIR_VOIX && a == AnimationNeoPixel::VEILLE && micNumeriqueVoixDetectee()) a = AnimationNeoPixel::VU_METRE;

        if (a == AnimationNeoPixel::ETEINT) {                                // rien à redessiner : on économise le CPU
            if (derniere != a) { toutEteindre(); afficher(); derniere = a; }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint32_t t = millis();
        switch (a) {
            case AnimationNeoPixel::VEILLE:             animVeille(t);              break;
            case AnimationNeoPixel::ARC_EN_CIEL:        animArcEnCiel(t);           break;
            case AnimationNeoPixel::RADAR:              animRadar(t);               break;
            case AnimationNeoPixel::ONDES:              animOndes(t);               break;
            case AnimationNeoPixel::VU_METRE:           animVuMetre();              break;
            case AnimationNeoPixel::CLIGNOTANT_GAUCHE:  animClignotant(t, false);   break;
            case AnimationNeoPixel::CLIGNOTANT_DROIT:   animClignotant(t, true);    break;
            case AnimationNeoPixel::ALERTE:             animAlerte(t);              break;
            case AnimationNeoPixel::APPEL_ENTRANT:      animAppelEntrant(t);        break;
            case AnimationNeoPixel::APPEL_EN_COURS:     animAppelEnCours(t);        break;
            default:                                    toutEteindre();             break;
        }
        afficher();
        derniere = a;
        vTaskDelay(pdMS_TO_TICKS(40));                                       // environ 25 images par seconde
    }
}
