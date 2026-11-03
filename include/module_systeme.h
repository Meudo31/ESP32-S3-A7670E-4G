#ifndef MODULE_SYSTEME_H
#define MODULE_SYSTEME_H

/* ══════════════════════════════════════════════════════════════════════════════
   MODULE SYSTÈME
   - Crée toutes les tâches FreeRTOS du programme et garde leur liste
     (pour afficher dans l'application la pile restante de chacune).
   - Rassemble les informations sur le processeur et les mémoires.
   - Sa tâche surveille la mémoire et prévient dans le journal quand une tâche
     manque de pile.
   ══════════════════════════════════════════════════════════════════════════════ */

#include <Arduino.h>
#include <ArduinoJson.h>

#define NOMBRE_MAXIMUM_DE_TACHES 24

struct TacheSurveillee {
    const char*  nom;
    TaskHandle_t poignee;
    uint32_t     tailleDeLaPile;     // octets
    UBaseType_t  priorite;
    BaseType_t   coeur;
};

// Crée une tâche FreeRTOS sur un cœur donné et l'ajoute à la liste des tâches surveillées.
// Renvoie true si la tâche a bien été créée.
bool creerTacheSurveillee(TaskFunction_t fonctionDeLaTache, const char* nom, uint32_t tailleDeLaPile,
                          UBaseType_t priorite, BaseType_t coeur, TaskHandle_t* poigneeCreee = nullptr);

// Nombre de tâches créées et accès à chacune (pour l'application).
int nombreDeTachesSurveillees();
const TacheSurveillee& tacheSurveillee(int numero);

// Raison du dernier redémarrage, en français (« Mise sous tension », « Plantage »...).
const char* raisonDuDernierRedemarrage();

// Nombre de démarrages depuis la première mise en service (mémorisé en flash).
uint32_t nombreDeDemarrages();

// Remplit un document JSON avec les informations processeur / flash / firmware.
void decrireLeProcesseur(JsonDocument& document);
// Remplit un document JSON avec l'état des mémoires (RAM interne et PSRAM).
void decrireLesMemoires(JsonDocument& document);

// Lance la tâche de surveillance de la mémoire et des piles.
bool demarrerModuleSysteme();

#endif // MODULE_SYSTEME_H
