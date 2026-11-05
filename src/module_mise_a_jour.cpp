#include <Update.h>

#include "configuration.h"
#include "module_mise_a_jour.h"
#include "module_appli.h"
#include "module_journal.h"
#include "module_systeme.h"

namespace {

enum class TypeDeMessage : uint8_t { ORDRE, MORCEAU, ANNULATION };

struct MessageDeMiseAJour {
    TypeDeMessage type;
    uint16_t      taille;
    uint8_t       octets[TAILLE_MAXIMUM_D_UN_MORCEAU_DE_FIRMWARE];
};

QueueHandle_t     fileDeLaMiseAJour  = nullptr;
volatile bool     enCours            = false;
size_t            tailleAttendue     = 0;
size_t            tailleRecue        = 0;
volatile uint32_t momentDuRedemarrage = 0;   // 0 = aucun redémarrage prévu

void repondre(const char* texte) { envoyerUneReponseDeMiseAJourALAppli(texte); }

void echouer(const char* etape) {
    char texte[120];
    snprintf(texte, sizeof(texte), "ERR %s : %s", etape, Update.errorString());
    ecrireDansLeJournal("[MISE A JOUR] %s", texte);
    if (enCours) Update.abort();
    enCours = false;
    repondre(texte);
}

void commencer(const char* arguments) {
    unsigned long taille = 0;
    char empreinteMD5[33] = {0};
    const int nombreDeValeurs = sscanf(arguments, "%lu %32s", &taille, empreinteMD5);
    if (nombreDeValeurs < 1 || taille == 0) {
        repondre("ERR format : START <taille> <md5>");
        return;
    }
    if (taille > ESP.getFreeSketchSpace()) {
        repondre("ERR fichier trop gros pour la partition de mise à jour");
        return;
    }
    if (enCours) Update.abort();
    if (!Update.begin(taille, U_FLASH)) { echouer("début"); return; }
    if (nombreDeValeurs == 2 && strlen(empreinteMD5) == 32 && !Update.setMD5(empreinteMD5)) { echouer("md5"); return; }
    enCours        = true;
    tailleAttendue = taille;
    tailleRecue    = 0;
    ecrireDansLeJournal("[MISE A JOUR] Début : %lu octets.", taille);
    repondre("OK START");
}

void terminer() {
    if (!enCours) { repondre("ERR aucune mise à jour en cours"); return; }
    if (tailleRecue != tailleAttendue) {
        char texte[80];
        snprintf(texte, sizeof(texte), "ERR taille : %u reçus sur %u", (unsigned)tailleRecue, (unsigned)tailleAttendue);
        Update.abort();
        enCours = false;
        repondre(texte);
        return;
    }
    if (!Update.end()) { echouer("vérification"); return; }   // vérifie aussi l'empreinte MD5
    enCours = false;
    ecrireDansLeJournal("[MISE A JOUR] Réussie. Redémarrage sur le nouveau firmware.");
    repondre("OK END");
    redemarrerDansQuelquesInstants(1500);
}

void traiterUnOrdre(const MessageDeMiseAJour& message) {
    char texte[80];
    const size_t longueur = min((size_t)message.taille, sizeof(texte) - 1);
    memcpy(texte, message.octets, longueur);
    texte[longueur] = '\0';

    if (strncmp(texte, "START", 5) == 0) commencer(texte + 5);
    else if (strcmp(texte, "END") == 0) terminer();
    else if (strcmp(texte, "ABORT") == 0) {
        if (enCours) Update.abort();
        enCours = false;
        repondre("OK ABORT");
    }
    else repondre("ERR ordre inconnu");
}

void ecrireUnMorceau(const MessageDeMiseAJour& message) {
    if (!enCours) return;
    // Une image d'application ESP32 commence toujours par l'octet 0xE9.
    if (tailleRecue == 0 && message.taille > 0 && message.octets[0] != 0xE9) {
        Update.abort();
        enCours = false;
        repondre("ERR ce n'est pas un firmware ESP32");
        return;
    }
    if (tailleRecue + message.taille > tailleAttendue) {
        Update.abort();
        enCours = false;
        repondre("ERR trop de données");
        return;
    }
    if (Update.write((uint8_t*)message.octets, message.taille) != message.taille) { echouer("écriture"); return; }
    tailleRecue += message.taille;
}

void tacheMiseAJour(void*) {
    static MessageDeMiseAJour message;   // 520 octets : hors de la pile
    for (;;) {
        if (xQueueReceive(fileDeLaMiseAJour, &message, pdMS_TO_TICKS(200)) == pdPASS) {
            switch (message.type) {
                case TypeDeMessage::ORDRE:   traiterUnOrdre(message); break;
                case TypeDeMessage::MORCEAU: ecrireUnMorceau(message); break;
                case TypeDeMessage::ANNULATION:
                    if (enCours) {
                        Update.abort();
                        enCours = false;
                        ecrireDansLeJournal("[MISE A JOUR] Annulée (téléphone déconnecté).");
                    }
                    break;
            }
        }
        if (momentDuRedemarrage != 0 && (int32_t)(millis() - momentDuRedemarrage) >= 0) {
            ecrireDansLeJournal("[SYSTEME] Redémarrage.");
            vTaskDelay(pdMS_TO_TICKS(300));   // laisse le journal s'écrire
            ESP.restart();
        }
    }
}

void mettreDansLaFile(TypeDeMessage type, const uint8_t* octets, size_t taille, TickType_t attente) {
    if (fileDeLaMiseAJour == nullptr) return;
    MessageDeMiseAJour* message = (MessageDeMiseAJour*)malloc(sizeof(MessageDeMiseAJour));
    if (message == nullptr) return;
    message->type   = type;
    message->taille = (uint16_t)min(taille, (size_t)TAILLE_MAXIMUM_D_UN_MORCEAU_DE_FIRMWARE);
    if (octets != nullptr) memcpy(message->octets, octets, message->taille);
    // Si la flash est plus lente que le Bluetooth, on attend ici : le téléphone attend
    // la confirmation de chaque écriture, il ralentit donc de lui-même.
    if (xQueueSend(fileDeLaMiseAJour, message, attente) != pdPASS && type == TypeDeMessage::MORCEAU) {
        repondre("ERR file pleine");
    }
    free(message);
}

} // namespace

void recevoirUnOrdreDeMiseAJour(const uint8_t* texte, size_t taille) {
    mettreDansLaFile(TypeDeMessage::ORDRE, texte, taille, pdMS_TO_TICKS(1000));
}

void recevoirUnMorceauDeFirmware(const uint8_t* octets, size_t taille) {
    mettreDansLaFile(TypeDeMessage::MORCEAU, octets, taille, pdMS_TO_TICKS(3000));
}

void annulerLaMiseAJourEnCours() {
    mettreDansLaFile(TypeDeMessage::ANNULATION, nullptr, 0, 0);
}

bool miseAJourEnCours() { return enCours; }

void redemarrerDansQuelquesInstants(uint32_t delaiMs) {
    momentDuRedemarrage = millis() + delaiMs;
    if (momentDuRedemarrage == 0) momentDuRedemarrage = 1;
}

bool demarrerModuleMiseAJour() {
    fileDeLaMiseAJour = xQueueCreate(8, sizeof(MessageDeMiseAJour));
    if (fileDeLaMiseAJour == nullptr) return false;
    return creerTacheSurveillee(tacheMiseAJour, "Mise a jour", 4096, PRIORITE_HAUTE, COEUR_COMMUNICATIONS);
}
