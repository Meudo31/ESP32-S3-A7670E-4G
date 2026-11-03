#include <ArduinoJson.h>

#include "configuration.h"
#include "module_explorateur_fichiers.h"
#include "module_appli.h"
#include "module_carte_sd.h"
#include "module_journal.h"
#include "module_systeme.h"

namespace {

struct DemandeSurLesFichiers {
    OperationSurLesFichiers operation;
    char chemin[200];
    char destination[200];
};

QueueHandle_t     fileDesDemandes = nullptr;
TaskHandle_t      tacheExplorateur = nullptr;
volatile uint32_t octetsConfirmesParLeTelephone = 0;
volatile bool     telechargementAnnule = false;

const int      MORCEAUX_ENTRE_DEUX_CONFIRMATIONS = 8;
const uint32_t ATTENTE_MAXIMUM_D_UNE_CONFIRMATION_MS = 5000;
const size_t   TAILLE_MAXIMUM_D_UN_MESSAGE_DE_LISTE = 380;   // reste sous la taille d'une notification Bluetooth

// ─── Empreinte CRC32 (la même que java.util.zip.CRC32 côté Android) ───
uint32_t continuerLeCRC32(uint32_t crc, const uint8_t* octets, size_t taille) {
    crc = ~crc;
    for (size_t position = 0; position < taille; position++) {
        crc ^= octets[position];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
    }
    return ~crc;
}

void repondre(const char* commande, bool reussi, const String& message) {
    repondreALAppli(commande, reussi, message.c_str());
}

// ─────────────────────────────── Liste d'un dossier ───────────────────────────────
// Envoyée en plusieurs messages : {"t":"ls","chemin":"/photos","e":[["nom",1,0,1700000000],...],"fin":0}
// Chaque élément : nom, 1 si dossier, taille en octets, date de modification.

void envoyerUnePageDeLaListe(const char* chemin, JsonDocument& page, bool derniere) {
    page["t"]      = "ls";
    page["chemin"] = chemin;
    page["fin"]    = derniere ? 1 : 0;
    publierUnEvenementVersLAppli(page);
    page.clear();
}

void listerUnDossierPourLAppli(const char* chemin) {
    JsonDocument page;
    JsonArray elements = page["e"].to<JsonArray>();
    size_t tailleDeLaPage = 0;
    int nombreDElements = 0;

    const bool reussi = listerUnDossier(chemin, [&](const ElementDUnDossier& element) {
        const size_t tailleDeLElement = element.nom.length() + 30;
        if (tailleDeLaPage + tailleDeLElement > TAILLE_MAXIMUM_D_UN_MESSAGE_DE_LISTE && tailleDeLaPage > 0) {
            envoyerUnePageDeLaListe(chemin, page, false);
            elements = page["e"].to<JsonArray>();
            tailleDeLaPage = 0;
            vTaskDelay(pdMS_TO_TICKS(15));
        }
        JsonArray ligne = elements.add<JsonArray>();
        ligne.add(element.nom);
        ligne.add(element.estUnDossier ? 1 : 0);
        ligne.add(element.tailleOctets);
        ligne.add((uint32_t)element.dateDeModification);
        tailleDeLaPage += tailleDeLElement;
        nombreDElements++;
    });
    envoyerUnePageDeLaListe(chemin, page, true);
    repondre("FICHIERS", reussi, reussi ? String(nombreDElements) + " élément(s)" : String("Dossier illisible ou carte SD absente"));
}

// ─────────────────────────────── Téléchargement ───────────────────────────────

bool attendreLaConfirmationDuTelephone(uint32_t octetsEnvoyes) {
    const uint32_t debut = millis();
    while (octetsConfirmesParLeTelephone < octetsEnvoyes) {
        if (telechargementAnnule || !appliConnectee()) return false;
        if (millis() - debut > ATTENTE_MAXIMUM_D_UNE_CONFIRMATION_MS) return false;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
    return octetsConfirmesParLeTelephone == octetsEnvoyes;
}

void envoyerLeDebutOuLaFin(const char* type, const char* chemin, uint32_t taille, bool reussi, uint32_t crc, const char* message) {
    JsonDocument evenement;
    evenement["t"]      = type;
    evenement["chemin"] = chemin;
    evenement["taille"] = taille;
    evenement["ok"]     = reussi;
    evenement["crc"]    = crc;
    evenement["msg"]    = message;
    publierUnEvenementVersLAppli(evenement);
}

void telechargerVersLeTelephone(const char* chemin) {
    ElementDUnDossier informations;
    if (!lireLesInformationsDUnElement(chemin, informations) || informations.estUnDossier) {
        repondre("FICHIER_TELECHARGER", false, "Fichier introuvable");
        return;
    }
    const size_t tailleDUnMorceau = tailleMaximumDUneNotification();
    uint8_t* morceau = (uint8_t*)malloc(tailleDUnMorceau);
    if (morceau == nullptr) {
        repondre("FICHIER_TELECHARGER", false, "Mémoire insuffisante");
        return;
    }
    octetsConfirmesParLeTelephone = 0;
    telechargementAnnule = false;
    envoyerLeDebutOuLaFin("dl_debut", chemin, informations.tailleOctets, true, 0, "");
    ecrireDansLeJournal("[FICHIERS] Téléchargement de %s (%u octets).", chemin, (unsigned)informations.tailleOctets);

    uint32_t octetsEnvoyes = 0;
    uint32_t crc = 0;
    int morceauxDepuisLaDerniereConfirmation = 0;
    const char* erreur = nullptr;

    while (octetsEnvoyes < informations.tailleOctets && erreur == nullptr) {
        // On ouvre, lit et referme à chaque morceau pour ne pas bloquer la carte SD aux autres tâches.
        size_t lus = 0;
        if (prendreLaCarteSD()) {
            File fichier = systemeDeFichiersDeLaCarteSD().open(chemin, FILE_READ);
            if (fichier && fichier.seek(octetsEnvoyes)) lus = fichier.read(morceau, tailleDUnMorceau);
            if (fichier) fichier.close();
            rendreLaCarteSD();
        }
        if (lus == 0) { erreur = "Lecture impossible"; break; }

        envoyerDesOctetsALAppli(morceau, lus);
        crc = continuerLeCRC32(crc, morceau, lus);
        octetsEnvoyes += lus;
        morceauxDepuisLaDerniereConfirmation++;

        const bool fin = octetsEnvoyes >= informations.tailleOctets;
        if (morceauxDepuisLaDerniereConfirmation >= MORCEAUX_ENTRE_DEUX_CONFIRMATIONS || fin) {
            morceauxDepuisLaDerniereConfirmation = 0;
            if (!attendreLaConfirmationDuTelephone(octetsEnvoyes)) {
                erreur = telechargementAnnule ? "Annulé" : "Le téléphone n'a pas tout reçu";
            }
        }
    }
    free(morceau);
    const bool reussi = erreur == nullptr;
    envoyerLeDebutOuLaFin("dl_fin", chemin, octetsEnvoyes, reussi, crc, reussi ? "" : erreur);
    if (!reussi) ecrireDansLeJournal("[FICHIERS] Téléchargement interrompu : %s.", erreur);
}

// ─────────────────────────────── Exécution ───────────────────────────────

void executer(const DemandeSurLesFichiers& demande) {
    switch (demande.operation) {
        case OperationSurLesFichiers::LISTER:
            listerUnDossierPourLAppli(demande.chemin);
            break;
        case OperationSurLesFichiers::RENOMMER_OU_DEPLACER: {
            const bool reussi = renommerOuDeplacer(demande.chemin, demande.destination);
            ecrireDansLeJournal("[FICHIERS] %s → %s : %s.", demande.chemin, demande.destination, reussi ? "OK" : "échec");
            repondre("FICHIER_RENOMMER", reussi, reussi ? String(demande.destination)
                                                        : String("Impossible (le nom existe déjà ?)"));
            break;
        }
        case OperationSurLesFichiers::SUPPRIMER: {
            const bool reussi = supprimerFichierOuDossier(demande.chemin);
            ecrireDansLeJournal("[FICHIERS] Suppression de %s : %s.", demande.chemin, reussi ? "OK" : "échec");
            repondre("FICHIER_SUPPRIMER", reussi, reussi ? String("Supprimé") : String("Suppression impossible"));
            break;
        }
        case OperationSurLesFichiers::CREER_UN_DOSSIER: {
            const bool reussi = creerUnDossier(demande.chemin);
            repondre("DOSSIER_CREER", reussi, reussi ? String("Dossier créé") : String("Création impossible"));
            break;
        }
        case OperationSurLesFichiers::TELECHARGER:
            telechargerVersLeTelephone(demande.chemin);
            break;
    }
}

void tacheExplorateurFichiers(void*) {
    static DemandeSurLesFichiers demande;
    for (;;) {
        if (xQueueReceive(fileDesDemandes, &demande, portMAX_DELAY) == pdPASS) executer(demande);
    }
}

} // namespace

bool demanderUneOperationSurLesFichiers(OperationSurLesFichiers operation, const String& chemin, const String& destination) {
    if (fileDesDemandes == nullptr || !chemin.startsWith("/")) return false;
    DemandeSurLesFichiers* demande = (DemandeSurLesFichiers*)calloc(1, sizeof(DemandeSurLesFichiers));
    if (demande == nullptr) return false;
    demande->operation = operation;
    strlcpy(demande->chemin, chemin.c_str(), sizeof(demande->chemin));
    strlcpy(demande->destination, destination.c_str(), sizeof(demande->destination));
    const bool ajoutee = xQueueSend(fileDesDemandes, demande, 0) == pdPASS;
    free(demande);
    return ajoutee;
}

void confirmerLaReceptionDuTelephone(uint32_t octetsRecus) {
    octetsConfirmesParLeTelephone = octetsRecus;
    if (tacheExplorateur != nullptr) xTaskNotifyGive(tacheExplorateur);
}

void annulerLeTelechargement() {
    telechargementAnnule = true;
    if (tacheExplorateur != nullptr) xTaskNotifyGive(tacheExplorateur);
}

bool demarrerModuleExplorateurFichiers() {
    fileDesDemandes = xQueueCreate(4, sizeof(DemandeSurLesFichiers));
    if (fileDesDemandes == nullptr) return false;
    return creerTacheSurveillee(tacheExplorateurFichiers, "Fichiers", 6144, PRIORITE_BASSE, COEUR_TRAITEMENTS, &tacheExplorateur);
}
