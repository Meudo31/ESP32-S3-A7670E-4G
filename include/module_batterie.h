#ifndef MODULE_BATTERIE_H
#define MODULE_BATTERIE_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE BATTERIE ET ALIMENTATION
   - Jauge MAX17048 (I2C) : tension de la batterie 18650, pourcentage de charge,
     vitesse de charge ou de décharge (%/heure), alertes.
   - Source d'alimentation : la carte ne relie aucune broche aux entrées USB et
     12 V. Sans câblage supplémentaire, on la DÉDUIT de la jauge (« en charge » =
     alimentation externe présente). Pour savoir exactement si le 12 V ou l'USB
     est présent, câbler un pont diviseur et renseigner BROCHE_MESURE_12V /
     BROCHE_MESURE_USB dans configuration.h.
   - Tension de la batterie vue par le modem (AT+CBC), en complément.
   La tâche lit tout cela toutes les 5 secondes.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

enum class SourceDAlimentation : uint8_t { INCONNUE, BATTERIE, EXTERNE, USB, ENTREE_12V, USB_ET_12V };

struct EtatDeLaBatterie {
    bool   jaugePresente;
    float  tensionVolts;
    float  chargePourcent;
    float  variationPourcentParHeure;   // > 0 : en charge, < 0 : en décharge
    bool   enCharge;
    bool   pleine;
    uint8_t alertes;                    // bits de la jauge (tension basse, haute, charge basse...)
    float  tension12V;                  // 0 si non câblée
    float  tensionUSB;                  // 0 si non câblée
    SourceDAlimentation source;
    float  autonomieHeures;             // estimation en décharge, -1 si inconnue
    uint16_t versionDeLaJauge;
};

EtatDeLaBatterie etatDeLaBatterie();
const char* nomDeLaSourceDAlimentation(SourceDAlimentation source);

void decrireLaBatterie(JsonDocument& document);

// À appeler après la détection de la carte (les broches I2C dépendent de la version).
bool demarrerModuleBatterie();

#endif // MODULE_BATTERIE_H
