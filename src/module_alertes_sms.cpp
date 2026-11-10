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
volatile bool actives = true;                 // interrupteur général
volatile bool alerteDeDemarrage = true;       // SMS à l'allumage
volatile bool alerteDePosition = true;        // SMS quand le GPS trouve une position
volatile bool alerteDeDeplacement = true;     // SMS à chaque déplacement de plus de 5 km
volatile bool testDemande = false;
volatile bool referenceAOublier = false;      // les alertes reviennent : repartir de la position actuelle

void enregistrerLesReglages() {
    Preferences memoire;
    memoire.begin("alertes", false);
    memoire.putString("numero", numero);
    memoire.putBool("actives", actives);
    memoire.putBool("demarrage", alerteDeDemarrage);
    memoire.putBool("position", alerteDePosition);
    memoire.putBool("deplacement", alerteDeDeplacement);
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

String lienMaps(const PositionGPS& p) {
    char lien[80];
    snprintf(lien, sizeof(lien), "https://maps.google.com/?q=%.6f,%.6f", p.latitude, p.longitude);
    return lien;
}

// « type » = l'interrupteur de ce genre d'alerte ; « forcer » = envoyer même alertes coupées (test, réponse).
bool envoyer(const String& texte, bool type = true, bool forcer = false) {
    if (numero.length() < 4) return false;
    if (!forcer && (!actives || !type)) return false;
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
    texte += p.positionValide ? String("Position : ") + lienMaps(p) : String("GPS : position en cours de recherche.");
    return texte;
}

void tacheDesAlertes(void*) {
    // 1. Message de démarrage, dès que le réseau est là.
    attendreQueLeModemSoitPret(portMAX_DELAY);
    const uint32_t debut = millis();
    while (!reseauDisponible() && millis() - debut < ATTENTE_MAXIMUM_DU_RESEAU_MS) vTaskDelay(pdMS_TO_TICKS(2000));
    vTaskDelay(pdMS_TO_TICKS(5000));   // laisse la jauge et le réseau se mettre à jour
    const PositionGPS auDemarrage = positionGPS();
    envoyer(messageDeDemarrage(), alerteDeDemarrage);

    // 2. Surveillance du GPS. Si le message de démarrage contenait déjà la position, elle sert de référence.
    bool     positionConnue = auDemarrage.positionValide;       // le GPS a une position en ce moment
    bool     referenceConnue = auDemarrage.positionValide;      // une position a déjà été envoyée
    double   latitudeEnvoyee = auDemarrage.latitude, longitudeEnvoyee = auDemarrage.longitude;
    uint32_t debutDeLaPerte = 0;
    bool     perteLongue = !auDemarrage.positionValide;         // sinon la première position est annoncée

    for (;;) {
        if (testDemande) {
            testDemande = false;
            envoyer(messageDeDemarrage(), true, true);   // le test part même si les alertes sont coupées
        }
        const PositionGPS p = positionGPS();
        // Alertes coupées puis remises : la position de référence est l'actuelle, sinon le premier
        // déplacement mesuré compterait tout le trajet fait pendant la coupure.
        if (referenceAOublier) {
            referenceAOublier = false;
            referenceConnue = false;
        }
        if (p.positionValide) {
            if (!positionConnue && perteLongue) {
                char texte[200];
                snprintf(texte, sizeof(texte), "Moto ESP32 : position GPS trouvee (%d satellites).\n%s",
                         p.satellitesUtilises, lienMaps(p).c_str());
                envoyer(texte, alerteDePosition);
                // Même si ce SMS n'est pas parti (alertes coupées), cette position devient la référence.
                latitudeEnvoyee = p.latitude; longitudeEnvoyee = p.longitude; referenceConnue = true;
            } else if (referenceConnue) {
                const double distance = distanceEnKm(latitudeEnvoyee, longitudeEnvoyee, p.latitude, p.longitude);
                if (distance > DISTANCE_ENTRE_DEUX_ALERTES_KM) {
                    char texte[200];
                    snprintf(texte, sizeof(texte), "Moto ESP32 : deplacement de %.1f km, %.0f km/h.\n%s",
                             distance, p.vitesseKmh, lienMaps(p).c_str());
                    envoyer(texte, alerteDeDeplacement);
                    latitudeEnvoyee = p.latitude; longitudeEnvoyee = p.longitude;
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
    if (nouvelEtat && !actives) referenceAOublier = true;
    actives = nouvelEtat;
    enregistrerLesReglages();
    ecrireDansLeJournal("[ALERTES] Alertes SMS %s.", actives ? "activées" : "coupées");
}

bool alertesActives() { return actives; }

void activerUneAlerte(TypeDAlerte type, bool nouvelEtat) {
    switch (type) {
        case TypeDAlerte::DEMARRAGE:   alerteDeDemarrage = nouvelEtat; break;
        case TypeDAlerte::POSITION:    alerteDePosition = nouvelEtat; break;
        case TypeDAlerte::DEPLACEMENT:
            if (nouvelEtat && !alerteDeDeplacement) referenceAOublier = true;
            alerteDeDeplacement = nouvelEtat;
            break;
    }
    enregistrerLesReglages();
}

bool uneAlerteEstActive(TypeDAlerte type) {
    switch (type) {
        case TypeDAlerte::DEMARRAGE:   return alerteDeDemarrage;
        case TypeDAlerte::POSITION:    return alerteDePosition;
        case TypeDAlerte::DEPLACEMENT: return alerteDeDeplacement;
    }
    return false;
}

namespace {

// Garde seulement les chiffres utiles d'un numéro : « +33 6 12… » et « 06 12… » donnent la même fin.
String finDuNumero(const String& brut) {
    String chiffres;
    for (size_t i = 0; i < brut.length(); i++) if (isDigit(brut[i])) chiffres += brut[i];
    return chiffres.length() > 9 ? chiffres.substring(chiffres.length() - 9) : chiffres;
}

// Texte en minuscules, sans espaces autour ni accents courants (« Arrêt » -> « arret »).
String simplifier(String texte) {
    texte.trim();
    texte.toLowerCase();
    texte.replace("ê", "e"); texte.replace("Ê", "e"); texte.replace("é", "e"); texte.replace("É", "e");
    texte.replace("è", "e"); texte.replace("à", "a");
    while (texte.endsWith(".") || texte.endsWith("!")) texte.remove(texte.length() - 1);
    texte.trim();
    return texte;
}

} // namespace

bool traiterUnSMSDeCommandeDesAlertes(const String& expediteur, const String& texte) {
    if (numero.length() < 4 || expediteur.length() < 4) return false;
    if (finDuNumero(expediteur) != finDuNumero(numero)) return false;   // seul le numéro des alertes commande

    const String mot = simplifier(texte);
    if (mot == "stop" || mot == "arret" || mot == "alertes off" || mot == "alertes 0" || mot == "sms off") {
        activerLesAlertes(false);
        envoyer("Moto ESP32 : alertes SMS automatiques coupees. Envoyez \"Marche\" pour les remettre.", true, true);
        return true;
    }
    if (mot == "marche" || mot == "start" || mot == "alertes on" || mot == "alertes 1" || mot == "sms on") {
        activerLesAlertes(true);
        envoyer("Moto ESP32 : alertes SMS automatiques activees. Envoyez \"Stop\" pour les couper.", true, true);
        return true;
    }
    return false;
}

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
    alerteDeDemarrage   = memoire.getBool("demarrage", true);
    alerteDePosition    = memoire.getBool("position", true);
    alerteDeDeplacement = memoire.getBool("deplacement", true);
    memoire.end();
    return creerTacheSurveillee(tacheDesAlertes, "Alertes", 4096, PRIORITE_BASSE, COEUR_TRAITEMENTS);
}
