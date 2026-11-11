#include <Adafruit_MAX1704X.h>
#include <Wire.h>

#include "configuration.h"
#include "module_batterie.h"
#include "module_carte.h"
#include "module_journal.h"
#include "module_reseau_4g.h"
#include "module_systeme.h"

namespace {

Adafruit_MAX17048  jauge;
EtatDeLaBatterie   etatActuel = {};
SemaphoreHandle_t  verrouDeLEtat = nullptr;

const float VARIATION_MINIMUM_POUR_ETRE_EN_CHARGE = 0.5f;   // %/h
const float TENSION_D_UNE_BATTERIE_PLEINE         = 4.15f;  // V
const float TENSION_MINIMUM_D_UNE_ENTREE_PRESENTE = 4.0f;   // V après le pont diviseur

float mesurerUneTensionParPontDiviseur(int broche, float rapportDuPont) {
    if (broche < 0) return 0.0f;
    return analogReadMilliVolts(broche) / 1000.0f * rapportDuPont;
}

SourceDAlimentation deduireLaSource(const EtatDeLaBatterie& etat) {
    const bool brochesCablees = BROCHE_MESURE_12V >= 0 || BROCHE_MESURE_USB >= 0;
    if (brochesCablees) {
        const bool presence12V = etat.tension12V > 9.0f;
        const bool presenceUSB = etat.tensionUSB > TENSION_MINIMUM_D_UNE_ENTREE_PRESENTE;
        if (presence12V && presenceUSB) return SourceDAlimentation::USB_ET_12V;
        if (presence12V) return SourceDAlimentation::ENTREE_12V;
        if (presenceUSB) return SourceDAlimentation::USB;
        return SourceDAlimentation::BATTERIE;
    }
    if (!etat.jaugePresente) return SourceDAlimentation::INCONNUE;
    // Sans broche de mesure : batterie qui se charge, ou pleine et stable = alimentation externe.
    if (etat.enCharge || (etat.pleine && etat.variationPourcentParHeure >= -0.1f)) return SourceDAlimentation::EXTERNE;
    return SourceDAlimentation::BATTERIE;
}

void lireLaBatterie(EtatDeLaBatterie& etat) {
    etat.tension12V = mesurerUneTensionParPontDiviseur(BROCHE_MESURE_12V, RAPPORT_PONT_DIVISEUR_12V);
    etat.tensionUSB = mesurerUneTensionParPontDiviseur(BROCHE_MESURE_USB, RAPPORT_PONT_DIVISEUR_USB);
    if (etat.jaugePresente) {
        etat.tensionVolts              = jauge.cellVoltage();
        etat.chargePourcent            = min(jauge.cellPercent(), 100.0f);
        etat.variationPourcentParHeure = jauge.chargeRate();
        etat.alertes                   = jauge.getAlertStatus();
        etat.enCharge                  = etat.variationPourcentParHeure > VARIATION_MINIMUM_POUR_ETRE_EN_CHARGE;
        etat.pleine                    = etat.tensionVolts >= TENSION_D_UNE_BATTERIE_PLEINE && etat.chargePourcent >= 95.0f;
        if (etat.alertes) jauge.clearAlertFlag(etat.alertes);
    }
    etat.source = deduireLaSource(etat);
    etat.autonomieHeures = (etat.variationPourcentParHeure < -0.1f)
                           ? etat.chargePourcent / -etat.variationPourcentParHeure : -1.0f;
}

void tacheBatterie(void*) {
    SourceDAlimentation sourcePrecedente = SourceDAlimentation::INCONNUE;
    bool alerteDeCharge20Signalee = false;
    for (;;) {
        EtatDeLaBatterie nouvelEtat = etatDeLaBatterie();
        lireLaBatterie(nouvelEtat);
        xSemaphoreTake(verrouDeLEtat, portMAX_DELAY);
        etatActuel = nouvelEtat;
        xSemaphoreGive(verrouDeLEtat);

        if (nouvelEtat.source != sourcePrecedente) {
            sourcePrecedente = nouvelEtat.source;
            ecrireDansLeJournal("[BATTERIE] Alimentation : %s (batterie %.2f V, %.0f %%).",
                                nomDeLaSourceDAlimentation(nouvelEtat.source), nouvelEtat.tensionVolts, nouvelEtat.chargePourcent);
        }
        if (nouvelEtat.jaugePresente && nouvelEtat.chargePourcent < 20 && !alerteDeCharge20Signalee) {
            alerteDeCharge20Signalee = true;
            ecrireDansLeJournal("[BATTERIE] Batterie faible : %.0f %%.", nouvelEtat.chargePourcent);
        }
        if (nouvelEtat.chargePourcent > 25) alerteDeCharge20Signalee = false;

        vTaskDelay(pdMS_TO_TICKS(5 * UNE_SECONDE_MS));
    }
}

} // namespace

EtatDeLaBatterie etatDeLaBatterie() {
    if (verrouDeLEtat == nullptr) return etatActuel;   // module pas encore démarré
    xSemaphoreTake(verrouDeLEtat, portMAX_DELAY);
    EtatDeLaBatterie copie = etatActuel;
    xSemaphoreGive(verrouDeLEtat);
    return copie;
}

const char* nomDeLaSourceDAlimentation(SourceDAlimentation source) {
    switch (source) {
        case SourceDAlimentation::BATTERIE:   return "batterie";
        case SourceDAlimentation::EXTERNE:    return "externe (USB ou 12 V)";
        case SourceDAlimentation::USB:        return "USB";
        case SourceDAlimentation::ENTREE_12V: return "12 V moto";
        case SourceDAlimentation::USB_ET_12V: return "USB et 12 V";
        default:                              return "inconnue";
    }
}

void decrireLaBatterie(JsonDocument& document) {
    const EtatDeLaBatterie etat = etatDeLaBatterie();
    document["t"]       = "bat";
    document["ok"]      = etat.jaugePresente ? 1 : 0;
    document["v"]       = etat.tensionVolts;
    document["pct"]     = etat.chargePourcent;
    document["taux"]    = etat.variationPourcentParHeure;
    document["charge"]  = etat.enCharge ? 1 : 0;
    document["pleine"]  = etat.pleine ? 1 : 0;
    document["alertes"] = etat.alertes;
    document["v12"]     = etat.tension12V;
    document["vusb"]    = etat.tensionUSB;
    document["mesure"]  = (BROCHE_MESURE_12V >= 0 || BROCHE_MESURE_USB >= 0) ? 1 : 0;
    document["source"]  = nomDeLaSourceDAlimentation(etat.source);
    document["autonomie"] = etat.autonomieHeures;
    document["jauge"]   = etat.versionDeLaJauge;
    document["vmodem"]  = etatDuReseau4G().tensionVueParLeModem;
    document["temp"]    = temperatureRead();
}

bool demarrerModuleBatterie() {
    verrouDeLEtat = xSemaphoreCreateMutex();
    if (verrouDeLEtat == nullptr) return false;
    const BrochesDeLaCarte& broches = brochesDeLaCarte();
    Wire.begin(broches.jaugeSDA, broches.jaugeSCL);
    etatActuel.jaugePresente = jauge.begin(&Wire);
    if (etatActuel.jaugePresente) {
        etatActuel.versionDeLaJauge = jauge.getChipID();
        ecrireDansLeJournal("[BATTERIE] Jauge MAX17048 trouvée (SDA %d, SCL %d).", broches.jaugeSDA, broches.jaugeSCL);
    } else {
        ecrireDansLeJournal("[BATTERIE] Jauge MAX17048 introuvable (SDA %d, SCL %d).", broches.jaugeSDA, broches.jaugeSCL);
    }
    if (BROCHE_MESURE_12V >= 0) pinMode(BROCHE_MESURE_12V, INPUT);
    if (BROCHE_MESURE_USB >= 0) pinMode(BROCHE_MESURE_USB, INPUT);
    return creerTacheSurveillee(tacheBatterie, "Batterie", 3072, PRIORITE_BASSE, COEUR_TRAITEMENTS);
}
