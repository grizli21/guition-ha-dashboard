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
LV_IMG_DECLARE(colonne_ia_aube);       // 12/11 : ciel animé (maison/ciel)
LV_IMG_DECLARE(colonne_ia_crepuscule);
LV_IMG_DECLARE(scintille_ia);
LV_IMG_DECLARE(nuage3_ia);
LV_IMG_DECLARE(etoiles_haut_ia); // A8 : teintée en blanc par page_ia.cpp
LV_IMG_DECLARE(neige_buisson_ia);
// A8, 1 case de large : une goutte (3 variantes) ou un flocon (2) au milieu d'un motif deux fois plus haut que le couloir
LV_IMG_DECLARE(goutte_ia_0);
LV_IMG_DECLARE(goutte_ia_1);
LV_IMG_DECLARE(goutte_ia_2);
LV_IMG_DECLARE(flocon_ia_0);
LV_IMG_DECLARE(flocon_ia_1);
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
