#ifndef MODULE_MISE_A_JOUR_H
#define MODULE_MISE_A_JOUR_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE MISE À JOUR : nouveau firmware envoyé par l'application en Bluetooth.
   Déroulement (voir docs/protocole_ble.md) :
     1. l'application envoie « START <taille> <md5> » ;
     2. puis le fichier firmware.bin par morceaux ;
     3. puis « END » : le module vérifie la taille et l'empreinte MD5, active le
        nouveau firmware et redémarre.
   Le firmware est écrit dans l'autre partition (app0 / app1) : celui qui tourne
   n'est jamais effacé. Une mise à jour interrompue ou abîmée ne change donc rien.
   L'écriture en flash se fait dans la tâche de ce module, pas dans la pile
   Bluetooth, pour ne pas la bloquer.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

#define TAILLE_MAXIMUM_D_UN_MORCEAU_DE_FIRMWARE 516

// Appelées par le module Bluetooth quand l'application écrit.
void recevoirUnOrdreDeMiseAJour(const uint8_t* texte, size_t taille);
void recevoirUnMorceauDeFirmware(const uint8_t* octets, size_t taille);

// Appelée par le module Bluetooth si le téléphone se déconnecte.
void annulerLaMiseAJourEnCours();

bool miseAJourEnCours();

// Programme un redémarrage dans « delaiMs » millisecondes (laisse le temps d'envoyer la réponse).
void redemarrerDansQuelquesInstants(uint32_t delaiMs);

bool demarrerModuleMiseAJour();

#endif // MODULE_MISE_A_JOUR_H
