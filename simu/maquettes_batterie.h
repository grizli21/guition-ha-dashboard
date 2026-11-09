// 26/10/2026 : maquettes du mode batterie (écran du séjour, radar LD2410S) : la page « Batterie et capteur »
// (batterie, réveils sur 24 h, dernier réveil) et la page Radar (16 zones, déclenchement / maintien). Simulateur seulement : rien de branché dans le firmware.
#pragma once
#include <lvgl.h>

// en_charge : l'écran est sur l'USB ; renvoie l'objet à supprimer après capture
lv_obj_t *maquette_page_batterie(bool en_charge); // 26/10 : page dédiée (la page Info n'est pas touchée)
// onglet 0 Déclenchement, 1 Maintien ; zone choisie (0-15)
lv_obj_t *maquette_radar(int onglet, int zone);
