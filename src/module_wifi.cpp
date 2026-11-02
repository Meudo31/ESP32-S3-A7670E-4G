#include "module_Wifi.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "SimpleFTPServer.h" // Inclusion du serveur FTP léger
#include <SD.h>

#include "module_batterie.h"
#include "module_DateHeure.h"
#include "module_gps.h"
#include "module_SD.h"
#include "module_sms.h"
#include "module_gyroscope.h"
#include "module_wifi.h"
#include "module_logs.h"
#include "secrets.h"   // SSID / mot de passe Wi-Fi (fichier privé)


/* Explications du stockage de la page web dans la mémoire flash
Le mot-clé PROGMEM (qui signifie Program Memory) est une directive très importante en informatique embarquée. 
Elle indique au compilateur d'enregistrer une donnée directement dans la mémoire Flash (la mémoire programme) de l'ESP32-S3, 
plutôt que de la charger dans la mémoire RAM (la mémoire de travail).
Voici pourquoi et comment cela fonctionne :
1. 	La différence entre RAM et FlashPour comprendre l'intérêt de PROGMEM, 
	il faut voir l'ESP32-S3 comme un ordinateur avec deux disques distincts :
		- La mémoire Flash (généralement 16 Mo) : C'est un grand placard. 
		C'est là qu'est stocké votre code compilé. 
		Elle est permanente et ne s'efface pas quand on coupe le courant.
		- La mémoire RAM (généralement 512 Ko) : 
		C'est votre bureau de travail. 
		Elle est petite mais ultra-rapide. 
		Par défaut, dès que vous créez une variable ou du texte (comme votre page web HTML), 
		l'ESP32 la copie de la Flash vers la RAM au démarrage pour pouvoir la manipuler.
2.	Le problème avec les pages WebVotre page HTML (index_html) contient des centaines de lignes de texte.
	Si vous ne mettez pas PROGMEM, ce texte va s'installer en RAM et occuper une place immense. 
	Il vous restera alors très peu de RAM pour exécuter les tâches lourdes comme le traitement des photos de la caméra ou les files d'attente FreeRTOS.
	En ajoutant PROGMEM, vous dites à l'ESP32 : "Laisse ce gros bloc de texte tranquille dans le grand placard (la Flash). 
		Ne le copie pas en RAM au démarrage."
3. 	Comment le serveur l'utilise ?
	Dans le code du serveur Wi-Fi, vous avez vu cette ligne :cpprequest->send_P(200, "text/html", index_html);
	Le _P à la fin de send_P signifie Flash/Progmem. 
	Cela indique au serveur Web : "Va lire la page HTML directement depuis la mémoire Flash octet par octet et envoie-la sur le Wi-Fi, sans jamais la charger en RAM". 
	Votre RAM reste ainsi totalement libre (0% d'occupation pour stocker le HTML).
⚠️ Une spécificité de l'ESP32-S3 (Pour aller plus loin)Sur les anciens Arduino (comme l'Arduino Uno), oublier PROGMEM faisait planter la carte instantanément par manque de RAM.
Sur l'ESP32-S3, l'architecture moderne utilise un mécanisme appelé MMU (Memory Management Unit). 
Les variables déclarées avec const sont souvent optimisées automatiquement en Flash. 
Cependant, utiliser explicitement le mot-clé PROGMEM (ou l'attribut const char variable[]) reste la méthode standard et sécurisée pour garantir la compatibilité et forcer le stockage des gros fichiers (HTML, images, polices de caractères) dans la mémoire Flash.*/

/*📂 Comment s'y connecter depuis votre iPhone ou Android ?
Connectez votre smartphone au réseau Wi-Fi de la carte : Moto_ESP32 (Mot de passe : voir include/secrets.h).
Ouvrez l'application d'exploration réseau de votre choix sur votre téléphone.
Ajoutez un nouveau stockage de type FTP en saisissant les paramètres réseau suivants :
    Serveur / Hôte (Host) : 192.168.4.1
    Port : 21 (Port FTP par défaut)
    Identifiant (User) : adminMot de passe (Password) : admin
    Une fois validé, l'arborescence complète de votre carte SD s'affiche à l'écran sous forme de dossiers systèmes.
Vous pouvez directement parcourir vos répertoires journaliers, ouvrir ou télécharger vos photos instantanément*/

// Configuration du réseau Wi-Fi de l'ESP32 (Mode Point d'Accès local)
const char* ssid     = SECRET_WIFI_SSID;       // défini dans include/secrets.h (non publié)
const char* password = SECRET_WIFI_PASSWORD;

AsyncWebServer server(80);
FtpServer ftp; // Instance du serveur FTP


// --- PAGE WEB PRINCIPALE (HTML/CSS/JS) ---
const char index_html[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <title>Dashboard Moto ESP32</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f4f7f6; color: #333; margin: 0; padding: 20px; }
        .container { max-width: 1000px; margin: auto; }
        h1 { text-align: center; color: #2c3e50; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 20px; margin-bottom: 20px; }
        .card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
        .card h2 { margin-top: 0; color: #34495e; border-bottom: 2px solid #ecf0f1; padding-bottom: 10px; }
        .value { font-size: 1.2rem; font-weight: bold; color: #2980b9; }
        #map-container { width: 100%; height: 350px; border-radius: 10px; overflow: hidden; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
        iframe { width: 100%; height: 100%; border: none; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📊 ESP32-S3 Système & Capteurs</h1>
        
        <div class="grid">
            <div class="card">
                <h2>🔋 Énergie & Système</h2>
                <p>Batterie : <span id="bat" class="value">--</span> %</p>
                <p>Date/Heure : <span id="sys_time" class="value">--</span></p>
            </div>
            <div class="card">
                <h2>🛰️ Navigation (GPS)</h2>
                <p>Vitesse : <span id="gps_speed" class="value">--</span> km/h</p>
                <p>Altitude : <span id="gps_alt" class="value">--</span> m</p>
                <p>Direction : <span id="gps_dir" class="value">--</span></p>
            </div>
            <div class="card">
                <h2>📈 Dynamique & G-Force</h2>
                <p>Force G : <span id="gyro_g" class="value">--</span> G</p>
                <p>Pitch : <span id="gyro_p" class="value">--</span>°</p>
                <p>Roll : <span id="gyro_r" class="value">--</span>°</p>
            </div>
            <div class="card">
                <h2>💾 Stockage & Télécom</h2>
                <p>Occupation SD : <span id="sd_perc" class="value">--</span> %</p>
                <p>Signal SIM : <span id="sim_sig" class="value">--</span> %</p>
                <p>Data Restante : <span id="sim_data" class="value">--</span> %</p>
            </div>
        </div>

        <div id="map-container">
            <iframe id="google-map" src=""></iframe>
        </div>
    </div>

    <script>
        let lastLat = 0, lastLon = 0;

        function updateDashboard() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('bat').innerText 		= data.bat;
                    document.getElementById('sys_time').innerText 	= data.time;
                    document.getElementById('gps_speed').innerText 	= data.gps_speed;
                    document.getElementById('gps_alt').innerText 	= data.gps_alt;
                    document.getElementById('gps_dir').innerText 	= data.gps_dir;
                    document.getElementById('gyro_g').innerText 	= data.gyro_g;
                    document.getElementById('gyro_p').innerText 	= data.gyro_p;
                    document.getElementById('gyro_r').innerText 	= data.gyro_r;
                    document.getElementById('sd_perc').innerText 	= data.sd_perc;
                    document.getElementById('sim_sig').innerText 	= data.sim_sig;
                    document.getElementById('sim_data').innerText 	= data.sim_data;

                    // Mise à jour de la carte si la position change
                    if(data.lat !== lastLat || data.lon !== lastLon) {
                        lastLat = data.lat;
                        lastLon = data.lon;
                        document.getElementById('google-map').src = `https://google.com{data.lat},${data.lon}&t=&z=15&ie=UTF8&iwloc=&output=embed`;
                    }
                });
        }
        setInterval(updateDashboard, 1000); // Mise à jour asynchrone toutes les secondes
        updateDashboard();
    </script>
</body>
</html>
)rawhtml";

bool initialiserWifi() {
    my_log_printf("🌐 [WIFI] Configuration du Point d'Accès...");
    WiFi.mode(WIFI_AP);
    
    if (WiFi.softAP(ssid, password)) {
        my_log_printf("✅ [WIFI] Point d'accès actif. IP : %s\n", WiFi.softAPIP().toString().c_str());       
        return true;
    }
    my_log_printf("❌ [WIFI_ERR] Impossible de lancer le point d'accès !");
    return false;
}

void tache_ServeurWeb(void * pvParameters) {
    my_log_printf("🌐 [WIFI_TASK] Démarrage de la tâche HTTP.");

    // Route principale : Envoi du squelette HTML
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    // Route d'API /data : Génère le JSON à la volée à partir de vos structures globales
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        json += "\"bat\":" 			+ String(jaugeBatterie.cellPercent(), 1) + ",";
        json += "\"time\":\"" 		+ Date_formatteeFR() + "\",";
        json += "\"lat\":" 			+ String(GPS_Data.latitude, 6) + ",";
        json += "\"lon\":" 			+ String(GPS_Data.longitude, 6) + ",";
        json += "\"gps_speed\":" 	+ String(GPS_Data.vitesse, 1) + ",";
        json += "\"gps_alt\":" 		+ String(GPS_Data.altitude, 1) + ",";
        json += "\"gps_dir\":\"" 	+ String(GPS_Data.Direction.degres, 1) + "° " + GPS_Data.Direction.NSEO + "\",";
        json += "\"gyro_g\":"   	+ String(lastGyroscopeData.forceGTotal, 2) + ",";
        json += "\"gyro_p\":"   	+ String(lastGyroscopeData.pitch, 1) + ",";
        json += "\"gyro_r\":"   	+ String(lastGyroscopeData.roll, 1) + ",";
        json += "\"sd_perc\":"  	+ String(volumeActuelCarteSD.usedPercentage, 1) + ",";
        json += "\"sim_sig\":"  	+ String(SIM_data.SIM_qualite_signal) + ",";
        json += "\"sim_data\":" 	+ String(SIM_data.pourcentageDataRestant);
        json += "}";
        
        request->send(200, "application/json", json);
    });

    // Lancement physique du serveur
    server.begin();

    // --- CONFIGURATION DU SERVEUR FTP ---
    ftp.addUser("admin", "admin"); // Identifiants de connexion pour votre téléphone
    ftp.addFilesystem("SD", &SD);   // Lie le FTP à la racine de la bibliothèque SD.h
    ftp.begin();                    // Démarre le protocole d'écoute FTP

    while(1) {
        // Le serveur FTP léger nécessite d'être ausculté à intervalles très réguliers
        // pour traiter les requêtes de fichiers entrantes/sortantes sans bloquer l'ESP32.
        ftp.handleFTP();

        // Un délai minimal de 5 millisecondes donne la réactivité parfaite pour les transferts
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
}
