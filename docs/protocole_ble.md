# Protocole Bluetooth entre le module et l'application Android

Le firmware (`src/module_appli.cpp`) expose **un seul service BLE** nommé `Moto_ESP32`.
L'application Android (`android/`) s'y connecte pour recevoir les données, envoyer des commandes et mettre à jour le firmware.

## Sécurité

Toutes les caractéristiques exigent une liaison **chiffrée et authentifiée (MITM)**.
Au premier branchement, Android demande un **code PIN à 6 chiffres** : c'est `SECRET_BLE_PIN` dans `include/secrets.h`
(voir `include/secrets.example.h`). Le téléphone est ensuite mémorisé (bonding).
Sans ce code, personne ne peut lire les données ni envoyer de firmware.

> Après un changement de code PIN, supprimer l'appairage « Moto_ESP32 » dans les réglages Bluetooth du téléphone.

## Service et caractéristiques

Service : `7a3b0001-5c1e-4b8a-9f2d-6d6f746f3031`

| UUID (préfixe) | Nom | Propriétés | Contenu |
|---|---|---|---|
| `7a3b0002` | Télémétrie | Notify, Read | Messages JSON (ci-dessous) |
| `7a3b0003` | Commande | Write | Texte, ex. `PHOTO` |
| `7a3b0004` | Réponse | Notify | `{"t":"rep","cmd":"PHOTO","ok":true,"msg":"..."}` |
| `7a3b0005` | Logs | Notify | Une ligne de log (texte) |
| `7a3b0006` | OTA contrôle | Write, Notify | `START`, `END`, `ABORT` et leurs réponses |
| `7a3b0007` | OTA données | Write | Morceaux binaires du firmware |

Tous les UUID se terminent par `-5c1e-4b8a-9f2d-6d6f746f3031`. L'application demande un MTU de 517 octets.

## Données en direct (Télémétrie)

Chaque notification contient un objet JSON dont le champ `t` donne le type :

| `t` | Cadence | Champs |
|---|---|---|
| `moto` | 0,5 s | `vit` km/h, `rpm`, `rap` (0 = point mort), `temp` °C, `ess` %, témoins 0/1 : `contact`, `beq`, `frein`, `embr`, `abs`, `clg`, `cld`, `route`, `code`, `warn` |
| `acc` | 0,5 s | `ax` `ay` `az` (m/s²), `roll` `pitch` (°), `g` |
| `gps` | 1 s | `fix` 0/1, `lat`, `lon`, `alt` m, `vit` km/h, `cap` °, `sat`, `hdop`, `maj` (horodatage Unix) |
| `sys` | 2 s | `fw` version, `build` date de compilation, `up` s, `heap` octets, `psram` octets, `part` partition active |
| `4g` | 2 s | `etat` (0 non connecté, 1 actif, 2 connecté), `sig` %, `conso` octets ce mois, `reste` % du forfait utilisé |
| `bat` | 5 s | `ok` 0/1 (jauge présente), `v` volts, `pct` % |

Les valeurs viennent des variables globales du firmware (`lastCANbusData`, `lastGyroscopeData`, `GPS_Data`, `SIM_data`, jauge MAX17048).
Tant qu'un module (bus CAN, accéléromètre…) n'est pas actif, ses valeurs restent à 0.

Pour ajouter une donnée : l'ajouter au JSON dans `envoyer...()` de `module_appli.cpp`, puis dans `Protocole.appliquer()` côté Android.

## Commandes

| Commande | Effet |
|---|---|
| `PING` | Répond `pong` |
| `STATUS` | Renvoie immédiatement toutes les données |
| `GPS` | Demande une nouvelle position au modem |
| `PHOTO` | Prend une photo, enregistrée sur la carte SD |
| `LOGS ON` / `LOGS OFF` | Active ou coupe l'envoi des logs |
| `REBOOT` | Redémarre le module après 1 s |

Les commandes sont insensibles à la casse. Chaque commande reçoit une réponse sur la caractéristique Réponse.
Pour en ajouter une : compléter `traiterCommande()` dans `module_appli.cpp` (et un bouton dans `EcranCommandes.kt`).

## Mise à jour du firmware (OTA)

1. L'application écrit `START <taille> <md5>` sur OTA contrôle. Le module répond `OK START` (ou `ERR ...`).
2. Elle envoie le fichier par morceaux de (MTU − 3) octets sur OTA données, chaque écriture avec accusé de réception.
3. Elle écrit `END`. Le module vérifie la taille et le MD5, active la nouvelle partition, répond `OK END` et redémarre.
4. `ABORT` annule. Une déconnexion pendant l'envoi annule aussi.

Le fichier à envoyer est `firmware.bin` (`.pio/build/esp32-s3-devkitc-1/firmware.bin`), pas l'image fusionnée avec le bootloader.
La table `default_16MB.csv` contient deux partitions applicatives (`app0`, `app1`) : le firmware en cours n'est jamais écrasé,
donc une mise à jour interrompue ou corrompue laisse le module sur l'ancienne version.
Pendant l'envoi, la télémétrie et les logs sont suspendus pour libérer la bande passante.
