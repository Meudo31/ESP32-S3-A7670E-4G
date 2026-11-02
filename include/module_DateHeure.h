#ifndef CONFIG_DATE_HEURE_H
#define CONFIG_DATE_HEURE_H

#include <time.h>
#include "main.h"

// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************

// -->> Structure de données partagée   ***********************************************

// -->> Variables Globales  ***********************************************
enum DateHeure_SourcesValues {
    DH_INCONNU,
    DH_GPS,	
    DH_SIM, 	
    DH_WIFI,   
    DH_REBBOOT 
};

extern DateHeure_SourcesValues Date_Heure_Source_MaJ;


// -->> Fonctions spécifiques utilisables partout.  ***********************************************
time_t          Heure_RTC();
String          Heure_formatteeFichier();
String          Heure_formatteeStandard();
String          Date_formatteeUS();
String          Date_formatteeUSFichier();
String          Date_formatteeFR();
String          Date_Heure();
int             JourDuMoisCourrant();
int             MoisCourrant();

// void DateHeure_updateViaWifi();
// void DateHeure_updateViaSIM();


// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************

// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************

// -->> Déclaration des tâches FreeRTOS  ****************


#endif // CONFIG_DATE_HEURE_H