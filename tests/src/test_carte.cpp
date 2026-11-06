// Test 2 : identité de la puce, mémoire flash et PSRAM.
// OK si : flash = 16 Mo et PSRAM ≈ 8 Mo. LED verte = OK, rouge = PSRAM absente.
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define PIN_LED_RGB 38

Adafruit_NeoPixel pixel(1, PIN_LED_RGB, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(1000);
  pixel.begin();
  pixel.setBrightness(30);

  Serial.println(F("\n===== TEST CARTE ====="));
  Serial.printf("Puce        : %s rév. %d, %d cœurs à %u MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), (unsigned)ESP.getCpuFreqMHz());
  Serial.printf("Flash       : %u Mo\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("RAM interne : %u Ko libres sur %u Ko\n", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024));

  const bool psramOk = psramFound() && ESP.getPsramSize() > 0;
  if (psramOk) {
    Serial.printf("PSRAM       : %u Ko libres sur %u Ko\n", (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(ESP.getPsramSize() / 1024));

    // Écriture / relecture d'un bloc de 1 Mo en PSRAM pour vérifier qu'elle est fiable.
    const size_t taille = 1024 * 1024;
    uint8_t* bloc = (uint8_t*)ps_malloc(taille);
    bool lectureOk = bloc != nullptr;
    if (bloc) {
      for (size_t i = 0; i < taille; i++) bloc[i] = (uint8_t)(i * 7);
      for (size_t i = 0; i < taille && lectureOk; i++) lectureOk = bloc[i] == (uint8_t)(i * 7);
      free(bloc);
    }
    Serial.printf("Test PSRAM 1 Mo : %s\n", lectureOk ? "OK" : "ÉCHEC");
    pixel.setPixelColor(0, lectureOk ? pixel.Color(0, 255, 0) : pixel.Color(255, 0, 0));
  } else {
    Serial.println(F("PSRAM       : ABSENTE. Vérifier memory_type (qio_opi pour R8, qio_qspi pour R2)."));
    pixel.setPixelColor(0, pixel.Color(255, 0, 0));
  }
  pixel.show();
  Serial.println(F("===== FIN ====="));
}

void loop() {}
