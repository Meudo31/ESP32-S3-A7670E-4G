// MODÈLE à copier en include/secrets.h puis à remplir. Ce fichier-ci peut être publié,
// secrets.h ne l'est jamais (il est exclu par .gitignore).
#pragma once

// Code PIN Bluetooth (6 chiffres) demandé par le téléphone lors du premier appairage avec l'application Android.
#define SECRET_BLE_PIN   123456

// Numéro qui reçoit les alertes SMS (démarrage, position GPS, déplacement de plus de 5 km), au format +33...
// Facultatif : il se règle aussi depuis l'application (écran Téléphone). Laisser "" pour ne rien envoyer.
#define SECRET_NUMERO_ALERTES   ""
