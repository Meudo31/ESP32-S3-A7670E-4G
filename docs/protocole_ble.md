# Protocole Bluetooth entre le module et l'application Android

Le firmware (`src/module_appli.cpp`) expose **un seul service BLE**, annoncé sous le nom `Moto_ESP32`.
L'application Android (`android/`) s'y connecte pour recevoir les données, envoyer des commandes,
parcourir la carte SD et mettre à jour le firmware.

## Sécurité

Toutes les caractéristiques exigent une liaison **chiffrée et authentifiée (MITM)**.
Au premier branchement, Android demande un **code PIN à 6 chiffres** : c'est `SECRET_BLE_PIN` dans `include/secrets.h`
(voir `include/secrets.example.h`). Le téléphone est ensuite mémorisé (bonding).

> Après un changement de code PIN, supprimer l'appairage « Moto_ESP32 » dans les réglages Bluetooth du téléphone.

## Service et caractéristiques

Service : `7a3b0001-5c1e-4b8a-9f2d-6d6f746f3031`. Tous les UUID se terminent par `-5c1e-4b8a-9f2d-6d6f746f3031`.
L'application demande un MTU de 517 octets.

| UUID (préfixe) | Nom | Propriétés | Contenu |
|---|---|---|---|
| `7a3b0002` | Données | Notify, Read | Messages JSON d'état (ci-dessous) |
| `7a3b0003` | Commande | Write | Texte : `NOM argument1\|argument2` |
| `7a3b0004` | Réponse | Notify | Réponses et événements JSON (SMS reçu, page de dossier, photo…) |
| `7a3b0005` | Journal | Notify | Une ligne du journal (texte) |
| `7a3b0006` | OTA contrôle | Write, Notify | `START`, `END`, `ABORT` et leurs réponses |
| `7a3b0007` | OTA données | Write | Morceaux binaires du firmware |
| `7a3b0008` | Fichier | Notify | Morceaux binaires d'un fichier téléchargé |
| `7a3b0009` | Fichier entrant | Write | Morceaux binaires d'un fichier envoyé vers la carte SD |

À la connexion, l'application envoie `HEURE <secondes Unix>` : le module se met à l'heure s'il n'a ni GPS ni réseau.

## Données en direct

Chaque notification contient un objet JSON dont le champ `t` donne le type.

| `t` | Cadence | Champs principaux |
|---|---|---|
| `gps` | 1 s | `fix` 0/1, `type` (2D/3D), `lat`, `lon`, `alt` m, `vit` / `vmax` km/h, `cap` °, `pdop` `hdop` `vdop`, satellites `sat` `s_gps` `s_glo` `s_gal` `s_bds`, `utc` (s Unix), `age` s |
| `bat` | 5 s | `ok` (jauge présente), `v` V, `pct` %, `taux` %/h, `charge`, `pleine`, `alertes`, `source`, `autonomie` h, `mesure` (12 V / USB câblés), `v12`, `vusb`, `vmodem`, `jauge`, `temp` |
| `4g` | 5 s | `pret`, `inscr` (code CREG), `op`, `techno`, `csq`, `dbm`, `sig` %, `mccmnc`, `tac`, `cell`, `bande`, `earfcn`, `rsrp`, `rsrq`, `rssi`, `snr`, `ip`, `apn` |
| `sys` | 5 s | `fw`, `build`, `carte`, `puce`, `rev`, `coeurs`, `mhz`, `temp`, `flash`, `flash_mhz`, `prog`, `prog_libre`, `sdk`, `mac`, `part`, `up` s, `reset`, `boots` |
| `ble` | 5 s | Liaison vue par le module : `rssi` dBm, `int` intervalle ms, `to` délai de coupure s, `deco` nombre de déconnexions, `hist` (4 dernières coupures : heure, raison, durée, signal) |
| `mem` | 5 s | `ram_total` `ram_libre` `ram_min` `ram_bloc`, `psram_total` `psram_libre` `psram_min` `psram_bloc` (octets) |
| `sim` | 15 s | `modele`, `logiciel`, `imei`, `voix` (le modem sait téléphoner), `etat`, `iccid`, `imsi`, `num` |
| `carte` | 15 s | `version` (V1/V2), `methode` (comment elle a été détectée), `camera`, `sd`, `sd_type`, `sd_total`, `sd_util`, `heure` (source de l'heure), `vol` |
| `neo` | 15 s et à chaque changement | `num`, `nom`, `lum` 0-255, `allumee`, `noms` (les 13 animations) |
| `appel` | 15 s et à chaque changement | `etat` (0 aucun, 1 sonnerie, 2 numérotation, 3 en communication), `num`, `duree` s |
| `taches` | sur `SYSTEME` | `debut` (1 = nouvelle liste), `l` : `[nom, pile, pile restante, priorité, cœur]` ; la liste arrive en plusieurs messages |

Pour ajouter une donnée : l'ajouter dans la fonction `decrire...()` du module concerné, puis dans `Protocole.appliquer()` côté Android.

## Événements (caractéristique Réponse)

| `t` | Quand | Champs |
|---|---|---|
| `rep` | Réponse à chaque commande | `cmd`, `ok`, `msg` |
| `sms_recu` | Un SMS arrive | `i` (emplacement dans la SIM), `de`, `date`, `txt` |
| `sms_liste_debut`, puis `sms` | Après `SMS_LISTE` | `i`, `de`, `date`, `txt`, `lu` |
| `sms_envoi` | Fin d'envoi d'un SMS | `num`, `ok` |
| `photo` | Après `PHOTO` | `ok`, `chemin`, `taille`, `msg` |
| `ls` | Après `FICHIERS` | `chemin`, `e` : `[nom, dossier 0/1, taille, date Unix]`, `fin` (dernière page) |
| `dl_debut`, `dl_fin` | Téléchargement d'un fichier | `chemin`, `taille`, puis `ok`, `crc`, `msg` |

## Commandes

Les arguments sont séparés du nom par un espace, et entre eux par `|`. Les noms sont insensibles à la casse.

| Commande | Effet |
|---|---|
| `PING` | Répond `pong` |
| `STATUS` | Renvoie immédiatement toutes les données |
| `SYSTEME` | Renvoie le système, les mémoires et la liste des tâches FreeRTOS |
| `REBOOT` | Redémarre le module |
| `LOGS ON` / `LOGS OFF` | Active ou coupe l'envoi du journal |
| `HEURE <secondes>` | Donne l'heure du téléphone |
| `PHOTO` | Prend une photo dans `/photos/jj-mm-aaaa/jj-mm-aaaa_hh-mm-ss_ESP32-A7670.jpg` |
| `GPS`, `RESEAU` | Renvoie la position ou l'état du réseau 4G |
| `SMS_ENVOYER numéro\|texte` | Envoie un SMS (découpé en plusieurs si besoin) |
| `SMS_LISTE` | Liste les SMS de la carte SIM |
| `SMS_SUPPRIMER i` | Supprime le SMS de l'emplacement `i` |
| `APPELER numéro`, `DECROCHER`, `RACCROCHER` | Téléphone (sans micro pour l'instant : on entend seulement l'appelant) |
| `VOLUME 0-5` | Volume du haut-parleur |
| `ANNONCER texte` | Fait prononcer un texte au haut-parleur |
| `SON_TEST` | Bip (`AT+SIMTONE`), phrase (`AT+CTTS`) et son SMS ; répond `SON_TEST` avec ce que le modem a accepté |
| `SON_JOUER chemin` | Copie un son de la carte SD (WAV, AMR, MP3) dans le modem et le joue sur le haut-parleur |
| `SORTIE_AUDIO 1\|3` | Sortie audio du modem (`AT+CSDVC`) : 3 = haut-parleur (par défaut), 1 = combiné ; mémorisée, puis test |
| `SONS_INSTALLER` | Copie `/sons/sms.wav` et `/sons/sonnerie.wav` de la carte SD dans le modem |
| `NEO n`, `NEO_SUIVANTE`, `NEO_ONOFF`, `NEO_LUMINOSITE 0-255` | Bande NeoPixel |
| `FICHIERS chemin` | Liste un dossier de la carte SD |
| `FICHIER_RENOMMER ancien\|nouveau`, `FICHIER_DEPLACER source\|destination` | Renomme ou déplace un fichier ou un dossier |
| `FICHIER_SUPPRIMER chemin` | Supprime un fichier ou un dossier (avec son contenu) |
| `DOSSIER_CREER chemin` | Crée un dossier (et ses parents) |
| `FICHIER_TELECHARGER chemin` | Envoie un fichier au téléphone (voir ci-dessous) |
| `DL_ACK n`, `DL_ANNULER` | Contrôle du téléchargement |
| `FICHIER_ENVOYER chemin\|taille` | Prépare la réception d'un fichier envoyé par le téléphone (voir ci-dessous) |
| `FICHIER_ENVOYER_FIN crc32` | Vérifie le fichier reçu et lui donne son nom définitif |
| `FICHIER_ENVOYER_ANNULER` | Abandonne l'envoi et efface le fichier partiel |

Pour en ajouter une : une ligne dans le tableau `COMMANDES` de `module_appli.cpp`, puis un bouton dans l'écran Android concerné.

## Téléchargement d'un fichier de la carte SD

1. L'application envoie `FICHIER_TELECHARGER /photos/03-11-2026/xxx.jpg`.
2. Le module répond `dl_debut` (chemin, taille), puis envoie le fichier en morceaux binaires sur la caractéristique Fichier.
3. Tous les 8 morceaux, il attend que l'application envoie `DL_ACK <octets reçus>`, pour ne pas saturer la liaison.
4. À la fin, `dl_fin` donne le CRC32 du fichier ; l'application le compare au sien. `DL_ANNULER` arrête le transfert.

## Envoi d'un fichier du téléphone vers la carte SD

1. L'application envoie `FICHIER_ENVOYER /sons/sms.wav|48210` et attend la réponse `rep` (ok, ou la raison : place, carte absente...).
2. Elle écrit le contenu par morceaux de (MTU − 3) octets, 509 au plus, sur la caractéristique Fichier entrant, chaque écriture avec accusé de réception.
   Le module les écrit dans `/sons/sms.wav.part`, par paquets de 8 Ko. Pendant l'envoi, les données en direct et le journal sont suspendus.
   En cas de problème (carte SD lente ou pleine), le module envoie aussitôt `rep` avec `cmd` = `FICHIER_ENVOYER`, `ok` = false et la cause :
   l'application arrête l'envoi.
3. Elle envoie `FICHIER_ENVOYER_FIN <crc32>`. Le module vérifie la taille et le CRC32, remplace un éventuel fichier du même nom
   et renomme `.part` en nom définitif. Une déconnexion ou `FICHIER_ENVOYER_ANNULER` efface le fichier partiel.

## Mise à jour du firmware (OTA)

1. L'application écrit `START <taille> <md5>` sur OTA contrôle. Le module répond `OK START` (ou `ERR ...`).
2. Elle envoie le fichier par morceaux de (MTU − 3) octets sur OTA données, chaque écriture avec accusé de réception.
3. Elle écrit `END`. Le module vérifie la taille et le MD5, active la nouvelle partition, répond `OK END` et redémarre.
4. `ABORT` annule. Une déconnexion pendant l'envoi annule aussi.

Le fichier à envoyer est `firmware.bin` : l'artefact **firmware-esp32** de l'action GitHub « Firmware ESP32 »,
ou `.pio/build/esp32-s3-devkitc-1/firmware.bin` après une compilation locale.
La table `default_16MB.csv` contient deux partitions applicatives : une mise à jour interrompue laisse le module sur l'ancienne version.
Pendant l'envoi, les données et le journal sont suspendus pour libérer la bande passante.
