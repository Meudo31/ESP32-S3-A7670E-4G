#include "configuration.h"
#include "module_envoi_internet.h"
#include "module_journal.h"
#include "module_modem.h"

namespace {

const char*    SEPARATEUR = "----MotoESP32Separateur7d1a";
const uint32_t ATTENTE_DE_LA_REPONSE_DU_SERVEUR_MS = 2 * UNE_MINUTE_MS;

SemaphoreHandle_t verrouDeLEnvoi = nullptr;      // un seul envoi à la fois
SemaphoreHandle_t reponseDuServeurArrivee = nullptr;
volatile int      codeHTTP = 0;
volatile int      tailleDeLaReponse = 0;

// « +HTTPACTION: 1,200,38 » arrive quand le serveur a répondu (ou que l'envoi a échoué).
// Fonction appelée par le module modem : pas de commande AT ici.
void quandLeServeurARepondu(const String& ligne) {
    const int premiereVirgule = ligne.indexOf(',');
    const int deuxiemeVirgule = ligne.indexOf(',', premiereVirgule + 1);
    if (premiereVirgule < 0) return;
    codeHTTP = ligne.substring(premiereVirgule + 1, deuxiemeVirgule < 0 ? ligne.length() : deuxiemeVirgule).toInt();
    tailleDeLaReponse = deuxiemeVirgule < 0 ? 0 : ligne.substring(deuxiemeVirgule + 1).toInt();
    xSemaphoreGive(reponseDuServeurArrivee);
}

// Les codes 7xx viennent du modem lui-même (pas du serveur).
String expliquerLeCode(int code) {
    switch (code) {
        case 0:   return "pas de reponse du serveur";
        case 702: return "delai depasse (reseau lent ?)";
        case 703: case 704: return "serveur injoignable";
        case 705: case 706: return "pas de connexion Internet (forfait data, APN ?)";
        case 711: case 712: case 713: case 714: return "adresse du serveur introuvable (DNS)";
        case 715: return "echec de la connexion securisee (HTTPS)";
        default:  return String("code ") + code;
    }
}

String corpsDuFormulaireAvant(const char* nomDuFichier, const char* typeDeContenu) {
    String texte;
    texte += String("--") + SEPARATEUR + "\r\nContent-Disposition: form-data; name=\"reqtype\"\r\n\r\nfileupload\r\n";
    texte += String("--") + SEPARATEUR + "\r\nContent-Disposition: form-data; name=\"time\"\r\n\r\n" + DUREE_DE_VIE_DES_PHOTOS + "\r\n";
    texte += String("--") + SEPARATEUR + "\r\nContent-Disposition: form-data; name=\"fileToUpload\"; filename=\""
           + nomDuFichier + "\"\r\nContent-Type: " + typeDeContenu + "\r\n\r\n";
    return texte;
}

String lireLeLienDansLaReponse(const String& reponse) {
    const int debut = reponse.indexOf("https://");
    if (debut < 0) return "";
    int fin = debut;
    while (fin < (int)reponse.length() && !isspace((unsigned char)reponse[fin])) fin++;
    return reponse.substring(debut, fin);
}

bool envoyer(const uint8_t* donnees, size_t taille, const char* nomDuFichier, const char* typeDeContenu,
             String& lien, String& erreur) {
    // 1. Le corps de la requête (formulaire « multipart ») est préparé en PSRAM.
    const String avant = corpsDuFormulaireAvant(nomDuFichier, typeDeContenu);
    const String apres = String("\r\n--") + SEPARATEUR + "--\r\n";
    const size_t tailleTotale = avant.length() + taille + apres.length();
    uint8_t* corps = (uint8_t*)(psramFound() ? ps_malloc(tailleTotale) : malloc(tailleTotale));
    if (corps == nullptr) { erreur = "memoire insuffisante"; return false; }
    memcpy(corps, avant.c_str(), avant.length());
    memcpy(corps + avant.length(), donnees, taille);
    memcpy(corps + avant.length() + taille, apres.c_str(), apres.length());

    // 2. Connexion Internet (déjà active en 4G le plus souvent : une erreur ici n'est pas grave).
    envoyerCommandeAT("AT+CGACT=1,1", nullptr, 15000);
    envoyerCommandeAT("AT+HTTPTERM", nullptr, 2000);   // au cas où un envoi précédent serait resté ouvert

    bool reussi = false;
    if (envoyerCommandeAT("AT+HTTPINIT", nullptr, 10000) != ResultatAT::OK) {
        erreur = "le modem refuse de demarrer le client HTTP";
    } else {
        const String contenu = String("AT+HTTPPARA=\"CONTENT\",\"multipart/form-data; boundary=") + SEPARATEUR + "\"";
        if (envoyerCommandeAT(String("AT+HTTPPARA=\"URL\",\"") + ADRESSE_DEPOT_DES_PHOTOS + "\"") != ResultatAT::OK
            || envoyerCommandeAT(contenu) != ResultatAT::OK) {
            erreur = "reglages HTTP refuses";
        } else if (envoyerCommandeATAvecTelechargement(String("AT+HTTPDATA=") + tailleTotale + ",120", corps, tailleTotale,
                                                       150000) != ResultatAT::OK) {
            erreur = "le modem n'a pas accepte la photo";
        } else {
            // 3. Envoi au serveur : le résultat arrive plus tard (+HTTPACTION).
            xSemaphoreTake(reponseDuServeurArrivee, 0);
            codeHTTP = 0;
            if (envoyerCommandeAT("AT+HTTPACTION=1", nullptr, 10000) != ResultatAT::OK) {
                erreur = "le modem refuse l'envoi";
            } else if (xSemaphoreTake(reponseDuServeurArrivee, pdMS_TO_TICKS(ATTENTE_DE_LA_REPONSE_DU_SERVEUR_MS)) != pdTRUE) {
                erreur = "pas de reponse du serveur en 2 minutes";
            } else if (codeHTTP != 200) {
                erreur = String("envoi refuse : ") + expliquerLeCode(codeHTTP);
            } else {
                // 4. La réponse du serveur est le lien de la photo.
                String reponse;
                const int tailleAnnoncee = tailleDeLaReponse;
                const int aLire = (tailleAnnoncee > 0 && tailleAnnoncee < 512) ? tailleAnnoncee : 512;
                envoyerCommandeATJusquALaLigne(String("AT+HTTPREAD=0,") + aLire, "+HTTPREAD: 0", &reponse, 10000);
                lien = lireLeLienDansLaReponse(reponse);
                reussi = lien.length() > 0;
                if (!reussi) erreur = "reponse du serveur sans lien";
            }
        }
        envoyerCommandeAT("AT+HTTPTERM", nullptr, 5000);
    }
    free(corps);
    return reussi;
}

} // namespace

bool deposerUnFichierSurInternet(const uint8_t* donnees, size_t taille, const char* nomDuFichier,
                                 const char* typeDeContenu, String& lien, String& erreur) {
    lien = "";
    erreur = "";
    if (verrouDeLEnvoi == nullptr || !leModemEstPret()) { erreur = "modem pas pret"; return false; }
    if (xSemaphoreTake(verrouDeLEnvoi, pdMS_TO_TICKS(5 * UNE_MINUTE_MS)) != pdTRUE) { erreur = "envoi deja en cours"; return false; }
    const uint32_t debut = millis();
    const bool reussi = envoyer(donnees, taille, nomDuFichier, typeDeContenu, lien, erreur);
    xSemaphoreGive(verrouDeLEnvoi);
    ecrireDansLeJournal("[INTERNET] Envoi de %s (%u octets) en %lu s : %s", nomDuFichier, (unsigned)taille,
                        (unsigned long)((millis() - debut) / 1000), reussi ? lien.c_str() : erreur.c_str());
    return reussi;
}

bool demarrerModuleEnvoiInternet() {
    verrouDeLEnvoi = xSemaphoreCreateMutex();
    reponseDuServeurArrivee = xSemaphoreCreateBinary();
    if (verrouDeLEnvoi == nullptr || reponseDuServeurArrivee == nullptr) return false;
    ecouterLesNotificationsDuModem("+HTTPACTION:", quandLeServeurARepondu);
    return true;
}
