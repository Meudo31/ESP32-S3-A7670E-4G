// Test 4 : carte micro-SD en SDMMC 1 bit (CLK 5, CMD 4, D0 6, détection 46).
// Sur la carte V2, GPIO 46 sert aussi au PCLK de la caméra : l'état de la détection n'est qu'indicatif.
// OK si : la carte est montée, le fichier de test est écrit, relu à l'identique puis effacé.
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

#define SD_CLK_PIN 5
#define SD_CMD_PIN 4
#define SD_D0_PIN  6
#define SD_CD_PIN  46

const char* FICHIER_TEST = "/test_esp32.txt";

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n===== TEST CARTE SD ====="));

  pinMode(SD_CD_PIN, INPUT_PULLUP);
  Serial.printf("Broche de détection (GPIO %d) : %s\n", SD_CD_PIN,
                digitalRead(SD_CD_PIN) == LOW ? "carte présente" : "niveau haut (carte absente ou logique inversée)");

  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
  if (!SD_MMC.begin("/sdcard", true)) {  // true = mode 1 bit
    Serial.println(F("Échec du montage. Vérifier la carte (FAT32) et les broches."));
    return;
  }

  const char* types[] = {"aucune", "MMC", "SD", "SDHC/SDXC", "inconnu"};
  uint8_t type = SD_MMC.cardType();
  Serial.printf("Type        : %s\n", types[type <= 4 ? type : 4]);
  Serial.printf("Taille      : %llu Mo\n", SD_MMC.cardSize() / (1024 * 1024));
  Serial.printf("Utilisé     : %llu Mo sur %llu Mo\n", SD_MMC.usedBytes() / (1024 * 1024), SD_MMC.totalBytes() / (1024 * 1024));

  const String contenu = "Test ESP32-S3 A7670E, millis=" + String(millis());
  File f = SD_MMC.open(FICHIER_TEST, FILE_WRITE);
  if (!f) {
    Serial.println(F("Écriture : ÉCHEC (ouverture)"));
    return;
  }
  f.print(contenu);
  f.close();

  f = SD_MMC.open(FICHIER_TEST, FILE_READ);
  const String relu = f ? f.readString() : String();
  if (f) f.close();
  Serial.printf("Relecture   : %s\n", relu == contenu ? "OK" : "ÉCHEC");

  Serial.printf("Effacement  : %s\n", SD_MMC.remove(FICHIER_TEST) ? "OK" : "ÉCHEC");

  Serial.println(F("Contenu de la racine :"));
  File racine = SD_MMC.open("/");
  for (File e = racine.openNextFile(); e; e = racine.openNextFile()) {
    Serial.printf("  %s%s  %u octets\n", e.name(), e.isDirectory() ? "/" : "", (unsigned)e.size());
  }
  Serial.println(F("===== FIN ====="));
}

void loop() {}
