#include "configuration.h"
#include "module_gps.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_modem.h"
#include "module_systeme.h"
#include "outils_texte.h"

namespace {

PositionGPS       positionActuelle = {};
SemaphoreHandle_t verrouDeLaPosition = nullptr;
TaskHandle_t      tacheDuGPS = nullptr;

const float    NOEUDS_VERS_KMH = 1.852f;
const uint32_t INTERVALLE_DE_LECTURE_MS = 1000;
const uint32_t INTERVALLE_DE_MISE_A_L_HEURE_MS = 10 * UNE_MINUTE_MS;

// Selon la version de son logiciel, le modem donne la position de deux façons :
//   - degrés et minutes  "4849.123456" (4 chiffres avant le point, 5 pour la longitude) → 48.818724 ;
//   - degrés décimaux    "48.818724"   (l'A7670E de Meudo répond ainsi).
// On les reconnaît au nombre de chiffres avant le point.
double convertirEnDegresDecimaux(String texte, const String& hemisphere, int chiffresDesDegres) {
    texte.trim();
    const double valeur = texte.toDouble();
    int chiffresAvantLePoint = texte.indexOf('.');
    if (chiffresAvantLePoint < 0) chiffresAvantLePoint = texte.length();
    double resultat = valeur;
    if (chiffresAvantLePoint > chiffresDesDegres) {
        const int    degres  = (int)(valeur / 100);
        const double minutes = valeur - degres * 100;
        resultat = degres + minutes / 60.0;
    }
    if (hemisphere == "S" || hemisphere == "W") resultat = -resultat;
    return resultat;
}

// "031126" + "141509.0"  →  secondes UTC depuis 1970
time_t convertirLaDateEtLHeure(const String& date, const String& heure) {
    if (date.length() < 6 || heure.length() < 6) return 0;
    return convertirUneDateUTCEnSecondes(2000 + date.substring(4, 6).toInt(), date.substring(2, 4).toInt(),
                                         date.substring(0, 2).toInt(), heure.substring(0, 2).toInt(),
                                         heure.substring(2, 4).toInt(), heure.substring(4, 6).toInt());
}

/* Réponse du modem (sans position) :  +CGNSSINFO: ,,,,,,,,,,,,,,,
   Avec position :
     +CGNSSINFO: 3,12,05,04,01,4849.123456,N,00204.567890,E,031126,141509.0,142.5,28.5,180.0,1.2,0.9,0.8
                 │ └──┴──┴──┴─ satellites par constellation (leur nombre varie selon le logiciel du modem)
                 └─ type de position (2 = 2D, 3 = 3D)
   On repère donc le champ « N » ou « S » et on lit tout le reste par rapport à lui. */
bool lireLaReponseDuGPS(const String& reponse, PositionGPS& position) {
    const int debut = reponse.indexOf("+CGNSSINFO:");
    if (debut < 0) return false;
    int fin = reponse.indexOf('\n', debut);
    if (fin < 0) fin = reponse.length();
    String champs[24];
    const int nombre = decouperLesChamps(reponse.substring(debut + 11, fin), champs, 24);

    int champHemisphere = -1;
    for (int numero = 2; numero < nombre; numero++) {
        if (champs[numero] == "N" || champs[numero] == "S") { champHemisphere = numero; break; }
    }
    if (champHemisphere < 0 || champHemisphere + 10 >= nombre) {
        position.positionValide = false;
        return true;
    }

    const int nombreDeConstellations = champHemisphere - 2;    // champs entre le type et la latitude
    position.satellitesGPS     = nombreDeConstellations >= 1 ? champs[1].toInt() : 0;
    position.satellitesGLONASS = nombreDeConstellations >= 2 ? champs[2].toInt() : 0;
    if (nombreDeConstellations >= 4) {
        position.satellitesGALILEO = champs[3].toInt();
        position.satellitesBEIDOU  = champs[4].toInt();
    } else {
        position.satellitesGALILEO = 0;
        position.satellitesBEIDOU  = nombreDeConstellations >= 3 ? champs[3].toInt() : 0;
    }
    position.satellitesUtilises = position.satellitesGPS + position.satellitesGLONASS
                                + position.satellitesGALILEO + position.satellitesBEIDOU;

    const int h = champHemisphere;
    position.typeDePosition = champs[0].toInt();
    position.latitude       = convertirEnDegresDecimaux(champs[h - 1], champs[h], 2);
    position.longitude      = convertirEnDegresDecimaux(champs[h + 1], champs[h + 2], 3);
    position.heureUTC       = convertirLaDateEtLHeure(champs[h + 3], champs[h + 4]);
    position.altitudeMetres = champs[h + 5].toFloat();
    position.vitesseKmh     = champs[h + 6].toFloat() * NOEUDS_VERS_KMH;
    position.capDegres      = champs[h + 7].toFloat();
    position.PDOP           = champs[h + 8].toFloat();
    position.HDOP           = champs[h + 9].toFloat();
    position.VDOP           = champs[h + 10].toFloat();
    position.positionValide = true;
    if (position.vitesseKmh > position.vitesseMaximumKmh) position.vitesseMaximumKmh = position.vitesseKmh;
    return true;
}

void allumerLeRecepteurGPS() {
    if (envoyerCommandeAT("AT+CGNSSPWR=1", nullptr, 5000) == ResultatAT::OK) {
        ecrireDansLeJournal("[GPS] Récepteur allumé. Première position : quelques minutes à ciel ouvert.");
    } else {
        ecrireDansLeJournal("[GPS] Le modem refuse d'allumer le récepteur GNSS.");
    }
}

void tacheGPS(void*) {
    attendreQueLeModemSoitPret(portMAX_DELAY);
    allumerLeRecepteurGPS();
    bool     positionValidePrecedente = false;
    uint32_t derniereMiseALHeure = 0;
    bool     horlogeDejaReglee = false;

    for (;;) {
        String reponse;
        PositionGPS nouvellePosition = positionGPS();
        if (envoyerCommandeAT("AT+CGNSSINFO", &reponse, 2000) == ResultatAT::OK
            && lireLaReponseDuGPS(reponse, nouvellePosition)) {
            nouvellePosition.miseAJourMs = millis();
            if (nouvellePosition.positionValide) nouvellePosition.dernierePositionValideMs = nouvellePosition.miseAJourMs;
            xSemaphoreTake(verrouDeLaPosition, portMAX_DELAY);
            positionActuelle = nouvellePosition;
            xSemaphoreGive(verrouDeLaPosition);

            if (nouvellePosition.positionValide != positionValidePrecedente) {
                positionValidePrecedente = nouvellePosition.positionValide;
                if (nouvellePosition.positionValide) {
                    ecrireDansLeJournal("[GPS] Position trouvée : %.6f, %.6f (%d satellites).", nouvellePosition.latitude,
                                        nouvellePosition.longitude, nouvellePosition.satellitesUtilises);
                    // La réponse brute du modem, pour vérifier le format des coordonnées.
                    String brut = reponse.substring(max(0, reponse.indexOf("+CGNSSINFO:")));
                    brut.trim();
                    ecrireDansLeJournal("[GPS] Réponse du modem : %s", brut.c_str());
                } else {
                    ecrireDansLeJournal("[GPS] Position perdue.");
                }
            }
            const bool lHeureDoitEtreReglee = !horlogeDejaReglee || millis() - derniereMiseALHeure > INTERVALLE_DE_MISE_A_L_HEURE_MS;
            if (nouvellePosition.positionValide && nouvellePosition.heureUTC > 0 && lHeureDoitEtreReglee) {
                mettreLHorlogeALHeure(nouvellePosition.heureUTC, SourceDeLHeure::GPS);
                derniereMiseALHeure = millis();
                horlogeDejaReglee = true;
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(INTERVALLE_DE_LECTURE_MS));
    }
}

} // namespace

PositionGPS positionGPS() {
    xSemaphoreTake(verrouDeLaPosition, portMAX_DELAY);
    PositionGPS copie = positionActuelle;
    xSemaphoreGive(verrouDeLaPosition);
    return copie;
}

void actualiserLaPositionGPS() {
    if (tacheDuGPS != nullptr) xTaskNotifyGive(tacheDuGPS);
}

String lienGoogleMaps(const PositionGPS& position) {
    char lien[80];
    snprintf(lien, sizeof(lien), "https://maps.google.com/?q=%.6f,%.6f", position.latitude, position.longitude);
    return lien;
}

void decrireLaPositionGPS(JsonDocument& document) {
    const PositionGPS p = positionGPS();
    document["t"]     = "gps";
    document["fix"]   = p.positionValide ? 1 : 0;
    document["type"]  = p.typeDePosition;
    document["lat"]   = serialized(String(p.latitude, 6));
    document["lon"]   = serialized(String(p.longitude, 6));
    document["alt"]   = p.altitudeMetres;
    document["vit"]   = p.vitesseKmh;
    document["vmax"]  = p.vitesseMaximumKmh;
    document["cap"]   = p.capDegres;
    document["pdop"]  = p.PDOP;
    document["hdop"]  = p.HDOP;
    document["vdop"]  = p.VDOP;
    document["s_gps"] = p.satellitesGPS;
    document["s_glo"] = p.satellitesGLONASS;
    document["s_gal"] = p.satellitesGALILEO;
    document["s_bds"] = p.satellitesBEIDOU;
    document["sat"]   = p.satellitesUtilises;
    document["utc"]   = (uint32_t)p.heureUTC;
    document["age"]   = p.miseAJourMs ? (millis() - p.miseAJourMs) / 1000 : -1;
}

bool demarrerModuleGPS() {
    verrouDeLaPosition = xSemaphoreCreateMutex();
    if (verrouDeLaPosition == nullptr) return false;
    return creerTacheSurveillee(tacheGPS, "GPS", 5120, PRIORITE_BASSE, COEUR_COMMUNICATIONS, &tacheDuGPS);
}
