// 08/11/2026 — passage à LVGL 9.6 : ces maquettes dessinaient avec le contexte de dessin de la 8.4
// (lv_draw_ctx_t, lv_event_get_draw_ctx, géométrie de la ligne en arguments). Elles utilisent maintenant
// les wrappers déjà dans src/ui_lvgl/ui_courbe.h (uc_cible_du, uc_objet_du, uc_segment, uc_texte, uc_arc,
// uc_carre) : un seul code pour les deux versions, comme les pages.
#include "maquettes_eau.h"
#include "../src/ui_lvgl/ui_anneau.h"
#include "../src/ui_lvgl/ui_courbe.h"
#include "../src/ui_lvgl/ui_theme.h"
#include "../src/ui_lvgl/ui_courbe.h" // 08/11 : wrappers de dessin pour les deux versions de LVGL
#include <math.h>
#include <stdio.h>
#include <string.h>

// 08/11 : lv_txt_get_width (8.4) n'a plus d'équivalent direct en 9.6 (lv_text_get_size rend largeur ET hauteur).
static int largeur_texte(const char *t, const lv_font_t *f)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_point_t s;
    lv_text_get_size(&s, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return s.x;
#else
    return largeur_texte(t, f);
#endif
}


// valeurs réelles de HA, 25/10 matin
static const float TEMP = 57.9f;     // sensor.ballon_eau_chaude_temperature
static const int CONSIGNE = 50;      // input_number.ballon_consigne
static const int CHAUDE_JOUR = 5;    // sensor.ballon_eau_chaude_eau_chaude_journalier (L)
static const int FROIDE_JOUR = 17;   // sensor.arrivee_eau_journalier (L)
static const float KWH_JOUR = 4.4f;  // sensor.jour_chauffe_eau
static const float DEBIT = 6.2f;     // un débit pendant une douche (L/min) : la maquette montre le bandeau « en cours »
static const int VOLUME = 150;       // contenance du ballon (25/10)
static const int CHAUDE_7J[7] = {38, 52, 41, 60, 47, 55, 5}; // L/jour : fictif (HA garde l'historique du journalier)

static lv_obj_t *fond()
{
    lv_obj_t *f = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, UI_ZONE_L, UI_ZONE_H);
    ui_style_bg_color(f, ui_c(UI_FOND), 0);
    ui_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ui_label(f, UI_POLICE_VALEUR, UI_TEXTE, "Eau chaude"), LV_ALIGN_TOP_MID, 0, 2);
    return f;
}

static lv_obj_t *carte(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t l, lv_coord_t h)
{
    lv_obj_t *c = ui_conteneur(p);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, l, h);
    lv_obj_set_pos(c, x, y);
    return c;
}

// tuile de compteur : petit libellé, grande valeur en police pixel + unité
static void tuile(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t l, const char *nom, uint32_t couleur, const char *valeur, const char *unite)
{
    lv_obj_t *c = carte(p, x, y, l, 64);
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_set_pos(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, nom), 0, 0);
    lv_obj_t *v;
    lv_obj_t *r = ui_valeur_unite(c, UI_POLICE_3X5_P, couleur, unite, &v);
    ui_label_texte(v, valeur);
    lv_obj_align(r, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// 25/10 (V1 : « les 3 cartes donnent l'impression que ce sont des boutons ») : des chiffres posés sur le fond,
// trois colonnes séparées par un trait fin, sans carte
static void chiffres_jour(lv_obj_t *f, lv_coord_t y)
{
    char v[3][16];
    snprintf(v[0], sizeof v[0], "%d", CHAUDE_JOUR);
    snprintf(v[1], sizeof v[1], "%d", FROIDE_JOUR);
    snprintf(v[2], sizeof v[2], "%.1f", KWH_JOUR);
    const char *noms[] = {"eau chaude", "eau totale", "énergie"}, *unites[] = {"L", "L", "kWh"};
    const uint32_t coul[] = {0xFF9A5B, UI_FROID, UI_ACCENT};
    lv_obj_t *t = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE3, "AUJOURD'HUI");
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_set_pos(t, UI_MARGE + 4, y);
    const lv_coord_t l = (UI_ZONE_L - 2 * UI_MARGE) / 3;
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *c = ui_conteneur(f);
        lv_obj_set_size(c, l, 52);
        lv_obj_set_pos(c, UI_MARGE + i * l, y + 18);
        lv_obj_t *val;
        lv_obj_t *r = ui_valeur_unite(c, UI_POLICE_3X5_M, coul[i], unites[i], &val);
        ui_label_texte(val, v[i]);
        lv_obj_align(r, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_align(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, noms[i]), LV_ALIGN_BOTTOM_MID, 0, 0);
        if (i)
        {
            lv_obj_t *s = ui_conteneur(f); // trait de séparation
            lv_obj_set_size(s, 1, 40);
            ui_style_bg_color(s, ui_c(UI_BORD), 0);
            ui_style_bg_opa(s, LV_OPA_COVER, 0);
            lv_obj_set_pos(s, UI_MARGE + i * l, y + 24);
        }
    }
}

static void boutons_modes(lv_obj_t *p, lv_coord_t y)
{
    const char *noms[] = {"Arrêt", "Auto", "Forcé"};
    const lv_coord_t l = (UI_ZONE_L - 2 * UI_MARGE - 16) / 3;
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *b = ui_bouton(p, l, 48);
        lv_obj_set_pos(b, UI_MARGE + i * (l + 8), y);
        if (i == 1)
            ui_style_ajouter(b, &ui_st_bouton_principal, 0);
        lv_obj_center(ui_label(b, UI_POLICE_TEXTE, i == 1 ? UI_SUR_ACCENT : UI_TEXTE, noms[i]));
    }
}

// bandeau « eau chaude en cours » (quand le débit n'est pas nul)
static void bandeau_debit(lv_obj_t *p, lv_coord_t y)
{
    lv_obj_t *c = carte(p, UI_MARGE, y, UI_ZONE_L - 2 * UI_MARGE, 36);
    ui_style_border_color(c, ui_c(UI_FROID), 0);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_t *t = ui_label(c, UI_POLICE_TEXTE, UI_FROID, ""); // la police de 16 a les symboles
    ui_label_texte_fmt(t, LV_SYMBOL_DOWNLOAD "  Eau chaude en cours · %.1f L/min", DEBIT);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
}

// --- V1 : ballon dessiné ---------------------------------------------------------------------------------------------
static void ballon_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    // cuve : chaud en haut (stratification), part chaude selon la température (55 °C et plus : presque toute la cuve)
    const float chaud = LV_CLAMP(0.1f, (TEMP - 20) / 40, 0.95f);
    const lv_coord_t h = lv_area_get_height(&a), limite = a.y1 + (lv_coord_t)(h * chaud);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 0;
    d.bg_grad.dir = LV_GRAD_DIR_VER;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = ui_c(0xFF6A3D);
    d.bg_grad.stops[0].frac = 0;
    d.bg_grad.stops[1].color = ui_c(0xFFB547);
    d.bg_grad.stops[1].frac = 255;
    lv_area_t z = {a.x1, a.y1, a.x2, limite};
    lv_draw_rect(ctx, &d, &z);
    d.bg_grad.stops[0].color = ui_c(0xC98F3A);
    d.bg_grad.stops[1].color = ui_c(UI_FROID);
    z = {a.x1, (lv_coord_t)(limite + 1), a.x2, a.y2};
    lv_draw_rect(ctx, &d, &z);
    // bulles (pendant la chauffe : elles montent)
    lv_draw_rect_dsc_t b;
    lv_draw_rect_dsc_init(&b);
    b.radius = LV_RADIUS_CIRCLE;
    b.bg_opa = LV_OPA_TRANSP;
    b.border_color = ui_c(0xFFE2B0);
    b.border_width = 1;
    b.border_opa = LV_OPA_70;
    const int bx[] = {22, 58, 90, 40, 75, 30, 100}, by[] = {40, 70, 30, 110, 140, 170, 95}, br[] = {4, 3, 5, 3, 4, 2, 3};
    for (int i = 0; i < 7; i++)
    {
        lv_area_t c = {(lv_coord_t)(a.x1 + bx[i] - br[i]), (lv_coord_t)(a.y1 + by[i] - br[i]), (lv_coord_t)(a.x1 + bx[i] + br[i]),
                       (lv_coord_t)(a.y1 + by[i] + br[i])};
        lv_draw_rect(ctx, &b, &c);
    }
}

static void v1(lv_obj_t *f)
{
    // cuve arrondie (découpe des coins)
    lv_obj_t *cuve = ui_conteneur(f);
    lv_obj_set_size(cuve, 124, 220);
    lv_obj_set_pos(cuve, 34, 60);
    lv_obj_set_style_radius(cuve, 40, 0);
    lv_obj_set_style_clip_corner(cuve, true, 0);
    ui_style_border_color(cuve, ui_c(UI_BORD), 0);
    ui_style_border_width(cuve, 3, 0);
    lv_obj_set_style_border_post(cuve, true, 0);
    lv_obj_add_event_cb(cuve, ballon_cb, LV_EVENT_DRAW_MAIN, NULL);
    // graduations
    for (int k = 0; k < 4; k++)
    {
        lv_obj_t *g = ui_conteneur(f);
        lv_obj_set_size(g, 10, 2);
        ui_style_bg_color(g, ui_c(UI_TEXTE3), 0);
        ui_style_bg_opa(g, LV_OPA_COVER, 0);
        lv_obj_set_pos(g, 20, 100 + k * 45);
    }
    // à droite : température, consigne, état
    lv_obj_t *v;
    lv_obj_t *r = ui_valeur_unite(f, UI_POLICE_TEMP, UI_TEXTE, "°C", &v);
    ui_label_texte_fmt(v, "%d", (int)(TEMP + 0.5f));
    lv_obj_set_pos(r, 180, 70);
    lv_obj_t *c = ui_label(f, UI_POLICE_PETITE, UI_TEXTE2, "");
    ui_label_texte_fmt(c, "consigne %d °C · au repos", CONSIGNE);
    lv_obj_set_pos(c, 184, 176);
    lv_obj_t *dch = ui_label(f, UI_POLICE_TEXTE, UI_ACCENT, "");
    ui_label_texte_fmt(dch, "≈ %d douches", (int)(VOLUME * LV_CLAMP(0.0f, (TEMP - 38) / (TEMP - 12), 1.0f) * 1.6f / 50));
    lv_obj_set_pos(dch, 184, 204);
    lv_obj_t *e = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE3, "");
    ui_label_texte_fmt(e, "combles 14 °C · chauffe 0 W");
    lv_obj_set_pos(e, 184, 236);
    chiffres_jour(f, 292);
    boutons_modes(f, 372);
    bandeau_debit(f, 428);
}

// --- V2 / V3 : anneau ------------------------------------------------------------------------------------------------
static void anneau_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const lv_point_t c = {(lv_coord_t)((a.x1 + a.x2) / 2), (lv_coord_t)((a.y1 + a.y2) / 2)};
    ui_anneau_dessiner(ctx, &c, lv_area_get_width(&a), 22, (TEMP - 20) / 50);
}

static lv_obj_t *anneau(lv_obj_t *f, lv_coord_t d, lv_coord_t y)
{
    lv_obj_t *o = ui_conteneur(f);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, (UI_ZONE_L - d) / 2, y);
    lv_obj_add_event_cb(o, anneau_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

static void barres_7j(lv_obj_t *f, lv_coord_t y)
{
    lv_obj_t *c = carte(f, UI_MARGE, y, UI_ZONE_L - 2 * UI_MARGE, 70);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_set_pos(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "Eau chaude · 7 jours"), 0, 0);
    const char *j[] = {"sa", "di", "lu", "ma", "me", "je", "auj."};
    for (int i = 0; i < 7; i++)
    {
        const lv_coord_t h = (lv_coord_t)(CHAUDE_7J[i] * 34 / 60);
        lv_obj_t *b = ui_conteneur(c);
        lv_obj_set_size(b, 26, LV_MAX(h, 2));
        lv_obj_set_style_radius(b, 3, 0);
        ui_style_bg_color(b, ui_c(i == 6 ? UI_ACCENT : 0x8A5A2E), 0);
        ui_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 150 + i * 30, -2);
        lv_obj_t *l = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE3, j[i]);
        lv_obj_align_to(l, b, LV_ALIGN_OUT_TOP_MID, 0, -1);
        if (i == 6)
            lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 150 + i * 30 - 2, -14);
    }
    lv_obj_t *m = ui_label(c, UI_POLICE_PETITE, UI_TEXTE, "moy. 49 L/j");
    lv_obj_set_pos(m, 0, 26);
}

static void v2(lv_obj_t *f)
{
    anneau(f, 200, 48);
    lv_obj_t *v;
    lv_obj_t *r = ui_valeur_unite(f, UI_POLICE_TEMP_PETITE, UI_TEXTE, "°C", &v);
    ui_label_texte_fmt(v, "%d", (int)(TEMP + 0.5f));
    lv_obj_align(r, LV_ALIGN_TOP_MID, 0, 118);
    lv_obj_t *s = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    ui_label_texte_fmt(s, "consigne %d · au repos", CONSIGNE);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 172);
    char t[16];
    snprintf(t, sizeof t, "%d", CHAUDE_JOUR);
    tuile(f, UI_MARGE, 254, 120, "Eau chaude auj.", 0xFF9A5B, t, "L");
    snprintf(t, sizeof t, "%d", FROIDE_JOUR);
    tuile(f, UI_MARGE + 128, 254, 120, "Eau froide auj.", UI_FROID, t, "L");
    snprintf(t, sizeof t, "%.1f", KWH_JOUR);
    tuile(f, UI_MARGE + 256, 254, 124, "Énergie auj.", UI_ACCENT, t, "kWh");
    barres_7j(f, 326);
    bandeau_debit(f, 404);
    lv_obj_t *mo = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE3, "Arrêt · Auto · Forcé : appui sur l'anneau");
    lv_obj_align(mo, LV_ALIGN_TOP_MID, 0, 448);
}

static void courbe_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = ui_c(UI_FROID);
    // débit d'eau chaude sur 24 h (fictif) : douche du matin, vaisselle midi, douche du soir
    for (lv_coord_t x = a.x1; x <= a.x2; x++)
    {
        const float h = (float)(x - a.x1) / lv_area_get_width(&a) * 24;
        float q = 7 * expf(-powf((h - 7.2f) / 0.15f, 2)) + 3 * expf(-powf((h - 12.5f) / 0.2f, 2)) + 6 * expf(-powf((h - 21) / 0.12f, 2));
        const lv_coord_t y = (lv_coord_t)(a.y2 - q / 8 * lv_area_get_height(&a));
        if (y < a.y2)
        {
            lv_area_t z = {x, y, x, a.y2};
            lv_draw_rect(ctx, &d, &z);
        }
    }
}

static void v3(lv_obj_t *f)
{
    anneau(f, 236, 48);
    const int douches = (int)(VOLUME * LV_CLAMP(0.0f, (TEMP - 38) / (TEMP - 12), 1.0f) * 1.6f / 50);
    lv_obj_t *n;
    lv_obj_t *r = ui_valeur_unite(f, UI_POLICE_TEMP, UI_ACCENT, "", &n);
    ui_label_texte_fmt(n, "%d", douches);
    lv_obj_align(r, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_t *s = ui_label(f, UI_POLICE_TEXTE, UI_TEXTE, "douches chaudes");
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 206);
    lv_obj_t *s2 = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    ui_label_texte_fmt(s2, "%d °C · ballon de %d L · consigne %d", (int)(TEMP + 0.5f), VOLUME, CONSIGNE);
    lv_obj_align(s2, LV_ALIGN_TOP_MID, 0, 290 - 4 - 14);
    lv_obj_t *c = carte(f, UI_MARGE, 306, UI_ZONE_L - 2 * UI_MARGE, 92);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_t *l = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    ui_label_texte_fmt(l, "Débit d'eau chaude · 24 h · aujourd'hui %d L chaude, %d L froide", CHAUDE_JOUR, FROIDE_JOUR);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_t *g = ui_conteneur(c);
    lv_obj_set_size(g, lv_pct(100), 56);
    lv_obj_align(g, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(g, courbe_cb, LV_EVENT_DRAW_MAIN, NULL);
    boutons_modes(f, 410);
}

lv_obj_t *maquette_eau(int v)
{
    lv_obj_t *f = fond();
    if (v == 1)
        v1(f);
    else if (v == 2)
        v2(f);
    else
        v3(f);
    return f;
}

// ======================================================================================  3 onglets (25/10, retenu)
#define T_MIN 30 // échelle du ballon (°C) : bas de la cuve
#define T_MAX 70 //                            haut de la cuve
#define CUVE_X 40 // même place dans Ballon et Réglages
#define CUVE_Y 66
#define CUVE_L 120
#define CUVE_H 250

static bool g_chauffe; // maquette : relais du chauffe-eau en marche

static void onglet_cb(lv_event_t *) {}

static lv_obj_t *fond_onglets(int choisi)
{
    lv_obj_t *f = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, UI_ZONE_L, UI_ZONE_H);
    ui_style_bg_color(f, ui_c(UI_FOND), 0);
    ui_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    static const char *const NOMS[] = {"Ballon", "Courbe", "Réglages"};
    ui_onglets(f, NOMS, 3, choisi, onglet_cb, nullptr, nullptr);
    return f;
}

static lv_coord_t y_de(float t) { return (lv_coord_t)(CUVE_H - (LV_CLAMP(T_MIN, t, T_MAX) - T_MIN) / (T_MAX - T_MIN) * CUVE_H); }

// résistance plongeante : bride au fond, serpentin à 4 branches et doigt de gant du thermostat ; grise au repos,
// rouge incandescent avec un halo quand le relais chauffe
static void resistance(uc_cible_t *ctx, const lv_area_t &a)
{
    const lv_coord_t cx = (a.x1 + a.x2) / 2, bas = a.y2 - 10, haut = a.y2 - 96, creux = a.y2 - 26;
    const uint32_t couleur = g_chauffe ? 0xFF4A2A : 0x8A929C;
    for (int passe = g_chauffe ? 0 : 1; passe < 2; passe++)
    {
        lv_draw_line_dsc_t l;
        lv_draw_line_dsc_init(&l);
        l.color = ui_c(passe ? couleur : 0xFF7A3D);
        l.width = passe ? 4 : 12; // 1re passe : halo large et transparent
        l.opa = passe ? LV_OPA_COVER : LV_OPA_30;
        l.round_start = l.round_end = 1;
        lv_draw_arc_dsc_t c;
        lv_draw_arc_dsc_init(&c);
        c.color = l.color;
        c.width = l.width;
        c.opa = l.opa;
        const lv_coord_t x[4] = {(lv_coord_t)(cx - 18), (lv_coord_t)(cx - 6), (lv_coord_t)(cx + 6), (lv_coord_t)(cx + 18)};
        const lv_coord_t y0[4] = {bas, creux, creux, bas};
        for (int j = 0; j < 4; j++)
        {
            const lv_point_t p1 = {x[j], y0[j]}, p2 = {x[j], haut};
            uc_segment(ctx, l, p1.x, p1.y, p2.x, p2.y);
        }
        const lv_point_t h1 = {(lv_coord_t)(cx - 12), haut}, h2 = {(lv_coord_t)(cx + 12), haut}, b1 = {cx, creux};
        uc_arc(ctx, c, &h1, 6 + l.width / 2, 180, 360);
        uc_arc(ctx, c, &h2, 6 + l.width / 2, 180, 360);
        uc_arc(ctx, c, &b1, 6 + l.width / 2, 0, 180);
    }
    // doigt de gant (thermostat), à côté du serpentin
    lv_draw_rect_dsc_t g;
    lv_draw_rect_dsc_init(&g);
    g.bg_color = ui_c(0x5A626C);
    g.radius = 3;
    lv_area_t z = {(lv_coord_t)(cx + 30), (lv_coord_t)(a.y2 - 70), (lv_coord_t)(cx + 35), bas};
    lv_draw_rect(ctx, &g, &z);
    // bride
    g.bg_color = ui_c(0x3A424D);
    z = {(lv_coord_t)(cx - 30), (lv_coord_t)(a.y2 - 10), (lv_coord_t)(cx + 40), a.y2};
    lv_draw_rect(ctx, &g, &z);
}

// cuve : remplie (dégradé chaud) jusqu'au niveau, vide au-dessus ; bulles tout le temps (plus nombreuses au-dessus de la
// résistance en chauffe)
static void cuve_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = ui_c(0x161A20);
    lv_draw_rect(ctx, &d, &a);
    d.bg_grad.dir = LV_GRAD_DIR_VER;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = ui_c(0xFF6A3D);
    d.bg_grad.stops[0].frac = 0;
    d.bg_grad.stops[1].color = ui_c(0x6E8FA8);
    d.bg_grad.stops[1].frac = 255;
    const float niveau = *(const float *)lv_event_get_user_data(e);
    lv_area_t z = {a.x1, (lv_coord_t)(a.y2 - niveau * lv_area_get_height(&a)), a.x2, a.y2};
    lv_draw_rect(ctx, &d, &z);
    resistance(ctx, a);
    lv_draw_rect_dsc_t b;
    lv_draw_rect_dsc_init(&b);
    b.radius = LV_RADIUS_CIRCLE;
    b.bg_opa = LV_OPA_TRANSP;
    b.border_color = ui_c(0xFFE2B0);
    b.border_width = 1;
    b.border_opa = LV_OPA_60;
    // les 7 premières : partout ; les 5 suivantes : colonne au-dessus du serpentin, en chauffe seulement
    const int bx[] = {22, 92, 34, 98, 70, 26, 88, 52, 66, 58, 70, 54}, by[] = {40, 62, 96, 120, 30, 150, 175, 128, 108, 84, 60, 40},
              br[] = {3, 4, 5, 3, 2, 4, 3, 2, 3, 2, 3, 2};
    for (int i = 0; i < (g_chauffe ? 12 : 7); i++)
    {
        lv_area_t c = {(lv_coord_t)(a.x1 + bx[i] - br[i]), (lv_coord_t)(a.y1 + by[i] - br[i]), (lv_coord_t)(a.x1 + bx[i] + br[i]),
                       (lv_coord_t)(a.y1 + by[i] + br[i])};
        if (c.y1 > z.y1)
            lv_draw_rect(ctx, &b, &c);
    }
}

static lv_obj_t *cuve(lv_obj_t *f, lv_coord_t x, lv_coord_t y, const float *niveau)
{
    lv_obj_t *o = ui_conteneur(f);
    lv_obj_set_size(o, CUVE_L, CUVE_H);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, 36, 0);
    lv_obj_set_style_clip_corner(o, true, 0);
    ui_style_border_color(o, ui_c(UI_BORD), 0);
    ui_style_border_width(o, 3, 0);
    lv_obj_set_style_border_post(o, true, 0);
    lv_obj_add_event_cb(o, cuve_cb, LV_EVENT_DRAW_MAIN, (void *)niveau);
    return o;
}

// ligne d'information : libellé discret à gauche, valeur à droite
static void ligne_info(lv_obj_t *f, lv_coord_t y, const char *nom, const char *valeur, uint32_t couleur)
{
    lv_obj_set_pos(ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE3, nom), 186, y + 2);
    lv_obj_align(ui_label(f, UI_POLICE_PETITE, couleur, valeur), LV_ALIGN_TOP_RIGHT, -UI_MARGE, y);
}

static void trait_h(lv_obj_t *f, lv_coord_t x, lv_coord_t y, lv_coord_t l)
{
    lv_obj_t *s = ui_conteneur(f);
    lv_obj_set_size(s, l, 1);
    ui_style_bg_color(s, ui_c(UI_BORD), 0);
    ui_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_pos(s, x, y);
}

// 25/10 (retour d'usage : « aucun réglage dans la page ballon, la consigne correspond au haut du ballon ») : cuve pleine = à la
// consigne ; à droite, de haut en bas jusqu'au pied de la cuve : température, douches, état ; en dessous, la journée
static void onglet_ballon(lv_obj_t *f)
{
    static float niveau;
    niveau = LV_CLAMP(0.0f, (TEMP - T_MIN) / (CONSIGNE - T_MIN), 1.0f);
    cuve(f, CUVE_X, CUVE_Y, &niveau);
    lv_obj_t *hc = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    ui_label_texte_fmt(hc, "%d°", CONSIGNE);
    lv_obj_set_pos(hc, 6, CUVE_Y + 4);
    lv_obj_t *hb = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE3, "");
    ui_label_texte_fmt(hb, "%d°", T_MIN);
    lv_obj_set_pos(hb, 6, CUVE_Y + CUVE_H - 18);
    lv_obj_t *v;
    lv_obj_t *r = ui_valeur_unite(f, UI_POLICE_TEMP, UI_TEXTE, "°C", &v);
    ui_label_texte_fmt(v, "%d", (int)(TEMP + 0.5f));
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 62);
    const int douches = (int)(VOLUME * LV_CLAMP(0.0f, (TEMP - 38) / (TEMP - 12), 1.0f) * 1.6f / 50);
    lv_obj_t *n;
    lv_obj_t *rd = ui_valeur_unite(f, UI_POLICE_3X5_M, UI_ACCENT, douches > 1 ? "douches" : "douche", &n);
    ui_label_texte_fmt(n, "%d", douches);
    lv_obj_align(rd, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 174);
    trait_h(f, 186, 222, UI_ZONE_L - 186 - UI_MARGE);
    char s[32];
    snprintf(s, sizeof s, g_chauffe ? "chauffe · 2,4 kW" : "au repos");
    ligne_info(f, 234, "Chauffe", s, g_chauffe ? 0xFF4A2A : UI_TEXTE2);
    ligne_info(f, 260, "Mode", "Auto · éco", UI_TEXTE2);
    ligne_info(f, 286, "Combles", "14 °C", UI_TEXTE2);
    chiffres_jour(f, 336);
    bandeau_debit(f, 428);
}

// Réglages : la même cuve, à la même place, graduée 30-70 °C, avec le curseur de consigne ; les modes et le régime éco
// en bas, sur toute la largeur
static void onglet_reglages(lv_obj_t *f)
{
    static float niveau;
    niveau = LV_CLAMP(0.0f, (TEMP - T_MIN) / (T_MAX - T_MIN), 1.0f);
    for (int tg = T_MIN; tg <= T_MAX; tg += 10)
    {
        lv_obj_t *l = ui_label(f, UI_POLICE_LEGENDE, tg == T_MIN || tg == T_MAX ? UI_TEXTE3 : UI_TEXTE3, "");
        ui_label_texte_fmt(l, "%d°", tg);
        lv_obj_set_pos(l, 6, CUVE_Y + y_de(tg) - 7 + (tg == T_MAX ? 6 : tg == T_MIN ? -6 : 0));
    }
    cuve(f, CUVE_X, CUVE_Y, &niveau);
    // curseur de consigne : trait ambre en travers de la cuve + bouton rond à droite (glisser haut / bas)
    const lv_coord_t yc = CUVE_Y + y_de(CONSIGNE);
    lv_obj_t *trait = ui_conteneur(f);
    lv_obj_set_size(trait, CUVE_L + 10, 3);
    ui_style_bg_color(trait, ui_c(UI_ACCENT), 0);
    ui_style_bg_opa(trait, LV_OPA_COVER, 0);
    lv_obj_set_pos(trait, CUVE_X, yc - 1);
    lv_obj_t *bouton = ui_conteneur(f);
    lv_obj_set_size(bouton, 30, 30);
    lv_obj_set_style_radius(bouton, LV_RADIUS_CIRCLE, 0);
    ui_style_bg_color(bouton, ui_c(UI_ACCENT), 0);
    ui_style_bg_opa(bouton, LV_OPA_COVER, 0);
    lv_obj_set_pos(bouton, CUVE_X + CUVE_L + 4, yc - 15);
    // à droite, comme la température dans Ballon : la consigne en grand
    lv_obj_t *t = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE3, "CONSIGNE");
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_align(t, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 60);
    lv_obj_t *v;
    lv_obj_t *r = ui_valeur_unite(f, UI_POLICE_TEMP, UI_ACCENT, "°C", &v);
    ui_label_texte_fmt(v, "%d", CONSIGNE);
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 78);
    lv_obj_t *aide = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE3, "glisser le curseur\nsur le ballon");
    lv_obj_set_style_text_align(aide, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(aide, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 186);
    trait_h(f, 186, 232, UI_ZONE_L - 186 - UI_MARGE);
    char s[24];
    snprintf(s, sizeof s, "%d °C", (int)(TEMP + 0.5f));
    ligne_info(f, 244, "Eau", s, UI_TEXTE2);
    snprintf(s, sizeof s, "%d L", VOLUME);
    ligne_info(f, 270, "Contenance", s, UI_TEXTE2);
    ligne_info(f, 296, "Chauffe", "relais tableau", UI_TEXTE2);
    boutons_modes(f, 336);
    lv_obj_t *c = carte(f, UI_MARGE, 398, UI_ZONE_L - 2 * UI_MARGE, 64);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 0, 0);
    lv_obj_align(ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "Régime éco"), LV_ALIGN_TOP_LEFT, 0, 12);
    lv_obj_align(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "une seule montée la nuit, en heures creuses"), LV_ALIGN_BOTTOM_LEFT, 0, -12);
    lv_obj_t *sw = lv_switch_create(c);
    lv_obj_set_size(sw, 50, 28);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_style_bg_color(sw, ui_c(0x3A424D), LV_PART_MAIN);
    ui_style_bg_color(sw, ui_c(UI_VERT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    ui_style_bg_color(sw, ui_c(0xF4F5F6), LV_PART_KNOB);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
}

static int16_t g_t24[288], g_c24[288];
static float heure_point(int i) { return fmodf(9 + i * 5 / 60.0f, 24); }
static bool relais(int i) { const float h = heure_point(i); return h > 0.4f && h < 3.9f; }
static bool heures_creuses(int i) { const float h = heure_point(i); return (h > 0.4f && h < 5.4f) || (h > 13.9f && h < 16.9f); }
static uint8_t g_q24[288];
// litres d'eau chaude par heure (heure k = 9h + k) : douche du soir, vaisselle, douche du matin
static const uint8_t g_l24[24] = {0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 4, 0, 46, 0, 0, 0, 0, 0, 0, 0, 0, 0, 38, 6};
static void courbe24_cb(lv_event_t *e)
{
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    uc_cadre_t c = {uc_cible_du(e), (lv_coord_t)(a.x1 + 40), (lv_coord_t)(a.x2 - 8), (lv_coord_t)(a.y1 + 8), (lv_coord_t)(a.y1 + 200),
                    0, 288, 30, 70};
    uc_grille(c, 10, "°");
    uc_heures(c, 9 * 60, 5, 360);
    uc_bandes(c, relais, 0xE5484D); // relais de chauffe : rouge, comme la chauffe du graphe
    // heures creuses / pleines : fine bande sous l'axe des heures (HC en vert, HP en gris)
    lv_draw_rect_dsc_t hd;
    lv_draw_rect_dsc_init(&hd);
    for (int i = 0; i < 288; i++)
    {
        hd.bg_color = ui_c(heures_creuses(i) ? 0x2F7D57 : 0x2B323C);
        const lv_coord_t x0 = c.x(i), x1 = i < 287 ? c.x(i + 1) : c.x1;
        lv_area_t z = {x0, (lv_coord_t)(c.bas + 22), x1, (lv_coord_t)(c.bas + 27)};
        lv_draw_rect(c.ctx, &hd, &z);
    }
    const uc_serie_t t = uc_serie(g_t24, -32768, 10);
    uc_degrade(c, t, 0xFF6A3D);
    uc_escalier(c, uc_serie(g_c24, -32768, 10), UI_TEXTE);
    uc_principale(c, t, 0xFF8A4B);
    // eau chaude tirée par tranche de 2 h, alignée sur l'axe des heures : une case discrète par tranche, barre + litres
    // au-dessus si on a tiré (25/10 : « une barre pour 2 h, le chiffre sur la barre »)
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = UI_POLICE_LEGENDE;
    ld.color = ui_c(UI_FROID);
    ld.align = LV_TEXT_ALIGN_CENTER;
    const lv_coord_t bas = a.y1 + 344, hmax = 46;
    for (int k = 0; k < 12; k++)
    {
        const int litres = g_l24[2 * k] + g_l24[2 * k + 1];
        const lv_coord_t x0 = c.x(k * 24) + 3, x1 = c.x(LV_MIN(k * 24 + 24, 287)) - 3;
        d.bg_color = ui_c(UI_BORD);
        lv_area_t z = {x0, (lv_coord_t)(bas - 2), x1, bas};
        lv_draw_rect(c.ctx, &d, &z);
        if (litres == 0)
            continue;
        d.bg_color = ui_c(UI_FROID);
        z.y1 = (lv_coord_t)(bas - LV_MAX(4, LV_MIN(hmax, litres * hmax / 50)));
        lv_draw_rect(c.ctx, &d, &z);
        char s[8];
        snprintf(s, sizeof s, "%d", litres);
        lv_area_t zt = {(lv_coord_t)(x0 - 8), (lv_coord_t)(z.y1 - 16), (lv_coord_t)(x1 + 8), (lv_coord_t)(z.y1 - 2)};
        uc_texte(c.ctx, ld, &zt, s, true);
    }
}

static void onglet_courbe(lv_obj_t *f)
{
    // 24 h fictives jusqu'à 9h, simulées pas à pas : refroidissement lent, chute à chaque puisage (débit), montée en
    // heures creuses (2h -> 5h30) jusqu'à 60 °C
    float t = 57;
    for (int i = 0; i < 288; i++)
    {
        const float h = fmodf(9 + i * 5 / 60.0f, 24), pas = 5 / 60.0f;
        const float q = 8 * expf(-powf((h - 7.3f) / 0.18f, 2)) + 4 * expf(-powf((h - 12.6f) / 0.2f, 2)) + 9 * expf(-powf((h - 21.2f) / 0.15f, 2));
        t -= 0.25f * pas + q * 0.12f;
        if (h > 0.4f && h < 3.9f) // relais en marche (vrai créneau du 25/10)
            t = LV_MIN(60.0f, t + 4.5f * pas);
        g_t24[i] = (int16_t)(t * 10);
        g_c24[i] = 500;
        g_q24[i] = (uint8_t)LV_MIN(12.0f, q);
    }
    lv_obj_t *v;
    lv_obj_t *r = ui_valeur_unite(f, UI_POLICE_TEMP_PETITE, UI_TEXTE, "°C", &v);
    ui_label_texte_fmt(v, "%d", (int)(TEMP + 0.5f));
    lv_obj_set_pos(r, 16, 54);
    int imn = 0, imx = 0;
    for (int i = 1; i < 288; i++)
    {
        if (g_t24[i] < g_t24[imn])
            imn = i;
        if (g_t24[i] > g_t24[imx])
            imx = i;
    }
    lv_obj_t *mn = ui_label(f, UI_POLICE_TEXTE, UI_FROID, "");
    ui_label_texte_fmt(mn, "min %d° à %02dh%02d", (g_t24[imn] + 5) / 10, (int)heure_point(imn), (int)(heure_point(imn) * 60) % 60);
    lv_obj_align(mn, LV_ALIGN_TOP_RIGHT, -14, 58);
    lv_obj_t *mx = ui_label(f, UI_POLICE_TEXTE, 0xFF8A4B, "");
    ui_label_texte_fmt(mx, "max %d° à %02dh%02d", (g_t24[imx] + 5) / 10, (int)heure_point(imx), (int)(heure_point(imx) * 60) % 60);
    lv_obj_align(mx, LV_ALIGN_TOP_RIGHT, -14, 82);
    lv_obj_t *g = ui_conteneur(f);
    lv_obj_set_size(g, UI_ZONE_L, UI_ZONE_H - 118 - 4);
    lv_obj_set_pos(g, 0, 118);
    lv_obj_add_event_cb(g, courbe24_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_t *qt = ui_label(f, UI_POLICE_LEGENDE, UI_FROID, "");
    ui_label_texte(qt, "Eau chaude tirée · litres par 2 h");
    lv_obj_set_pos(qt, 4, 118 + 262);
    // légende : chauffe, HC / HP
    const char *noms[] = {"chauffe", "heures creuses", "heures pleines"};
    const uint32_t coul[] = {0xE5484D, 0x2F7D57, 0x2B323C};
    lv_coord_t x = 4;
    for (int k = 0; k < 3; k++)
    {
        lv_obj_t *p = ui_conteneur(f);
        lv_obj_set_size(p, 10, 10);
        lv_obj_set_style_radius(p, 2, 0);
        ui_style_bg_color(p, ui_c(coul[k]), 0);
        ui_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_pos(p, x, 118 + 234);
        lv_obj_t *l = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE2, noms[k]);
        lv_obj_set_pos(l, x + 14, 118 + 231);
        x += 14 + largeur_texte(noms[k], UI_POLICE_LEGENDE) + 12;
    }
}

lv_obj_t *maquette_eau_onglet(int onglet)
{
    lv_obj_t *f = fond_onglets(onglet == 3 ? 0 : onglet);
    g_chauffe = onglet == 3;
    if (onglet == 0 || onglet == 3)
        onglet_ballon(f);
    else if (onglet == 1)
        onglet_courbe(f);
    else
        onglet_reglages(f);
    return f;
}
