#ifndef MODULE_HORLOGE_H
#define MODULE_HORLOGE_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE HORLOGE : date et heure du module.
   L'ESP32 n'a pas de pile de sauvegarde : au démarrage il ne connaît pas l'heure.
   Elle est remise à l'heure, par ordre de préférence, par :
     - le GPS (heure atomique des satellites) ;
     - le réseau 4G (commande AT+CCLK?) ;
     - le téléphone, à chaque connexion de l'application.
   Sa tâche redemande l'heure au réseau tant qu'aucune source ne l'a donnée,
   puis toutes les heures.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <time.h>

enum class SourceDeLHeure { AUCUNE, RESEAU_4G, GPS, TELEPHONE };

// Règle le fuseau horaire. À appeler au début du setup().
void preparerLHorloge();

// true si l'heure a été donnée par une source fiable depuis le démarrage.
bool lHeureEstConnue();
SourceDeLHeure sourceDeLHeure();
const char* nomDeLaSourceDeLHeure();

// Remet l'horloge à l'heure à partir d'un horodatage UTC (secondes depuis 1970).
void mettreLHorlogeALHeure(time_t secondesUTC, SourceDeLHeure source);

// Convertit une date du calendrier UTC (année complète, mois 1..12) en secondes depuis 1970.
time_t convertirUneDateUTCEnSecondes(int annee, int mois, int jour, int heure, int minute, int seconde);

// Textes prêts à l'emploi, à l'heure locale :
String dateDuJourPourNomDeFichier();      // "03-11-2026"
String dateEtHeurePourNomDeFichier();     // "03-11-2026_14-05-09"
String heurePourLeJournal();              // "14:05:09"

bool demarrerModuleHorloge();

#endif // MODULE_HORLOGE_H
