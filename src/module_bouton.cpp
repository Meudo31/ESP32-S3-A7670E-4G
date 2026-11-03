#include "configuration.h"
#include "module_bouton.h"
#include "module_journal.h"
#include "module_neopixels.h"
#include "module_systeme.h"

namespace {

TaskHandle_t tacheDuBouton = nullptr;

const uint32_t DUREE_ANTI_REBOND_MS = 40;
const uint32_t DUREE_D_UN_APPUI_LONG_MS = 1500;

bool boutonEnfonce() { return digitalRead(BROCHE_BOUTON_ANIMATIONS) == LOW; }   // le bouton relie la broche à la masse

void IRAM_ATTR quandLeBoutonEstEnfonce() {
    BaseType_t uneTachePlusPrioritaireEstReveillee = pdFALSE;
    vTaskNotifyGiveFromISR(tacheDuBouton, &uneTachePlusPrioritaireEstReveillee);
    portYIELD_FROM_ISR(uneTachePlusPrioritaireEstReveillee);
}

void tacheBouton(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);           // attend un front descendant
        vTaskDelay(pdMS_TO_TICKS(DUREE_ANTI_REBOND_MS));
        if (!boutonEnfonce()) continue;                    // simple parasite

        const uint32_t debutDeLAppui = millis();
        bool appuiLongDejaTraite = false;
        while (boutonEnfonce()) {
            if (!appuiLongDejaTraite && millis() - debutDeLAppui >= DUREE_D_UN_APPUI_LONG_MS) {
                appuiLongDejaTraite = true;
                allumerOuEteindreLesNeopixels();
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!appuiLongDejaTraite) passerALAnimationSuivante();

        vTaskDelay(pdMS_TO_TICKS(DUREE_ANTI_REBOND_MS));
        ulTaskNotifyTake(pdTRUE, 0);                       // oublie les rebonds du relâchement
    }
}

} // namespace

bool demarrerModuleBouton() {
    pinMode(BROCHE_BOUTON_ANIMATIONS, INPUT_PULLUP);
    if (!creerTacheSurveillee(tacheBouton, "Bouton", 3072, PRIORITE_NORMALE, COEUR_TRAITEMENTS, &tacheDuBouton)) return false;
    attachInterrupt(digitalPinToInterrupt(BROCHE_BOUTON_ANIMATIONS), quandLeBoutonEstEnfonce, FALLING);
    ecrireDansLeJournal("[BOUTON] Bouton prêt sur GPIO %d.", BROCHE_BOUTON_ANIMATIONS);
    return true;
}
