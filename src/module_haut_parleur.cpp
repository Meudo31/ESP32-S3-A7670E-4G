#include <Preferences.h>

#include "configuration.h"
#include "module_haut_parleur.h"
#include "module_appli.h"
#include "module_carte_sd.h"
#include "module_journal.h"
#include "module_modem.h"
#include "module_systeme.h"
#include "outils_texte.h"

namespace {

enum class SonDemande : uint8_t { ANNONCE, NOUVEAU_SMS, DEBUT_SONNERIE, FIN_SONNERIE, VOLUME, TEST, SORTIE };

struct DemandeDeSon {
    SonDemande son;
    char       texte[120];
    int        valeur;
};

QueueHandle_t fileDesSons      = nullptr;
volatile bool sonnerieEnCours  = false;
volatile int  volumeActuel     = 4;
volatile int  sortieAudio      = 3;   // AT+CSDVC : 1 = combiné (écouteur), 3 = haut-parleur

const char* FICHIER_SON_SMS_DANS_LE_MODEM      = "c:/sms.wav";
const char* FICHIER_SONNERIE_DANS_LE_MODEM     = "c:/sonnerie.wav";
const uint32_t INTERVALLE_ENTRE_DEUX_SONNERIES_MS = 4000;

void envoyerLaDemande(SonDemande son, const String& texte = "", int valeur = 0) {
    if (fileDesSons == nullptr) return;
    DemandeDeSon demande;
    demande.son    = son;
    demande.valeur = valeur;
    strlcpy(demande.texte, texte.c_str(), sizeof(demande.texte));
    xQueueSend(fileDesSons, &demande, 0);
}

bool prononcer(const char* texte) {
    const String commande = String("AT+CTTS=1,\"") + convertirTexteEnUcs2Hexadecimal(texte) + "\"";
    const bool reussi = envoyerCommandeAT(commande, nullptr, 5000) == ResultatAT::OK;
    if (!reussi) ecrireDansLeJournal("[HAUT-PARLEUR] Le modem refuse la synthèse vocale (AT+CTTS) : variante sans voix ?");
    return reussi;
}

bool jouerUnFichierDuModem(const char* cheminDansLeModem) {
    const String commande = String("AT+CCMXPLAY=\"") + cheminDansLeModem + "\",0,0";
    return envoyerCommandeAT(commande, nullptr, 3000) == ResultatAT::OK;
}

void arreterToutSon() {
    envoyerCommandeAT("AT+CCMXSTOP", nullptr, 1000);
    envoyerCommandeAT("AT+CTTS=0", nullptr, 1000);
}

// Le son doit sortir sur le haut-parleur du module (et non être envoyé au correspondant d'un appel).
void choisirLaSortieAudio() {
    envoyerCommandeAT("AT+CDTAM=0", nullptr, 1000);
    const bool reussi = envoyerCommandeAT(String("AT+CSDVC=") + sortieAudio, nullptr, 2000) == ResultatAT::OK;
    ecrireDansLeJournal("[HAUT-PARLEUR] Sortie audio %d (%s) : %s.", sortieAudio,
                        sortieAudio == 1 ? "combiné" : "haut-parleur", reussi ? "OK" : "refusée par le modem");
}

// Un bip pur, qui ne dépend ni de la synthèse vocale ni d'un fichier : AT+SIMTONE=1,fréquence,durée ON,OFF,durée totale.
bool jouerUnBip() {
    return envoyerCommandeAT("AT+SIMTONE=1,1000,200,0,1000", nullptr, 2000) == ResultatAT::OK;
}

const char* texteDuResultat(ResultatAT resultat) {
    switch (resultat) {
        case ResultatAT::OK:            return "OK";
        case ResultatAT::ERREUR:        return "refusé";
        case ResultatAT::DELAI_DEPASSE: return "pas de réponse";
        default:                        return "modem absent";
    }
}

// Essaie tout ce qui peut faire du bruit et dit à l'application ce que le modem a accepté.
void testerLeHautParleur() {
    String reponse;
    String compteRendu;
    if (envoyerCommandeAT("AT+CSDVC?", &reponse, 2000) == ResultatAT::OK) {
        reponse.replace("\r", " "); reponse.replace("\n", " "); reponse.replace("OK", ""); reponse.trim();
        compteRendu += "Sortie " + reponse + ". ";
    } else {
        compteRendu += "Sortie (CSDVC) inconnue. ";
    }
    reponse = "";
    if (envoyerCommandeAT("AT+CLVL?", &reponse, 2000) == ResultatAT::OK) {
        reponse.replace("\r", " "); reponse.replace("\n", " "); reponse.replace("OK", ""); reponse.trim();
        compteRendu += "Volume " + reponse + ". ";
    }
    const ResultatAT bip = envoyerCommandeAT("AT+SIMTONE=1,1000,300,0,1500", nullptr, 2000);
    compteRendu += String("Bip : ") + texteDuResultat(bip) + ". ";
    vTaskDelay(pdMS_TO_TICKS(1800));
    const String voix = String("AT+CTTS=1,\"") + convertirTexteEnUcs2Hexadecimal("Test du haut-parleur") + "\"";
    const ResultatAT parole = envoyerCommandeAT(voix, nullptr, 5000);
    compteRendu += String("Voix : ") + texteDuResultat(parole) + ". ";
    vTaskDelay(pdMS_TO_TICKS(2500));
    const ResultatAT fichier = envoyerCommandeAT(String("AT+CCMXPLAY=\"") + FICHIER_SON_SMS_DANS_LE_MODEM + "\",0,0", nullptr, 3000);
    compteRendu += String("Son SMS : ") + (fichier == ResultatAT::OK ? "OK" : "non installé ou refusé") + ".";
    ecrireDansLeJournal("[HAUT-PARLEUR] Test : %s", compteRendu.c_str());
    repondreALAppli("SON_TEST", bip == ResultatAT::OK || parole == ResultatAT::OK || fichier == ResultatAT::OK, compteRendu.c_str());
}

void appliquerLeVolume() {
    envoyerCommandeAT(String("AT+CLVL=") + volumeActuel, nullptr, 1000);
    // Volume de la synthèse vocale : 0..2 (paramètres : volume, volume système, chiffres, hauteur, vitesse)
    const int volumeDeLaVoix = volumeActuel >= 4 ? 2 : (volumeActuel >= 2 ? 1 : 0);
    envoyerCommandeAT(String("AT+CTTSPARAM=") + volumeDeLaVoix + ",3,0,1,1", nullptr, 1000);
}

void traiterUneDemande(const DemandeDeSon& demande) {
    switch (demande.son) {
        case SonDemande::ANNONCE:
            prononcer(demande.texte);
            break;
        case SonDemande::NOUVEAU_SMS:
            if (!jouerUnFichierDuModem(FICHIER_SON_SMS_DANS_LE_MODEM) && !prononcer("Nouveau message")) jouerUnBip();
            break;
        case SonDemande::DEBUT_SONNERIE:
            sonnerieEnCours = true;
            break;
        case SonDemande::FIN_SONNERIE:
            if (sonnerieEnCours) arreterToutSon();
            sonnerieEnCours = false;
            break;
        case SonDemande::VOLUME:
            volumeActuel = constrain(demande.valeur, 0, 5);
            appliquerLeVolume();
            break;
        case SonDemande::TEST:
            testerLeHautParleur();
            break;
        case SonDemande::SORTIE:
            sortieAudio = demande.valeur;
            choisirLaSortieAudio();
            appliquerLeVolume();
            testerLeHautParleur();
            break;
    }
}

void faireSonnerUneFois() {
    if (!jouerUnFichierDuModem(FICHIER_SONNERIE_DANS_LE_MODEM) && !prononcer("Appel entrant")) jouerUnBip();
}

void tacheHautParleur(void*) {
    attendreQueLeModemSoitPret(portMAX_DELAY);
    choisirLaSortieAudio();
    appliquerLeVolume();
    DemandeDeSon demande;
    uint32_t derniereSonnerie = 0;
    for (;;) {
        const TickType_t attente = sonnerieEnCours ? pdMS_TO_TICKS(200) : portMAX_DELAY;
        if (xQueueReceive(fileDesSons, &demande, attente) == pdPASS) traiterUneDemande(demande);
        if (sonnerieEnCours && millis() - derniereSonnerie >= INTERVALLE_ENTRE_DEUX_SONNERIES_MS) {
            derniereSonnerie = millis();
            faireSonnerUneFois();
        }
    }
}

bool copierUnFichierDansLeModem(const char* cheminSurLaCarte, const char* cheminDansLeModem, String& compteRendu) {
    if (!prendreLaCarteSD()) return false;
    File fichier = systemeDeFichiersDeLaCarteSD().open(cheminSurLaCarte, FILE_READ);
    if (!fichier) {
        rendreLaCarteSD();
        compteRendu += String(cheminSurLaCarte) + " absent. ";
        return false;
    }
    const size_t taille = fichier.size();
    uint8_t* contenu = (uint8_t*)ps_malloc(taille);
    const bool lu = contenu != nullptr && fichier.read(contenu, taille) == taille;
    fichier.close();
    rendreLaCarteSD();
    if (!lu) {
        free(contenu);
        compteRendu += String(cheminSurLaCarte) + " illisible. ";
        return false;
    }
    envoyerCommandeAT(String("AT+FSDEL=\"") + cheminDansLeModem + "\"", nullptr, 2000);   // ancienne version éventuelle
    const String commande = String("AT+CFTRANRX=\"") + cheminDansLeModem + "\"," + taille;
    const bool copie = envoyerCommandeATAvecDonnees(commande, contenu, taille, false, nullptr, 30000) == ResultatAT::OK;
    free(contenu);
    compteRendu += String(cheminDansLeModem) + (copie ? " installé. " : " : échec de copie. ");
    return copie;
}

} // namespace

void annoncerAuHautParleur(const String& texte) { envoyerLaDemande(SonDemande::ANNONCE, texte); }
void jouerLeSonDUnNouveauSMS()                  { envoyerLaDemande(SonDemande::NOUVEAU_SMS); }
void demarrerLaSonnerie()                       { envoyerLaDemande(SonDemande::DEBUT_SONNERIE); }
void arreterLaSonnerie()                        { envoyerLaDemande(SonDemande::FIN_SONNERIE); }

void reglerLeVolumeDuHautParleur(int volume) {
    volume = constrain(volume, 0, 5);
    Preferences memoirePermanente;
    memoirePermanente.begin("haut-parleur", false);
    memoirePermanente.putInt("volume", volume);
    memoirePermanente.end();
    envoyerLaDemande(SonDemande::VOLUME, "", volume);
}

int volumeDuHautParleur() { return volumeActuel; }

void testerLeHautParleurDuModule() { envoyerLaDemande(SonDemande::TEST); }

bool choisirLaSortieDuHautParleur(int sortie) {
    if (sortie != 1 && sortie != 3) return false;
    Preferences memoirePermanente;
    memoirePermanente.begin("haut-parleur", false);
    memoirePermanente.putInt("sortie", sortie);
    memoirePermanente.end();
    envoyerLaDemande(SonDemande::SORTIE, "", sortie);
    return true;
}

int sortieDuHautParleur() { return sortieAudio; }

String installerLesSonsDepuisLaCarteSD() {
    if (!leModemEstPret()) return "Modem pas prêt.";
    String compteRendu;
    copierUnFichierDansLeModem("/sons/sms.wav", FICHIER_SON_SMS_DANS_LE_MODEM, compteRendu);
    copierUnFichierDansLeModem("/sons/sonnerie.wav", FICHIER_SONNERIE_DANS_LE_MODEM, compteRendu);
    ecrireDansLeJournal("[HAUT-PARLEUR] Installation des sons : %s", compteRendu.c_str());
    return compteRendu;
}

bool demarrerModuleHautParleur() {
    Preferences memoirePermanente;
    memoirePermanente.begin("haut-parleur", true);
    volumeActuel = memoirePermanente.getInt("volume", 4);
    sortieAudio  = memoirePermanente.getInt("sortie", 3);
    memoirePermanente.end();

    fileDesSons = xQueueCreate(8, sizeof(DemandeDeSon));
    if (fileDesSons == nullptr) return false;
    return creerTacheSurveillee(tacheHautParleur, "Haut-parleur", 4096, PRIORITE_NORMALE, COEUR_COMMUNICATIONS);
}
