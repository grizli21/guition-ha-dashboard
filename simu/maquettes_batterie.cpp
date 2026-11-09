// 08/11/2026 — passage à LVGL 9.6 : ces maquettes dessinaient avec le contexte de dessin de la 8.4
// (lv_draw_ctx_t, lv_event_get_draw_ctx, géométrie de la ligne en arguments). Elles utilisent maintenant
// les wrappers déjà dans src/ui_lvgl/ui_courbe.h (uc_cible_du, uc_objet_du, uc_segment, uc_texte, uc_arc,
// uc_carre) : un seul code pour les deux versions, comme les pages.
#include "maquettes_batterie.h"
#include "../src/ui_lvgl/ui_theme.h"
#include "../src/ui_lvgl/ui_courbe.h" // 08/11 : wrappers de dessin pour les deux versions de LVGL
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


// valeurs de la maquette : batterie à 3,92 V (78 %), débranchée le 24/10 à 21h40 ; radar : quelqu'un à 2,40 m ;
// réveil de la production (page Info du 26/10) : Wi-Fi 0,17 s, MQTT 0,16 s, démarrage 1,39 s
static bool g_charge;
static const int POURCENT = 78;

static lv_obj_t *fond(const char *titre)
{
    lv_obj_t *f = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, UI_ZONE_L, UI_ZONE_H);
    ui_style_bg_color(f, ui_c(UI_FOND), 0);
    ui_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    if (titre)
        lv_obj_align(ui_label(f, UI_POLICE_VALEUR, UI_TEXTE, titre), LV_ALIGN_TOP_MID, 0, 2);
    return f;
}

// ---------------------------------------------------------------------------------------------------- batterie
static uint32_t couleur_batterie() { return g_charge || POURCENT > 30 ? UI_VERT : POURCENT > 15 ? UI_ACCENT : UI_ROUGE; }

// pile dessinée : corps arrondi + téton, remplie selon le pourcentage ; éclair blanc en charge
static void pile_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 10;
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = ui_c(UI_TEXTE2);
    d.border_width = 3;
    lv_area_t corps = {a.x1, a.y1, (lv_coord_t)(a.x2 - 9), a.y2};
    lv_draw_rect(ctx, &d, &corps);
    lv_draw_rect_dsc_t t;
    lv_draw_rect_dsc_init(&t);
    t.radius = 3;
    t.bg_color = ui_c(UI_TEXTE2);
    lv_area_t teton = {(lv_coord_t)(a.x2 - 7), (lv_coord_t)(a.y1 + 16), a.x2, (lv_coord_t)(a.y2 - 16)};
    lv_draw_rect(ctx, &t, &teton);
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = 6;
    r.bg_color = ui_c(couleur_batterie());
    const lv_coord_t l = lv_area_get_width(&corps) - 14;
    lv_area_t plein = {(lv_coord_t)(corps.x1 + 7), (lv_coord_t)(corps.y1 + 7), (lv_coord_t)(corps.x1 + 7 + l * POURCENT / 100),
                       (lv_coord_t)(corps.y2 - 7)};
    lv_draw_rect(ctx, &r, &plein);
    if (g_charge)
    {
        lv_draw_label_dsc_t ld;
        lv_draw_label_dsc_init(&ld);
        ld.font = UI_POLICE_TITRE;
        ld.color = ui_c(UI_FOND);
        ld.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t z = {corps.x1, (lv_coord_t)(corps.y1 + 14), corps.x2, corps.y2};
        uc_texte(ctx, ld, &z, LV_SYMBOL_CHARGE, true);
    }
}

// carte batterie, posée (pas de flex : la page est fixe, sans défilement)
static void carte_batterie(lv_obj_t *f, lv_coord_t y)
{
    lv_obj_t *c = ui_conteneur(f);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 104);
    lv_obj_set_pos(c, UI_MARGE, y);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_set_style_pad_ver(c, 12, 0);
    lv_obj_t *pile = ui_conteneur(c);
    lv_obj_set_size(pile, 112, 52);
    lv_obj_set_pos(pile, 0, 0);
    lv_obj_add_event_cb(pile, pile_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_t *v;
    lv_obj_t *r = ui_valeur_unite(c, UI_POLICE_3X5_M, couleur_batterie(), "%", &v);
    ui_label_texte_fmt(v, "%d", POURCENT);
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_t *tension = ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, g_charge ? "4,12 V · monte" : "3,92 V");
    lv_obj_align(tension, LV_ALIGN_TOP_RIGHT, 0, 40);
    lv_obj_t *etat = ui_label(c, UI_POLICE_TEXTE, g_charge ? UI_VERT : UI_TEXTE, g_charge ? LV_SYMBOL_USB "  En charge" : "Sur batterie");
    lv_obj_set_pos(etat, 0, 62);
    lv_obj_t *depuis = ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, g_charge ? "branché depuis 35 min" : "débranchée depuis 1 j 19 h");
    lv_obj_align(depuis, LV_ALIGN_TOP_RIGHT, 0, 64);
}

// ---------------------------------------------------------------------------------------------------- éveils 24 h
// réveils fictifs de la journée (heure de début en h depuis -24 h, durée en min, cause) et batterie 92 -> 78 %
struct eveil_t
{
    float debut_h; // 0 = il y a 24 h
    float minutes;
    int cause; // 0 radar, 1 toucher, 2 minuterie (Wi-Fi absent)
};
static const eveil_t EVEILS[] = {{0.6f, 9, 0},  {1.4f, 3, 0},  {3.2f, 22, 1}, {4.0f, 6, 0},  {6.1f, 14, 0}, {8.9f, 2, 0},
                                 {11.3f, 1, 2}, {11.4f, 1, 2}, {15.7f, 2, 0}, {17.1f, 8, 0}, {17.6f, 19, 1}, {18.4f, 5, 0},
                                 {21.9f, 12, 0}, {22.6f, 4, 1}, {23.5f, 3, 0}};
static const uint32_t COUL_CAUSE[] = {UI_VERT, UI_ACCENT, 0x6B7480};
#define T_HEURE_FIN 14.5f // l'heure qu'il est (14h30)

static void eveils_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const lv_coord_t x0 = a.x1 + 40, x1 = a.x2 - 4, haut = a.y1 + 6, bas = a.y1 + 70, bande0 = bas + 6, bande1 = bas + 30;
    auto x = [&](float h) { return (lv_coord_t)(x0 + h / 24 * (x1 - x0)); };
    lv_draw_line_dsc_t gl;
    lv_draw_line_dsc_init(&gl);
    gl.color = ui_c(0x1F252D);
    gl.width = 1;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = UI_POLICE_LEGENDE;
    ld.color = ui_c(UI_TEXTE3);
    // échelle de la batterie (%, à gauche) : 0 / 50 / 100
    for (int v = 0; v <= 100; v += 50)
    {
        const lv_coord_t y = bas - (lv_coord_t)(v / 100.0f * (bas - haut));
        const lv_point_t p1 = {x0, y}, p2 = {x1, y};
        uc_segment(ctx, gl, p1.x, p1.y, p2.x, p2.y);
        char s[8];
        snprintf(s, sizeof s, "%d%%", v);
        lv_area_t z = {a.x1, (lv_coord_t)(y - 7), (lv_coord_t)(x0 - 4), (lv_coord_t)(y + 7)};
        ld.align = LV_TEXT_ALIGN_RIGHT;
        uc_texte(ctx, ld, &z, s, true);
    }
    // repères d'heures (toutes les 6 h, heures du jour, maintenant au bord droit)
    ld.align = LV_TEXT_ALIGN_CENTER;
    for (int h = 0; h < 24; h++)
    {
        const int heure = ((int)(T_HEURE_FIN + 0.5f) - 24 + h + 48) % 24;
        const float pos = h + (T_HEURE_FIN - (int)T_HEURE_FIN);
        if (heure % 6)
            continue;
        const lv_point_t p1 = {x(pos), haut}, p2 = {x(pos), bande1};
        uc_segment(ctx, gl, p1.x, p1.y, p2.x, p2.y);
        char s[8];
        snprintf(s, sizeof s, "%02dh", heure);
        lv_area_t z = {(lv_coord_t)(x(pos) - 18), (lv_coord_t)(bande1 + 4), (lv_coord_t)(x(pos) + 18), (lv_coord_t)(bande1 + 18)};
        uc_texte(ctx, ld, &z, s, true);
    }
    // bande des réveils : fond discret, un bloc par période allumée (couleur = cause)
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    d.bg_color = ui_c(0x161A20);
    lv_area_t fondb = {x0, bande0, x1, bande1};
    lv_draw_rect(ctx, &d, &fondb);
    d.radius = 2;
    for (const eveil_t &ev : EVEILS)
    {
        d.bg_color = ui_c(COUL_CAUSE[ev.cause]);
        lv_area_t z = {x(ev.debut_h), (lv_coord_t)(bande0 + 3), (lv_coord_t)LV_MAX(x(ev.debut_h) + 2, x(ev.debut_h + ev.minutes / 60)),
                       (lv_coord_t)(bande1 - 3)};
        lv_draw_rect(ctx, &d, &z);
    }
    // courbe de la batterie : descend surtout pendant les réveils (écran allumé), presque plate en veille
    lv_draw_line_dsc_t bl;
    lv_draw_line_dsc_init(&bl);
    bl.color = ui_c(UI_VERT);
    bl.width = 2;
    bl.round_start = bl.round_end = 1;
    float pct = 92;
    lv_point_t prec = {x0, (lv_coord_t)(bas - pct / 100 * (bas - haut))};
    for (int i = 1; i <= 96; i++)
    {
        const float h = i / 4.0f;
        float allume = 0;
        for (const eveil_t &ev : EVEILS)
            allume += LV_MAX(0.0f, LV_MIN(h, ev.debut_h + ev.minutes / 60) - LV_MAX(h - 0.25f, ev.debut_h));
        pct -= 0.06f + allume * 9.0f; // veille ~0,25 %/h, écran allumé ~9 %/h (fictif)
        const lv_point_t p = {x(h), (lv_coord_t)(bas - pct / 100 * (bas - haut))};
        uc_segment(ctx, bl, prec.x, prec.y, p.x, p.y);
        prec = p;
    }
}

static void carte_eveils(lv_obj_t *f, lv_coord_t y)
{
    lv_obj_t *t = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE2, "RÉVEILS · 24 H");
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_set_pos(t, UI_MARGE + 4, y);
    lv_obj_t *res = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE2, "15 réveils · allumé 2 h 10 en tout");
    lv_obj_align(res, LV_ALIGN_TOP_RIGHT, -UI_MARGE - 4, y);
    lv_obj_t *c = ui_conteneur(f);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 150);
    lv_obj_set_pos(c, UI_MARGE, y + 18);
    lv_obj_set_style_pad_all(c, 8, 0);
    lv_obj_t *g = ui_conteneur(c);
    lv_obj_set_size(g, lv_pct(100), 120);
    lv_obj_add_event_cb(g, eveils_cb, LV_EVENT_DRAW_MAIN, NULL);
    // légende : cause des réveils + batterie
    const char *noms[] = {"radar", "toucher", "minuterie", "batterie"};
    const uint32_t coul[] = {UI_VERT, UI_ACCENT, 0x6B7480, UI_VERT};
    lv_coord_t x = 40;
    for (int k = 0; k < 4; k++)
    {
        lv_obj_t *p = ui_conteneur(c);
        lv_obj_set_size(p, k == 3 ? 14 : 10, k == 3 ? 2 : 10);
        lv_obj_set_style_radius(p, 2, 0);
        ui_style_bg_color(p, ui_c(coul[k]), 0);
        ui_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_pos(p, x, k == 3 ? 128 : 124);
        lv_obj_t *l = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, noms[k]);
        lv_obj_set_pos(l, x + (k == 3 ? 18 : 14), 121);
        x += (k == 3 ? 18 : 14) + largeur_texte(noms[k], UI_POLICE_LEGENDE) + 14;
    }
}

// ---------------------------------------------------------------------------------------------------- dernier réveil
static void carte_demarrage(lv_obj_t *f, lv_coord_t y)
{
    lv_obj_t *c = ui_conteneur(f);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 58);
    lv_obj_set_pos(c, UI_MARGE, y);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 0, 0);
    lv_obj_align(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "dernier réveil\nradar, 14h32"), LV_ALIGN_LEFT_MID, 0, 0);
    const char *val[] = {"0.17", "0.16", "1.39"}, *nom[] = {"Wi-Fi", "MQTT", "écran"};
    const uint32_t coul[] = {UI_FROID, UI_FROID, UI_ACCENT};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *col = ui_conteneur(c);
        lv_obj_set_size(col, 70, 48);
        lv_obj_align(col, LV_ALIGN_RIGHT_MID, -(2 - i) * 76, 0);
        lv_obj_t *v;
        lv_obj_t *r = ui_valeur_unite(col, UI_POLICE_3X5_P, coul[i], "s", &v);
        ui_label_texte(v, val[i]);
        lv_obj_align(r, LV_ALIGN_TOP_MID, 0, 2);
        lv_obj_align(ui_label(col, UI_POLICE_LEGENDE, UI_TEXTE2, nom[i]), LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

lv_obj_t *maquette_page_batterie(bool en_charge)
{
    g_charge = en_charge;
    lv_obj_t *f = fond("Batterie et capteur");
    carte_batterie(f, 48);
    carte_eveils(f, 162);
    carte_demarrage(f, 340);
    lv_obj_t *b = ui_bouton(f, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(b, UI_MARGE, 410);
    lv_obj_t *l = ui_label(b, UI_POLICE_TEXTE, UI_TEXTE, "Réglages du radar");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_align(ui_label(b, UI_POLICE_TEXTE, UI_TEXTE2, LV_SYMBOL_RIGHT), LV_ALIGN_RIGHT_MID, -4, 0);
    return f;
}

// ---------------------------------------------------------------------------------------------------- page Radar
// seuils de la production (radar.h, 26/10) : déclenchement / maintien des portes 0-7 puis 8-15 ; signal : quelqu'un à 2,40 m
static const uint8_t DECL[16] = {75, 65, 40, 30, 25, 25, 20, 20, 51, 50, 30, 28, 25, 25, 25, 25};
static const uint8_t MAINT[16] = {20, 20, 20, 20, 20, 20, 15, 14, 25, 25, 25, 25, 25, 22, 22, 22};
static const uint8_t SIGNAL[16] = {38, 30, 34, 62, 22, 16, 12, 10, 9, 8, 7, 6, 6, 5, 5, 5};
static int g_onglet, g_zone;
static const float DISTANCE = 2.40f; // m : la présence vue par le radar

#define G_X 34
#define G_Y 104
#define G_H 200
static void graphe_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const uint8_t *seuil = g_onglet ? MAINT : DECL;
    const lv_coord_t x0 = a.x1 + G_X, x1 = a.x2 - 6, bas = a.y1 + G_H, pas = (x1 - x0) / 16;
    auto y = [&](int v) { return (lv_coord_t)(bas - v * (G_H - 10) / 100); }; // 10 px en haut pour l'étiquette « 100 »
    // grille 0 / 25 / 50 / 75 / 100
    lv_draw_line_dsc_t gl;
    lv_draw_line_dsc_init(&gl);
    gl.color = ui_c(0x1F252D);
    gl.width = 1;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = UI_POLICE_LEGENDE;
    ld.color = ui_c(UI_TEXTE3);
    for (int v = 0; v <= 100; v += 25)
    {
        const lv_point_t p1 = {x0, y(v)}, p2 = {x1, y(v)};
        uc_segment(ctx, gl, p1.x, p1.y, p2.x, p2.y);
        char s[8];
        snprintf(s, sizeof s, "%d", v);
        lv_area_t z = {a.x1, (lv_coord_t)(y(v) - 7), (lv_coord_t)(x0 - 6), (lv_coord_t)(y(v) + 7)};
        ld.align = LV_TEXT_ALIGN_RIGHT;
        uc_texte(ctx, ld, &z, s, true);
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    for (int i = 0; i < 16; i++)
    {
        const lv_coord_t bx = x0 + i * pas + 3, bx2 = x0 + (i + 1) * pas - 3;
        // seuil : colonne (ambre la zone choisie, grise les autres)
        d.bg_color = ui_c(i == g_zone ? UI_ACCENT : 0x3A424D);
        d.bg_opa = i == g_zone ? LV_OPA_COVER : LV_OPA_80;
        lv_area_t z = {bx, y(seuil[i]), bx2, bas};
        lv_draw_rect(ctx, &d, &z);
        // signal mesuré : trait cyan en travers (au-dessus du seuil = déclenche)
        lv_draw_rect_dsc_t s;
        lv_draw_rect_dsc_init(&s);
        s.radius = 2;
        s.bg_color = ui_c(SIGNAL[i] >= seuil[i] ? UI_VERT : UI_FROID);
        lv_area_t zs = {(lv_coord_t)(bx - 2), (lv_coord_t)(y(SIGNAL[i]) - 2), (lv_coord_t)(bx2 + 2), (lv_coord_t)(y(SIGNAL[i]) + 2)};
        lv_draw_rect(ctx, &s, &zs);
    }
    // distances sous l'axe
    ld.align = LV_TEXT_ALIGN_CENTER;
    for (int m = 0; m <= 12; m += 3)
    {
        char s[8];
        snprintf(s, sizeof s, "%d m", m);
        const lv_coord_t xm = x0 + m * pas * 16 / 12;
        lv_area_t z = {(lv_coord_t)(xm - 20), (lv_coord_t)(bas + 6), (lv_coord_t)(xm + 20), (lv_coord_t)(bas + 20)};
        uc_texte(ctx, ld, &z, s, true);
    }
    // présence : petit triangle sous l'axe à la distance mesurée
    lv_draw_rect_dsc_t pr;
    lv_draw_rect_dsc_init(&pr);
    pr.radius = LV_RADIUS_CIRCLE;
    pr.bg_color = ui_c(UI_VERT);
    const lv_coord_t xp = x0 + (lv_coord_t)(DISTANCE / 12 * pas * 16);
    lv_area_t zp = {(lv_coord_t)(xp - 5), (lv_coord_t)(bas - 5), (lv_coord_t)(xp + 5), (lv_coord_t)(bas + 5)};
    lv_draw_rect(ctx, &pr, &zp);
}

static void onglet_cb(lv_event_t *) {}

lv_obj_t *maquette_radar(int onglet, int zone)
{
    g_onglet = onglet;
    g_zone = zone;
    lv_obj_t *f = fond(nullptr);
    static const char *const NOMS[] = {"Détection", "Maintien"};
    ui_onglets(f, NOMS, 2, onglet, onglet_cb, nullptr, nullptr);
    // en direct : présence et distance ; légende
    lv_obj_t *pt = ui_conteneur(f);
    lv_obj_set_size(pt, 10, 10);
    lv_obj_set_style_radius(pt, LV_RADIUS_CIRCLE, 0);
    ui_style_bg_color(pt, ui_c(UI_VERT), 0);
    ui_style_bg_opa(pt, LV_OPA_COVER, 0);
    lv_obj_set_pos(pt, UI_MARGE + 2, 62);
    lv_obj_set_pos(ui_label(f, UI_POLICE_TEXTE, UI_TEXTE, "Présence à 2,40 m"), UI_MARGE + 18, 56);
    lv_obj_t *aide = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE2, onglet ? "seuil pour rester « présent »" : "seuil pour détecter quelqu'un");
    lv_obj_align(aide, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 60);
    // légende : seuil (colonne) / signal (trait)
    lv_obj_t *lg = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE3, "colonne = seuil · trait = signal mesuré (vert : au-dessus, détecte)");
    lv_obj_set_pos(lg, UI_MARGE, 84);
    lv_obj_t *g = ui_conteneur(f);
    lv_obj_set_size(g, UI_ZONE_L, G_H + 24);
    lv_obj_set_pos(g, 0, G_Y);
    lv_obj_add_event_cb(g, graphe_cb, LV_EVENT_DRAW_MAIN, NULL);

    // zone choisie : − valeur +
    lv_obj_t *c = ui_conteneur(f);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 72);
    lv_obj_set_pos(c, UI_MARGE, 336);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 0, 0);
    char s[48];
    snprintf(s, sizeof s, "Zone %d", zone);
    lv_obj_align(ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, s), LV_ALIGN_TOP_LEFT, 0, 12);
    snprintf(s, sizeof s, "%.2f – %.2f m", zone * 0.75, (zone + 1) * 0.75);
    for (char *q = s; *q; q++)
        if (*q == '.')
            *q = ',';
    lv_obj_align(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, s), LV_ALIGN_TOP_LEFT, 64, 15);
    snprintf(s, sizeof s, "signal %d · d'origine %d", SIGNAL[zone], onglet ? 31 : 32);
    lv_obj_align(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE3, s), LV_ALIGN_BOTTOM_LEFT, 0, -12);
    lv_obj_t *plus = ui_bouton(c, 44, 44);
    lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(ui_label(plus, UI_POLICE_TITRE, UI_TEXTE, "+"));
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *v;
    lv_obj_t *r = ui_valeur_unite(c, UI_POLICE_3X5_P, UI_ACCENT, "", &v);
    ui_label_texte_fmt(v, "%d", (onglet ? MAINT : DECL)[zone]);
    lv_obj_align(r, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_obj_t *moins = ui_bouton(c, 44, 44);
    lv_obj_set_style_radius(moins, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(ui_label(moins, UI_POLICE_TITRE, UI_TEXTE, "−"));
    lv_obj_align(moins, LV_ALIGN_RIGHT_MID, -116, 0);

    // actions
    const char *act[] = {"Lire le radar", "Envoyer au radar", "Réglage auto"};
    const lv_coord_t l = (UI_ZONE_L - 2 * UI_MARGE - 16) / 3;
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *b = ui_bouton(f, l, 52);
        lv_obj_set_pos(b, UI_MARGE + i * (l + 8), 416);
        if (i == 1)
            ui_style_ajouter(b, &ui_st_bouton_principal, 0);
        lv_obj_center(ui_label(b, UI_POLICE_PETITE, i == 1 ? UI_SUR_ACCENT : UI_TEXTE, act[i]));
    }
    return f;
}
