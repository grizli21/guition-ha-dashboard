// 06/11/2026 (mission RAM, retour d'usage : « 8k c'est accepté / 100k de moins ce n'est pas acceptable ») :
// backend d'allocation LVGL 9.6 en LV_STDLIB_CUSTOM — le même choix que la 8.4 livrée
// (maitre-84/include/lv_conf.h:49 : « LV_MEM_CUSTOM 1 » avec ps_malloc, realloc en ps_realloc :74).
// Le réglage vient de .banc_v9_esp/gen_conf.py (ligne « LV_USE_STDLIB_MALLOC »), plus d'édition
// manuelle de conf_v9/lv_conf.h. En LV_STDLIB_CLIB (conf générée jusqu'ici), le
// heap_caps_malloc_extmem_enable(512) de setup()
// ne poussait en PSRAM que les blocs > 512 o : les structures LVGL (objets, styles, écrans, tâches
// de dessin, une par zone sale et par image) restaient dans le tas interne — mesuré 06/11 :
// interne 41 656 o au début de la page Informations, 14 996 o à la fin, plus gros bloc 7 156 o.
// Contrat des quatre fonctions lu dans lib/lvgl-9.6.0/include/lvgl/stdlib/lv_mem.h:122-141.
// Comme ps_malloc en 8.4 : PSRAM d'abord, repli RAM interne si la PSRAM manque.
#include <lvgl.h>

// Garde : ne définir ces symboles QUE quand la conf choisit le backend CUSTOM. En CLIB, la lib
// fournit déjà lv_malloc_core/lv_free_core/lv_realloc_core/lv_mem_monitor_core
// (src/stdlib/clib/lv_mem_core_clib.c:64 et :86) et lv_mem_init (:38) — sans cette condition,
// double définition au lien (vérifié par la lecture, pas encore par un build CLIB de ce fichier).
#if LVGL_VERSION_MAJOR >= 9 && defined(ARDUINO) && LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
#include <esp_heap_caps.h>

// Appelés par lv_init/lv_deinit (src/lv_init.c:176 et :523) : chaque backend en fournit un ; en
// LV_STDLIB_CUSTOM c'est à l'application de le fournir. Rien à initialiser avec heap_caps.
void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

void *lv_malloc_core(size_t taille)
{
    void *p = heap_caps_malloc(taille, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(taille, MALLOC_CAP_8BIT); // repli interne, comme ps_malloc
}

void lv_free_core(void *p) { heap_caps_free(p); }

void *lv_realloc_core(void *p, size_t taille)
{
    void *n = heap_caps_realloc(p, taille, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!n)
        n = heap_caps_realloc(p, taille, MALLOC_CAP_8BIT); // repli interne
    return n;
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    const size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t libre = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    mon->total_size = total;
    mon->free_size = libre;
    mon->free_biggest_size = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    mon->used_pct = total ? (uint8_t)(100 - (unsigned)(100 * libre / total)) : 0;
    mon->frag_pct = libre ? (uint8_t)(100 - (unsigned)(100 * mon->free_biggest_size / libre)) : 0;
    mon->free_cnt = mon->used_cnt = 0; // non fournis par heap_caps ; le moniteur ne sert qu'aux logs LVGL
    mon->max_used = 0;
}
#endif
