#ifndef CONFIG_SD_H
#define CONFIG_SD_H
#include "main.h"
#include "module_camera.h"



// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************
#define CAPACITE_QUEUE_SD 15

enum class TypeDonneeSD {
    LOG,
    GPS,
    GYRO,
    PHOTO
};

// -->> Structure de données partagée entre le (Cœur 1) et le (Cœur 0)  ***********************************************
struct MessageStockage {
    TypeDonneeSD type;
    String texte;       // Données textuelles (vide pour les photos)
    PhotoComplete* fb;    // Pointeur d'image (NULL pour le texte)
};

struct VolumeCarteSD {
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t freeBytes;
    float    usedPercentage;
};

// -->> Variables Globales  ***********************************************
extern VolumeCarteSD volumeActuelCarteSD;

// -->> Fonctions spécifiques utilisables partout.  ***********************************************
bool envoyerAuStockage(TypeDonneeSD type, String texte, PhotoComplete* fb = NULL); // Le "= NULL" à la fin indique au compilateur que ce paramètre est optionnel

// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************
// extern SemaphoreHandle_t xMutex_SIM;

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************
extern QueueHandle_t queueStockageSD;

// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************
extern TaskHandle_t xTacheVerifierEspaceCarteSDHandle;

// -->> Déclaration des tâches FreeRTOS  ***********************************************
void TaskStockageDansSD(void * pvParameters);
void taskVerifierEspaceCarteSD(void * pvParameters);

#endif // CONFIG_SMS_H

