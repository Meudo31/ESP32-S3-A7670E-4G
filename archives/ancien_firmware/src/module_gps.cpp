#include <HardwareSerial.h>

#include "module_gps.h"
#include "module_DateHeure.h"
#include "module_SD.h"


/* Format de la trame GPS
    // +CGNSSINFO: 3,1,4849.1234,N,00204.5678,E,270826,101500.0,142.5,28.5,180.0,1.0,0.8,0.5,14
    champs[1]     = "1"         ──> Le FIX est bon.
    champs[2],[3] = 4849.1234,N ──> converti par notre fonction donne 48.818723 (Latitude décimale).
    champs[4],[5] = 00204.5678,E ──>converti par notre fonction donne 2.076130 (Longitude décimale).
    champs[6]     = "270826"    ──> Mis en forme sous la forme 27/08/2026 (Date).
    champs[7]     = "101500.0"  ──> Mis en forme sous la forme 10:15:00 (Heure UTC).
    champs[9]     = "28.5"      ──> 28.5 * 1.852 = la moto se déplace à 52.7 km/h.
    champs[10]    = "180.0"     ──> La moto se déplace plein Sud ⬇️.
    champs[11]    = "1.0"       ──> [PDOP] (Position Dilution of Precision) C est l indicateur global le plus important. Il combine les précisions horizontale et verticale. Plus les satellites sont répartis uniformément partout dans le ciel, plus ce chiffre est bas, et plus votre position est exacte.
    champs[11]    = "0.8"       ──> [HDOP] (Horizontal Dilution of Precision) Pour un traceur de moto, c est le facteur clé. Il indique la précision du point sur une carte 2D (au sol). Si le HDOP est bas, le point bleu sur Google Maps sera parfaitement fixe sur la route. S il est élevé, le point va sauter d une rue à l autre de manière aberrante.
    champs[11]    = "0.5"       ──> [VDOP] (Vertical Dilution of Precision) Il indique la précision de la hauteur de la moto par rapport au niveau de la mer. En raison de la position des satellites (toujours au-dessus de nous et jamais sous la Terre), le VDOP est mathématiquement toujours moins bon que le HDOP.
    champs[11]    = "14"        ──> Nombre de satellites actuellement verrouillés. Il faut un minimum strict de 3 satellites pour un calcul 2D (sans altitude). Il faut un minimum de 4 satellites pour un calcul 3D (avec altitude). Sur votre moto en extérieur avec l antenne configurée en mode multi-constellations (AT+CGNSSMODE=7), ce chiffre oscillera généralement de manière excellente entre 10 et 22 satellites.*/

		
/* Grille d évaluation des scores DOP (PDOP, HDOP, VDOP)
    Pour interpréter ces trois chiffres dans votre code d'alarme, vous devez suivre cette échelle de notation :
    Inférieur à 1.0 :  🏆 (Scénario de laboratoire ou ciel parfait).
    Entre 1.0 et 2.0 : 🍏 (C'est ce que vous obtiendrez sur la route en rase campagne).
    Entre 2.0 et 5.0 : 🌤️ (La position est tout à fait fiable pour retrouver la moto).
    Supérieur à 5.0 :  ⚠️ (La moto est probablement entourée de grands immeubles "canyon urbain" ou sous des arbres très denses. Le point de géolocalisation va manquer de précision).
    Supérieur à 20.0 : ❌ (Le calcul comporte des centaines de mètres d'erreur).*/


/*  PDOP = 1.0 ──> 100 % Idéal dehors               (Précision maximale au centimètre près)
    PDOP = 2.0 ──> 80 %  Excellent roulage          (Précision excellente, inférieure à 2 mètres)
    PDOP = 3.5 ──> 50 %  En ville (Canyon urbain)   (Précision moyenne, environ 5 à 8 mètres)
    PDOP = 5.0 ──> 20 %  Sous des arbres denses     (Précision faible, le point bleu peut sauter de quelques dizaines de mètres)
    PDOP => 6.0 ──> 0 %  Dans un garage             (Signal trop bruité pour être fiable)*/

const String pointsCardinaux[] = { // Un tableau contenant nos 8 directions dans l'ordre des aiguilles d'une montre
"Nord ⬆️", "Nord-Est ↗️", "Est ➡️", "Sud-Est ↘️", "Sud ⬇️", "Sud-Ouest ↙️", "Ouest ⬅️", "Nord-Ouest ↖️"};
// "N", "NE", "E", "SE", "S", "SO", "O", "NO"};

const int PDOP_EXCELLENT  = 1; // Équivaut à 100%
const int PDOP_MAUVAIS    = 6; // Équivaut à 0%

CoordonneesGPS  GPS_Data;
TaskHandle_t xTacheDonneesGPS_SMS_Handle   = NULL;
TaskHandle_t xTacheDonneesGPS_SD_Handle    = NULL;
TaskHandle_t xTacheDonneesGPS_BT_Handle    = NULL;
TaskHandle_t xTacheDonneesGPS_Wifi_Handle  = NULL;

TaskHandle_t xTacheGPSHandle = NULL;


float convertion_DegreMinutes_Decimal(String coord, String direction) {
    if (coord.length() == 0) {
        my_log_printf("[🛰️ Cœur 1] Erreur lors de la récupération de la trame. La latitude ou la longitude est vide.");
        return 0.0;
    }

    int pointIdx      = coord.indexOf('.');
    String degresStr  = coord.substring(0, pointIdx - 2);
    String minutesStr = coord.substring(pointIdx - 2);
    
    float decimal = degresStr.toFloat() + (minutesStr.toFloat() / 60.0);
    
    if (direction == "S" || direction == "W") decimal = -decimal;

    return decimal;
}

void taskGPSUpdateAllData(void * pvParameters) {  // Déclenché par xTacheGPSHandle
    for(;;) {
        my_log_printf("[🛰️ Cœur 1] Activation électrique du récepteur GPS...");
        modemSerial.println("AT+CGNSSMODE=7"); vTaskDelay(pdMS_TO_TICKS(200)); // GPS + Galileo + GLONASS
        modemSerial.println("AT+CGNSSPWR=1");  vTaskDelay(pdMS_TO_TICKS(500));

        QueFaireDeLObjet_Values modeChoisi;
        uint32_t valeurNotification;


       if (xTaskNotifyWait(0, 0, &valeurNotification, portMAX_DELAY) == pdTRUE) {
            modeChoisi = static_cast<QueFaireDeLObjet_Values>(valeurNotification);

            my_log_printf("\n[🛰️ Cœur 1] Demande de position au module GNSS (AT+CGNSSINFO)...");
            
            // Nettoyage du tampon série avant d'écrire
            while(modemSerial.available()) { modemSerial.read(); }

            modemSerial.println("AT+CGNSSINFO");

            String reponseModem = "";
            long timeout = millis();
            while ((millis() - timeout) < 2000) {
                while (modemSerial.available()) {
                    reponseModem += (char)modemSerial.read();
                }
            }

            if (reponseModem.indexOf("+CGNSSINFO:") != -1) {
                String latitude, longitude, NS, EO;
                struct tm tempsCalendrier;

                int indexDebut = reponseModem.indexOf("+CGNSSINFO:");
                if (indexDebut == -1) {
                    my_log_printf("[🛰️ Cœur 1] Erreur lors de la récupération de la trame. Elle pourrait être vide.");
                    return;
                }

                String donneesBrutes = reponseModem.substring(indexDebut + 11);
                donneesBrutes.trim();

                //    0      1       2       3        4         5     6      7        8        9       10      11     12     13        14           1 
                // [Mode],[Fix],[Latitude],[N/S],[Longitude],[E/W],[Date],[Time],[Altitude],[Speed],[Course],[PDOP],[HDOP],[VDOP],[Satellites]
                // %d,    %d,   %s,        %s,   %s,         %s,   %s,    %s,    %f,        %f,     %f,      %f,    %f,    %f,    %d
                // +CGNSSINFO: 3,1,4849.1234,N,00204.5678,E,260826,141522.0,125.3,0.0,0.0,1.2,1.0,0.8,12
                sscanf(donneesBrutes.c_str(), "%d,%d,%s,%c,%s,%c,%s,%s,%f,%f,%f,%f,%f,%f,%d", 
                    /*  0 */ &GPS_Data.source, 
                    /*  1 */ &GPS_Data.fixValide, 
                    /*  2 */ &latitude, // post-traitement
                    /*  3 */ &NS,       // post-traitement
                    /*  4 */ &longitude,// post-traitement
                    /*  5 */ &EO,       // post-traitement
                    /*  6 */ &GPS_Data.date, 
                    /*  7 */ &GPS_Data.heure, 
                    /*  8 */ &GPS_Data.altitude, 
                    /*  9 */ &GPS_Data.vitesse, // post-traitement
                    /* 10 */ &GPS_Data.Direction.degres, 
                    /* 11 */ &GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision, 
                    /* 12 */ &GPS_Data.Precision_Position.HDOP_Horizontal_Dilution_Of_Precision, 
                    /* 13 */ &GPS_Data.Precision_Position.VDOP_Vertical_Dilution_Of_Precision, 
                    /* 14 */ &GPS_Data.NbSatelites);

                GPS_Data.latitude           = convertion_DegreMinutes_Decimal(latitude,  NS);
                GPS_Data.longitude          = convertion_DegreMinutes_Decimal(longitude, EO);
                GPS_Data.vitesse            = GPS_Data.vitesse * 1.852; // Nœuds vers km/h
                
                /*  Formule mathématique :
                    1. On ajoute 22.5 pour décaler le Nord (qui chevauche 0°) au début du calcul.
                    2. On divise par 45 (car chaque secteur fait 45°).
                    3. L'opérateur '%' (modulo 8) s'assure que si on obtient 8, on retombe bien sur 0 (le Nord).*/
                GPS_Data.Direction.NSEO = pointsCardinaux[(int)((GPS_Data.Direction.degres + 22.5) / 45.0) % 8];

                tempsCalendrier.tm_year  = ("20" + GPS_Data.date.substring(4, 6)).toInt() - 1900;     // Années depuis 1900 (Reconstruit "2026") 
                tempsCalendrier.tm_mon   = GPS_Data.date.substring(2, 4).toInt() - 1;        // Mois de 0 à 11
                tempsCalendrier.tm_mday  = GPS_Data.date.substring(0, 2).toInt();
                tempsCalendrier.tm_hour  = GPS_Data.date.substring(0, 2).toInt();
                tempsCalendrier.tm_min   = GPS_Data.date.substring(2, 4).toInt();
                tempsCalendrier.tm_sec   = GPS_Data.date.substring(4, 6).toInt();
                tempsCalendrier.tm_isdst = -1;  // Laisse l'OS gérer l'heure d'été

                // Mise à jour de la date Heure Système de l'ESP32-S3 avec le timestamp UNIX calculé à partir de la date et l'heure GPS
                time_t timestampUnix = mktime(&tempsCalendrier);    // Calcul mathématique du nombre de secondes depuis 1970
                if (timestampUnix != -1) { // Écrit directement le timestamp dans le contrôleur RTC de l'ESP32-S3
                    struct timeval tv;
                    tv.tv_sec = timestampUnix;
                    tv.tv_usec = 0;
                    
                    if (settimeofday(&tv, NULL) == 0) {  
                        my_log_printf("[⏰ RTC] Horloge ESP32 mise à jour avec succès ! Timestamp : %ld\n", timestampUnix);
                        Date_Heure_Source_MaJ = DH_GPS;
                    }
                } else {my_log_printf("[⏰ RTC] ❌ Erreur : Impossible de calculer le timestamp UNIX.");}

                GPS_Data.MaJ_TimeStamp      = time(NULL);  // Pour savoir à quelle heure les données ont été mises à jour

                GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision = 100.0 * (PDOP_MAUVAIS - GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision) / (PDOP_MAUVAIS - PDOP_EXCELLENT);
                if (GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision <= 0.0)            GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision =   0; // Sécurité 1 : Si le GPS n'a pas de FIX, le PDOP vaut souvent 0.0 ou est vide
                if (GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision <= PDOP_EXCELLENT) GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision = 100; // Sécurité 2 : Si le signal est exceptionnel (inférieur à 1.0), on bloque à 100%
                if (GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision >= PDOP_MAUVAIS)   GPS_Data.Precision_Position.PDOP_Position_Dilution_Of_Precision =   0; // Sécurité 3 : Si le signal est trop dégradé (supérieur à 6.0), on bloque à 0%
                
                my_log_printf("[🛰️ Cœur 1] Données prêtes -> Lat: %f | Lon: %f | Direction | Vitesse: %.1f km/h\n", 
                            GPS_Data.latitude, GPS_Data.longitude, GPS_Data.Direction.NSEO, GPS_Data.vitesse);

                switch (modeChoisi){
                    case QueFaireDeLObjet_Values::ENVOIE_SMS:
                        my_log_printf("[🛰️ GPS Cœur 1] Données GPS à envoyée par SMS");
                        xTaskNotifyGive(xTacheDonneesGPS_SMS_Handle);
                    break;
                    
                    case QueFaireDeLObjet_Values::STOCKAGE_SD:
                        my_log_printf("[🛰️ GPS Cœur 1] Données GPS à stockée dans la carte SD");
                        xTaskNotifyGive(xTacheDonneesGPS_SD_Handle);
                        break;    

                    case QueFaireDeLObjet_Values::ENVOIE_BLUETOOTH:
                        my_log_printf("[🛰️ GPS Cœur 1] Données GPS à envoyée par Bluetooth");
                        xTaskNotifyGive(xTacheDonneesGPS_BT_Handle);
                        break;
                    
                    case QueFaireDeLObjet_Values::ENVOIE_WIFI:
                        my_log_printf("[🛰️ GPS Cœur 1] Données GPS à envoyée par WiFi");
                        xTaskNotifyGive(xTacheDonneesGPS_Wifi_Handle);
                        break;  

                    default: my_log_printf("[🛰️ GPS Cœur 1] La valeur %d de valeurNotification est inconnue.\n", modeChoisi); break;
                }
            }
        }

        // vTaskDelay(xDelai_UneMinute);
    }
}






void taskStockageDonneesGPSSD(void * pvParameters) {  // Déchenché par xTacheDonneesGPS_SD_Handle
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);        // La tâche s'endort ici (0% CPU) en attendant le signal
        my_log_printf("[🛰️ GPS Cœur 1] Écriture des coordonnées GPS sur la carte SD...");
        String donneesGPS = GPS_Data.date + String(" ") + GPS_Data.heure +
                            String(", ") +   String(GPS_Data.latitude,  6) + 
                            String(",") +    String(GPS_Data.longitude, 6) + 
                            String(", ") +   GPS_Data.Direction.NSEO + 
                            String(", ") +   String(GPS_Data.vitesse,  1) + String(" km/h") +
                            String(", ") +   String(GPS_Data.altitude, 1) + String(" m");

        envoyerAuStockage(TypeDonneeSD::GPS, donneesGPS);
    }
}

void taskEnvoiDonneesGPSBluetooth(void * pvParameters) { // Déclenché par xTacheDonneesGPS_BT_Handle
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);        // La tâche s'endort ici (0% CPU) en attendant le signal
        my_log_printf("[🛰️ GPS Cœur 1] Téléversement via Bluetooth...");
        
    }
}

void taskEnvoiDonneesGPSWifi(void * pvParameters) {  // Déclenché par xTacheDonneesGPS_Wifi_Handle
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);        // La tâche s'endort ici (0% CPU) en attendant le signal
        my_log_printf("[🛰️ GPS Cœur 1] Téléversement via Wi-Fi...");
        
    }
}