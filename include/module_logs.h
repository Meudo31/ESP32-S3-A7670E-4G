#ifndef CONFIG_LOGS_H
#define CONFIG_LOGS_H
#include "main.h"


// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************
// Broches du bus I2C pour la jauge de batterie MAX17048 (Version V2 de la carte)


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************
#define TAILLE_MAX_LOG 128      // Taille maximale d'une seule ligne de log (en caractères)
#define CAPACITE_QUEUE 30       // Nombre maximal de messages stockés dans la file d'attente

// -->> Structure de données partagée entre le (Cœur 1) et le (Cœur 0)  ***********************************************

// -->> Variables globales  ***********************************************

// -->> Fonctions utilisées dans le module_GPS.cpp  ***********************************************
void my_log_printf(const char *format, ...);


// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************
extern QueueHandle_t queueLogs;
 
// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************

// -->> Déclaration des tâches FreeRTOS  ***********************************************
void tacheGestionLogs(void * pvParameters);  // Déclenché par xQueueReceive


#endif // CONFIG_LOGS_H
