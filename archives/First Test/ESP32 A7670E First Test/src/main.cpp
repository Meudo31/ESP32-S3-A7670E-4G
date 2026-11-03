#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define PIN_LED_RGB 38 // WS2812B intégrée de la Waveshare ESP32-S3 A7670E
#define NUM_PIXELS 1
#define PERIODE_FADE 10 // ms entre deux pas de variation

Adafruit_NeoPixel pixel(NUM_PIXELS, PIN_LED_RGB, NEO_GRB + NEO_KHZ800);

int dutyCycle = 0;
bool descente = false;

void setup() {
  Serial.begin(115200);

  pixel.begin();
  pixel.setBrightness(50);

  // 3 flashs blancs = la carte a bien démarré
  for (int i = 0; i < 3; i++) {
    pixel.setPixelColor(0, pixel.Color(255, 255, 255));
    pixel.show();
    delay(250);
    pixel.clear();
    pixel.show();
    delay(750);
  }

  String thisFilePath = __FILE__;
  Serial.printf("Démarrage terminé après %lu ms\n", millis());
  Serial.println("----- " + thisFilePath.substring(thisFilePath.lastIndexOf('\\') + 1)
                 + " compilé le " __DATE__ " à " __TIME__ " -----");
  Serial.println(F("------------------------ MEUDO ------------------------"));
}

void loop() {
  pixel.setPixelColor(0, pixel.Color(dutyCycle,0, 0)); // vert
  pixel.show();

  // Respiration : monte jusqu'à 255, puis redescend jusqu'à 0
  if (!descente) { if (++dutyCycle >= 255) descente = true; }
  else { if (--dutyCycle <= 0) descente = false; }

  // Un message toutes les 500 ms seulement
  static uint32_t dernierLog = 0;
  if (millis() - dernierLog >= 500) {
    dernierLog = millis();
    Serial.printf("descente: %d dutyCycle: %d\n", descente, dutyCycle);
  }

  delay(PERIODE_FADE);
}