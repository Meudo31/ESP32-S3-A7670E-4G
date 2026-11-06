#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <math.h>

#include "configuration.h"
#include "module_neopixels.h"
#include "module_appli.h"
#include "module_batterie.h"
#include "module_camera.h"
#include "module_carte_sd.h"
#include "module_gps.h"
#include "module_modem.h"
#include "module_reseau_4g.h"
#include "module_haut_parleur.h"
#include "module_journal.h"
#include "module_carte.h"
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
    "Chenillard bleu", "État des modules", "Toutes LED éteintes",
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

// ─────────────────────────────── Les animations ─────────────────────────────────
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

// ─────────────────────────────── Chenillard bleu ───────────────────────────────
// Toutes les 1/2 seconde, la LED suivante lance un bref éclair bleu.

const uint32_t PERIODE_DU_CHENILLARD_BLEU_MS = 500;
const uint32_t DUREE_D_UN_ECLAIR_MS          = 120;

void dessinerLeChenillardBleu(uint32_t temps, int premiereLed, int derniereLed) {
    const int nombreDeLed = derniereLed - premiereLed + 1;
    const int ledDuMoment = premiereLed + (temps / PERIODE_DU_CHENILLARD_BLEU_MS) % nombreDeLed;
    const bool eclair = (temps % PERIODE_DU_CHENILLARD_BLEU_MS) < DUREE_D_UN_ECLAIR_MS;
    for (int led = premiereLed; led <= derniereLed; led++) allumer(led, 0, 0, 0);
    if (eclair) allumer(ledDuMoment, 0, 0, 255);
}

void animationChenillardBleu(uint32_t temps) { dessinerLeChenillardBleu(temps, 0, N - 1); }

// ─────────────────────────────── État des modules ───────────────────────────────
// Les premières LED montrent chacune un module, dans sa propre couleur :
//   allumée fixe = le module fonctionne ; bref éclair toutes les 1/2 seconde = il ne fonctionne pas.
// Les LED suivantes font le chenillard bleu.

struct VoyantDUnModule {
    const char* nom;
    uint8_t rouge, vert, bleu;
};

const VoyantDUnModule VOYANTS[] = {
    { "Carte SD",            255, 255, 255 },   // LED 1 : blanc
    { "Modem 4G",            255, 180,   0 },   // LED 2 : jaune
    { "Réseau 4G",             0, 255, 255 },   // LED 3 : cyan
    { "GPS (position)",        0, 255,   0 },   // LED 4 : vert
    { "Batterie",            255,  80,   0 },   // LED 5 : orange
    { "Caméra",              160,   0, 255 },   // LED 6 : violet
    { "Appli connectée",     255,   0, 120 },   // LED 7 : rose
};
const int NOMBRE_DE_VOYANTS = sizeof(VOYANTS) / sizeof(VOYANTS[0]);

bool modulesQuiFonctionnent[NOMBRE_DE_VOYANTS] = {};
uint32_t derniereVerificationDesModules = 0;

void verifierLesModules() {
    const EtatDuReseau4G reseau = etatDuReseau4G();
    const EtatDeLaBatterie batterie = etatDeLaBatterie();
    modulesQuiFonctionnent[0] = carteSDMontee();
    modulesQuiFonctionnent[1] = leModemEstPret();
    modulesQuiFonctionnent[2] = reseau.enregistrement == 1 || reseau.enregistrement == 5;   // inscrit (ou itinérance)
    modulesQuiFonctionnent[3] = positionGPS().positionValide;
    modulesQuiFonctionnent[4] = batterie.jaugePresente && batterie.chargePourcent > 15;
    modulesQuiFonctionnent[5] = cameraDisponible();
    modulesQuiFonctionnent[6] = appliConnectee();
}

void animationEtatDesModules(uint32_t temps) {
    // L'état des modules est relu deux fois par seconde (inutile à chaque image).
    if (millis() - derniereVerificationDesModules >= 500) {
        derniereVerificationDesModules = millis();
        verifierLesModules();
    }
    const bool eclair = (temps % PERIODE_DU_CHENILLARD_BLEU_MS) < DUREE_D_UN_ECLAIR_MS;
    for (int numero = 0; numero < NOMBRE_DE_VOYANTS && numero < N; numero++) {
        const VoyantDUnModule& voyant = VOYANTS[numero];
        if (modulesQuiFonctionnent[numero] || eclair) allumer(numero, voyant.rouge, voyant.vert, voyant.bleu);
        else allumer(numero, 0, 0, 0);
    }
    if (NOMBRE_DE_VOYANTS < N) dessinerLeChenillardBleu(temps, NOMBRE_DE_VOYANTS, N - 1);
}

void animationToutesEteintes(uint32_t) { toutEteindre(); }

typedef void (*FonctionDAnimation)(uint32_t temps);
const FonctionDAnimation ANIMATIONS[NOMBRE_D_ANIMATIONS] = {
    animationArcEnCiel, animationRespiration, animationBalayage, animationComete, animationFeuDeCamp,
    animationGyrophare, animationEtoiles,     animationChenillard, animationConfettis, animationVagueOcean,
    animationChenillardBleu, animationEtatDesModules, animationToutesEteintes,
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
    // Sur la carte V1, le GPIO 2 est l'horloge I2C de la jauge de batterie : on ne pilote pas la bande dessus.
    const BrochesDeLaCarte& broches = brochesDeLaCarte();
    if (versionDeLaCarte() == VersionDeCarte::V1
        && (BROCHE_BANDE_NEOPIXELS == broches.jaugeSDA || BROCHE_BANDE_NEOPIXELS == broches.jaugeSCL)) {
        ecrireDansLeJournal("[NEOPIXELS] GPIO %d utilisé par la jauge de batterie sur la carte V1 : bande désactivée.",
                            BROCHE_BANDE_NEOPIXELS);
        return false;
    }
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
