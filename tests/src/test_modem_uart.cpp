// Test 6 : dialogue AT avec le modem A7670E par UART.
// Le schéma V2 relie GPIO 17 et 18 au modem (TXD1/RXD1) à travers un adaptateur de niveau.
// Le sens n'est pas documenté : le test essaie les deux, envoie "AT" et garde celui qui répond.
// Ensuite, le moniteur série devient un terminal AT (taper une commande, Entrée).
// Prérequis : DIP 4G sur ON, antenne LTE, carte SIM sans code PIN.
// OK si : "AT" répond OK, puis ATI, AT+CPIN?, AT+CSQ et AT+CEREG? donnent des réponses cohérentes.
#include <Arduino.h>

HardwareSerial modem(1);

String envoyerAT(const char* commande, uint32_t attenteMs = 1000) {
  while (modem.available()) modem.read();
  modem.print(commande);
  modem.print("\r\n");
  String reponse;
  const uint32_t debut = millis();
  while (millis() - debut < attenteMs) {
    while (modem.available()) reponse += (char)modem.read();
    if (reponse.indexOf("OK\r\n") >= 0 || reponse.indexOf("ERROR") >= 0) break;
  }
  reponse.trim();
  return reponse;
}

bool essayer(int rx, int tx) {
  Serial.printf("Essai RX=%d TX=%d à 115200 bauds : ", rx, tx);
  modem.end();
  modem.begin(115200, SERIAL_8N1, rx, tx);
  // Le modem met plusieurs secondes à démarrer après la mise sous tension.
  for (int i = 0; i < 10; i++) {
    if (envoyerAT("AT", 500).indexOf("OK") >= 0) {
      Serial.println(F("le modem répond"));
      return true;
    }
    delay(1000);
  }
  Serial.println(F("pas de réponse"));
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n===== TEST MODEM (UART) ====="));

  if (!essayer(17, 18) && !essayer(18, 17)) {
    Serial.println(F("Aucune réponse. Vérifier le DIP 4G, attendre 20 s après la mise sous tension et relancer (RESET)."));
    Serial.println(F("Si rien ne répond, le modem n'est joignable qu'en USB (DIP USB sur OFF)."));
    return;
  }

  const char* commandes[] = {"ATE0", "ATI", "AT+CPIN?", "AT+CSQ", "AT+CEREG?", "AT+COPS?", "AT+CCLK?"};
  for (const char* c : commandes) {
    Serial.printf("> %s\n%s\n", c, envoyerAT(c, 3000).c_str());
  }
  Serial.println(F("\nTerminal AT prêt (régler le moniteur pour envoyer CR+LF)."));
}

void loop() {
  while (Serial.available()) modem.write(Serial.read());
  while (modem.available()) Serial.write(modem.read());
}
