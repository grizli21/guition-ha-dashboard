// 22/10/2026 : page Ouvrants (maquette validée le 22/10) : store terrasse et volet cuisine (curseur vertical
// façon store, à glisser, + ouvrir / stop / fermer), et la toile du vidéoprojecteur, qui suit la Fire TV
// (automatisation HA 1791000000163) : boutons de secours + « Séance » qui allume seulement la Fire TV.
// Positions comme HA : 100 = ouvert (volet relevé, store banne SORTI, toile remontée), 0 = fermé (toile tout en bas).
// 22/10 : le store terrasse est un store BANNE : sa toile se déroule depuis le caisson quand il sort (retour d'usage).
#pragma once
#include <lvgl.h>

#define OV_NB 2 // store, volet
struct ov_ouvrant_t
{
    const char *nom;
    int pos;
    bool banne; // store banne : la toile dessinée = la partie sortie ; flèche du bas = sortir
};
extern ov_ouvrant_t ov_ouvrants[OV_NB];
extern int ov_toile;          // position de la toile (100 = remontée)
extern bool ov_firetv;        // Fire TV allumée (séance en cours)
extern int ov_toile_mouvement; // 0 arrêtée, 1 remonte, -1 descend

enum ov_action_t
{
    OV_OUVRIR,
    OV_STOP,
    OV_FERMER,
    OV_POSITION, // aller à pos
    OV_SEANCE    // allumer (pos = 1) ou éteindre (pos = 0) la Fire TV
};
// idx : 0 store, 1 volet, OV_NB = toile
extern void (*ov_commande)(int idx, ov_action_t action, int pos);

void page_ouvrants_creer(lv_obj_t *zone);
void page_ouvrants_quitter();
void page_ouvrants_maj();
