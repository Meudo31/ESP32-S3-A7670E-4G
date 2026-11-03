#ifndef MODULE_CARTE_SD_H
#define MODULE_CARTE_SD_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE CARTE SD (lecteur micro-SD de la carte, en mode SDMMC 1 bit).
   Toutes les tâches passent par ce module pour lire ou écrire : un verrou
   empêche deux tâches d'écrire en même temps.
   Sa tâche surveille la carte : si elle est retirée puis remise, elle est remontée.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <FS.h>
#include <functional>

struct InformationsCarteSD {
    bool     montee;
    const char* type;           // "SDHC/SDXC", "SD", "MMC" ou "aucune"
    uint64_t tailleTotaleOctets;
    uint64_t espaceUtiliseOctets;
};

struct ElementDUnDossier {
    String   nom;
    bool     estUnDossier;
    uint32_t tailleOctets;
    time_t   dateDeModification;
};

bool carteSDMontee();
InformationsCarteSD informationsDeLaCarteSD();

// Verrou pour une suite d'opérations (ex. lecture d'un gros fichier par morceaux).
bool prendreLaCarteSD(uint32_t attenteMaximumMs = 5000);
void rendreLaCarteSD();

// Opérations simples (elles prennent et rendent le verrou elles-mêmes).
bool ajouterALaFinDUnFichier(const char* chemin, const String& texte);
bool ecrireUnFichier(const char* chemin, const uint8_t* octets, size_t taille);
bool creerUnDossier(const char* chemin);              // crée aussi les dossiers parents
bool renommerOuDeplacer(const char* ancienChemin, const char* nouveauChemin);
bool supprimerFichierOuDossier(const char* chemin);   // un dossier est supprimé avec tout son contenu
bool lireLesInformationsDUnElement(const char* chemin, ElementDUnDossier& element);
bool listerUnDossier(const char* chemin, const std::function<void(const ElementDUnDossier&)>& pourChaqueElement);

// Accès direct au système de fichiers (à utiliser entre prendreLaCarteSD() et rendreLaCarteSD()).
fs::FS& systemeDeFichiersDeLaCarteSD();

bool demarrerModuleCarteSD();

#endif // MODULE_CARTE_SD_H
