#ifndef MODULE_MODEM_H
#define MODULE_MODEM_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE MODEM : dialogue avec le modem 4G SIMCom A7670E par commandes AT.

   Un seul fil (l'UART) relie l'ESP32 au modem, et plusieurs tâches veulent s'en
   servir (SMS, appels, GPS, réseau, haut-parleur...). Ce module est donc le seul
   à lire et écrire sur ce fil :
     - une tâche qui veut parler au modem appelle envoyerCommandeAT() : la
       commande est envoyée, puis la fonction attend la réponse (OK ou ERROR) ;
       un verrou garantit qu'une seule commande est en cours à la fois ;
     - le modem envoie aussi des messages spontanés (« RING » pour un appel,
       « +CMTI » pour un SMS...) : on les appelle des notifications. Les modules
       intéressés s'y abonnent avec ecouterLesNotificationsDuModem().
       La tâche du modem les lit quand aucune commande n'est en cours.

   ⚠️ Une fonction abonnée à une notification ne doit jamais envoyer de commande
      AT elle-même : elle doit juste prévenir sa tâche (file d'attente, notification).
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

enum class ResultatAT { OK, ERREUR, DELAI_DEPASSE, MODEM_ABSENT };

typedef void (*FonctionAppeleeSurNotification)(const String& ligne);

struct IdentiteDuModem {
    String modele;          // ex. "A7670E-FASE"
    String versionLogiciel;
    String imei;
    bool   voixDisponible;  // certaines variantes de l'A7670E n'ont ni voix ni audio
};

// Démarre la tâche du modem (recherche du modem, réglages, puis écoute des notifications).
bool demarrerModuleModem();

bool leModemEstPret();
// Attend que le modem ait fini de démarrer. Renvoie false si l'attente est dépassée.
bool attendreQueLeModemSoitPret(TickType_t attenteMaximum);

// Envoie une commande AT et attend OK ou ERROR. Les lignes de réponse (sans OK) sont mises dans « reponse ».
ResultatAT envoyerCommandeAT(const char* commande, String* reponse = nullptr, uint32_t delaiMaximumMs = 3000);
ResultatAT envoyerCommandeAT(const String& commande, String* reponse = nullptr, uint32_t delaiMaximumMs = 3000);

// Pour les commandes qui demandent des données après l'invite « > » (envoi de SMS, copie de fichier).
ResultatAT envoyerCommandeATAvecDonnees(const String& commande, const uint8_t* donnees, size_t taille,
                                        bool terminerParCtrlZ, String* reponse = nullptr, uint32_t delaiMaximumMs = 20000);

// Pour les commandes qui attendent la ligne « DOWNLOAD » avant les données (AT+HTTPDATA).
ResultatAT envoyerCommandeATAvecTelechargement(const String& commande, const uint8_t* donnees, size_t taille,
                                               uint32_t delaiMaximumMs = 60000);

// Pour les commandes dont les données arrivent APRÈS le OK (AT+HTTPREAD) : lit jusqu'à la ligne
// qui commence par « debutDeLaDerniereLigne » (ex. "+HTTPREAD: 0"), qui n'est pas mise dans la réponse.
ResultatAT envoyerCommandeATJusquALaLigne(const String& commande, const char* debutDeLaDerniereLigne, String* reponse,
                                          uint32_t delaiMaximumMs = 10000);

// Pour enchaîner plusieurs commandes sans qu'une autre tâche ne s'intercale.
bool prendreLeModem(uint32_t attenteMaximumMs = 10000);
void rendreLeModem();

// S'abonne aux notifications qui commencent par « debutDeLigne » (ex. "RING", "+CMTI:").
void ecouterLesNotificationsDuModem(const char* debutDeLigne, FonctionAppeleeSurNotification fonction);

const IdentiteDuModem& identiteDuModem();

#endif // MODULE_MODEM_H
