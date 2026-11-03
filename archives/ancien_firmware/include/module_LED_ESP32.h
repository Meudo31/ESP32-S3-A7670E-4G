#ifndef CONFIG_LED_ESP32_H
#define CONFIG_LED_ESP32_H
#include "main.h"



// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************
#define LED_INTEGREE_GPIO	21	// La carte Waveshare utilise généralement la broche GPIO 21 pour sa LED système intégrée
// #define PIN_NEOPIXEL		38 // GPIO officiel de la LED RGB intégrée
#define NUM_PIXELS  		1  // Il n'y a qu'une seule LED sur la carte


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************

// -->> Structure de données partagée   ***********************************************

// -->> Variables globales  ***********************************************

// -->> Fonctions spécifiques utilisables partout.  ***********************************************
bool initialisationLEDIntegree ();
bool initialisationNeopixelIntegree();

// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************

// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************

// -->> Déclaration des tâches FreeRTOS  ***********************************************
void taskClignotementLED    (void * pvParameters);
void taskHeartBeatLED 		(void * pvParameters);
void taskNeopixelAnimationSimple(void * pvParameters);

#endif // CONFIG_LED_ESP32_H