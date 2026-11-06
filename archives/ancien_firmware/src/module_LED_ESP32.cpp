#include <Adafruit_NeoPixel.h>
#include "module_LED_ESP32.h"


#define LED_INTEGREE_PERIODE 500
#define LED_INTEGREE_PERIODE_HEAT_BEAT 10

Adafruit_NeoPixel pixel(NUM_PIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);


bool initialisationLEDIntegree () {
    pinMode(LED_INTEGREE_GPIO, OUTPUT);
    digitalWrite(LED_INTEGREE_GPIO, LOW); // Éteindre la LED au démarrage
    my_log_printf("[⚙️ SETUP] LED intégrée OK.");
    return true;
}

void taskClignotementLED(void * pvParameters) { // Déclenché par vTaskDelay dans la fonction
  my_log_printf("[Cœur 1] Tâche LED démarrée sur le cœur : %s", xPortGetCoreID());

  for(;;) {
    digitalWrite(LED_INTEGREE_GPIO, HIGH);
    vTaskDelay(pdMS_TO_TICKS(LED_INTEGREE_PERIODE));

    digitalWrite(LED_INTEGREE_GPIO, LOW);
    vTaskDelay(pdMS_TO_TICKS(LED_INTEGREE_PERIODE));
  }
}

void taskHeartBeatLED (void * pvParameters) {  // Déclenché par vTaskDelay dans la fonction
  my_log_printf("[Cœur 1] Tâche LED démarrée sur le cœur : %s", xPortGetCoreID());
  
  for(;;) {
    for(int dutyCycle = 0; dutyCycle <= 255; dutyCycle++){   // increase the LED brightness
      
      analogWrite(BUILTIN_LED, dutyCycle);  // changing the LED brightness with PWM
      vTaskDelay(pdMS_TO_TICKS(LED_INTEGREE_PERIODE_HEAT_BEAT));  
    }
    
    for(int dutyCycle = 255; dutyCycle >= 0; dutyCycle--){  // decrease the LED brightness
      analogWrite(BUILTIN_LED, dutyCycle);
      vTaskDelay(pdMS_TO_TICKS(LED_INTEGREE_PERIODE_HEAT_BEAT));
    }
  }
}


bool initialisationNeopixelIntegree() {
    pixel.begin(); // Initialise le NeoPixel
    pixel.setBrightness(50); // Luminosité à 50% (pour économiser la batterie)
    return true;
}

void taskNeopixelAnimationSimple(void * pvParameters) {
  while(1){
    // Étape 1 : Allumer en VERT (Succès opération)
    pixel.setPixelColor(0, pixel.Color(0, 255, 0)); // R=0, G=255, B=0
    pixel.show();
    vTaskDelay(1000);

    // // Étape 2 : Allumer en ROUGE (Erreur ou alerte)
    // pixel.setPixelColor(0, pixel.Color(255, 0, 0)); // R=255, G=0, B=0
    // pixel.show();
    // vTaskDelay(1000);

    // Étape 3 : Éteindre la LED
    pixel.clear();
    pixel.show();
    vTaskDelay(2000);
  }
}
