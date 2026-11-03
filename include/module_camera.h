#ifndef MODULE_CAMERA_H
#define MODULE_CAMERA_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE CAMÉRA : prise de photo et enregistrement sur la carte SD.
   Un dossier par jour, et un nom de fichier qui donne la date et l'heure :
        /photos/03-11-2026/03-11-2026_14-05-09_ESP32-A7670.jpg
   La tâche attend une demande (application, ou plus tard SMS), prend la photo,
   l'enregistre et prévient l'application avec le chemin du fichier.

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

bool demarrerModuleCamera();

#endif // MODULE_CAMERA_H
