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

bool demarrerModuleExplorateurFichiers();

#endif // MODULE_EXPLORATEUR_FICHIERS_H
