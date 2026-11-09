// 31/10/2026 : maquettes de la page PC réorganisée en 4 onglets (Système | GPU | Processus | IA) : Claude retiré de
// Système (il est dans IA), graphe de charge CPU + 16 fils façon btop d'origine, contenu de la VRAM par carte façon hub
// hub IA. Simulateur seulement : rien de branché dans le firmware.
#pragma once
#include <lvgl.h>

// variante 0 : graphe à gauche + 16 fils en grille à droite (btop d'origine) ; 1 : graphe en largeur + 16 barres dessous
lv_obj_t *maquette_pc_systeme(int variante);
lv_obj_t *maquette_pc_gpu();
