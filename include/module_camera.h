#ifndef MODULE_CAMERA_H
#define MODULE_CAMERA_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE CAMÉRA : prise de photo et enregistrement sur la carte SD.
   Un dossier par jour, et un nom de fichier qui donne la date et l'heure :
        /photos/03-11-2026/03-11-2026_14-05-09_ESP32-A7670.jpg
   La tâche attend une demande de l'application, prend la photo, l'enregistre
   et prévient l'application avec le chemin du fichier.
   La commande SMS « Photo » prend une photo plus petite avec prendreUnePhotoAEnvoyer().

   ⚠️ Carte V2 : la caméra utilise GPIO 41 (signal HREF), où est branchée la bande
      NeoPixel. Les deux ne peuvent pas fonctionner ensemble : la caméra reste
      désactivée tant que la bande est sur GPIO 41 (voir configuration.h).
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

#define DOSSIER_DES_PHOTOS "/photos"

bool cameraDisponible();
const char* etatDeLaCamera();   // explication lisible (prête, absente, conflit de broche...)

// Demande une photo. Le résultat arrive dans l'application (événement « photo »).
bool demanderUnePhoto();

// Photo plus petite, prise pour être envoyée par Internet (commande SMS « Photo »).
struct PhotoAEnvoyer {
    uint8_t* jpeg;       // copie de l'image en PSRAM : à libérer avec free()
    size_t   taille;
    String   chemin;     // fichier enregistré sur la carte SD ("" si pas de carte)
    String   erreur;     // explication si la photo n'a pas pu être prise
};

// Prend la photo dans la tâche qui appelle (bloquant, quelques secondes) : d'abord en 800 x 600,
// puis en 640 x 480 si l'image dépasse « tailleMaximum ». Elle est aussi enregistrée sur la carte SD.
bool prendreUnePhotoAEnvoyer(PhotoAEnvoyer& photo, size_t tailleMaximum);

bool demarrerModuleCamera();

#endif // MODULE_CAMERA_H
