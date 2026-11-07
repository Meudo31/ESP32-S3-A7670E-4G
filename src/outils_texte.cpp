#include "outils_texte.h"

namespace {

bool estUnChiffreHexadecimal(char caractere) { return isxdigit((unsigned char)caractere) != 0; }

uint16_t lireQuatreChiffresHexadecimaux(const char* texte) {
    char quatreChiffres[5] = { texte[0], texte[1], texte[2], texte[3], '\0' };
    return (uint16_t)strtoul(quatreChiffres, nullptr, 16);
}

void ajouterLeCaractereEnUtf8(String& texte, uint32_t pointDeCode) {
    if (pointDeCode < 0x80) {
        texte += (char)pointDeCode;
    } else if (pointDeCode < 0x800) {
        texte += (char)(0xC0 | (pointDeCode >> 6));
        texte += (char)(0x80 | (pointDeCode & 0x3F));
    } else if (pointDeCode < 0x10000) {
        texte += (char)(0xE0 | (pointDeCode >> 12));
        texte += (char)(0x80 | ((pointDeCode >> 6) & 0x3F));
        texte += (char)(0x80 | (pointDeCode & 0x3F));
    } else {
        texte += (char)(0xF0 | (pointDeCode >> 18));
        texte += (char)(0x80 | ((pointDeCode >> 12) & 0x3F));
        texte += (char)(0x80 | ((pointDeCode >> 6) & 0x3F));
        texte += (char)(0x80 | (pointDeCode & 0x3F));
    }
}

void ajouterQuatreChiffres(String& texte, uint16_t valeur) {
    char quatreChiffres[5];
    snprintf(quatreChiffres, sizeof(quatreChiffres), "%04X", valeur);
    texte += quatreChiffres;
}

} // namespace

String convertirTexteEnUcs2Hexadecimal(const String& texteUtf8) {
    String resultat;
    const uint8_t* octets = (const uint8_t*)texteUtf8.c_str();
    const size_t   taille = texteUtf8.length();
    size_t position = 0;
    while (position < taille) {
        uint32_t pointDeCode;
        const uint8_t premier = octets[position];
        if (premier < 0x80) {
            pointDeCode = premier;
            position += 1;
        } else if ((premier & 0xE0) == 0xC0 && position + 1 < taille) {
            pointDeCode = ((premier & 0x1F) << 6) | (octets[position + 1] & 0x3F);
            position += 2;
        } else if ((premier & 0xF0) == 0xE0 && position + 2 < taille) {
            pointDeCode = ((premier & 0x0F) << 12) | ((octets[position + 1] & 0x3F) << 6) | (octets[position + 2] & 0x3F);
            position += 3;
        } else if ((premier & 0xF8) == 0xF0 && position + 3 < taille) {
            pointDeCode = ((premier & 0x07) << 18) | ((octets[position + 1] & 0x3F) << 12)
                        | ((octets[position + 2] & 0x3F) << 6) | (octets[position + 3] & 0x3F);
            position += 4;
        } else {
            pointDeCode = '?';
            position += 1;
        }
        if (pointDeCode >= 0x10000) {   // émoji : deux mots de 16 bits (paire de substitution)
            pointDeCode -= 0x10000;
            ajouterQuatreChiffres(resultat, 0xD800 | (pointDeCode >> 10));
            ajouterQuatreChiffres(resultat, 0xDC00 | (pointDeCode & 0x3FF));
        } else {
            ajouterQuatreChiffres(resultat, (uint16_t)pointDeCode);
        }
    }
    return resultat;
}

String convertirUcs2HexadecimalEnTexte(const String& texteHexadecimal) {
    String resultat;
    const char* texte = texteHexadecimal.c_str();
    const size_t taille = texteHexadecimal.length();
    for (size_t position = 0; position + 4 <= taille; position += 4) {
        uint32_t pointDeCode = lireQuatreChiffresHexadecimaux(texte + position);
        const bool debutDePaire = pointDeCode >= 0xD800 && pointDeCode <= 0xDBFF;
        if (debutDePaire && position + 8 <= taille) {
            const uint16_t finDePaire = lireQuatreChiffresHexadecimaux(texte + position + 4);
            pointDeCode = 0x10000 + ((pointDeCode - 0xD800) << 10) + (finDePaire - 0xDC00);
            position += 4;
        }
        ajouterLeCaractereEnUtf8(resultat, pointDeCode);
    }
    return resultat;
}

String decoderSiCEstDeLUcs2(const String& texte) {
    if (texte.length() < 4 || texte.length() % 4 != 0) return texte;
    for (size_t position = 0; position < texte.length(); position++) {
        if (!estUnChiffreHexadecimal(texte[position])) return texte;
    }
    // Un numéro de téléphone en UCS2 n'a que des caractères latins : chaque groupe commence par "00".
    for (size_t position = 0; position < texte.length(); position += 4) {
        if (texte[position] != '0' || texte[position + 1] != '0') return texte;
    }
    return convertirUcs2HexadecimalEnTexte(texte);
}

bool texteEnAsciiSimple(const String& texteUtf8) {
    for (size_t position = 0; position < texteUtf8.length(); position++) {
        const uint8_t caractere = (uint8_t)texteUtf8[position];
        if (caractere >= 0x80 || caractere == '[' || caractere == ']' || caractere == '{' || caractere == '}'
            || caractere == '\\' || caractere == '^' || caractere == '~' || caractere == '|' || caractere == '`') {
            return false;   // ces caractères n'existent pas (ou comptent double) dans l'alphabet SMS de base
        }
    }
    return true;
}

size_t nombreDeCaracteres(const String& texteUtf8) {
    size_t nombre = 0;
    for (size_t position = 0; position < texteUtf8.length(); position++) {
        if (((uint8_t)texteUtf8[position] & 0xC0) != 0x80) nombre++;
    }
    return nombre;
}

int decouperLesChamps(const String& ligne, String champs[], int nombreMaximumDeChamps) {
    int nombreDeChamps = 0;
    bool entreGuillemets = false;
    String champEnCours;
    for (size_t position = 0; position < ligne.length(); position++) {
        const char caractere = ligne[position];
        if (caractere == '"') {
            entreGuillemets = !entreGuillemets;
        } else if (caractere == ',' && !entreGuillemets) {
            if (nombreDeChamps < nombreMaximumDeChamps) champs[nombreDeChamps++] = champEnCours;
            champEnCours = "";
        } else {
            champEnCours += caractere;
        }
    }
    if (nombreDeChamps < nombreMaximumDeChamps) champs[nombreDeChamps++] = champEnCours;
    for (int numero = 0; numero < nombreDeChamps; numero++) champs[numero].trim();
    return nombreDeChamps;
}

String nettoyerPourCommandeAT(const String& texte) {
    String resultat = texte;
    resultat.replace("\"", "'");
    resultat.replace("\\", "/");
    resultat.replace("\r", " ");
    resultat.replace("\n", " ");
    return resultat;
}

namespace {

// Équivalent sans accent d'un caractère Latin-1 (U+00C0 à U+00FF), ou nullptr.
const char* lettreSansAccent(uint32_t pointDeCode) {
    static const char* const TABLE[64] = {
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",   // C0..CF
        "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "", "ss",    // D0..DF
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // E0..EF
        "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "", "y",     // F0..FF
    };
    if (pointDeCode >= 0xC0 && pointDeCode <= 0xFF) return TABLE[pointDeCode - 0xC0];
    switch (pointDeCode) {
        case 0x0152: return "OE";
        case 0x0153: return "oe";
        case 0x00AB: case 0x00BB: case 0x201C: case 0x201D: return "\"";
        case 0x2018: case 0x2019: return "'";
        case 0x2013: case 0x2014: case 0x2500: case 0x2550: return "-";
        case 0x2026: return "...";
        case 0x00B0: return " deg";
        case 0x00A0: case 0x202F: return " ";
        case 0x2192: return "->";
        default:     return nullptr;
    }
}

} // namespace

String texteSansAccentPourSMS(const String& texteUtf8) {
    String resultat;
    resultat.reserve(texteUtf8.length());
    const uint8_t* octets = (const uint8_t*)texteUtf8.c_str();
    const size_t   taille = texteUtf8.length();
    size_t position = 0;
    while (position < taille) {
        const uint8_t premier = octets[position];
        if (premier < 0x80) {
            switch ((char)premier) {
                case '[': case '{': resultat += '('; break;
                case ']': case '}': resultat += ')'; break;
                case '_': case '~': case '^': case '|': case '\\': resultat += '-'; break;
                case '\r': break;
                default: resultat += (char)premier; break;
            }
            position++;
            continue;
        }
        uint32_t pointDeCode = 0;
        size_t   longueur = 1;
        if ((premier & 0xE0) == 0xC0)      { pointDeCode = premier & 0x1F; longueur = 2; }
        else if ((premier & 0xF0) == 0xE0) { pointDeCode = premier & 0x0F; longueur = 3; }
        else if ((premier & 0xF8) == 0xF0) { pointDeCode = premier & 0x07; longueur = 4; }
        if (position + longueur > taille) break;
        for (size_t suite = 1; suite < longueur; suite++) pointDeCode = (pointDeCode << 6) | (octets[position + suite] & 0x3F);
        position += longueur;
        const char* remplacement = lettreSansAccent(pointDeCode);
        if (remplacement != nullptr) resultat += remplacement;   // les autres symboles (émojis...) sont supprimés
    }
    return resultat;
}
