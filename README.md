# ESP32-Moto

Boîtier connecté pour moto, basé sur la carte **Waveshare ESP32-S3 A7670E 4G**, piloté par une application Android en Bluetooth.

> 🚧 Projet en cours de développement : le firmware compile, mais plusieurs fonctions restent à valider sur la carte (voir [À vérifier sur la carte](#à-vérifier-sur-la-carte)).

---

## Fonctionnalités

- **GPS** (GPS, GLONASS, Galileo, BeiDou) par le modem A7670E : position, vitesse, altitude, cap, satellites, précision
- **4G** : SMS envoyés et reçus, appels sortants et entrants, toutes les informations du réseau et de la carte SIM
- **Photo** enregistrée sur la carte SD, un dossier par jour : `/photos/jj-mm-aaaa/jj-mm-aaaa_hh-mm-ss_ESP32-A7670.jpg`
- **Bande de 14 LED NeoPixel** (GPIO 40) avec 10 animations, changées par le **bouton** (GPIO 21) ou l'application
- **Haut-parleur** du modem : nom de l'animation prononcé à chaque changement, son à l'arrivée d'un SMS, sonnerie et voix de l'appelant
- **Batterie** : charge, tension, en charge ou non, source d'alimentation, autonomie (jauge MAX17048)
- **Journal** du module : un fichier par jour sur la carte SD (`/journaux/jj-mm-aaaa.log`), aussi visible en direct dans l'application
- **Explorateur de la carte SD** dans l'application : parcourir, renommer, déplacer, supprimer, créer des dossiers, télécharger
- **Mise à jour du firmware** par Bluetooth depuis l'application
- **État du module** : processeur, mémoires, tâches FreeRTOS, version de la carte (V1 ou V2, détectée au démarrage)

L'application Android est dans [`android/`](android/README.md), le protocole Bluetooth dans [`docs/protocole_ble.md`](docs/protocole_ble.md).

---

## Matériel

| Élément | Détail |
|---|---|
| Carte | Waveshare ESP32-S3 A7670E 4G (ESP32-S3, 16 Mo de flash, PSRAM), version V1 ou V2 |
| Modem | SIMCom A7670E (LTE Cat-1 / 2G / GNSS), relié en UART à l'ESP32-S3 |
| Antennes | LTE et GNSS (connecteurs IPEX) |
| Caméra | OV2640 / OV5640 (connecteur 24 broches) |
| Stockage | Carte micro-SD |
| Batterie | Li-ion 18650 |
| Haut-parleur | Sur les pastilles SPK du modem |
| Bande LED | 14 × WS2812B sur GPIO 40 |
| Bouton | Entre GPIO 21 et la masse (GND) |
| Alimentation moto | 12 V sur l'entrée solaire (cavalier MPPT SET sur 12 V) |

### Interrupteurs DIP (dos de la carte)

| DIP | Position | Rôle |
|---|---|---|
| CAM | ON | Alimente la caméra |
| 4G | ON | Alimente et démarre le modem |

### Broches

Communes aux deux versions (voir [`include/configuration.h`](include/configuration.h)) :

| Fonction | GPIO |
|---|---|
| Bande NeoPixel | 40 |
| Bouton des animations | 21 |
| Modem A7670E (UART, sens détecté au démarrage) | 17, 18 |
| Carte SD (SDMMC 1 bit) | CLK 5, CMD 4, D0 6, CD 46 |
| LED RGB de la carte | 38 |
| Caméra SIOD / SIOC, D0 à D7 | 15 / 16, 7 à 14 |
| Moniteur série (UART0, via CH343) | TX 43, RX 44 |

Qui changent entre V1 et V2 (voir [`include/module_carte.h`](include/module_carte.h)) :

| Fonction | V1 | V2 |
|---|---|---|
| Jauge batterie MAX17048 (SDA / SCL) | 3 / 2 | 15 / 16 |
| Caméra XCLK, VSYNC, HREF, PCLK | 34, 36, 35, 37 | 39, 42, **41**, 46 |

Au démarrage, le module cherche la jauge sur 15/16 (V2), puis sur 3/2 (V1), puis regarde la taille de la PSRAM.

> ⚠️ **Sur la V2, la caméra utilise le GPIO 41** : la bande NeoPixel est donc sur le GPIO 40.
> Si la bande est un jour remise sur le 41, le firmware la garde et désactive la caméra.

---

## Architecture logicielle

Chaque fonction est un **module** (`src/module_xxx.cpp` + `include/module_xxx.h`) qui tourne dans **sa propre tâche FreeRTOS**.
Le code est écrit en français, avec des noms explicites, pour se lire presque comme un livre.
Les tâches de communication sont sur le cœur 0, les traitements sur le cœur 1.

```
src/
├── main.cpp                         Démarre les modules dans l'ordre, puis laisse FreeRTOS travailler
├── module_carte.cpp                 Détecte la version de la carte (V1 ou V2) et ses broches
├── module_systeme.cpp               Processeur, mémoires, tâches surveillées, compteur de démarrages
├── module_journal.cpp               Journal : série, carte SD (un fichier par jour) et application
├── module_carte_sd.cpp              Accès à la carte SD partagé entre les tâches
├── module_horloge.cpp               Heure du module (GPS, puis réseau, puis téléphone)
├── module_modem.cpp                 Seul module qui parle au modem (commandes AT, notifications)
├── module_reseau_4g.cpp             Réseau 4G, carte SIM, signal
├── module_sms.cpp                   Envoi et réception des SMS
├── module_appels.cpp                Appels entrants et sortants
├── module_haut_parleur.cpp          Voix de synthèse, sons, sonnerie, volume
├── module_gps.cpp                   Position GNSS
├── module_camera.cpp                Photos sur la carte SD
├── module_neopixels.cpp             Les 10 animations de la bande LED
├── module_bouton.cpp                Bouton : appui court = animation suivante, appui long = allumer / éteindre
├── module_batterie.cpp              Jauge de batterie et source d'alimentation
├── module_explorateur_fichiers.cpp  Fichiers de la carte SD pour l'application
├── module_mise_a_jour.cpp           Mise à jour du firmware par Bluetooth
├── module_appli.cpp                 Bluetooth : données, commandes, journal vers l'application
└── outils_texte.cpp                 Conversions de texte (UCS2 du modem, découpage des champs)
```

L'ancien firmware (liaison USB avec le modem, MMS, Wi-Fi, bus CAN, accéléromètre) est conservé dans [`archives/`](archives/) pour en reprendre des morceaux plus tard.

---

## Compilation

**Sans rien installer** : à chaque modification, GitHub compile le firmware (onglet **Actions** → « Firmware ESP32 »).
L'artefact **firmware-esp32** contient `firmware.bin`, à envoyer par l'application (écran Mise à jour).

**Avec PlatformIO** (VS Code) :
1. Copier `include/secrets.example.h` en `include/secrets.h` et choisir le code PIN Bluetooth (`SECRET_BLE_PIN`).
   Ce fichier n'est jamais envoyé sur GitHub.
2. Vérifier le type de PSRAM dans `platformio.ini` :
   - ESP32-S3**R8** (8 Mo, OPI) : `board_build.arduino.memory_type = qio_opi`
   - ESP32-S3**R2** (2 Mo, Quad) : `board_build.arduino.memory_type = qio_qspi`
3. Brancher la carte sur le port USB-C (convertisseur CH343), **Upload**, puis **Monitor** (115200 bauds).

Si le téléversement échoue : maintenir **BOOT**, appuyer brièvement sur **RESET**, relâcher **BOOT** au début du téléversement.

La carte SIM doit avoir son **code PIN désactivé**.

### Sons personnalisés (facultatif)

Déposer `/sons/sms.wav` et `/sons/sonnerie.wav` (WAV mono 8 kHz) sur la carte SD, puis « Installer les sons » dans l'application.
Sans eux, le module prononce une phrase à la place.

---

## À vérifier sur la carte

Ces points ne peuvent pas être testés sans la carte :

- **Modem** : la liaison UART 17/18 (le sens est détecté automatiquement) ; s'il ne démarre pas seul, renseigner `BROCHE_ALLUMAGE_MODEM`.
- **Haut-parleur** : branché sur les pastilles SPK du modem ; la voix de synthèse (`AT+CTTS`) n'existe pas sur toutes les variantes de l'A7670E
  (la variante « LNXY-UBL » n'a pas du tout de voix). L'application affiche si le modem sait téléphoner.
- **Caméra** : à essayer maintenant que la bande NeoPixel est sur le GPIO 40.
- **Bouton** : doit relier le GPIO 21 à la masse (le firmware active la résistance de tirage interne).
- **Source d'alimentation** : la carte ne permet pas de mesurer directement le 12 V ou l'USB ; la source est déduite de la charge de la batterie.
  Pour une mesure exacte, câbler un pont diviseur vers une entrée analogique (`BROCHE_MESURE_12V`, `BROCHE_MESURE_USB`).
- **GPS** : l'ordre des champs de `AT+CGNSSINFO` (nombre de satellites par constellation) peut varier selon la version du modem.
- **Bluetooth** : la vitesse de téléchargement des fichiers et des photos.

---

## Documentation

- [Documentation Waveshare ESP32-S3-A7670E-4G](https://docs.waveshare.com/ESP32-S3-A7670E-4G)
- [FAQ Waveshare](https://docs.waveshare.com/ESP32-S3-A7670E-4G/FAQ)
