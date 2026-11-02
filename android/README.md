# Application Android « Moto ESP32 »

Application Kotlin / Jetpack Compose pour piloter le module de la moto en Bluetooth Low Energy.

## Fonctions

- **Tableau de bord** : données moto (bus CAN), position GPS (ouverture dans une appli de carte), batterie du boîtier, réseau 4G, accéléromètre, état du module.
- **Commandes** : photo, actualisation GPS, actualisation des données, test de liaison, redémarrage, commande libre.
- **Logs** : journal du module en direct.
- **Mise à jour** : envoi d'un nouveau firmware par Bluetooth (OTA), avec vérification MD5.

Le protocole est décrit dans [`docs/protocole_ble.md`](../docs/protocole_ble.md).

## Installer l'application

Sans rien installer sur le PC : à chaque modification du dossier `android/`, GitHub compile l'APK.
1. Onglet **Actions** du dépôt → dernier passage de « Application Android ».
2. Télécharger l'artefact **MotoESP32-apk** (fichier zip contenant `app-debug.apk`).
3. Copier l'APK sur le téléphone et l'ouvrir (autoriser l'installation d'applications inconnues).

Avec Android Studio : ouvrir le dossier `android/`, brancher le téléphone, bouton ▶.
En ligne de commande : `cd android && ./gradlew assembleDebug`.

Android 8.0 minimum (API 26).

## Premier branchement

1. Dans `include/secrets.h` du firmware, définir `#define SECRET_BLE_PIN 123456` (6 chiffres de votre choix), puis téléverser le firmware.
2. Ouvrir l'application, autoriser le Bluetooth, **Rechercher un module**, choisir `Moto_ESP32`.
3. Android demande le code PIN : saisir `SECRET_BLE_PIN`.
4. Les fois suivantes : bouton **Se connecter à ma moto**.

## Organisation du code

```
app/src/main/java/fr/meudo/motoesp32/
├── MainActivity.kt          Permissions Bluetooth et lancement de l'interface
├── MotoViewModel.kt         Dernier module utilisé, lecture du fichier firmware
├── ble/Protocole.kt         UUID et lecture des messages JSON (testé dans src/test)
├── ble/MotoBleClient.kt     Recherche, connexion, appairage, commandes, mise à jour OTA
└── ui/                      Écrans Compose (tableau de bord, commandes, logs, mise à jour)
```
