#include <ArduinoJson.h>
#include <esp_heap_caps.h>

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

// ─────────────────────────────── Envoi depuis le téléphone ───────────────────────────────
// Les morceaux reçus sont gardés en mémoire et écrits sur la carte SD par paquets de 8 Ko :
// moins d'accès à la carte, et la carte n'est jamais bloquée longtemps pour les autres tâches.

const size_t TAILLE_DU_TAMPON_DE_RECEPTION = 8192;

struct ReceptionDUnFichier {
    bool     enCours = false;
    String   cheminFinal;
    String   cheminTemporaire;
    File     fichier;                 // reste ouvert pendant toute la réception : pas de réouverture tous les 8 Ko
    uint32_t tailleAttendue = 0;
    uint32_t octetsRecus = 0;
    uint32_t crc = 0;
    bool     erreurDEcriture = false;
    bool     erreurSignalee = false;   // l'application a déjà été prévenue
    char     causeDeLErreur[96] = "";
    uint8_t* tampon = nullptr;
    size_t   remplissage = 0;
};

ReceptionDUnFichier reception;
SemaphoreHandle_t   verrouDeLaReception = nullptr;

// Les morceaux arrivent dans la tâche du Bluetooth, qui a très peu de pile : on ne les écrit pas sur la
// carte SD depuis là (c'est ce qui faisait planter le module), on les confie à une tâche dédiée.
struct MorceauRecu {
    uint16_t taille;
    uint8_t  octets[520];   // un morceau fait au plus 509 octets
};
const int          NOMBRE_DE_MORCEAUX_EN_AVANCE = 48;   // ≈ 24 Ko : de quoi attendre une carte SD lente
QueueHandle_t      fileDesMorceaux = nullptr;
volatile int       morceauxEnAttente = 0;
portMUX_TYPE       verrouDuCompteur = portMUX_INITIALIZER_UNLOCKED;
const uint32_t     ATTENTE_MAXIMUM_POUR_DEPOSER_UN_MORCEAU_MS = 8000;

void compterUnMorceau(int variation) {
    portENTER_CRITICAL(&verrouDuCompteur);
    morceauxEnAttente += variation;
    portEXIT_CRITICAL(&verrouDuCompteur);
}

void viderLaFileDesMorceaux() {
    xQueueReset(fileDesMorceaux);
    portENTER_CRITICAL(&verrouDuCompteur);
    morceauxEnAttente = 0;
    portEXIT_CRITICAL(&verrouDuCompteur);
}

// Note la première erreur ; la tâche « Réception » la signale aussitôt à l'application, qui arrête l'envoi.
void noterUneErreurDeReception(const char* cause) {
    if (reception.erreurDEcriture) return;
    strlcpy(reception.causeDeLErreur, cause, sizeof(reception.causeDeLErreur));
    reception.erreurDEcriture = true;
}

// Écrit le tampon dans le fichier .part. En cas d'échec (carte occupée par une autre tâche, écriture partielle),
// on réessaie plusieurs fois avant d'abandonner.
bool viderLeTamponSurLaCarte() {
    size_t dejaEcrits = 0;
    for (int essai = 1; essai <= 4 && dejaEcrits < reception.remplissage; essai++) {
        if (!prendreLaCarteSD(8000)) {
            ecrireDansLeJournal("[FICHIERS] Carte SD occupée (essai %d).", essai);
            continue;
        }
        if (reception.fichier) {
            dejaEcrits += reception.fichier.write(reception.tampon + dejaEcrits, reception.remplissage - dejaEcrits);
        }
        rendreLaCarteSD();
        if (dejaEcrits < reception.remplissage) {
            ecrireDansLeJournal("[FICHIERS] Écriture incomplète sur la carte SD (essai %d).", essai);
            vTaskDelay(pdMS_TO_TICKS(100 * essai));
        }
    }
    const bool reussi = dejaEcrits == reception.remplissage;
    reception.remplissage = 0;
    return reussi;
}

void fermerLeFichierDeReception() {
    if (!reception.fichier) return;
    if (prendreLaCarteSD(10000)) {
        reception.fichier.close();
        rendreLaCarteSD();
    }
}

void oublierLaReception(bool effacerLeFichierTemporaire) {
    fermerLeFichierDeReception();
    if (effacerLeFichierTemporaire && reception.cheminTemporaire.length() > 0) {
        supprimerFichierOuDossier(reception.cheminTemporaire.c_str());
    }
    free(reception.tampon);
    reception = ReceptionDUnFichier();
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

bool commencerLaReceptionDUnFichier(const String& chemin, uint32_t taille, String& compteRendu) {
    if (!chemin.startsWith("/") || chemin.endsWith("/")) { compteRendu = "Chemin invalide"; return false; }
    if (!carteSDMontee()) { compteRendu = "Carte SD absente"; return false; }
    const InformationsCarteSD carte = informationsDeLaCarteSD();
    if (carte.tailleTotaleOctets - carte.espaceUtiliseOctets < (uint64_t)taille + 65536) {
        compteRendu = "Plus assez de place sur la carte SD";
        return false;
    }
    viderLaFileDesMorceaux();
    xSemaphoreTake(verrouDeLaReception, portMAX_DELAY);
    if (reception.enCours) oublierLaReception(true);
    reception.tampon = (uint8_t*)malloc(TAILLE_DU_TAMPON_DE_RECEPTION);
    if (reception.tampon == nullptr) {
        xSemaphoreGive(verrouDeLaReception);
        compteRendu = "Mémoire insuffisante";
        return false;
    }
    reception.cheminFinal      = chemin;
    reception.cheminTemporaire = chemin + ".part";
    reception.tailleAttendue   = taille;
    supprimerFichierOuDossier(reception.cheminTemporaire.c_str());   // reste d'un envoi précédent interrompu
    if (prendreLaCarteSD(8000)) {
        reception.fichier = systemeDeFichiersDeLaCarteSD().open(reception.cheminTemporaire, FILE_WRITE);
        rendreLaCarteSD();
    }
    const bool fichierCree = (bool)reception.fichier;
    if (fichierCree) reception.enCours = true;
    else             oublierLaReception(false);
    xSemaphoreGive(verrouDeLaReception);
    compteRendu = fichierCree ? String("Prêt à recevoir ") + taille + " octets" : String("Impossible de créer le fichier");
    if (fichierCree) ecrireDansLeJournal("[FICHIERS] Réception de %s (%u octets) depuis le téléphone.", chemin.c_str(), (unsigned)taille);
    return fichierCree;
}

bool receptionDUnFichierEnCours() { return reception.enCours; }

// Tâche « Réception » : écrit sur la carte SD les morceaux déposés par le Bluetooth.
static void ajouterUnMorceauAuFichier(const uint8_t* octets, size_t taille) {
    xSemaphoreTake(verrouDeLaReception, portMAX_DELAY);
    if (reception.enCours && !reception.erreurDEcriture) {
        if (reception.octetsRecus + taille > reception.tailleAttendue) {
            noterUneErreurDeReception("Le module a reçu plus d'octets qu'annoncé");
        } else {
            reception.crc = continuerLeCRC32(reception.crc, octets, taille);
            reception.octetsRecus += taille;
            while (taille > 0) {
                const size_t place = TAILLE_DU_TAMPON_DE_RECEPTION - reception.remplissage;
                const size_t aCopier = min(place, taille);
                memcpy(reception.tampon + reception.remplissage, octets, aCopier);
                reception.remplissage += aCopier;
                octets += aCopier;
                taille -= aCopier;
                if (reception.remplissage == TAILLE_DU_TAMPON_DE_RECEPTION && !viderLeTamponSurLaCarte()) {
                    noterUneErreurDeReception("Écriture sur la carte SD impossible");
                    break;
                }
            }
        }
    }
    xSemaphoreGive(verrouDeLaReception);
}

// Prévient l'application dès qu'une erreur survient, pour qu'elle n'envoie pas le reste du fichier pour rien.
static void signalerLErreurALAppli() {
    if (!reception.enCours || !reception.erreurDEcriture || reception.erreurSignalee) return;
    reception.erreurSignalee = true;
    ecrireDansLeJournal("[FICHIERS] Réception de %s interrompue à %u octets : %s.",
                        reception.cheminFinal.c_str(), (unsigned)reception.octetsRecus, reception.causeDeLErreur);
    repondre("FICHIER_ENVOYER", false, reception.causeDeLErreur);
}

static void tacheReceptionDeFichiers(void*) {
    static MorceauRecu morceau;
    for (;;) {
        if (xQueueReceive(fileDesMorceaux, &morceau, pdMS_TO_TICKS(200)) == pdPASS) {
            ajouterUnMorceauAuFichier(morceau.octets, morceau.taille);
            compterUnMorceau(-1);
        }
        signalerLErreurALAppli();
    }
}

// Appelé par le Bluetooth : dépose le morceau dans la file, sans toucher à la carte SD.
// Si la file est pleine, on attend : le téléphone attend l'accusé de réception, il ralentit donc tout seul.
void recevoirUnMorceauDuTelephone(const uint8_t* octets, size_t taille) {
    static MorceauRecu morceau;   // statique : surtout pas 520 octets sur la petite pile du Bluetooth
    if (fileDesMorceaux == nullptr || !reception.enCours || reception.erreurDEcriture || taille == 0) return;
    morceau.taille = min(taille, sizeof(morceau.octets));
    memcpy(morceau.octets, octets, morceau.taille);
    compterUnMorceau(+1);
    if (xQueueSend(fileDesMorceaux, &morceau, pdMS_TO_TICKS(ATTENTE_MAXIMUM_POUR_DEPOSER_UN_MORCEAU_MS)) != pdPASS) {
        compterUnMorceau(-1);
        noterUneErreurDeReception("La carte SD est trop lente, réessayer");
    }
}

bool terminerLaReceptionDUnFichier(uint32_t crcAttendu, String& compteRendu) {
    // On attend que la tâche « Réception » ait écrit tous les morceaux encore dans la file.
    const uint32_t debut = millis();
    while (morceauxEnAttente > 0 && millis() - debut < 30 * UNE_SECONDE_MS) vTaskDelay(pdMS_TO_TICKS(20));
    xSemaphoreTake(verrouDeLaReception, portMAX_DELAY);
    bool reussi = false;
    if (!reception.enCours) {
        compteRendu = "Aucun envoi en cours";
    } else if (reception.erreurDEcriture) {
        compteRendu = reception.causeDeLErreur;
    } else if (!viderLeTamponSurLaCarte()) {
        compteRendu = "Écriture sur la carte SD impossible";
    } else if (reception.octetsRecus != reception.tailleAttendue) {
        compteRendu = String("Fichier incomplet : ") + reception.octetsRecus + " octets sur " + reception.tailleAttendue;
    } else if (reception.crc != crcAttendu) {
        compteRendu = "Fichier abîmé pendant le transfert (CRC différent), réessayer";
    } else {
        fermerLeFichierDeReception();
        supprimerFichierOuDossier(reception.cheminFinal.c_str());   // remplace un fichier du même nom
        reussi = renommerOuDeplacer(reception.cheminTemporaire.c_str(), reception.cheminFinal.c_str());
        compteRendu = reussi ? reception.cheminFinal : String("Impossible de renommer le fichier reçu");
    }
    ecrireDansLeJournal("[FICHIERS] Réception de %s : %s.", reception.cheminFinal.c_str(), reussi ? "OK" : compteRendu.c_str());
    oublierLaReception(!reussi);
    xSemaphoreGive(verrouDeLaReception);
    return reussi;
}

void annulerLaReceptionDUnFichier() {
    if (verrouDeLaReception == nullptr) return;
    viderLaFileDesMorceaux();
    xSemaphoreTake(verrouDeLaReception, portMAX_DELAY);
    if (reception.enCours) {
        ecrireDansLeJournal("[FICHIERS] Réception de %s annulée.", reception.cheminFinal.c_str());
        oublierLaReception(true);
    }
    xSemaphoreGive(verrouDeLaReception);
}

// La file des morceaux (≈ 25 Ko) va de préférence dans la PSRAM, pour laisser la RAM interne au Bluetooth.
static QueueHandle_t creerLaFileDesMorceaux() {
    static StaticQueue_t structureDeLaFile;
    uint8_t* stockage = (uint8_t*)heap_caps_malloc(NOMBRE_DE_MORCEAUX_EN_AVANCE * sizeof(MorceauRecu), MALLOC_CAP_SPIRAM);
    if (stockage != nullptr) {
        return xQueueCreateStatic(NOMBRE_DE_MORCEAUX_EN_AVANCE, sizeof(MorceauRecu), stockage, &structureDeLaFile);
    }
    return xQueueCreate(16, sizeof(MorceauRecu));
}

bool demarrerModuleExplorateurFichiers() {
    fileDesDemandes = xQueueCreate(4, sizeof(DemandeSurLesFichiers));
    verrouDeLaReception = xSemaphoreCreateMutex();
    fileDesMorceaux     = creerLaFileDesMorceaux();
    if (fileDesDemandes == nullptr || verrouDeLaReception == nullptr || fileDesMorceaux == nullptr) return false;
    if (!creerTacheSurveillee(tacheReceptionDeFichiers, "Reception", 6144, PRIORITE_NORMALE, COEUR_TRAITEMENTS)) return false;
    return creerTacheSurveillee(tacheExplorateurFichiers, "Fichiers", 6144, PRIORITE_BASSE, COEUR_TRAITEMENTS, &tacheExplorateur);
}
