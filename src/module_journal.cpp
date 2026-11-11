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
    if (fileDesLignes == nullptr) fileDesLignes = creerUneFileEnPSRAM(CAPACITE_DE_LA_FILE, sizeof(LigneDuJournal));
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
