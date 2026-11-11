#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <map>
#include <vector>
#include <functional>
#include <math.h>
#include <time.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <mbedtls/platform.h>

#include "certificats_racines.h"
#include "configuration.h"
#include "secrets.h"
#include "module_synchro.h"
#include "module_appli.h"
#include "module_camera.h"
#include "module_canbus.h"
#include "module_carte_sd.h"
#include "module_gps.h"
#include "module_horloge.h"
#include "module_journal.h"
#include "module_systeme.h"
#include "module_wifi.h"

// Valeurs par défaut des réglages : à mettre dans include/secrets.h (jamais publié).
// Elles restent facultatives : tout se règle aussi depuis l'appli.
#ifndef SECRET_WIFI_MAISON_NOM
#define SECRET_WIFI_MAISON_NOM ""
#endif
#ifndef SECRET_WIFI_MAISON_MOT_DE_PASSE
#define SECRET_WIFI_MAISON_MOT_DE_PASSE ""
#endif
#ifndef SECRET_API_ADRESSE
#define SECRET_API_ADRESSE ""
#endif
#ifndef SECRET_API_CLE
#define SECRET_API_CLE ""
#endif
#ifndef SECRET_GOOGLE_CLIENT_ID
#define SECRET_GOOGLE_CLIENT_ID ""
#endif
#ifndef SECRET_GOOGLE_CLIENT_SECRET
#define SECRET_GOOGLE_CLIENT_SECRET ""
#endif
#ifndef SECRET_GOOGLE_JETON
#define SECRET_GOOGLE_JETON ""
#endif

namespace {

// ─────────────────────────────── Réglages ───────────────────────────────

const uint32_t ATTENTE_APRES_LE_DEMARRAGE_MS    = 45 * UNE_SECONDE_MS;
const uint32_t INTERVALLE_DE_RECHERCHE_MS       = UNE_MINUTE_MS;        // on cherche le Wi-Fi de la maison
const uint32_t INTERVALLE_ENTRE_DEUX_SYNCHROS_MS = 15 * UNE_MINUTE_MS;  // une fois à la maison
const float    VITESSE_MAXIMUM_POUR_CHERCHER    = 15.0f;                // km/h : en roulant, inutile de chercher
const uint32_t DELAI_DE_CONNEXION_MS            = 20 * UNE_SECONDE_MS;
const size_t   TAILLE_D_UN_PAQUET               = 24 * 1024;            // octets lus sur la carte SD par requête
const char*    FICHIER_DE_PROGRESSION           = DOSSIER_DE_LA_SYNCHRO "/progression.txt";
const char*    DOSSIER_DES_TRACES_GPS           = "/gps";
const char*    DOSSIER_DES_TRAMES_CAN           = "/can";
const char*    NOM_DE_L_ALBUM_GOOGLE            = "Moto ESP32";
const char*    NOM_DU_DOSSIER_DRIVE             = "Moto ESP32";   // classeurs (journal, GPS) et trames CAN
const size_t   TAILLE_D_UN_MORCEAU_DRIVE        = 512 * 1024;   // multiple de 256 Kio, exigé par Google Drive

struct Reglages {
    String wifiNom, wifiMotDePasse;
    String api, cle;
    String googleId, googleSecret, googleJeton, album;
    bool   active = true;
    int    modeCAN = 1;   // trames CAN brutes : 0 = gardées sur la carte, 1 = Google Drive, 2 = base de données
    String dossierDrive;
};

struct Bilan {
    uint32_t journaux = 0, positions = 0, trames = 0, fichiersCAN = 0, photos = 0, erreurs = 0;
};

SemaphoreHandle_t verrou = nullptr;
Reglages          reglages;          // protégé par le verrou
String            etatLisible = "En attente";   // protégé par le verrou
String            derniereErreur;               // protégé par le verrou
Bilan             dernierBilan;                 // protégé par le verrou
time_t            heureDeLaDerniereSynchro = 0;
uint32_t          derniereSynchroReussieMs = 0;

TaskHandle_t      tacheSynchro = nullptr;
volatile bool     enCours = false;
volatile bool     demandeManuelle = false;
volatile bool     interruption = false;
volatile bool     testDemande = false;
bool              suspendue = false;           // le module a planté pendant la dernière synchro : plus d'automatique
const size_t      MEMOIRE_INTERNE_MINIMUM = 40 * 1024;   // le Wi-Fi en prend ≈ 35 Ko ; en dessous, il étoufferait le Bluetooth
volatile uint8_t  raisonDeDeconnexion = 0;     // dernier code de l'ESP32 quand la box refuse la connexion
int               etatDuDernierTest = -1;      // -1 aucun, 0 échec, 1 réussi, 2 en cours (protégé par le verrou)
String            messageDuDernierTest;        // protégé par le verrou

// ─────────────────────────────── Outils ───────────────────────────────

Reglages copieDesReglages() {
    xSemaphoreTake(verrou, portMAX_DELAY);
    Reglages copie = reglages;
    xSemaphoreGive(verrou);
    return copie;
}

void publierLEtat() {
    JsonDocument d;
    decrireLaSynchronisation(d);
    publierUnEvenementVersLAppli(d);
}

void noterLEtat(const String& texte, bool journal = true) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    etatLisible = texte;
    xSemaphoreGive(verrou);
    if (journal) ecrireDansLeJournal("[SYNCHRO] %s", texte.c_str());
    publierLEtat();
}

void noterUneErreur(const String& texte) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    derniereErreur = texte;
    xSemaphoreGive(verrou);
    ecrireDansLeJournal("[SYNCHRO] Erreur : %s", texte.c_str());
}

void enregistrerUnReglage(const char* cle, const String& valeur) {
    Preferences memoire;
    memoire.begin("synchro", false);
    memoire.putString(cle, valeur);
    memoire.end();
}

void enregistrerUnInterrupteur(const char* cle, bool valeur) {
    Preferences memoire;
    memoire.begin("synchro", false);
    memoire.putBool(cle, valeur);
    memoire.end();
}

void lireLesReglages() {
    Preferences memoire;
    memoire.begin("synchro", true);
    reglages.wifiNom        = memoire.getString("ssid",     SECRET_WIFI_MAISON_NOM);
    reglages.wifiMotDePasse = memoire.getString("mdp",      SECRET_WIFI_MAISON_MOT_DE_PASSE);
    reglages.api            = memoire.getString("api",      SECRET_API_ADRESSE);
    reglages.cle            = memoire.getString("cle",      SECRET_API_CLE);
    reglages.googleId       = memoire.getString("g_id",     SECRET_GOOGLE_CLIENT_ID);
    reglages.googleSecret   = memoire.getString("g_secret", SECRET_GOOGLE_CLIENT_SECRET);
    reglages.googleJeton    = memoire.getString("g_jeton",  SECRET_GOOGLE_JETON);
    reglages.album          = memoire.getString("album",    "");
    reglages.active         = memoire.getBool("active", true);
    reglages.modeCAN        = memoire.getInt("can", 1);
    reglages.dossierDrive   = memoire.getString("dossier", "");
    memoire.end();
}

// Empreinte d'un chemin (FNV-1a 64 bits, rendue positive) : identifiant stable d'un fichier
// dans la base, calculé ici sans avoir à interroger le serveur.
int64_t empreinteDuChemin(const String& chemin) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < chemin.length(); i++) {
        h ^= (uint8_t)chemin[i];
        h *= 1099511628211ULL;
    }
    return (int64_t)(h & 0x7FFFFFFFFFFFFFFFULL);
}

String nombreLong(int64_t valeur) {
    char texte[24];
    snprintf(texte, sizeof(texte), "%lld", (long long)valeur);
    return texte;
}

// Date et heure UTC au format ISO 8601, comprise par PostgreSQL (timestamptz).
String dateISO(time_t secondesUTC) {
    struct tm u;
    gmtime_r(&secondesUTC, &u);
    char texte[24];
    strftime(texte, sizeof(texte), "%Y-%m-%dT%H:%M:%SZ", &u);
    return texte;
}

// Heure locale (France) → secondes UTC, heure d'été comprise (le fuseau est réglé par module_horloge).
time_t heureLocaleEnSecondes(int jour, int mois, int annee, int heure, int minute, int seconde) {
    struct tm t = {};
    t.tm_mday = jour; t.tm_mon = mois - 1; t.tm_year = annee - 1900;
    t.tm_hour = heure; t.tm_min = minute; t.tm_sec = seconde;
    t.tm_isdst = -1;
    return mktime(&t);
}

// « 03-11-2026 » ou « 03-11-2026_14-05-09... » au début d'un nom de fichier → secondes UTC (0 si absent).
time_t dateDuNomDeFichier(const String& nom, bool avecLHeure) {
    int j = 0, m = 0, a = 0, h = 0, mi = 0, s = 0;
    const int lus = sscanf(nom.c_str(), "%d-%d-%d_%d-%d-%d", &j, &m, &a, &h, &mi, &s);
    if (lus < 3 || a < 2020 || m < 1 || m > 12 || j < 1 || j > 31) return 0;
    if (avecLHeure && lus < 6) return 0;
    return heureLocaleEnSecondes(j, m, a, avecLHeure ? h : 0, avecLHeure ? mi : 0, avecLHeure ? s : 0);
}

String nomSansDossier(const String& chemin) {
    const int barre = chemin.lastIndexOf('/');
    return barre < 0 ? chemin : chemin.substring(barre + 1);
}

// Remplace les octets UTF-8 invalides (ligne de journal coupée au milieu d'un accent...)
// par « ? » : PostgreSQL refuserait sinon tout le paquet.
String texteUTF8Propre(const char* texte) {
    String propre;
    const uint8_t* p = (const uint8_t*)texte;
    while (*p) {
        int suite = 0;
        if (*p < 0x80)                suite = 0;
        else if ((*p & 0xE0) == 0xC0) suite = 1;
        else if ((*p & 0xF0) == 0xE0) suite = 2;
        else if ((*p & 0xF8) == 0xF0) suite = 3;
        else { propre += '?'; p++; continue; }
        bool valide = true;
        for (int i = 1; i <= suite; i++) if ((p[i] & 0xC0) != 0x80) { valide = false; break; }
        if (!valide) { propre += '?'; p++; continue; }
        if (*p < 0x20 && *p != '\t') propre += ' ';
        else for (int i = 0; i <= suite; i++) propre += (char)p[i];
        p += suite + 1;
    }
    return propre;
}

String encoderPourURL(const String& texte) {
    String resultat;
    const char* chiffres = "0123456789ABCDEF";
    for (size_t i = 0; i < texte.length(); i++) {
        const char c = texte[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') resultat += c;
        else { resultat += '%'; resultat += chiffres[(uint8_t)c >> 4]; resultat += chiffres[(uint8_t)c & 15]; }
    }
    return resultat;
}

bool estHexadecimal(const char* texte, size_t longueur) {
    for (size_t i = 0; i < longueur; i++) if (!isxdigit((unsigned char)texte[i])) return false;
    return true;
}

// ─────────────────────────────── Progression (ce qui est déjà parti) ───────────────────────────────

struct Avancement {
    uint32_t octets = 0;   // octets du fichier déjà envoyés
    uint32_t lignes = 0;   // lignes déjà lues (sert de numéro de ligne unique dans la base)
};
std::map<String, Avancement> progression;

void chargerLaProgression() {
    progression.clear();
    if (!prendreLaCarteSD(5000)) return;
    File fichier = systemeDeFichiersDeLaCarteSD().open(FICHIER_DE_PROGRESSION, FILE_READ);
    while (fichier && fichier.available()) {
        String ligne = fichier.readStringUntil('\n');
        const int a = ligne.indexOf(';');
        const int b = a < 0 ? -1 : ligne.indexOf(';', a + 1);
        if (b < 0) continue;
        Avancement avancement;
        avancement.octets = strtoul(ligne.substring(0, a).c_str(), nullptr, 10);
        avancement.lignes = strtoul(ligne.substring(a + 1, b).c_str(), nullptr, 10);
        progression[ligne.substring(b + 1)] = avancement;
    }
    if (fichier) fichier.close();
    rendreLaCarteSD();
}

void enregistrerLaProgression() {
    String texte;
    for (const auto& element : progression) {
        texte += String(element.second.octets) + ";" + String(element.second.lignes) + ";" + element.first + "\n";
    }
    ecrireUnFichier(FICHIER_DE_PROGRESSION, (const uint8_t*)texte.c_str(), texte.length());
}

// Fichiers d'un dossier (pas de récursion) : chemin complet et taille.
struct FichierTrouve { String chemin; uint32_t taille; };

std::vector<FichierTrouve> fichiersDuDossier(const String& dossier, const char* extension) {
    std::vector<FichierTrouve> trouves;
    listerUnDossier(dossier.c_str(), [&](const ElementDUnDossier& e) {
        if (!e.estUnDossier && e.nom.endsWith(extension)) trouves.push_back({ dossier + "/" + e.nom, e.tailleOctets });
    });
    return trouves;
}

std::vector<String> sousDossiers(const String& dossier) {
    std::vector<String> trouves;
    listerUnDossier(dossier.c_str(), [&](const ElementDUnDossier& e) {
        if (e.estUnDossier) trouves.push_back(dossier + "/" + e.nom);
    });
    return trouves;
}

// ─────────────────────────────── Requêtes HTTP ───────────────────────────────

// Une connexion gardée ouverte vers un même serveur (évite de refaire la poignée de main TLS
// à chaque paquet, ce qui prend plus d'une seconde sur l'ESP32).
struct Connexion {
    WiFiClientSecure securisee;
    WiFiClient       simple;
    HTTPClient       http;
};

struct EnTete { const char* nom; String valeur; };

int requeteHTTP(Connexion& c, const String& url, const char* methode, const uint8_t* corps, size_t taille,
                const std::vector<EnTete>& entetes, String* reponse, bool garderLaConnexion,
                const char* enteteALire = nullptr, String* valeurLue = nullptr) {
    c.http.setReuse(garderLaConnexion);
    c.http.setConnectTimeout(10000);
    c.http.setTimeout(30000);
    const bool chiffree = url.startsWith("https://");
    const bool pret = chiffree ? c.http.begin(c.securisee, url) : c.http.begin(c.simple, url);
    if (!pret) {
        if (reponse) *reponse = "adresse invalide";
        return -1;
    }
    for (const EnTete& e : entetes) c.http.addHeader(e.nom, e.valeur);
    if (enteteALire != nullptr) { const char* noms[] = { enteteALire }; c.http.collectHeaders(noms, 1); }
    const int code = c.http.sendRequest(methode, const_cast<uint8_t*>(corps), taille);
    if (enteteALire != nullptr && valeurLue != nullptr) *valeurLue = c.http.header(enteteALire);
    if (code > 0) {
        String texte = c.http.getString();   // à lire en entier pour pouvoir réutiliser la connexion
        if (reponse) *reponse = texte;
    } else if (reponse) {
        *reponse = HTTPClient::errorToString(code);
    }
    c.http.end();
    return code;
}

void preparerLaConnexion(Connexion& c) {
    c.securisee.setCACert(CERTIFICATS_RACINES);
    c.securisee.setHandshakeTimeout(20);
}

// ─────────────────────────────── Base de données (PostgREST / Supabase) ───────────────────────────────

enum class Resultat { REUSSI, REFUSE, ECHEC };   // REFUSE : données rejetées (on les saute), ECHEC : on arrête

Resultat insererDansLaTable(Connexion& c, const Reglages& r, const char* table, const String& corps, bool csv) {
    String url = r.api + "/" + table;
    std::vector<EnTete> entetes = {
        { "Content-Type",  csv ? String("text/csv") : String("application/json") },
        { "Prefer",        "resolution=ignore-duplicates,return=minimal" },
    };
    if (r.cle.length() > 0) {
        entetes.push_back({ "apikey", r.cle });
        // Les nouvelles clés Supabase (« sb_publishable_... ») ne sont pas des JWT : seulement dans « apikey ».
        if (!r.cle.startsWith("sb_")) entetes.push_back({ "Authorization", "Bearer " + r.cle });
    }
    String reponse;
    const int code = requeteHTTP(c, url, "POST", (const uint8_t*)corps.c_str(), corps.length(), entetes, &reponse, true);
    if (code >= 200 && code < 300) return Resultat::REUSSI;
    noterUneErreur(String(table) + " : HTTP " + code + " " + reponse.substring(0, 160));
    // 400 / 409 / 422 : ces lignes-là sont refusées (donnée invalide) ; on les saute pour ne pas bloquer.
    if (code == 400 || code == 409 || code == 422) return Resultat::REFUSE;
    return Resultat::ECHEC;   // réseau, clé refusée (401/403), table absente (404), serveur en panne (5xx)
}

// Fiche d'un fichier de la carte SD (table « fichiers »), envoyée avant ses lignes.
bool envoyerLaFicheDuFichier(Connexion& c, const Reglages& r, const String& chemin, const char* genre,
                             time_t debut, long long debutMs, int vitesseKbits, const String& idDrive = "") {
    JsonDocument d;
    d["id"]     = serialized(nombreLong(empreinteDuChemin(chemin)));
    d["chemin"] = chemin;
    d["genre"]  = genre;
    if (debut > 0) d["debut"] = dateISO(debut); else d["debut"] = nullptr;
    if (debutMs >= 0) d["debut_ms"] = debutMs; else d["debut_ms"] = nullptr;
    if (vitesseKbits > 0) d["vitesse_kbits"] = vitesseKbits; else d["vitesse_kbits"] = nullptr;
    d["firmware"] = VERSION_DU_FIRMWARE;
    if (idDrive.length() > 0) d["drive_id"] = idDrive; else d["drive_id"] = nullptr;
    String corps;
    serializeJson(d, corps);
    return insererDansLaTable(c, r, "fichiers", corps, false) != Resultat::ECHEC;
}

// Convertit une ligne du fichier en une ligne à envoyer (objet JSON, tableau JSON ou ligne CSV).
// Renvoie false pour ignorer la ligne (en-tête, ligne abîmée).
typedef std::function<bool(const char* ligne, uint32_t numero, String& sortie)> Convertisseur;
// Envoie un paquet déjà mis en forme.
typedef std::function<Resultat(const String& corps)> Envoyeur;

// Forme d'un paquet : début, séparateur entre les lignes, fin.
struct FormeDuPaquet { String debut; char separateur; const char* fin; };

// Envoie la suite d'un fichier texte, par paquets de lignes complètes. [cle] repère l'avancement
// (un même fichier peut partir vers deux destinations). Renvoie le nombre de lignes envoyées,
// ou -1 si la synchronisation doit s'arrêter.
long envoyerLaSuiteDUnFichier(const FichierTrouve& fichier, const String& cle, const FormeDuPaquet& forme,
                              const Convertisseur& convertir, const Envoyeur& envoyer) {
    Avancement& avancement = progression[cle];
    if (fichier.taille < avancement.octets) avancement = Avancement();   // fichier remplacé : on recommence
    if (fichier.taille == avancement.octets) return 0;

    char* tampon = (char*)ps_malloc(TAILLE_D_UN_PAQUET + 1);
    if (tampon == nullptr) { noterUneErreur("mémoire insuffisante"); return -1; }
    long total = 0;
    while (avancement.octets < fichier.taille && !interruption) {
        size_t lus = 0;
        if (prendreLaCarteSD(5000)) {
            File f = systemeDeFichiersDeLaCarteSD().open(fichier.chemin, FILE_READ);
            if (f && f.seek(avancement.octets)) lus = f.read((uint8_t*)tampon, min<size_t>(TAILLE_D_UN_PAQUET, fichier.taille - avancement.octets));
            if (f) f.close();
            rendreLaCarteSD();
        }
        if (lus == 0) break;

        // On ne prend que des lignes complètes : la dernière est peut-être en cours d'écriture.
        size_t utile = lus;
        while (utile > 0 && tampon[utile - 1] != '\n') utile--;
        if (utile == 0) {
            if (lus < TAILLE_D_UN_PAQUET) break;   // ligne pas encore terminée : à la prochaine fois
            utile = lus;                           // ligne géante (fichier abîmé) : coupée
        }

        String corps;
        corps.reserve(utile * 2);
        corps += forme.debut;
        uint32_t numero = avancement.lignes;
        int lignesDuPaquet = 0;
        char* debut = tampon;
        char* fin = tampon + utile;
        while (debut < fin) {
            char* finDeLigne = (char*)memchr(debut, '\n', fin - debut);
            if (finDeLigne == nullptr) finDeLigne = fin;
            *finDeLigne = 0;
            if (finDeLigne > debut && finDeLigne[-1] == '\r') finDeLigne[-1] = 0;
            numero++;
            String convertie;
            if (*debut && convertir(debut, numero, convertie)) {
                if (lignesDuPaquet > 0) corps += forme.separateur;
                corps += convertie;
                lignesDuPaquet++;
            }
            debut = finDeLigne + 1;
        }
        corps += forme.fin;

        if (lignesDuPaquet > 0) {
            const Resultat resultat = envoyer(corps);
            if (resultat == Resultat::ECHEC) { free(tampon); return -1; }
            if (resultat == Resultat::REFUSE) {
                xSemaphoreTake(verrou, portMAX_DELAY); dernierBilan.erreurs++; xSemaphoreGive(verrou);
            } else {
                total += lignesDuPaquet;
            }
        }
        avancement.octets += utile;
        avancement.lignes = numero;
        enregistrerLaProgression();
    }
    free(tampon);
    return interruption ? -1 : total;
}

const FormeDuPaquet TABLEAU_JSON = { "[", ',', "]" };

// ─── Lecture des fichiers de la carte ───

// Journal : /journaux/jj-mm-aaaa.log, lignes « hh:mm:ss  texte ». Heure en secondes UTC (0 si inconnue).
struct LigneDeJournal { time_t heure; String texte; };

LigneDeJournal lireUneLigneDeJournal(const char* ligne, int jour, int mois, int annee) {
    LigneDeJournal l = { 0, String() };
    int h = 0, mi = 0, s = 0;
    const char* texte = ligne;
    if (sscanf(ligne, "%2d:%2d:%2d", &h, &mi, &s) == 3 && strlen(ligne) >= 8) {
        texte = ligne + 8;
        while (*texte == ' ') texte++;
        if (annee > 2000) l.heure = heureLocaleEnSecondes(jour, mois, annee, h, mi, s);
    }
    l.texte = texteUTF8Propre(texte);
    return l;
}

// Trace GPS : /gps/jj-mm-aaaa.csv, lignes « secondesUTC;lat;lon;alt;km/h;cap;satellites;hdop ».
struct PointLu { time_t heure; double latitude, longitude; float valeurs[5]; };   // altitude, km/h, cap, satellites, hdop (NAN = inconnu)

bool lireUnPointGPS(const char* ligne, PointLu& p) {
    if (!isdigit((unsigned char)ligne[0])) return false;   // en-tête
    String champs[8];
    String copie(ligne);
    int n = 0, debut = 0;
    while (n < 8) {
        const int pv = copie.indexOf(';', debut);
        champs[n++] = pv < 0 ? copie.substring(debut) : copie.substring(debut, pv);
        if (pv < 0) break;
        debut = pv + 1;
    }
    if (n < 3) return false;
    p.heure = (time_t)strtoul(champs[0].c_str(), nullptr, 10);
    p.latitude = strtod(champs[1].c_str(), nullptr);
    p.longitude = strtod(champs[2].c_str(), nullptr);
    if (p.heure < 1600000000 || !isfinite(p.latitude) || !isfinite(p.longitude) || fabs(p.latitude) > 90 || fabs(p.longitude) > 180) return false;
    for (int i = 0; i < 5; i++) p.valeurs[i] = 3 + i < n && champs[3 + i].length() ? strtof(champs[3 + i].c_str(), nullptr) : NAN;
    return true;
}

// Heure locale lisible, triable, comprise par Google Sheets : « 2026-11-10 14:05:09 ».
String heureLocaleLisible(time_t secondes) {
    if (secondes <= 0) return "";
    struct tm t;
    localtime_r(&secondes, &t);
    char texte[24];
    strftime(texte, sizeof(texte), "%Y-%m-%d %H:%M:%S", &t);
    return texte;
}

// ─── Base de données (facultative) : journaux ───

long envoyerLesJournaux(Connexion& c, const Reglages& r) {
    long total = 0;
    for (const FichierTrouve& fichier : fichiersDuDossier(DOSSIER_DES_JOURNAUX, ".log")) {
        if (interruption) return -1;
        if (progression[fichier.chemin].octets >= fichier.taille) continue;
        const String nom = nomSansDossier(fichier.chemin);
        const time_t minuit = dateDuNomDeFichier(nom, false);
        int jour = 0, mois = 0, annee = 0;
        sscanf(nom.c_str(), "%d-%d-%d", &jour, &mois, &annee);
        if (minuit == 0) annee = 0;
        if (!envoyerLaFicheDuFichier(c, r, fichier.chemin, "journal", minuit, -1, 0)) return -1;
        const String identifiant = nombreLong(empreinteDuChemin(fichier.chemin));
        const long envoyees = envoyerLaSuiteDUnFichier(fichier, fichier.chemin, TABLEAU_JSON,
            [&](const char* ligne, uint32_t numero, String& sortie) {
                const LigneDeJournal l = lireUneLigneDeJournal(ligne, jour, mois, annee);
                JsonDocument d;
                d["fichier_id"] = serialized(identifiant);
                d["ligne"] = numero;
                if (l.heure > 0) d["horodatage"] = dateISO(l.heure); else d["horodatage"] = nullptr;
                d["texte"] = l.texte;
                serializeJson(d, sortie);
                return true;
            },
            [&](const String& corps) { return insererDansLaTable(c, r, "journaux", corps, false); });
        if (envoyees < 0) return -1;
        total += envoyees;
    }
    return total;
}

// ─── Base de données (facultative) : trace GPS ───

long envoyerLesPositions(Connexion& c, const Reglages& r) {
    long total = 0;
    static const char* NOMS[5] = { "altitude", "vitesse_kmh", "cap", "satellites", "hdop" };
    for (const FichierTrouve& fichier : fichiersDuDossier(DOSSIER_DES_TRACES_GPS, ".csv")) {
        if (interruption) return -1;
        if (progression[fichier.chemin].octets >= fichier.taille) continue;
        const long envoyees = envoyerLaSuiteDUnFichier(fichier, fichier.chemin, TABLEAU_JSON,
            [](const char* ligne, uint32_t, String& sortie) {
                PointLu p;
                if (!lireUnPointGPS(ligne, p)) return false;
                JsonDocument d;
                d["horodatage"] = dateISO(p.heure);
                d["latitude"]   = serialized(String(p.latitude, 6));
                d["longitude"]  = serialized(String(p.longitude, 6));
                for (int i = 0; i < 5; i++) { if (isfinite(p.valeurs[i])) d[NOMS[i]] = p.valeurs[i]; else d[NOMS[i]] = nullptr; }
                serializeJson(d, sortie);
                return true;
            },
            [&](const String& corps) { return insererDansLaTable(c, r, "positions_gps", corps, false); });
        if (envoyees < 0) return -1;
        total += envoyees;
    }
    return total;
}

// ─── Trames CAN : /can/jj-mm-aaaa/*.csv, lignes « ms;identifiant;X|S;dlc;données » ───

// Lit l'en-tête d'un fichier CAN : vitesse du bus, et heure de début (ligne « # debut <secondes> <ms> »
// des firmwares 0.6.0 et suivants ; sinon heure du nom du fichier + ms de la première trame).
void lireLEnteteCAN(const FichierTrouve& fichier, int& kbits, time_t& debut, long long& debutMs) {
    kbits = 0; debut = 0; debutMs = -1;
    char texte[512];
    size_t lus = 0;
    if (prendreLaCarteSD(5000)) {
        File f = systemeDeFichiersDeLaCarteSD().open(fichier.chemin, FILE_READ);
        if (f) { lus = f.read((uint8_t*)texte, sizeof(texte) - 1); f.close(); }
        rendreLaCarteSD();
    }
    texte[lus] = 0;
    for (char* ligne = strtok(texte, "\n"); ligne != nullptr; ligne = strtok(nullptr, "\n")) {
        long secondes = 0; unsigned long ms = 0; int k = 0;
        if (sscanf(ligne, "# bus CAN %d", &k) == 1) kbits = k;
        else if (sscanf(ligne, "# debut %ld %lu", &secondes, &ms) == 2) { debut = secondes; debutMs = ms; }
        else if (isdigit((unsigned char)ligne[0]) && debutMs < 0) { debutMs = strtoull(ligne, nullptr, 10); break; }
    }
    if (debut == 0) debut = dateDuNomDeFichier(nomSansDossier(fichier.chemin), true);
    if (debut == 0) debutMs = -1;
}

long envoyerLesTramesCAN(Connexion& c, const Reglages& r) {
    long total = 0;
    for (const String& dossier : sousDossiers(DOSSIER_DES_TRAMES_CAN)) {
        for (const FichierTrouve& fichier : fichiersDuDossier(dossier, ".csv")) {
            if (interruption) return -1;
            if (progression[fichier.chemin].octets >= fichier.taille) continue;
            int kbits; time_t debut; long long debutMs;
            lireLEnteteCAN(fichier, kbits, debut, debutMs);
            if (!envoyerLaFicheDuFichier(c, r, fichier.chemin, "can", debut, debutMs, kbits)) return -1;
            const String identifiant = nombreLong(empreinteDuChemin(fichier.chemin));
            noterLEtat("Trames CAN : " + fichier.chemin, false);
            const long envoyees = envoyerLaSuiteDUnFichier(fichier, fichier.chemin,
                FormeDuPaquet{ "fichier_id,ligne,ms,identifiant,etendue,dlc,donnees\n", '\n', "\n" },
                [&](const char* ligne, uint32_t numero, String& sortie) {
                    if (!isdigit((unsigned char)ligne[0])) return false;   // commentaires et en-tête
                    const char* champs[5];
                    size_t longueurs[5];
                    const char* p = ligne;
                    for (int i = 0; i < 5; i++) {
                        const char* pv = strchr(p, ';');
                        champs[i] = p;
                        longueurs[i] = pv ? (size_t)(pv - p) : strlen(p);
                        if (!pv && i < 4) return false;
                        p = pv ? pv + 1 : p + longueurs[i];
                    }
                    if (longueurs[1] == 0 || longueurs[1] > 8 || !estHexadecimal(champs[1], longueurs[1])) return false;
                    if (longueurs[2] != 1 || (champs[2][0] != 'X' && champs[2][0] != 'S')) return false;
                    if (longueurs[3] == 0 || longueurs[3] > 2 || !isdigit((unsigned char)champs[3][0])) return false;
                    if (longueurs[4] > 16 || (longueurs[4] % 2) != 0 || !estHexadecimal(champs[4], longueurs[4])) return false;
                    sortie = identifiant;
                    sortie += ','; sortie += numero;
                    sortie += ','; sortie.concat(champs[0], longueurs[0]);
                    sortie += ','; sortie.concat(champs[1], longueurs[1]);
                    sortie += ','; sortie += champs[2][0] == 'X' ? "true" : "false";
                    sortie += ','; sortie.concat(champs[3], longueurs[3]);
                    sortie += ','; sortie.concat(champs[4], longueurs[4]);
                    return true;
                },
                [&](const String& corps) { return insererDansLaTable(c, r, "trames_can", corps, true); });
            if (envoyees < 0) return -1;
            total += envoyees;
        }
    }
    return total;
}

// ─────────────────────────────── Google Photos ───────────────────────────────

String   jetonDAccesGoogle;
String   raisonGoogle;   // pourquoi le dernier jeton a été refusé
uint32_t jetonValableJusquaMs = 0;

bool obtenirUnJetonGoogle(Connexion& c, const Reglages& r) {
    if (jetonDAccesGoogle.length() > 0 && (int32_t)(jetonValableJusquaMs - millis()) > 60000) return true;
    const String corps = "client_id=" + encoderPourURL(r.googleId) + "&client_secret=" + encoderPourURL(r.googleSecret)
                       + "&refresh_token=" + encoderPourURL(r.googleJeton) + "&grant_type=refresh_token";
    String reponse;
    const int code = requeteHTTP(c, "https://oauth2.googleapis.com/token", "POST", (const uint8_t*)corps.c_str(), corps.length(),
                                 { { "Content-Type", "application/x-www-form-urlencoded" } }, &reponse, false);
    JsonDocument d;
    if (code != 200 || deserializeJson(d, reponse) || !d["access_token"].is<const char*>()) {
        const String erreur = d["error"] | "";
        if (code < 0)                          raisonGoogle = "Google injoignable (pas d'Internet ?)";
        else if (erreur == "invalid_client")   raisonGoogle = "ID client ou code secret faux (Google Cloud > Identifiants)";
        else if (erreur == "invalid_grant")    raisonGoogle = "jeton refusé : expiré, révoqué ou créé avec un autre client. Refaire l'étape OAuth Playground";
        else if (erreur == "unauthorized_client") raisonGoogle = "ce client n'a pas le droit d'utiliser ce jeton (refaire le jeton avec ce client)";
        else                                   raisonGoogle = "Google refuse la connexion (HTTP " + String(code) + " " + erreur + ")";
        noterUneErreur(raisonGoogle);
        return false;
    }
    jetonDAccesGoogle = d["access_token"].as<const char*>();
    jetonValableJusquaMs = millis() + (d["expires_in"] | 3600) * 1000UL;
    return true;
}

// Crée l'album « Moto ESP32 » la première fois (son identifiant est gardé en mémoire).
void preparerLAlbumGoogle(Connexion& c, Reglages& r) {
    if (r.album.length() > 0) return;
    JsonDocument demande;
    demande["album"]["title"] = NOM_DE_L_ALBUM_GOOGLE;
    String corps, reponse;
    serializeJson(demande, corps);
    const int code = requeteHTTP(c, "https://photoslibrary.googleapis.com/v1/albums", "POST", (const uint8_t*)corps.c_str(), corps.length(),
                                 { { "Content-Type", "application/json" }, { "Authorization", "Bearer " + jetonDAccesGoogle } },
                                 &reponse, false);
    JsonDocument d;
    if (code == 200 && !deserializeJson(d, reponse) && d["id"].is<const char*>()) {
        r.album = d["id"].as<const char*>();
        enregistrerUnReglage("album", r.album);
        xSemaphoreTake(verrou, portMAX_DELAY); reglages.album = r.album; xSemaphoreGive(verrou);
        ecrireDansLeJournal("[SYNCHRO] Album Google Photos « %s » créé.", NOM_DE_L_ALBUM_GOOGLE);
    } else {
        noterUneErreur("album Google Photos non créé (HTTP " + String(code) + "), photos envoyées sans album");
    }
}

// Envoie une photo : 1) les octets → jeton d'envoi ; 2) création de l'élément dans la photothèque.
Resultat envoyerUnePhotoAGoogle(Connexion& c, const Reglages& r, const FichierTrouve& photo, String& idGoogle, String& lien) {
    uint8_t* image = (uint8_t*)ps_malloc(photo.taille);
    if (image == nullptr) return Resultat::REFUSE;
    size_t lus = 0;
    if (prendreLaCarteSD(5000)) {
        File f = systemeDeFichiersDeLaCarteSD().open(photo.chemin, FILE_READ);
        if (f) { lus = f.read(image, photo.taille); f.close(); }
        rendreLaCarteSD();
    }
    if (lus != photo.taille || lus < 4 || image[0] != 0xFF || image[1] != 0xD8) {   // pas un JPEG complet
        free(image);
        return Resultat::REFUSE;
    }
    const String nom = nomSansDossier(photo.chemin);
    String jetonDEnvoi;
    const int code = requeteHTTP(c, "https://photoslibrary.googleapis.com/v1/uploads", "POST", image, lus, {
            { "Content-Type", "application/octet-stream" },
            { "Authorization", "Bearer " + jetonDAccesGoogle },
            { "X-Goog-Upload-Content-Type", "image/jpeg" },
            { "X-Goog-Upload-File-Name", nom },
            { "X-Goog-Upload-Protocol", "raw" },
        }, &jetonDEnvoi, false);
    free(image);
    if (code != 200 || jetonDEnvoi.length() == 0) {
        noterUneErreur("envoi de " + nom + " à Google : HTTP " + String(code));
        return code == 400 ? Resultat::REFUSE : Resultat::ECHEC;
    }

    JsonDocument demande;
    if (r.album.length() > 0) demande["albumId"] = r.album;
    JsonObject element = demande["newMediaItems"].to<JsonArray>().add<JsonObject>();
    const time_t prise = dateDuNomDeFichier(nom, true);
    if (prise > 0) {
        struct tm t; localtime_r(&prise, &t);
        char description[48];
        strftime(description, sizeof(description), "Moto, le %d/%m/%Y à %H:%M:%S", &t);
        element["description"] = description;
    }
    element["simpleMediaItem"]["fileName"] = nom;
    element["simpleMediaItem"]["uploadToken"] = jetonDEnvoi;
    String corps, reponse;
    serializeJson(demande, corps);
    const int code2 = requeteHTTP(c, "https://photoslibrary.googleapis.com/v1/mediaItems:batchCreate", "POST",
                                  (const uint8_t*)corps.c_str(), corps.length(),
                                  { { "Content-Type", "application/json" }, { "Authorization", "Bearer " + jetonDAccesGoogle } },
                                  &reponse, false);
    JsonDocument d;
    if (code2 != 200 || deserializeJson(d, reponse)) {
        noterUneErreur("Google Photos refuse " + nom + " : HTTP " + String(code2) + " " + reponse.substring(0, 120));
        return code2 == 400 ? Resultat::REFUSE : Resultat::ECHEC;
    }
    JsonObject media = d["newMediaItemResults"][0]["mediaItem"];
    idGoogle = media["id"] | "";
    lien = media["productUrl"] | "";
    if (idGoogle.length() == 0) {
        noterUneErreur("Google Photos : " + String(d["newMediaItemResults"][0]["status"]["message"] | "réponse inattendue"));
        return Resultat::REFUSE;
    }
    return Resultat::REUSSI;
}

// ─────────────────────────────── Google Drive (trames CAN brutes) ───────────────────────────────

// Crée le dossier « Moto ESP32 » de Google Drive la première fois (son identifiant est gardé en mémoire).
bool preparerLeDossierDrive(Connexion& c, Reglages& r) {
    if (r.dossierDrive.length() > 0) return true;
    JsonDocument demande;
    demande["name"] = NOM_DU_DOSSIER_DRIVE;
    demande["mimeType"] = "application/vnd.google-apps.folder";
    String corps, reponse;
    serializeJson(demande, corps);
    const int code = requeteHTTP(c, "https://www.googleapis.com/drive/v3/files", "POST", (const uint8_t*)corps.c_str(), corps.length(),
                                 { { "Content-Type", "application/json" }, { "Authorization", "Bearer " + jetonDAccesGoogle } },
                                 &reponse, false);
    JsonDocument d;
    if (code != 200 || deserializeJson(d, reponse) || !d["id"].is<const char*>()) {
        noterUneErreur("dossier Google Drive non créé (HTTP " + String(code) + ") " + reponse.substring(0, 120));
        return false;
    }
    r.dossierDrive = d["id"].as<const char*>();
    enregistrerUnReglage("dossier", r.dossierDrive);
    xSemaphoreTake(verrou, portMAX_DELAY); reglages.dossierDrive = r.dossierDrive; xSemaphoreGive(verrou);
    ecrireDansLeJournal("[SYNCHRO] Dossier Google Drive « %s » créé.", NOM_DU_DOSSIER_DRIVE);
    return true;
}

// Envoi « reprenable » de Google Drive : on ouvre une session, puis on envoie le fichier
// par morceaux de 512 Kio (un fichier CAN peut faire plus de 100 Mo).
Resultat envoyerUnFichierADrive(Connexion& c, const Reglages& r, const FichierTrouve& fichier, String& idDrive) {
    // Nom dans Drive : « 03-11-2026_14-05-09.csv » (déjà daté par module_canbus).
    JsonDocument meta;
    meta["name"] = nomSansDossier(fichier.chemin);
    meta["parents"].to<JsonArray>().add(r.dossierDrive);
    meta["description"] = "Trames CAN brutes de la moto (" + fichier.chemin + ")";
    String corps, reponse, session;
    serializeJson(meta, corps);
    int code = requeteHTTP(c, "https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable&fields=id", "POST",
                           (const uint8_t*)corps.c_str(), corps.length(), {
                               { "Content-Type", "application/json; charset=UTF-8" },
                               { "Authorization", "Bearer " + jetonDAccesGoogle },
                               { "X-Upload-Content-Type", "text/csv" },
                               { "X-Upload-Content-Length", String(fichier.taille) },
                           }, &reponse, false, "Location", &session);
    if (code != 200 || session.length() == 0) {
        noterUneErreur("Google Drive refuse l'envoi de " + fichier.chemin + " (HTTP " + String(code) + ") " + reponse.substring(0, 100));
        return code == 400 ? Resultat::REFUSE : Resultat::ECHEC;
    }
    uint8_t* morceau = (uint8_t*)ps_malloc(TAILLE_D_UN_MORCEAU_DRIVE);
    if (morceau == nullptr) return Resultat::ECHEC;
    uint32_t envoye = 0;
    Resultat resultat = Resultat::ECHEC;
    while (!interruption) {
        const size_t aLire = min<size_t>(TAILLE_D_UN_MORCEAU_DRIVE, fichier.taille - envoye);
        size_t lus = 0;
        if (aLire > 0 && prendreLaCarteSD(5000)) {
            File f = systemeDeFichiersDeLaCarteSD().open(fichier.chemin, FILE_READ);
            if (f && f.seek(envoye)) lus = f.read(morceau, aLire);
            if (f) f.close();
            rendreLaCarteSD();
        }
        if (lus != aLire) { noterUneErreur("lecture de " + fichier.chemin + " impossible"); break; }
        const String plage = "bytes " + String(envoye) + "-" + String(envoye + lus - 1) + "/" + String(fichier.taille);
        code = requeteHTTP(c, session, "PUT", morceau, lus, { { "Content-Range", plage } }, &reponse, true);
        if (code == 308) {   // morceau reçu, Google attend la suite
            envoye += lus;
            continue;
        }
        JsonDocument d;
        if ((code == 200 || code == 201) && !deserializeJson(d, reponse)) {
            idDrive = d["id"] | "";
            resultat = Resultat::REUSSI;
        } else {
            noterUneErreur("envoi de " + fichier.chemin + " à Drive interrompu (HTTP " + String(code) + ")");
        }
        break;
    }
    free(morceau);
    return resultat;
}

long envoyerLesTramesCANADrive(Connexion& base, Reglages& r) {
    // Pendant que la moto tourne, le fichier du moment s'écrit encore : on attend qu'elle soit coupée.
    if (busCANActif()) return 0;
    Connexion drive;
    preparerLaConnexion(drive);
    bool drivePret = false;
    long total = 0;
    for (const String& dossier : sousDossiers(DOSSIER_DES_TRAMES_CAN)) {
        for (const FichierTrouve& fichier : fichiersDuDossier(dossier, ".csv")) {
            if (interruption) return -1;
            Avancement& avancement = progression[fichier.chemin];
            if (fichier.taille == 0 || avancement.octets >= fichier.taille) continue;
            if (!drivePret) {
                if (!obtenirUnJetonGoogle(drive, r) || !preparerLeDossierDrive(drive, r)) return -1;
                drivePret = true;
            }
            noterLEtat("Trames CAN vers Google Drive : " + nomSansDossier(fichier.chemin), false);
            String idDrive;
            const Resultat resultat = envoyerUnFichierADrive(drive, r, fichier, idDrive);
            if (resultat == Resultat::ECHEC) return -1;
            if (resultat == Resultat::REUSSI) {
                total++;
                if (r.api.length() > 0) {
                    int kbits; time_t debut; long long debutMs;
                    lireLEnteteCAN(fichier, kbits, debut, debutMs);
                    envoyerLaFicheDuFichier(base, r, fichier.chemin, "can", debut, debutMs, kbits, idDrive);
                }
            }
            avancement.octets = fichier.taille;
            enregistrerLaProgression();
        }
    }
    return total;
}

// ─────────────────────────────── Google Sheets (journal et trace GPS) ───────────────────────────────
// Un classeur par année dans le dossier « Moto ESP32 » : « Moto ESP32 - 2026 », onglets « Journal » et « GPS ».
// (Un classeur Google tient 10 millions de cellules : largement une année de sorties.)

const char* ENTETES_DU_JOURNAL = "[[\"Date et heure\",\"Texte\",\"Fichier\"]]";
const char* ENTETES_DU_GPS     = "[[\"Date et heure\",\"Latitude\",\"Longitude\",\"Altitude (m)\",\"Vitesse (km/h)\",\"Cap (°)\",\"Satellites\",\"HDOP\",\"Carte\"]]";

Resultat ajouterDesLignesAuClasseur(Connexion& c, const String& classeur, const char* onglet, const String& corps) {
    const String url = "https://sheets.googleapis.com/v4/spreadsheets/" + classeur + "/values/" + onglet
                     + ":append?valueInputOption=RAW&insertDataOption=INSERT_ROWS";
    for (int essai = 0; essai < 3; essai++) {
        String reponse;
        const int code = requeteHTTP(c, url, "POST", (const uint8_t*)corps.c_str(), corps.length(),
                                     { { "Content-Type", "application/json" }, { "Authorization", "Bearer " + jetonDAccesGoogle } },
                                     &reponse, true);
        if (code == 200) return Resultat::REUSSI;
        if (code == 429) { vTaskDelay(pdMS_TO_TICKS(30000)); continue; }   // trop de requêtes par minute : on patiente
        noterUneErreur(String("Google Sheets (") + onglet + ") : HTTP " + code + " " + reponse.substring(0, 120));
        return code == 400 ? Resultat::REFUSE : Resultat::ECHEC;
    }
    return Resultat::ECHEC;
}

// Identifiant du classeur de l'année (créé la première fois et gardé en mémoire), vide en cas d'échec.
String classeurDeLAnnee(Connexion& c, Reglages& r, int annee) {
    char cle[12];
    snprintf(cle, sizeof(cle), "s%d", annee);
    Preferences memoire;
    memoire.begin("synchro", true);
    String id = memoire.getString(cle, "");
    memoire.end();
    if (id.length() > 0) return id;

    JsonDocument demande;
    demande["properties"]["title"] = String("Moto ESP32 - ") + annee;
    demande["properties"]["locale"] = "fr_FR";
    demande["properties"]["timeZone"] = "Europe/Paris";
    JsonArray onglets = demande["sheets"].to<JsonArray>();
    onglets.add<JsonObject>()["properties"]["title"] = "Journal";
    onglets.add<JsonObject>()["properties"]["title"] = "GPS";
    String corps, reponse;
    serializeJson(demande, corps);
    Connexion creation;
    preparerLaConnexion(creation);
    const int code = requeteHTTP(creation, "https://sheets.googleapis.com/v4/spreadsheets", "POST", (const uint8_t*)corps.c_str(), corps.length(),
                                 { { "Content-Type", "application/json" }, { "Authorization", "Bearer " + jetonDAccesGoogle } },
                                 &reponse, false);
    JsonDocument d;
    if (code != 200 || deserializeJson(d, reponse) || !d["spreadsheetId"].is<const char*>()) {
        noterUneErreur("classeur Google Sheets non créé (HTTP " + String(code) + ") " + reponse.substring(0, 100)
                       + (code == 403 ? " : activer l'API Google Sheets" : ""));
        return "";
    }
    id = d["spreadsheetId"].as<const char*>();
    // Rangé dans le dossier « Moto ESP32 » (au mieux : s'il reste à la racine du Drive, ce n'est pas grave).
    if (preparerLeDossierDrive(creation, r)) {
        requeteHTTP(creation, "https://www.googleapis.com/drive/v3/files/" + id + "?addParents=" + r.dossierDrive + "&removeParents=root",
                    "PATCH", (const uint8_t*)"{}", 2,
                    { { "Content-Type", "application/json" }, { "Authorization", "Bearer " + jetonDAccesGoogle } }, nullptr, false);
    }
    ajouterDesLignesAuClasseur(c, id, "Journal", String("{\"values\":") + ENTETES_DU_JOURNAL + "}");
    ajouterDesLignesAuClasseur(c, id, "GPS", String("{\"values\":") + ENTETES_DU_GPS + "}");
    memoire.begin("synchro", false);
    memoire.putString(cle, id);
    memoire.end();
    ecrireDansLeJournal("[SYNCHRO] Classeur Google Sheets « Moto ESP32 - %d » créé.", annee);
    return id;
}

int anneeDuNomDeFichier(const String& nom) {
    int j = 0, m = 0, a = 0;
    if (sscanf(nom.c_str(), "%d-%d-%d", &j, &m, &a) == 3 && a > 2020) return a;
    if (!lHeureEstConnue()) return 0;
    const time_t maintenant = time(nullptr);
    struct tm t;
    localtime_r(&maintenant, &t);
    return t.tm_year + 1900;
}

const FormeDuPaquet LIGNES_DE_CLASSEUR = { "{\"values\":[", ',', "]}" };

long envoyerLeJournalAuClasseur(Connexion& c, Reglages& r) {
    long total = 0;
    for (const FichierTrouve& fichier : fichiersDuDossier(DOSSIER_DES_JOURNAUX, ".log")) {
        if (interruption) return -1;
        const String cle = "sheets:" + fichier.chemin;
        if (progression[cle].octets >= fichier.taille) continue;
        const String nom = nomSansDossier(fichier.chemin);
        const int annee = anneeDuNomDeFichier(nom);
        if (annee == 0) continue;   // journal sans date, et heure toujours inconnue : plus tard
        const String classeur = classeurDeLAnnee(c, r, annee);
        if (classeur.length() == 0) return -1;
        int jour = 0, mois = 0, a = 0;
        if (sscanf(nom.c_str(), "%d-%d-%d", &jour, &mois, &a) != 3) a = 0;
        const long envoyees = envoyerLaSuiteDUnFichier(fichier, cle, LIGNES_DE_CLASSEUR,
            [&](const char* ligne, uint32_t, String& sortie) {
                const LigneDeJournal l = lireUneLigneDeJournal(ligne, jour, mois, a);
                JsonDocument d;
                JsonArray rangee = d.to<JsonArray>();
                rangee.add(heureLocaleLisible(l.heure));
                rangee.add(l.texte);
                rangee.add(nom);
                serializeJson(d, sortie);
                return true;
            },
            [&](const String& corps) { return ajouterDesLignesAuClasseur(c, classeur, "Journal", corps); });
        if (envoyees < 0) return -1;
        total += envoyees;
    }
    return total;
}

long envoyerLaTraceAuClasseur(Connexion& c, Reglages& r) {
    long total = 0;
    for (const FichierTrouve& fichier : fichiersDuDossier(DOSSIER_DES_TRACES_GPS, ".csv")) {
        if (interruption) return -1;
        const String cle = "sheets:" + fichier.chemin;
        if (progression[cle].octets >= fichier.taille) continue;
        const int annee = anneeDuNomDeFichier(nomSansDossier(fichier.chemin));
        if (annee == 0) continue;
        const String classeur = classeurDeLAnnee(c, r, annee);
        if (classeur.length() == 0) return -1;
        const long envoyees = envoyerLaSuiteDUnFichier(fichier, cle, LIGNES_DE_CLASSEUR,
            [](const char* ligne, uint32_t, String& sortie) {
                PointLu p;
                if (!lireUnPointGPS(ligne, p)) return false;
                JsonDocument d;
                JsonArray rangee = d.to<JsonArray>();
                rangee.add(heureLocaleLisible(p.heure));
                rangee.add(serialized(String(p.latitude, 6)));
                rangee.add(serialized(String(p.longitude, 6)));
                for (int i = 0; i < 5; i++) { if (isfinite(p.valeurs[i])) rangee.add(p.valeurs[i]); else rangee.add(""); }
                rangee.add("https://maps.google.com/?q=" + String(p.latitude, 6) + "," + String(p.longitude, 6));
                serializeJson(d, sortie);
                return true;
            },
            [&](const String& corps) { return ajouterDesLignesAuClasseur(c, classeur, "GPS", corps); });
        if (envoyees < 0) return -1;
        total += envoyees;
    }
    return total;
}

long envoyerAuxClasseursGoogle(Reglages& r, Bilan& bilan) {
    Connexion jeton, feuilles;
    preparerLaConnexion(jeton);
    preparerLaConnexion(feuilles);
    if (!obtenirUnJetonGoogle(jeton, r)) return -1;
    noterLEtat("Journal vers Google Sheets", false);
    long n = envoyerLeJournalAuClasseur(feuilles, r);
    if (n < 0) return -1;
    bilan.journaux += n;
    noterLEtat("Trace GPS vers Google Sheets", false);
    n = envoyerLaTraceAuClasseur(feuilles, r);
    if (n < 0) return -1;
    bilan.positions += n;
    return 0;
}

long envoyerLesPhotos(Connexion& base, Reglages& r) {
    Connexion google;
    preparerLaConnexion(google);
    bool googlePret = false;
    long total = 0;
    for (const String& dossier : sousDossiers(DOSSIER_DES_PHOTOS)) {
        for (const FichierTrouve& photo : fichiersDuDossier(dossier, ".jpg")) {
            if (interruption) return -1;
            Avancement& avancement = progression[photo.chemin];
            if (photo.taille == 0 || avancement.octets >= photo.taille) continue;
            if (!googlePret) {
                if (!obtenirUnJetonGoogle(google, r)) return -1;
                preparerLAlbumGoogle(google, r);
                googlePret = true;
            }
            noterLEtat("Photo vers Google Photos : " + nomSansDossier(photo.chemin), false);
            String idGoogle, lien;
            const Resultat resultat = envoyerUnePhotoAGoogle(google, r, photo, idGoogle, lien);
            if (resultat == Resultat::ECHEC) return -1;
            if (resultat == Resultat::REUSSI) {
                total++;
                if (r.api.length() > 0) {
                    JsonDocument d;
                    d["chemin"] = photo.chemin;
                    const time_t prise = dateDuNomDeFichier(nomSansDossier(photo.chemin), true);
                    if (prise > 0) d["horodatage"] = dateISO(prise); else d["horodatage"] = nullptr;
                    d["taille"] = photo.taille;
                    d["google_id"] = idGoogle;
                    d["google_url"] = lien;
                    String corps;
                    serializeJson(d, corps);
                    insererDansLaTable(base, r, "photos", corps, false);   // la photo est dans Google même si ça échoue
                }
            } else {
                xSemaphoreTake(verrou, portMAX_DELAY); dernierBilan.erreurs++; xSemaphoreGive(verrou);
            }
            avancement.octets = photo.taille;
            avancement.lignes = 1;
            enregistrerLaProgression();
        }
    }
    return total;
}

// ─────────────────────────────── Wi-Fi de la maison ───────────────────────────────

bool reseauDeLaMaisonAPortee(const String& nom) {
    WiFi.mode(WIFI_STA);
    const int nombre = WiFi.scanNetworks(false, false, false, 200);
    bool trouve = false;
    for (int i = 0; i < nombre && !trouve; i++) trouve = WiFi.SSID(i) == nom;
    WiFi.scanDelete();
    return trouve;
}

bool seConnecterALaMaison(const Reglages& r) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("moto-esp32");
    WiFi.begin(r.wifiNom.c_str(), r.wifiMotDePasse.length() ? r.wifiMotDePasse.c_str() : nullptr);
    const uint32_t debut = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - debut < DELAI_DE_CONNEXION_MS && !interruption) vTaskDelay(pdMS_TO_TICKS(250));
    return WiFi.status() == WL_CONNECTED;
}

void eteindreLeWifiDeLaMaison() {
    if (wifiOuvert()) return;   // le point d'accès de l'appli a pris la main : on n'y touche pas
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// ─────────────────────────────── Synchronisation complète ───────────────────────────────

void synchroniser(Reglages& r) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    dernierBilan = Bilan();
    derniereErreur = "";
    xSemaphoreGive(verrou);
    chargerLaProgression();
    creerUnDossier(DOSSIER_DE_LA_SYNCHRO);

    Bilan bilan;
    long n = 0;
    const bool baseReglee   = r.api.length() > 0;
    const bool googleRegle  = r.googleJeton.length() > 0 && r.googleId.length() > 0;
    Connexion base;
    preparerLaConnexion(base);
    if (baseReglee) {
        noterLEtat("Envoi des journaux", false);
        n = envoyerLesJournaux(base, r);
        if (n >= 0) { bilan.journaux = n; noterLEtat("Envoi des positions GPS", false); n = envoyerLesPositions(base, r); }
        if (n >= 0) bilan.positions = n;
        if (n >= 0 && r.modeCAN == 2) { n = envoyerLesTramesCAN(base, r); if (n >= 0) bilan.trames = n; }
    }
    if (n >= 0 && googleRegle) {
        n = envoyerAuxClasseursGoogle(r, bilan);
        if (n >= 0) n = envoyerLesPhotos(base, r);
        if (n >= 0) bilan.photos = n;
        if (n >= 0 && r.modeCAN == 1) { n = envoyerLesTramesCANADrive(base, r); if (n >= 0) bilan.fichiersCAN = n; }
    }
    if (!baseReglee && !googleRegle) noterUneErreur("compte Google pas réglé (écran Synchro de l'appli)");
    const bool reussie = n >= 0 && (baseReglee || googleRegle);

    xSemaphoreTake(verrou, portMAX_DELAY);
    bilan.erreurs = dernierBilan.erreurs;
    dernierBilan = bilan;
    xSemaphoreGive(verrou);
    if (reussie) {
        derniereSynchroReussieMs = millis();
        heureDeLaDerniereSynchro = lHeureEstConnue() ? time(nullptr) : 0;
    }
    char resume[200];
    snprintf(resume, sizeof(resume), "%s : %lu ligne(s) de journal, %lu position(s), %lu fichier(s) CAN, %lu trame(s) CAN, %lu photo(s)%s",
             interruption ? "Interrompue" : (reussie ? "Terminée" : "Arrêtée sur une erreur"),
             (unsigned long)bilan.journaux, (unsigned long)bilan.positions,
             (unsigned long)bilan.fichiersCAN, (unsigned long)bilan.trames, (unsigned long)bilan.photos, bilan.erreurs ? " (des données refusées ont été sautées)" : "");
    noterLEtat(resume);
}

// ─────────────────────────────── Test de la connexion (après la saisie dans l'appli) ───────────────────────────────

// Coupe un texte UTF-8 à [octets] au plus sans casser un caractère accentué.
String couperProprement(const String& texte, size_t octets) {
    if (texte.length() <= octets) return texte;
    size_t fin = octets;
    while (fin > 0 && (texte[fin] & 0xC0) == 0x80) fin--;
    return texte.substring(0, fin) + "…";
}

void publierLeTest(int etat, const String& message) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    etatDuDernierTest = etat;
    messageDuDernierTest = couperProprement(message, 360);
    xSemaphoreGive(verrou);
    if (etat != 2) ecrireDansLeJournal("[SYNCHRO] Test du Wi-Fi : %s", message.c_str());
    JsonDocument d;
    decrireLeTestDuWifi(d);
    publierUnEvenementVersLAppli(d);
}

// Traduit le code que l'ESP32 reçoit quand la connexion échoue.
String explicationDeLaRaison(uint8_t raison) {
    switch (raison) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_AUTH_EXPIRE:
            return "mot de passe refusé par la box (vérifie majuscules, chiffres et caractères spéciaux).";
        case WIFI_REASON_NO_AP_FOUND:
            return "la box ne répond plus, ou elle n'accepte que le WPA3 (mettre la box en mode WPA2/WPA3 mixte).";
        case WIFI_REASON_ASSOC_TOOMANY:
            return "la box a déjà trop d'appareils connectés.";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_CONNECTION_FAIL:
            return "la box refuse le module (filtrage par adresse MAC activé ?).";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "signal perdu pendant la connexion : rapprocher la moto de la box.";
        case 0:
            return "pas de réponse de la box en 20 s (signal trop faible ?).";
        default:
            return "refus de la box (code " + String(raison) + ").";
    }
}

void testerLaConnexion(const Reglages& r) {
    if (r.wifiNom.length() == 0) { publierLeTest(0, "Aucun Wi-Fi de la maison n'est réglé."); return; }
    publierLeTest(2, "Recherche du réseau « " + r.wifiNom + " »…");
    WiFi.mode(WIFI_STA);
    const int nombre = WiFi.scanNetworks(false, false, false, 200);
    int trouve = -1, ressemblant = -1;
    String vus;
    for (int i = 0; i < nombre; i++) {
        const String nom = WiFi.SSID(i);
        if (nom == r.wifiNom) { if (trouve < 0) trouve = i; continue; }   // résultats triés du plus fort au plus faible
        String a = nom, b = r.wifiNom;
        a.trim(); b.trim(); a.toLowerCase(); b.toLowerCase();
        if (ressemblant < 0 && a == b) ressemblant = i;
        if (nom.length() > 0 && vus.length() < 140 && vus.indexOf("« " + nom + " »") < 0) vus += (vus.length() ? ", « " : "« ") + nom + " »";
    }
    if (trouve < 0) {
        String message;
        if (nombre <= 0)          message = "Aucun réseau Wi-Fi capté : moto trop loin de la box ?";
        else if (ressemblant >= 0) message = "Réseau « " + r.wifiNom + " » introuvable, mais « " + WiFi.SSID(ressemblant)
                                           + " » existe : vérifie les majuscules et les espaces.";
        else message = "Réseau « " + r.wifiNom + " » introuvable. Nom mal écrit, box qui n'émet qu'en 5 GHz "
                       "(le module ne capte que le 2,4 GHz), ou moto trop loin. Réseaux captés : " + vus + ".";
        WiFi.scanDelete();
        eteindreLeWifiDeLaMaison();
        publierLeTest(0, message);
        return;
    }
    const int signal = WiFi.RSSI(trouve);
    const int canal = WiFi.channel(trouve);
    const wifi_auth_mode_t securite = WiFi.encryptionType(trouve);
    WiFi.scanDelete();
    publierLeTest(2, "Réseau trouvé (signal " + String(signal) + " dBm, canal " + String(canal) + "). Connexion…");

    raisonDeDeconnexion = 0;
    if (!seConnecterALaMaison(r)) {
        String message = "Connexion impossible : " + explicationDeLaRaison(raisonDeDeconnexion);
        if (securite == WIFI_AUTH_OPEN && r.wifiMotDePasse.length() > 0) message += " (Ce réseau est ouvert : laisser le mot de passe vide.)";
        if (signal < -80) message += " Signal faible (" + String(signal) + " dBm).";
        eteindreLeWifiDeLaMaison();
        publierLeTest(0, message);
        return;
    }
    String message = "Wi-Fi OK (adresse " + WiFi.localIP().toString() + ", signal " + String(WiFi.RSSI()) + " dBm). ";
    publierLeTest(2, message + "Test d'Internet…");
    Connexion c;
    preparerLaConnexion(c);
    String reponse;
    const int code = requeteHTTP(c, "http://connectivitycheck.gstatic.com/generate_204", "GET", nullptr, 0, {}, &reponse, false);
    if (code != 204) {
        eteindreLeWifiDeLaMaison();
        publierLeTest(0, message + "Mais pas d'accès à Internet (" + (code < 0 ? reponse : "HTTP " + String(code)) + ").");
        return;
    }
    message += "Internet OK. ";
    bool reussi = true;
    if (r.googleId.length() > 0 && r.googleJeton.length() > 0) {
        jetonDAccesGoogle = "";   // on vérifie vraiment les identifiants
        if (obtenirUnJetonGoogle(c, r)) message += "Compte Google OK : la synchro peut démarrer.";
        else { message += "Compte Google refusé : " + raisonGoogle; reussi = false; }
    } else {
        message += "Compte Google pas encore réglé.";
    }
    message += " (RAM interne libre : " + String(memoireInterneLibre() / 1024) + " Ko)";
    eteindreLeWifiDeLaMaison();
    publierLeTest(reussi ? 1 : 0, message);
}

// Garde contre les redémarrages en boucle : notée dans la mémoire RTC (qui survit à un plantage,
// sans user la flash) pendant que le Wi-Fi tourne. Si le module redémarre sur un plantage alors
// qu'elle est posée, la synchro automatique est suspendue.
RTC_NOINIT_ATTR uint32_t gardeDuWifi;
const uint32_t GARDE_POSEE = 0x5A1C0DE5;

void poserLaGarde(bool posee) { gardeDuWifi = posee ? GARDE_POSEE : 0; }

void verifierLaGarde() {
    const bool posee = gardeDuWifi == GARDE_POSEE;
    gardeDuWifi = 0;
    const esp_reset_reason_t raison = esp_reset_reason();
    const bool plantage = raison == ESP_RST_PANIC || raison == ESP_RST_INT_WDT || raison == ESP_RST_TASK_WDT
                       || raison == ESP_RST_WDT || raison == ESP_RST_BROWNOUT;
    if (posee && plantage) {
        suspendue = true;
        etatLisible = "Suspendue : le module a redémarré pendant la dernière synchro (" + String(raisonDuDernierRedemarrage())
                    + "). Appuyer sur Tester ou Synchroniser pour réessayer.";
        ecrireDansLeJournal("[SYNCHRO] %s", etatLisible.c_str());
    }
}

void tacheDeSynchronisation(void*) {
    // Au démarrage, on attend que le reste soit installé (sauf si l'appli demande un test).
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ATTENTE_APRES_LE_DEMARRAGE_MS));
    xTaskNotifyGive(tacheSynchro);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(INTERVALLE_DE_RECHERCHE_MS));
        const bool manuelle = demandeManuelle;
        demandeManuelle = false;
        Reglages r = copieDesReglages();

        if (testDemande || manuelle) {
            suspendue = false;   // l'utilisateur redemande : on retente
        }
        const size_t memoireInterne = memoireInterneLibre();
        if ((testDemande || manuelle) && memoireInterne < MEMOIRE_INTERNE_MINIMUM) {
            const String m = "Mémoire interne insuffisante (" + String(memoireInterne / 1024) + " Ko libres, il en faut "
                           + String(MEMOIRE_INTERNE_MINIMUM / 1024) + ") : Wi-Fi non lancé pour protéger le Bluetooth.";
            if (testDemande) publierLeTest(0, m); else noterLEtat(m);
            testDemande = false;
            continue;
        }
        if (testDemande) {
            testDemande = false;
            if (wifiOuvert()) {
                publierLeTest(0, "Le Wi-Fi du module sert à un transfert de l'appli : réessayer dans 2 minutes.");
                continue;
            }
            interruption = false;
            enCours = true;
            poserLaGarde(true);
            testerLaConnexion(r);
            poserLaGarde(false);
            enCours = false;
            continue;
        }

        if (r.wifiNom.length() == 0) { if (manuelle) noterLEtat("Wi-Fi de la maison non réglé"); continue; }
        if (!r.active && !manuelle) continue;
        if (suspendue && !manuelle) continue;
        // Recherche automatique seulement quand l'appli n'est pas connectée : le balayage Wi-Fi
        // occupe la radio partagée et peut faire décrocher le Bluetooth.
        if (!manuelle && appliConnectee()) continue;
        if (!manuelle && memoireInterne < MEMOIRE_INTERNE_MINIMUM) continue;
        if (!manuelle && derniereSynchroReussieMs != 0 && millis() - derniereSynchroReussieMs < INTERVALLE_ENTRE_DEUX_SYNCHROS_MS) continue;
        const PositionGPS position = positionGPS();
        if (!manuelle && position.positionValide && position.vitesseKmh > VITESSE_MAXIMUM_POUR_CHERCHER) continue;
        if (!carteSDMontee()) { if (manuelle) noterLEtat("Pas de carte SD : rien à envoyer"); continue; }

        interruption = false;
        enCours = true;
        poserLaGarde(true);
        if (wifiOuvert()) {   // le point d'accès de l'appli est prioritaire
            poserLaGarde(false);
            enCours = false;
            if (manuelle) noterLEtat("Wi-Fi occupé par l'appli (transfert en cours)");
            continue;
        }
        if (!reseauDeLaMaisonAPortee(r.wifiNom)) {
            eteindreLeWifiDeLaMaison();
            poserLaGarde(false);
            enCours = false;
            noterLEtat("Wi-Fi « " + r.wifiNom + " » hors de portée", manuelle);
            continue;
        }
        noterLEtat("Connexion au Wi-Fi « " + r.wifiNom + " »");
        raisonDeDeconnexion = 0;
        if (!seConnecterALaMaison(r)) {
            eteindreLeWifiDeLaMaison();
            poserLaGarde(false);
            enCours = false;
            noterUneErreur("connexion au Wi-Fi impossible : " + explicationDeLaRaison(raisonDeDeconnexion));
            noterLEtat("Connexion au Wi-Fi impossible");
            continue;
        }
        ecrireDansLeJournal("[SYNCHRO] Connecté (%s, %d dBm).", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        synchroniser(r);
        eteindreLeWifiDeLaMaison();
        poserLaGarde(false);
        enCours = false;
    }
}

} // namespace

// ─────────────────────────────── Réglages depuis l'appli ───────────────────────────────

void choisirLeWifiDeLaMaison(const String& nom, const String& motDePasse) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    reglages.wifiNom = nom;
    if (motDePasse.length() > 0 || nom.length() == 0) reglages.wifiMotDePasse = motDePasse;   // vide = on garde l'ancien
    const String mdp = reglages.wifiMotDePasse;
    xSemaphoreGive(verrou);
    enregistrerUnReglage("ssid", nom);
    enregistrerUnReglage("mdp", mdp);
}

void choisirLeServeurDeDonnees(const String& adresse, const String& cle) {
    String propre = adresse;
    propre.trim();
    while (propre.endsWith("/")) propre.remove(propre.length() - 1);
    xSemaphoreTake(verrou, portMAX_DELAY);
    reglages.api = propre;
    if (cle.length() > 0 || propre.length() == 0) reglages.cle = cle;   // vide = on garde l'ancienne
    const String c = reglages.cle;
    xSemaphoreGive(verrou);
    enregistrerUnReglage("api", propre);
    enregistrerUnReglage("cle", c);
}

void choisirLeClientGoogle(const String& identifiant, const String& secret) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    reglages.googleId = identifiant;
    if (secret.length() > 0 || identifiant.length() == 0) reglages.googleSecret = secret;
    const String s = reglages.googleSecret;
    xSemaphoreGive(verrou);
    jetonDAccesGoogle = "";
    enregistrerUnReglage("g_id", identifiant);
    enregistrerUnReglage("g_secret", s);
}

void choisirLeJetonGoogle(const String& jetonDeRafraichissement) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    reglages.googleJeton = jetonDeRafraichissement;
    reglages.album = "";   // nouveau compte peut-être : l'album sera recréé si besoin
    xSemaphoreGive(verrou);
    jetonDAccesGoogle = "";
    enregistrerUnReglage("g_jeton", jetonDeRafraichissement);
    enregistrerUnReglage("album", "");
}

void activerLaSynchronisation(bool active) {
    xSemaphoreTake(verrou, portMAX_DELAY);
    reglages.active = active;
    xSemaphoreGive(verrou);
    enregistrerUnInterrupteur("active", active);
}

void choisirLEnvoiDesTramesCAN(int mode) {
    if (mode < 0 || mode > 2) mode = 1;
    xSemaphoreTake(verrou, portMAX_DELAY);
    reglages.modeCAN = mode;
    xSemaphoreGive(verrou);
    Preferences memoire;
    memoire.begin("synchro", false);
    memoire.putInt("can", mode);
    memoire.end();
}

void synchroniserMaintenant() {
    demandeManuelle = true;
    derniereSynchroReussieMs = 0;
    if (tacheSynchro != nullptr) xTaskNotifyGive(tacheSynchro);
}

void testerLeWifiDeLaMaison() {
    testDemande = true;
    if (tacheSynchro != nullptr) xTaskNotifyGive(tacheSynchro);
}

void decrireLeTestDuWifi(JsonDocument& d) {
    d["t"] = "wifi_test";
    if (verrou == nullptr) { d["etat"] = -1; return; }
    xSemaphoreTake(verrou, portMAX_DELAY);
    d["etat"] = etatDuDernierTest;
    d["msg"]  = messageDuDernierTest;
    xSemaphoreGive(verrou);
}

void interrompreLaSynchronisation() {
    if (!enCours) return;
    interruption = true;
    for (int attente = 0; attente < 80 && enCours; attente++) vTaskDelay(pdMS_TO_TICKS(100));
}

bool synchronisationEnCours() { return enCours; }

void decrireLaSynchronisation(JsonDocument& d) {
    d["t"] = "synchro";
    if (verrou == nullptr) { d["etat"] = "Module non démarré"; return; }
    xSemaphoreTake(verrou, portMAX_DELAY);
    d["actif"]  = reglages.active ? 1 : 0;
    d["can"]    = reglages.modeCAN;
    d["ssid"]   = reglages.wifiNom;
    d["mdp"]    = reglages.wifiMotDePasse.length() > 0 ? 1 : 0;
    d["api"]    = reglages.api;
    d["cle"]    = reglages.cle.length() > 0 ? 1 : 0;
    d["g_id"]   = reglages.googleId.length() > 0 ? 1 : 0;
    d["g_jeton"] = reglages.googleJeton.length() > 0 ? 1 : 0;
    d["album"]  = reglages.album.length() > 0 ? 1 : 0;
    d["encours"] = enCours ? 1 : 0;
    d["etat"]   = etatLisible.substring(0, 110);
    d["err"]    = derniereErreur.substring(0, 100);
    d["der"]    = (uint32_t)heureDeLaDerniereSynchro;
    d["n_j"]    = dernierBilan.journaux;
    d["n_g"]    = dernierBilan.positions;
    d["n_c"]    = dernierBilan.fichiersCAN;
    d["n_t"]    = dernierBilan.trames;
    d["n_p"]    = dernierBilan.photos;
    xSemaphoreGive(verrou);
}

bool demarrerModuleSynchro() {
    verrou = xSemaphoreCreateMutex();
    if (verrou == nullptr) return false;
    lireLesReglages();
    verifierLaGarde();
    // Les échanges chiffrés (TLS) demandent ≈ 40 Ko par connexion : dans la PSRAM plutôt que dans la
    // RAM interne (le firmware Arduino les met d'office en RAM interne, qui manque avec le Bluetooth).
    mbedtls_platform_set_calloc_free(
        [](size_t nombre, size_t taille) -> void* {
            void* bloc = heap_caps_calloc(nombre, taille, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            return bloc != nullptr ? bloc : heap_caps_calloc(nombre, taille, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        },
        [](void* bloc) { heap_caps_free(bloc); });
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        if (info.wifi_sta_disconnected.reason != 0) raisonDeDeconnexion = info.wifi_sta_disconnected.reason;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    // Les échanges TLS demandent une grosse pile.
    return creerTacheSurveillee(tacheDeSynchronisation, "Synchro", 12288, PRIORITE_BASSE, COEUR_TRAITEMENTS, &tacheSynchro);
}
