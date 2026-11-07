#include <math.h>

#include "configuration.h"
#include "module_commandes_sms.h"
#include "module_alertes_sms.h"
#include "module_batterie.h"
#include "module_camera.h"
#include "module_envoi_internet.h"
#include "module_gps.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_modem.h"
#include "module_reseau_4g.h"
#include "module_sms.h"
#include "module_systeme.h"
#include "outils_texte.h"

namespace {

enum class CommandeSMS : uint8_t { AUCUNE, PHOTO, GPS, ETAT, AIDE };

struct DemandeDeCommande {
    CommandeSMS commande;
    char        expediteur[24];
};

QueueHandle_t fileDesCommandes = nullptr;

// ─────────────────────────────── Reconnaissance ───────────────────────────────

CommandeSMS reconnaitreLaCommande(const String& texte) {
    String mot = texteSansAccentPourSMS(texte);   // « État » → « Etat »
    mot.trim();
    mot.toLowerCase();
    while (mot.endsWith(".") || mot.endsWith("!") || mot.endsWith("?")) {
        mot.remove(mot.length() - 1);
        mot.trim();
    }
    if (mot == "photo")                 return CommandeSMS::PHOTO;
    if (mot == "gps" || mot == "position") return CommandeSMS::GPS;
    if (mot == "etat" || mot == "status") return CommandeSMS::ETAT;
    if (mot == "aide" || mot == "help") return CommandeSMS::AIDE;
    return CommandeSMS::AUCUNE;
}

// Garde les chiffres d'un numéro : "+33 6 12-34" → "3361234".
String chiffresDuNumero(const String& numero) {
    String chiffres;
    for (size_t position = 0; position < numero.length(); position++) {
        if (isdigit((unsigned char)numero[position])) chiffres += numero[position];
    }
    return chiffres;
}

// "+33612345678" et "0612345678" désignent le même téléphone : on compare les 9 derniers chiffres.
bool memeTelephone(const String& numero1, const String& numero2) {
    const String a = chiffresDuNumero(numero1), b = chiffresDuNumero(numero2);
    if (a.length() < 6 || b.length() < 6) return false;
    const size_t longueur = min((size_t)9, min(a.length(), b.length()));
    return a.substring(a.length() - longueur) == b.substring(b.length() - longueur);
}

// ─────────────────────────────── Réponses ───────────────────────────────

void repondre(const char* expediteur, const String& texte) {
    const String texteSMS = texteSansAccentPourSMS(texte);
    const bool demande = demanderLEnvoiDUnSMS(expediteur, texteSMS);
    if (!demande) ecrireDansLeJournal("[COMMANDES] Réponse NON envoyée (file des SMS pleine ?).");
}

String depuis(uint32_t instantMs) {
    const uint32_t secondes = (millis() - instantMs) / 1000;
    if (secondes < 60)   return String(secondes) + " s";
    if (secondes < 3600) return String(secondes / 60) + " min";
    return String(secondes / 3600) + " h " + String((secondes / 60) % 60) + " min";
}

String texteGPS() {
    actualiserLaPositionGPS();
    vTaskDelay(pdMS_TO_TICKS(1500));   // laisse le GPS lire une position toute fraîche
    const PositionGPS p = positionGPS();
    char ligne[120];
    if (p.positionValide) {
        String texte = "Position moto :\n" + lienGoogleMaps(p) + "\n";
        snprintf(ligne, sizeof(ligne), "Vitesse %.0f km/h, cap %.0f, alt. %.0f m, %d satellites (HDOP %.1f).",
                 p.vitesseKmh, p.capDegres, p.altitudeMetres, p.satellitesUtilises, p.HDOP);
        return texte + ligne;
    }
    if (p.dernierePositionValideMs != 0) {
        return "GPS : pas de position en ce moment (" + String(p.satellitesUtilises) + " satellites).\n"
               "Derniere position connue, il y a " + depuis(p.dernierePositionValideMs) + " :\n" + lienGoogleMaps(p);
    }
    return "GPS : aucune position depuis le demarrage (" + String(p.satellitesUtilises)
         + " satellites). Il faut quelques minutes a ciel ouvert.";
}

String texteEtat() {
    String texte = String("Etat moto ESP32 (firmware ") + VERSION_DU_FIRMWARE + ")\n";

    const EtatDeLaBatterie batterie = etatDeLaBatterie();
    char ligne[160];
    if (batterie.jaugePresente) {
        snprintf(ligne, sizeof(ligne), "Batterie %d %% (%.2f V)%s, alim. %s", (int)lroundf(batterie.chargePourcent),
                 batterie.tensionVolts, batterie.enCharge ? ", en charge" : "", nomDeLaSourceDAlimentation(batterie.source));
        texte += ligne;
        if (!batterie.enCharge && batterie.autonomieHeures > 0) {
            snprintf(ligne, sizeof(ligne), ", autonomie ~%.0f h", batterie.autonomieHeures);
            texte += ligne;
        }
        texte += ".\n";
    } else {
        texte += "Batterie : jauge absente.\n";
    }

    const EtatDuReseau4G reseau = etatDuReseau4G();
    const char* inscription = reseau.enregistrement == 1 ? "inscrit"
                            : reseau.enregistrement == 5 ? "itinerance"
                            : reseau.enregistrement == 2 ? "recherche"
                            : reseau.enregistrement == 3 ? "refuse" : "non inscrit";
    snprintf(ligne, sizeof(ligne), "Modem : %s %s, %s, signal %d %% (%d dBm), SIM %s.\n",
             reseau.operateur.length() ? reseau.operateur.c_str() : "?", reseau.technologie.c_str(), inscription,
             reseau.signalPourcent, reseau.signalDbm, reseau.etatSim.c_str());
    texte += ligne;

    const PositionGPS p = positionGPS();
    texte += p.positionValide ? String("GPS : position OK, ") + p.satellitesUtilises + " satellites.\n"
                              : String("GPS : pas de position (") + p.satellitesUtilises + " satellites).\n";

    texte += String("Allume depuis ") + depuis(0) + ", demarrage n." + nombreDeDemarrages() + " ("
           + raisonDuDernierRedemarrage() + ").\n";

    // Dernières lignes du journal, raccourcies pour limiter le nombre de SMS.
    texte += "Journal :\n";
    String lignes = dernieresLignesDuJournal(LIGNES_DU_JOURNAL_DANS_ETAT);
    int debut = 0;
    while (debut < (int)lignes.length()) {
        int fin = lignes.indexOf('\n', debut);
        if (fin < 0) fin = lignes.length();
        String uneLigne = lignes.substring(debut, fin);
        uneLigne.replace("  ", " ");
        if (uneLigne.length() > 90) uneLigne = uneLigne.substring(0, 87) + "...";
        texte += uneLigne + "\n";
        debut = fin + 1;
    }
    return texte;
}

void executerPhoto(const char* expediteur) {
    PhotoAEnvoyer photo;
    if (!prendreUnePhotoAEnvoyer(photo, TAILLE_MAXIMUM_PHOTO_PAR_SMS)) {
        repondre(expediteur, "Photo impossible : " + photo.erreur + ".");
        return;
    }
    const String nomDuFichier = (lHeureEstConnue() ? dateEtHeurePourNomDeFichier() : String("photo")) + "_moto.jpg";
    String lien, erreur;
    const bool deposee = deposerUnFichierSurInternet(photo.jpeg, photo.taille, nomDuFichier.c_str(), "image/jpeg", lien, erreur);
    free(photo.jpeg);

    if (deposee) {
        repondre(expediteur, String("Photo moto (valable ") + DUREE_DE_VIE_DES_PHOTOS + ") :\n" + lien);
    } else {
        String texte = "Photo prise mais envoi impossible : " + erreur + ".";
        if (photo.chemin.length()) texte += "\nEnregistree sur la carte SD : " + photo.chemin;
        repondre(expediteur, texte);
    }
}

void executer(const DemandeDeCommande& demande) {
    switch (demande.commande) {
        case CommandeSMS::PHOTO: executerPhoto(demande.expediteur); break;
        case CommandeSMS::GPS:   repondre(demande.expediteur, texteGPS()); break;
        case CommandeSMS::ETAT:  repondre(demande.expediteur, texteEtat()); break;
        case CommandeSMS::AIDE:
            repondre(demande.expediteur, "Commandes : Photo (lien vers une photo), Gps (position et carte), "
                                         "Etat (batterie, modem, journal), Aide.");
            break;
        default: break;
    }
}

const char* nomDeLaCommande(CommandeSMS commande) {
    switch (commande) {
        case CommandeSMS::PHOTO: return "Photo";
        case CommandeSMS::GPS:   return "Gps";
        case CommandeSMS::ETAT:  return "Etat";
        case CommandeSMS::AIDE:  return "Aide";
        default:                 return "?";
    }
}

void tacheDesCommandes(void*) {
    DemandeDeCommande demande;
    for (;;) {
        if (xQueueReceive(fileDesCommandes, &demande, portMAX_DELAY) != pdPASS) continue;
        ecrireDansLeJournal("[COMMANDES] Commande « %s » reçue par SMS.", nomDeLaCommande(demande.commande));
        executer(demande);
    }
}

} // namespace

bool transmettreUnSMSAuxCommandes(const String& expediteur, const String& texte) {
    const CommandeSMS commande = reconnaitreLaCommande(texte);
    if (commande == CommandeSMS::AUCUNE || fileDesCommandes == nullptr) return false;

    if (!memeTelephone(expediteur, numeroDesAlertes())) {
        ecrireDansLeJournal("[COMMANDES] Commande « %s » refusée : %s n'est pas le numéro des alertes%s.", texte.c_str(),
                            expediteur.c_str(), numeroDesAlertes().length() ? "" : " (aucun numéro réglé dans l'appli)");
        return true;
    }
    DemandeDeCommande demande = {};
    demande.commande = commande;
    strlcpy(demande.expediteur, expediteur.c_str(), sizeof(demande.expediteur));
    if (xQueueSend(fileDesCommandes, &demande, 0) != pdPASS) {
        ecrireDansLeJournal("[COMMANDES] Trop de commandes en attente : « %s » ignorée.", texte.c_str());
    }
    return true;
}

bool demarrerModuleCommandesSMS() {
    fileDesCommandes = xQueueCreate(4, sizeof(DemandeDeCommande));
    if (fileDesCommandes == nullptr) return false;
    return creerTacheSurveillee(tacheDesCommandes, "Commandes SMS", 6144, PRIORITE_NORMALE, COEUR_TRAITEMENTS);
}
