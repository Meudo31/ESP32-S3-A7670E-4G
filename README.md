# ESP32-Moto

Boîtier connecté pour moto, basé sur la carte **Waveshare ESP32-S3 A7670E 4G**.
Il suit la position de la moto, la photographie, lit les données du bus CAN et se pilote par SMS, Wi-Fi ou Bluetooth.

> 🚧 Projet en cours de développement : certains modules sont encore expérimentaux (voir [État des modules](#état-des-modules)).

---

## Fonctionnalités

- **Localisation GNSS** (GPS + Galileo + GLONASS) par le modem A7670E
- **Commandes par SMS** depuis un numéro autorisé : `Photo`, `GPS`, `Status`, `Reboot`
- **Envoi de la position par SMS** (lien Google Maps, vitesse, direction)
- **Photo** (caméra OV) envoyée par MMS, enregistrée sur carte SD ou transmise en Wi-Fi / Bluetooth
- **Suivi de la consommation data** du forfait 4G
- **Lecture du bus CAN** de la moto (frein, ABS, embrayage, béquille, feux…)
- **Accéléromètre** ADXL345 avec interruption de mouvement
- **Point d'accès Wi-Fi** avec page web de données et serveur FTP pour la carte SD
- **Bluetooth Low Energy** pour consulter les logs depuis un smartphone
- **Journalisation** sur le port série et sur la carte SD
- **Jauge de batterie** MAX17048
- **LED RGB** WS2812B d'état

---

## Matériel

| Élément | Détail |
|---|---|
| Carte | Waveshare ESP32-S3 A7670E 4G (ESP32-S3, 16 Mo de flash, PSRAM) |
| Modem | SIMCom A7670E (LTE Cat-1 / 2G / GNSS), relié en **USB** à l'ESP32-S3 |
| Antennes | LTE et GNSS (connecteurs IPEX) |
| Caméra | OV2640 / OV5640 (connecteur 24 broches) |
| Stockage | Carte micro-SD |
| Batterie | Li-ion 18650 |
| Alimentation moto | 12 V sur l'entrée solaire (cavalier MPPT SET sur 12 V) |
| Accéléromètre | ADXL345 |
| Bus CAN | Transceiver WCMCU-230 (SN65HVD230) |
| Micro | INMP441 (I2S) |

### Interrupteurs DIP (dos de la carte)

| DIP | Position | Rôle |
|---|---|---|
| CAM | ON | Alimente la caméra |
| HUB | ON | Alimente le hub USB |
| 4G | ON | Alimente et démarre le modem |
| USB | **OFF** | Relie l'USB du modem à l'ESP32-S3 (ON = au connecteur USB-C) |

### Broches principales

| Fonction | GPIO |
|---|---|
| Modem A7670E (USB natif) | 19 (D−), 20 (D+) |
| Carte SD (SDMMC 1 bit) | CLK 5, CMD 4, D0 6, CD 46 |
| LED RGB WS2812B | 38 |
| Jauge batterie MAX17048 (I2C) | SDA 15, SCL 16 |
| Moniteur série (UART0, via CH343) | TX 43, RX 44 |

Le détail du câblage des modules externes est dans les commentaires de [`include/main.h`](include/main.h).

---

## Architecture logicielle

Le programme repose sur **FreeRTOS**, avec des tâches réparties sur les deux cœurs :

- **Cœur 0 (réseau)** : réception des SMS, envoi des SMS et MMS, serveur web, Bluetooth, bus CAN
- **Cœur 1 (traitement)** : GPS, caméra, carte SD, logs, accéléromètre, LED

Les tâches communiquent par **files** (`xQueue`) et **notifications** (`xTaskNotify`). Chaque fonction matérielle est isolée dans un module :

```
src/
├── main.cpp               Initialisation et lancement des tâches
├── modem_usb.cpp          Liaison USB hôte avec le modem A7670E (classe ModemUSB)
├── module_sms.cpp         Initialisation du modem, SMS, MMS, consommation data
├── module_gps.cpp         Position GNSS
├── module_camera.cpp      Prise de photo (+ données EXIF)
├── module_SD.cpp          Stockage sur carte SD
├── module_logs.cpp        Journalisation (série + SD)
├── module_wifi.cpp        Point d'accès, page web, serveur FTP
├── module_bluetooth.cpp   Logs en Bluetooth Low Energy
├── module_CANbus.cpp      Écoute et décodage du bus CAN
├── module_gyroscope.cpp   Accéléromètre ADXL345
├── module_batterie.cpp    Jauge de batterie MAX17048
├── module_LED_ESP32.cpp   LED RGB d'état
├── module_DateHeure.cpp   Date et heure système
├── module_display.cpp     Écran TFT (désactivé)
├── module_hautParleur.cpp Messages audio (désactivé)
└── module_test_micro.cpp  Test du micro I2S
```

---

## Compilation

1. Installer **VS Code** et l'extension **PlatformIO**.
2. Cloner le dépôt et l'ouvrir dans VS Code.
3. Vérifier le type de PSRAM de la puce dans `platformio.ini` :
   - ESP32-S3**R8** (8 Mo, OPI) : `board_build.arduino.memory_type = qio_opi`
   - ESP32-S3**R2** (2 Mo, Quad) : `board_build.arduino.memory_type = qio_qspi`
4. Brancher la carte sur le port USB-C (convertisseur CH343).
5. **Upload**, puis **Monitor** (115200 bauds).

Si le téléversement échoue : maintenir **BOOT**, appuyer brièvement sur **RESET**, relâcher **BOOT** au début du téléversement.

> `ARDUINO_USB_CDC_ON_BOOT=0` est obligatoire : l'USB natif de l'ESP32-S3 est réservé au modem, et `Serial` passe par le CH343.

---

## Configuration

Dans [`src/module_sms.cpp`](src/module_sms.cpp) :

- `monOperateur` : opérateur de la carte SIM (APN et paramètres MMS)
- `listeNumerosTelephone` : numéros autorisés à envoyer des commandes

La carte SIM doit avoir son **code PIN désactivé**.

> ⚠️ Ne publiez pas de numéros de téléphone, mots de passe ou identifiants dans le code si le dépôt devient public : placez-les dans un fichier exclu par `.gitignore`.

---

## Commandes SMS

Envoyer depuis un numéro autorisé :

| SMS | Action |
|---|---|
| `Photo` | Prend une photo et l'envoie par MMS |
| `GPS` | Renvoie la position par SMS |
| `Status` | État du système *(à développer)* |
| `Reboot` | Redémarre la carte |

---

## État des modules

| Module | État |
|---|---|
| Modem USB, SMS | Réécrit, à valider sur la carte |
| GPS | En cours |
| MMS photo | En cours |
| Caméra, carte SD, logs | En cours |
| Wi-Fi, FTP, Bluetooth | En cours |
| Bus CAN, accéléromètre | En cours |
| Écran, haut-parleur | Désactivés |

La liste détaillée des évolutions prévues est dans [`Liste_des_taches_a_faire.txt`](Liste_des_taches_a_faire.txt).

### Points connus

- Plusieurs tâches lisent le modem en même temps : la réception des SMS peut « voler » les réponses destinées au GPS ou au MMS. Une tâche unique de gestion du modem est prévue.
- Les broches caméra notées dans `main.h` sont à vérifier avec la documentation Waveshare de la carte V2.

---

## Documentation

- [Documentation Waveshare ESP32-S3-A7670E-4G](https://docs.waveshare.com/ESP32-S3-A7670E-4G)
- [FAQ Waveshare](https://docs.waveshare.com/ESP32-S3-A7670E-4G/FAQ)
