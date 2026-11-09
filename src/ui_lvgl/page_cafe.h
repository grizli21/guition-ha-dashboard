// 22/10/2026 : page Café (maquette validée le 21/10), avec un sélecteur Café | Bouilloire.
// Programmer = une heure, sans jour à choisir : c'est la prochaine occurrence (aujourd'hui si l'heure n'est pas
// passée, sinon demain). Café : raccourcis « dans 15 min » + les 2 dernières heures utilisées.
// Bouilloire : 5 températures (50 / 70 / 80 / 90 / 100 °C, boutons de l'ESPHome .215).
#pragma once
#include <lvgl.h>

#define CF_CAFE 0
#define CF_BOUILLOIRE 1

struct cf_programme_t
{
    int minute; // heure programmée, en minutes depuis minuit
    bool actif;
};
extern cf_programme_t cf_programmes[2]; // café, bouilloire
extern int cf_recentes[2];              // 2 dernières heures utilisées pour le café (minutes, -1 = aucune)
extern int cf_temp;                     // index de la température de la bouilloire (0..4)
extern bool cf_bouilloire_chauffe;      // binary_sensor.bouilloire_en_chauffe
extern int cf_maintenant;               // heure courante (minutes depuis minuit), mise à jour par l'appelant

enum cf_action_t
{
    CF_PROGRAMMER, // programme[appareil] a changé (heure ou actif)
    CF_DIRECT,     // café maintenant, ou chauffer la bouilloire à la température choisie
    CF_ARRETER     // arrêter la bouilloire
};
extern void (*cf_commande)(int appareil, cf_action_t action);
extern const int CF_TEMPERATURES[5];

void page_cafe_creer(lv_obj_t *zone);
void page_cafe_quitter();
void page_cafe_maj();
