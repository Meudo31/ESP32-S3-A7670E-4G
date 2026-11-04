#ifndef MODULE_HAUT_PARLEUR_H
#define MODULE_HAUT_PARLEUR_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE HAUT-PARLEUR
   Le haut-parleur est branché sur la sortie « SPK » de la carte, c'est-à-dire
   sur le modem A7670E (l'ESP32 n'a pas de sortie audio sur cette carte).
   On le fait donc parler avec des commandes AT du modem :
     - AT+CTTS     : synthèse vocale (lecture d'un texte) ;
     - AT+CCMXPLAY : lecture d'un fichier .wav enregistré dans la mémoire du modem ;
     - AT+CLVL     : volume ;
     - AT+CSDVC    : sortie audio (3 = haut-parleur, 1 = combiné) ;
     - AT+SIMTONE  : simple bip, quand le modem n'a pas de voix.
   Pendant un appel, la voix du correspondant sort aussi sur ce haut-parleur.

   Sons personnalisés (facultatif) : déposer sur la carte SD
        /sons/sms.wav        joué à l'arrivée d'un SMS
        /sons/sonnerie.wav   joué en boucle pendant un appel entrant
   puis appuyer sur « Installer les sons » dans l'application : ils sont copiés
   dans la mémoire du modem. Sans ces fichiers, une phrase est prononcée à la place.
   (WAV mono 8 kHz 16 bits conseillé.)
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

// Prononce un texte (synthèse vocale du modem).
void annoncerAuHautParleur(const String& texte);

void jouerLeSonDUnNouveauSMS();
void demarrerLaSonnerie();
void arreterLaSonnerie();

// Volume de 0 (muet) à 5 (maximum). Mémorisé en flash.
void reglerLeVolumeDuHautParleur(int volume);
int  volumeDuHautParleur();

// Diagnostic : bip, voix et son SMS ; le résultat (ce que le modem accepte) est renvoyé à l'application.
void testerLeHautParleurDuModule();
// Sortie audio du modem (AT+CSDVC) : 1 = combiné (écouteur), 3 = haut-parleur. Mémorisée.
bool choisirLaSortieDuHautParleur(int sortie);
int  sortieDuHautParleur();

// Copie /sons/*.wav de la carte SD vers la mémoire du modem. Renvoie un compte rendu lisible.
String installerLesSonsDepuisLaCarteSD();

bool demarrerModuleHautParleur();

#endif // MODULE_HAUT_PARLEUR_H
