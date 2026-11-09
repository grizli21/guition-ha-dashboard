// 22/10/2026 : page Lumières (maquette validée le 22/10) : sélecteur de pièce (Toutes + une par pièce), lampes en
// colonne à gauche (rond = allumer/éteindre la lampe ; tap = régler cette lampe ; APPUI LONG = l'ajouter à la
// sélection, comme l'ancienne page), à droite : disque chromatique (teinte = angle, saturation = blanc au
// centre -> pur au bord), jauge de luminosité, jauge de température de blanc, Allumer / Éteindre la sélection.
// Envoi à HA : light.turn_on hs_color [teinte, saturation] ou color_temp_kelvin, brightness_pct.
// 23/10 : nom, pièce et « sait faire la couleur » viennent aussi de HA (valeurs ci-dessous = en attendant le 1er message) ;
// les onglets de pièces restent ceux de la page.
#pragma once
#include <lvgl.h>

#define LU_NB 11

struct lu_lampe_t
{
    const char *nom;
    const char *piece;
    bool couleur;    // sait faire la couleur (sinon blanc réglable seulement)
    bool on;
    int lum;         // %
    bool mode_hs;    // true : couleur (teinte/saturation), false : blanc (kelvin)
    int kelvin;      // 2200..6500
    int teinte, sat; // 0..359, 0..100
    bool hors_ligne;
};
extern lu_lampe_t lu_lampes[LU_NB];

extern void (*lu_commande)(int lampe); // la lampe a changé (marche, luminosité, couleur…)

void page_lumieres_creer(lv_obj_t *zone);
void page_lumieres_quitter();
void page_lumieres_maj();
