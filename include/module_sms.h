#ifndef MODULE_SMS_H
#define MODULE_SMS_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE SMS : envoi et réception des SMS par le modem.
   - Envoi : demanderLEnvoiDUnSMS() met le message dans une file ; la tâche SMS
     l'envoie et prévient l'application du résultat.
   - Réception : le modem signale chaque nouveau SMS (+CMTI). La tâche le lit,
     joue un son sur le haut-parleur, l'enregistre sur la carte SD
     (/sms/03-11-2026.txt) et le transmet à l'application.
   - Les SMS restent stockés dans la carte SIM : l'application peut les lister
     et les supprimer.
   Les accents sont conservés (jeu de caractères UCS2). Un SMS avec accents est
   limité à 70 caractères, un SMS sans accent à 160 : un texte plus long est
   envoyé en plusieurs SMS.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <functional>

struct MessageSMS {
    int    numeroDansLaSim;   // index dans la mémoire de la carte SIM
    String expediteur;
    String date;              // "26/11/03,14:05:09+04"
    String texte;
    bool   lu;
};

// Met un SMS dans la file d'envoi. Renvoie false si le texte ou le numéro est vide, ou si la file est pleine.
bool demanderLEnvoiDUnSMS(const String& numero, const String& texte);

// Lit tous les SMS de la carte SIM (appelle la fonction pour chacun). Renvoie false si le modem ne répond pas.
bool listerLesSMS(const std::function<void(const MessageSMS&)>& pourChaqueSMS);

bool supprimerUnSMS(int numeroDansLaSim);

bool demarrerModuleSMS();

#endif // MODULE_SMS_H
