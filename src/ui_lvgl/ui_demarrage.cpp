#include "ui_demarrage.h"
#include "ui_theme.h"
#include "ui_appareil.h"

LV_FONT_DECLARE(heavitas_60);
LV_IMG_DECLARE(icon_espressif);
LV_IMG_DECLARE(icon_arduino);

static lv_obj_t *o_ecran, *o_barre, *o_etat;

void ui_demarrage_creer(const char *piece)
{
    o_ecran = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o_ecran);
    lv_obj_set_size(o_ecran, UI_ECRAN, UI_ECRAN);
    ui_style_bg_color(o_ecran, ui_c(UI_FOND), 0);
    ui_style_bg_opa(o_ecran, LV_OPA_COVER, 0);
    lv_obj_add_flag(o_ecran, LV_OBJ_FLAG_CLICKABLE); // le doigt ne passe pas à l'interface dessous
    lv_obj_clear_flag(o_ecran, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titre = ui_label(o_ecran, &heavitas_60, UI_TEXTE, "DASHBOARD");
    lv_obj_align(titre, LV_ALIGN_CENTER, 0, -130);
    lv_obj_t *p = ui_label(o_ecran, UI_POLICE_VALEUR, UI_ACCENT, piece ? piece : "");
    lv_obj_align_to(p, titre, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    o_barre = lv_bar_create(o_ecran);
    lv_obj_set_size(o_barre, 280, 12);
    lv_obj_align(o_barre, LV_ALIGN_CENTER, 0, 20);
    lv_bar_set_range(o_barre, 0, 100);
    ui_style_bg_color(o_barre, ui_c(UI_CARTE2), LV_PART_MAIN);
    ui_style_bg_color(o_barre, ui_c(UI_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(o_barre, 300, 0);

    o_etat = ui_label(o_ecran, UI_POLICE_PETITE, UI_TEXTE2, TXT("Démarrage…", "Starting…"));
    lv_obj_set_width(o_etat, 360);
    lv_obj_set_style_text_align(o_etat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(o_etat, o_barre, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    lv_obj_t *esp = lv_img_create(o_ecran);
    ui_img_src(esp, &icon_espressif);
    lv_obj_align(esp, LV_ALIGN_BOTTOM_LEFT, 40, -40);
    lv_obj_t *ard = lv_img_create(o_ecran);
    ui_img_src(ard, &icon_arduino);
    lv_obj_align(ard, LV_ALIGN_BOTTOM_RIGHT, -40, -20);
}

void ui_demarrage_etape(const char *texte, int pct)
{
    if (!o_ecran)
        return;
    ui_label_texte(o_etat, texte);
    if (pct >= 0 && lv_bar_get_value(o_barre) != pct)
        lv_bar_set_value(o_barre, pct, LV_ANIM_ON);
}

static lv_obj_t *g_a_supprimer = nullptr;
static void supprimer()
{
    if (g_a_supprimer)
        lv_obj_del(g_a_supprimer);
    g_a_supprimer = nullptr;
}

// 24/10 (retour d'usage : « la transition n'est pas fluide ») : plus de fondu dessiné (l'écran entier semi-transparent au-dessus
// de la page dessinée pour la 1re fois : saccadé sur l'ESP32) ; passage par le rétroéclairage (ui_appareil_transition)
void ui_demarrage_fin()
{
    if (!o_ecran)
        return;
    g_a_supprimer = o_ecran;
    o_ecran = o_barre = o_etat = nullptr;
    ui_appareil_transition(supprimer);
}

bool ui_demarrage_affiche() { return o_ecran != nullptr; }
