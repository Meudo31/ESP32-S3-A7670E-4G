#ifndef MODULE_WIFI_H
#define MODULE_WIFI_H

#include <Arduino.h>

// Initialise le Wi-Fi (Mode Point d'Accès ou Station)
bool initialiserWifi();

// Tâche FreeRTOS du serveur Web
void tache_ServeurWeb(void * pvParameters);

#endif
