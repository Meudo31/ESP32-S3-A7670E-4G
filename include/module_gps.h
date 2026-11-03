#ifndef MODULE_GPS_H
#define MODULE_GPS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE GPS : position par le récepteur GNSS du modem (GPS, GLONASS, Galileo, BeiDou).
   La tâche allume le récepteur puis lui demande la position chaque seconde
   (commande AT+CGNSSINFO). La première position valide remet l'horloge à l'heure.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

struct PositionGPS {
    bool     positionValide;
    int      typeDePosition;        // 2 = 2D (sans altitude), 3 = 3D
    double   latitude, longitude;   // degrés décimaux
    float    altitudeMetres;
    float    vitesseKmh;
    float    capDegres;             // 0 = nord, 90 = est
    float    PDOP, HDOP, VDOP;      // précision : plus c'est petit, mieux c'est (< 2 excellent, > 5 médiocre)
    int      satellitesGPS, satellitesGLONASS, satellitesGALILEO, satellitesBEIDOU;
    int      satellitesUtilises;
    time_t   heureUTC;              // heure donnée par les satellites
    float    vitesseMaximumKmh;     // depuis le démarrage
    uint32_t miseAJourMs;
};

PositionGPS positionGPS();

// Demande une nouvelle position tout de suite.
void actualiserLaPositionGPS();

void decrireLaPositionGPS(JsonDocument& document);

bool demarrerModuleGPS();

#endif // MODULE_GPS_H
