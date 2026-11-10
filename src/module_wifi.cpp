#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_random.h>

#include "configuration.h"
#include "module_wifi.h"
#include "module_carte_sd.h"
#include "module_journal.h"
#include "module_mise_a_jour.h"
#include "module_systeme.h"
#include "module_synchro.h"

namespace {

const char*    NOM_DU_WIFI                   = "Moto_ESP32";
const uint32_t ARRET_APRES_INACTIVITE_MS     = 2 * 60 * 1000;
const size_t   TAILLE_DU_TAMPON              = 16 * 1024;   // écritures sur la carte SD par paquets de 16 Ko

WebServer        serveur(80);
String           motDePasseDuWifi;
volatile bool    ouvertureDemandee = false;
volatile bool    fermetureDemandee = false;
volatile bool    ouvert            = false;
uint32_t         derniereActivite  = 0;
TaskHandle_t     tacheWifi         = nullptr;

// ─────────────────────────────── Envoi d'un fichier vers la carte SD ───────────────────────────────

struct FichierRecu {
    File     fichier;
    String   cheminFinal;
    String   cheminTemporaire;
    uint8_t* tampon = nullptr;
    size_t   remplissage = 0;
    size_t   octets = 0;
    bool     erreur = false;
    String   message;
};
FichierRecu recu;

bool ecrireLeTampon() {
    if (recu.remplissage == 0) return true;
    bool reussi = false;
    if (prendreLaCarteSD(8000)) {
        reussi = recu.fichier && recu.fichier.write(recu.tampon, recu.remplissage) == recu.remplissage;
        rendreLaCarteSD();
    }
    recu.remplissage = 0;
    return reussi;
}

void oublierLeFichierRecu(bool effacer) {
    if (recu.fichier && prendreLaCarteSD(8000)) {
        recu.fichier.close();
        rendreLaCarteSD();
    }
    if (effacer && recu.cheminTemporaire.length() > 0) supprimerFichierOuDossier(recu.cheminTemporaire.c_str());
    free(recu.tampon);
    recu = FichierRecu();
}

void recevoirUnMorceauDeFichier() {
    HTTPUpload& envoi = serveur.upload();
    derniereActivite = millis();
    switch (envoi.status) {
        case UPLOAD_FILE_START: {
            oublierLeFichierRecu(true);
            recu.cheminFinal = serveur.arg("chemin");
            if (!recu.cheminFinal.startsWith("/") || recu.cheminFinal.endsWith("/")) {
                recu.erreur = true; recu.message = "Chemin invalide"; break;
            }
            recu.cheminTemporaire = recu.cheminFinal + ".part";
            recu.tampon = (uint8_t*)ps_malloc(TAILLE_DU_TAMPON);
            supprimerFichierOuDossier(recu.cheminTemporaire.c_str());
            if (recu.tampon != nullptr && prendreLaCarteSD(8000)) {
                recu.fichier = systemeDeFichiersDeLaCarteSD().open(recu.cheminTemporaire, FILE_WRITE);
                rendreLaCarteSD();
            }
            if (!recu.fichier) { recu.erreur = true; recu.message = "Impossible de créer le fichier (carte SD ?)"; }
            break;
        }
        case UPLOAD_FILE_WRITE: {
            if (recu.erreur) break;
            const uint8_t* octets = envoi.buf;
            size_t reste = envoi.currentSize;
            while (reste > 0 && !recu.erreur) {
                const size_t aCopier = min(reste, TAILLE_DU_TAMPON - recu.remplissage);
                memcpy(recu.tampon + recu.remplissage, octets, aCopier);
                recu.remplissage += aCopier;
                recu.octets += aCopier;
                octets += aCopier;
                reste -= aCopier;
                if (recu.remplissage == TAILLE_DU_TAMPON && !ecrireLeTampon()) {
                    recu.erreur = true; recu.message = "Écriture sur la carte SD impossible";
                }
            }
            break;
        }
        case UPLOAD_FILE_END:
            if (!recu.erreur && !ecrireLeTampon()) { recu.erreur = true; recu.message = "Écriture sur la carte SD impossible"; }
            break;
        case UPLOAD_FILE_ABORTED:
            recu.erreur = true; recu.message = "Envoi interrompu";
            break;
    }
}

void terminerLEnvoiDUnFichier() {
    bool reussi = !recu.erreur && recu.cheminFinal.length() > 0;
    if (reussi) {
        if (recu.fichier && prendreLaCarteSD(8000)) { recu.fichier.close(); rendreLaCarteSD(); }
        supprimerFichierOuDossier(recu.cheminFinal.c_str());
        reussi = renommerOuDeplacer(recu.cheminTemporaire.c_str(), recu.cheminFinal.c_str());
        if (!reussi) recu.message = "Impossible de renommer le fichier reçu";
    }
    ecrireDansLeJournal("[WIFI] Réception de %s (%u octets) : %s.", recu.cheminFinal.c_str(), (unsigned)recu.octets,
                        reussi ? "OK" : recu.message.c_str());
    if (reussi) serveur.send(200, "text/plain", "OK");
    else        serveur.send(500, "text/plain", recu.message.length() ? recu.message : String("Échec"));
    oublierLeFichierRecu(!reussi);
}

// ─────────────────────────────── Téléchargement d'un fichier de la carte SD ───────────────────────────────

void envoyerUnFichierAuTelephone() {
    derniereActivite = millis();
    const String chemin = serveur.arg("chemin");
    File fichier;
    if (prendreLaCarteSD(8000)) {
        fichier = systemeDeFichiersDeLaCarteSD().open(chemin, FILE_READ);
        rendreLaCarteSD();
    }
    if (!fichier || fichier.isDirectory()) {
        serveur.send(404, "text/plain", "Fichier introuvable");
        return;
    }
    const size_t taille = fichier.size();
    uint8_t* tampon = (uint8_t*)ps_malloc(TAILLE_DU_TAMPON);
    if (tampon == nullptr) {
        fichier.close();
        serveur.send(500, "text/plain", "Mémoire insuffisante");
        return;
    }
    serveur.setContentLength(taille);
    serveur.send(200, "application/octet-stream", "");
    size_t envoyes = 0;
    while (envoyes < taille) {
        size_t lus = 0;
        if (prendreLaCarteSD(8000)) {   // la carte n'est prise que le temps de lire 16 Ko
            lus = fichier.read(tampon, min(TAILLE_DU_TAMPON, taille - envoyes));
            rendreLaCarteSD();
        }
        if (lus == 0) break;
        if (serveur.client().write(tampon, lus) != lus) break;
        envoyes += lus;
        derniereActivite = millis();
    }
    free(tampon);
    if (prendreLaCarteSD(8000)) { fichier.close(); rendreLaCarteSD(); }
    ecrireDansLeJournal("[WIFI] Envoi de %s : %u / %u octets.", chemin.c_str(), (unsigned)envoyes, (unsigned)taille);
}

// ─────────────────────────────── Mise à jour du firmware ───────────────────────────────

bool   erreurDeMiseAJour = false;
String messageDeMiseAJour;

void recevoirUnMorceauDeFirmware() {
    HTTPUpload& envoi = serveur.upload();
    derniereActivite = millis();
    switch (envoi.status) {
        case UPLOAD_FILE_START:
            erreurDeMiseAJour = false;
            messageDeMiseAJour = "";
            annulerLaMiseAJourEnCours();   // au cas où une mise à jour par Bluetooth serait en cours
            // Un envoi précédent coupé net (téléphone parti) laisse Update « en cours » : sans ça,
            // toutes les mises à jour suivantes seraient refusées jusqu'au redémarrage du module.
            if (Update.isRunning()) Update.abort();
            ecrireDansLeJournal("[WIFI] Mise à jour du firmware reçue par Wi-Fi...");
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { erreurDeMiseAJour = true; messageDeMiseAJour = Update.errorString(); }
            break;
        case UPLOAD_FILE_WRITE:
            if (!erreurDeMiseAJour && Update.write(envoi.buf, envoi.currentSize) != envoi.currentSize) {
                erreurDeMiseAJour = true; messageDeMiseAJour = Update.errorString();
            }
            break;
        case UPLOAD_FILE_END:
            if (!erreurDeMiseAJour && !Update.end(true)) { erreurDeMiseAJour = true; messageDeMiseAJour = Update.errorString(); }
            break;
        case UPLOAD_FILE_ABORTED:
            Update.abort();
            erreurDeMiseAJour = true; messageDeMiseAJour = "Envoi interrompu";
            break;
    }
}

void terminerLaMiseAJour() {
    if (erreurDeMiseAJour) {
        ecrireDansLeJournal("[WIFI] Mise à jour refusée : %s.", messageDeMiseAJour.c_str());
        serveur.send(500, "text/plain", messageDeMiseAJour);
        return;
    }
    ecrireDansLeJournal("[WIFI] Mise à jour vérifiée, redémarrage.");
    serveur.send(200, "text/plain", "OK");
    redemarrerDansQuelquesInstants(1500);
}

// ─────────────────────────────── Point d'accès ───────────────────────────────

void preparerLeServeur() {
    serveur.on("/ping", HTTP_GET, []() { derniereActivite = millis(); serveur.send(200, "text/plain", "ok"); });
    serveur.on("/fichier", HTTP_GET, envoyerUnFichierAuTelephone);
    serveur.on("/fichier", HTTP_POST, terminerLEnvoiDUnFichier, recevoirUnMorceauDeFichier);
    serveur.on("/firmware", HTTP_POST, terminerLaMiseAJour, recevoirUnMorceauDeFirmware);
    serveur.onNotFound([]() { serveur.send(404, "text/plain", "?"); });
}

void allumer() {
    interrompreLaSynchronisation();   // le Wi-Fi de la maison et le point d'accès ne vont pas ensemble
    WiFi.mode(WIFI_AP);
    WiFi.softAP(NOM_DU_WIFI, motDePasseDuWifi.c_str(), 6, 0, 1);   // canal 6, visible, un seul téléphone
    serveur.begin();
    ouvert = true;
    derniereActivite = millis();
    ecrireDansLeJournal("[WIFI] Point d'accès %s ouvert (%s).", NOM_DU_WIFI, WiFi.softAPIP().toString().c_str());
}

void eteindre() {
    serveur.stop();
    oublierLeFichierRecu(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    ouvert = false;
    ecrireDansLeJournal("[WIFI] Point d'accès fermé.");
}

void tacheDuWifi(void*) {
    preparerLeServeur();
    for (;;) {
        if (!ouvert) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // dort jusqu'à une demande d'ouverture
        }
        if (ouvertureDemandee && !ouvert) allumer();
        ouvertureDemandee = false;
        if (!ouvert) continue;
        serveur.handleClient();
        if (fermetureDemandee || millis() - derniereActivite > ARRET_APRES_INACTIVITE_MS) {
            fermetureDemandee = false;
            eteindre();
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

String tirerUnMotDePasse() {
    const char* lettres = "abcdefghjkmnpqrstuvwxyz23456789";
    String motDePasse;
    for (int i = 0; i < 12; i++) motDePasse += lettres[esp_random() % strlen(lettres)];
    return motDePasse;
}

} // namespace

bool ouvrirLeWifi(String& nom, String& motDePasse, String& adresse) {
    if (tacheWifi == nullptr) return false;
    fermetureDemandee = false;
    ouvertureDemandee = true;
    derniereActivite = millis();
    xTaskNotifyGive(tacheWifi);
    for (int attente = 0; attente < 240 && !ouvert; attente++) vTaskDelay(pdMS_TO_TICKS(50));   // 12 s : le temps d'arrêter une synchro en cours
    nom = NOM_DU_WIFI;
    motDePasse = motDePasseDuWifi;
    adresse = "192.168.4.1";
    return ouvert;
}

void fermerLeWifi() {
    if (ouvert) fermetureDemandee = true;
}

bool wifiOuvert() { return ouvert; }

bool demarrerModuleWifi() {
    // Mot de passe tiré une fois pour toutes : le téléphone se souvient ainsi du réseau déjà accepté.
    Preferences memoire;
    memoire.begin("wifi", false);
    motDePasseDuWifi = memoire.getString("mdp", "");
    if (motDePasseDuWifi.length() < 8) {
        motDePasseDuWifi = tirerUnMotDePasse();
        memoire.putString("mdp", motDePasseDuWifi);
    }
    memoire.end();
    WiFi.mode(WIFI_OFF);
    return creerTacheSurveillee(tacheDuWifi, "Wifi", 8192, PRIORITE_NORMALE, COEUR_TRAITEMENTS, &tacheWifi);
}
