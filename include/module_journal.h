#ifndef MODULE_JOURNAL_H
#define MODULE_JOURNAL_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE JOURNAL : toutes les traces (logs) du programme.
   Chaque ligne est :
     - affichée sur le moniteur série ;
     - envoyée à l'application si elle est connectée ;
     - enregistrée sur la carte SD dans un fichier par jour :
           /journaux/03-11-2026.log
       (tant que l'heure est inconnue : /journaux/sans-date.log)
   Les écritures sur la carte SD sont regroupées par la tâche du journal
   pour ne pas user la carte ni ralentir les autres tâches.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

#define LONGUEUR_MAXIMUM_D_UNE_LIGNE_DU_JOURNAL 192
#define DOSSIER_DES_JOURNAUX "/journaux"

// Prépare la file d'attente. À appeler en tout premier dans le setup().
void preparerLeJournal();

// Écrit une ligne dans le journal, comme printf. Utilisable depuis n'importe quelle tâche.
void ecrireDansLeJournal(const char* format, ...) __attribute__((format(printf, 1, 2)));

// Lance la tâche qui écrit les lignes sur le port série, la carte SD et l'application.
bool demarrerModuleJournal();

#endif // MODULE_JOURNAL_H
