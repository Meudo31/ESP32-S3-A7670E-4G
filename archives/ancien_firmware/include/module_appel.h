#ifndef CONFIG_APPEL_H
#define CONFIG_APPEL_H
#include "main.h"

/* ───────────── APPELS TÉLÉPHONIQUES VIA LE MODEM A7670E ─────────────
   Le micro ANALOGIQUE et le haut-parleur ANALOGIQUE sont branchés sur les bornes MIC et SPK de la carte,
   c'est-à-dire sur le circuit audio du modem. L'ESP32 ne manipule aucun son pendant un appel : il envoie
   seulement des commandes AT (décrocher, raccrocher, composer, régler le volume).

   À CONFIRMER SUR TON MATÉRIEL : que les bornes MIC/SPK de la carte Waveshare sont bien reliées à l'audio
   du modem (schéma de la carte), et quelle sortie audio donne du son (APPEL_SORTIE_AUDIO ci-dessous). */

#define APPEL_SORTIE_AUDIO       3      // AT+CSDVC : 3 = haut-parleur, 1 = écouteur/casque. À essayer si aucun son n'est audible.
#define APPEL_VOLUME             3      // AT+CLVL : volume de sortie (la plage exacte dépend du firmware du modem)
#define APPEL_GAIN_MICRO         -1     // AT+CMICGAIN : -1 = ne pas modifier le gain du micro analogique
#define APPEL_REPONSE_AUTO       true   // décroche automatiquement pour les numéros autorisés
#define APPEL_DELAI_REPONSE_MS   2500   // délai avant de décrocher, pour laisser le panneau NeoPixel signaler l'appel
#define APPEL_REJETER_INCONNUS   false  // true : raccroche au nez des numéros inconnus ; false : on les laisse sonner

bool initialiserAppelsVocaux();                       // configure le modem (identification de l'appelant, volume, sortie audio)
void taskAppels(void * pvParameters);                 // suit l'état de l'appel et pilote le panneau NeoPixel

bool appelerNumero(const char* numero);               // compose un numéro (chiffres et +), retourne false si le numéro est invalide
void raccrocherAppel();
bool appelEnCours();

// Appelée par la tâche de réception du modem pour chaque ligne reçue : retire du tampon les lignes d'appel
// (+CLIP, NO CARRIER, VOICE CALL...) et retourne true si elle en a trouvé.
bool traiterLignesAppelModem(String &tampon);

#endif // CONFIG_APPEL_H
