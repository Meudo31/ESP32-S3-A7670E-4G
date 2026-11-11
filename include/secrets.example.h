// MODÈLE à copier en include/secrets.h puis à remplir. Ce fichier-ci peut être publié,
// secrets.h ne l'est jamais (il est exclu par .gitignore).
#pragma once

// Code PIN Bluetooth (6 chiffres) demandé par le téléphone lors du premier appairage avec l'application Android.
#define SECRET_BLE_PIN   123456

// Numéro qui reçoit les alertes SMS (démarrage, position GPS, déplacement de plus de 5 km), au format +33...
// Facultatif : il se règle aussi depuis l'application (écran Téléphone). Laisser "" pour ne rien envoyer.
#define SECRET_NUMERO_ALERTES   ""

// ─── Synchronisation au retour à la maison (module_synchro) : tout se règle aussi depuis l'appli ───
// Wi-Fi de la maison (mode station).
#define SECRET_WIFI_MAISON_NOM            ""
#define SECRET_WIFI_MAISON_MOT_DE_PASSE   ""
// Base de données : adresse de l'API PostgREST et sa clé.
//   Supabase : "https://<projet>.supabase.co/rest/v1" et la clé « publishable » (ou « anon »).
//   Serveur maison (dossier serveur/) : "http://192.168.1.x:3000" et la clé créée par serveur/creer_cle.sh.
#define SECRET_API_ADRESSE                ""
#define SECRET_API_CLE                    ""
// Compte Google (photos → Google Photos, trames CAN → Google Drive) : voir docs/synchro_maison.md.
#define SECRET_GOOGLE_CLIENT_ID           ""
#define SECRET_GOOGLE_CLIENT_SECRET       ""
#define SECRET_GOOGLE_JETON               ""   // jeton de rafraîchissement (refresh token)
