#ifndef MODULE_APPELS_H
#define MODULE_APPELS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE APPELS : appels vocaux par le modem.
   - Passer un appel, décrocher un appel entrant, raccrocher.
   - Pendant la sonnerie d'un appel entrant, le haut-parleur sonne.
   - Il n'y a pas encore de micro : on entend le correspondant, lui ne nous entend pas.
   La tâche suit l'état de l'appel (notifications du modem + vérification AT+CLCC)
   et prévient l'application à chaque changement.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

enum class EtatDUnAppel : uint8_t {
    AUCUN,              // pas d'appel
    SONNERIE,           // appel entrant, pas encore décroché
    NUMEROTATION,       // appel sortant, le correspondant n'a pas encore décroché
    EN_COMMUNICATION,
};

struct AppelEnCours {
    EtatDUnAppel etat;
    String       numero;
    uint32_t     debutMs;   // début de la communication (millis)
};

AppelEnCours appelEnCours();
const char* nomDeLEtatDUnAppel(EtatDUnAppel etat);

bool appeler(const String& numero);
bool decrocher();
bool raccrocher();

void decrireLAppelEnCours(JsonDocument& document);

bool demarrerModuleAppels();

#endif // MODULE_APPELS_H
