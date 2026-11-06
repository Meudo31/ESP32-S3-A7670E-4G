#ifndef CONFIG_BATTERIE_H
#define CONFIG_BATTERIE_H

#include <Adafruit_MAX1704X.h>

#include "main.h"


// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************
// Broches du bus I2C pour la jauge de batterie MAX17048 (Version V2 de la carte)
#define I2C_SDA_PIN      15
#define I2C_SCL_PIN      16


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************

// -->> Structure de données partagée entre le (Cœur 1) et le (Cœur 0)  ***********************************************

// -->> Variables globales  ***********************************************
bool initialisationJaugeBatterie();
extern Adafruit_MAX17048 jaugeBatterie;

// -->> Fonctions utilisées dans le module_GPS.cpp  ***********************************************



// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************

// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************

// -->> Déclaration des tâches FreeRTOS  ***********************************************
void taskBatterieESP32AfficheInfos(void * pvParameters);


#endif // CONFIG_BATTERIE_H



