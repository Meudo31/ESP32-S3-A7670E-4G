#include "driver/twai.h" // Pilote natif CAN/TWAI d'Espressif
#include "module_CANbus.h"

/*Le Câblage Matériel (WCMCU-230 ➡️ ESP32-S3 ➡️ Véhicule)
Pour respecter notre plan de brochage sans conflit, 
nous connectons les broches logiques du module sur les broches libres gérées par le contrôleur interne (TWAI) :
Côté ESP32 :
	3V3 ➡️ 3V3
	GND ➡️ GNDC
	TX (ou TX) ➡️ GPIO 14
	CRX (ou RX) ➡️ GPIO 13
	RS ➡️ GND (Permet de mettre le module en mode haute vitesse normal)
Côté Véhicule (Prise OBD2) :
	CANH ➡️ Ligne CAN High du véhicule (Broche 6 de la prise OBD2)
	CANL ➡️ Ligne CAN Low du véhicule (Broche 14 de la prise OBD2)*/

/*Poids des données (Endianness) : 
Le protocole J1939 de l'Indian transmet les données en Little Endian (l'octet de poids faible arrive en premier), 
le code utilise donc la formule (data[4] << 8) | data[3] pour reconstituer les valeurs proprement.*/

// Configuration des broches logiques reliées au WCMCU-230
#define PIN_CAN_TX 14
#define PIN_CAN_RX 13

// Variable globale partagée pour vos logs ou votre écran
uint32_t dernierIdCANIntercepte = 0;
CANbusData lastCANbusData;




// =========================================================================
// 1. EXTRACTEUR EXHAUSTIF DU PGN 0xF001 (EBC1 - Electronic Brake Controller 1)
// =========================================================================

void extraireToutPGN_F001(const uint8_t* data) {
    // Octet 0 : Commutateurs et états ABS/ASR
    lastCANbusData.asrBrakeControlStatus = data[0] & 0x03;// Bits 0-1 : ASR Brake Control Status
    lastCANbusData.asrEngineControlStatus = (data[0] >> 2) & 0x03;// Bits 2-3 : ASR Engine Control Status
    lastCANbusData.brakePedalSwitch = ((data[0] >> 4) & 0x03) == 0x02;// Bits 4-5 : Brake Pedal Switch (Masque standard J1939 : 01 = Off, 10 = On)
    lastCANbusData.antiLockBrakeActive = ((data[0] >> 6) & 0x03) == 0x01; // Bits 6-7 : ABS Active Status (01 = Actif)
    
    if (data[1] <= 250) { // 251-255 sont des valeurs d'erreur/non disponibles
        lastCANbusData.brakeApplicationPressure = data[1] * 0.4;  // Octet 1 : Pression de freinage appliquée (Résolution standard : 0.4% / bit)
    }
    
    // Note : Les octets 2 à 7 du PGN F001 contiennent des données de stabilité (ESC/YAW) 
    // qui dépendent des options de la Scout (ABS Standard ou Centrale Inertielle).
}

// =========================================================================
// 2. EXTRACTEUR EXHAUSTIF DU PGN 0xF005 (ETC2 - Transmission Control 2 / Safety)
// =========================================================================

void extraireToutPGN_F005(const uint8_t* data) {
    // Octet 0 : Statut de l'embrayage et de la transmission
    lastCANbusData.clutchSwitchActive = ((data[0] >> 6) & 0x03) == 0x01;// Bits 6-7 : Clutch Switch (01 = Débrayé/Tiré, 10 = Embrayé/Lâché)
    lastCANbusData.selectedGearRaw = data[1];// Octet 1 : Rapport sélectionné (0 = Neutral, 1-6 = Vitesses, 251 = Parc)
    uint16_t inputSpeedRaw = (data[3] << 8) | data[2];// Octet 2-3 : Vitesse d'entrée de boîte (Transmission Input Shaft Speed)
    if (inputSpeedRaw < 0xFAFF) {
        lastCANbusData.transmissionInputSpeed = inputSpeedRaw * 0.125;// Format : 2 octets Little Endian, Résolution : 0.125 RPM / bit
    }
    // Bit 0 : Kickstand Switch (1 = Sécurité active / Béquille sortie)
    lastCANbusData.kickstandSwitchOut = (data[4] & 0x01) == 0x01;	// Octet 4 : Masque spécifique Polaris/Indian pour la sécurité béquille // Bit 0 : Kickstand Switch (1 = Sécurité active / Béquille sortie)
}

// =========================================================================
// 3. EXTRACTEUR EXHAUSTIF DU PGN 0xFD7D (LSTD - Lighting Status Display)
// =========================================================================

void extraireToutPGN_FD7D(const uint8_t* data) {
    // Octet 0 : Commandes d'éclairage principales
    lastCANbusData.runningLightsActive = (data[0] & 0x03) == 0x01;// Bits 0-1 : Running Lights (Veilleuses)
    lastCANbusData.lowBeamActive = ((data[0] >> 2) & 0x03) == 0x01;// Bits 2-3 : Low Beam Status (Feux de croisement)
    lastCANbusData.highBeamActive = ((data[0] >> 4) & 0x03) == 0x01;// Bits 4-5 : High Beam Status (Feux de route)
    // Octet 1 : Indicateurs de direction et freinage
    lastCANbusData.turnSignalLeft = (data[1] & 0x03) == 0x01;// Bits 0-1 : Left Turn Signal (Clignotant Gauche)
    lastCANbusData.turnSignalRight = ((data[1] >> 2) & 0x03) == 0x01;// Bits 2-3 : Right Turn Signal (Clignotant Droit)
    // Octet 2 : Feux spéciaux
    lastCANbusData.hazardLightsActive = (data[2] & 0x03) == 0x01;// Bits 0-1 : Hazard Signal (Warnings)
    lastCANbusData.brakeLightsActive = ((data[2] >> 4) & 0x03) == 0x01;// Bits 4-5 : Brake Light Status (Feu Stop physique activé par la centrale de puissance)
}






bool initialiserCANbus() {
    my_log_printf("🚗 [CAN] Configuration du émetteur-récepteur WCMCU-230 (SN65HVD230)...");

    // 1. Configuration du timing (Vitesse du bus)
    // Choisissez la vitesse correspondant à votre réseau : 
    // - Automobile (OBD2 standard) : TWAI_TIMING_CONFIG_500KBITS()
    // - Industriel / Domotique : TWAI_TIMING_CONFIG_250KBITS() ou 125KBITS()
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX, (gpio_num_t)PIN_CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS(); // Vitesse d'écoute réglée sur 250 kbps pour l'Indian Scoute Bobber
    
    // 2. Configuration des Filtres (Acceptation globale de toutes les trames pour le scanneur)
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // 3. Installation du pilote matériel dans le processeur de l'ESP32-S3
    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        my_log_printf("❌ [CAN_ERR] Impossible d'installer le pilote matériel TWAI/CAN !");
        return false;
    }

    // 4. Démarrage de la puce d'écoute réseau
    if (twai_start() != ESP_OK) {
        my_log_printf("❌ [CAN_ERR] Échec du démarrage du bus CAN.");
        return false;
    }

	lastCANbusData.indianVitesseKMH 		= 0.0;
	lastCANbusData.indianRPM 				= 0;
	lastCANbusData.indianRapportEngage 		= 0; // 0 = Neutre, 1 à 6 = Vitesses
	lastCANbusData.indianTemperatureMoteur 	= 0.0;
	lastCANbusData.indianContactActif 		= false;
	lastCANbusData.indianNiveauEssence 		= 0;       // Pourcentage (0 à 100%)
	lastCANbusData.indianBequilleSortie 	= false;   // true = Moto sur béquille
	lastCANbusData.indianFreinActif 		= false;       // true = Freinage en cours
	lastCANbusData.indianEmbrayageEnfonce 	= false; // true = Levier d'embrayage tiré
	lastCANbusData.indianClignotantGauche 	= false;
	lastCANbusData.indianClignotantDroit 	= false;
	lastCANbusData.indianFeuxDeRoute 		= false;
	lastCANbusData.indianNiveauEssence 		= 0;       // Pourcentage (0 à 100%)
	lastCANbusData.indianBequilleSortie 	= false;   // true = Moto sur béquille
	lastCANbusData.indianFreinActif 		= false;       // true = Freinage en cours
	lastCANbusData.indianEmbrayageEnfonce 	= false; // true = Levier d'embrayage tiré
	lastCANbusData.indianClignotantGauche 	= false;
	lastCANbusData.indianClignotantDroit 	= false;
	lastCANbusData.indianFeuxDeRoute 		= false;
	lastCANbusData.brakePedalSwitch 		= false;          // Pédale/Levier de frein enfoncé
	lastCANbusData.asrEngineControlStatus 	= 0;    // Statut contrôle moteur ASR (2 bits)
	lastCANbusData.asrBrakeControlStatus 	= 0;     // Statut contrôle frein ASR (2 bits)
	lastCANbusData.antiLockBrakeActive 		= false;       // ABS en action (Anti-blocage actif)
	lastCANbusData.brakeApplicationPressure = 0.0;  // Pression de freinage demandée (en %)
	lastCANbusData.clutchSwitchActive 		= false;       // Levier d'embrayage tiré
	lastCANbusData.kickstandSwitchOut 		= false;       // Béquille latérale dépliée
	lastCANbusData.selectedGearRaw 			= 0;           // Rapport brut de boîte de vitesse
	lastCANbusData.transmissionInputSpeed 	= 0.0;    // Vitesse de rotation d'entrée de boîte (RPM)
	lastCANbusData.runningLightsActive		= false;       // Feux de position / Veilleuses
	lastCANbusData.lowBeamActive 			= false;             // Feux de croisement (Codes)
	lastCANbusData.highBeamActive 			= false;            // Feux de route (Pleins phares)
	lastCANbusData.turnSignalLeft 			= false;            // Clignotant Gauche actif
	lastCANbusData.turnSignalRight 			= false;           // Clignotant Droit actif
	lastCANbusData.hazardLightsActive 		= false;        // Feux de détresse (Warnings)
	lastCANbusData.brakeLightsActive 		= false;         // Feu stop arrière allumé


    my_log_printf("✅ [CAN] Module WCMCU-230 opérationnel à 500 kbps.");
    return true;
}


/*💡 Pourquoi ce décodage J1939 bit à bit est crucial :
La norme J1939 n'utilise presque jamais un octet entier pour un simple bouton (On/Off). 
Pour optimiser la bande passante, elle utilise des états codés sur 2 bits :
	00 : Désactivé / Off
	01 : Activé / On
	10 : Erreur matérielle (Court-circuit)
	11 : Non disponible / Option absente
Le code utilise des décalages binaires (>>) et des masques logiques (& 0x03) 
pour extraire isolément ces paires de bits de chaque octet de la trame.
*/



void tacheEcouteCAN(void * pvParameters) {
    my_log_printf("🚗 [CAN_TASK] Scanneur de réseau actif en arrière-plan.");
    twai_message_t messageRx;

    while(1) {
        // La tâche se bloque ici (0% CPU) tant qu'aucune trame physique n'apparaît sur le bus.
        // Dès qu'un octet passe, le contrôleur matériel réveille la tâche en moins de 1 microseconde.
        if (twai_receive(&messageRx, portMAX_DELAY) == ESP_OK) {
            
           // Les motos Indian utilisent des identifiants étendus (29 bits)
            if (messageRx.extd && !(messageRx.rtr)) {
                
                uint32_t id = messageRx.identifier;
                
                // On extrait le PGN (Parameter Group Number) situé au milieu de l'ID 29-bits
                uint32_t pgn = (id >> 8) & 0x3FFFF;

                // --- 1. SÉLECTION ET DÉCODAGE DES PARAMÈTRES PAR COMPOSANT ---
                switch(pgn) {
                    
                    // PGN 0xF004 (Electronic Engine Controller 1) : Régime Moteur (RPM)
                    case CAN_BUS_REGIME_MOTEUR_1: 
                    case CAN_BUS_REGIME_MOTEUR_2: {
                        // Le RPM est codé sur les octets 3 et 4 (Index 3 et 4), Résolution: 0.125 RPM/bit
                        uint16_t rpmBrut = (messageRx.data[4] << 8) | messageRx.data[3];
                        lastCANbusData.indianRPM = (int)(rpmBrut * 0.125);
                        
                        // Si le moteur se met à tourner alors que l'alarme est armée -> Alerte !
                        if (lastCANbusData.indianRPM > 500) {
                            lastCANbusData.indianContactActif = true;
                        }
                        break;
                    }

                    // PGN 0xFEF1 (Cruise Control/Vehicle Speed) : Vitesse réelle de la moto
                    case CAN_BUS_VITESSE_1:
                    case CAN_BUS_VITESSE_2: {
                        // Vitesse de la roue sur les octets 1 et 2, Résolution: 1/256 km/h par bit
                        uint16_t vitesseBrute = (messageRx.data[2] << 8) | messageRx.data[1];
                        lastCANbusData.indianVitesseKMH = (float)(vitesseBrute / 256.0);
                        break;
                    }

                    // PGN 0xF002 (Transmission Gear) : Rapport de boîte de vitesse engagé
                    case CAN_BUS_RAPPORT_1:
                    case CAN_BUS_RAPPORT_2: {
                        // L'octet 3 donne le rapport engagé
                        uint8_t gearBrut = messageRx.data[3];
                        if (gearBrut == 0x00) lastCANbusData.indianRapportEngage = 0;      // Neutral (N)
                        else if (gearBrut >= 1 && gearBrut <= 6) lastCANbusData.indianRapportEngage = gearBrut; // Vitesse 1 à 6
                        break;
                    }

                    // PGN 0xFEEE (Engine Temperature) : Température du liquide de refroidissement
                    case CAN_BUS_TEMPERATURE_1:
                    case CAN_BUS_TEMPERATURE_2: {
                        // Premier octet, Résolution : 1°C par bit, avec un décalage de -40°C
                        int tempBrute = messageRx.data[0];
                        lastCANbusData.indianTemperatureMoteur = (float)(tempBrute - 40);
                        break;
                    }
                    
                    // PGN Spécifique Polaris (État de la clé et du contact de démarrage)
                    case CAN_BUS_CLE_CONTACT: {
                        // Analyse de l'octet de statut d'allumage
                        if ((messageRx.data[0] & 0x01) == 0x01) {
                            if (!lastCANbusData.indianContactActif) {
                                lastCANbusData.indianContactActif = true;
                                Serial.println("🚨 [INDIAN_ALERT] LE CONTACT DE LA MOTO VIENT D'ÊTRE MIS !");
                                
                                // // DÉCLENCHEUR SÉCURITÉ : On réveille la caméra pour photographier le pilote
                                // if (xTacheCameraHandle != NULL) {
                                //     xTaskNotify(xTacheCameraHandle, 1, eSetValueWithOverwrite); // 1 = CARTE_SD
                                // }
                            }
                        } else {
                            lastCANbusData.indianContactActif = false;
                        }
                        break;
                    }

					// PGN 0xFEF2 (Fuel Level / Niveau d'essence)
					case CAN_BUS_FUEL_LEVEL_1:
					case CAN_BUS_FUEL_LEVEL_2: {
						// L'octet 1 (deuxième octet) donne le niveau d'essence directement en %
						// Résolution standard J1939 : 0.4% par bit. Souvent lissé à 1% par bit chez Polaris.
						lastCANbusData.indianNiveauEssence = messageRx.data[1];
						break;
					}

					// PGN 0xF001 (Brake Switch / État des Freins)
					case CAN_BUS_BREAK_DATA_1: case CAN_BUS_BREAK_DATA_2:
                        extraireToutPGN_F001(messageRx.data);
                        break;

					// PGN 0xF005 (Clutch & Kickstand / Embrayage et Béquille)
					case CAN_BUS_BOITE_VITESSE_1: case CAN_BUS_BOITE_VITESSE_2:
                        extraireToutPGN_F005(messageRx.data);
                        break;

					// PGN 0xFD7D (Lighting Status / Clignotants et Phares)
					case CAN_BUS_FEUX_1: case CAN_BUS_FEUX_2:
                        extraireToutPGN_FD7D(messageRx.data);
                        break;
                }

                // --- 2. AFFICHAGE DE SYNTHÈSE DANS LA CONSOLE SÉRIE ---
                // S'affiche uniquement toutes les secondes pour ne pas saturer l'écran
                static unsigned long dernierAffichage = 0;
                if (millis() - dernierAffichage > 1000) {
                    dernierAffichage = millis();
                    Serial.printf("🏍️ [SCOUT_BOBBER] RPM: %d tr/min | Speed: %0.1f km/h | Gear: %d | Engine Temp: %0.1f°C | Ignition: %s\n",
                                  lastCANbusData.indianRPM, 
								  lastCANbusData.indianVitesseKMH, 
								  lastCANbusData.indianRapportEngage, 
								  lastCANbusData.indianTemperatureMoteur, lastCANbusData.
								  indianContactActif ? "ON" : "OFF");
                }
			}
			
			// Pas besoin de delay() ici car 'twai_receive' bloque déjà la tâche proprement 
			// de manière asynchrone tant que la file matérielle est vide.
		}
	}
}



/**************************************************
 * #include "module_CAN.h"
#include "driver/twai.h"
#include "structures.h"

// =========================================================================
// 1. EXTRACTEUR EXHAUSTIF DU PGN 0xF001 (EBC1 - Electronic Brake Controller 1)
// =========================================================================
// Variables PGN 0xF001
bool brakePedalSwitch = false;          // Pédale/Levier de frein enfoncé
uint8_t asrEngineControlStatus = 0;    // Statut contrôle moteur ASR (2 bits)
uint8_t asrBrakeControlStatus = 0;     // Statut contrôle frein ASR (2 bits)
bool antiLockBrakeActive = false;       // ABS en action (Anti-blocage actif)
float brakeApplicationPressure = 0.0;  // Pression de freinage demandée (en %)

void extraireToutPGN_F001(const uint8_t* data) {
    // Octet 0 : Commutateurs et états ABS/ASR
    // Bits 0-1 : ASR Brake Control Status
    asrBrakeControlStatus = data[0] & 0x03;
    // Bits 2-3 : ASR Engine Control Status
    asrEngineControlStatus = (data[0] >> 2) & 0x03;
    // Bits 4-5 : Brake Pedal Switch (Masque standard J1939 : 01 = Off, 10 = On)
    brakePedalSwitch = ((data[0] >> 4) & 0x03) == 0x02;
    // Bits 6-7 : ABS Active Status (01 = Actif)
    antiLockBrakeActive = ((data[0] >> 6) & 0x03) == 0x01;

    // Octet 1 : Pression de freinage appliquée (Résolution standard : 0.4% / bit)
    if (data[1] <= 250) { // 251-255 sont des valeurs d'erreur/non disponibles
        brakeApplicationPressure = data[1] * 0.4;
    }
    
    // Note : Les octets 2 à 7 du PGN F001 contiennent des données de stabilité (ESC/YAW) 
    // qui dépendent des options de la Scout (ABS Standard ou Centrale Inertielle).
}

// =========================================================================
// 2. EXTRACTEUR EXHAUSTIF DU PGN 0xF005 (ETC2 - Transmission Control 2 / Safety)
// =========================================================================
// Variables PGN 0xF005
bool clutchSwitchActive = false;       // Levier d'embrayage tiré
bool kickstandSwitchOut = false;       // Béquille latérale dépliée
uint8_t selectedGearRaw = 0;           // Rapport brut de boîte de vitesse
float transmissionInputSpeed = 0.0;    // Vitesse de rotation d'entrée de boîte (RPM)

void extraireToutPGN_F005(const uint8_t* data) {
    // Octet 0 : Statut de l'embrayage et de la transmission
    // Bits 6-7 : Clutch Switch (01 = Débrayé/Tiré, 10 = Embrayé/Lâché)
    clutchSwitchActive = ((data[0] >> 6) & 0x03) == 0x01;

    // Octet 1 : Rapport sélectionné (0 = Neutral, 1-6 = Vitesses, 251 = Parc)
    selectedGearRaw = data[1];

    // Octet 2-3 : Vitesse d'entrée de boîte (Transmission Input Shaft Speed)
    // Format : 2 octets Little Endian, Résolution : 0.125 RPM / bit
    uint16_t inputSpeedRaw = (data[3] << 8) | data[2];
    if (inputSpeedRaw < 0xFAFF) {
        transmissionInputSpeed = inputSpeedRaw * 0.125;
    }

    // Octet 4 : Masque spécifique Polaris/Indian pour la sécurité béquille
    // Bit 0 : Kickstand Switch (1 = Sécurité active / Béquille sortie)
    kickstandSwitchOut = (data[4] & 0x01) == 0x01;
}

// =========================================================================
// 3. EXTRACTEUR EXHAUSTIF DU PGN 0xFD7D (LSTD - Lighting Status Display)
// =========================================================================
// Variables PGN 0xFD7D
bool runningLightsActive = false;       // Feux de position / Veilleuses
bool lowBeamActive = false;             // Feux de croisement (Codes)
bool highBeamActive = false;            // Feux de route (Pleins phares)
bool turnSignalLeft = false;            // Clignotant Gauche actif
bool turnSignalRight = false;           // Clignotant Droit actif
bool hazardLightsActive = false;        // Feux de détresse (Warnings)
bool brakeLightsActive = false;         // Feu stop arrière allumé

void extraireToutPGN_FD7D(const uint8_t* data) {
    // Octet 0 : Commandes d'éclairage principales
    // Bits 0-1 : Running Lights (Veilleuses)
    runningLightsActive = (data[0] & 0x03) == 0x01;
    // Bits 2-3 : Low Beam Status (Feux de croisement)
    lowBeamActive = ((data[0] >> 2) & 0x03) == 0x01;
    // Bits 4-5 : High Beam Status (Feux de route)
    highBeamActive = ((data[0] >> 4) & 0x03) == 0x01;

    // Octet 1 : Indicateurs de direction et freinage
    // Bits 0-1 : Left Turn Signal (Clignotant Gauche)
    turnSignalLeft = (data[1] & 0x03) == 0x01;
    // Bits 2-3 : Right Turn Signal (Clignotant Droit)
    turnSignalRight = ((data[1] >> 2) & 0x03) == 0x01;

    // Octet 2 : Feux spéciaux
    // Bits 0-1 : Hazard Signal (Warnings)
    hazardLightsActive = (data[2] & 0x03) == 0x01;
    // Bits 4-5 : Brake Light Status (Feu Stop physique activé par la centrale de puissance)
    brakeLightsActive = ((data[2] >> 4) & 0x03) == 0x01;
}

// =========================================================================
// INTÉGRATION LOGIQUE DANS VOTRE TÂCHE FREE RTOS EXISTANTE
// =========================================================================
void tache_EcouteCAN(void * pvParameters) {
    twai_message_t messageRx;

    while(1) {
        if (twai_receive(&messageRx, portMAX_DELAY) == ESP_OK) {
            if (messageRx.extd && !messageRx.rtr) {
                uint32_t id = messageRx.identifier;
                uint32_t pgn = (id >> 8) & 0x3FFFF;

                switch(pgn) {
                    case 0x3F001: case 0x0F001:
                        extraireToutPGN_F001(messageRx.data);
                        break;

                    case 0x3F005: case 0x0F005:
                        extraireToutPGN_F005(messageRx.data);
                        break;

                    case 0x3FD7D: case 0x0FD7D:
                        extraireToutPGN_FD7D(messageRx.data);
                        break;
                        
                    // ... Conservez vos autres PGN (RPM, Vitesse, Température) ...
                }
                
                // Débogage analytique toutes les 2 secondes
                static unsigned long timerDiag = 0;
                if (millis() - timerDiag > 2000) {
                    timerDiag = millis();
                    Serial.println("\n📊 --- DIAGNOSTIC COMPLET INDIAN SCOUT ---");
                    Serial.printf("🛑 FREINS     : Levier/Pedale: %s | Feu Stop: %s | ABS Actif: %s\n", brakePedalSwitch ? "ON" : "OFF", brakeLightsActive ? "ON" : "OFF", antiLockBrakeActive ? "YES" : "NO");
                    Serial.printf("🦿 SÉCURITÉ   : Béquille Sortie: %s | Embrayage Tiré: %s\n", kickstandSwitchOut ? "OUI" : "NON", clutchSwitchActive ? "OUI" : "NON");
                    Serial.printf("💡 ÉCLAIRAGE  : Croisement: %s | Route: %s | Clignotants: [G:%s D:%s]\n", lowBeamActive ? "ON" : "OFF", highBeamActive ? "ON" : "OFF", turnSignalLeft ? "<-" : " ", turnSignalRight ? "->" : " ");
                    Serial.printf("⚙️ BOÎTE      : Rapport: %d | RPM Entrée Boîte: %0.0f tr/min\n", selectedGearRaw, transmissionInputSpeed);
                }
            }
        }
    }
}

*******************************************************************************/