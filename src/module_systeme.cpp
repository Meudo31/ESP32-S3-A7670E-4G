#include <esp_heap_caps.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_core_dump.h>

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
String          rapportDuDernierPlantage;   // "" = aucun plantage enregistré

// En dessous de cette pile restante, on prévient dans le journal.
const uint32_t PILE_RESTANTE_INQUIETANTE = 512;

void compterCeDemarrage() {
    Preferences memoirePermanente;
    memoirePermanente.begin("systeme", false);
    compteurDeDemarrages = memoirePermanente.getUInt("demarrages", 0) + 1;
    memoirePermanente.putUInt("demarrages", compteurDeDemarrages);
    memoirePermanente.end();
}

const char* nomDeLException(uint32_t cause) {
    switch (cause) {
        case 0:  return "instruction invalide";
        case 2:  return "erreur de lecture d'instruction";
        case 3:  return "erreur d'accès mémoire";
        case 6:  return "division par zéro";
        case 9:  return "accès mal aligné";
        case 28: return "lecture interdite (pointeur nul ?)";
        case 29: return "écriture interdite (pointeur nul ?)";
        default: return "autre";
    }
}

// Lors d'un plantage (exception, chien de garde), l'ESP32 enregistre l'état du programme dans la
// partition « coredump » de la flash. Au démarrage suivant, on en tire un résumé lisible :
// la tâche fautive et les adresses du programme où elle se trouvait (à décoder avec le fichier
// firmware.elf de la même compilation). Le résumé est gardé en mémoire permanente jusqu'au
// plantage suivant, puis l'enregistrement est effacé pour faire place au prochain.
void lireLeRapportDePlantage() {
    Preferences memoirePermanente;
    memoirePermanente.begin("systeme", false);
    size_t adresse = 0, taille = 0;
    if (esp_core_dump_image_get(&adresse, &taille) == ESP_OK && taille > 0) {
        esp_core_dump_summary_t* resume = (esp_core_dump_summary_t*)calloc(1, sizeof(esp_core_dump_summary_t));
        if (resume != nullptr && esp_core_dump_get_summary(resume) == ESP_OK) {
            char texte[300];
            int longueur = snprintf(texte, sizeof(texte), "Démarrage n°%u : %s, tâche « %.16s », PC 0x%08lx, %s (cause %lu). Pile :",
                                    (unsigned)compteurDeDemarrages, raisonDuDernierRedemarrage(), resume->exc_task,
                                    (unsigned long)resume->exc_pc, nomDeLException(resume->ex_info.exc_cause),
                                    (unsigned long)resume->ex_info.exc_cause);
            const uint32_t profondeur = min<uint32_t>(resume->exc_bt_info.depth, 10);
            for (uint32_t i = 0; i < profondeur && longueur > 0 && longueur < (int)sizeof(texte) - 12; i++) {
                longueur += snprintf(texte + longueur, sizeof(texte) - longueur, " 0x%08lx", (unsigned long)resume->exc_bt_info.bt[i]);
            }
            if (resume->exc_bt_info.corrupted && longueur > 0 && longueur < (int)sizeof(texte) - 14) {
                snprintf(texte + longueur, sizeof(texte) - longueur, " (pile abîmée)");
            }
            memoirePermanente.putString("plantage", texte);
            memoirePermanente.putUInt("plantages", memoirePermanente.getUInt("plantages", 0) + 1);
        }
        free(resume);
        esp_core_dump_image_erase();
    }
    rapportDuDernierPlantage = memoirePermanente.getString("plantage", "");
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

const String& rapportDePlantage() { return rapportDuDernierPlantage; }

void decrireLeDernierPlantage(JsonDocument& document) {
    document["t"]   = "plantage";
    document["txt"] = rapportDuDernierPlantage;
}

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

QueueHandle_t creerUneFileEnPSRAM(UBaseType_t nombreDElements, UBaseType_t tailleDUnElement) {
    uint8_t* stockage = (uint8_t*)heap_caps_malloc((size_t)nombreDElements * tailleDUnElement, MALLOC_CAP_SPIRAM);
    StaticQueue_t* structure = (StaticQueue_t*)heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (stockage != nullptr && structure != nullptr) {
        return xQueueCreateStatic(nombreDElements, tailleDUnElement, stockage, structure);
    }
    free(stockage);
    free(structure);
    return xQueueCreate(nombreDElements, tailleDUnElement);
}

size_t memoireInterneLibre()  { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
size_t plusGrandBlocInterne() { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }

bool demarrerModuleSysteme() {
    compterCeDemarrage();
    lireLeRapportDePlantage();
    ecrireDansLeJournal("[SYSTEME] Démarrage n°%u. Raison : %s.", (unsigned)compteurDeDemarrages, raisonDuDernierRedemarrage());
    ecrireDansLeJournal("[SYSTEME] %s rév. %d, %d cœurs à %u MHz, flash %u Mo, PSRAM %u Ko.",
                        ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), (unsigned)ESP.getCpuFreqMHz(),
                        (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)), (unsigned)(ESP.getPsramSize() / 1024));
    if (!rapportDuDernierPlantage.isEmpty()) ecrireDansLeJournal("[SYSTEME] Dernier plantage : %s", rapportDuDernierPlantage.c_str());
    return creerTacheSurveillee(tacheSysteme, "Systeme", 3072, PRIORITE_BASSE, COEUR_TRAITEMENTS);
}
