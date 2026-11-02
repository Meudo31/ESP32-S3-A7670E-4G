#include <HardwareSerial.h>
#include "esp_camera.h"

#include "module_sms.h"
#include "module_DateHeure.h"
#include "module_camera.h"
#include "module_gps.h"
#include "module_appel.h"
#include "secrets.h"   // numéros de téléphone autorisés (fichier privé)

#define NB_NO_TEL_REPERTOIRE 10

enum Operateurs { ORANGE, SFR, BOUYGUES, FREE, NB_OPERATEURS };         // NB_OPERATEURS est utilisé pour connaître le nombre d'opérateurs définis dans la structure Config_operateurs
enum Actions    { A_PHOTO, A_GPS, A_STATUS, A_REBOOT, NB_ACTIONS };     // NB_ACTIONS est utilisé pour connaître le nombre d'actions définies dans le tableau NomsActions
enum PersonnesDuRepertoire { SIM=0, MOI=1, NATH=2 };                    // limité à NB_NO_TEL_REPERTOIRE

const Operateurs monOperateur = BOUYGUES; // A modifier selon l'opérateur utilisé
const String NomsActions[NB_ACTIONS] = { "Photo", "GPS", "Status", "Reboot"};

struct elementRepertoire {
    String Nom;
    const char* numero;
} listeNumerosTelephone[NB_NO_TEL_REPERTOIRE] = {
    {"SIM",  SECRET_TEL_SIM},
    {"Moi",  SECRET_TEL_MOI},
    {"Nath", SECRET_TEL_NATH}
 };

// Compare deux numéros sur leurs 9 derniers chiffres (indifférent à +33 / 0 / espaces)
static bool memeNumero(const char* a, const char* b) {
    if (a == NULL || b == NULL) return false;
    char da[32], db[32];
    int na = 0, nb = 0;
    for (const char* p = a; *p && na < 31; p++) if (*p >= '0' && *p <= '9') da[na++] = *p;
    for (const char* p = b; *p && nb < 31; p++) if (*p >= '0' && *p <= '9') db[nb++] = *p;
    if (na < 9 || nb < 9) return false;
    return strncmp(da + na - 9, db + nb - 9, 9) == 0;
}

int indiceRepertoire(const char* numero) {
    for (int i = 0; i < NB_NO_TEL_REPERTOIRE; i++) {
        if (listeNumerosTelephone[i].numero != NULL && memeNumero(numero, listeNumerosTelephone[i].numero)) return i;
    }
    return -1;
}

// Qui peut déclencher un décrochage automatique : moi et Nath (pas la carte SIM elle-même)
bool numeroAutorisePourAppel(const char* numero) {
    int i = indiceRepertoire(numero);
    return (i == MOI || i == NATH);
}

// ─── CONFIGURATION OPÉRATEUR ───
struct Config_operateurs{  // A initialiser
    String NomOperateur;
    String APN;
    String MMSC;
    String Proxy;
    String Port;
    int DernierJourFacture;
    long long LimiteForfaitBytes;
} SIM_configsOperateurs[NB_OPERATEURS] = {
    {"Orange/Sosh",     "orange.mms",      "http://orange.fr", "192.168.10.200", "8080", 0, 0},
    {"SFR",             "mmssfr",          "http://mms1",      "10.151.0.1",     "8080", 0, 0},
    {"Bouygues Télécom","mmsbouygtel.com", "http://bouyguestelecom.fr", "",      "",    19, 1073741824ULL}, // 1 Go = 1024 * 1024 * 1024 octets
    {"Free Mobile",     "mms",             "http://free.fr",            "",      "",     0, 0}
};


RTC_DATA_ATTR SIM_struct SIM_data; //RTC_DATA_ATTR pour que cette variable soit stockée en ROM


void miseAJour_ConsommationData4G(){
    // 1. GESTION DE LA RÉINITIALISATION LE 19 DE CHAQUE MOIS
    if (    JourDuMoisCourrant() >= SIM_configsOperateurs[monOperateur].DernierJourFacture   
       &&   MoisCourrant()       != SIM_data.dernierMoisReinitialise) {
        my_log_printf("[📊 DATA] Jour de renouvellement du forfait (le %d) ! Volume data consomée %dMo ! Réinitialisation du compteur.\n", 
                            SIM_configsOperateurs[monOperateur].DernierJourFacture, 
                            SIM_data.dataConsommeeCeMoisBytes/1024.0);
        SIM_data.dataConsommeeCeMoisBytes = 0;
        SIM_data.dernierMoisReinitialise = MoisCourrant();
    }

    // 2. INTERROGATION DU COMPTEUR DU MODEM 4G (Réponse attendue : +GDCNT: [Octets_Envoyés],[Octets_Reçus])
    modemSerial.println("AT+GDCNT?");
    
    String reponse = "";
    long timeout = millis();
    while ((millis() - timeout) < 15000) {
        while (modemSerial.available()) {reponse += (char)modemSerial.read();}
    }

    int idx = reponse.indexOf("+GDCNT:");
    if (idx != -1) {
        String donnees = reponse.substring(idx + 7);
        donnees.trim();

        // Séparation des octets envoyés et reçus
        int virgule = donnees.indexOf(',');
        if (virgule != -1) {
            SIM_data.dataRecuesCeMoisBytes    += donnees.substring(0, virgule).toInt();
            SIM_data.dataEnvoyeeCeMoisBytes   += donnees.substring(virgule + 1).toInt();
            SIM_data.dataConsommeeCeMoisBytes += (SIM_data.dataRecuesCeMoisBytes + SIM_data.dataEnvoyeeCeMoisBytes);

            modemSerial.println("AT+GDCNT=0");  // Réinitialisation du compteur interne du modem pour la prochaine fois afin de ne pas compter deux fois les mêmes octets
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    // 3. CALCUL DU POURCENTAGE ET AFFICHAGE
    SIM_data.pourcentageDataRestant = (SIM_data.dataConsommeeCeMoisBytes / SIM_configsOperateurs[monOperateur].LimiteForfaitBytes) * 100;
    SIM_data.dateMaJ                = Heure_RTC();

    if (SIM_data.pourcentageDataRestant > 90.0) {my_log_printf("⚠️ ALERTE : Vous avez consommé plus de 90% de votre forfait 1 Go !\n", SIM_configsOperateurs[monOperateur].LimiteForfaitBytes/(1024.0 * 1024.0));}
}

bool initialisationModem4G() {
    modemSerial.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    
    my_log_printf("[⚙️ SETUP] Envoi de l'impulsion physique PWRKEY au A7670E...");
    pinMode(MODEM_PWRKEY_PIN, OUTPUT);
    digitalWrite(MODEM_PWRKEY_PIN, LOW);   vTaskDelay(pdMS_TO_TICKS(100)); 
    digitalWrite(MODEM_PWRKEY_PIN, HIGH);  vTaskDelay(pdMS_TO_TICKS(1000)); // Maintien 1s
    digitalWrite(MODEM_PWRKEY_PIN, LOW);   vTaskDelay(pdMS_TO_TICKS(4000)); // Attente boot

    modemSerial.println("AT+CMGF=1"); // Prépare le modem à envoyer du texte
    vTaskDelay(pdMS_TO_TICKS(200));

    return true; /*-*-*-*-*-*-* Que faire quand ça plante ???? *-*-*-*-*-*-*-*-*-*-*-*-*-*/
}

void taskReceptionSMS(void * pvParameters) {  // Déclenché par modemSerial.available() dans la fonction
    my_log_printf("[📩 Cœur 0] Tâche Réception SMS démarrée sur le cœur : %s ", xPortGetCoreID());

    // Configurer Modem Pour Reception
    my_log_printf("[📩 Cœur 0] Configuration du A7670E pour la réception...");
    modemSerial.println("AT+CMGF=1");           vTaskDelay(pdMS_TO_TICKS(200)); // 1. Mode texte pour les SMS
    modemSerial.println("AT+CSCS=\"GSM\"");     vTaskDelay(pdMS_TO_TICKS(200)); // 2. Mode d'encodage des caractères standard
    modemSerial.println("AT+CNMI=2,2,0,0,0");   vTaskDelay(pdMS_TO_TICKS(500)); // 3. Configuration cruciale des notifications (AT+CNMI) (Paramètre 2 = 2 : Dit au modem de balancer DIRECTEMENT le contenu du SMS sur le port série de l'ESP32 dès qu'il arrive, sans le stocker sur la carte SIM.)
    my_log_printf("[📩 Cœur 0] Modem prêt à recevoir.");


    String tamponSerie = "";

    for(;;) {
        while (modemSerial.available()) {
            char c = modemSerial.read();
            tamponSerie += c;

            // Lignes d'appel (+CLIP, NO CARRIER...) : traitées par module_appel, puis retirées du tampon
            if (c == '\n') traiterLignesAppelModem(tamponSerie);

            // Détection de la notification de réception directe : +CMT:
            // Format attendu du modem : +CMT: "+33612345678","","26/08/28,14:30:00+08"\r\n[Texte du Message]\r\n
            if (tamponSerie.endsWith("\r\n") && tamponSerie.indexOf("+CMT:") != -1) {
                
                // On attend une fraction de seconde pour être sûr que tout le message est dans le tampon
                vTaskDelay(pdMS_TO_TICKS(500));
                while (modemSerial.available()) {tamponSerie += (char)modemSerial.read();}

                my_log_printf("\n[📩 Cœur 0] Nouveau SMS reçu sur le réseau !");
                
                // Isoler la ligne +CMT
                int idxCmt            = tamponSerie.indexOf("+CMT:");
                int premierGuillemet  = tamponSerie.indexOf('"', idxCmt);
                int deuxiemeGuillemet = tamponSerie.indexOf('"', premierGuillemet + 1);
                
                // 1. Extraction du numéro de téléphone de l'expéditeur
                String numeroExpediteur = tamponSerie.substring(premierGuillemet + 1, deuxiemeGuillemet);
                
                // 2. Extraction du corps du message textuel
                // Le message se trouve après la deuxième ligne (\r\n) de la notification +CMT
                int finLigneEntete  = tamponSerie.indexOf("\n", deuxiemeGuillemet);
                String corpsMessage = tamponSerie.substring(finLigneEntete + 1);
                corpsMessage.trim(); // Supprime les espaces et retours à la ligne inutiles

                my_log_printf("[📩 Cœur 0] Expéditeur : %s\n", numeroExpediteur.c_str());
                my_log_printf("[📩 Cœur 0] Contenu    : %s\n", corpsMessage.c_str());

                // 3. Traitement sécurisé de la commande
                // analyserEtExecuterCommande(numeroExpediteur, corpsMessage); //

                    String Numero  = numeroExpediteur.c_str();
                    String Message = corpsMessage.c_str();

                    // Étape de sécurité 1 : On refuse catégoriquement d'écouter les numéros inconnus
                    if (indiceRepertoire(numeroExpediteur.c_str()) != MOI) {
                        my_log_printf("[📩 Cœur 0] 🔒 Commande rejetée : L'expéditeur %s n'est pas autorisé.\n", Numero.c_str());
                        my_log_printf("[📩 Cœur 0] 🔒 Commande rejetée : Voici son message : %s\n",              Message.c_str());
                        tamponSerie = "";
                        continue;       // on reste dans la boucle : une tâche FreeRTOS ne doit jamais faire return
                    }

                    // Étape 2 : Analyse des mots-clés (Commandes)
                    if (Message == NomsActions[Actions::A_PHOTO]) {
                        my_log_printf("[📩 Cœur 0] 🚀 Commande reçue : Demande de photo.");
                        xTaskNotify(xTacheCameraHandle, static_cast<uint32_t>(QueFaireDeLObjet_Values::ENVOIE_SMS), eSetValueWithOverwrite);
                    } 
                    else if (Message == NomsActions[Actions::A_GPS]) {
                        my_log_printf("[📩 Cœur 0] 🚀 Commande reçue : Coordonnées GPS.");
                        // xQueueOverwrite(xQueue_SMS_Send_GPS, &GPS_Data);
                        xTaskNotify(xTacheGPSHandle, static_cast<uint32_t>(QueFaireDeLObjet_Values::ENVOIE_SMS), eSetValueWithOverwrite);
                    } 
                    else if (Message == NomsActions[Actions::A_STATUS]) {
                        my_log_printf("[📩 Cœur 0] 🚀 Commande reçue : Demande de statut.");
                        // *-*-*-*-*-*-*-* Tâche à developper pour renvoyer un SMS avec le statut du système (batterie, GPS, 4G, etc.) *-*-*-*-*-*-*-*
                    } 
                    else if (Message == NomsActions[Actions::A_REBOOT]) {
                        my_log_printf("[📩 Cœur 0] 🚀 Commande reçue : Force Reboot Système !");
                        Serial.flush();
                        ESP.restart(); // Déclenche le redémarrage étudié ensemble
                    } 
                    else {
                        my_log_printf("[📩 Cœur 0] Commande %s inconnue.\n", Message.c_str());
                    }

                // Nettoyage du tampon pour le prochain message
                tamponSerie = "";
            }
        }

        if (tamponSerie.length() > 500) {tamponSerie = "";} // Si le tampon s'emballe ou contient de vieilles réponses AT, on le vide régulièrement
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Laisse souffler le processeur
    }
}



void taskEnvoiPhotoSMS(void * pvParameters) {  //
    PhotoComplete * fbRecu = NULL;
    while(1) {
        if (xQueueReceive(queuePhotoMMS, &fbRecu, portMAX_DELAY) == pdPASS) {
            uint8_t* adresseMemoirePhoto    = fbRecu->buf; // fbRecu->buf = pointeur vers le premier octet de la photo (uint8_t*)
            size_t   tailleExactePhotoBytes = fbRecu->len; // fbRecu->len = taille exacte de l'image compressée en octets (size_t)
            
            
            my_log_printf("[💡 Cœur 1] Réveil ! Envoie d'une photo par MMS");
            my_log_printf("[📟 MMS] Préparation de l'envoi d'une photo de %d octets...\n", tailleExactePhotoBytes);
            
            modemSerial.println("AT+CMMSINIT");                                                            vTaskDelay(pdMS_TO_TICKS(500)); // 1. Initialisation de la pile logicielle MMS du module SIMCom
            modemSerial.printf ("AT+CGDCONT=1,\"IP\",\"%s\"\n", SIM_configsOperateurs[monOperateur].APN);  vTaskDelay(pdMS_TO_TICKS(500)); // 2. Configuration du point d'accès (APN) dédié aux MMS (On utilise le profil de connexion réseau 1)
            modemSerial.printf ("AT+CMMSCURL=\"%s\"\n",         SIM_configsOperateurs[monOperateur].MMSC); vTaskDelay(pdMS_TO_TICKS(200)); // 3. Configuration des paramètres du centre de traitement MMS (MMSC)
            // modemSerial.printf ("AT+CMMSPROXY=\"%s\",%d\n", SIM_configsOperateurs[BOUYGUES].Proxy, SIM_configsOperateurs[BOUYGUES].Port); vTaskDelay(pdMS_TO_TICKS(200)); //    Configuration du Proxy et du Port (Requis pour Orange/SFR, mettez à 0 ou omettez pour Free)
            modemSerial.println("AT+CMMSCID=1");                                                           vTaskDelay(pdMS_TO_TICKS(200)); //    Liaison du MMS au profil de connexion réseau 1
            modemSerial.println("AT+CMMSEDIT=1");                                                          vTaskDelay(pdMS_TO_TICKS(200)); // 4. Entrée en mode édition du Message
            modemSerial.printf ("AT+CMMSDOWN=\"PIC\",%d,15000\n", tailleExactePhotoBytes);                // 5. Transfert de l'image de l'ESP32 vers la mémoire tampon du modem (Paramètres : "PIC" (type de fichier), taille en octets, timeout en ms (ici 15s))
            
            long timeout = millis();
            bool modemPret = false;
            while ((millis() - timeout) < 3000) {
                if (modemSerial.find("CONNECT")) {  // Le module répond "CONNECT" lorsqu'il est prêt à recevoir le flux binaire
                    modemPret = true;
                    break;
                }
            }

            if (modemPret) {
                // Envoi effectif des octets binaires de la photo sur la liaison série
                my_log_printf("[📟 MMS] Envoi du flux binaire de l'image au modem...");
                modemSerial.write(adresseMemoirePhoto, tailleExactePhotoBytes);  vTaskDelay(pdMS_TO_TICKS(1000)); //    Laisse le temps au tampon de se stabiliser
                modemSerial.printf("AT+CMMSRECP=\"%s\"\n", listeNumerosTelephone[MOI].numero);      vTaskDelay(pdMS_TO_TICKS(200));  // 6. Configuration des métadonnées du MMS (Destinataire et Titre)
                modemSerial.println("AT+CMMSEDIT=2");                                               vTaskDelay(pdMS_TO_TICKS(100));  //    Ajout d'un titre textuel
                modemSerial.println("AT+CMMSTITLE=\"Alerte Traceur Moto !\"");                      vTaskDelay(pdMS_TO_TICKS(200));
                
                my_log_printf("[📟 MMS] Envoi du MMS en cours (Cette opération peut prendre 10 à 20 secondes)...");
                modemSerial.println("AT+CMMSSEND");                                              // 7. ENVOI DU MESSAGE SUR LE RÉSEAU CELLULAIRE

                // Attente du rapport de transmission du réseau (Le module doit répondre "+CMMSSEND: 200" en cas de succès)
                timeout = millis();
                bool envoiReussi = false;
                while ((millis() - timeout) < 30000) { // Timeout large de 30 secondes pour l'émission data
                    if (modemSerial.available()) {
                        String reponse = modemSerial.readString();
                        my_log_printf("[Modem] %s", reponse);
                        if (reponse.indexOf("+CMMSSEND: 200") != -1 || reponse.indexOf("OK") != -1) {
                            envoiReussi = true;
                            break;
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                
                modemSerial.println("AT+CMMSTERM"); vTaskDelay(pdMS_TO_TICKS(200)); // 8. Nettoyage obligatoire de la mémoire et fermeture de la session

                if (envoiReussi) {my_log_printf("[📟 MMS] 🏆 Succès ! Le MMS contenant la photo a été transmis.");} 
                else             {my_log_printf("[📟 MMS] ❌ Échec de l'envoi du MMS (Vérifiez votre crédit data ou vos paramètres APN).");}
            }
            else {
                my_log_printf("[📟 MMS] ❌ Erreur : Le modem n'a pas accepté le transfert de l'image.");
                modemSerial.println("AT+CMMSTERM"); // Ferme la session proprement
            }
            
            // LIBÉRATION CRITIQUE : C'est la tâche finale qui nettoie la RAM
            free(fbRecu);  // Libère le Frame Buffer pour que la RAM de l'ESP32-S3 ne sature pas au prochain déclenchement
            my_log_printf("[📷 CAM Cœur 1] Mémoire de la photo libérée. Retour en veille.");
        }
    }
}

void taskEnvoiDonneesGPSSMS(void * pvParameters) {  // Déclenché par xTacheDonneesGPS_Handle
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);    // La tâche s'endort ici (0% CPU) en attendant le signal
        my_log_printf("[💬 Cœur 0] Envoie des coordonnées GPS par SMS...");

        // Commande AT d'ouverture de message
        modemSerial.print("AT+CMGS=\"");
        modemSerial.print(listeNumerosTelephone[MOI].numero);
        modemSerial.println("\"");
        vTaskDelay(pdMS_TO_TICKS(300));

        // Texte du SMS
        modemSerial.print("Traceur Moto - Position : https://www.google.com/maps/@");
        modemSerial.print(GPS_Data.latitude, 6);
        modemSerial.print(",");
        modemSerial.print(GPS_Data.longitude, 6);
        modemSerial.printf("m\n");
        modemSerial.printf("Vitesse : %.1f km/h\n", GPS_Data.vitesse);
        modemSerial.printf("Direction : %s\n",      GPS_Data.Direction.NSEO);
        modemSerial.printf("Mis à jour : %s\n",     GPS_Data.MaJ_TimeStamp);
        vTaskDelay(pdMS_TO_TICKS(100));
        modemSerial.write(26); // Validation de fin de SMS (ASCII 26 / Ctrl+Z)
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Laisse le modem émettre vers l'antenne relais 5 secondes
        my_log_printf("[💬 Cœur 0] Coornonnées GPS envoyées par SMS.");
    }
}

