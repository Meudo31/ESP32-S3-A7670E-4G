#ifndef MODULE_SYNCHRO_H
#define MODULE_SYNCHRO_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE SYNCHRONISATION « RETOUR À LA MAISON »
   Quand la moto est à portée du Wi-Fi de la maison (mode station), le module
   s'y connecte et déverse ce que la carte SD a enregistré depuis la dernière fois :

     carte SD                              destination
     ───────────────────────────────────   ────────────────────────────────────
     /journaux/jj-mm-aaaa.log              table journaux        (base de données)
     /gps/jj-mm-aaaa.csv                   table positions_gps   (base de données)
     /can/jj-mm-aaaa/*.csv                 Google Drive, dossier « Moto ESP32 - trames CAN »
                                           (ou table trames_can pour un serveur maison)
     /photos/jj-mm-aaaa/*.jpg              Google Photos (album « Moto ESP32 »)
                                           + table photos (lien vers la photo)

   Choix de Meudo (10/11/2026) : tout en cloud gratuit. Les tables vont chez
   Supabase (offre gratuite : 500 Mo), les photos chez Google Photos, et les
   trames CAN brutes (plus de 100 Mo par heure de route) dans Google Drive
   (15 Go gratuits), avec le même compte Google que les photos.

   La base de données est un PostgreSQL vu à travers PostgREST : c'est exactement
   ce que proposent Supabase (dans le cloud) et le petit serveur maison fourni dans
   le dossier serveur/ (Docker). Le module envoie des requêtes
       POST <adresse de l'API>/<table>
   avec la clé dans les en-têtes « apikey » et « Authorization: Bearer ».
   Chaque ligne a une clé unique : renvoyer deux fois la même ligne ne crée pas
   de doublon (Prefer: resolution=ignore-duplicates).

   Ce qui a déjà été envoyé est noté sur la carte SD dans /synchro/progression.txt
   (nombre d'octets et de lignes envoyés pour chaque fichier). Un fichier qui
   grossit encore (journal du jour) est repris là où on s'était arrêté.

   Quand chercher le Wi-Fi : toutes les minutes, sauf quand la moto roule
   (vitesse GPS > 15 km/h), et au plus une synchronisation complète toutes les
   15 minutes. L'appli peut demander une synchronisation tout de suite.
   Le point d'accès de l'appli (module_wifi) est prioritaire : son ouverture
   interrompt la synchronisation.

   Réglages (mémoire permanente, réglables depuis l'appli, valeurs par défaut
   dans secrets.h) : Wi-Fi de la maison, adresse et clé de l'API, identifiants
   Google (client OAuth + jeton de rafraîchissement). Les mots de passe et les
   clés ne sont jamais renvoyés à l'appli : seulement « réglé / pas réglé ».
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

#define DOSSIER_DE_LA_SYNCHRO "/synchro"

// Réglages (gardés en mémoire permanente). Un texte vide efface le réglage.
void choisirLeWifiDeLaMaison(const String& nom, const String& motDePasse);
void choisirLeServeurDeDonnees(const String& adresse, const String& cle);
void choisirLeClientGoogle(const String& identifiant, const String& secret);
void choisirLeJetonGoogle(const String& jetonDeRafraichissement);
void activerLaSynchronisation(bool active);
// Trames CAN brutes : 0 = gardées sur la carte SD, 1 = fichiers CSV dans Google Drive (par défaut),
// 2 = table trames_can de la base (seulement pour un serveur maison : trop volumineux pour un cloud gratuit).
void choisirLEnvoiDesTramesCAN(int mode);

// Lance une synchronisation tout de suite (si le Wi-Fi de la maison est à portée).
void synchroniserMaintenant();

// À appeler avant d'utiliser le Wi-Fi pour autre chose (point d'accès de l'appli) :
// arrête proprement la synchronisation en cours (attend au plus quelques secondes).
void interrompreLaSynchronisation();
bool synchronisationEnCours();

// Message {"t":"synchro", ...} pour l'appli : réglages (sans les secrets) et état.
void decrireLaSynchronisation(JsonDocument& document);

bool demarrerModuleSynchro();

#endif // MODULE_SYNCHRO_H
