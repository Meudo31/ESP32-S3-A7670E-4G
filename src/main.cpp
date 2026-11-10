/* ══════════════════════════════════════════════════════════════════════════════
   MODULE MOTO — Waveshare ESP32-S3 A7670E 4G
   ──────────────────────────────────────────────────────────────────────────────
   Ce fichier ne fait que démarrer les modules, dans l'ordre. Chaque module est
   dans son propre fichier (src/module_xxx.cpp et include/module_xxx.h) et
   possède sa tâche FreeRTOS :

     Module               Tâche          Rôle
     ─────────────────    ───────────    ──────────────────────────────────────────
     journal              Journal        traces sur le port série, la carte SD et l'appli
     carte SD             Carte SD       montage et surveillance de la carte
     système              Systeme        mémoire, piles des tâches, infos processeur
     batterie             Batterie       jauge MAX17048, source d'alimentation
     néopixels            NeoPixels      13 animations sur la bande de 14 LED
     bouton               Bouton         change d'animation
     caméra               Camera         photos enregistrées sur la carte SD
     modem                Modem          dialogue AT avec l'A7670E, notifications
     horloge              Horloge        heure du réseau
     réseau 4G            Reseau 4G      SIM, opérateur, signal, cellule
     GPS                  GPS            position, vitesse, satellites
     SMS                  SMS            envoi et réception
     appels               Appels         passer, décrocher, raccrocher
     haut-parleur         Haut-parleur   annonces, sons, sonnerie
     explorateur          Fichiers       fichiers de la carte SD pour l'appli
     bus CAN              Bus CAN        écoute des trames de la moto (écoute seule)
     mise à jour          Mise a jour    nouveau firmware envoyé par l'appli
     appli                Appli BLE      liaison Bluetooth avec l'application Android
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

#include "configuration.h"
#include "module_appels.h"
#include "module_appli.h"
#include "module_batterie.h"
#include "module_bouton.h"
#include "module_camera.h"
#include "module_carte.h"
#include "module_canbus.h"
#include "module_carte_sd.h"
#include "module_explorateur_fichiers.h"
#include "module_gps.h"
#include "module_haut_parleur.h"
#include "module_wifi.h"
#include "module_alertes_sms.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_mise_a_jour.h"
#include "module_modem.h"
#include "module_neopixels.h"
#include "module_reseau_4g.h"
#include "module_sms.h"
#include "module_systeme.h"

namespace {

int nombreDeModulesEnEchec = 0;

void demarrer(const char* nomDuModule, bool (*fonctionDeDemarrage)()) {
    if (!fonctionDeDemarrage()) {
        nombreDeModulesEnEchec++;
        ecrireDansLeJournal("[DEMARRAGE] Échec du module « %s » (les autres continuent).", nomDuModule);
    }
}

void afficherLEnTete() {
    ecrireDansLeJournal("══════════════════════════════════════════════");
    ecrireDansLeJournal("  Module moto %s — compilé le %s à %s", VERSION_DU_FIRMWARE, __DATE__, __TIME__);
    ecrireDansLeJournal("══════════════════════════════════════════════");
}

} // namespace

void setup() {
    Serial.begin(115200);
    preparerLeJournal();
    preparerLHorloge();

    // 1. Quelle carte avons-nous ? (les broches de la jauge et de la caméra en dépendent)
    detecterLaVersionDeLaCarte();

    // 2. Le stockage et les traces d'abord, pour garder la trace de tout le reste.
    demarrer("Carte SD", demarrerModuleCarteSD);
    demarrer("Journal",  demarrerModuleJournal);
    afficherLEnTete();
    ecrireDansLeJournal("[DEMARRAGE] Carte Waveshare %s (%s).", nomDeLaVersionDeCarte(), methodeDeDetectionDeLaCarte());
    demarrer("Système",  demarrerModuleSysteme);

    // 3. Ce qui est branché directement sur l'ESP32.
    demarrer("Batterie",  demarrerModuleBatterie);
    demarrer("NeoPixels", demarrerModuleNeopixels);
    demarrer("Bouton",    demarrerModuleBouton);
    demarrer("Caméra",    demarrerModuleCamera);
    demarrer("Bus CAN",   demarrerModuleCANBus);

    // 4. Le modem et tout ce qui passe par lui. Les abonnements aux notifications
    //    (SMS, appels) sont pris avant que le modem ne finisse de démarrer.
    demarrer("Modem",        demarrerModuleModem);
    demarrer("Horloge",      demarrerModuleHorloge);
    demarrer("Réseau 4G",    demarrerModuleReseau4G);
    demarrer("GPS",          demarrerModuleGPS);
    demarrer("SMS",          demarrerModuleSMS);
    demarrer("Appels",       demarrerModuleAppels);
    demarrer("Haut-parleur", demarrerModuleHautParleur);
    demarrer("Alertes SMS",  demarrerModuleAlertesSMS);

    // 5. La liaison avec l'application Android.
    demarrer("Explorateur de fichiers", demarrerModuleExplorateurFichiers);
    demarrer("Mise à jour",             demarrerModuleMiseAJour);
    demarrer("Wi-Fi",                   demarrerModuleWifi);
    demarrer("Appli Bluetooth",         demarrerModuleAppli);

    ecrireDansLeJournal("[DEMARRAGE] Terminé : %d tâche(s) lancée(s), %d module(s) en échec.",
                        nombreDeTachesSurveillees(), nombreDeModulesEnEchec);
}

void loop() {
    // Tout le travail est fait par les tâches FreeRTOS : la boucle principale n'a plus rien à faire.
    vTaskDelete(nullptr);
}
