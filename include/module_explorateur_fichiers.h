#ifndef MODULE_EXPLORATEUR_FICHIERS_H
#define MODULE_EXPLORATEUR_FICHIERS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE EXPLORATEUR DE FICHIERS : la carte SD vue depuis l'application.
   Lister un dossier, renommer, déplacer, supprimer, créer un dossier, et
   télécharger un fichier vers le téléphone.

   Téléchargement : le fichier part par morceaux sur la caractéristique « Fichier ».
   Tous les 8 morceaux, le module attend que le téléphone confirme le nombre
   d'octets reçus (commande DL_ACK) : rien ne peut se perdre en route, et une
   empreinte CRC32 finale permet au téléphone de vérifier le fichier.

   Envoi depuis le téléphone (dans l'autre sens) :
     1. FICHIER_ENVOYER /dossier/nom.ext|taille  → le module prépare « nom.ext.part »
     2. le téléphone écrit le contenu, morceau par morceau, sur la caractéristique
        « Fichier entrant » (écriture avec accusé de réception : rien ne se perd) ;
     3. FICHIER_ENVOYER_FIN crc32 → le module vérifie la taille et le CRC32, puis
        renomme « .part » en nom définitif (un fichier du même nom est remplacé).
   FICHIER_ENVOYER_ANNULER ou une déconnexion efface le fichier « .part ».
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

enum class OperationSurLesFichiers : uint8_t {
    LISTER, RENOMMER_OU_DEPLACER, SUPPRIMER, CREER_UN_DOSSIER, TELECHARGER,
};

// Met une opération dans la file de la tâche. « destination » ne sert qu'à RENOMMER_OU_DEPLACER.
bool demanderUneOperationSurLesFichiers(OperationSurLesFichiers operation, const String& chemin,
                                        const String& destination = "");

// Le téléphone confirme avoir reçu ce nombre d'octets du fichier en cours de téléchargement.
void confirmerLaReceptionDuTelephone(uint32_t octetsRecus);
void annulerLeTelechargement();

// Envoi d'un fichier du téléphone vers la carte SD (voir plus haut).
bool commencerLaReceptionDUnFichier(const String& chemin, uint32_t taille, String& compteRendu);
void recevoirUnMorceauDuTelephone(const uint8_t* octets, size_t taille);   // appelé par le Bluetooth
bool terminerLaReceptionDUnFichier(uint32_t crcAttendu, String& compteRendu);
void annulerLaReceptionDUnFichier();
bool receptionDUnFichierEnCours();   // pendant un envoi, les données en direct sont suspendues

bool demarrerModuleExplorateurFichiers();

#endif // MODULE_EXPLORATEUR_FICHIERS_H
