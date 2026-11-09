#include "ui_sonnette.h"
#include "ui_camera.h"
#include "ui_diag.h"
#include "page_reglages.h"
#include <lvgl.h>

#define SONNETTE_MS 60000 // production : video_on_doorbell_timout

// un ÉCRAN à part (et non un calque par-dessus la page) : LVGL ne dessine que l'écran actif ; par-dessus la page, chaque
// mise à jour de celle-ci (heure, températures…) aurait fait redessiner le fond de la vue sur l'image de la caméra
static lv_obj_t *g_ecran = nullptr, *g_precedent = nullptr;
static lv_timer_t *g_minuterie = nullptr;

static void fermer()
{
    if (g_minuterie)
    {
        lv_timer_del(g_minuterie);
        g_minuterie = nullptr;
    }
    if (!g_ecran)
        return;
    ui_trace("[sonnette] vue caméra fermée\n");
    if (g_precedent)
        lv_scr_load(g_precedent);
    lv_obj_del_async(g_ecran); // différé : fermer() peut venir de l'appui sur un de ses objets ; la caméra s'arrête avec la zone
    g_ecran = nullptr;
    g_precedent = nullptr;
}

static void touche(lv_event_t *)
{
    lv_indev_wait_release(lv_indev_get_act()); // le doigt qui ferme ne clique pas la page qui revient en dessous
    fermer();
}

void ui_sonnette_recevoir(bool sonne)
{
    if (!sonne)
    {
        fermer();
        return;
    }
    if (!rg_reglages.camera)
        return;
    lv_disp_trig_activity(NULL); // l'écran se rallume (comme un toucher)
    if (!g_ecran)
    {
        ui_trace("[sonnette] on sonne : vue caméra\n");
        g_precedent = lv_scr_act();
        g_ecran = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(g_ecran, lv_color_black(), 0);
        lv_obj_clear_flag(g_ecran, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *zone = lv_obj_create(g_ecran);
        lv_obj_remove_style_all(zone);
        lv_obj_set_size(zone, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(zone, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(zone, LV_OPA_COVER, 0);
        lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(zone, touche, LV_EVENT_PRESSED, NULL); // fermée dès l'appui
        lv_scr_load(g_ecran);
        ui_camera_afficher(zone, UI_CAM_ENTREE);
    }
    if (g_minuterie) // une nouvelle sonnerie relance les 60 s
        lv_timer_reset(g_minuterie);
    else
        g_minuterie = lv_timer_create([](lv_timer_t *) { fermer(); }, SONNETTE_MS, NULL);
}

bool ui_sonnette_affichee() { return g_ecran != nullptr; }
