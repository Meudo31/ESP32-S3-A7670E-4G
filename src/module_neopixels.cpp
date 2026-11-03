#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <math.h>

#include "configuration.h"
#include "module_neopixels.h"
#include "module_appli.h"
#include "module_haut_parleur.h"
#include "module_journal.h"
#include "module_systeme.h"

namespace {

Adafruit_NeoPixel bande(NOMBRE_DE_LED_DE_LA_BANDE, BROCHE_BANDE_NEOPIXELS, NEO_GRB + NEO_KHZ800);

volatile int  animationEnCours = 0;
volatile int  luminosite       = 80;
volatile bool bandeAllumee     = true;
TaskHandle_t  tacheDesNeopixels = nullptr;

const uint32_t DUREE_D_UNE_IMAGE_MS = 20;   // 50 images par seconde
const int      N = NOMBRE_DE_LED_DE_LA_BANDE;

const char* const NOMS_DES_ANIMATIONS[NOMBRE_D_ANIMATIONS] = {
    "Arc-en-ciel", "Respiration", "Balayage", "Comète", "Feu de camp",
    "Gyrophare", "Étoiles", "Chenillard", "Confettis", "Vague océan",
};

// ─────────────────────────────── Outils de dessin ───────────────────────────────

void allumer(int led, uint8_t rouge, uint8_t vert, uint8_t bleu) { bande.setPixelColor(led, rouge, vert, bleu); }

void allumerEnTeinte(int led, uint16_t teinte, uint8_t intensite = 255) {
    bande.setPixelColor(led, Adafruit_NeoPixel::gamma32(Adafruit_NeoPixel::ColorHSV(teinte, 255, intensite)));
}

void toutEteindre() { bande.clear(); }

// Assombrit chaque LED (traînées des animations).
void assombrirTout(uint8_t pourcentageConserve) {
    for (int led = 0; led < N; led++) {
        const uint32_t couleur = bande.getPixelColor(led);
        const uint8_t rouge = ((couleur >> 16) & 0xFF) * pourcentageConserve / 100;
        const uint8_t vert  = ((couleur >> 8) & 0xFF) * pourcentageConserve / 100;
        const uint8_t bleu  = (couleur & 0xFF) * pourcentageConserve / 100;
        allumer(led, rouge, vert, bleu);
    }
}

// ─────────────────────────────── Les dix animations ───────────────────────────────
// Chaque animation dessine une image en fonction du temps écoulé (en millisecondes).

void animationArcEnCiel(uint32_t temps) {
    for (int led = 0; led < N; led++) allumerEnTeinte(led, (uint16_t)(temps * 12 + led * 65536L / N));
}

void animationRespiration(uint32_t temps) {
    const float respiration = (sinf(temps * 2.0f * PI / 4000.0f) + 1.0f) / 2.0f;   // un souffle toutes les 4 s
    const uint8_t intensite = (uint8_t)(10 + respiration * 245);
    for (int led = 0; led < N; led++) allumer(led, 0, intensite / 3, intensite);
}

void animationBalayage(uint32_t temps) {
    const int aller = (temps / 70) % (2 * (N - 1));
    const int position = aller < N ? aller : 2 * (N - 1) - aller;   // va et vient
    assombrirTout(55);
    allumer(position, 255, 0, 0);
}

void animationComete(uint32_t temps) {
    const int tete = (temps / 50) % (N + 6);
    assombrirTout(70);
    if (tete < N) allumer(tete, 255, 255, 255);
}

void animationFeuDeCamp(uint32_t) {
    for (int led = 0; led < N; led++) {
        const uint8_t flamme = random(120, 256);
        allumer(led, flamme, flamme * random(20, 45) / 100, 0);
    }
}

void animationGyrophare(uint32_t temps) {
    const int phase = (temps / 120) % 8;   // deux éclairs rouges, deux éclairs bleus
    toutEteindre();
    const bool eclair = (phase % 2) == 0;
    if (!eclair) return;
    for (int led = 0; led < N; led++) {
        const bool moitieGauche = led < N / 2;
        if (phase < 4 && moitieGauche)   allumer(led, 255, 0, 0);
        if (phase >= 4 && !moitieGauche) allumer(led, 0, 0, 255);
    }
}

void animationEtoiles(uint32_t) {
    assombrirTout(90);
    if (random(100) < 25) {
        const int led = random(N);
        allumer(led, 255, 255, 180);
    }
}

void animationChenillard(uint32_t temps) {
    const int decalage = (temps / 150) % 3;
    for (int led = 0; led < N; led++) {
        if ((led + decalage) % 3 == 0) allumer(led, 255, 140, 0);
        else allumer(led, 0, 0, 0);
    }
}

void animationConfettis(uint32_t temps) {
    assombrirTout(85);
    if (random(100) < 40) allumerEnTeinte(random(N), (uint16_t)(temps * 20 + random(16384)));
}

void animationVagueOcean(uint32_t temps) {
    for (int led = 0; led < N; led++) {
        const float vague = (sinf(led * 0.6f + temps / 400.0f) + 1.0f) / 2.0f;
        allumer(led, 0, (uint8_t)(60 + vague * 120), (uint8_t)(120 + vague * 135));
    }
}

typedef void (*FonctionDAnimation)(uint32_t temps);
const FonctionDAnimation ANIMATIONS[NOMBRE_D_ANIMATIONS] = {
    animationArcEnCiel, animationRespiration, animationBalayage, animationComete, animationFeuDeCamp,
    animationGyrophare, animationEtoiles,     animationChenillard, animationConfettis, animationVagueOcean,
};

// ─────────────────────────────── Mémorisation et annonce ───────────────────────────────

void memoriserLesReglages() {
    Preferences memoirePermanente;
    memoirePermanente.begin("neopixels", false);
    memoirePermanente.putInt("animation", animationEnCours);
    memoirePermanente.putInt("luminosite", luminosite);
    memoirePermanente.end();
}

void annoncerLeChangement() {
    const char* nom = nomDeLAnimation(animationEnCours);
    ecrireDansLeJournal("[NEOPIXELS] Animation %d : %s.", animationEnCours + 1, nom);
    annoncerAuHautParleur(nom);
    JsonDocument evenement;
    decrireLesNeopixels(evenement);
    publierUnEvenementVersLAppli(evenement);
}

void tacheNeopixels(void*) {
    uint32_t debutDeLAnimation = millis();
    int animationAffichee = -1;
    for (;;) {
        if (animationAffichee != animationEnCours) {
            animationAffichee = animationEnCours;
            debutDeLAnimation = millis();
            toutEteindre();
        }
        bande.setBrightness(bandeAllumee ? luminosite : 0);
        ANIMATIONS[animationAffichee](millis() - debutDeLAnimation);
        bande.show();
        vTaskDelay(pdMS_TO_TICKS(DUREE_D_UNE_IMAGE_MS));
    }
}

} // namespace

int numeroDeLAnimationEnCours() { return animationEnCours; }

const char* nomDeLAnimation(int numero) {
    return (numero >= 0 && numero < NOMBRE_D_ANIMATIONS) ? NOMS_DES_ANIMATIONS[numero] : "?";
}

void choisirLAnimation(int numero) {
    if (numero < 0 || numero >= NOMBRE_D_ANIMATIONS) return;
    animationEnCours = numero;
    bandeAllumee = true;
    memoriserLesReglages();
    annoncerLeChangement();
}

void passerALAnimationSuivante() { choisirLAnimation((animationEnCours + 1) % NOMBRE_D_ANIMATIONS); }

void reglerLaLuminositeDesNeopixels(int nouvelleLuminosite) {
    luminosite = constrain(nouvelleLuminosite, 0, 255);
    memoriserLesReglages();
}

void allumerOuEteindreLesNeopixels() {
    bandeAllumee = !bandeAllumee;
    ecrireDansLeJournal("[NEOPIXELS] Bande %s.", bandeAllumee ? "allumée" : "éteinte");
    JsonDocument evenement;
    decrireLesNeopixels(evenement);
    publierUnEvenementVersLAppli(evenement);
}

void decrireLesNeopixels(JsonDocument& document) {
    document["t"]       = "neo";
    document["num"]     = animationEnCours;
    document["nom"]     = nomDeLAnimation(animationEnCours);
    document["lum"]     = luminosite;
    document["allumee"] = bandeAllumee ? 1 : 0;
    JsonArray noms = document["noms"].to<JsonArray>();
    for (int numero = 0; numero < NOMBRE_D_ANIMATIONS; numero++) noms.add(NOMS_DES_ANIMATIONS[numero]);
}

bool demarrerModuleNeopixels() {
    Preferences memoirePermanente;
    memoirePermanente.begin("neopixels", true);
    animationEnCours = constrain(memoirePermanente.getInt("animation", 0), 0, NOMBRE_D_ANIMATIONS - 1);
    luminosite       = constrain(memoirePermanente.getInt("luminosite", 80), 0, 255);
    memoirePermanente.end();

    bande.begin();
    bande.clear();
    bande.show();
    ecrireDansLeJournal("[NEOPIXELS] %d LED sur GPIO %d, animation « %s ».", N, BROCHE_BANDE_NEOPIXELS,
                        nomDeLAnimation(animationEnCours));
    return creerTacheSurveillee(tacheNeopixels, "NeoPixels", 3072, PRIORITE_BASSE, COEUR_TRAITEMENTS, &tacheDesNeopixels);
}
