#include <Preferences.h>
#include <driver/twai.h>
#include <math.h>

#include "configuration.h"
#include "module_canbus.h"
#include "module_carte.h"
#include "module_carte_sd.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_systeme.h"

namespace {

// ─────────────────────────────── Réglages ───────────────────────────────

const int      VITESSES_A_ESSAYER[]           = { 250, 500, 125, 1000 };   // kbit/s, la plus probable d'abord
const uint32_t DUREE_D_ECOUTE_PAR_VITESSE_MS  = 1500;
const uint32_t TRAMES_POUR_VALIDER_UNE_VITESSE = 5;
const uint32_t PAUSE_ENTRE_DEUX_RECHERCHES_MS = 5000;
const uint32_t SILENCE_AVANT_MOTO_COUPEE_MS   = 5000;
const uint32_t SILENCE_AVANT_NOUVEAU_FICHIER_MS = 30000;
const uint32_t PERIODE_D_ECRITURE_SD_MS       = 2000;
const size_t   TAILLE_DU_TAMPON_SD            = 24 * 1024;
const int      NOMBRE_MAXIMUM_DE_TRAMES_VUES  = 160;
const int      NOMBRE_MAXIMUM_DE_CODES_DEFAUT = 12;
const char*    DOSSIER_DES_ENREGISTREMENTS    = "/can";

// ─────────────────────────────── Valeurs décodées ───────────────────────────────

struct CodeDefaut {
    uint8_t  source;     // adresse J1939 du module qui signale le défaut
    uint32_t spn;
    uint8_t  fmi;
    uint8_t  occurrences;
};

struct ValeursDecodees {
    float regime = NAN, couple = NAN, vitesse = NAN, vitesseAvant = NAN, patinage = NAN;
    float papillon = NAN, temperatureMoteur = NAN, carburant = NAN, autonomieKm = NAN;
    float debitCarburant = NAN, consoInstantanee = NAN, consoMoyenne = NAN;
    float tensionBatterie = NAN, temperatureExterieure = NAN, odometre = NAN, trip = NAN;
    float pressionAvant = NAN, pressionArriere = NAN, temperaturePneuAvant = NAN, temperaturePneuArriere = NAN;
    char  rapport = 0;                 // 'N', '1'..'6', 0 si inconnu
    int   frein = -1, embrayage = -1, regulateur = -1;
    int   coupeCircuit = -1, bequille = -1, inclinaison = -1;
    int   clignotantGauche = -1, clignotantDroit = -1, warning = -1, feuCroisement = -1, feuRoute = -1;
    int   lampes = -1;                 // DM1 : MIL 1 + Stop 2 + Alerte 4 + Protection 8
    bool  obd = false;                 // des réponses OBD-II 11 bits ont été vues
};

// ─────────────────────────────── État partagé ───────────────────────────────

SemaphoreHandle_t verrou = nullptr;
ValeursDecodees   valeurs;
TrameCANVue       tramesVues[NOMBRE_MAXIMUM_DE_TRAMES_VUES];
int               nombreDeTramesVues = 0;
CodeDefaut        codesDefaut[NOMBRE_MAXIMUM_DE_CODES_DEFAUT];
int               nombreDeCodesDefaut = 0;

volatile int      vitesseReglee = 0;          // 0 = automatique
volatile bool     vitesseAChanger = false;
volatile bool     enregistrement = true;
volatile bool     effacementDemande = false;

volatile int      vitesseActuelle = 0;        // kbit/s, 0 = pas encore trouvée
volatile bool     driverInstalle = false;
volatile uint32_t totalDesTrames = 0;
volatile uint32_t tramesParSeconde = 0;
volatile uint32_t erreursDuBus = 0;
volatile uint32_t trameManqueesSD = 0;
volatile uint32_t derniereTrameMs = 0;
volatile bool     recherche = false;
String            fichierEnCours;             // protégé par le verrou
String            dernierProbleme;            // protégé par le verrou

// Tampon des lignes à écrire sur la carte SD (utilisé seulement par la tâche).
char*  tamponSD = nullptr;
size_t remplissageSD = 0;

// Réassemblage des messages J1939 longs (BAM : TP.CM + TP.DT), un à la fois par source.
struct MessageLong {
    bool     actif = false;
    uint32_t pgn = 0;
    uint16_t taille = 0;
    uint8_t  paquets = 0;
    uint8_t  recus = 0;
    uint8_t  octets[256];
};
MessageLong messagesLongs[256 / 32];   // quelques sources suffisent (index = source % 8)

// ─────────────────────────────── Outils ───────────────────────────────

void enregistrerLesReglages() {
    Preferences memoire;
    memoire.begin("canbus", false);
    memoire.putInt("vitesse", vitesseReglee);
    memoire.putBool("enreg", enregistrement);
    memoire.end();
}

void lireLesReglages() {
    Preferences memoire;
    memoire.begin("canbus", true);
    vitesseReglee  = memoire.getInt("vitesse", 0);
    enregistrement = memoire.getBool("enreg", true);
    memoire.end();
}

void noterUnProbleme(const char* texte) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    dernierProbleme = texte;
    xSemaphoreGive(verrou);
}

uint16_t mot(const uint8_t* d, int i) { return d[i] | (d[i + 1] << 8); }
uint32_t motLong(const uint8_t* d, int i) {
    return (uint32_t)d[i] | ((uint32_t)d[i + 1] << 8) | ((uint32_t)d[i + 2] << 16) | ((uint32_t)d[i + 3] << 24);
}
// Champ J1939 sur 2 bits : 0 = non, 1 = oui, 2 = erreur, 3 = non disponible → -1 sauf 0 et 1.
int deuxBits(uint8_t octet, int decalage) {
    const int v = (octet >> decalage) & 3;
    return v <= 1 ? v : -1;
}

// PGN et adresse source d'un identifiant 29 bits.
uint32_t pgnJ1939(uint32_t id) {
    const uint32_t pf = (id >> 16) & 0xFF;
    uint32_t pgn = (id >> 8) & 0x3FFFF;
    if (pf < 240) pgn &= 0x3FF00;   // PDU1 : l'octet PS est l'adresse du destinataire
    return pgn;
}
uint8_t sourceJ1939(uint32_t id) { return id & 0xFF; }

// ─────────────────────────────── Décodage J1939 ───────────────────────────────

void decoderLesCodesDefaut(uint8_t source, const uint8_t* d, int taille) {
    if (taille < 2) return;
    // Octet 0 : lampes, 2 bits chacune (MIL 6-7, Stop 4-5, Alerte 2-3, Protection 0-1).
    int lampes = 0;
    if (((d[0] >> 6) & 3) == 1) lampes |= 1;
    if (((d[0] >> 4) & 3) == 1) lampes |= 2;
    if (((d[0] >> 2) & 3) == 1) lampes |= 4;
    if ((d[0] & 3) == 1)        lampes |= 8;

    // On remplace les codes de cette source par ceux du message.
    int garde = 0;
    for (int i = 0; i < nombreDeCodesDefaut; i++) if (codesDefaut[i].source != source) codesDefaut[garde++] = codesDefaut[i];
    nombreDeCodesDefaut = garde;
    for (int i = 2; i + 3 < taille && nombreDeCodesDefaut < NOMBRE_MAXIMUM_DE_CODES_DEFAUT; i += 4) {
        const uint32_t spn = d[i] | (d[i + 1] << 8) | ((uint32_t)(d[i + 2] & 0xE0) << 11);
        const uint8_t  fmi = d[i + 2] & 0x1F;
        if (spn == 0 || spn == 0x7FFFF) continue;   // « aucun défaut »
        codesDefaut[nombreDeCodesDefaut++] = { source, spn, fmi, (uint8_t)(d[i + 3] & 0x7F) };
    }
    valeurs.lampes = lampes;   // lampes du dernier message DM1 reçu
}

void decoderJ1939(uint32_t pgn, uint8_t sa, const uint8_t* d, int n) {
    ValeursDecodees& v = valeurs;
    switch (pgn) {
        case 61444:   // EEC1 : régime (octets 3-4, 0,125 tr/min), couple demandé (octet 7, -125 %)
            if (n >= 5 && mot(d, 3) != 0xFFFF) v.regime = mot(d, 3) * 0.125f;
            if (n >= 8 && d[7] != 0xFF) v.couple = d[7] - 125.0f;
            break;
        case 61445:   // ETC2 : rapport engagé, en ASCII dans l'octet 5 sur Indian
            if (n >= 6) {
                const uint8_t c = d[5];
                if (c == 'N' || (c >= '1' && c <= '6')) v.rapport = (char)c;
                else if (c >= 0x7C && c <= 0x82) v.rapport = c == 0x7D ? 'N' : (char)('0' + (c - 0x7D));   // codage J1939 standard
            }
            break;
        case 65265:   // CCVS : vitesse (ABS), frein / embrayage / régulateur (moteur)
            if (sa != 39 && n >= 3 && mot(d, 1) != 0xFFFF) v.vitesse = mot(d, 1) / 256.0f;
            if (sa == 0 && n >= 4) {
                if (deuxBits(d[3], 4) >= 0) v.frein = deuxBits(d[3], 4);
                if (deuxBits(d[3], 6) >= 0) v.embrayage = deuxBits(d[3], 6);
                if (deuxBits(d[3], 0) >= 0) v.regulateur = deuxBits(d[3], 0);
            }
            break;
        case 65215:   // EBC2 : vitesse de la roue avant, patinage arrière
            if (n >= 2 && mot(d, 0) != 0xFFFF) v.vitesseAvant = mot(d, 0) / 256.0f;
            if (n >= 5 && d[4] <= 250) v.patinage = (d[4] - 125) / 16.0f;
            break;
        case 65262:   // ET1 : température moteur (liquide sur la Scout, culasse sur les Thunder Stroke)
            if (n >= 1 && d[0] != 0xFF) v.temperatureMoteur = d[0] - 40.0f;
            break;
        case 65266:   // LFE : débit, consommations, papillon
            if (n >= 2 && mot(d, 0) != 0xFFFF) v.debitCarburant = mot(d, 0) * 0.05f;
            if (n >= 4 && mot(d, 2) != 0xFFFF && mot(d, 2) != 0) v.consoInstantanee = 100.0f / (mot(d, 2) / 512.0f);
            if (n >= 6 && mot(d, 4) != 0xFFFF && mot(d, 4) != 0) v.consoMoyenne = 100.0f / (mot(d, 4) / 512.0f);
            if (n >= 7 && d[6] != 0xFF) v.papillon = d[6] * 0.4f;
            break;
        case 65276:   // Niveau de carburant (octet 1, 0,4 %)
            if (n >= 2 && d[1] != 0xFF && (sa == 0 || isnan(v.carburant))) v.carburant = d[1] * 0.4f;
            break;
        case 65382:   // Indian : autonomie restante en km (octets 3-4)
            if (sa == 0 && n >= 5 && d[3] != 0xFF) {
                static int candidat = -1;
                static uint32_t depuis = 0;
                const int km = d[3] | ((d[4] == 0xFF ? 0 : d[4]) << 8);
                // Indian envoie parfois de courtes rafales fausses : un grand saut doit tenir 3 s.
                if (!isnan(v.autonomieKm) && abs(km - (int)v.autonomieKm) <= 60) { v.autonomieKm = km; candidat = -1; }
                else if (km != candidat) { candidat = km; depuis = millis(); }
                else if (millis() - depuis >= 3000) { v.autonomieKm = km; candidat = -1; }
            }
            break;
        case 65271:   // VEP1 : tension batterie (octets 4-5, 0,05 V)
            if (n >= 6 && mot(d, 4) != 0xFFFF) v.tensionBatterie = mot(d, 4) * 0.05f;
            break;
        case 65269:   // AMB : température extérieure
            if (n >= 5 && mot(d, 3) != 0xFFFF) v.temperatureExterieure = mot(d, 3) * 0.03125f - 273.0f;
            break;
        case 65217:   // HRVD : compteur total et trip 1 (0,005 km)
            if (n >= 8) {
                if (motLong(d, 0) != 0xFFFFFFFFu) v.odometre = motLong(d, 0) * 0.005f;
                if (motLong(d, 4) != 0xFFFFFFFFu) v.trip = motLong(d, 4) * 0.005f;
            }
            break;
        case 65268: { // Pneus (TPMS) : position, pression, température
            if (n < 2) break;
            const bool avant = d[0] == 0x00 || d[0] == 0x11;
            const bool arriere = d[0] == 0x10 || d[0] == 0x21 || d[0] == 0x01;
            if (d[1] != 0xFF) {
                const float bars = d[1] * 0.580152f * 0.0689476f;   // psi → bar
                if (avant) v.pressionAvant = bars; else if (arriere) v.pressionArriere = bars;
            }
            if (n >= 4 && mot(d, 2) != 0xFFFF) {
                const float t = mot(d, 2) / 32.0f - 273.0f;
                if (avant) v.temperaturePneuAvant = t; else if (arriere) v.temperaturePneuArriere = t;
            }
            break;
        }
        case 65381:   // Indian : commodos et contacteurs
            if (sa == 0 && n >= 8) {
                v.coupeCircuit = (d[4] >> 6) & 1;   // 1 = RUN
                v.bequille = d[7] & 1;
            }
            if (sa == 39 && n >= 3) {
                v.feuCroisement = (d[0] >> 4) & 1;
                v.feuRoute = (d[0] >> 6) & 1;
                v.clignotantGauche = (d[1] >> 2) & 1;
                v.clignotantDroit = (d[1] >> 4) & 1;
                v.warning = d[2] & 1;
            }
            break;
        case 2304:    // Indian : capteur de chute (127 = droite, ~113 = sur la béquille)
            if (n >= 1 && d[0] != 0xFF) v.inclinaison = d[0];
            break;
        case 65226:   // DM1 : codes défaut actifs (message court)
            decoderLesCodesDefaut(sa, d, n);
            break;
        default:
            break;
    }
}

// Messages J1939 de plus de 8 octets diffusés par BAM (TP.CM 60416 puis TP.DT 60160).
void reassemblerUnMessageLong(uint32_t pgn, uint8_t sa, const uint8_t* d, int n) {
    MessageLong& m = messagesLongs[sa % 8];
    if (pgn == 60416 && n >= 8 && d[0] == 0x20) {          // annonce BAM
        m.actif = true;
        m.taille = min<uint16_t>(mot(d, 1), sizeof(m.octets));
        m.paquets = d[3];
        m.recus = 0;
        m.pgn = d[5] | (d[6] << 8) | ((uint32_t)d[7] << 16);
    } else if (pgn == 60160 && n >= 2 && m.actif) {         // un paquet de données
        const int numero = d[0];
        if (numero < 1 || numero > m.paquets) { m.actif = false; return; }
        for (int i = 1; i < n; i++) {
            const int position = (numero - 1) * 7 + (i - 1);
            if (position < m.taille) m.octets[position] = d[i];
        }
        m.recus++;
        if (m.recus >= m.paquets) {
            m.actif = false;
            if (m.pgn == 65226) decoderLesCodesDefaut(sa, m.octets, m.taille);
        }
    }
}

// Réponses OBD-II (11 bits, 0x7E8 à 0x7EF), si un outil interroge la moto pendant l'écoute.
void decoderOBD(const uint8_t* d, int n) {
    if (n < 4 || d[1] != 0x41) return;
    ValeursDecodees& v = valeurs;
    v.obd = true;
    const uint8_t a = d[3], b = n > 4 ? d[4] : 0;
    switch (d[2]) {
        case 0x05: v.temperatureMoteur = a - 40.0f; break;
        case 0x0C: v.regime = (a * 256 + b) / 4.0f; break;
        case 0x0D: v.vitesse = a; break;
        case 0x11: v.papillon = a * 100.0f / 255.0f; break;
        case 0x2F: v.carburant = a * 100.0f / 255.0f; break;
        case 0x42: v.tensionBatterie = (a * 256 + b) / 1000.0f; break;
        case 0x46: v.temperatureExterieure = a - 40.0f; break;
        default: break;
    }
}

// ─────────────────────────────── Liste des trames vues ───────────────────────────────

void retenirLaTrame(const twai_message_t& m) {
    const bool etendue = m.extd;
    for (int i = 0; i < nombreDeTramesVues; i++) {
        TrameCANVue& t = tramesVues[i];
        if (t.identifiant == m.identifier && t.etendue == etendue) {
            t.longueur = m.data_length_code;
            memcpy(t.donnees, m.data, 8);
            t.compteur++;
            t.derniereFoisMs = millis();
            return;
        }
    }
    if (nombreDeTramesVues >= NOMBRE_MAXIMUM_DE_TRAMES_VUES) return;
    TrameCANVue& t = tramesVues[nombreDeTramesVues++];
    t.identifiant = m.identifier;
    t.etendue = etendue;
    t.longueur = m.data_length_code;
    memcpy(t.donnees, m.data, 8);
    t.compteur = 1;
    t.derniereFoisMs = millis();
}

void traiterUneTrame(const twai_message_t& m) {
    if (m.rtr) return;
    const int n = min<int>(m.data_length_code, 8);
    xSemaphoreTake(verrou, portMAX_DELAY);
    retenirLaTrame(m);
    if (m.extd) {
        const uint32_t pgn = pgnJ1939(m.identifier);
        const uint8_t  sa  = sourceJ1939(m.identifier);
        if (pgn == 60416 || pgn == 60160) reassemblerUnMessageLong(pgn, sa, m.data, n);
        else decoderJ1939(pgn, sa, m.data, n);
    } else if (m.identifier >= 0x7E8 && m.identifier <= 0x7EF) {
        decoderOBD(m.data, n);
    }
    xSemaphoreGive(verrou);
}

// ─────────────────────────────── Enregistrement sur la carte SD ───────────────────────────────

void ouvrirUnNouveauFichier() {
    String dossier = String(DOSSIER_DES_ENREGISTREMENTS) + "/" + (lHeureEstConnue() ? dateDuJourPourNomDeFichier() : String("sans-date"));
    String nom = lHeureEstConnue() ? dateEtHeurePourNomDeFichier()
                                   : String("demarrage-") + nombreDeDemarrages() + "_" + (millis() / 1000) + "s";
    String chemin = dossier + "/" + nom + ".csv";
    creerUnDossier(dossier.c_str());
    char entete[96];
    snprintf(entete, sizeof(entete), "# bus CAN %d kbit/s, firmware %s\nms;id;type;dlc;donnees\n", vitesseActuelle, VERSION_DU_FIRMWARE);
    ajouterALaFinDUnFichier(chemin.c_str(), entete);
    xSemaphoreTake(verrou, portMAX_DELAY);
    fichierEnCours = chemin;
    xSemaphoreGive(verrou);
    ecrireDansLeJournal("[CAN] Enregistrement des trames dans %s", chemin.c_str());
}

void ajouterAuTampon(const twai_message_t& m) {
    if (tamponSD == nullptr) return;
    if (remplissageSD + 48 > TAILLE_DU_TAMPON_SD) { trameManqueesSD++; return; }
    char* p = tamponSD + remplissageSD;
    int ecrit = snprintf(p, 48, "%lu;%0*lX;%c;%d;", (unsigned long)millis(), m.extd ? 8 : 3,
                         (unsigned long)m.identifier, m.extd ? 'X' : 'S', m.data_length_code);
    for (int i = 0; i < min<int>(m.data_length_code, 8); i++) {
        static const char hex[] = "0123456789ABCDEF";
        p[ecrit++] = hex[m.data[i] >> 4];
        p[ecrit++] = hex[m.data[i] & 15];
    }
    p[ecrit++] = '\n';
    remplissageSD += ecrit;
}

void viderLeTamponSurLaCarteSD() {
    if (remplissageSD == 0) return;
    String chemin;
    xSemaphoreTake(verrou, portMAX_DELAY);
    chemin = fichierEnCours;
    xSemaphoreGive(verrou);
    if (chemin.length() == 0 || !carteSDMontee()) { remplissageSD = 0; return; }
    if (prendreLaCarteSD(500)) {
        File fichier = systemeDeFichiersDeLaCarteSD().open(chemin.c_str(), FILE_APPEND);
        if (fichier) {
            fichier.write((const uint8_t*)tamponSD, remplissageSD);
            fichier.close();
        }
        rendreLaCarteSD();
    }
    remplissageSD = 0;
}

// ─────────────────────────────── Pilote TWAI ───────────────────────────────

twai_timing_config_t reglageDeVitesse(int kbits) {
    switch (kbits) {
        case 125:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS(); return t; }
        case 500:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS(); return t; }
        case 1000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();   return t; }
        default:   { twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS(); return t; }
    }
}

void arreterLePilote() {
    if (!driverInstalle) return;
    twai_stop();
    twai_driver_uninstall();
    driverInstalle = false;
}

bool demarrerLePilote(int kbits) {
    arreterLePilote();
    // ÉCOUTE SEULE : le contrôleur n'émet jamais, même pas l'acquittement des trames.
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)BROCHE_CAN_TX, (gpio_num_t)BROCHE_CAN_RX, TWAI_MODE_LISTEN_ONLY);
    general.rx_queue_len = 256;
    general.tx_queue_len = 0;
    const twai_timing_config_t vitesse = reglageDeVitesse(kbits);
    const twai_filter_config_t filtre = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&general, &vitesse, &filtre) != ESP_OK) return false;
    if (twai_start() != ESP_OK) { twai_driver_uninstall(); return false; }
    driverInstalle = true;
    return true;
}

uint32_t erreursActuelles() {
    twai_status_info_t s;
    if (!driverInstalle || twai_get_status_info(&s) != ESP_OK) return 0;
    return s.bus_error_count;
}

// Écoute une vitesse pendant un moment : true si des trames arrivent sans erreur.
bool essayerUneVitesse(int kbits) {
    if (!demarrerLePilote(kbits)) return false;
    uint32_t recues = 0;
    const uint32_t debut = millis();
    twai_message_t m;
    while (millis() - debut < DUREE_D_ECOUTE_PAR_VITESSE_MS) {
        if (twai_receive(&m, pdMS_TO_TICKS(100)) == ESP_OK) recues++;
    }
    const uint32_t erreurs = erreursActuelles();
    ecrireDansLeJournal("[CAN] Essai à %d kbit/s : %lu trame(s), %lu erreur(s).", kbits, (unsigned long)recues, (unsigned long)erreurs);
    if (recues >= TRAMES_POUR_VALIDER_UNE_VITESSE && erreurs <= recues / 10) return true;
    arreterLePilote();
    return false;
}

// Trouve la vitesse du bus. Renvoie 0 si le bus est silencieux.
int trouverLaVitesse() {
    recherche = true;
    int trouvee = 0;
    if (vitesseReglee > 0) {
        if (demarrerLePilote(vitesseReglee)) trouvee = vitesseReglee;
    } else {
        for (int kbits : VITESSES_A_ESSAYER) {
            if (vitesseAChanger) break;
            if (essayerUneVitesse(kbits)) { trouvee = kbits; break; }
        }
    }
    recherche = false;
    return trouvee;
}

// ─────────────────────────────── La tâche ───────────────────────────────

void tacheCANBus(void*) {
    bool silenceSignale = false;
    bool rechercheSansSuccesSignalee = false;
    for (;;) {
        vitesseAChanger = false;
        vitesseActuelle = 0;
        const int kbits = trouverLaVitesse();
        if (kbits == 0) {
            if (!rechercheSansSuccesSignalee) {
                ecrireDansLeJournal("[CAN] Aucune trame : moto coupée, ou transceiver non branché. Nouvel essai toutes les %lu s.",
                                    (unsigned long)(PAUSE_ENTRE_DEUX_RECHERCHES_MS / 1000));
                rechercheSansSuccesSignalee = true;
            }
            noterUnProbleme("Aucune trame reçue (moto coupée ou câblage)");
            for (uint32_t attente = 0; attente < PAUSE_ENTRE_DEUX_RECHERCHES_MS && !vitesseAChanger; attente += 100) vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        rechercheSansSuccesSignalee = false;
        vitesseActuelle = kbits;
        noterUnProbleme("");
        ecrireDansLeJournal("[CAN] Écoute du bus à %d kbit/s (écoute seule, rien n'est émis).", kbits);

        uint32_t debutDeSeconde = millis(), tramesDeLaSeconde = 0, derniereEcriture = millis();
        uint32_t dernierCompteDErreurs = erreursActuelles();
        uint32_t secondesEnErreur = 0;
        bool fichierOuvert = false;
        twai_message_t m;

        while (!vitesseAChanger) {
            if (effacementDemande) {
                xSemaphoreTake(verrou, portMAX_DELAY);
                nombreDeTramesVues = 0;
                nombreDeCodesDefaut = 0;
                valeurs.lampes = -1;
                xSemaphoreGive(verrou);
                effacementDemande = false;
            }
            if (twai_receive(&m, pdMS_TO_TICKS(100)) == ESP_OK) {
                const uint32_t maintenant = millis();
                // Reprise après un long silence (nouvelle sortie) : nouveau fichier.
                if (enregistrement && (!fichierOuvert || maintenant - derniereTrameMs > SILENCE_AVANT_NOUVEAU_FICHIER_MS)) {
                    viderLeTamponSurLaCarteSD();
                    if (carteSDMontee()) { ouvrirUnNouveauFichier(); fichierOuvert = true; }
                }
                if (silenceSignale) { ecrireDansLeJournal("[CAN] Le bus reprend."); silenceSignale = false; }
                derniereTrameMs = maintenant;
                totalDesTrames++;
                tramesDeLaSeconde++;
                traiterUneTrame(m);
                if (enregistrement && fichierOuvert) ajouterAuTampon(m);
            }

            const uint32_t maintenant = millis();
            if (maintenant - debutDeSeconde >= 1000) {
                tramesParSeconde = tramesDeLaSeconde;
                tramesDeLaSeconde = 0;
                debutDeSeconde = maintenant;
                const uint32_t erreurs = erreursActuelles();
                const uint32_t nouvelles = erreurs - dernierCompteDErreurs;
                dernierCompteDErreurs = erreurs;
                erreursDuBus = erreurs;
                // Beaucoup d'erreurs et aucune trame : mauvaise vitesse ou câblage inversé.
                secondesEnErreur = (nouvelles > 20 && tramesParSeconde == 0) ? secondesEnErreur + 1 : 0;
                if (secondesEnErreur >= 3) {
                    ecrireDansLeJournal("[CAN] Trop d'erreurs à %d kbit/s : CAN H et CAN L inversés, ou mauvaise vitesse.", kbits);
                    noterUnProbleme("Erreurs sur le bus : vérifier CAN H / CAN L ou la vitesse");
                    if (vitesseReglee == 0) break;   // en automatique, on recherche à nouveau
                    secondesEnErreur = 0;
                }
                if (!silenceSignale && derniereTrameMs != 0 && maintenant - derniereTrameMs > SILENCE_AVANT_MOTO_COUPEE_MS) {
                    ecrireDansLeJournal("[CAN] Plus de trames depuis %lu s (moto coupée ?).", (unsigned long)(SILENCE_AVANT_MOTO_COUPEE_MS / 1000));
                    silenceSignale = true;
                    viderLeTamponSurLaCarteSD();
                }
            }
            if (maintenant - derniereEcriture >= PERIODE_D_ECRITURE_SD_MS || remplissageSD > TAILLE_DU_TAMPON_SD * 3 / 4) {
                derniereEcriture = maintenant;
                if (enregistrement) viderLeTamponSurLaCarteSD(); else remplissageSD = 0;
            }
        }
        viderLeTamponSurLaCarteSD();
        arreterLePilote();
    }
}

void ajouterSiConnu(JsonObject objet, const char* cle, float valeur, int decimales = 1) {
    if (isnan(valeur)) return;
    const float facteur = powf(10, decimales);
    objet[cle] = roundf(valeur * facteur) / facteur;
}
void ajouterSiConnu(JsonObject objet, const char* cle, int valeur) {
    if (valeur >= 0) objet[cle] = valeur;
}

} // namespace

// ─────────────────────────────── Fonctions publiques ───────────────────────────────

bool busCANActif() { return derniereTrameMs != 0 && millis() - derniereTrameMs < SILENCE_AVANT_MOTO_COUPEE_MS; }

int nombreDeTramesCANVues() {
    xSemaphoreTake(verrou, portMAX_DELAY);
    const int n = nombreDeTramesVues;
    xSemaphoreGive(verrou);
    return n;
}

bool trameCANVue(int numero, TrameCANVue& trame) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    const bool existe = numero >= 0 && numero < nombreDeTramesVues;
    if (existe) trame = tramesVues[numero];
    xSemaphoreGive(verrou);
    return existe;
}

bool choisirLaVitesseDuBusCAN(int kbits) {
    if (kbits != 0 && kbits != 125 && kbits != 250 && kbits != 500 && kbits != 1000) return false;
    vitesseReglee = kbits;
    enregistrerLesReglages();
    vitesseAChanger = true;
    ecrireDansLeJournal("[CAN] Vitesse du bus : %s.", kbits == 0 ? "recherche automatique" : String(String(kbits) + " kbit/s").c_str());
    return true;
}

void activerLEnregistrementCAN(bool actif) {
    enregistrement = actif;
    enregistrerLesReglages();
    ecrireDansLeJournal("[CAN] Enregistrement des trames sur la carte SD %s.", actif ? "activé" : "coupé");
}

bool enregistrementCANActif() { return enregistrement; }

void effacerLesTramesCANVues() { effacementDemande = true; }

void decrireLeBusCAN(JsonDocument& document) {
    document["t"]      = "can";
    document["etat"]   = recherche ? "recherche" : (busCANActif() ? "actif" : (vitesseActuelle ? "silence" : "absent"));
    document["kbps"]   = (int)vitesseActuelle;
    document["reglage"] = (int)vitesseReglee;
    document["tps"]    = busCANActif() ? (uint32_t)tramesParSeconde : 0;
    document["total"]  = (uint32_t)totalDesTrames;
    document["err"]    = (uint32_t)erreursDuBus;
    document["enr"]    = enregistrement ? 1 : 0;
    document["perdues"] = (uint32_t)trameManqueesSD;
    document["tx"]     = BROCHE_CAN_TX;
    document["rx"]     = BROCHE_CAN_RX;
    document["age"]    = derniereTrameMs ? (long)((millis() - derniereTrameMs) / 1000) : -1L;

    xSemaphoreTake(verrou, portMAX_DELAY);
    const ValeursDecodees v = valeurs;
    document["ids"]     = nombreDeTramesVues;
    document["fichier"] = fichierEnCours;
    document["pb"]      = dernierProbleme;
    JsonArray codes = document["dtc"].to<JsonArray>();
    for (int i = 0; i < nombreDeCodesDefaut; i++) {
        JsonArray c = codes.add<JsonArray>();
        c.add(codesDefaut[i].spn);
        c.add(codesDefaut[i].fmi);
        c.add(codesDefaut[i].occurrences);
        c.add(codesDefaut[i].source);
    }
    xSemaphoreGive(verrou);

    JsonObject d = document["v"].to<JsonObject>();
    ajouterSiConnu(d, "rpm", v.regime, 0);
    ajouterSiConnu(d, "vit", v.vitesse);
    ajouterSiConnu(d, "vav", v.vitesseAvant);
    ajouterSiConnu(d, "pat", v.patinage);
    if (v.rapport) d["rap"] = String(v.rapport);
    ajouterSiConnu(d, "pap", v.papillon, 0);
    ajouterSiConnu(d, "cpl", v.couple, 0);
    ajouterSiConnu(d, "tmot", v.temperatureMoteur, 0);
    ajouterSiConnu(d, "carb", v.carburant, 0);
    ajouterSiConnu(d, "auto", v.autonomieKm, 0);
    ajouterSiConnu(d, "debit", v.debitCarburant, 2);
    ajouterSiConnu(d, "conso", v.consoInstantanee);
    ajouterSiConnu(d, "cmoy", v.consoMoyenne);
    ajouterSiConnu(d, "bat", v.tensionBatterie, 2);
    ajouterSiConnu(d, "text", v.temperatureExterieure);
    ajouterSiConnu(d, "odo", v.odometre);
    ajouterSiConnu(d, "trip", v.trip);
    ajouterSiConnu(d, "pav", v.pressionAvant, 2);
    ajouterSiConnu(d, "par", v.pressionArriere, 2);
    ajouterSiConnu(d, "tpav", v.temperaturePneuAvant, 0);
    ajouterSiConnu(d, "tpar", v.temperaturePneuArriere, 0);
    ajouterSiConnu(d, "frein", v.frein);
    ajouterSiConnu(d, "embr", v.embrayage);
    ajouterSiConnu(d, "regul", v.regulateur);
    ajouterSiConnu(d, "coupe", v.coupeCircuit);
    ajouterSiConnu(d, "beq", v.bequille);
    ajouterSiConnu(d, "chute", v.inclinaison);
    ajouterSiConnu(d, "clig", v.clignotantGauche);
    ajouterSiConnu(d, "clid", v.clignotantDroit);
    ajouterSiConnu(d, "warn", v.warning);
    ajouterSiConnu(d, "croi", v.feuCroisement);
    ajouterSiConnu(d, "route", v.feuRoute);
    ajouterSiConnu(d, "lamp", v.lampes);
    if (v.obd) d["obd"] = 1;
}

String resumeDuBusCAN() {
    if (!busCANActif()) return "";
    xSemaphoreTake(verrou, portMAX_DELAY);
    const ValeursDecodees v = valeurs;
    const int codes = nombreDeCodesDefaut;
    xSemaphoreGive(verrou);
    String texte = "Moto:";
    if (!isnan(v.regime))  texte += " " + String((int)v.regime) + "tr/min";
    if (!isnan(v.vitesse)) texte += " " + String((int)v.vitesse) + "km/h";
    if (v.rapport)         texte += String(" rapport ") + v.rapport;
    if (!isnan(v.carburant)) texte += " essence " + String((int)v.carburant) + "%";
    if (!isnan(v.autonomieKm)) texte += " autonomie " + String((int)v.autonomieKm) + "km";
    if (!isnan(v.temperatureMoteur)) texte += " moteur " + String((int)v.temperatureMoteur) + "C";
    if (!isnan(v.tensionBatterie)) texte += " batterie " + String(v.tensionBatterie, 1) + "V";
    if (codes > 0) texte += " defauts " + String(codes);
    return texte;
}

bool demarrerModuleCANBus() {
    verrou = xSemaphoreCreateMutex();
    if (verrou == nullptr) return false;
    lireLesReglages();
    if (BROCHE_CAN_TX < 0 || BROCHE_CAN_RX < 0) {
        ecrireDansLeJournal("[CAN] Bus CAN désactivé (broches à -1 dans configuration.h).");
        return true;
    }
    if (versionDeLaCarte() == VersionDeCarte::V1) {
        // Sur la carte V1, le GPIO 3 est la ligne SDA de la jauge de batterie.
        ecrireDansLeJournal("[CAN] Carte V1 : le GPIO %d sert à la jauge de batterie, bus CAN désactivé.", BROCHE_CAN_TX);
        return true;
    }
    tamponSD = (char*)(psramFound() ? ps_malloc(TAILLE_DU_TAMPON_SD) : malloc(TAILLE_DU_TAMPON_SD));
    if (tamponSD == nullptr) ecrireDansLeJournal("[CAN] Pas de mémoire pour le tampon : pas d'enregistrement sur la carte SD.");
    ecrireDansLeJournal("[CAN] Transceiver sur GPIO %d (TX) et GPIO %d (RX), vitesse %s, enregistrement %s.",
                        BROCHE_CAN_TX, BROCHE_CAN_RX,
                        vitesseReglee == 0 ? "automatique" : String(String((int)vitesseReglee) + " kbit/s").c_str(),
                        enregistrement ? "activé" : "coupé");
    return creerTacheSurveillee(tacheCANBus, "Bus CAN", 6144, PRIORITE_NORMALE, COEUR_TRAITEMENTS);
}
