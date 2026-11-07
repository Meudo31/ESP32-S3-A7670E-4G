#ifndef MODULE_COMMANDES_SMS_H
#define MODULE_COMMANDES_SMS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE COMMANDES PAR SMS
   Quand un SMS arrive, son texte est comparé aux commandes connues (majuscules,
   minuscules et accents indifférents) :
     « Photo » → prend une photo, la dépose sur Internet et répond avec le lien ;
     « Gps »   → répond avec la position (lien Google Maps), la vitesse, les satellites ;
     « Etat »  → répond avec la batterie, le modem, le GPS et les dernières lignes du journal ;
     « Aide »  → répond avec la liste des commandes.
   Sécurité : seul le numéro des alertes SMS (réglé dans l'application, écran
   Téléphone) peut commander le module. Les SMS des autres numéros sont ignorés.
   Les réponses sont écrites sans accent : 160 caractères par SMS au lieu de 70.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

// Appelée par le module SMS pour chaque SMS reçu. Renvoie true si le texte est une commande
// (elle est alors exécutée par la tâche des commandes).
bool transmettreUnSMSAuxCommandes(const String& expediteur, const String& texte);

bool demarrerModuleCommandesSMS();

#endif // MODULE_COMMANDES_SMS_H
