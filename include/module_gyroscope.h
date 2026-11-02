#ifndef CONFIG_GYRO_H
#define CONFIG_GYRO_H
#include "main.h"

/*L'ADXL345 fonctionne en I2C sur l'adresse standard 0x53. 
    VCC ➡️ 3V3
    GND ➡️ GND
    SDA ➡️ GPIO 7 (Bus partagé)
    SCL ➡️ GPIO 6 (Bus partagé)
    INT1 (Broche d'interruption) ➡️ GPIO 1 de l'ESP32-S3
    CS ➡️ 3V3 (Indispensable pour forcer le module en mode I2C) [1] (https://www.bettlink.com/blog/adxl345-guide)*/

// Broches I2C définies pour la carte Waveshare
#define PIN_SDA 7
#define PIN_SCL 6
#define PIN_ADXL_INT 1

struct GyroData {
    // HMC5883L (Boussole) ---
    float accelerationX;
    float accelerationY;
    float accelerationZ;
    float vitesseX;
    float vitesseY;
    float vitesseZ;
    float temperature;
    float roll;
    float pitch;
    float forceGTotal;
    float vitesseGlobaleMS;
    String timeStamp;

    // HMC5883L (Boussole) ---
    float champMagnetiqueX;
    float champMagnetiqueY;
    float champMagnetiqueZ;
    float cap;

    // BMP180 (Environnement) ---
    float bmpTemperature;
    float perssionAtmo;
    float bmpAltitude;
};


extern GyroData lastGyroscopeData;

// Prototype de la fonction globale de stockage (définie dans votre module SD)
// extern bool envoyerAuStockage(int type, const char* date, const char* heure, String texte, void* fb = NULL);

bool initialiserGyroscope();

void tacheLectureGyro(void * pvParameters);


#endif