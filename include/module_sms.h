#ifndef CONFIG_SMS_H
#define CONFIG_SMS_H
#include "main.h"

#define MODEM_RX_PIN 
#define MODEM_TX_PIN
#define MODEM_PWRKEY_PIN 

// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************


// -->> Structure de données partagée entre le (Cœur 1) et le (Cœur 0)  ***********************************************
struct SIM_struct {   // Définition physique des variables en mémoire RTC (Verrouillées pendant le dodo)
	int SIM_status; 			// 0=Non-connecté 1=actif mais non connecté 2=Connecté
	int SIM_qualite_signal;	    // Pourcentage
    unsigned long dataConsommeeCeMoisBytes;     // pour avoir en Go : / (1024.0 * 1024.0)
    unsigned long dataRecuesCeMoisBytes;        // pour avoir en Go : / (1024.0 * 1024.0)
    unsigned long dataEnvoyeeCeMoisBytes;       // pour avoir en Go : / (1024.0 * 1024.0)
    int pourcentageDataRestant;
    int dernierMoisReinitialise;
    time_t dateMaJ;
};

// -->> Variables Globales  ***********************************************
extern RTC_DATA_ATTR SIM_struct SIM_data; //RTC_DATA_ATTR pour que cette variable soit stockée en ROM

// -->> Fonctions spécifiques utilisables partout.  ***********************************************
bool initialisationModem4G();
void miseAJour_ConsommationData4G();

// Répertoire : position d'un numéro dans la liste (-1 si absent). Seuls les 9 derniers chiffres sont comparés
// (+33612345678 et 0612345678 désignent le même numéro).
int  indiceRepertoire(const char* numero);
bool numeroAutorisePourAppel(const char* numero);   // true pour les numéros qui peuvent déclencher un décrochage automatique

// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************
// extern SemaphoreHandle_t xMutex_SIM;

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************
// QueueHandle_t xQueue_SMS_EnvoiePhoto = NULL;

// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************

// -->> Déclaration des tâches FreeRTOS  ***********************************************
// void taskEnvoieSMSposition     (void * pvParameters);
void taskReceptionSMS          (void * pvParameters);
// void taskEnvoyerMMSPhoto       (void * pvParameters);

#endif // CONFIG_SMS_H
