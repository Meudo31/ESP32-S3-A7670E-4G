#include <Arduino.h>
#include <TFT_eSPI.h> // Bibliothèque graphique principale

#include "module_batterie.h"
#include "module_gps.h"
#include "module_gyroscope.h"
#include "module_DateHeure.h"
#include "module_CANbus.h"


/*L'écran GC9A01 communique via le protocole SPI. 
  Nous configurons les broches sur le bus SPI secondaire (SPI2) afin de ne pas interférer avec la carte SD.
	VCC ➡️ 3V3
	GND ➡️ GND
	SCL (Clock) ➡️ GPIO 12
	SDA (MOSI) ➡️ GPIO 11
	RES (Reset) ➡️ GPIO 10
	DC (Data/Command) ➡️ GPIO 9
	CS (Chip Select) ➡️ GPIO 14 (ou une broche libre disponible)
	BLK (Backlight / Optionnel) ➡️ 3V3 (pour l'allumer en continu)*/


TFT_eSPI tft = TFT_eSPI();
TFT_eSprite img = TFT_eSprite(&tft); // Sprite RAM anti-scintillement

// Variable d'animation pour le clignotement des icônes (Warnings/Clignotants)
bool flashAlternateur = false;

void dessinIndicateursSpeciaux() {
    flashAlternateur = !flashAlternateur; // Alterne vrai/faux à chaque cycle (250ms)

    // 1. CLIGNOTANT GAUCHE (Flèche verte à gauche)
    if (lastCANbusData.turnSignalLeft) {
        if (flashAlternateur) {
            img.fillTriangle(25, 120, 40, 110, 40, 130, TFT_GREEN);
            img.fillRect(40, 115, 15, 10, TFT_GREEN);
        }
    }

    // 2. CLIGNOTANT DROIT (Flèche verte à droite)
    if (lastCANbusData.turnSignalRight) {
        if (flashAlternateur) {
            img.fillTriangle(215, 120, 200, 110, 200, 130, TFT_GREEN);
            img.fillRect(185, 115, 15, 10, TFT_GREEN);
        }
    }

    // 3. PLEIN PHARE (Icône Bleue en haut au centre)
    if (lastCANbusData.highBeamActive) {
        img.fillRoundRect(110, 45, 20, 12, 4, TFT_BLUE);
        img.drawFastHLine(95, 48, 10, TFT_BLUE);
        img.drawFastHLine(95, 51, 10, TFT_BLUE);
        img.drawFastHLine(95, 54, 10, TFT_BLUE);
    }

    // 4. ALERTE BÉQUILLE SORTIE (Texte Rouge clignotant)
    if (lastCANbusData.kickstandSwitchOut) {
        if (flashAlternateur) {
            img.setTextColor(TFT_RED);
            img.drawCentreString("STAND OUT", 120, 195, 2);
        }
    }

    // 5. ALERTES DYNAMIQUES : FREIN / EMBRAYAGE
    if (lastCANbusData.brakeLightsActive) {
        img.fillRect(95, 140, 50, 4, TFT_RED); // Ligne rouge de freinage actif
    }
}

bool initialiserEcran() {
    my_log_printf("🎨 [DISPLAY] Initialisation de l'écran circulaire GC9A01...");
    
    tft.init();
    tft.setRotation(0); // Ajustez de 0 à 3 selon l'orientation de votre boîtier
    
    // Création du Sprite de la taille de l'écran (240x240)
    if (img.createSprite(240, 240) == NULL) {
        my_log_printf("❌ [DISPLAY_ERR] Mémoire RAM insuffisante pour le Sprite !");
        return false;
    }
    
    my_log_printf("✅ [DISPLAY] Écran configuré.");
    return true;
}



void tacheGestionEcran(void * pvParameters) {
    my_log_printf("🎨 [DISPLAY_TASK] Démarrage du rafraîchissement graphique.");

    while(1) {
        // 1. Fond d'écran
        img.fillSprite(TFT_BLACK);

        // 2. Compte-tours graphique périphérique (Arc de cercle RPM)
        // On dessine une barre périphérique qui s'allonge selon le régime moteur (max 8000 tr/min)
        int maxRpmLigne = (lastCANbusData.indianRPM * 360) / 8000;
        if (maxRpmLigne > 360) maxRpmLigne = 360;
        
        // Bordure de jauge RPM (Verte si OK, Rouge si zone rouge > 5500 tr/min)
        uint16_t couleurRpm = (lastCANbusData.indianRPM > 5500) ? TFT_RED : TFT_GREEN;
        img.drawCircle(120, 120, 117, TFT_DARKGREY);
        // Approximation visuelle de l'arc de cercle selon le régime moteur
        for (int angle = 0; angle < maxRpmLigne; angle += 4) {
            float rad = (angle - 90) * M_PI / 180.0;
            int xArc = 120 + 117 * cos(rad);
            int yArc = 120 + 117 * sin(rad);
            img.fillCircle(xArc, yArc, 3, couleurRpm);
        }

        // 3. VITESSE NUMÉRIQUE CENTRALE (Grand affichage)
        img.setTextColor(TFT_WHITE);
        img.setTextDatum(MC_DATUM);
        char vitStr[8];
        snprintf(vitStr, sizeof(vitStr), "%0.0f", lastCANbusData.indianVitesseKMH);
        img.drawString(vitStr, 120, 100, 7); // Font 7 (Gros chiffres style compteur)
        
        img.setTextColor(TFT_SILVER);
        img.drawString("km/h", 120, 135, 2);

        // 4. RAPPORT ENGAGÉ (Boîte de vitesse à droite)
        img.fillRect(165, 80, 30, 35, TFT_NAVY);
        img.drawRect(165, 80, 30, 35, TFT_CYAN);
        img.setTextColor(TFT_CYAN);
        if (lastCANbusData.indianRapportEngage == 0) {
            img.drawString("N", 180, 97, 4); // N pour Neutre (Vert en réalité)
        } else {
            img.drawString(String(lastCANbusData.indianRapportEngage), 180, 97, 4);
        }

        // 5. JAUGE D'ESSENCE (À gauche) & TEMPÉRATURE (À droite)
        // Carburant
        img.setTextColor(TFT_YELLOW);
        char carbStr[12];
        snprintf(carbStr, sizeof(carbStr), "⛽ %d%%", lastCANbusData.indianNiveauEssence);
        img.drawString(carbStr, 50, 75, 2);

        // Température moteur
        img.setTextColor(TFT_ORANGE);
        char tempStr[12];
        snprintf(tempStr, sizeof(tempStr), "%0.0f°C 🌡️", lastCANbusData.indianTemperatureMoteur);
        img.drawString(tempStr, 190, 75, 2);

        // 6. LIEN AVEC LES SUPPORTS ET ÉCLAIRAGES MATÉRIELS
        dessinIndicateursSpeciaux();

        // 7. FOOTER : Régime moteur textuel exact
        img.setTextColor(TFT_WHITE);
        char rpmStr[16];
        snprintf(rpmStr, sizeof(rpmStr), "RPM: %d", lastCANbusData.indianRPM);
        img.drawString(rpmStr, 120, 170, 2);

        // 8. Envoi immédiat du buffer à l'écran GC9A01
        img.pushSprite(0, 0);

        // Fréquence d'exécution calquée sur les animations de clignotants (250 ms)
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
