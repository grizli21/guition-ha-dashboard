// 02/11/2026 : page Panneau LED : panneau de luminothérapie rouge / infrarouge de l'atelier (HA ESPHome « panneau_led_rouge »).
// 2 onglets : Séance (temps restant en grand, LED réellement allumées, longueurs d'onde, minuterie / illimité, durée,
// radiateur et ventilateurs, démarrer / arrêter) et Intensités (toutes les LED + les 7 longueurs d'onde, 0-100 %).
#pragma once
#include <lvgl.h>

#define PNL_DUREE_MIN 1  // number.panneau_led_rouge_duree_minuterie : bornes permises par HA
#define PNL_DUREE_MAX 29
#define PNL_RESTANT_INCONNU (-1) // mode Illimité : sensor.panneau_led_rouge_temps_restant n'a pas de valeur

#define PNL_VENTI_MAX 45 // ventilateurs à fond à partir de cette température du radiateur (°C)
#define PNL_SECURITE 50  // coupure de sécurité du panneau au-delà (°C)

// select.panneau_led_rouge_longueurs_d_onde : ALL / RED-YELLOW / NIR
enum pnl_longueurs_t
{
    PNL_TOUTES,
    PNL_ROUGE_JAUNE,
    PNL_NIR
};
// select.panneau_led_rouge_mode
enum pnl_mode_t
{
    PNL_MINUTERIE,
    PNL_ILLIMITE
};

#define PNL_NB_INT 8 // intensités 0-100 % : toutes les LED, puis 590, 630, 660, 810, 830, 850, 940 nm
extern const char *const PNL_CLES_INT[PNL_NB_INT]; // clés du contrat MQTT (mêmes mots dans la page et dans le JSON)

struct pnl_etat_t
{
    bool seance;   // switch.panneau_led_rouge_seance : séance en cours
    bool allumees; // binary_sensor.panneau_led_rouge_led_allumees : LED réellement allumées
    int longueurs; // pnl_longueurs_t
    int mode;      // pnl_mode_t
    int duree;     // durée de la minuterie, minutes
    int restant;   // minutes restantes, PNL_RESTANT_INCONNU en illimité (ou rien de connu)
    int intensites[PNL_NB_INT]; // number.panneau_led_rouge_intensite_*
    int radiateur; // sensor.panneau_led_rouge_temperature_radiateur, °C
    int ventilos;  // sensor.panneau_led_rouge_vitesse_ventilateurs, %
};
extern pnl_etat_t pnl_etat;

// quoi : "seance", "longueurs", "mode", "duree" ou une clé d'intensité (PNL_CLES_INT) — la valeur est déjà dans pnl_etat.
extern void (*pnl_commande)(const char *quoi);

void page_panneau_led_creer(lv_obj_t *zone);
void page_panneau_led_quitter();
void page_panneau_led_maj(); // à appeler quand les données reçues changent (sans effet si la page n'est pas affichée)
