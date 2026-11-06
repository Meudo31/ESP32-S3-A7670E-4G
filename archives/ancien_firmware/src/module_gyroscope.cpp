#include <Wire.h>
#include <Adafruit_ADXL345_U.h> // Bibliothèque unifiée d'Adafruit pour l'ADXL345
#include <Wire.h>


#include "module_gyroscope.h"

#define GRAVITE_TERRESTRE 9.81

GyroData lastGyroscopeData;

// Variables globales de calcul (internes à ce module)
Adafruit_ADXL345_Unified adxl = Adafruit_ADXL345_Unified(12345);

// Outils de synchronisation FreeRTOS pour l'interruption
SemaphoreHandle_t xSemMouvementADXL = NULL;

void IRAM_ATTR fonctionInterruptionADXL() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xSemMouvementADXL != NULL) {
        xSemaphoreGiveFromISR(xSemMouvementADXL, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void ecrireRegistreADXL(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(0x53); // Adresse I2C d'usine de l'ADXL345
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

bool initialiserGyroscope() {
    my_log_printf("🔄 [ADXL345] Initialisation du capteur de mouvement...");

    if (!Wire.begin(PIN_SDA, PIN_SCL, 100000)) return false;

    // Initialisation de la bibliothèque Adafruit
    if (!adxl.begin()) {
        my_log_printf("❌ [ADXL345_ERR] Capteur ADXL345 introuvable sur le bus I2C !");
        return false;
    }

    // Configuration de la plage de mesure (+/- 2G est idéal pour détecter les petits chocs)
    adxl.setRange(ADXL345_RANGE_2_G);

    // Création du sémaphore FreeRTOS
    xSemMouvementADXL = xSemaphoreCreateBinary();
    if (xSemMouvementADXL == NULL) return false;

    // Configuration du GPIO de l'ESP32-S3
    pinMode(PIN_ADXL_INT, INPUT_PULLDOWN); // L'ADXL345 génère un signal HAUT (3.3V) lors d'une alerte

    // --- CONFIGURATION DES REGISTRES DE CHOC (WoM) VIA LES CONSTANTES ADAFRUIT ---
    // 1 LSB = 62.5 mg. Une valeur de 4 = ~250 mg de secousse minimum pour déclencher
    ecrireRegistreADXL(ADXL345_REG_THRESH_ACT, 4); 
    
    // Activer la détection de mouvement sur les axes X, Y, et Z en mode couplage alternatif (AC)
    ecrireRegistreADXL(ADXL345_REG_ACT_INACT_CTL, 0x70); 
    
    // Mapper l'interruption d'activité sur la broche physique INT1 (0x00 = INT1)
    ecrireRegistreADXL(ADXL345_REG_INT_MAP, 0x00); 
    
    // Activer le commutateur d'interruption "Activity"
    ecrireRegistreADXL(ADXL345_REG_INT_ENABLE, 0x10); 

    // Attacher l'interruption sur le front montant (RISING)
    attachInterrupt(digitalPinToInterrupt(PIN_ADXL_INT), fonctionInterruptionADXL, RISING);

    my_log_printf("✅ [ADXL345] Capteur configuré et Alarme anti-intrusion armée.");
    return true;
}

void tacheLectureGyro(void * pvParameters) {
    my_log_printf("📊 [ADXL345_TASK] Mode sentinelle actif. Consommation CPU = 0%.");
    sensors_event_t event;

    while(1) {
        // Blocage total de la tâche (0% CPU) tant que le fil INT1 ne passe pas à 3.3V
        if (xQueueReceive(xSemMouvementADXL, NULL, portMAX_DELAY) == pdPASS) {
            
            my_log_printf("\n🚨 [ALERTE_ADXL345] MOUVEMENT / CHOC SUSPECT SUR LE VÉHICULE !");

            // Récupération des valeurs d'accélération au moment du choc
            adxl.getEvent(&event);

            // 1. Récupération des forces d'accélération
            adxl.getEvent(&event);
            float x = event.acceleration.x;
            float y = event.acceleration.y;
            float z = event.acceleration.z;

            // 2. CALCUL DU PITCH ET DU ROLL (En degrés)
            // Roll (Roulis) : inclinaison gauche/droite
            float roll = atan2(y, z) * 180.0 / M_PI;
            
            // Pitch (Tangage) : inclinaison avant/arrière
            float pitch = atan2(-x, sqrt(y * y + z * z)) * 180.0 / M_PI;

            // 3. Calcul de la force G globale subie
            float forceGTotal = sqrt(x * x + y * y + z * z) / GRAVITE_TERRESTRE;


            lastGyroscopeData.accelerationX     = x;
            lastGyroscopeData.accelerationY     = y;
            lastGyroscopeData.accelerationZ     = z;
            lastGyroscopeData.roll              = roll;
            lastGyroscopeData.pitch             = pitch;
            lastGyroscopeData.forceGTotal       = forceGTotal;

            // Affichage des calculs d'orientation spatiale dans la console
            my_log_printf("📊 [ADXL345] Accel mesurée -> X: %0.1f | Y: %0.1f | Z: %0.1f m/s²\n", 
                          event.acceleration.x, event.acceleration.y, event.acceleration.z);
            Serial.printf("📊 [ADXL345] Choc : %0.2f G | Pitch : %0.1f° | Roll : %0.1f°\n",
                          forceGTotal, pitch, roll);



            // REARMEMENT MATÉRIEL OBLIGATOIRE : Lecture du registre INT_SOURCE (0x30) pour effacer l'interruption
            Wire.beginTransmission(0x53);
            Wire.write(ADXL345_REG_INT_SOURCE); 
            Wire.endTransmission();
            Wire.requestFrom(0x53, 1);
            if(Wire.available()) { Wire.read(); } 

            // Pause de sécurité anti-rebond (5 secondes de calme imposé)
            vTaskDelay(pdMS_TO_TICKS(5000));
            
            // Nettoyage des résidus accumulés
            xQueueReset(xSemMouvementADXL);
            my_log_printf("📊 [ADXL345_TASK] Système réarmé. Retour en veille.");
        }
    }
}






/*
void tacheUpdateAllDataGyro_HW_290_GY_87_MPU6050_HMC5883L_BMP180(void * pvParameters) {
    my_log_printf("📊 [IMU_TASK] Démarrage des calculs physiques.");
    
    sensors_event_t a, g, temp;

    while(1) {
        // 1. Gestion du temps écoulé (Delta t précis pour les calculs de vitesse)
        unsigned long tempsActuelMicros = micros();
        float dt = (tempsActuelMicros - dernierTempsMicros) / 1000000.0; // Conversion en secondes
        dernierTempsMicros = tempsActuelMicros;

        // Évite un calcul aberrant au tout premier cycle ou après un bug de l'horloge
        if (dt <= 0.0 || dt > 1.0) dt = 0.1; 

        // 2. Acquisition des données brutes HMC5883L (Boussole) ---
        mpu.getEvent(&a, &g, &temp);
        lastGyroscopeData.accelerationX = a.acceleration.x;
        lastGyroscopeData.accelerationY = a.acceleration.y;
        lastGyroscopeData.accelerationZ = a.acceleration.z;

        // 3. Calcul des forces en G (Magnitude globale de l'accélération)
        float forceGTotal = sqrt(pow(a.acceleration.x, 2) + 
                                 pow(a.acceleration.y, 2) + 
                                 pow(a.acceleration.z, 2)) / GRAVITE_TERRESTRE;

        // 4. Calcul des angles d'inclinaison (en degrés)
        // Roll (Roulis) : inclinaison gauche/droite
        float roll = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / M_PI;
        // Pitch (Tangage) : inclinaison avant/arrière
        float pitch = atan2(-a.acceleration.x, sqrt(pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2))) * 180.0 / M_PI;

        // 5. Calcul de l'estimation de la vitesse linéaire (m/s)
        // Pour estimer la vitesse d'un déplacement, on retire l'effet de la pesanteur théorique (Z)
        float accPureZ = a.acceleration.z - GRAVITE_TERRESTRE;
        
        // Intégration mathématique (V = a * dt)
        vitesseX += a.acceleration.x * dt;
        vitesseY += a.acceleration.y * dt;
        vitesseZ += accPureZ * dt;

        // Vitesse scalaire globale résultante (conversion optionnelle en km/h : multiplier par 3.6)
        float vitesseGlobaleMS = sqrt(pow(vitesseX, 2) + pow(vitesseY, 2) + pow(vitesseZ, 2));


        lastGyroscopeData.accelerationX     = a.acceleration.x;
        lastGyroscopeData.accelerationY     = a.acceleration.y;
        lastGyroscopeData.accelerationZ     = a.acceleration.z;
        lastGyroscopeData.vitesseX          = vitesseX;
        lastGyroscopeData.vitesseY          = vitesseY;
        lastGyroscopeData.vitesseZ          = vitesseY;
        lastGyroscopeData.temperature       = temp.temperature;
        lastGyroscopeData.roll              = roll;
        lastGyroscopeData.pitch             = pitch;
        lastGyroscopeData.forceGTotal       = forceGTotal;
        lastGyroscopeData.vitesseGlobaleMS  = vitesseGlobaleMS;
        lastGyroscopeData.timeStamp         = Date_Heure();


        // --- 2. Lecture HMC5883L (Boussole) ---
        sensors_event_t event_mag;
        mag.getEvent(&event_mag);

        // Calcul du cap (Heading) en radians, puis en degrés
        float heading = atan2(event_mag.magnetic.y, event_mag.magnetic.x);
        
        // Ajustement de la déclinaison magnétique (Optionnel, environ +0.03 rad en France)
        float declinationAngle = 0.03;
        heading += declinationAngle;
        
        // Correction des angles si en dehors de [0, 2*PI]
        if(heading < 0) heading += 2*PI;
        if(heading > 2*PI) heading -= 2*PI;
        float headingDegrees = heading * 180/M_PI;

        lastGyroscopeData.champMagnetiqueX = event_mag.magnetic.x;
        lastGyroscopeData.champMagnetiqueY = event_mag.magnetic.y;
        lastGyroscopeData.champMagnetiqueZ = event_mag.magnetic.z;
        lastGyroscopeData.cap              = headingDegrees;

        my_log_printf("--- BOUSSOLE (HMC5883L) ---\n");
        my_log_printf("Champ magnétique (uT): X=%.2f, Y=%.2f, Z=%.2f\n",     event_mag.magnetic.x, event_mag.magnetic.y, event_mag.magnetic.z);
        my_log_printf("Orientation (Cap):      %.1f° (0=Nord, 90=Est...)\n", headingDegrees);



        // --- 3. Lecture BMP180 (Environnement) ---
        lastGyroscopeData.bmpTemperature = bmp.readTemperature();
        lastGyroscopeData.perssionAtmo   = bmp.readPressure();
        lastGyroscopeData.bmpAltitude    = bmp.readAltitude();


        my_log_printf("--- ENVIRONNEMENT (BMP180) ---\n");
        my_log_printf("Température:            %.1f °C\n",      bmp.readTemperature());
        my_log_printf("Pression:               %ld Pa\n",       bmp.readPressure());
        my_log_printf("Altitude estimée:       %.1f mètres\n",  bmp.readAltitude());


        // 6. Formatage des données calculées pour la carte SD
        // Structure de la ligne : G_Total;Pitch,Roll;Vitesse_MS
        String donneesCalculees = String(forceGTotal, 2)                        + ";" +
                                  String(pitch, 1) + "," + String(roll, 1)      + ";" +
                                  String(vitesseGlobaleMS, 2)                   + ";" +
                                  String(temp.temperature, 2)                   + ";" +
                                  String(event_mag.magnetic.x, 2)               + "," +
                                  String(event_mag.magnetic.y, 2)               + "," +
                                  String(event_mag.magnetic.z, 2)               + ";" +
                                  String(lastGyroscopeData.bmpTemperature, 2)   + ";" +
                                  String(lastGyroscopeData.perssionAtmo,   2)   + ";" +
                                  String(lastGyroscopeData.bmpAltitude,    2);
                                  
        // 7. Envoi au gestionnaire SD (Type 2 pour l'IMU)
        bool stockageOk = envoyerAuStockage(TypeDonneeSD::GYRO, donneesCalculees);

        if (!stockageOk) {
            my_log_printf("⚠️ [IMU_WARN] Tampon SD saturé, perte du calcul de dynamique.");
        }


        // Fréquence d'échantillonnage rapide (100 ms) pour une meilleure précision d'intégration
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void IRAM_ATTR fonctionInterruptionMPU() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xSemMouvementGyroDetecte != NULL) {
        xSemaphoreGiveFromISR(xSemMouvementGyroDetecte, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void tacheInterruptionMouvementGyro(void * pvParameters) {
    my_log_printf("📊 [IMU_TASK] Surveillance anti-intrusion active.");

    while(1) {
        // En veille totale tant qu'aucun mouvement n'est physique
        if (xSemaphoreTake(xSemMouvementGyroDetecte, portMAX_DELAY) == pdPASS) {
            
            my_log_printf("\n🚨 [ALERTE_IMU] MOUVEMENT SUSPECT DÉTECTÉ !");
            
            // --- LIEN CRITIQUE AVEC LA TÂCHE 1 (CAMÉRA) ---
            if (xTacheCameraHandle != NULL) {
                my_log_printf("📸 [IMU -> CAM] Envoi de l'ordre de capture immédiate (Scénario : Enregistrement SD)...");
                
                // On envoie l'ordre à la caméra. Ici, on demande le scénario CARTE_SD pour stocker la preuve de l'intrusion.
                xTaskNotify(xTacheCameraHandle, static_cast<uint32_t>(QueFaireDeLObjet_Values::ENVOIE_SMS), eSetValueWithOverwrite);

            } else {my_log_printf("❌ [IMU_ERR] Impossible de déclencher la photo : La Tâche 1 n'est pas initialisée !");}
            
            // Temporisation d'anti-rebond pour éviter de mitrailler des photos si le capteur continue de bouger
            vTaskDelay(pdMS_TO_TICKS(5000)); // Pause de 5 secondes après une alerte
            
            // Nettoyage de la file d'interruption pour réarmement propre
            xSemaphoreTake(xSemMouvementGyroDetecte, 0);
            my_log_printf("📊 [IMU_TASK] Zone sécurisée. Réarmement de l'alarme...");
        }
    }
}*/