#include <Wire.h>

#include "configuration.h"
#include "module_carte.h"

namespace {

const uint8_t ADRESSE_I2C_JAUGE_MAX17048 = 0x36;

const BrochesDeLaCarte BROCHES_V1 = {
    /* jauge  */ 3, 2,
    /* caméra */ 34, 15, 16,
    /* Y2..Y9 */ 7, 8, 9, 10, 11, 12, 13, 14,
    /* VSYNC, HREF, PCLK */ 36, 35, 37,
    /* partage du bus I2C */ false,
};

const BrochesDeLaCarte BROCHES_V2 = {
    /* jauge  */ 15, 16,
    /* caméra */ 39, 15, 16,
    /* Y2..Y9 */ 7, 8, 9, 10, 11, 12, 13, 14,
    /* VSYNC, HREF, PCLK */ 42, 41, 46,
    /* partage du bus I2C */ true,
};

VersionDeCarte versionDetectee = VersionDeCarte::INCONNUE;
const char*    methodeUtilisee = "pas encore détectée";

bool jaugeDeBatteriePresenteSur(int brocheSDA, int brocheSCL) {
    Wire.begin(brocheSDA, brocheSCL);
    Wire.beginTransmission(ADRESSE_I2C_JAUGE_MAX17048);
    const bool presente = (Wire.endTransmission() == 0);
    Wire.end();
    return presente;
}

} // namespace

VersionDeCarte detecterLaVersionDeLaCarte() {
    if (jaugeDeBatteriePresenteSur(BROCHES_V2.jaugeSDA, BROCHES_V2.jaugeSCL)) {
        versionDetectee = VersionDeCarte::V2;
        methodeUtilisee = "jauge de batterie trouvée sur GPIO 15/16";
    }
    else if (jaugeDeBatteriePresenteSur(BROCHES_V1.jaugeSDA, BROCHES_V1.jaugeSCL)) {
        versionDetectee = VersionDeCarte::V1;
        methodeUtilisee = "jauge de batterie trouvée sur GPIO 3/2";
    }
    else if (ESP.getPsramSize() >= 6 * 1024 * 1024) {
        versionDetectee = VersionDeCarte::V2;
        methodeUtilisee = "pas de jauge, mais PSRAM de 8 Mo (puce R8)";
    }
    else if (ESP.getPsramSize() > 0) {
        versionDetectee = VersionDeCarte::V1;
        methodeUtilisee = "pas de jauge, PSRAM de 2 Mo (puce R2)";
    }
    else {
        versionDetectee = VersionDeCarte::INCONNUE;
        methodeUtilisee = "ni jauge ni PSRAM : broches V2 utilisées par défaut";
    }
    return versionDetectee;
}

VersionDeCarte versionDeLaCarte() { return versionDetectee; }

const char* nomDeLaVersionDeCarte() {
    switch (versionDetectee) {
        case VersionDeCarte::V1: return "V1";
        case VersionDeCarte::V2: return "V2";
        default:                 return "inconnue";
    }
}

const char* methodeDeDetectionDeLaCarte() { return methodeUtilisee; }

const BrochesDeLaCarte& brochesDeLaCarte() {
    return (versionDetectee == VersionDeCarte::V1) ? BROCHES_V1 : BROCHES_V2;
}
