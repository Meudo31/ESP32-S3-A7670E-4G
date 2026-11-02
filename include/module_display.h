#ifndef CONFIG_DISPLAY_H
#define CONFIG_DISPLAY_H


/*inibuild_flags = 
    -D USER_SETUP_LOADED=1   ; Force la bibliothèque à ignorer ses fichiers par défaut
    -D GC9A01_DRIVER=1       ; Active le pilote spécifique de votre écran
    -D TFT_WIDTH=240         ; Largeur en pixels
    -D TFT_HEIGHT=240        ; Hauteur en pixels
    
    ; --- CONFIGURATION DES BROCHES PHYSIQUES DU BUS SPI ---
    -D TFT_MOSI=11           ; Données (SDA) -> Connecté au GPIO 11
    -D TFT_SCLK=12           ; Horloge (SCL) -> Connecté au GPIO 12
    -D TFT_CS=15             ; Sélection de l'esclave (CS) -> Connecté au GPIO 14
    -D TFT_DC=9              ; Commande/Donnée (DC) -> Connecté au GPIO 9
    -D TFT_RST=10            ; Réinitialisation (RES) -> Connecté au GPIO 10
    -D TFT_MISO=-1           ; Désactivé (-1) car l'écran n'envoie rien à l'ESP32
Pourquoi ce choix de broches est-ce une astuce d'architecture ?Isolation complète (Zéro Conflit) : 
L'ESP32-S3 possède plusieurs contrôleurs SPI internes. 
En choisissant les GPIO 11, 12, 14, nous utilisons le bus matériel SPI2 (FSPI). 
Cela isole complètement le flux graphique ultra-rapide de l'écran par rapport au bus SPI principal qui gère votre carte SD en tâche de fond, 
évitant ainsi les collisions de données ou les ralentissements visuels.
Le cas de la broche MISO à -1 : 
	Dans un bus SPI classique, la broche MISO (Master In Slave Out) sert au périphérique pour répondre au microcontrôleur. 
	Comme un écran ne fait que recevoir des paquets de pixels pour les afficher et n'a jamais besoin de parler à l'ESP32, 
	nous avons configuré cette broche à -1 (désactivée). 
	Cela vous permet de gagner une broche GPIO précieuse pour le reste de votre moto.
Le rôle secret des broches de contrôle (DC et RST) : 
	Les broches GPIO 9 (DC) et GPIO 10 (RES) ne font pas partie du protocole SPI strict, mais sont indispensables pour le contrôleur GC9A01. 
	RST réinitialise la puce de l'écran à l'allumage, et 
	DC sert de commutateur ultra-rapide : 
		quand elle est à 0V, l'ESP32 envoie une commande (ex: "change la luminosité"), 
		quand elle est à 3.3V, l'ESP32 envoie des données brutes (les couleurs des pixels de votre compteur).*/


extern bool initialiserEcran();
extern void tacheGestionEcran(void * pvParameters);

#endif