# Plan de test du module ESP32-Moto (avant branchement sur la moto)

Carte : **Waveshare ESP32-S3 A7670E 4G** (dépôt GitHub `Meudo31/ESP32-Moto`).
Principe : **un petit programme de test par fonctionnalité**, testé seul sur le bureau (alimentation USB-C), avec un critère "OK" clair. On n'active le programme complet (`main.cpp` avec toutes les tâches FreeRTOS) qu'une fois tous les tests individuels validés.

## Ce qui existe déjà sur GitHub

- `ESP32-Moto` : le vrai projet. 17 modules dans `src/` (~3 300 lignes), README détaillé, liste de tâches. **Toutes les tâches sont commentées dans `main.cpp`**, donc rien ne tourne encore en même temps.
- `ESP32-Moto/First Test/` : premier test validé (LED RGB GPIO 38 qui "respire").
- `ESP32-S3-A7670E-4G` : dépôt vide.
- `Mon-Projet-Moto` : modèle PlatformIO pour ESP8266, sans rapport.

## Incohérences repérées (à régler avant de tester)

| Point | Constat |
|---|---|
| Liaison modem | Le README parle d'une liaison **USB** (`modem_usb.cpp`, classe `ModemUSB`) mais ce fichier n'existe **ni sur GitHub ni dans l'archive ESP32-Moto-main.zip** (dont le code est identique à GitHub). Le code utilise encore un UART, et `module_sms.h` définit `MODEM_RX_PIN` / `MODEM_TX_PIN` **vides** : `modemSerial.begin(...)` ne compile donc pas en l'état. Les GPIO 19/20 sont les broches USB D−/D+ de l'ESP32-S3, donc la liaison modem est à reconstruire (USB hôte ou UART, selon le schéma Waveshare). |
| USB série | Le README impose `ARDUINO_USB_CDC_ON_BOOT=0`, mais `platformio.ini` contient `=1`. |
| Carte SD | Le README parle de **SDMMC 1 bit** (CLK 5, CMD 4, D0 6), mais `main.cpp` appelle `SD.begin()` (mode SPI). |
| Broches caméra | Celles de `main.h` sont fausses. Doc Waveshare V2 : XCLK 39, SIOD 15, SIOC 16 (même bus I2C que la jauge), Y2…Y9 = 7…14, VSYNC 42, HREF 41, PCLK 46 (partagé avec la détection SD), PWDN/RESET non câblés. |
| Secrets | Ton `.gitignore` local (dans le zip) exclut bien `secrets.h`, mais celui de GitHub est un modèle C++ générique : `include/secrets.h` (Wi-Fi + 3 vrais numéros de téléphone) est **commité** ; il n'est pas dans `.gitignore` contrairement à ce que dit son commentaire. Le dépôt est privé, mais à corriger avant de le rendre public. |

## Les tests, dans l'ordre

Chaque étape s'appuie sur les précédentes. Colonne "OK si" = ce que tu dois voir au moniteur série (115200 bauds).

### Bloc 1 : la carte seule
| # | Fonction | Matériel / broches | OK si |
|---|---|---|---|
| 0 | Préparation | DIP CAM/HUB/4G sur ON, USB sur OFF ; vérifier PSRAM R8 (OPI) ou R2 (Quad) | Upload + moniteur série fonctionnent |
| 1 | LED RGB | WS2812B GPIO 38 | ✅ déjà fait (First Test) |
| 2 | PSRAM / flash | — | `ESP.getPsramSize()` ≈ 8 Mo, flash 16 Mo |
| 3 | Jauge batterie | MAX17048, I2C SDA 15 / SCL 16 | Tension et % cohérents avec un multimètre |
| 4 | Carte SD | SDMMC 1 bit CLK 5, CMD 4, D0 6, CD 46 | Écrit, relit et efface un fichier ; affiche taille et espace libre |
| 5 | Date / heure système | RTC interne | L'heure avance correctement ; survit à un deep sleep de 30 s |

### Bloc 2 : le modem A7670E (avec carte SIM, PIN désactivé)
| # | Fonction | OK si |
|---|---|---|
| 6 | Communication AT (`test_modem_uart`, GPIO 17/18 d'après le schéma V2) | `AT` → `OK`, `ATI` donne le modèle et le firmware |
| 7 | SIM et réseau | `AT+CPIN?` → `READY`, `AT+CSQ` > 10, `AT+CEREG?` enregistré, nom de l'opérateur |
| 8 | Heure réseau | `AT+CCLK?` donne la bonne date/heure, recopiée dans l'heure système |
| 9 | Envoi SMS | Tu reçois un SMS de test |
| 10 | Réception SMS + filtrage | Un SMS d'un numéro autorisé est accepté, un inconnu est rejeté |
| 11 | Commandes SMS | Depuis le numéro des alertes : `Gps`, `Etat`, `Aide` répondent ; un autre numéro est ignoré |
| 12 | Appel voix | La carte décroche / raccroche / appelle (module_appel) |
| 13 | Data 4G | APN configuré, IP obtenue, requête HTTP GET réussie ; compteur de data |

### Bloc 3 : GPS
| # | Fonction | OK si |
|---|---|---|
| 14 | Fix GNSS | Antenne GNSS dehors : position valide en quelques minutes (`AT+CGNSSINFO`), nombre de satellites |
| 15 | SMS de position | Lien Google Maps + vitesse + cap reçus par SMS |
| 16 | Enregistrement trace | Positions horodatées écrites sur la SD |

### Bloc 4 : caméra
| # | Fonction | OK si |
|---|---|---|
| 17 | Capture | Photo JPEG en PSRAM (après validation des broches V2) |
| 18 | Photo sur SD | Fichier lisible sur PC, avec GPS/date dans l'EXIF |
| 19 | Photo par SMS | Commande SMS `Photo` → SMS avec un lien vers la photo (pas de MMS sur l'A7670E) |

### Bloc 5 : connexions sans fil
| # | Fonction | OK si |
|---|---|---|
| 20 | Wi-Fi point d'accès | Le téléphone se connecte, la page web affiche les données |
| 21 | FTP | Fichiers de la SD visibles depuis un client FTP |
| 22 | Bluetooth LE | Les logs s'affichent dans une appli BLE (nRF Connect) |

### Bloc 6 : capteurs externes
| # | Fonction | OK si |
|---|---|---|
| 23 | Accéléromètre ADXL345 | Valeurs X/Y/Z stables à plat ; interruption déclenchée quand on bouge la carte |
| 24 | Micro INMP441 (I2S) | Niveau sonore qui réagit, enregistrement WAV sur SD |
| 25 | Bus CAN (SN65HVD230) | Au bureau : mode loopback du TWAI. Sur la moto : **écoute seule** (listen-only) d'abord, pour ne jamais émettre sur le bus |

### Bloc 7 : alimentation moto (au bureau, avec une alim de labo)
| # | Fonction | OK si |
|---|---|---|
| 26 | Entrée 12 V | Cavalier MPPT sur 12 V ; la carte démarre sur 12-14,5 V, rien ne chauffe |
| 27 | Bascule batterie | Coupure du 12 V → la carte continue sur la 18650 |
| 28 | Consommation / veille | Mesure en marche et en deep sleep (pour ne pas vider la batterie de la moto) |

### Bloc 8 : intégration
Réactiver les tâches de `main.cpp` une par une. Point connu : plusieurs tâches lisent le modem en même temps ; il faut une **tâche unique de gestion du modem** avant d'activer SMS + GPS + MMS ensemble.

## Proposition de mise en œuvre
Un dossier `tests/` dans le dépôt, avec un environnement PlatformIO par test (`pio run -e test_sd`, `pio run -e test_modem`…), qui réutilise les modules existants de `src/` quand c'est possible. Rien n'est modifié tant que tu n'as pas validé.
