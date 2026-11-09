// 22/10/2026 : page Qualité de l'air (maquette validée le 21/10) : une carte par purificateur : PM2,5 en gros
// (couleur selon le niveau), courbe des 24 dernières heures, une info (COV, filtre…) et ses modes.
// Les purificateurs (noms, modes) sont des données : la page ne connaît que leur nombre.
#pragma once
#include <lvgl.h>

#define AQ_NB 2
#define AQ_HIST 48 // points de la courbe (24 h, un point par demi-heure)
#define AQ_VIDE 255 // point sans mesure (écran redémarré, purificateur hors ligne) : la courbe le saute

struct aq_purificateur_t
{
    const char *piece;
    const char *appareil;
    int pm25;              // µg/m³
    uint8_t hist[AQ_HIST]; // PM2,5 des 24 dernières heures, le plus récent en dernier
    char info[32];         // ex. « COV 112 · 24 h »
    const char *modes[5];
    int nb_modes;
    int mode; // index du mode actif (-1 : aucun bouton, ex. préréglage Xiaomi « Favorite »)
    bool en_ligne;
    // 25/10 (retour d'usage : « l'hélice n'est pas fonction des tours réels ») : vitesse réelle du ventilateur envoyée par HA ;
    // rpm_connu faux (HA ne l'envoie pas) : l'hélice tourne à vitesse fixe dès que le mode n'est pas « Arrêt »
    int rpm;
    bool rpm_connu;
};
extern aq_purificateur_t aq_purificateurs[AQ_NB];

extern void (*aq_commande)(int purificateur, int mode);

void page_air_creer(lv_obj_t *zone);
void page_air_quitter();
void page_air_maj();
