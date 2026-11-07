#ifndef MODULE_ENVOI_INTERNET_H
#define MODULE_ENVOI_INTERNET_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE ENVOI PAR INTERNET : dépose un fichier (une photo) sur un hébergement
   temporaire par la 4G et renvoie le lien de téléchargement.
   Le modem A7670E ne sait pas envoyer de MMS : la photo part donc par Internet
   (client HTTP du modem : AT+HTTPINIT, AT+HTTPDATA, AT+HTTPACTION) et c'est le
   lien qui est envoyé par SMS.
   L'adresse du dépôt et la durée de vie du lien sont dans configuration.h.
   Pas de tâche : la fonction est appelée par la tâche des commandes SMS.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

// Envoie le fichier (bloquant, jusqu'à 2 minutes). Renvoie true et le lien, ou false et l'erreur.
bool deposerUnFichierSurInternet(const uint8_t* donnees, size_t taille, const char* nomDuFichier,
                                 const char* typeDeContenu, String& lien, String& erreur);

bool demarrerModuleEnvoiInternet();

#endif // MODULE_ENVOI_INTERNET_H
