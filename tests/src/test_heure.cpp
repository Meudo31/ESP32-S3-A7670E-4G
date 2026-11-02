// Test 5 : l'heure système continue de tourner pendant un deep sleep.
// Au premier démarrage, l'heure est réglée sur celle de la compilation, puis la carte
// dort 20 s, se réveille, compare l'heure à celle attendue, et recommence.
// OK si : l'écart affiché reste de l'ordre de la seconde au fil des cycles.
#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#define DUREE_SOMMEIL_S 20

// Conservées pendant le deep sleep (RAM du RTC).
RTC_DATA_ATTR time_t heureAvantSommeil = 0;
RTC_DATA_ATTR int nbReveils = 0;

void afficherHeure(const char* prefixe) {
  time_t maintenant = time(nullptr);
  struct tm t;
  localtime_r(&maintenant, &t);
  char texte[32];
  strftime(texte, sizeof(texte), "%d/%m/%Y %H:%M:%S", &t);
  Serial.printf("%s : %s\n", prefixe, texte);
}

// Convertit __DATE__ ("Nov  2 2026") et __TIME__ ("13:45:00") en heure système.
void reglerHeureCompilation() {
  const char* mois = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char m[4] = {0};
  struct tm t = {};
  sscanf(__DATE__, "%3s %d %d", m, &t.tm_mday, &t.tm_year);
  sscanf(__TIME__, "%d:%d:%d", &t.tm_hour, &t.tm_min, &t.tm_sec);
  t.tm_mon = (strstr(mois, m) - mois) / 3;
  t.tm_year -= 1900;
  struct timeval tv = {mktime(&t), 0};
  settimeofday(&tv, nullptr);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n===== TEST DATE ET HEURE ====="));

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    nbReveils++;
    const time_t attendu = heureAvantSommeil + DUREE_SOMMEIL_S;
    const long ecart = (long)(time(nullptr) - attendu);
    Serial.printf("Réveil n°%d par la minuterie\n", nbReveils);
    afficherHeure("Heure système");
    Serial.printf("Écart par rapport à l'heure attendue : %+ld s (le démarrage compte pour ~1 s)\n", ecart);
  } else {
    Serial.println(F("Premier démarrage : réglage sur l'heure de compilation"));
    reglerHeureCompilation();
    nbReveils = 0;
    afficherHeure("Heure système");
  }

  Serial.printf("Deep sleep pendant %d s...\n", DUREE_SOMMEIL_S);
  Serial.flush();
  heureAvantSommeil = time(nullptr);
  esp_sleep_enable_timer_wakeup((uint64_t)DUREE_SOMMEIL_S * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}
