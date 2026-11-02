#include "module_appel.h"
#include "module_sms.h"
#include "module_neopixel.h"

enum class EvenementAppel : uint8_t { ENTRANT, EN_COURS, TERMINE };

struct MessageAppel {
    EvenementAppel type;
    char           numero[24];
};

enum class EtatAppel : uint8_t { LIBRE, SONNE, EN_COURS };

static QueueHandle_t     queueEvenementsAppel = NULL;
static volatile EtatAppel etatAppel = EtatAppel::LIBRE;

#define APPEL_DELAI_SECURITE_MS 60000   // si la fin d'appel n'est jamais reçue, on repasse en libre après ce délai

static void envoyerAT(const char* commande) {
    modemSerial.println(commande);
    vTaskDelay(pdMS_TO_TICKS(150));
}

static void posterEvenement(EvenementAppel type, const char* numero) {
    if (queueEvenementsAppel == NULL) return;
    MessageAppel m;
    m.type = type;
    strncpy(m.numero, numero, sizeof(m.numero) - 1);
    m.numero[sizeof(m.numero) - 1] = '\0';
    xQueueSend(queueEvenementsAppel, &m, 0);      // jamais bloquant : appelée depuis la tâche de réception du modem
}

bool initialiserAppelsVocaux() {
    queueEvenementsAppel = xQueueCreate(8, sizeof(MessageAppel));
    if (queueEvenementsAppel == NULL) {
        my_log_printf("[📞 APPEL] Erreur : file d'événements impossible à créer.");
        return false;
    }

    char commande[32];
    envoyerAT("AT+CLIP=1");                                        // le modem indique le numéro de l'appelant (+CLIP)
    snprintf(commande, sizeof(commande), "AT+CSDVC=%d", APPEL_SORTIE_AUDIO); envoyerAT(commande);
    snprintf(commande, sizeof(commande), "AT+CLVL=%d",  APPEL_VOLUME);       envoyerAT(commande);
    if (APPEL_GAIN_MICRO >= 0) { snprintf(commande, sizeof(commande), "AT+CMICGAIN=%d", APPEL_GAIN_MICRO); envoyerAT(commande); }

    my_log_printf("[📞 APPEL] Modem configuré pour les appels vocaux (sortie audio %d, volume %d).", APPEL_SORTIE_AUDIO, APPEL_VOLUME);
    return true;
}

bool appelerNumero(const char* numero) {
    if (numero == NULL || numero[0] == '\0') return false;
    char propre[24];
    size_t n = 0;
    for (const char* p = numero; *p && n < sizeof(propre) - 1; p++) {
        if ((*p >= '0' && *p <= '9') || (*p == '+' && n == 0)) propre[n++] = *p;   // on ne garde que des chiffres, avec + en tête
        else if (*p != ' ' && *p != '.' && *p != '-') return false;                // tout autre caractère : numéro refusé
    }
    propre[n] = '\0';
    if (n < 3) return false;

    my_log_printf("[📞 APPEL] Appel sortant vers %s", propre);
    modemSerial.printf("ATD%s;\r\n", propre);                  // le ; final est obligatoire pour un appel vocal
    return true;
}

void raccrocherAppel() {
    my_log_printf("[📞 APPEL] Raccrochage demandé.");
    modemSerial.println("AT+CHUP");
}

bool appelEnCours() { return etatAppel == EtatAppel::EN_COURS; }

// ─── Analyse des lignes envoyées par le modem ───
// Lignes reconnues : +CLIP: "numéro",..  |  VOICE CALL: BEGIN  |  VOICE CALL: END..  |  NO CARRIER / BUSY / NO ANSWER
bool traiterLignesAppelModem(String &tampon) {
    // Pendant la réception d'un SMS, le corps du message peut contenir n'importe quel texte : on n'y cherche pas d'appel.
    if (tampon.indexOf("+CMT:") != -1) return false;

    bool trouve = false;
    int debut = 0;
    while (true) {
        int fin = tampon.indexOf('\n', debut);
        if (fin == -1) break;                                   // ligne incomplète : on la garde pour plus tard

        String ligne = tampon.substring(debut, fin);
        ligne.trim();
        bool consommee = false;

        if (ligne.startsWith("+CLIP:")) {
            int g1 = ligne.indexOf('"');
            int g2 = (g1 >= 0) ? ligne.indexOf('"', g1 + 1) : -1;
            String numero = (g1 >= 0 && g2 > g1) ? ligne.substring(g1 + 1, g2) : String("");
            posterEvenement(EvenementAppel::ENTRANT, numero.c_str());
            consommee = true;
        } else if (ligne.startsWith("VOICE CALL") && ligne.indexOf("BEGIN") != -1) {
            posterEvenement(EvenementAppel::EN_COURS, "");
            consommee = true;
        } else if ((ligne.startsWith("VOICE CALL") && ligne.indexOf("END") != -1)
                   || ligne == "NO CARRIER" || ligne == "BUSY" || ligne == "NO ANSWER") {
            posterEvenement(EvenementAppel::TERMINE, "");
            consommee = true;
        } else if (ligne == "RING") {
            consommee = true;                                   // le numéro arrive avec +CLIP : RING seul ne sert à rien
        }

        if (consommee) {
            tampon.remove(debut, fin - debut + 1);              // on retire la ligne du tampon sans avancer
            trouve = true;
        } else {
            debut = fin + 1;
        }
    }
    return trouve;
}

// ─── Tâche de gestion ───
static void terminerAppel(AnimationNeoPixel animationAvant) {
    etatAppel = EtatAppel::LIBRE;
    neopixelDefinirAnimation(animationAvant);
}

void taskAppels(void * pvParameters) {
    AnimationNeoPixel animationAvant = AnimationNeoPixel::VEILLE;
    TickType_t debutSonnerie    = 0;
    bool       reponseProgrammee = false;

    for (;;) {
        // Délai d'attente : prochaine échéance entre la réponse automatique et la sécurité de fin d'appel
        TickType_t attente = portMAX_DELAY;
        if (etatAppel != EtatAppel::LIBRE) {
            TickType_t ecoule = xTaskGetTickCount() - debutSonnerie;
            TickType_t limite = pdMS_TO_TICKS(APPEL_DELAI_SECURITE_MS);
            if (reponseProgrammee && etatAppel == EtatAppel::SONNE) {
                TickType_t delaiRep = pdMS_TO_TICKS(APPEL_DELAI_REPONSE_MS);
                if (delaiRep < limite) limite = delaiRep;
            }
            attente = (ecoule >= limite) ? 0 : (limite - ecoule);
        }

        MessageAppel m;
        if (xQueueReceive(queueEvenementsAppel, &m, attente) == pdPASS) {
            switch (m.type) {
                case EvenementAppel::ENTRANT: {
                    if (etatAppel != EtatAppel::LIBRE) break;           // +CLIP se répète à chaque sonnerie
                    int indice = indiceRepertoire(m.numero);
                    my_log_printf("[📞 APPEL] Appel entrant de %s%s", m.numero, indice >= 0 ? " (répertoire)" : " (inconnu)");
                    if (!numeroAutorisePourAppel(m.numero)) {
                        if (APPEL_REJETER_INCONNUS) raccrocherAppel();
                        break;                                          // numéro non autorisé : pas de décrochage automatique
                    }
                    animationAvant = neopixelAnimationCourante();
                    neopixelDefinirAnimation(AnimationNeoPixel::APPEL_ENTRANT);
                    etatAppel = EtatAppel::SONNE;
                    debutSonnerie = xTaskGetTickCount();
                    reponseProgrammee = APPEL_REPONSE_AUTO;
                    break;
                }
                case EvenementAppel::EN_COURS:
                    if (etatAppel == EtatAppel::LIBRE) animationAvant = neopixelAnimationCourante();   // appel sortant
                    my_log_printf("[📞 APPEL] Communication établie.");
                    etatAppel = EtatAppel::EN_COURS;
                    reponseProgrammee = false;
                    debutSonnerie = xTaskGetTickCount();
                    neopixelDefinirAnimation(AnimationNeoPixel::APPEL_EN_COURS);
                    break;
                case EvenementAppel::TERMINE:
                    if (etatAppel != EtatAppel::LIBRE) {
                        my_log_printf("[📞 APPEL] Appel terminé.");
                        terminerAppel(animationAvant);
                    }
                    reponseProgrammee = false;
                    break;
            }
        } else if (etatAppel == EtatAppel::SONNE && reponseProgrammee) {
            // Délai écoulé sans que l'appelant ait raccroché : on décroche
            reponseProgrammee = false;
            my_log_printf("[📞 APPEL] Décrochage automatique.");
            modemSerial.println("ATA");
            debutSonnerie = xTaskGetTickCount();                        // repart pour le délai de sécurité
        } else if (etatAppel != EtatAppel::LIBRE) {
            // Sécurité : la fin d'appel n'est jamais arrivée
            my_log_printf("[📞 APPEL] Aucune nouvelle du modem, retour à l'état libre.");
            terminerAppel(animationAvant);
        }
    }
}
