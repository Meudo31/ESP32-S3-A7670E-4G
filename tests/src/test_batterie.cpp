// Test 3 : jauge de batterie MAX17048 sur I2C (SDA 15, SCL 16, carte V2).
// OK si : l'adresse 0x36 est trouvée et la tension correspond à celle mesurée au multimètre.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>

#define I2C_SDA_PIN 15
#define I2C_SCL_PIN 16

Adafruit_MAX17048 jauge;
bool jaugeOk = false;

void scannerI2C() {
  Serial.println(F("Scan du bus I2C :"));
  int nb = 0;
  for (uint8_t adresse = 1; adresse < 127; adresse++) {
    Wire.beginTransmission(adresse);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  périphérique trouvé en 0x%02X%s\n", adresse, adresse == 0x36 ? " (MAX17048)" : "");
      nb++;
    }
  }
  if (nb == 0) Serial.println(F("  aucun périphérique. Vérifier les broches SDA/SCL."));
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n===== TEST BATTERIE ====="));
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  scannerI2C();

  jaugeOk = jauge.begin(&Wire);
  if (!jaugeOk) {
    Serial.println(F("MAX17048 introuvable."));
    return;
  }
  Serial.printf("MAX17048 trouvé, version de puce 0x%04X\n", jauge.getChipID());
}

void loop() {
  if (!jaugeOk) return;
  Serial.printf("Tension %.3f V | Charge %.1f %% | Variation %.1f %%/h\n",
                jauge.cellVoltage(), jauge.cellPercent(), jauge.chargeRate());
  delay(2000);
}
