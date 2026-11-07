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
SemaphoreHandle_t verrouDeLaCamera = nullptr;   // une seule prise de vue à la fois (appli ou SMS)
volatile bool cameraPrete     = false;
const char*   etatActuel      = "pas encore démarrée";
uint32_t      numeroDePhotoSansDate = 0;

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
        xSemaphoreTake(verrouDeLaCamera, portMAX_DELAY);
        prendreEtEnregistrerUnePhoto();
        xSemaphoreGive(verrouDeLaCamera);
    }
}

// Prend une image à la taille demandée. La caméra garde ensuite cette taille : l'appelant la remet.
camera_fb_t* prendreUneImageEn(framesize_t taille) {
    sensor_t* capteur = esp_camera_sensor_get();
    if (capteur == nullptr) return nullptr;
    capteur->set_framesize(capteur, taille);
    // Les images déjà en mémoire ont l'ancienne taille, et le réglage automatique d'exposition
    // a besoin de quelques images : on en jette deux.
    for (int image = 0; image < 2; image++) {
        camera_fb_t* ancienne = esp_camera_fb_get();
        if (ancienne) esp_camera_fb_return(ancienne);
    }
    return esp_camera_fb_get();
}

} // namespace

bool cameraDisponible() { return cameraPrete; }

const char* etatDeLaCamera() { return etatActuel; }

bool demanderUnePhoto() {
    if (tacheDeLaCamera == nullptr) return false;
    xTaskNotifyGive(tacheDeLaCamera);
    return true;
}

bool prendreUnePhotoAEnvoyer(PhotoAEnvoyer& photo, size_t tailleMaximum) {
    photo = { nullptr, 0, "", "" };
    if (!cameraPrete) {
        photo.erreur = String("camera ") + etatActuel;
        return false;
    }
    if (xSemaphoreTake(verrouDeLaCamera, pdMS_TO_TICKS(20 * UNE_SECONDE_MS)) != pdTRUE) {
        photo.erreur = "camera occupee";
        return false;
    }
    sensor_t* capteur = esp_camera_sensor_get();
    const framesize_t tailleHabituelle = capteur ? (framesize_t)capteur->status.framesize : FRAMESIZE_SVGA;

    camera_fb_t* image = prendreUneImageEn(FRAMESIZE_SVGA);
    if (image != nullptr && image->len > tailleMaximum) {
        esp_camera_fb_return(image);
        image = prendreUneImageEn(FRAMESIZE_VGA);
    }

    if (image == nullptr) {
        photo.erreur = "la camera n'a pas renvoye d'image";
    } else if (image->len > tailleMaximum) {
        photo.erreur = String("photo trop grosse (") + image->len + " octets)";
    } else {
        photo.jpeg = (uint8_t*)(psramFound() ? ps_malloc(image->len) : malloc(image->len));
        if (photo.jpeg == nullptr) {
            photo.erreur = "memoire insuffisante";
        } else {
            memcpy(photo.jpeg, image->buf, image->len);
            photo.taille = image->len;
            if (carteSDMontee()) {
                const String chemin = cheminDeLaNouvellePhoto();
                if (ecrireUnFichier(chemin.c_str(), image->buf, image->len)) photo.chemin = chemin;
            }
        }
    }
    if (image) esp_camera_fb_return(image);
    if (capteur) capteur->set_framesize(capteur, tailleHabituelle);
    xSemaphoreGive(verrouDeLaCamera);

    ecrireDansLeJournal("[CAMERA] Photo pour envoi : %s (%u octets) %s.", photo.jpeg ? "prise" : "ÉCHEC",
                        (unsigned)photo.taille, photo.chemin.length() ? photo.chemin.c_str() : photo.erreur.c_str());
    return photo.jpeg != nullptr;
}

bool demarrerModuleCamera() {
    verrouDeLaCamera = xSemaphoreCreateMutex();
    if (verrouDeLaCamera == nullptr) return false;
    return creerTacheSurveillee(tacheCamera, "Camera", 6144, PRIORITE_NORMALE, COEUR_TRAITEMENTS, &tacheDeLaCamera);
}
