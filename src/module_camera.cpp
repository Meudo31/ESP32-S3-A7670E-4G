#include <ArduinoJson.h>
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
volatile bool cameraPrete     = false;
const char*   etatActuel      = "pas encore démarrée";
uint32_t      numeroDePhotoSansDate = 0;

bool lesBrochesSontLibres() {
    if (brochesDeLaCarte().cameraHREF == BROCHE_BANDE_NEOPIXELS) {
        etatActuel = "désactivée : GPIO 41 est utilisé par la bande NeoPixel (déplacer la bande sur un GPIO libre, ex. 40)";
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
    if (psramFound()) {
        reglages.frame_size   = FRAMESIZE_UXGA;    // 1600 x 1200
        reglages.jpeg_quality = 10;                // 0 = meilleure qualité, 63 = la plus faible
        reglages.fb_count     = 2;
        reglages.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        reglages.frame_size   = FRAMESIZE_SVGA;    // 800 x 600
        reglages.jpeg_quality = 12;
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
        // La première image peut dater d'avant la demande : on la jette.
        camera_fb_t* image = esp_camera_fb_get();
        if (image) esp_camera_fb_return(image);
        image = esp_camera_fb_get();

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
    cameraPrete = lesBrochesSontLibres() && initialiserLaCamera();
    if (!cameraPrete) ecrireDansLeJournal("[CAMERA] Caméra %s.", etatActuel);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // attend une demande de photo
        prendreEtEnregistrerUnePhoto();
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

bool demarrerModuleCamera() {
    return creerTacheSurveillee(tacheCamera, "Camera", 6144, PRIORITE_NORMALE, COEUR_TRAITEMENTS, &tacheDeLaCamera);
}
