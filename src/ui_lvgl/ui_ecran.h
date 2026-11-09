// 05/11/2026 : scroll par bande, étage 1 — l'adaptateur par lequel le patch de la librairie
// (lib/lvgl-8.4.0/src/core/lv_obj_scroll.c) accède à la mémoire d'image. La librairie ne déclare
// ces deux fonctions que FAIBLES (__attribute__((weak)) → NULL) : un projet qui ne les définit
// pas (le simulateur, la production) garde le code LVGL d'origine, octet pour octet.
#pragma once
#include <stdint.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Renvoie le tampon dans lequel LVGL dessinera la prochaine image (mode deux mémoires, rotation 0),
// ou NULL si l'écran n'est pas dans ce mode (chambres : rotation 90, mode partiel).
// NULL = chemin rapide désactivé.
uint8_t *ui_ecran_memoire_active(void);

// Signale au projet que le décalage vient d'écrire (par le CPU, hors des zones dessinées par LVGL)
// les lignes de cette zone écran dans le tampon où LVGL va dessiner : elles sont salies dans le
// cache interne et doivent être vidées en PSRAM (cache_ecrire) avant la bascule de la dalle.
void ui_ecran_zone_decalee(const lv_area_t *zone);


#ifdef __cplusplus
}
#endif
