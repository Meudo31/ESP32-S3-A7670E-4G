#ifndef CONFIG_CAMERA_H
#define CONFIG_CAMERA_H

#include "main.h"


/*Le contrôleur d'affichage interne de l'ESP32-S3 utilise ces lignes comme un bus de données industriel parallèle ultra-rapide : 
    PCLK (Pixel Clock) : C'est le métronome qui bat à chaque fois qu'un pixel est disponible.
    HREF (Horizontal Reference) : Cette ligne passe à l'état logique 1 pour indiquer à l'ESP32 qu'une ligne horizontale de la photo est en train d'être transmise.
    VSYNC (Vertical Sync) : Une impulsion est envoyée ici dès qu'une image entière est terminée. C'est elle qui déclenche l'interruption FreeRTOS indiquant à votre code : "La photo est prête en RAM, vous pouvez y injecter vos données EXIF !" 
    
    Y2 à Y9 représentent les 8 canaux parallèles qui transportent les octets de l'image (le bus de données 8 bits).*/


// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************
// ─── CONFIGURATION DES BROCHES DE LA CAMÉRA (Waveshare ESP32-S3) ───
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    40
#define SIOD_GPIO_NUM    38
#define SIOC_GPIO_NUM    39

#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      33
#define Y6_GPIO_NUM      21
#define Y5_GPIO_NUM      18
#define Y4_GPIO_NUM      17
#define Y3_GPIO_NUM      16
#define Y2_GPIO_NUM      15
#define VSYNC_GPIO_NUM   6
#define HREF_GPIO_NUM    7
#define PCLK_GPIO_NUM    13

// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************
struct PhotoComplete {
    uint8_t* buf;  // La boîte pour l'adresse de l'image modifiée en RAM
    size_t len;  // La boîte pour la taille exacte de cette image
}; // <-- Ce bloc crée officiellement le nouveau type "PhotoComplete"

// -->> Structure de données partagée entre le (Cœur 1) et le (Cœur 0)  ***********************************************

// -->> Variables Globales  ***********************************************
bool initialisationHardwareCamera();

// -->> Fonctions spécifiques utilisables partout.  ***********************************************


// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************
extern QueueHandle_t queuePhotoMMS;
extern QueueHandle_t queuePhotoSD;
extern QueueHandle_t queuePhotoBT;
extern QueueHandle_t queuePhotoWifi;

// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************
extern TaskHandle_t xTacheCameraHandle;

// -->> Déclaration des tâches FreeRTOS  ***********************************************
void taskPriseDePhoto       (void * pvParameters);  // Parametre type: QueFaireDeLObjet_Values
void taskStockagePhotoSD    (void * pvParameters);  // Parametre type: camera_fb_t 
void taskEnvoiPhotoSMS      (void * pvParameters);  // Parametre type: camera_fb_t 
void taskEnvoiPhotoBluetooth(void * pvParameters);  // Parametre type: camera_fb_t 
void taskEnvoiPhotoWifi     (void * pvParameters);  // Parametre type: camera_fb_t 



#endif // CONFIG_CAMERA_H