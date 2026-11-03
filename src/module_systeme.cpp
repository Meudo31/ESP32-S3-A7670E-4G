#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "configuration.h"
#include "module_systeme.h"
#include "module_journal.h"
#include "module_carte.h"

/* ──────────────────────────────────────────────────────────────────────────────
   Registre des tâches et informations sur le processeur et la mémoire.
   ────────────────────────────────────────────────────────────────────────────── */

namespace {

TacheSurveillee listeDesTaches[NOMBRE_MAXIMUM_DE_TACHES];
int             nombreDeTaches = 0;
uint32_t        compteurDeDemarrages = 0;

// En dessous de cette pile restante, on prévient dans le journal.
const uint32_t PILE_RESTANTE_INQUIETANTE = 512;

void compterCeDemarrage() {
    Preferences memoirePermanente;
    memoirePermanente.begin("systeme", false);
    compteurDeDemarrages = memoirePermanente.getUInt("demarrages", 0) + 1;
    memoirePermanente.putUInt("demarrages", compteurDeDemarrages);
    memoirePermanente.end();
}

void verifierLesPilesDesTaches() {
    static bool dejaSignale[NOMBRE_MAXIMUM_DE_TACHES] = {false};
    for (int numero = 0; numero < nombreDeTaches; numero++) {
        const uint32_t pileRestante = uxTaskGetStackHighWaterMark(listeDesTaches[numero].poignee);
        if (pileRestante < PILE_RESTANTE_INQUIETANTE && !dejaSignale[numero]) {
            dejaSignale[numero] = true;
            ecrireDansLeJournal("[SYSTEME] Attention : la tâche « %s » n'a plus que %u octets de pile.",
                                listeDesTaches[numero].nom, (unsigned)pileRestante);
        }
    }
}

void verifierLaMemoireLibre() {
    static bool dejaSignale = false;
    const uint32_t memoireLibre = ESP.getFreeHeap();
    if (memoireLibre < 20 * 1024 && !dejaSignale) {
        dejaSignale = true;
        ecrireDansLeJournal("[SYSTEME] Attention : il ne reste que %u octets de RAM interne.", (unsigned)memoireLibre);
    }
}

void tacheSysteme(void*) {
    for (;;) {
        verifierLesPilesDesTaches();
        verifierLaMemoireLibre();
        vTaskDelay(pdMS_TO_TICKS(30 * UNE_SECONDE_MS));
    }
}

} // namespace

// ─────────────────────────────── Registre des tâches ───────────────────────────────

bool creerTacheSurveillee(TaskFunction_t fonctionDeLaTache, const char* nom, uint32_t tailleDeLaPile,
                          UBaseType_t priorite, BaseType_t coeur, TaskHandle_t* poigneeCreee) {
    TaskHandle_t poignee = nullptr;
    if (xTaskCreatePinnedToCore(fonctionDeLaTache, nom, tailleDeLaPile, nullptr, priorite, &poignee, coeur) != pdPASS) {
        ecrireDansLeJournal("[SYSTEME] Impossible de créer la tâche « %s » (mémoire insuffisante ?).", nom);
        return false;
    }
    if (poigneeCreee != nullptr) *poigneeCreee = poignee;
    if (nombreDeTaches < NOMBRE_MAXIMUM_DE_TACHES) {
        listeDesTaches[nombreDeTaches++] = { nom, poignee, tailleDeLaPile, priorite, coeur };
    }
    return true;
}

int nombreDeTachesSurveillees() { return nombreDeTaches; }

const TacheSurveillee& tacheSurveillee(int numero) { return listeDesTaches[numero]; }

// ─────────────────────────────── Informations ───────────────────────────────

const char* raisonDuDernierRedemarrage() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "Mise sous tension";
        case ESP_RST_EXT:       return "Broche RESET";
        case ESP_RST_SW:        return "Redémarrage demandé";
        case ESP_RST_PANIC:     return "Plantage (exception)";
        case ESP_RST_INT_WDT:   return "Chien de garde (interruption)";
        case ESP_RST_TASK_WDT:  return "Chien de garde (tâche bloquée)";
        case ESP_RST_WDT:       return "Chien de garde";
        case ESP_RST_DEEPSLEEP: return "Réveil de veille profonde";
        case ESP_RST_BROWNOUT:  return "Baisse de tension";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "Inconnue";
    }
}

uint32_t nombreDeDemarrages() { return compteurDeDemarrages; }

void decrireLeProcesseur(JsonDocument& document) {
    const esp_partition_t* partitionActive = esp_ota_get_running_partition();
    const uint64_t adresseMac = ESP.getEfuseMac();   // adresse de base de la puce (octets dans l'ordre inverse)
    char texteMac[18];
    snprintf(texteMac, sizeof(texteMac), "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(adresseMac), (uint8_t)(adresseMac >> 8), (uint8_t)(adresseMac >> 16),
             (uint8_t)(adresseMac >> 24), (uint8_t)(adresseMac >> 32), (uint8_t)(adresseMac >> 40));

    document["t"]          = "sys";
    document["fw"]         = VERSION_DU_FIRMWARE;
    document["build"]      = __DATE__ " " __TIME__;
    document["carte"]      = nomDeLaVersionDeCarte();
    document["puce"]       = ESP.getChipModel();
    document["rev"]        = ESP.getChipRevision();
    document["coeurs"]     = ESP.getChipCores();
    document["mhz"]        = ESP.getCpuFreqMHz();
    document["temp"]       = temperatureRead();
    document["flash"]      = ESP.getFlashChipSize();
    document["flash_mhz"]  = ESP.getFlashChipSpeed() / 1000000;
    document["prog"]       = ESP.getSketchSize();
    document["prog_libre"] = ESP.getFreeSketchSpace();
    document["sdk"]        = ESP.getSdkVersion();
    document["mac"]        = texteMac;
    document["part"]       = partitionActive ? partitionActive->label : "?";
    document["up"]         = (uint32_t)(millis() / 1000);
    document["reset"]      = raisonDuDernierRedemarrage();
    document["boots"]      = compteurDeDemarrages;
}

void decrireLesMemoires(JsonDocument& document) {
    document["t"]               = "mem";
    document["ram_total"]       = ESP.getHeapSize();
    document["ram_libre"]       = ESP.getFreeHeap();
    document["ram_min"]         = ESP.getMinFreeHeap();
    document["ram_bloc"]        = ESP.getMaxAllocHeap();
    document["psram_total"]     = ESP.getPsramSize();
    document["psram_libre"]     = ESP.getFreePsram();
    document["psram_min"]       = ESP.getMinFreePsram();
    document["psram_bloc"]      = ESP.getMaxAllocPsram();
}

bool demarrerModuleSysteme() {
    compterCeDemarrage();
    ecrireDansLeJournal("[SYSTEME] Démarrage n°%u. Raison : %s.", (unsigned)compteurDeDemarrages, raisonDuDernierRedemarrage());
    ecrireDansLeJournal("[SYSTEME] %s rév. %d, %d cœurs à %u MHz, flash %u Mo, PSRAM %u Ko.",
                        ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), (unsigned)ESP.getCpuFreqMHz(),
                        (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)), (unsigned)(ESP.getPsramSize() / 1024));
    return creerTacheSurveillee(tacheSysteme, "Systeme", 3072, PRIORITE_BASSE, COEUR_TRAITEMENTS);
}
