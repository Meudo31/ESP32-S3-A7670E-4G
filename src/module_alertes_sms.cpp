#include <Preferences.h>
#include <math.h>

#include "configuration.h"
#include "module_alertes_sms.h"
#include "module_batterie.h"
#include "module_gps.h"
#include "module_journal.h"
#include "module_modem.h"
#include "module_reseau_4g.h"
#include "module_sms.h"
#include "module_systeme.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef SECRET_NUMERO_ALERTES
#define SECRET_NUMERO_ALERTES ""
#endif

namespace {

const double   DISTANCE_ENTRE_DEUX_ALERTES_KM     = 5.0;
const uint32_t PERTE_AVANT_NOUVELLE_POSITION_MS   = 5 * 60 * 1000;   // évite un SMS à chaque passage sous un pont
const uint32_t ATTENTE_MAXIMUM_DU_RESEAU_MS       = 3 * 60 * 1000;

String        numero;
volatile bool actives = true;
volatile bool testDemande = false;

void enregistrerLesReglages() {
    Preferences memoire;
    memoire.begin("alertes", false);
    memoire.putString("numero", numero);
    memoire.putBool("actives", actives);
    memoire.end();
}

// Distance « à vol d'oiseau » entre deux points (formule de haversine).
double distanceEnKm(double lat1, double lon1, double lat2, double lon2) {
    const double rayonDeLaTerreKm = 6371.0;
    const double dLat = radians(lat2 - lat1);
    const double dLon = radians(lon2 - lon1);
    const double a = sin(dLat / 2) * sin(dLat / 2) + cos(radians(lat1)) * cos(radians(lat2)) * sin(dLon / 2) * sin(dLon / 2);
    return 2 * rayonDeLaTerreKm * atan2(sqrt(a), sqrt(1 - a));
}

bool envoyer(const String& texte) {
    if (!actives || numero.length() < 4) return false;
    const bool demande = demanderLEnvoiDUnSMS(numero, texte);
    ecrireDansLeJournal("[ALERTES] SMS %s : %s", demande ? "envoyé" : "NON envoyé", texte.c_str());
    return demande;
}

bool reseauDisponible() {
    const EtatDuReseau4G reseau = etatDuReseau4G();
    return reseau.enregistrement == 1 || reseau.enregistrement == 5;
}

String messageDeDemarrage() {
    const EtatDeLaBatterie batterie = etatDeLaBatterie();
    const EtatDuReseau4G reseau = etatDuReseau4G();
    String texte = String("Moto ESP32 allumee (firmware ") + VERSION_DU_FIRMWARE + ").\n";
    if (batterie.jaugePresente) {
        texte += String("Batterie ") + (int)lroundf(batterie.chargePourcent) + " % (" + String(batterie.tensionVolts, 2) + " V)";
        texte += batterie.enCharge ? ", en charge" : "";
        texte += String(", alim. ") + nomDeLaSourceDAlimentation(batterie.source) + ".\n";
    } else {
        texte += "Batterie : jauge absente.\n";
    }
    texte += String("Reseau ") + reseau.operateur + " " + reseau.technologie + ", signal " + reseau.signalPourcent + " %.\n";
    texte += String("Redemarrage : ") + raisonDuDernierRedemarrage() + " (n. " + nombreDeDemarrages() + ").\n";
    const PositionGPS p = positionGPS();
    texte += p.positionValide ? String("Position : ") + lienGoogleMaps(p) : String("GPS : position en cours de recherche.");
    return texte;
}

void tacheDesAlertes(void*) {
    // 1. Message de démarrage, dès que le réseau est là.
    attendreQueLeModemSoitPret(portMAX_DELAY);
    const uint32_t debut = millis();
    while (!reseauDisponible() && millis() - debut < ATTENTE_MAXIMUM_DU_RESEAU_MS) vTaskDelay(pdMS_TO_TICKS(2000));
    vTaskDelay(pdMS_TO_TICKS(5000));   // laisse la jauge et le réseau se mettre à jour
    const PositionGPS auDemarrage = positionGPS();
    envoyer(messageDeDemarrage());

    // 2. Surveillance du GPS. Si le message de démarrage contenait déjà la position, elle sert de référence.
    bool     positionConnue = auDemarrage.positionValide;       // le GPS a une position en ce moment
    bool     referenceConnue = auDemarrage.positionValide;      // une position a déjà été envoyée
    double   latitudeEnvoyee = auDemarrage.latitude, longitudeEnvoyee = auDemarrage.longitude;
    uint32_t debutDeLaPerte = 0;
    bool     perteLongue = !auDemarrage.positionValide;         // sinon la première position est annoncée

    for (;;) {
        if (testDemande) {
            testDemande = false;
            envoyer(messageDeDemarrage());
        }
        const PositionGPS p = positionGPS();
        if (p.positionValide) {
            if (!positionConnue && perteLongue) {
                char texte[200];
                snprintf(texte, sizeof(texte), "Moto ESP32 : position GPS trouvee (%d satellites).\n%s",
                         p.satellitesUtilises, lienGoogleMaps(p).c_str());
                if (envoyer(texte)) {
                    latitudeEnvoyee = p.latitude; longitudeEnvoyee = p.longitude; referenceConnue = true;
                }
            } else if (referenceConnue) {
                const double distance = distanceEnKm(latitudeEnvoyee, longitudeEnvoyee, p.latitude, p.longitude);
                if (distance > DISTANCE_ENTRE_DEUX_ALERTES_KM) {
                    char texte[200];
                    snprintf(texte, sizeof(texte), "Moto ESP32 : deplacement de %.1f km, %.0f km/h.\n%s",
                             distance, p.vitesseKmh, lienGoogleMaps(p).c_str());
                    if (envoyer(texte)) { latitudeEnvoyee = p.latitude; longitudeEnvoyee = p.longitude; }
                }
            } else {
                latitudeEnvoyee = p.latitude; longitudeEnvoyee = p.longitude; referenceConnue = true;
            }
            positionConnue = true;
            perteLongue = false;
            debutDeLaPerte = 0;
        } else {
            if (positionConnue) debutDeLaPerte = millis();
            positionConnue = false;
            if (debutDeLaPerte != 0 && millis() - debutDeLaPerte >= PERTE_AVANT_NOUVELLE_POSITION_MS) perteLongue = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

} // namespace

void choisirLeNumeroDesAlertes(const String& nouveauNumero) {
    numero = nouveauNumero;
    numero.trim();
    enregistrerLesReglages();
    ecrireDansLeJournal("[ALERTES] Numéro des alertes %s.", numero.length() ? "enregistré" : "effacé");
}

String numeroDesAlertes() { return numero; }

void activerLesAlertes(bool nouvelEtat) {
    actives = nouvelEtat;
    enregistrerLesReglages();
}

bool alertesActives() { return actives; }

bool envoyerUneAlerteDeTest() {
    if (numero.length() < 4) return false;
    testDemande = true;
    return true;
}

bool demarrerModuleAlertesSMS() {
    Preferences memoire;
    memoire.begin("alertes", true);
    numero  = memoire.getString("numero", SECRET_NUMERO_ALERTES);
    actives = memoire.getBool("actives", true);
    memoire.end();
    return creerTacheSurveillee(tacheDesAlertes, "Alertes", 4096, PRIORITE_BASSE, COEUR_TRAITEMENTS);
}
