#ifndef CONFIG_MAIN_H
#define CONFIG_MAIN_H

#include <Arduino.h>
#include <FreeRTOS.h>
#include <Adafruit_NeoPixel.h>

#include "module_logs.h"


// Waveshare ESP32-S3 A7670E 4G


/* Utilisation des BROCHES Waveshare ESP32-S3 A7670E
        -->>  batterie ────────────────────────────────────
        Broches du bus I2C pour la jauge de batterie MAX17048 (Version V2 de la carte)
        #define I2C_SDA_PIN      15
        #define I2C_SCL_PIN      16

        -->>  CAMÉRA  ───────────────────────────
        #define PWDN_GPIO_NUM     -1
        #define RESET_GPIO_NUM    -1
        #define XCLK_GPIO_NUM     15
        #define SIOD_GPIO_NUM     40
        #define SIOC_GPIO_NUM     39
        #define Y9_GPIO_NUM       16
        #define Y8_GPIO_NUM       17
        #define Y7_GPIO_NUM       18
        #define Y6_GPIO_NUM       12
        #define Y5_GPIO_NUM       10
        #define Y4_GPIO_NUM       8
        #define Y3_GPIO_NUM       9
        #define Y2_GPIO_NUM       11
        #define VSYNC_GPIO_NUM    6
        #define HREF_GPIO_NUM     7
        #define PCLK_GPIO_NUM     13

        /*-->>  GPS et 4G (UART2) CE N EST PAS UN UART MAIS UNE CONNEXION USB (ModemSerial)──────────────────────────────────────────
        _________________________ #define MODEM_TX_PIN     	20 _________________________
        _________________________ #define MODEM_RX_PIN     	19 _________________________
        _________________________ #define MODEM_PWRKEY_PIN 	41 _________________________

        /*-->>  Haut parleur IL UTILISE LE MODEM  ────────────────────────────────────
        _________________________ #define I2S_PIN_BCK         41 // HautParleur (ou 26) Horloge interne BCK _________________________
        _________________________ #define I2S_PIN_WS          40 // HautParleur Horloge interne WS (LRCLK) _________________________
        _________________________ #define I2S_PIN_DATA_OUT    42 // HautParleur Broche DIN de l'amplificateur _________________________
        _________________________ #define I2S_PORT_HP         I2S_NUM_0 // Partage le même périphérique I2S_NUM_0 que le micro _________________________

        /*-->>  La carte Waveshare utilise généralement la broche GPIO 21 pour sa LED système intégrée ────────────────────────────────────
        #define LED_INTEGREE_GPIO	38	// 


---------------------------------------------------------------------------------------------------
--------------------- BROCHAGE ((((pas assez de GPIO pour ajouter l'écran)))) ---------------------
---------------------------------------------------------------------------------------------------
        19  Modem 4G -                  39 SCL caméra -                         26  Flash/PSRAM
        20  Modem 4G -                  40 SDA caméra -                         27  Flash/PSRAM
        4   Micro-SD CMD -              15 XCLK (Horloge) -                     28  Flash/PSRAM
        5   Micro-SD CLK -              13 PCLK (Pixel Clock) -                 29  Flash/PSRAM
        6   Micro-SD D0 -               6  VSYNC (Vertical Sync) -              30  Flash/PSRAM
        46  Micro-SD CD -               7  HREF (Horizontal Sync) -             31  Flash/PSRAM
        38  LED RVB WS2812B             11  Y2 -                                32  Flash/PSRAM
        15  Jauge batterie -            9   Y3 -                                33  Flash/PSRAM
        16  Jauge batterie -            8   Y4 -                                34  Flash/PSRAM
                                        10  Y5 -                                35  Flash/PSRAM
                                        12  Y6 -                                36  Flash/PSRAM
                                        18  Y7 -                                37  Flash/PSRAM
                                        17  Y8 -
                                        16  Y9 -
                Autres LED d'état (témoins matériels indépendants des GPIOs) :
                    Alimentation (Power) : Bleue  
                    Réseau (Network / Status du modem) : Rouge  
                    Charge Solaire : Verte  
                    Inversion de polarité batterie : Jaune 

    [Micro]     3V3      ➡️ VDD 
    [CAN Bus]   3V3      ➡️ 3V3         
    [Gyroscope] 3V3      ➡️ VCC                              
    [Gyroscope] 3V3      ➡️ CS  (Indispensable pour forcer le module en mode I2C)

    [Micro]     GND      ➡️ GND
    [Micro]     GND      ➡️ L/R (Sélectionne le canal gauche pour forcer le micro en mode Mono)
    [CAN Bus]   GND      ➡️ GND      
    [CAN Bus]   GND      ➡️ RS  (Permet de mettre le module en mode haute vitesse normal) [Vérifier s'il y pas déjà une resistance pull-down]
    [Gyroscope] GND      ➡️ GND                             

    [Micro INMP441]  GPIO 42 ➡️ SD      (Ligne de données audio entrantes)
    [Micro INMP441]  GPIO 1  ➡️ SCK/BCK (Horloge de cadencement des bits)
    [Micro INMP441]  GPIO 2  ➡️ WS      (Horloge de synchronisation des mots)
    [NeoPixels]      GPIO 14 ➡️ DIN     (via 330 Ω ; alimentation 5 V dédiée, masses communes)
    [Micro analogique + Haut-parleur] ➡️ bornes MIC et SPK de la carte = audio du modem (appels uniquement)
    [CAN Bus]   GPIO 43  ➡️ CTX     (ou TX) 
    [CAN Bus]   GPIO 44  ➡️ CRX     (ou RX) 
    [Gyroscope] GPIO 42  ➡️ SDA  (Bus partagé)  [SDO / MOSI ???]
    [Gyroscope] GPIO 41  ➡️ SCL  (Bus partagé)  [SCK ???]
    [Gyroscope] GPIO 21  ➡️ INT1 (Broche d'interruption de l'ESP32-S3)    


    3V3                                         GND
    3V3                                         43-TX Moniteur série (UART 0)       [CTX CAN bus]
    GND                                         44-RX Moniteur série (UART 0)       [CRX CAN bus]
    4  SD_CMD Micro-SD                          41  [SCK accéléromètre]
    5  SD_CLK Micro-SD                          40  SDA caméra  
    6  SD_D0  Micro-SD        VSYNC caméra      39  SCL caméra  
    46 SD_CD  Micro-SD                          45  [SD micro]
    15 Jauge batterie  XCLK caméra              2   [WS/LRC micro]
    16 Jauge batterie  Y9 caméra                42  [MOSI accéléromètre]
    7  HREF caméra                              1   [SCK/BLK micro]
    8  Y4 caméra                                21  [INT1 accéléromètre]
    9  Y3 caméra                                3   ESP32 sélection JTAG
    10 Y5 caméra                                19  USB D- (RX) Modem 4G GPS  
    11 Y2 caméra                                20  USB D+ (TX) Modem 4G GPS
    12 Y6 caméra                                0   ESP32 mode de téléchargement du code
    13 PCLK caméra                              18  Y7 caméra   
    14                                          17  Y8 caméra   
    5V                                          VBAT
    GND                                         GND
                --- Il n'y a pas de broche entre 20 et 38 (compris). Les GPIO vont jusau'à 46 ---
                            ----- Interrupteurs DIP : CAM, 4G et HUB sur ON. USB sur OFF -----


Accéléromètre ADXL345 (en mode SPI) :
    SCK :               GPIO 41
    SDO / MOSI :        GPIO 42
    INT1 :              GPIO 21

Transceiver CAN (WCMCU-230) : (pas de croisement TX->RX et RX->TX)
    CTX (Transmit) :    GPIO 43 (TX)
    CRX (Receive) :     GPIO 44 (RX)

Microphone INMP441 (I2S) :
    SCK / BCLK :        GPIO 1
    WS / LRC :          GPIO 2
    SD (Serial Data) :  GPIO 42

Panneau NeoPixel (WS2812B) :
    DIN :               GPIO 14

   

/!\ Avertissement d'alimentation
L'ajout simultané d'un modem 4G en émission, d'une caméra active, d'un écran rétroéclairé et de plusieurs capteurs 
demande un courant important. Assurez-vous d'alimenter votre carte via une source USB stable d'au moins 2A 
ou une batterie correctement dimensionnée.

Remarque importante pour l'ESP32-S3 et le Moniteur Série :
    La puce intègre un port USB natif. 
    Vous pouvez configurer l'IDE Arduino pour utiliser USB-CDC On Boot (ou "USB Serial/JTAG Controller"). 
    De cette façon, vos messages de débogage (Serial.println) passeront par le câble USB natif de la carte, 
    ce qui laisse entièrement libres les broches TX (43) et RX (44) pour votre bus CAN, 
    tout en gardant vos logs de debug actifs en permanence.

    Pour configurer VS Code afin d'utiliser l'interface USB native de l'ESP32-S3 via USB-CDC On Boot, 
    la méthode dépend de l'extension que vous utilisez (PlatformIO ou l'extension Arduino officielle).
    Voici comment procéder pour les deux cas :  
    1. Sous PlatformIO (Le plus courant)Si vous développez avec PlatformIO, tout se configure dans le fichier platformio.ini 
       à la racine de votre projet.Ajoutez les drapeaux de compilation (build_flags) suivants dans votre environnement :
            Ini, TOML
            [env:esp32s3]
            platform = espressif32
            board = esp32-s3-devkitc-1  ; (Adaptez selon votre carte exacte)
            framework = arduino
            monitor_speed = 115200
            build_flags = 
                -D ARDUINO_USB_CDC_ON_BOOT=1  // Force l'activation du port série virtuel dès le démarrage (boot). La fonction standard Serial dans votre code utilisera directement le port USB natif.
                -D ARDUINO_USB_MODE=1         // Active le mode matériel natif CDC/JTAG de l'ESP32-S3.

    2. Sous l'extension Arduino (Microsoft) dans VS Code
       Si vous utilisez l'extension Arduino classique :
            Ouvrez le fichier de réglages de votre espace de travail situé dans .vscode/arduino.json.
            Assurez-vous que les options du fabricant sont configurées pour activer le mode USB natif.
       Vous pouvez aussi utiliser la palette de commandes de VS Code 
            (Ctrl+Shift+P ou Cmd+Shift+P), puis taper et sélectionner 
                Arduino: Board Config. Dans la liste qui s'affiche, modifiez les paramètres suivants :
                    USB CDC On Boot :   Enabled
                    USB Mode :   CDC and JTAG

⚠️ Attention au téléversement (Upload) :Lorsque ARDUINO_USB_CDC_ON_BOOT=1 est actif, 
        l'ESP32-S3 gère sa liaison série de manière logicielle via l'USB natif. 
        Si PlatformIO ou l'IDE n'arrive pas à redémarrer la carte automatiquement pour envoyer le code 
        (A_timed_out_waiting_for_packet), il vous suffira de maintenir le bouton BOOT de la carte enfoncé, 
        d'appuyer brièvement sur RESET, puis de relâcher BOOT au moment exact où le téléversement commence.  


Pourquoi il faut éviter les GPIO 0 et 3 pour le bus CAN :
    Bouton BOOT (utilisable en entrée/sortie numérique simple, mais avec une résistance de pull-up interne et un comportement particulier au démarrage).
    Ce sont des broches de configuration (strapping pins) : 
    Sur l'ESP32-S3, le niveau logique de ces broches est lu au moment exact du démarrage (reset) 
    pour déterminer le mode de fonctionnement de la puce 
    (par exemple, le mode de téléchargement du code pour le GPIO 0 ou la sélection JTAG pour le GPIO 3).  
    Risque de blocage au démarrage : Le transceiver CAN (WCMCU-230) connecté sur ces broches risque d'imposer un 
                                    état électrique indésirable au moment de la mise sous tension. 
                                    Cela peut empêcher l'ESP32-S3 de démarrer normalement 
                                    (blocage, boucle de redémarrage ou impossibilité de flasher le code). 
                                    De plus, le GPIO 0 est physiquement relié au bouton BOOT de la carte.
🎛️ Détail de chaque commutateur DIP
1. Le commutateur CAM (Caméra)
    À quoi il sert : Il active ou coupe l'alimentation électrique dédiée au connecteur 24-broches FPC de la caméra OV2640.
    En pratique : Si vous n'utilisez pas la caméra à un moment donné du projet, le mettre sur OFF coupe complètement son alimentation. 
    Cela permet d'économiser l'énergie de la batterie et de libérer totalement les broches de données de l'ESP32 sans interférence résiduelle.
2. Le commutateur HUB (USB Hub / TinyUSB)
    À quoi il sert : Il contrôle l'alimentation électrique du circuit d'attaque du HUB USB intégré à la carte.
    En pratique : Ce circuit est celui qui gère le raccordement à haute vitesse entre l'ESP32-S3 et les lignes de données USB du modem A7670E. 
    Le mettre sur OFF désactive ce pont d'alimentation lorsque la carte tourne uniquement sur batterie et bascule en mode basse consommation. 
    Pour que la liaison TinyUSB + PPP fonctionne, il doit être sur ON.
3. Le commutateur 4G (Modem A7670E)
    À quoi il sert : Il s'agit du commutateur d'arrêt matériel total de la puce cellulaire 4G / GPS.
    En pratique : C'est le moyen le plus simple d'éteindre le modem sans écrire de code. 
    En le positionnant sur OFF, le module 4G est coupé du rail d'alimentation principal. 
    Si vous souhaitez le contrôler plus finement par code à la place (via vos tâches), 
    vous devez laisser ce switch sur OFF et utiliser directement le déclencheur câblé sur le GPIO 33 ou 22 
    (selon la version de la carte) pour envoyer des impulsions logiques HIGH/LOW.
4. USB : C'est le sélecteur de canal de routage USB. 
    Comme l'ESP32-S3 et le modem 4G se partagent l'unique port physique externe USB-C de la carte, 
    ce switch sert d'aiguillage électrique :
    Position OFF : Le connecteur USB-C externe est directement relié au convertisseur CH343/CH334 de l'ESP32-S3. 
        C'est le mode requis pour flasher votre code depuis PlatformIO et afficher le Moniteur Série CDC.
    Position ON : Le port USB-C externe bascule et se connecte directement aux pistes USB du modem A7670E. 
            Cela permet à votre ordinateur de communiquer avec le modem directement (pour faire des tests AT ou diagnostics via un terminal PC), 
    tandis que l'ESP32 utilise son canal TinyUSB interne pour le dialogue d'arrière-plan.

*/



// -->>  Broches de la carte Waveshare ESP32-S3 A7670E ***********************************************
// #define MODEM_TX_PIN     	43
// #define MODEM_RX_PIN     	44
// #define MODEM_PWRKEY_PIN 	41


// -------------------------------------------------------------------------------------- //
// ----------- Déclarations des outils de synchronisation globaux. ---------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Constantes  ***********************************************
const TickType_t xDelai_CinqMinutes    = pdMS_TO_TICKS(5 * 60 * 1000); //  5 x 60 secondes x 1000 millisecondes = 300 000 ms soit 5 minutes
const TickType_t xDelai_UneMinute      = pdMS_TO_TICKS(    60 * 1000);
const TickType_t xDelai_TrenteSecondes = pdMS_TO_TICKS(    30 * 1000);
const TickType_t xDelai_DixSecondes    = pdMS_TO_TICKS(    10 * 1000);

enum QueFaireDeLObjet_Values {
    ENVOIE_SMS,
    STOCKAGE_SD,
    ENVOIE_BLUETOOTH,
    ENVOIE_WIFI,
    NB_CLIENTS_PHOTO
};

#define COEUR_RESEAU_0     0
#define COEUR_TRAITEMENT_1 1
#define PRIORITE_1 1
#define PRIORITE_2 2
#define PRIORITE_3 3

// -->> Variables globales  ***********************************************
extern HardwareSerial modemSerial;	// On réutilise l'UART 1 partagé pour communiquer avec le modem.

// -->> Fonctions spécifiques utilisables partout.  ***********************************************


// -------------------------------------------------------------------------------------- //
// ---------------------- Déclarations des outils Free RTOS. ---------------------------- //
// -------------------------------------------------------------------------------------- //
// -->> Mutex permet de bloquer la lecture de la donnée pendant sa mise à jour  ***********************************************

// -->> Queue permet d'attendre la prochaine mis à jour  ***********************************************

// -->> Déclaration des tâches FreeRTOS  ***********************************************


#endif // CONFIG_MAIN_H


/*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-

struct Gyroscope {
    int Angle_Roll_Roulis__lateral;
	int Angle_Pitch_Tangage__assiette;
	int Angle_Yaw_Lacet__direction;
	float temperature;

*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/