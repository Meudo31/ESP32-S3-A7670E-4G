#include <sys/time.h>

#include "configuration.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_modem.h"
#include "module_systeme.h"

namespace {

volatile SourceDeLHeure sourceActuelle = SourceDeLHeure::AUCUNE;

String formaterLHeureLocale(const char* modele) {
    time_t maintenant = time(nullptr);
    struct tm heureLocale;
    localtime_r(&maintenant, &heureLocale);
    char texte[40];
    strftime(texte, sizeof(texte), modele, &heureLocale);
    return String(texte);
}

// Réponse du modem : +CCLK: "26/11/03,14:05:09+04"  (heure locale, fuseau en quarts d'heure)
bool lireLHeureDuReseau() {
    String reponse;
    if (envoyerCommandeAT("AT+CCLK?", &reponse, 2000) != ResultatAT::OK) return false;
    int annee, mois, jour, heure, minute, seconde, quartsDHeure = 0;
    char signe = '+';
    const int debut = reponse.indexOf('"');
    if (debut < 0) return false;
    const int lus = sscanf(reponse.c_str() + debut + 1, "%d/%d/%d,%d:%d:%d%c%d",
                           &annee, &mois, &jour, &heure, &minute, &seconde, &signe, &quartsDHeure);
    if (lus < 6 || annee < 24 || annee == 70 || annee == 80) return false;   // 70/80 = horloge du modem pas encore réglée
    time_t secondesUTC = convertirUneDateUTCEnSecondes(2000 + annee, mois, jour, heure, minute, seconde);
    const int decalageSecondes = quartsDHeure * 15 * 60;
    secondesUTC += (signe == '-') ? decalageSecondes : -decalageSecondes;
    mettreLHorlogeALHeure(secondesUTC, SourceDeLHeure::RESEAU_4G);
    return true;
}

void tacheHorloge(void*) {
    attendreQueLeModemSoitPret(portMAX_DELAY);
    envoyerCommandeAT("AT+CTZU=1");   // le modem prend l'heure du réseau automatiquement
    for (;;) {
        if (sourceActuelle == SourceDeLHeure::AUCUNE || sourceActuelle == SourceDeLHeure::RESEAU_4G) {
            lireLHeureDuReseau();
        }
        const uint32_t attente = lHeureEstConnue() ? 60 * UNE_MINUTE_MS : 30 * UNE_SECONDE_MS;
        vTaskDelay(pdMS_TO_TICKS(attente));
    }
}

} // namespace

// Convertit une date du calendrier UTC en secondes depuis 1970 (sans dépendre du fuseau horaire).
time_t convertirUneDateUTCEnSecondes(int annee, int mois, int jour, int heure, int minute, int seconde) {
    // Algorithme de Howard Hinnant (jours depuis le 1er janvier 1970)
    annee -= (mois <= 2);
    const int ere = (annee >= 0 ? annee : annee - 399) / 400;
    const unsigned anneeDeLEre = (unsigned)(annee - ere * 400);
    const unsigned jourDeLAnnee = (153 * (mois + (mois > 2 ? -3 : 9)) + 2) / 5 + jour - 1;
    const unsigned jourDeLEre = anneeDeLEre * 365 + anneeDeLEre / 4 - anneeDeLEre / 100 + jourDeLAnnee;
    const long jours = (long)ere * 146097 + (long)jourDeLEre - 719468;
    return (time_t)jours * 86400 + heure * 3600 + minute * 60 + seconde;
}


void preparerLHorloge() {
    setenv("TZ", FUSEAU_HORAIRE, 1);
    tzset();
}

bool lHeureEstConnue() { return sourceActuelle != SourceDeLHeure::AUCUNE; }

SourceDeLHeure sourceDeLHeure() { return sourceActuelle; }

const char* nomDeLaSourceDeLHeure() {
    switch (sourceActuelle) {
        case SourceDeLHeure::RESEAU_4G: return "réseau 4G";
        case SourceDeLHeure::GPS:       return "GPS";
        case SourceDeLHeure::TELEPHONE: return "téléphone";
        default:                        return "inconnue";
    }
}

void mettreLHorlogeALHeure(time_t secondesUTC, SourceDeLHeure source) {
    if (secondesUTC < 1700000000) return;   // date absurde (avant 2023) : on l'ignore
    const bool premiereFois = !lHeureEstConnue();
    struct timeval nouvelleHeure = { secondesUTC, 0 };
    settimeofday(&nouvelleHeure, nullptr);
    sourceActuelle = source;
    if (premiereFois) {
        ecrireDansLeJournal("[HORLOGE] Heure réglée par : %s (%s).", nomDeLaSourceDeLHeure(),
                            formaterLHeureLocale("%d/%m/%Y %H:%M:%S").c_str());
    }
}

String dateDuJourPourNomDeFichier()  { return formaterLHeureLocale("%d-%m-%Y"); }
String dateEtHeurePourNomDeFichier() { return formaterLHeureLocale("%d-%m-%Y_%H-%M-%S"); }
String heurePourLeJournal()          { return formaterLHeureLocale("%H:%M:%S"); }

bool demarrerModuleHorloge() {
    return creerTacheSurveillee(tacheHorloge, "Horloge", 3072, PRIORITE_BASSE, COEUR_COMMUNICATIONS);
}
