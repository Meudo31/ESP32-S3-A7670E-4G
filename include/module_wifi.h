#ifndef MODULE_WIFI_H
#define MODULE_WIFI_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE WI-FI (à la demande)
   Le Bluetooth reste la liaison de tous les jours. Pour les gros transferts
   (mise à jour du firmware, fichiers de la carte SD), l'application demande au
   module d'ouvrir son propre Wi-Fi (« Moto_ESP32 », WPA2) : elle s'y connecte,
   fait le transfert environ 50 fois plus vite, puis le module l'éteint.
   Le Wi-Fi partage la radio du Bluetooth : il ne reste donc jamais allumé
   pour rien (arrêt automatique après 2 minutes sans requête).

   Petit serveur web sur http://192.168.4.1 :
     GET  /ping                        → "ok"
     GET  /fichier?chemin=/dossier/x   → contenu du fichier
     POST /fichier?chemin=/dossier/x   → envoi d'un fichier (multipart, champ « fichier »)
     POST /firmware                    → mise à jour du firmware (multipart), puis redémarrage
   Le mot de passe est tiré au hasard au premier démarrage et transmis à
   l'application par le Bluetooth (liaison chiffrée).
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

// Ouvre le point d'accès Wi-Fi ; renvoie son nom, son mot de passe et son adresse.
bool ouvrirLeWifi(String& nom, String& motDePasse, String& adresse);
void fermerLeWifi();
bool wifiOuvert();

bool demarrerModuleWifi();

#endif // MODULE_WIFI_H
