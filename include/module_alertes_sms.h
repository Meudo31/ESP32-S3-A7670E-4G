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

   On peut tout couper (interrupteur général) ou seulement un genre d'alerte, depuis
   l'application ou par SMS envoyé depuis le numéro des alertes :
     « Stop » (ou « Arrêt ») coupe les alertes, « Marche » les remet ; le module répond par SMS.
   Ces réglages sont gardés dans la mémoire permanente (ils survivent au redémarrage).
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

void   choisirLeNumeroDesAlertes(const String& numero);   // "" = alertes coupées
String numeroDesAlertes();
void   activerLesAlertes(bool actives);              // interrupteur général
bool   alertesActives();
bool   envoyerUneAlerteDeTest();                    // part même si les alertes sont coupées

enum class TypeDAlerte { DEMARRAGE, POSITION, DEPLACEMENT };
void   activerUneAlerte(TypeDAlerte type, bool active);
bool   uneAlerteEstActive(TypeDAlerte type);

// SMS reçu : si c'est « Stop » ou « Marche » venant du numéro des alertes, l'exécute et renvoie true.
bool   traiterUnSMSDeCommandeDesAlertes(const String& expediteur, const String& texte);

bool demarrerModuleAlertesSMS();

#endif // MODULE_ALERTES_SMS_H
