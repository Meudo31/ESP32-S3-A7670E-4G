
#include "module_DateHeure.h"


/* Différentes possibilitée de format de date et heure avec strftime()
%d : Jour du mois sur deux chiffres (01 à 31)
%m : Numéro du mois sur deux chiffres (01 à 12)
%Y : Année complète sur 4 chiffres (2026)
%y : Année abrégée sur 2 chiffres (26)
%H : Heure au format 24h (00 à 23)
%I : Heure au format 12h (01 à 12)
%p : Indicateur AM ou PM (utile si format 12h)
%M : Minutes (00 à 59)
%S : Secondes (00 à 59)
%A : Nom du jour de la semaine en toutes lettres (ex: Friday)
%B : Nom du mois en toutes lettres (ex: August)*/

DateHeure_SourcesValues Date_Heure_Source_MaJ;


time_t Heure_RTC(){					// Nb secondes depuis 1970
	return time(NULL);
}

String Heure_formatteeFichier(){	// HH-MM-SS
	time_t timestampActuel = time(NULL);				// 1. Récupération des secondes écoulées depuis 1970
	struct tm *infoTemps = localtime(&timestampActuel);	// 2. Conversion en structure de calendrier (struct tm)
    char messageFormate[16];							// 3. Création d'un tableau de caractères (Buffer) pour stocker le texte

    strftime(messageFormate, sizeof(messageFormate), "%H-%M-%S", infoTemps);
	return (messageFormate);
}

String Heure_formatteeStandard(){	// HH:MM:SS
	time_t timestampActuel = time(NULL);				// 1. Récupération des secondes écoulées depuis 1970
	struct tm *infoTemps = localtime(&timestampActuel);	// 2. Conversion en structure de calendrier (struct tm)
    char messageFormate[16];							// 3. Création d'un tableau de caractères (Buffer) pour stocker le texte

    strftime(messageFormate, sizeof(messageFormate), "%H:%M:%S", infoTemps);
	return (messageFormate);
}

String Date_formatteeUS(){			// YYYY/MM/DD
	time_t timestampActuel = time(NULL);				// 1. Récupération des secondes écoulées depuis 1970
	struct tm *infoTemps = localtime(&timestampActuel);	// 2. Conversion en structure de calendrier (struct tm)
    char messageFormate[16];							// 3. Création d'un tableau de caractères (Buffer) pour stocker le texte

    strftime(messageFormate, sizeof(messageFormate), "%Y/%m/%d", infoTemps);
	return (messageFormate);
}

String Date_formatteeUSFichier(){			// YYYY-MM-DD
	time_t timestampActuel = time(NULL);				// 1. Récupération des secondes écoulées depuis 1970
	struct tm *infoTemps = localtime(&timestampActuel);	// 2. Conversion en structure de calendrier (struct tm)
    char messageFormate[16];							// 3. Création d'un tableau de caractères (Buffer) pour stocker le texte

    strftime(messageFormate, sizeof(messageFormate), "%Y-%m-%d", infoTemps);
	return (messageFormate);
}

String Date_formatteeFR(){			// DD/MM/YYYY
	time_t timestampActuel = time(NULL);				// 1. Récupération des secondes écoulées depuis 1970
	struct tm *infoTemps = localtime(&timestampActuel);	// 2. Conversion en structure de calendrier (struct tm)
    char messageFormate[16];							// 3. Création d'un tableau de caractères (Buffer) pour stocker le texte

    strftime(messageFormate, sizeof(messageFormate), "%d/%m/%Y", infoTemps);
	return (messageFormate);
}

String Date_Heure(){				// DD/MM/YYYY HH:MM:SS
	time_t timestampActuel = time(NULL);				// 1. Récupération des secondes écoulées depuis 1970
	struct tm *infoTemps = localtime(&timestampActuel);	// 2. Conversion en structure de calendrier (struct tm)
    char messageFormate[64];							// 3. Création d'un tableau de caractères (Buffer) pour stocker le texte

    strftime(messageFormate, sizeof(messageFormate), "%d/%m/%Y à %H:%M:%S", infoTemps);
	return (messageFormate);
}

int JourDuMoisCourrant(){
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
	return (lt->tm_mday);
}

int MoisCourrant(){
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
	return (lt->tm_mon);
}

/*
			void reglerHorlogeInterneESP32(int annee, int mois, int jour, int heure, int minute, int seconde) {
				// 1. Remplissage d'une structure de calendrier standard (struct tm)
				struct tm tempsCalendrier;
				tempsCalendrier.tm_year = annee - 1900; // L'année doit compter le décalage depuis 1900
				tempsCalendrier.tm_mon  = mois - 1;     // Les mois commencent à 0 (0 = Janvier)
				tempsCalendrier.tm_mday = jour;
				tempsCalendrier.tm_hour = heure;
				tempsCalendrier.tm_min  = minute;
				tempsCalendrier.tm_sec  = seconde;
				tempsCalendrier.tm_isdst = -1;          // Laisse le système calculer l'heure d'été automatiquement

				// 2. Conversion du calendrier en Timestamp UNIX (nombre de secondes depuis 1970)
				time_t timestampUnix = mktime(&tempsCalendrier);

				// 3. Préparation de la structure demandée par settimeofday
				struct timeval structureTemps;
				structureTemps.tv_sec = timestampUnix; // Attribution des secondes
				structureTemps.tv_usec = 0;            // Zéro microseconde (suffisant pour notre traceur)

				// 4. RÉGLAGE DU COMPTEUR RTC DE L'ESP32
				if (settimeofday(&structureTemps, NULL) == 0) {
					Serial.println("[⏰ RTC] L'horloge interne de l'ESP32-S3 a été synchronisée !");
				} else {
					Serial.println("[⏰ RTC] ❌ Erreur : Impossible de régler l'heure.");
				}
			}
*/