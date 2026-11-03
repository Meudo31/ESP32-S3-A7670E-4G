# Application Android « Moto ESP32 »

Application Kotlin / Jetpack Compose pour piloter le module de la moto en Bluetooth Low Energy.

## Fonctions

Menu latéral avec un écran par fonction :

- **Tableau de bord** : résumé de tout le module (batterie, GPS, 4G, animation, appel en cours).
- **GPS** : carte OpenStreetMap avec le trajet, compteur de vitesse, boussole, altitude, satellites par constellation, précision, courbes.
- **Téléphone** : SMS (lire, envoyer, supprimer), appels (composer, décrocher, raccrocher) et toutes les données du réseau 4G et de la carte SIM.
  Un appel entrant s'affiche par-dessus n'importe quel écran.
- **Photo** : prendre une photo (enregistrée sur la carte SD du module) et la voir.
- **Fichiers** : parcourir la carte SD, renommer, déplacer, supprimer, créer un dossier, télécharger un fichier sur le téléphone.
- **Lumières et son** : animation en cours et choix parmi les 10, luminosité, volume, faire parler le module.
- **Batterie** : charge, tension, source d'alimentation, autonomie, historique.
- **Module** : processeur, mémoires, tâches FreeRTOS, carte SD, version de la carte, redémarrage.
- **Journal** : journal du module en direct.
- **Mise à jour** : envoi d'un nouveau firmware par Bluetooth (OTA), avec vérification MD5.

La carte a besoin d'Internet sur le téléphone pour charger les fonds de carte.

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
├── ble/MotoBleClient.kt     Recherche, connexion, appairage, commandes, téléchargements, mise à jour OTA
└── ui/
    ├── AppliMoto.kt         Menu latéral, connexion, appel entrant
    ├── Graphiques.kt        Jauges, boussole, barres, courbes (dessinés avec Canvas)
    ├── Composants.kt        Cartes et lignes « libellé : valeur »
    └── Ecran*.kt            Un fichier par écran
```
