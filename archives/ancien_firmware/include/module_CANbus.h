#ifndef CONFIG_CAN_BUS_H
#define CONFIG_CAN_BUS_H

#include <Adafruit_MAX1704X.h>

#include "main.h"

/*Le Câblage Matériel (WCMCU-230 ➡️ ESP32-S3 ➡️ Véhicule)
Pour respecter notre plan de brochage sans conflit, 
nous connectons les broches logiques du module sur les broches libres gérées par le contrôleur interne (TWAI) :
	Côté ESP32 :3V3 ➡️ 3V3
		GND ➡️ GND
		CTX (ou TX) ➡️ GPIO 14
		CRX (ou RX) ➡️ GPIO 13
		RS ➡️ GND (Permet de mettre le module en mode haute vitesse normal)
	Côté Véhicule (Prise OBD2) :
		CANH ➡️ Ligne CAN High du véhicule (Broche 6 de la prise OBD2)
		CANL ➡️ Ligne CAN Low du véhicule (Broche 14 de la prise OBD2)*/

// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************
// Configuration des broches logiques reliées au WCMCU-230
#define PIN_CAN_TX 14
#define PIN_CAN_RX 13

#define CAN_BUS_REGIME_MOTEUR_1 0x3F004
#define CAN_BUS_REGIME_MOTEUR_2 0x0F004
#define CAN_BUS_VITESSE_1 0x3FEF1 
#define CAN_BUS_VITESSE_2 0x0FEF1
#define CAN_BUS_RAPPORT_1 0x3F002
#define CAN_BUS_RAPPORT_2 0x0F002
#define CAN_BUS_TEMPERATURE_1 0x3FEEE
#define CAN_BUS_TEMPERATURE_2 0x0FEEE
#define CAN_BUS_CLE_CONTACT 0x1FFFD
#define CAN_BUS_FUEL_LEVEL_1 0x3FEF2
#define CAN_BUS_FUEL_LEVEL_2 0x0FEF2
#define CAN_BUS_BREAK_DATA_1  0x3F001
#define CAN_BUS_BREAK_DATA_2 0x0F001
#define CAN_BUS_BOITE_VITESSE_1 0x3F005
#define CAN_BUS_BOITE_VITESSE_2 0x0F005
#define CAN_BUS_FEUX_1 0x3FD7D
#define CAN_BUS_FEUX_2 0x0FD7D


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************

// -->> Structure de données partagée entre le (Cœur 1) et le (Cœur 0)  ***********************************************
struct CANbusData {
	float indianVitesseKMH;
	int   indianRPM;
	int   indianRapportEngage; // 0 = Neutre, 1 à 6 = Vitesses
	float indianTemperatureMoteur;
	bool  indianContactActif;
	int   indianNiveauEssence;       // Pourcentage (0 à 100%)
	bool  indianBequilleSortie;   // true = Moto sur béquille
	bool  indianFreinActif;       // true = Freinage en cours
	bool  indianEmbrayageEnfonce; // true = Levier d'embrayage tiré
	bool  indianClignotantGauche;
	bool  indianClignotantDroit;
	bool  indianFeuxDeRoute;

	bool brakePedalSwitch;          // Pédale/Levier de frein enfoncé
	uint8_t asrEngineControlStatus;    // Statut contrôle moteur ASR (2 bits)
	uint8_t asrBrakeControlStatus;     // Statut contrôle frein ASR (2 bits)
	bool antiLockBrakeActive;       // ABS en action (Anti-blocage actif)
	float brakeApplicationPressure;  // Pression de freinage demandée (en %)

	bool clutchSwitchActive;       // Levier d'embrayage tiré
	bool kickstandSwitchOut;       // Béquille latérale dépliée
	uint8_t selectedGearRaw;           // Rapport brut de boîte de vitesse
	float transmissionInputSpeed;    // Vitesse de rotation d'entrée de boîte (RPM)

	bool runningLightsActive;       // Feux de position / Veilleuses
	bool lowBeamActive;             // Feux de croisement (Codes)
	bool highBeamActive;            // Feux de route (Pleins phares)
	bool turnSignalLeft;            // Clignotant Gauche actif
	bool turnSignalRight;           // Clignotant Droit actif
	bool hazardLightsActive;        // Feux de détresse (Warnings)
	bool brakeLightsActive;         // Feu stop arrière allumé
};



// -->> Variables globales  ***********************************************
extern CANbusData lastCANbusData;
bool initialiserCANbus();

// -->> Fonctions utilisées dans le module_GPS.cpp  ***********************************************



// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************

// -->> Handle global pour pouvoir déclencher cette tâche depuis l'extérieur (ex: depuis le Cœur 0 par SMS)  ***********************************************

// -->> Déclaration des tâches FreeRTOS  ***********************************************
void tacheEcouteCAN(void * pvParameters);


#endif // CONFIG_BATTERIE_H



