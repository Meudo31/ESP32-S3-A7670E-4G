#ifndef MODULE_APPELS_H
#define MODULE_APPELS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE APPELS : appels vocaux par le modem.
   - Passer un appel, décrocher un appel entrant, raccrocher.
   - Pendant la sonnerie d'un appel entrant, le haut-parleur sonne.
   - Micro : la carte a un micro MEMS soudé (boîtier métal marqué « 6186 », à côté du
     connecteur USB), câblé sur l'entrée MIC_P/MIC_N du modem A7670E. Le correspondant
     nous entend donc sans rien brancher. Réglages : gain (AT+CMICGAIN 0..8, mémorisé),
     micro coupé (AT+CMUT), test (MICRO_TEST). Appliqués au démarrage et à chaque appel.
   - Touches DTMF pendant l'appel (AT+VTS), pour un serveur vocal ou une messagerie.
   - Décrochage automatique (option, mémorisée) : seulement pour le numéro des alertes,
     après quelques secondes de sonnerie (écouter autour de la moto, appel mains libres).
   - SMS « Appelle » envoyé depuis le numéro des alertes : le module rappelle ce numéro.
   La tâche suit l'état de l'appel (notifications du modem + vérification AT+CLCC)
   et prévient l'application à chaque changement.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

enum class EtatDUnAppel : uint8_t {
    AUCUN,              // pas d'appel
    SONNERIE,           // appel entrant, pas encore décroché
    NUMEROTATION,       // appel sortant, le correspondant n'a pas encore décroché
    EN_COMMUNICATION,
};

struct AppelEnCours {
    EtatDUnAppel etat;
    String       numero;
    uint32_t     debutMs;   // début de la communication (millis)
};

AppelEnCours appelEnCours();
const char* nomDeLEtatDUnAppel(EtatDUnAppel etat);

bool appeler(const String& numero);
bool decrocher();
bool raccrocher();

// Le module appelle le numéro des alertes (SMS « Appelle »). false si pas de numéro ou appel en cours.
bool rappelerLeNumeroDesAlertes();
// Touches 0-9, *, #, A-D envoyées au correspondant pendant la communication.
bool envoyerUneToucheDTMF(const String& touches);

bool reglerLeGainDuMicro(int gain);   // 0..8, mémorisé
int  gainDuMicroActuel();
void couperLeMicro(bool coupe);
bool leMicroEstCoupe();
void activerLeDecrochageAutomatique(bool actif);
bool leDecrochageAutomatiqueEstActif();
void testerLeMicroDuModule();         // réponse MICRO_TEST à l'application

void decrireLAppelEnCours(JsonDocument& document);

bool demarrerModuleAppels();

#endif // MODULE_APPELS_H
