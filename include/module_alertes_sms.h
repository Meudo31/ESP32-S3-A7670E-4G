#ifndef MODULE_ALERTES_SMS_H
#define MODULE_ALERTES_SMS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE ALERTES PAR SMS
   Envoie automatiquement un SMS au numéro choisi :
     - au démarrage : batterie, alimentation, réseau, raison du redémarrage ;
     - quand le GPS trouve (ou retrouve, après 5 min sans position) une position,
       avec un lien Google Maps ;
     - à chaque déplacement de plus de 5 km depuis la dernière position envoyée.
   Le numéro n'est jamais écrit dans le code (dépôt public) : il se règle depuis
   l'application (écran Téléphone), ou dans include/secrets.h (SECRET_NUMERO_ALERTES).
   Il est gardé dans la mémoire permanente du module.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

void   choisirLeNumeroDesAlertes(const String& numero);   // "" = alertes coupées
String numeroDesAlertes();
void   activerLesAlertes(bool actives);
bool   alertesActives();
bool   envoyerUneAlerteDeTest();

bool demarrerModuleAlertesSMS();

#endif // MODULE_ALERTES_SMS_H
