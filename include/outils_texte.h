#ifndef OUTILS_TEXTE_H
#define OUTILS_TEXTE_H

/* ══════════════════════════════════════════════════════════════════════════════
   OUTILS DE TEXTE
   Le modem est réglé en jeu de caractères « UCS2 » (AT+CSCS="UCS2") pour que les
   SMS gardent leurs accents : chaque caractère y est écrit en 4 chiffres
   hexadécimaux. Exemple : "Été" ↔ "00C9007400E9".
   Ces fonctions font la traduction entre ce format et le texte UTF-8 de l'ESP32.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>

// "Été" → "00C9007400E9"
String convertirTexteEnUcs2Hexadecimal(const String& texteUtf8);

// "00C9007400E9" → "Été"
String convertirUcs2HexadecimalEnTexte(const String& texteHexadecimal);

// Un numéro ou un nom renvoyé par le modem peut être en UCS2 ou en clair :
// on ne le traduit que s'il ressemble vraiment à de l'UCS2 ("002B0033..." pour "+3...").
String decoderSiCEstDeLUcs2(const String& texte);

// true si le texte ne contient que des caractères ASCII simples (un SMS peut alors faire 160 caractères).
bool texteEnAsciiSimple(const String& texteUtf8);

// Nombre de caractères (et non d'octets) d'un texte UTF-8.
size_t nombreDeCaracteres(const String& texteUtf8);

// Rend un texte sûr pour un SMS « sans accent » (160 caractères par SMS) :
// accents retirés (é → e, ç → c, œ → oe), guillemets « » → ", tirets longs → -,
// caractères absents de l'alphabet GSM ([ ] { } _ ~ ^ | \\) remplacés, autres symboles supprimés.
String texteSansAccentPourSMS(const String& texteUtf8);

// Découpe une ligne « a,"b,c",d » en champs, sans couper à l'intérieur des guillemets. Les guillemets sont retirés.
int decouperLesChamps(const String& ligne, String champs[], int nombreMaximumDeChamps);

// Remplace " et \ pour pouvoir glisser un texte dans une commande AT.
String nettoyerPourCommandeAT(const String& texte);

#endif // OUTILS_TEXTE_H
