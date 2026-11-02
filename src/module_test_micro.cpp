#include "module_test_micro.h"
#include "driver/i2s.h" // Pilote natif I2S d'Espressif
#include <math.h>

// Configuration du format audio
#define I2S_PORT            I2S_NUM_0
#define TAILLE_BUFFER_AUDIO 512         // Nombre d'échantillons par lecture (32 ms à 16 kHz)

// Tampons statiques : ils restent hors de la pile de la tâche
static int32_t bufferBrut[TAILLE_BUFFER_AUDIO];
static int16_t bufferPCM[TAILLE_BUFFER_AUDIO];

static volatile float niveauNormalise = 0.0f;
static volatile bool  voixDetectee    = false;
static volatile bool  fluxActif       = false;
static StreamBufferHandle_t fluxMicro = NULL;

bool initialiserTestMicrophone() {
    my_log_printf("🎙️ [AUDIO] Configuration du périphérique I2S pour l'INMP441 (SCK=%d WS=%d SD=%d)...", I2S_PIN_BCK, I2S_PIN_WS, I2S_PIN_SD);

    // 1. Définition de la configuration structurelle du bus I2S
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), // L'ESP32 est maître, il reçoit la donnée
        .sample_rate = MICRO_FREQ_ECHANTILLONNAGE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,        // L'INMP441 transmet des échantillons sur 24/32 bits
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,         // Mode Mono (Canal gauche connecté au GND)
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,            // Priorité de l'interruption
        .dma_buf_count = 4,                                  // Nombre de buffers DMA
        .dma_buf_len = TAILLE_BUFFER_AUDIO,                  // Taille de chaque buffer
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    // 2. Définition du routage des broches physiques
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_PIN_BCK,
        .ws_io_num = I2S_PIN_WS,
        .data_out_num = I2S_PIN_NO_CHANGE, // Pas de sortie audio : le haut-parleur passe par le modem
        .data_in_num = I2S_PIN_SD
    };

    // 3. Installation et application du pilote
    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) {
        my_log_printf("❌ [AUDIO_ERR] Échec de l'installation du pilote I2S !");
        return false;
    }

    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
        my_log_printf("❌ [AUDIO_ERR] Échec du mappage des broches I2S !");
        return false;
    }

    fluxMicro = xStreamBufferCreate(MICRO_TAILLE_FLUX_OCTETS, 1);
    if (fluxMicro == NULL) {
        my_log_printf("❌ [AUDIO_ERR] Mémoire insuffisante pour le flux audio.");
        return false;
    }

    my_log_printf("✅ [AUDIO] Microphone INMP441 initialisé.");
    return true;
}

float  micNumeriqueNiveau()       { return niveauNormalise; }
bool   micNumeriqueVoixDetectee() { return voixDetectee; }

void micNumeriqueActiverFlux(bool actif) {
    if (actif && fluxMicro) xStreamBufferReset(fluxMicro);   // repart d'un tampon vide : pas de vieil audio
    fluxActif = actif;
}

size_t micNumeriqueLire(int16_t* destination, size_t nbEchantillons, TickType_t attente) {
    if (!fluxMicro) return 0;
    return xStreamBufferReceive(fluxMicro, destination, nbEchantillons * sizeof(int16_t), attente) / sizeof(int16_t);
}

void tacheTestMicro(void * pvParameters) {
    my_log_printf("🎙️ [AUDIO_TASK] Démarrage de l'analyse du son ambiant.");

    size_t octetsLus = 0;
    float  bruitDeFond = 40.0f;      // estimation continue du bruit ambiant (échelle 16 bits)
    float  niveauLisse = 0.0f;
    int    trameVoix = 0;            // buffers consécutifs au-dessus du seuil
    int    trameSilence = 0;         // buffers consécutifs sans voix, pour le maintien
    int    compteurAffichage = 0;

    while(1) {
        // Lecture bloquante avec délai maximal : si le micro ne répond pas, la tâche ne reste pas bloquée indéfiniment
        esp_err_t resultat = i2s_read(I2S_PORT, bufferBrut, sizeof(bufferBrut), &octetsLus, pdMS_TO_TICKS(200));
        if (resultat != ESP_OK || octetsLus == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t nombreEchantillons = octetsLus / sizeof(int32_t);

        // Conversion 32 bits ➡️ 16 bits avec écrêtage, et calcul du volume (RMS)
        int64_t sommeCarres = 0;
        for (size_t i = 0; i < nombreEchantillons; i++) {
            int32_t s = bufferBrut[i] >> MICRO_DECALAGE_BITS;
            if (s >  32767) s =  32767;
            if (s < -32768) s = -32768;
            bufferPCM[i] = (int16_t)s;
            sommeCarres += (int64_t)s * s;
        }
        float rms = sqrtf((float)sommeCarres / (float)nombreEchantillons);

        // Niveau normalisé : -60 dB ➡️ 0.0 et -10 dB ➡️ 1.0, lissé
        float db = 20.0f * log10f((rms < 1.0f ? 1.0f : rms) / 32768.0f);
        float cible = (db + 60.0f) / 50.0f;
        if (cible < 0.0f) cible = 0.0f;
        if (cible > 1.0f) cible = 1.0f;
        niveauLisse += 0.4f * (cible - niveauLisse);
        niveauNormalise = niveauLisse;

        // Détection de voix : le volume doit dépasser durablement le bruit de fond
        float seuil = bruitDeFond * MICRO_FACTEUR_VOIX;
        if (seuil < MICRO_SEUIL_MIN_RMS) seuil = MICRO_SEUIL_MIN_RMS;
        if (rms > seuil) {
            if (++trameVoix >= 3) { voixDetectee = true; trameSilence = 0; }
        } else {
            trameVoix = 0;
            if (voixDetectee && ++trameSilence >= 15) voixDetectee = false;   // maintien d'environ 0,5 s
            if (!voixDetectee) bruitDeFond += 0.02f * (rms - bruitDeFond);    // le bruit de fond suit l'ambiance
        }

        // Copie de l'audio pour un moteur de reconnaissance vocal éventuel (sans jamais bloquer)
        if (fluxActif && fluxMicro) xStreamBufferSend(fluxMicro, bufferPCM, nombreEchantillons * sizeof(int16_t), 0);

#if MICRO_VUMETRE_SERIE
        if (++compteurAffichage >= 5 && rms > MICRO_SEUIL_MIN_RMS) {
            compteurAffichage = 0;
            int barres = (int)(niveauLisse * 30.0f);
            String ligne = "🎙️ [VU] ";
            for (int b = 0; b < barres; b++) ligne += "■";
            my_log_printf("%s (%0.0f)", ligne.c_str(), rms);
        }
#else
        (void)compteurAffichage;
#endif
    }
}
