#include <SD_MMC.h>
#include <vector>

#include "configuration.h"
#include "module_carte_sd.h"
#include "module_journal.h"
#include "module_systeme.h"

namespace {

SemaphoreHandle_t    verrouDeLaCarte = nullptr;
volatile bool        carteMontee     = false;
InformationsCarteSD  dernieresInformations = { false, "aucune", 0, 0 };

const char* nomDuTypeDeCarte(uint8_t type) {
    switch (type) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SD";
        case CARD_SDHC: return "SDHC/SDXC";
        default:        return "aucune";
    }
}

bool monterLaCarte() {
    SD_MMC.setPins(BROCHE_CARTE_SD_HORLOGE, BROCHE_CARTE_SD_COMMANDE, BROCHE_CARTE_SD_DONNEES);
    const bool modeUnBit = true;
    if (!SD_MMC.begin("/sdcard", modeUnBit) || SD_MMC.cardType() == CARD_NONE) {
        SD_MMC.end();
        return false;
    }
    return true;
}

void mettreAJourLesInformations() {
    InformationsCarteSD informations = { carteMontee, "aucune", 0, 0 };
    if (carteMontee) {
        informations.type                = nomDuTypeDeCarte(SD_MMC.cardType());
        informations.tailleTotaleOctets  = SD_MMC.totalBytes();
        informations.espaceUtiliseOctets = SD_MMC.usedBytes();
    }
    dernieresInformations = informations;
}

// Crée chaque dossier parent du chemin (« /photos/03-11-2026/photo.jpg » → /photos puis /photos/03-11-2026).
void creerLesDossiersParents(const char* chemin) {
    String dossier;
    const String cheminComplet(chemin);
    for (int position = 1; position < (int)cheminComplet.length(); position++) {
        if (cheminComplet[position] == '/') {
            dossier = cheminComplet.substring(0, position);
            if (!SD_MMC.exists(dossier)) SD_MMC.mkdir(dossier);
        }
    }
}

bool supprimerRecursivement(const String& chemin) {
    File element = SD_MMC.open(chemin);
    if (!element) return false;
    if (!element.isDirectory()) {
        element.close();
        return SD_MMC.remove(chemin);
    }
    // On note d'abord les chemins du contenu, puis on les supprime (on ne modifie pas un dossier pendant qu'on le parcourt).
    std::vector<String> contenu;
    for (File enfant = element.openNextFile(); enfant; enfant = element.openNextFile()) {
        contenu.push_back(String(enfant.path()));
        enfant.close();
    }
    element.close();
    for (const String& cheminEnfant : contenu) {
        if (!supprimerRecursivement(cheminEnfant)) return false;
    }
    return SD_MMC.rmdir(chemin);
}

void tacheSurveillanceCarteSD(void*) {
    uint32_t compteur = 0;
    for (;;) {
        if (prendreLaCarteSD()) {
            if (!carteMontee) {
                carteMontee = monterLaCarte();
                if (carteMontee) {
                    ecrireDansLeJournal("[CARTE SD] Carte montée (%s, %llu Mo).",
                                        nomDuTypeDeCarte(SD_MMC.cardType()), SD_MMC.cardSize() / (1024 * 1024));
                    mettreAJourLesInformations();
                }
            } else if (SD_MMC.cardType() == CARD_NONE) {
                carteMontee = false;
                SD_MMC.end();
                ecrireDansLeJournal("[CARTE SD] Carte retirée.");
                mettreAJourLesInformations();
            } else if (compteur % 12 == 0) {   // l'espace libre est long à calculer : une fois par minute
                mettreAJourLesInformations();
            }
            rendreLaCarteSD();
        }
        compteur++;
        vTaskDelay(pdMS_TO_TICKS(5 * UNE_SECONDE_MS));
    }
}

} // namespace

// ─────────────────────────────── Verrou ───────────────────────────────

bool prendreLaCarteSD(uint32_t attenteMaximumMs) {
    return verrouDeLaCarte != nullptr && xSemaphoreTakeRecursive(verrouDeLaCarte, pdMS_TO_TICKS(attenteMaximumMs)) == pdTRUE;
}

void rendreLaCarteSD() { xSemaphoreGiveRecursive(verrouDeLaCarte); }

fs::FS& systemeDeFichiersDeLaCarteSD() { return SD_MMC; }

bool carteSDMontee() { return carteMontee; }

InformationsCarteSD informationsDeLaCarteSD() { return dernieresInformations; }

// ─────────────────────────────── Opérations ───────────────────────────────

bool ajouterALaFinDUnFichier(const char* chemin, const String& texte) {
    if (!carteMontee || !prendreLaCarteSD(2000)) return false;
    creerLesDossiersParents(chemin);
    File fichier = SD_MMC.open(chemin, FILE_APPEND);
    const bool reussi = fichier && fichier.print(texte) == texte.length();
    if (fichier) fichier.close();
    rendreLaCarteSD();
    return reussi;
}

bool ecrireUnFichier(const char* chemin, const uint8_t* octets, size_t taille) {
    if (!carteMontee || !prendreLaCarteSD()) return false;
    creerLesDossiersParents(chemin);
    File fichier = SD_MMC.open(chemin, FILE_WRITE);
    const bool reussi = fichier && fichier.write(octets, taille) == taille;
    if (fichier) fichier.close();
    rendreLaCarteSD();
    return reussi;
}

bool creerUnDossier(const char* chemin) {
    if (!carteMontee || !prendreLaCarteSD()) return false;
    creerLesDossiersParents(chemin);
    const bool reussi = SD_MMC.exists(chemin) || SD_MMC.mkdir(chemin);
    rendreLaCarteSD();
    return reussi;
}

bool renommerOuDeplacer(const char* ancienChemin, const char* nouveauChemin) {
    if (!carteMontee || !prendreLaCarteSD()) return false;
    bool reussi = false;
    if (SD_MMC.exists(ancienChemin) && !SD_MMC.exists(nouveauChemin)) {
        creerLesDossiersParents(nouveauChemin);
        reussi = SD_MMC.rename(ancienChemin, nouveauChemin);
    }
    rendreLaCarteSD();
    return reussi;
}

bool supprimerFichierOuDossier(const char* chemin) {
    if (!carteMontee || strcmp(chemin, "/") == 0 || !prendreLaCarteSD()) return false;
    const bool reussi = supprimerRecursivement(String(chemin));
    rendreLaCarteSD();
    return reussi;
}

bool lireLesInformationsDUnElement(const char* chemin, ElementDUnDossier& element) {
    if (!carteMontee || !prendreLaCarteSD()) return false;
    File fichier = SD_MMC.open(chemin);
    const bool trouve = (bool)fichier;
    if (trouve) {
        element.nom                = fichier.name();
        element.estUnDossier       = fichier.isDirectory();
        element.tailleOctets       = fichier.isDirectory() ? 0 : fichier.size();
        element.dateDeModification = fichier.getLastWrite();
        fichier.close();
    }
    rendreLaCarteSD();
    return trouve;
}

bool listerUnDossier(const char* chemin, const std::function<void(const ElementDUnDossier&)>& pourChaqueElement) {
    if (!carteMontee || !prendreLaCarteSD()) return false;
    File dossier = SD_MMC.open(chemin);
    const bool estUnDossier = dossier && dossier.isDirectory();
    if (estUnDossier) {
        for (File enfant = dossier.openNextFile(); enfant; enfant = dossier.openNextFile()) {
            ElementDUnDossier element;
            element.nom                = enfant.name();
            element.estUnDossier       = enfant.isDirectory();
            element.tailleOctets       = enfant.isDirectory() ? 0 : enfant.size();
            element.dateDeModification = enfant.getLastWrite();
            enfant.close();
            pourChaqueElement(element);
        }
    }
    if (dossier) dossier.close();
    rendreLaCarteSD();
    return estUnDossier;
}

bool demarrerModuleCarteSD() {
    verrouDeLaCarte = xSemaphoreCreateRecursiveMutex();
    if (verrouDeLaCarte == nullptr) return false;
    // Premier montage tout de suite, pour que le journal du démarrage soit enregistré.
    if (prendreLaCarteSD()) {
        carteMontee = monterLaCarte();
        if (carteMontee) mettreAJourLesInformations();
        rendreLaCarteSD();
    }
    if (!carteMontee) ecrireDansLeJournal("[CARTE SD] Pas de carte SD (nouvel essai toutes les 5 s).");
    return creerTacheSurveillee(tacheSurveillanceCarteSD, "Carte SD", 4096, PRIORITE_BASSE, COEUR_TRAITEMENTS);
}
