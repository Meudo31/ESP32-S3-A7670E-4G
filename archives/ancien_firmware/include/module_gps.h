#ifndef CONFIG_GPS_H
#define CONFIG_GPS_H
#include "main.h"


// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************

// -->> Structure de données partagée entre le (Cœur 1) et le (Cœur 0)  ***********************************************
struct CoordonneesGPS {
    int     source;         // 1=GPS  2=Galileo 3=GLONASS
    bool    fixValide;      // 1=data issues dernière consult 2=dernière consultation KO
    float   latitude;       // +44.1345 
    String  NS;
    float   longitude;      //-27.987654
    String  EO;
    String  date;
    String  heure;
    float   altitude;
    float   vitesse;
    struct {
        float degres;
        String NSEO;       // N, NO, NE, S, SO ...
    } Direction;
   struct {
	    int Pourcentage;  // Plus le pourcentage est grand, plus la précision est bonne
        float PDOP_Position_Dilution_Of_Precision;      // C est l indicateur global le plus important. Il combine les précisions horizontale et verticale. Plus les satellites sont répartis uniformément partout dans le ciel, plus ce chiffre est bas, et plus votre position est exacte.
        float HDOP_Horizontal_Dilution_Of_Precision;    // Pour un traceur de moto, c est le facteur clé. Il indique la précision du point sur une carte 2D (au sol). Si le HDOP est bas, le point bleu sur Google Maps sera parfaitement fixe sur la route. S il est élevé, le point va sauter d une rue à l autre de manière aberrante.
        float VDOP_Vertical_Dilution_Of_Precision;      // Il indique la précision de la hauteur de la moto par rapport au niveau de la mer. En raison de la position des satellites (toujours au-dessus de nous et jamais sous la Terre), le VDOP est mathématiquement toujours moins bon que le HDOP.
    } Precision_Position;
    int     NbSatelites;    // Nombre de satellites actuellement verrouillés. Il faut un minimum strict de 3 satellites pour un calcul 2D (sans altitude). Il faut un minimum de 4 satellites pour un calcul 3D (avec altitude). Sur votre moto en extérieur avec l antenne configurée en mode multi-constellations (AT+CGNSSMODE=7), ce chiffre oscillera généralement de manière excellente entre 10 et 22 satellites.
    time_t  MaJ_TimeStamp;  // Date et Heure de la mise à jour de ces données
};


// -->> Variables globales  ***********************************************
extern CoordonneesGPS  GPS_Data;

// -->> Fonctions spécifiques utilisables partout.  ***********************************************


// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************
extern TaskHandle_t xTacheDonneesGPS_SMS_Handle;
extern TaskHandle_t xTacheDonneesGPS_SD_Handle;
extern TaskHandle_t xTacheDonneesGPS_BT_Handle;
extern TaskHandle_t xTacheDonneesGPS_Wifi_Handle;

// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************
extern TaskHandle_t xTacheGPSHandle;
 
// -->> Déclaration des tâches FreeRTOS  ***********************************************
void taskGPSUpdateAllData           (void * pvParameters);
void taskEnvoiDonneesGPSSMS         (void * pvParameters);
void taskStockageDonneesGPSSD       (void * pvParameters);
void taskEnvoiDonneesGPSBluetooth   (void * pvParameters);
void taskEnvoiDonneesGPSWifi        (void * pvParameters);

#endif // CONFIG_GPS_H
