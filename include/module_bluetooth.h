#ifndef MODULE_BLUETOOTH_H
#define MODULE_BLUETOOTH_H

#include "main.h"

// Initialise le Bluetooth Classic
bool initialiserBluetooth();

// Tâche FreeRTOS pour diffuser les logs en temps réel
void tacheLogsBluetooth(void * pvParameters);

#endif
