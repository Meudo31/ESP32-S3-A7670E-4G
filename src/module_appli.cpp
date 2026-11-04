#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>
#include <esp_bt.h>

#include "configuration.h"
#include "module_appli.h"
#include "module_appels.h"
#include "module_batterie.h"
#include "module_camera.h"
#include "module_carte.h"
#include "module_carte_sd.h"
#include "module_explorateur_fichiers.h"
#include "module_gps.h"
#include "module_haut_parleur.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_mise_a_jour.h"
#include "module_neopixels.h"
#include "module_reseau_4g.h"
#include "module_sms.h"
#include "module_systeme.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef SECRET_BLE_PIN
#warning "SECRET_BLE_PIN absent de include/secrets.h : code PIN Bluetooth par défaut 123456 (à changer !)"
#define SECRET_BLE_PIN 123456
#endif

namespace {

// ─────────────────────────────── État de la liaison ───────────────────────────────

BLEServer*         serveur          = nullptr;
BLECharacteristic* caracDonnees     = nullptr;
BLECharacteristic* caracReponse     = nullptr;
BLECharacteristic* caracJournal     = nullptr;
BLECharacteristic* caracOtaControle = nullptr;
BLECharacteristic* caracFichier     = nullptr;

volatile bool     telephoneConnecte   = false;
volatile bool     telephoneAuthentifie = false;
volatile bool     journalDemande      = true;
volatile uint16_t identifiantDeConnexion = 0;

// ─── Surveillance de la liaison Bluetooth (pour comprendre et éviter les coupures) ───
esp_bd_addr_t     adresseDuTelephone = {0};
volatile int8_t   signalDuTelephoneDbm = 0;        // RSSI mesuré par le module
volatile uint16_t intervalleDeLiaison = 0;         // en unités de 1,25 ms
volatile uint16_t delaiDeCoupure = 0;              // délai de supervision, en unités de 10 ms
volatile bool     parametresAVerifier = false;     // le téléphone vient de changer les paramètres
volatile bool     deconnexionANoter = false;
volatile uint8_t  raisonDeLaDerniereDeconnexion = 0;
uint32_t          debutDeLaConnexion = 0;
uint32_t          nombreDeDeconnexions = 0;
const int         TAILLE_DE_L_HISTORIQUE = 4;
String            historiqueDesDeconnexions[TAILLE_DE_L_HISTORIQUE];
const uint16_t    DELAI_DE_COUPURE_SOUHAITE = 2000;   // 20 s, en unités de 10 ms

SemaphoreHandle_t verrouDesNotifications = nullptr;   // une seule notification à la fois
QueueHandle_t     fileDesCommandes       = nullptr;
QueueHandle_t     fileDuJournal          = nullptr;

const size_t TAILLE_MAXIMUM_D_UNE_COMMANDE = 512;
const size_t TAILLE_MAXIMUM_D_UN_MESSAGE   = 600;

struct CommandeRecue { char texte[TAILLE_MAXIMUM_D_UNE_COMMANDE]; };
struct LigneDuJournalPourLAppli { char texte[LONGUEUR_MAXIMUM_D_UNE_LIGNE_DU_JOURNAL + 12]; };

// Cadences d'envoi des données régulières (millisecondes)
const uint32_t PERIODE_GPS        = 1000;
const uint32_t PERIODE_BATTERIE   = 5000;
const uint32_t PERIODE_RESEAU     = 5000;
const uint32_t PERIODE_SYSTEME    = 5000;
const uint32_t PERIODE_LENTE      = 15000;   // néopixels, carte SD, SIM, appel

// ─────────────────────────────── Envoi de notifications ───────────────────────────────

const uint32_t PAUSE_ENTRE_DEUX_NOTIFICATIONS_MS = 8;

void notifier(BLECharacteristic* caracteristique, const uint8_t* octets, size_t taille) {
    if (!telephoneConnecte || !telephoneAuthentifie || caracteristique == nullptr) return;
    xSemaphoreTake(verrouDesNotifications, portMAX_DELAY);
    caracteristique->setValue((uint8_t*)octets, taille);
    caracteristique->notify();
    // Petite pause entre deux notifications : sans elle, une rafale (tout l'état d'un coup, une page de
    // dossier, un fichier) remplit la file d'envoi du Bluetooth et le téléphone finit par couper la liaison.
    vTaskDelay(pdMS_TO_TICKS(PAUSE_ENTRE_DEUX_NOTIFICATIONS_MS));
    xSemaphoreGive(verrouDesNotifications);
}

void notifierUnTexte(BLECharacteristic* caracteristique, const char* texte) {
    notifier(caracteristique, (const uint8_t*)texte, strlen(texte));
}

void envoyerUnDocument(JsonDocument& document) { publierUnEvenementVersLAppli(document); }

// ─────────────────────────────── Données régulières ───────────────────────────────

void envoyerLaPositionGPS()  { JsonDocument d; decrireLaPositionGPS(d);       envoyerUnDocument(d); }
void envoyerLaBatterie()     { JsonDocument d; decrireLaBatterie(d);          envoyerUnDocument(d); }
void envoyerLeReseau()       { JsonDocument d; decrireLeReseau4G(d);          envoyerUnDocument(d); }
void envoyerLaCarteSIM()     { JsonDocument d; decrireLaCarteSIMEtLeModem(d); envoyerUnDocument(d); }
void envoyerLesNeopixels()   { JsonDocument d; decrireLesNeopixels(d);        envoyerUnDocument(d); }
void envoyerLAppelEnCours()  { JsonDocument d; decrireLAppelEnCours(d);       envoyerUnDocument(d); }
void envoyerLeProcesseur() {
    JsonDocument d;
    decrireLeProcesseur(d);
    envoyerUnDocument(d);
    // La liaison Bluetooth vue par le module, dans un message à part (le message « sys » est déjà long).
    JsonDocument liaison;
    liaison["t"]    = "ble";
    liaison["rssi"] = signalDuTelephoneDbm;
    liaison["int"]  = intervalleDeLiaison * 1.25f;
    liaison["to"]   = delaiDeCoupure / 100.0f;
    liaison["deco"] = nombreDeDeconnexions;
    JsonArray historique = liaison["hist"].to<JsonArray>();
    for (const String& ligne : historiqueDesDeconnexions) if (ligne.length() > 0) historique.add(ligne);
    envoyerUnDocument(liaison);
}
void envoyerLesMemoires()    { JsonDocument d; decrireLesMemoires(d);         envoyerUnDocument(d); }

void envoyerLaCarteEtLaCarteSD() {
    const InformationsCarteSD sd = informationsDeLaCarteSD();
    JsonDocument d;
    d["t"]       = "carte";
    d["version"] = nomDeLaVersionDeCarte();
    d["methode"] = methodeDeDetectionDeLaCarte();
    d["camera"]  = etatDeLaCamera();
    d["sd"]      = sd.montee ? 1 : 0;
    d["sd_type"] = sd.type;
    d["sd_total"] = sd.tailleTotaleOctets;
    d["sd_util"] = sd.espaceUtiliseOctets;
    d["heure"]   = nomDeLaSourceDeLHeure();
    d["vol"]     = volumeDuHautParleur();
    envoyerUnDocument(d);
}

// Liste des tâches, par paquets de 5 : {"t":"taches","debut":1,"fin":0,"l":[["GPS",5120,1800,1,0],...]}
void envoyerLesTaches() {
    const int nombre = nombreDeTachesSurveillees();
    for (int premiere = 0; premiere < nombre; premiere += 5) {
        JsonDocument d;
        d["t"]   = "taches";
        d["debut"] = premiere == 0 ? 1 : 0;
        d["fin"] = premiere + 5 >= nombre ? 1 : 0;
        JsonArray liste = d["l"].to<JsonArray>();
        for (int numero = premiere; numero < min(premiere + 5, nombre); numero++) {
            const TacheSurveillee& tache = tacheSurveillee(numero);
            JsonArray ligne = liste.add<JsonArray>();
            ligne.add(tache.nom);
            ligne.add(tache.tailleDeLaPile);
            ligne.add((uint32_t)uxTaskGetStackHighWaterMark(tache.poignee));
            ligne.add((uint32_t)tache.priorite);
            ligne.add((int)tache.coeur);
        }
        envoyerUnDocument(d);
    }
}

void envoyerToutLEtat() {
    envoyerLeProcesseur();
    envoyerLesMemoires();
    envoyerLaCarteEtLaCarteSD();
    envoyerLaBatterie();
    envoyerLaPositionGPS();
    envoyerLeReseau();
    envoyerLaCarteSIM();
    envoyerLesNeopixels();
    envoyerLAppelEnCours();
}

// ─────────────────────────────── Commandes ───────────────────────────────
// Format : NOM argument1|argument2   (le nom est insensible à la casse, les arguments non)

struct ArgumentsDeCommande {
    String premier;
    String second;
    String tout;
};

typedef void (*FonctionDeCommande)(const char* nom, const ArgumentsDeCommande& arguments);

void commandePing(const char* nom, const ArgumentsDeCommande&) { repondreALAppli(nom, true, "pong"); }

void commandeEtat(const char* nom, const ArgumentsDeCommande&) {
    envoyerToutLEtat();
    repondreALAppli(nom, true, "Données envoyées");
}

void commandeSysteme(const char* nom, const ArgumentsDeCommande&) {
    envoyerLeProcesseur();
    envoyerLesMemoires();
    envoyerLesTaches();
    repondreALAppli(nom, true, "Informations système envoyées");
}

void commandeRedemarrer(const char* nom, const ArgumentsDeCommande&) {
    repondreALAppli(nom, true, "Redémarrage dans 1 s");
    redemarrerDansQuelquesInstants(1000);
}

void commandeJournal(const char* nom, const ArgumentsDeCommande& arguments) {
    journalDemande = !arguments.premier.equalsIgnoreCase("OFF");
    if (!journalDemande) xQueueReset(fileDuJournal);
    repondreALAppli(nom, true, journalDemande ? "Journal activé" : "Journal coupé");
}

void commandeHeure(const char* nom, const ArgumentsDeCommande& arguments) {
    const time_t secondesUTC = (time_t)strtoul(arguments.premier.c_str(), nullptr, 10);
    if (sourceDeLHeure() == SourceDeLHeure::GPS) {
        repondreALAppli(nom, true, "Heure déjà donnée par le GPS");
        return;
    }
    mettreLHorlogeALHeure(secondesUTC, SourceDeLHeure::TELEPHONE);
    repondreALAppli(nom, lHeureEstConnue(), "Horloge réglée");
}

void commandePhoto(const char* nom, const ArgumentsDeCommande&) {
    const bool demandee = demanderUnePhoto();
    repondreALAppli(nom, demandee, demandee ? "Photo demandée" : "Caméra non démarrée");
}

void commandeGPS(const char* nom, const ArgumentsDeCommande&) {
    actualiserLaPositionGPS();
    repondreALAppli(nom, true, "Position demandée");
}

void commandeReseau(const char* nom, const ArgumentsDeCommande&) {
    actualiserLeReseau4G();
    envoyerLaCarteSIM();
    repondreALAppli(nom, true, "Réseau en cours d'actualisation");
}

void commandeEnvoyerUnSMS(const char* nom, const ArgumentsDeCommande& arguments) {
    const bool accepte = demanderLEnvoiDUnSMS(arguments.premier, arguments.second);
    repondreALAppli(nom, accepte, accepte ? "SMS en cours d'envoi" : "Numéro ou texte vide, ou file pleine");
}

// Chaque SMS part dans un message : {"t":"sms","i":3,"de":"+336...","date":"...","txt":"...","lu":1}
void commandeListerLesSMS(const char* nom, const ArgumentsDeCommande&) {
    int nombre = 0;
    JsonDocument debut;
    debut["t"] = "sms_liste_debut";
    envoyerUnDocument(debut);
    const bool reussi = listerLesSMS([&](const MessageSMS& sms) {
        JsonDocument d;
        d["t"]    = "sms";
        d["i"]    = sms.numeroDansLaSim;
        d["de"]   = sms.expediteur;
        d["date"] = sms.date;
        d["txt"]  = sms.texte;
        d["lu"]   = sms.lu ? 1 : 0;
        envoyerUnDocument(d);
        nombre++;
        vTaskDelay(pdMS_TO_TICKS(10));
    });
    repondreALAppli(nom, reussi, reussi ? (String(nombre) + " SMS").c_str() : "Le modem ne répond pas");
}

void commandeSupprimerUnSMS(const char* nom, const ArgumentsDeCommande& arguments) {
    const bool reussi = supprimerUnSMS(arguments.premier.toInt());
    repondreALAppli(nom, reussi, reussi ? "SMS supprimé" : "Suppression impossible");
}

void commandeAppeler(const char* nom, const ArgumentsDeCommande& arguments) {
    const bool lance = appeler(arguments.premier);
    repondreALAppli(nom, lance, lance ? "Appel en cours" : "Numéro vide ou modem sans voix");
}

void commandeDecrocher(const char* nom, const ArgumentsDeCommande&) {
    const bool lance = decrocher();
    repondreALAppli(nom, lance, lance ? "Décroché" : "Aucun appel entrant");
}

void commandeRaccrocher(const char* nom, const ArgumentsDeCommande&) {
    raccrocher();
    repondreALAppli(nom, true, "Raccroché");
}

void commandeVolume(const char* nom, const ArgumentsDeCommande& arguments) {
    reglerLeVolumeDuHautParleur(arguments.premier.toInt());
    repondreALAppli(nom, true, "Volume réglé");
}

void commandeInstallerLesSons(const char* nom, const ArgumentsDeCommande&) {
    const String compteRendu = installerLesSonsDepuisLaCarteSD();
    repondreALAppli(nom, compteRendu.indexOf("installé") >= 0, compteRendu.c_str());
}

void commandeTesterLeSon(const char* nom, const ArgumentsDeCommande&) {
    testerLeHautParleurDuModule();   // le résultat arrive dans quelques secondes (réponse SON_TEST)
}

void commandeJouerUnSon(const char* nom, const ArgumentsDeCommande& arguments) {
    if (!jouerUnSonDeLaCarteSD(arguments.tout)) repondreALAppli(nom, false, "Chemin invalide");
}

void commandeSortieAudio(const char* nom, const ArgumentsDeCommande& arguments) {
    if (!choisirLaSortieDuHautParleur(arguments.premier.toInt())) repondreALAppli(nom, false, "Sortie 1 (combiné) ou 3 (haut-parleur)");
}

void commandeAnnoncer(const char* nom, const ArgumentsDeCommande& arguments) {
    annoncerAuHautParleur(arguments.tout);
    repondreALAppli(nom, true, "Annonce envoyée au haut-parleur");
}

void commandeChoisirLAnimation(const char* nom, const ArgumentsDeCommande& arguments) {
    choisirLAnimation(arguments.premier.toInt());
    repondreALAppli(nom, true, nomDeLAnimation(numeroDeLAnimationEnCours()));
}

void commandeAnimationSuivante(const char* nom, const ArgumentsDeCommande&) {
    passerALAnimationSuivante();
    repondreALAppli(nom, true, nomDeLAnimation(numeroDeLAnimationEnCours()));
}

void commandeLuminosite(const char* nom, const ArgumentsDeCommande& arguments) {
    reglerLaLuminositeDesNeopixels(arguments.premier.toInt());
    envoyerLesNeopixels();
    repondreALAppli(nom, true, "Luminosité réglée");
}

void commandeAllumerOuEteindre(const char* nom, const ArgumentsDeCommande&) {
    allumerOuEteindreLesNeopixels();
    repondreALAppli(nom, true, "OK");
}

void demanderAuxFichiers(const char* nom, OperationSurLesFichiers operation, const String& chemin, const String& destination = "") {
    if (!demanderUneOperationSurLesFichiers(operation, chemin, destination)) {
        repondreALAppli(nom, false, "Chemin invalide ou explorateur occupé");
    }
    // Sinon, la réponse viendra du module explorateur de fichiers une fois l'opération faite.
}

void commandeListerUnDossier(const char* nom, const ArgumentsDeCommande& a) {
    demanderAuxFichiers(nom, OperationSurLesFichiers::LISTER, a.tout.isEmpty() ? String("/") : a.tout);
}
void commandeRenommer(const char* nom, const ArgumentsDeCommande& a) {
    demanderAuxFichiers(nom, OperationSurLesFichiers::RENOMMER_OU_DEPLACER, a.premier, a.second);
}
void commandeSupprimerUnFichier(const char* nom, const ArgumentsDeCommande& a) {
    demanderAuxFichiers(nom, OperationSurLesFichiers::SUPPRIMER, a.tout);
}
void commandeCreerUnDossier(const char* nom, const ArgumentsDeCommande& a) {
    demanderAuxFichiers(nom, OperationSurLesFichiers::CREER_UN_DOSSIER, a.tout);
}
void commandeTelecharger(const char* nom, const ArgumentsDeCommande& a) {
    demanderAuxFichiers(nom, OperationSurLesFichiers::TELECHARGER, a.tout);
}
void commandeConfirmationDeTelechargement(const char*, const ArgumentsDeCommande& a) {
    confirmerLaReceptionDuTelephone(strtoul(a.premier.c_str(), nullptr, 10));   // pas de réponse : trop fréquent
}
void commandeAnnulerLeTelechargement(const char* nom, const ArgumentsDeCommande&) {
    annulerLeTelechargement();
    repondreALAppli(nom, true, "Téléchargement annulé");
}
void commandeRecevoirUnFichier(const char* nom, const ArgumentsDeCommande& a) {
    String compteRendu;
    const bool reussi = commencerLaReceptionDUnFichier(a.premier, strtoul(a.second.c_str(), nullptr, 10), compteRendu);
    repondreALAppli(nom, reussi, compteRendu.c_str());
}
void commandeTerminerLaReception(const char* nom, const ArgumentsDeCommande& a) {
    String compteRendu;
    const bool reussi = terminerLaReceptionDUnFichier(strtoul(a.premier.c_str(), nullptr, 10), compteRendu);
    repondreALAppli(nom, reussi, compteRendu.c_str());
}
void commandeAnnulerLaReception(const char* nom, const ArgumentsDeCommande&) {
    annulerLaReceptionDUnFichier();
    repondreALAppli(nom, true, "Envoi annulé");
}

struct CommandeConnue {
    const char*        nom;
    FonctionDeCommande executer;
};

const CommandeConnue COMMANDES[] = {
    { "PING",                commandePing },
    { "STATUS",              commandeEtat },
    { "SYSTEME",             commandeSysteme },
    { "REBOOT",              commandeRedemarrer },
    { "LOGS",                commandeJournal },
    { "HEURE",               commandeHeure },
    { "PHOTO",               commandePhoto },
    { "GPS",                 commandeGPS },
    { "RESEAU",              commandeReseau },
    { "SMS_ENVOYER",         commandeEnvoyerUnSMS },
    { "SMS_LISTE",           commandeListerLesSMS },
    { "SMS_SUPPRIMER",       commandeSupprimerUnSMS },
    { "APPELER",             commandeAppeler },
    { "DECROCHER",           commandeDecrocher },
    { "RACCROCHER",          commandeRaccrocher },
    { "VOLUME",              commandeVolume },
    { "SONS_INSTALLER",      commandeInstallerLesSons },
    { "ANNONCER",            commandeAnnoncer },
    { "SON_TEST",            commandeTesterLeSon },
    { "SON_JOUER",           commandeJouerUnSon },
    { "SORTIE_AUDIO",        commandeSortieAudio },
    { "NEO",                 commandeChoisirLAnimation },
    { "NEO_SUIVANTE",        commandeAnimationSuivante },
    { "NEO_LUMINOSITE",      commandeLuminosite },
    { "NEO_ONOFF",           commandeAllumerOuEteindre },
    { "FICHIERS",            commandeListerUnDossier },
    { "FICHIER_RENOMMER",    commandeRenommer },
    { "FICHIER_DEPLACER",    commandeRenommer },
    { "FICHIER_SUPPRIMER",   commandeSupprimerUnFichier },
    { "DOSSIER_CREER",       commandeCreerUnDossier },
    { "FICHIER_TELECHARGER", commandeTelecharger },
    { "DL_ACK",              commandeConfirmationDeTelechargement },
    { "DL_ANNULER",          commandeAnnulerLeTelechargement },
    { "FICHIER_ENVOYER",     commandeRecevoirUnFichier },
    { "FICHIER_ENVOYER_FIN", commandeTerminerLaReception },
    { "FICHIER_ENVOYER_ANNULER", commandeAnnulerLaReception },
};

void executerUneCommande(const char* texte) {
    String ligne(texte);
    ligne.trim();
    const int espace = ligne.indexOf(' ');
    String nom = espace < 0 ? ligne : ligne.substring(0, espace);
    nom.toUpperCase();

    ArgumentsDeCommande arguments;
    arguments.tout = espace < 0 ? String("") : ligne.substring(espace + 1);
    arguments.tout.trim();
    const int separateur = arguments.tout.indexOf('|');
    arguments.premier = separateur < 0 ? arguments.tout : arguments.tout.substring(0, separateur);
    arguments.second  = separateur < 0 ? String("") : arguments.tout.substring(separateur + 1);

    if (nom != "DL_ACK") ecrireDansLeJournal("[APPLI] Commande : %s", nom.c_str());
    for (const CommandeConnue& commande : COMMANDES) {
        if (nom == commande.nom) {
            commande.executer(commande.nom, arguments);
            return;
        }
    }
    repondreALAppli(nom.c_str(), false, "Commande inconnue");
}

// ─────────────────────────────── Rappels Bluetooth ───────────────────────────────

size_t copierLeTexteEcrit(BLECharacteristic* caracteristique, char* tampon, size_t tailleDuTampon) {
    size_t taille = min(caracteristique->getLength(), tailleDuTampon - 1);
    memcpy(tampon, caracteristique->getData(), taille);
    tampon[taille] = '\0';
    return taille;
}

class RappelsDuServeur : public BLEServerCallbacks {
    void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* parametres) override {
        telephoneConnecte      = true;
        telephoneAuthentifie   = false;
        identifiantDeConnexion = parametres->connect.conn_id;
        memcpy(adresseDuTelephone, parametres->connect.remote_bda, sizeof(esp_bd_addr_t));
        debutDeLaConnexion = millis();
        intervalleDeLiaison = parametres->connect.conn_params.interval;
        delaiDeCoupure      = parametres->connect.conn_params.timeout;
        // Puissance maximale aussi pour la liaison elle-même (et pas seulement pour l'annonce).
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, ESP_PWR_LVL_P18);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL1, ESP_PWR_LVL_P18);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL2, ESP_PWR_LVL_P18);
        // Les paramètres tolérants (coupure après 20 s) ne sont PAS demandés ici : au même moment le téléphone
        // règle lui-même la liaison et le chiffrement démarre ; deux procédures croisées font couper la liaison
        // 40 s plus tard (raison 0x22). surveillerLaLiaison() les demande une fois la liaison bien installée.
        // Demande le chiffrement : le téléphone affiche la saisie du code PIN s'il n'est pas encore appairé.
        esp_ble_set_encryption(parametres->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
        Serial.println("[APPLI] Téléphone connecté, authentification...");
    }
    void onDisconnect(BLEServer*, esp_ble_gatts_cb_param_t* parametres) override {
        telephoneConnecte    = false;
        telephoneAuthentifie = false;
        annulerLaMiseAJourEnCours();
        annulerLeTelechargement();
        annulerLaReceptionDUnFichier();
        // La raison est notée dans le journal du jour pour comprendre les coupures :
        // 0x08 = plus de signal (trop loin, perturbations), 0x13 = coupé par le téléphone,
        // 0x16 = coupé par le module, 0x3E = liaison jamais établie.
        // Le détail (durée, signal, délai de coupure) est noté par la tâche Appli, hors du Bluetooth.
        raisonDeLaDerniereDeconnexion = parametres->disconnect.reason;
        deconnexionANoter = true;
        BLEDevice::startAdvertising();
    }
};

class RappelsDeSecurite : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return SECRET_BLE_PIN; }
    void onPassKeyNotify(uint32_t) override {}
    bool onConfirmPIN(uint32_t) override { return false; }   // pas de comparaison numérique : code PIN obligatoire
    bool onSecurityRequest() override { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t resultat) override {
        telephoneAuthentifie = resultat.success;
        if (resultat.success) {
            ecrireDansLeJournal("[APPLI] Téléphone connecté et authentifié.");
        } else {
            Serial.printf("[APPLI] Échec d'authentification (raison 0x%x), déconnexion.\n", resultat.fail_reason);
            if (serveur) serveur->disconnect(identifiantDeConnexion);
        }
    }
};

class RappelsDeLaCommande : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* caracteristique) override {
        if (!telephoneAuthentifie) return;
        CommandeRecue* commande = (CommandeRecue*)malloc(sizeof(CommandeRecue));
        if (commande == nullptr) return;
        if (copierLeTexteEcrit(caracteristique, commande->texte, sizeof(commande->texte)) > 0
            && xQueueSend(fileDesCommandes, commande, 0) != pdPASS) {
            repondreALAppli("?", false, "Module occupé, réessayer");
        }
        free(commande);
    }
};

class RappelsDuControleOTA : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* caracteristique) override {
        if (!telephoneAuthentifie) return;
        recevoirUnOrdreDeMiseAJour(caracteristique->getData(), caracteristique->getLength());
    }
};

class RappelsDuFichierEntrant : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* caracteristique) override {
        if (!telephoneAuthentifie) return;
        recevoirUnMorceauDuTelephone(caracteristique->getData(), caracteristique->getLength());
    }
};

class RappelsDesDonneesOTA : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* caracteristique) override {
        if (!telephoneAuthentifie) return;
        recevoirUnMorceauDeFirmware(caracteristique->getData(), caracteristique->getLength());
    }
};

// Crée une caractéristique lisible seulement après appairage (chiffrement + code PIN).
BLECharacteristic* creerUneCaracteristique(BLEService* service, const char* uuid, uint32_t proprietes) {
    BLECharacteristic* caracteristique = service->createCharacteristic(uuid, proprietes);
    caracteristique->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
    if (proprietes & BLECharacteristic::PROPERTY_NOTIFY) {
        BLE2902* descripteur = new BLE2902();
        descripteur->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
        caracteristique->addDescriptor(descripteur);
    }
    return caracteristique;
}

// Événements radio : le téléphone choisit les paramètres de la liaison (et Android remet souvent
// un délai de coupure de 5 s) ; on les note, et la tâche Appli redemande 20 s si besoin.
void surveillerLaRadio(esp_gap_ble_cb_event_t evenement, esp_ble_gap_cb_param_t* parametres) {
    if (evenement == ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT && parametres->update_conn_params.status == ESP_BT_STATUS_SUCCESS) {
        intervalleDeLiaison = parametres->update_conn_params.conn_int;
        delaiDeCoupure      = parametres->update_conn_params.timeout;
        parametresAVerifier = true;
    } else if (evenement == ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT && parametres->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS) {
        signalDuTelephoneDbm = parametres->read_rssi_cmpl.rssi;
    }
}

void demanderUneLiaisonTolerante() {
    // Intervalle 15 à 30 ms, aucune latence, coupure seulement après 20 s sans échange.
    serveur->updateConnParams(adresseDuTelephone, 12, 24, 0, DELAI_DE_COUPURE_SOUHAITE);
}

// Appelé régulièrement par la tâche Appli.
void surveillerLaLiaison() {
    static uint32_t derniereMesure = 0, derniereDemande = 0;
    static int demandesSansSucces = 0;

    if (deconnexionANoter) {
        deconnexionANoter = false;
        nombreDeDeconnexions++;
        const uint32_t duree = (millis() - debutDeLaConnexion) / 1000;
        const char* explication = raisonDeLaDerniereDeconnexion == 0x08 ? "plus de signal"
                                : raisonDeLaDerniereDeconnexion == 0x13 ? "coupé par le téléphone"
                                : raisonDeLaDerniereDeconnexion == 0x16 ? "coupé par le module"
                                : raisonDeLaDerniereDeconnexion == 0x22 ? "procédure sans réponse"
                                : raisonDeLaDerniereDeconnexion == 0x3E ? "liaison non établie" : "autre";
        char ligne[96];
        snprintf(ligne, sizeof(ligne), "%s 0x%02X %s, après %u s, signal %d dBm, coupure %.1f s",
                 heurePourLeJournal().c_str(), raisonDeLaDerniereDeconnexion, explication, (unsigned)duree,
                 signalDuTelephoneDbm, delaiDeCoupure / 100.0f);
        for (int i = TAILLE_DE_L_HISTORIQUE - 1; i > 0; i--) historiqueDesDeconnexions[i] = historiqueDesDeconnexions[i - 1];
        historiqueDesDeconnexions[0] = ligne;
        ecrireDansLeJournal("[APPLI] Téléphone déconnecté : %s.", ligne);
        demandesSansSucces = 0;
    }
    if (!telephoneConnecte) return;

    if (millis() - derniereMesure >= 5 * UNE_SECONDE_MS) {
        derniereMesure = millis();
        esp_ble_gap_read_rssi(adresseDuTelephone);
    }
    if (parametresAVerifier) {
        parametresAVerifier = false;
        ecrireDansLeJournal("[APPLI] Liaison : intervalle %.1f ms, coupure après %.1f s.",
                            intervalleDeLiaison * 1.25f, delaiDeCoupure / 100.0f);
        if (delaiDeCoupure >= DELAI_DE_COUPURE_SOUHAITE) demandesSansSucces = 0;
    }
    // Le téléphone a imposé un délai de coupure court : on redemande 20 s (2 fois au plus d'affilée,
    // et pas pendant un transfert qui a besoin d'une liaison rapide).
    // On attend que le téléphone ait fini de s'installer (authentifié depuis plus de 15 s), et on laisse
    // 45 s entre deux demandes : une demande sans réponse n'expire qu'au bout de 40 s.
    const bool transfertEnCours = miseAJourEnCours() || receptionDUnFichierEnCours();
    if (delaiDeCoupure < DELAI_DE_COUPURE_SOUHAITE && !transfertEnCours && telephoneAuthentifie && demandesSansSucces < 2
        && millis() - debutDeLaConnexion >= 15 * UNE_SECONDE_MS && millis() - derniereDemande >= 45 * UNE_SECONDE_MS) {
        derniereDemande = millis();
        demandesSansSucces++;
        demanderUneLiaisonTolerante();
    }
}

void preparerLeBluetooth() {
    BLEDevice::init(NOM_BLUETOOTH_DU_MODULE);
    BLEDevice::setMTU(517);
    // Puissance d'émission maximale de l'ESP32-S3 (+18 dBm au lieu de +9 dBm) : meilleure portée,
    // et la liaison résiste mieux au modem 4G tout proche.
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P18);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P18);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P18);

    BLEDevice::setCustomGapHandler(surveillerLaRadio);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(new RappelsDeSecurite());
    BLESecurity* securite = new BLESecurity();
    securite->setStaticPIN(SECRET_BLE_PIN);
    securite->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);

    serveur = BLEDevice::createServer();
    serveur->setCallbacks(new RappelsDuServeur());
    BLEService* service = serveur->createService(BLEUUID(UUID_SERVICE_APPLI), 40);

    caracDonnees = creerUneCaracteristique(service, UUID_DONNEES,
                       BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic* caracCommande = creerUneCaracteristique(service, UUID_COMMANDE, BLECharacteristic::PROPERTY_WRITE);
    caracCommande->setCallbacks(new RappelsDeLaCommande());
    caracReponse = creerUneCaracteristique(service, UUID_REPONSE, BLECharacteristic::PROPERTY_NOTIFY);
    caracJournal = creerUneCaracteristique(service, UUID_JOURNAL, BLECharacteristic::PROPERTY_NOTIFY);
    caracOtaControle = creerUneCaracteristique(service, UUID_OTA_CONTROLE,
                           BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    caracOtaControle->setCallbacks(new RappelsDuControleOTA());
    BLECharacteristic* caracOtaDonnees = creerUneCaracteristique(service, UUID_OTA_DONNEES,
                           BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    caracOtaDonnees->setCallbacks(new RappelsDesDonneesOTA());
    caracFichier = creerUneCaracteristique(service, UUID_FICHIER, BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic* caracFichierEntrant = creerUneCaracteristique(service, UUID_FICHIER_ENTRANT, BLECharacteristic::PROPERTY_WRITE);
    caracFichierEntrant->setCallbacks(new RappelsDuFichierEntrant());
    service->start();

    BLEAdvertising* annonce = BLEDevice::getAdvertising();
    annonce->addServiceUUID(UUID_SERVICE_APPLI);
    annonce->setScanResponse(true);
    BLEDevice::startAdvertising();
}

// ─────────────────────────────── La tâche ───────────────────────────────

void tacheAppli(void*) {
    static CommandeRecue commande;
    static LigneDuJournalPourLAppli ligne;
    uint32_t dernierGPS = 0, derniereBatterie = 0, dernierReseau = 0, dernierSysteme = 0, dernierEnvoiLent = 0;
    uint32_t derniereRelanceDeLAnnonce = 0;
    bool etaitAuthentifie = false;

    for (;;) {
        while (xQueueReceive(fileDesCommandes, &commande, 0) == pdPASS) executerUneCommande(commande.texte);
        surveillerLaLiaison();

        const bool authentifie = appliConnectee();
        if (authentifie && !etaitAuthentifie) {
            vTaskDelay(pdMS_TO_TICKS(1500));   // laisse le téléphone s'abonner aux notifications
            envoyerToutLEtat();
        }
        etaitAuthentifie = authentifie;

        // Sans téléphone connecté, on relance l'annonce Bluetooth toutes les 30 s par sécurité :
        // le module reste toujours visible pour que l'appli puisse se reconnecter toute seule.
        if (!telephoneConnecte && millis() - derniereRelanceDeLAnnonce >= 30 * UNE_SECONDE_MS) {
            derniereRelanceDeLAnnonce = millis();
            BLEDevice::startAdvertising();
        }

        // Pendant une mise à jour ou un envoi de fichier, on laisse toute la liaison au transfert.
        if (authentifie && !miseAJourEnCours() && !receptionDUnFichierEnCours()) {
            const uint32_t maintenant = millis();
            if (maintenant - dernierGPS       >= PERIODE_GPS)      { dernierGPS = maintenant;       envoyerLaPositionGPS(); }
            if (maintenant - derniereBatterie >= PERIODE_BATTERIE) { derniereBatterie = maintenant; envoyerLaBatterie(); }
            if (maintenant - dernierReseau    >= PERIODE_RESEAU)   { dernierReseau = maintenant;    envoyerLeReseau(); }
            if (maintenant - dernierSysteme   >= PERIODE_SYSTEME)  { dernierSysteme = maintenant;   envoyerLesMemoires(); envoyerLeProcesseur(); }
            if (maintenant - dernierEnvoiLent >= PERIODE_LENTE) {
                dernierEnvoiLent = maintenant;
                envoyerLaCarteEtLaCarteSD();
                envoyerLesNeopixels();
                envoyerLaCarteSIM();
                envoyerLAppelEnCours();
            }
            for (int numero = 0; numero < 5 && xQueueReceive(fileDuJournal, &ligne, 0) == pdPASS; numero++) {
                notifierUnTexte(caracJournal, ligne.texte);
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

} // namespace

// ─────────────────────────────── API publique ───────────────────────────────

bool appliConnectee() { return telephoneConnecte && telephoneAuthentifie; }

void publierUnEvenementVersLAppli(JsonDocument& message) {
    if (!appliConnectee()) return;
    char texte[TAILLE_MAXIMUM_D_UN_MESSAGE];
    const size_t taille = serializeJson(message, texte, sizeof(texte));
    if (taille > tailleMaximumDUneNotification()) {
        Serial.printf("[APPLI] Message « %s » trop long (%u octets) : non envoyé.\n", message["t"].as<const char*>(), (unsigned)taille);
        return;
    }
    notifier(caracDonnees, (const uint8_t*)texte, taille);
}

void repondreALAppli(const char* commande, bool reussi, const char* message) {
    JsonDocument reponse;
    reponse["t"]   = "rep";
    reponse["cmd"] = commande;
    reponse["ok"]  = reussi;
    reponse["msg"] = message;
    char texte[TAILLE_MAXIMUM_D_UN_MESSAGE];
    const size_t taille = serializeJson(reponse, texte, sizeof(texte));
    notifier(caracReponse, (const uint8_t*)texte, min(taille, tailleMaximumDUneNotification()));
}

void transmettreUneLigneDuJournalALAppli(const char* ligne) {
    if (fileDuJournal == nullptr || !appliConnectee() || !journalDemande || miseAJourEnCours() || receptionDUnFichierEnCours()) return;
    LigneDuJournalPourLAppli copie;
    strlcpy(copie.texte, ligne, sizeof(copie.texte));
    xQueueSend(fileDuJournal, &copie, 0);   // file pleine : la ligne reste sur la carte SD et le port série
}

void envoyerUneReponseDeMiseAJourALAppli(const char* texte) { notifierUnTexte(caracOtaControle, texte); }

void envoyerDesOctetsALAppli(const uint8_t* octets, size_t taille) { notifier(caracFichier, octets, taille); }

size_t tailleMaximumDUneNotification() {
    uint16_t mtu = 23;
    if (serveur != nullptr && telephoneConnecte) mtu = serveur->getPeerMTU(identifiantDeConnexion);
    if (mtu < 23) mtu = 23;
    return min((size_t)(mtu - 3), (size_t)500);
}

bool demarrerModuleAppli() {
    verrouDesNotifications = xSemaphoreCreateMutex();
    fileDesCommandes       = xQueueCreate(8, sizeof(CommandeRecue));
    fileDuJournal          = xQueueCreate(20, sizeof(LigneDuJournalPourLAppli));
    if (verrouDesNotifications == nullptr || fileDesCommandes == nullptr || fileDuJournal == nullptr) return false;
    preparerLeBluetooth();
    ecrireDansLeJournal("[APPLI] Bluetooth actif : « %s ».", NOM_BLUETOOTH_DU_MODULE);
    return creerTacheSurveillee(tacheAppli, "Appli BLE", 8192, PRIORITE_BASSE, COEUR_COMMUNICATIONS);
}
