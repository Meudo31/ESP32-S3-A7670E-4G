#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_camera.h>

#include "configuration.h"
#include "module_camera.h"
#include "module_appli.h"
#include "module_carte.h"
#include "module_carte_sd.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_systeme.h"

namespace {

TaskHandle_t  tacheDeLaCamera = nullptr;
SemaphoreHandle_t verrouDeLaCamera = nullptr;   // un seul accès au capteur à la fois (photo ou réglage)
volatile bool cameraPrete     = false;
const char*   etatActuel      = "pas encore démarrée";
uint32_t      numeroDePhotoSansDate = 0;

// ─────────────────────────────── Réglages de l'image ───────────────────────────────
// Chaque réglage : son nom (dans les commandes, le message « camera » et la mémoire permanente),
// ses limites, sa valeur par défaut et la fonction du capteur qui l'applique.
// Les valeurs sont celles de la bibliothèque esp32-camera (capteur OV2640 de la carte Waveshare).

typedef int (*FonctionDuCapteur)(sensor_t* capteur, int valeur);

struct ReglageDeLaCamera {
    const char*       nom;         // 15 caractères au plus (clé de la mémoire permanente)
    int               minimum;
    int               maximum;     // pour « taille », remplacé par tailleMaximum()
    int               parDefaut;   // pour « taille » et « qualite », dépend de la PSRAM
    FonctionDuCapteur appliquer;   // nullptr : réglage utilisé par le firmware lui-même
};

#define FONCTION_DU_CAPTEUR(fonction, type) \
    [](sensor_t* c, int v) -> int { return c->fonction ? c->fonction(c, (type)v) : -1; }

const ReglageDeLaCamera REGLAGES[] = {
    // Image
    { "taille",    0, FRAMESIZE_UXGA, -1, FONCTION_DU_CAPTEUR(set_framesize, framesize_t) },
    { "qualite",   4, 63,   -1, FONCTION_DU_CAPTEUR(set_quality, int) },        // 4 = meilleure, 63 = plus faible
    { "lum",      -2, 2,     0, FONCTION_DU_CAPTEUR(set_brightness, int) },
    { "contraste",-2, 2,     0, FONCTION_DU_CAPTEUR(set_contrast, int) },
    { "satur",    -2, 2,     0, FONCTION_DU_CAPTEUR(set_saturation, int) },
    { "nettete",  -2, 2,     0, FONCTION_DU_CAPTEUR(set_sharpness, int) },      // pas sur l'OV2640
    { "debruit",   0, 8,     0, FONCTION_DU_CAPTEUR(set_denoise, int) },        // pas sur l'OV2640
    { "effet",     0, 6,     0, FONCTION_DU_CAPTEUR(set_special_effect, int) }, // 0 aucun, 1 négatif, 2 N&B, 3 rouge, 4 vert, 5 bleu, 6 sépia
    // Balance des blancs
    { "awb",       0, 1,     1, FONCTION_DU_CAPTEUR(set_whitebal, int) },
    { "awb_gain",  0, 1,     1, FONCTION_DU_CAPTEUR(set_awb_gain, int) },
    { "wb_mode",   0, 4,     0, FONCTION_DU_CAPTEUR(set_wb_mode, int) },        // 0 auto, 1 soleil, 2 nuageux, 3 bureau, 4 maison
    // Exposition
    { "aec",       0, 1,     1, FONCTION_DU_CAPTEUR(set_exposure_ctrl, int) },
    { "aec2",      0, 1,     0, FONCTION_DU_CAPTEUR(set_aec2, int) },
    { "ae_niv",   -2, 2,     0, FONCTION_DU_CAPTEUR(set_ae_level, int) },
    { "aec_val",   0, 1200, 300, FONCTION_DU_CAPTEUR(set_aec_value, int) },     // exposition manuelle (aec = 0)
    // Gain
    { "agc",       0, 1,     1, FONCTION_DU_CAPTEUR(set_gain_ctrl, int) },
    { "agc_gain",  0, 30,    0, FONCTION_DU_CAPTEUR(set_agc_gain, int) },       // gain manuel (agc = 0) : 1x à 31x
    { "gain_max",  0, 6,     0, FONCTION_DU_CAPTEUR(set_gainceiling, gainceiling_t) }, // plafond auto : 2x à 128x
    // Corrections
    { "bpc",       0, 1,     0, FONCTION_DU_CAPTEUR(set_bpc, int) },            // pixels noirs
    { "wpc",       0, 1,     1, FONCTION_DU_CAPTEUR(set_wpc, int) },            // pixels blancs
    { "gamma",     0, 1,     1, FONCTION_DU_CAPTEUR(set_raw_gma, int) },
    { "lenc",      0, 1,     1, FONCTION_DU_CAPTEUR(set_lenc, int) },           // correction de l'objectif
    { "dcw",       0, 1,     1, FONCTION_DU_CAPTEUR(set_dcw, int) },            // réduction de taille (downsize)
    // Orientation et test
    { "miroir",    0, 1,     0, FONCTION_DU_CAPTEUR(set_hmirror, int) },
    { "retourne",  0, 1,     0, FONCTION_DU_CAPTEUR(set_vflip, int) },
    { "mire",      0, 1,     0, FONCTION_DU_CAPTEUR(set_colorbar, int) },       // barres de couleur de test
    // Prise de vue (firmware)
    { "jeter",     0, 10,    1, nullptr },   // images jetées avant la photo (laisse l'exposition s'ajuster)
};
const int NOMBRE_DE_REGLAGES = sizeof(REGLAGES) / sizeof(REGLAGES[0]);

const char* ESPACE_DE_LA_MEMOIRE = "camera";

int  valeursDesReglages[NOMBRE_DE_REGLAGES];
bool reglageNonPrisEnCharge[NOMBRE_DE_REGLAGES];   // le capteur a refusé ce réglage

int tailleMaximum() { return psramFound() ? FRAMESIZE_UXGA : FRAMESIZE_SVGA; }

int maximumDuReglage(int numero) { return strcmp(REGLAGES[numero].nom, "taille") == 0 ? tailleMaximum() : REGLAGES[numero].maximum; }

int valeurParDefaut(int numero) {
    const char* nom = REGLAGES[numero].nom;
    if (strcmp(nom, "taille") == 0)  return psramFound() ? FRAMESIZE_UXGA : FRAMESIZE_SVGA;   // 1600 x 1200 ou 800 x 600
    if (strcmp(nom, "qualite") == 0) return psramFound() ? 10 : 12;
    return REGLAGES[numero].parDefaut;
}

int numeroDuReglage(const char* nom) {
    for (int numero = 0; numero < NOMBRE_DE_REGLAGES; numero++) if (strcasecmp(REGLAGES[numero].nom, nom) == 0) return numero;
    return -1;
}

int valeurDuReglage(const char* nom) {
    const int numero = numeroDuReglage(nom);
    return numero < 0 ? 0 : valeursDesReglages[numero];
}

void lireLesReglagesMemorises() {
    Preferences memoirePermanente;
    const bool ouverte = memoirePermanente.begin(ESPACE_DE_LA_MEMOIRE, true);
    for (int numero = 0; numero < NOMBRE_DE_REGLAGES; numero++) {
        const int valeur = ouverte ? memoirePermanente.getInt(REGLAGES[numero].nom, valeurParDefaut(numero)) : valeurParDefaut(numero);
        valeursDesReglages[numero] = constrain(valeur, REGLAGES[numero].minimum, maximumDuReglage(numero));
    }
    if (ouverte) memoirePermanente.end();
}

void memoriserUnReglage(int numero) {
    Preferences memoirePermanente;
    memoirePermanente.begin(ESPACE_DE_LA_MEMOIRE, false);
    memoirePermanente.putInt(REGLAGES[numero].nom, valeursDesReglages[numero]);
    memoirePermanente.end();
}

// Envoie un réglage au capteur (le verrou de la caméra doit être pris). Renvoie false si le capteur refuse.
bool appliquerUnReglage(sensor_t* capteur, int numero) {
    if (REGLAGES[numero].appliquer == nullptr) return true;
    const bool accepte = REGLAGES[numero].appliquer(capteur, valeursDesReglages[numero]) == 0;
    reglageNonPrisEnCharge[numero] = !accepte;
    return accepte;
}

void appliquerTousLesReglages() {
    sensor_t* capteur = esp_camera_sensor_get();
    if (capteur == nullptr) return;
    int refuses = 0;
    for (int numero = 0; numero < NOMBRE_DE_REGLAGES; numero++) if (!appliquerUnReglage(capteur, numero)) refuses++;
    ecrireDansLeJournal("[CAMERA] Réglages appliqués (%d non pris en charge par le capteur).", refuses);
}

const char* nomDuCapteur() {
    sensor_t* capteur = cameraPrete ? esp_camera_sensor_get() : nullptr;
    if (capteur == nullptr) return "";
    switch (capteur->id.PID) {
        case 0x26:   return "OV2640";
        case 0x77:   return "OV7725";
        case 0x7673: return "OV7670";
        case 0x3660: return "OV3660";
        case 0x5640: return "OV5640";
        case 0x9b:   return "GC2145";
        default:     return "inconnu";
    }
}

void publierLesReglages() {
    JsonDocument message;
    decrireLesReglagesDeLaCamera(message);
    publierUnEvenementVersLAppli(message);
}

bool lesBrochesSontLibres() {
    if (brochesDeLaCarte().cameraHREF == BROCHE_BANDE_NEOPIXELS) {
        etatActuel = "désactivée : la bande NeoPixel est sur la broche HREF de la caméra (déplacer la bande sur un GPIO libre)";
        return false;
    }
    return true;
}

bool initialiserLaCamera() {
    const BrochesDeLaCarte& broches = brochesDeLaCarte();
    camera_config_t reglages = {};
    reglages.pin_pwdn     = -1;
    reglages.pin_reset    = -1;
    reglages.pin_xclk     = broches.cameraXCLK;
    reglages.pin_d0       = broches.cameraY2;
    reglages.pin_d1       = broches.cameraY3;
    reglages.pin_d2       = broches.cameraY4;
    reglages.pin_d3       = broches.cameraY5;
    reglages.pin_d4       = broches.cameraY6;
    reglages.pin_d5       = broches.cameraY7;
    reglages.pin_d6       = broches.cameraY8;
    reglages.pin_d7       = broches.cameraY9;
    reglages.pin_vsync    = broches.cameraVSYNC;
    reglages.pin_href     = broches.cameraHREF;
    reglages.pin_pclk     = broches.cameraPCLK;
    if (broches.cameraPartageLeBusI2CDeLaJauge) {
        // V2 : la caméra se règle par le même bus I2C que la jauge de batterie, déjà ouvert par Wire (port 0).
        reglages.pin_sccb_sda  = -1;
        reglages.pin_sccb_scl  = -1;
        reglages.sccb_i2c_port = 0;
    } else {
        reglages.pin_sccb_sda  = broches.cameraSIOD;
        reglages.pin_sccb_scl  = broches.cameraSIOC;
    }
    reglages.xclk_freq_hz = 20000000;
    reglages.ledc_timer   = LEDC_TIMER_0;
    reglages.ledc_channel = LEDC_CHANNEL_0;
    reglages.pixel_format = PIXFORMAT_JPEG;
    reglages.grab_mode    = CAMERA_GRAB_LATEST;
    // Les mémoires d'image sont prévues pour la plus grande taille : on peut ensuite la réduire à volonté.
    reglages.frame_size   = (framesize_t)tailleMaximum();   // 1600 x 1200 avec PSRAM, sinon 800 x 600
    reglages.jpeg_quality = valeurDuReglage("qualite");     // 4 = meilleure qualité, 63 = la plus faible
    if (psramFound()) {
        reglages.fb_count     = 2;
        reglages.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        reglages.fb_count     = 1;
        reglages.fb_location  = CAMERA_FB_IN_DRAM;
    }

    const esp_err_t erreur = esp_camera_init(&reglages);
    if (erreur != ESP_OK) {
        ecrireDansLeJournal("[CAMERA] Caméra introuvable (erreur 0x%x). Interrupteur DIP « CAM » sur ON ?", erreur);
        etatActuel = "absente ou non alimentée (interrupteur DIP CAM ?)";
        return false;
    }
    etatActuel = "prête";
    ecrireDansLeJournal("[CAMERA] Caméra prête (capteur 0x%x).", esp_camera_sensor_get()->id.PID);
    appliquerTousLesReglages();
    return true;
}

String cheminDeLaNouvellePhoto() {
    if (lHeureEstConnue()) {
        return String(DOSSIER_DES_PHOTOS) + "/" + dateDuJourPourNomDeFichier() + "/"
             + dateEtHeurePourNomDeFichier() + "_" + NOM_DU_MODULE_DANS_LES_FICHIERS + ".jpg";
    }
    // Heure encore inconnue : on numérote les photos pour ne rien écraser.
    numeroDePhotoSansDate++;
    return String(DOSSIER_DES_PHOTOS) + "/sans-date/demarrage-" + nombreDeDemarrages() + "_photo-"
         + numeroDePhotoSansDate + "_" + NOM_DU_MODULE_DANS_LES_FICHIERS + ".jpg";
}

void prendreEtEnregistrerUnePhoto() {
    JsonDocument resultat;
    resultat["t"] = "photo";

    if (!cameraPrete) {
        resultat["ok"]  = false;
        resultat["msg"] = String("Caméra ") + etatActuel;
    } else if (!carteSDMontee()) {
        resultat["ok"]  = false;
        resultat["msg"] = "Pas de carte SD";
    } else {
        // Les premières images peuvent dater d'avant la demande (ou d'avant un réglage) : on les jette.
        // Le réglage « jeter » permet d'en jeter plus, pour laisser l'exposition s'ajuster.
        for (int jetee = 0; jetee < valeurDuReglage("jeter"); jetee++) {
            camera_fb_t* ancienne = esp_camera_fb_get();
            if (ancienne) esp_camera_fb_return(ancienne);
        }
        camera_fb_t* image = esp_camera_fb_get();

        if (image == nullptr) {
            resultat["ok"]  = false;
            resultat["msg"] = "La caméra n'a pas renvoyé d'image";
        } else {
            const String chemin = cheminDeLaNouvellePhoto();
            const bool enregistree = ecrireUnFichier(chemin.c_str(), image->buf, image->len);
            resultat["ok"]     = enregistree;
            resultat["chemin"] = chemin;
            resultat["taille"] = image->len;
            resultat["msg"]    = enregistree ? "Photo enregistrée" : "Échec d'écriture sur la carte SD";
            ecrireDansLeJournal("[CAMERA] Photo %s : %s (%u octets).", enregistree ? "enregistrée" : "NON enregistrée",
                                chemin.c_str(), (unsigned)image->len);
            esp_camera_fb_return(image);
        }
    }
    publierUnEvenementVersLAppli(resultat);
}

void tacheCamera(void*) {
    xSemaphoreTake(verrouDeLaCamera, portMAX_DELAY);
    cameraPrete = lesBrochesSontLibres() && initialiserLaCamera();
    xSemaphoreGive(verrouDeLaCamera);
    if (!cameraPrete) ecrireDansLeJournal("[CAMERA] Caméra %s.", etatActuel);
    publierLesReglages();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // attend une demande de photo
        xSemaphoreTake(verrouDeLaCamera, portMAX_DELAY);
        prendreEtEnregistrerUnePhoto();
        xSemaphoreGive(verrouDeLaCamera);
    }
}

} // namespace

bool cameraDisponible() { return cameraPrete; }

const char* etatDeLaCamera() { return etatActuel; }

bool demanderUnePhoto() {
    if (tacheDeLaCamera == nullptr) return false;
    xTaskNotifyGive(tacheDeLaCamera);
    return true;
}

bool reglerLaCamera(const char* nom, int valeur, String& explication) {
    const int numero = numeroDuReglage(nom);
    if (numero < 0) {
        explication = String("Réglage inconnu : ") + nom;
        return false;
    }
    const ReglageDeLaCamera& reglage = REGLAGES[numero];
    if (valeur < reglage.minimum || valeur > maximumDuReglage(numero)) {
        explication = String(reglage.nom) + " : valeur entre " + reglage.minimum + " et " + maximumDuReglage(numero);
        return false;
    }
    valeursDesReglages[numero] = valeur;
    memoriserUnReglage(numero);

    bool accepte = true;
    if (cameraPrete && verrouDeLaCamera != nullptr) {
        if (xSemaphoreTake(verrouDeLaCamera, pdMS_TO_TICKS(10 * UNE_SECONDE_MS)) != pdTRUE) {
            explication = String(reglage.nom) + " mémorisé, appliqué après la photo en cours";
            publierLesReglages();
            return true;
        }
        sensor_t* capteur = esp_camera_sensor_get();
        if (capteur != nullptr) accepte = appliquerUnReglage(capteur, numero);
        xSemaphoreGive(verrouDeLaCamera);
    }
    if (!accepte) {
        explication = String(reglage.nom) + " : non pris en charge par le capteur " + nomDuCapteur();
    } else if (!cameraPrete) {
        explication = String(reglage.nom) + " = " + valeur + " mémorisé (caméra non démarrée)";
    } else {
        explication = String(reglage.nom) + " = " + valeur;
    }
    ecrireDansLeJournal("[CAMERA] Réglage %s = %d%s.", reglage.nom, valeur, accepte ? "" : " (refusé par le capteur)");
    publierLesReglages();
    return accepte;
}

void remettreLesReglagesDeLaCameraParDefaut() {
    Preferences memoirePermanente;
    memoirePermanente.begin(ESPACE_DE_LA_MEMOIRE, false);
    memoirePermanente.clear();
    memoirePermanente.end();
    lireLesReglagesMemorises();
    if (cameraPrete && verrouDeLaCamera != nullptr && xSemaphoreTake(verrouDeLaCamera, pdMS_TO_TICKS(10 * UNE_SECONDE_MS)) == pdTRUE) {
        appliquerTousLesReglages();
        xSemaphoreGive(verrouDeLaCamera);
    }
    ecrireDansLeJournal("[CAMERA] Réglages remis par défaut.");
    publierLesReglages();
}

void decrireLesReglagesDeLaCamera(JsonDocument& document) {
    document["t"]       = "camera";
    document["pret"]    = cameraPrete ? 1 : 0;
    document["capteur"] = nomDuCapteur();
    document["tmax"]    = tailleMaximum();
    JsonObject valeurs = document["r"].to<JsonObject>();
    for (int numero = 0; numero < NOMBRE_DE_REGLAGES; numero++) valeurs[REGLAGES[numero].nom] = valeursDesReglages[numero];
    JsonArray refuses = document["ns"].to<JsonArray>();
    for (int numero = 0; numero < NOMBRE_DE_REGLAGES; numero++) if (reglageNonPrisEnCharge[numero]) refuses.add(REGLAGES[numero].nom);
}

bool demarrerModuleCamera() {
    lireLesReglagesMemorises();
    verrouDeLaCamera = xSemaphoreCreateMutex();
    if (verrouDeLaCamera == nullptr) return false;
    return creerTacheSurveillee(tacheCamera, "Camera", 6144, PRIORITE_NORMALE, COEUR_TRAITEMENTS, &tacheDeLaCamera);
}
