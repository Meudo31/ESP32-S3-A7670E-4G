#include <Wire.h>

#include "module_batterie.h"


Adafruit_MAX17048 jaugeBatterie;

bool initialisationJaugeBatterie() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (jaugeBatterie.begin()) {
        my_log_printf("[⚙️ SETUP] Jauge Batterie OK. Niveau : %.1f %%\n", jaugeBatterie.cellPercent());
        return true;
    } else {
        my_log_printf("[⚙️ SETUP] ⚠️ Alerte : Jauge MAX17048 introuvable sur le bus I2C !");
        return false;
    }
}

void taskBatterieESP32AfficheInfos(void * pvParameters) {  // Déclenché par vTaskDelay dans la fonction
    for(;;) {
        // On affiche la batterie interne toutes les 5 minutes pour vérification.
        my_log_printf("[🔋 MONITOR] Tension cellule 18650 : %.2f V | Charge : %.1f %%\n", 
                    jaugeBatterie.cellVoltage(), jaugeBatterie.cellPercent());

        vTaskDelay(xDelai_CinqMinutes); 
        }
}
