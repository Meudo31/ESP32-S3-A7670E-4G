#include <stdarg.h>

#include "configuration.h"
#include "module_journal.h"
#include "module_carte_sd.h"
#include "module_horloge.h"
#include "module_appli.h"
#include "module_systeme.h"

namespace {

struct LigneDuJournal {
    char texte[LONGUEUR_MAXIMUM_D_UNE_LIGNE_DU_JOURNAL];
};

const int      CAPACITE_DE_LA_FILE           = 40;
const size_t   TAILLE_DU_TAMPON_AVANT_ECRITURE = 2048;
const uint32_t DELAI_MAXIMUM_AVANT_ECRITURE_MS = 5 * UNE_SECONDE_MS;

QueueHandle_t fileDesLignes = nullptr;

// Les dernières lignes restent en mémoire (pour la commande SMS « Etat »).
const int         NOMBRE_DE_LIGNES_GARDEES = 10;
String            dernieresLignes[NOMBRE_DE_LIGNES_GARDEES];
int               prochaineLigneGardee = 0;
SemaphoreHandle_t verrouDesDernieresLignes = nullptr;

void garderLaLigne(const String& ligne) {
    if (verrouDesDernieresLignes == nullptr) return;
    xSemaphoreTake(verrouDesDernieresLignes, portMAX_DELAY);
    dernieresLignes[prochaineLigneGardee] = ligne;
    prochaineLigneGardee = (prochaineLigneGardee + 1) % NOMBRE_DE_LIGNES_GARDEES;
    xSemaphoreGive(verrouDesDernieresLignes);
}
String        tamponPourLaCarteSD;
uint32_t      derniereEcritureSurLaCarte = 0;

String cheminDuFichierDuJour() {
    if (!lHeureEstConnue()) return String(DOSSIER_DES_JOURNAUX) + "/sans-date.log";
    return String(DOSSIER_DES_JOURNAUX) + "/" + dateDuJourPourNomDeFichier() + ".log";
}

void viderLeTamponSurLaCarteSD() {
    if (tamponPourLaCarteSD.isEmpty()) return;
    if (ajouterALaFinDUnFichier(cheminDuFichierDuJour().c_str(), tamponPourLaCarteSD)) {
        tamponPourLaCarteSD = "";
    } else if (tamponPourLaCarteSD.length() > 4 * TAILLE_DU_TAMPON_AVANT_ECRITURE) {
        tamponPourLaCarteSD = "";   // pas de carte SD : on n'accumule pas indéfiniment
    }
    derniereEcritureSurLaCarte = millis();
}

void tacheJournal(void*) {
    LigneDuJournal ligne;
    for (;;) {
        if (xQueueReceive(fileDesLignes, &ligne, pdMS_TO_TICKS(1000)) == pdPASS) {
            String ligneHorodatee = heurePourLeJournal() + "  " + ligne.texte;
            Serial.println(ligneHorodatee);
            transmettreUneLigneDuJournalALAppli(ligneHorodatee.c_str());
            garderLaLigne(ligneHorodatee);
            tamponPourLaCarteSD += ligneHorodatee;
            tamponPourLaCarteSD += '\n';
        }
        const bool tamponPlein  = tamponPourLaCarteSD.length() >= TAILLE_DU_TAMPON_AVANT_ECRITURE;
        const bool tamponAncien = millis() - derniereEcritureSurLaCarte >= DELAI_MAXIMUM_AVANT_ECRITURE_MS;
        if (tamponPlein || tamponAncien) viderLeTamponSurLaCarteSD();
    }
}

} // namespace

void preparerLeJournal() {
    if (fileDesLignes == nullptr) fileDesLignes = xQueueCreate(CAPACITE_DE_LA_FILE, sizeof(LigneDuJournal));
    if (verrouDesDernieresLignes == nullptr) verrouDesDernieresLignes = xSemaphoreCreateMutex();
}

String dernieresLignesDuJournal(int nombre) {
    String texte;
    if (verrouDesDernieresLignes == nullptr) return texte;
    if (nombre > NOMBRE_DE_LIGNES_GARDEES) nombre = NOMBRE_DE_LIGNES_GARDEES;
    xSemaphoreTake(verrouDesDernieresLignes, portMAX_DELAY);
    for (int age = nombre; age >= 1; age--) {   // de la plus ancienne à la plus récente
        const String& ligne = dernieresLignes[(prochaineLigneGardee - age + NOMBRE_DE_LIGNES_GARDEES) % NOMBRE_DE_LIGNES_GARDEES];
        if (ligne.isEmpty()) continue;
        texte += ligne;
        texte += '\n';
    }
    xSemaphoreGive(verrouDesDernieresLignes);
    return texte;
}

void ecrireDansLeJournal(const char* format, ...) {
    LigneDuJournal ligne;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(ligne.texte, sizeof(ligne.texte), format, arguments);
    va_end(arguments);

    if (fileDesLignes == nullptr || xQueueSend(fileDesLignes, &ligne, 0) != pdPASS) {
        // Pas encore de tâche, ou file pleine : on affiche au moins sur le port série.
        Serial.println(ligne.texte);
    }
}

bool demarrerModuleJournal() {
    preparerLeJournal();
    return creerTacheSurveillee(tacheJournal, "Journal", 4096, PRIORITE_BASSE, COEUR_TRAITEMENTS);
}
