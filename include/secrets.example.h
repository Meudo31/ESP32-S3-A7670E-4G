// MODÈLE à copier en include/secrets.h puis à remplir. Ce fichier-ci peut être publié.
#pragma once

// Point d'accès Wi-Fi de la moto (mot de passe : 8 caractères minimum)
#define SECRET_WIFI_SSID      "Moto_ESP32"
#define SECRET_WIFI_PASSWORD  "CHANGE_MOI_12345"

// Code PIN Bluetooth (6 chiffres) demandé par le téléphone lors du premier appairage avec l'application Android
#define SECRET_BLE_PIN   123456

// Répertoire téléphonique autorisé à piloter la moto par SMS
#define SECRET_TEL_SIM   "+33600000000"
#define SECRET_TEL_MOI   "+33600000001"
#define SECRET_TEL_NATH  "+33600000002"
