// 11/11/2026 : images de la page « IA » (maquette validée du 10/11). Dessin régénéré par
// tools/gen_scene_ia.py ; formats LVGL 9.6. Le fond n'est plus deux images 380 x 412 (626 Ko la paire)
// mais une COLONNE 1 x 412 RVB565 répétée par LVGL (LV_IMAGE_ALIGN_TILE) + petits décors RVB565A8
// (étoiles/lune la nuit, soleil/nuages le jour, buisson et touffes toujours) — vérifié au pixel près
// contre la scène de référence par l'outil. Clawd et marques RVB565A8, icônes de rail A8 64 x 64
// recolorables (comme icon_pc). C'est la variante 2 (icon_ia_etincelle) qui est dans le rail
// (icône retenue le 11/11) ; icon_ia et icon_ia_bulle restent présentées (capture 184).
#pragma once
#include <lvgl.h>

LV_IMG_DECLARE(colonne_ia_nuit);
LV_IMG_DECLARE(colonne_ia_jour);
LV_IMG_DECLARE(etoiles_ia);
LV_IMG_DECLARE(lune_ia);
LV_IMG_DECLARE(soleil_ia);
LV_IMG_DECLARE(nuage1_ia);
LV_IMG_DECLARE(nuage2_ia);
LV_IMG_DECLARE(touffes_ia_nuit);
LV_IMG_DECLARE(touffes_ia_jour);
LV_IMG_DECLARE(buisson_ia);
LV_IMG_DECLARE(clawd_ia);
LV_IMG_DECLARE(clawd_ia_repos);
LV_IMG_DECLARE(marque_ia_reflechit);
LV_IMG_DECLARE(marque_ia_attente);
LV_IMG_DECLARE(marque_ia_repos);
LV_IMG_DECLARE(icon_ia);
LV_IMG_DECLARE(icon_ia_etincelle);
LV_IMG_DECLARE(icon_ia_bulle);
