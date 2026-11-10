#ifndef MODULE_CANBUS_H
#define MODULE_CANBUS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE BUS CAN : écoute du bus CAN de la moto (Indian Scout Bobber 2025).

   Matériel : contrôleur CAN intégré à l'ESP32-S3 (TWAI) + transceiver 3,3 V
   SN65HVD230, branché sur la prise diagnostic Delphi 8 broches de la moto :
       H = CAN High (fil jaune)   G = CAN Low (fil vert foncé)   D = masse
       TX du contrôleur → GPIO 3 (CTX)    RX du contrôleur ← GPIO 1 (CRX)
   La résistance de 120 Ω du module transceiver doit être retirée : le bus de
   la moto a déjà ses terminaisons.

   SÉCURITÉ : le contrôleur est en mode ÉCOUTE SEULE (TWAI_MODE_LISTEN_ONLY).
   Il n'envoie jamais rien et n'acquitte même pas les trames : il ne peut pas
   perturber la moto.

   Ce que fait la tâche :
     1. Trouve la vitesse du bus (250, 500, 125 ou 1000 kbit/s) en écoutant,
        ou utilise celle choisie dans l'appli (commande CAN_VITESSE).
     2. Garde la liste de toutes les trames vues (identifiant, compteur,
        dernières données) pour la découverte.
     3. Décode les trames connues : J1939 (Indian / Polaris : 29 bits) et
        OBD-II (réponses 11 bits 0x7E8, si un outil interroge la moto).
     4. Enregistre toutes les trames brutes sur la carte SD pendant les
        sorties : /can/jj-mm-aaaa/jj-mm-aaaa_hh-mm-ss.csv
        (une ligne par trame : millisecondes;identifiant;type;longueur;données)

   Les décodages viennent d'une Indian Springfield 2017 (projet
   Prinsessen/indian-thunderstroke-canbus) et de la norme J1939 : ils sont à
   vérifier sur la Scout 2025. Voir docs/canbus_indian.md.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

// Une trame vue sur le bus (pour l'écran « Trames » de l'appli).
struct TrameCANVue {
    uint32_t identifiant;
    bool     etendue;          // 29 bits (J1939) ou 11 bits
    uint8_t  longueur;
    uint8_t  donnees[8];
    uint32_t compteur;
    uint32_t derniereFoisMs;   // millis() de la dernière réception
};

// true quand des trames arrivent (moto allumée et câblage correct).
bool busCANActif();

// Nombre de trames différentes vues, et copie de l'une d'elles (false si le numéro n'existe plus).
int  nombreDeTramesCANVues();
bool trameCANVue(int numero, TrameCANVue& trame);

// Réglages (gardés en mémoire flash) :
//   vitesse en kbit/s : 0 = recherche automatique, sinon 125, 250, 500 ou 1000.
bool choisirLaVitesseDuBusCAN(int kbits);
void activerLEnregistrementCAN(bool actif);
bool enregistrementCANActif();
// Oublie la liste des trames vues et les codes défaut (les valeurs décodées restent).
void effacerLesTramesCANVues();

// Message {"t":"can", ...} pour l'appli : état du bus et valeurs décodées.
void decrireLeBusCAN(JsonDocument& document);
// Résumé court pour un SMS (régime, vitesse, rapport, carburant...), vide si rien n'est connu.
String resumeDuBusCAN();

bool demarrerModuleCANBus();

#endif // MODULE_CANBUS_H
