#include "page_poele.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "ui_theme.h"
#include "ui_anneau.h"
#include "ui_courbe.h"
#include "icones_ventilo_rgb.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const pl_etat_t pl_etat_depart = {0, 0, 100, 0, 60, 0, PL_ETEINT, {240, 30, 180, 3.0f, 0.01f, 10.0f, 30}, {}, {}, {}, -1}; // valeur de départ (en flash)
UI_PSRAM(pl_etat_t, pl_etat_p, 1, &pl_etat_depart); // 29/10 : en PSRAM (ui_psram.h)
void (*pl_commande)(const char *quoi) = nullptr;

#define FUMEES_MAX 500
#define CORPS_MAX 180
#define ANNEAU_D 290
#define ANNEAU_X 57
#define ANNEAU_Y 50
#define EXT_L 23 // anneau des fumées
#define INT_L 9  // anneau du corps

static lv_obj_t *g_zone = nullptr;
static int g_vue = 0;     // 0 Feu, 1 Courbe, 2 Réglages
static int g_echelle = 1; // courbe : 1 h / 3 h / 12 h

// Feu
static lv_obj_t *o_onglets[3], *o_anneau, *o_fumees, *o_corps, *o_puce, *o_puce_point, *o_puce_txt;
static lv_obj_t *o_ventilo, *o_vpoints[3], *o_tir_qui, *o_tir_val, *o_tir, *o_tir_mini, *o_modes[3];
static int g_vent_anime = -1;
// Courbe
static lv_obj_t *o_graphe, *o_tir_bande, *o_tir_txt, *o_cval, *o_cmin, *o_cmax, *o_echelles[3], *o_couches[4], *o_bulle, *o_bulle_txt;
static bool g_couches[4] = {true, true, true, true}; // consigne, poêle, recharge, tirage
static int g_scrub = -1;                            // point lu au doigt (-1 = aucun)
// Réglages
static lv_obj_t *o_vals[7];

static void construire();
static void envoyer(const char *quoi)
{
    if (pl_commande)
        pl_commande(quoi);
}

// =====================================================================  FEU
static void anneau_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_point_t c = {(lv_coord_t)(a.x1 + lv_area_get_width(&a) / 2), (lv_coord_t)(a.y1 + lv_area_get_height(&a) / 2)};
    // fumées : le dégradé de l'anneau commun, assombri au-delà de la valeur
    ui_anneau_dessiner(ctx, &c, ANNEAU_D, EXT_L, (float)pl_etat.fumees / FUMEES_MAX, true); // la pastille d'état est dans l'ouverture

    // corps : anneau fin intérieur, orange jusqu'à la valeur
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.width = INT_L;
    d.rounded = 0;
    const lv_coord_t r = ANNEAU_D / 2 - EXT_L - 6;
    // 25/10 (retour d'usage : « dégradé aussi pour la température du corps ») : comme l'anneau extérieur, les 10 derniers degrés de
    // chaque bout s'estompent vers le fond ; orange jusqu'à la valeur, gris au-delà (segments de 2°)
    const int fin = LV_CLAMP(0, pl_etat.corps, CORPS_MAX) * 270 / CORPS_MAX; // degrés depuis le début (135°)
    const int NB = 135;
    for (int k = 0; k < NB; k++)
    {
        lv_color_t col = ui_c(k * 2 < fin ? UI_CHAUFFE : UI_CARTE2);
        if (k < 5 || k >= NB - 5)
        {
            const int bord = k < 5 ? k : NB - 1 - k;
            col = lv_color_mix(col, ui_c(UI_FOND), (uint8_t)(255 * (bord + 0.5f) / 5));
        }
        d.color = col;
        const int deb = 135 + k * 2;
        uc_arc(ctx, d, &c, r, deb % 360, (deb + (k == NB - 1 ? 2 : 3)) % 360); // 1° de recouvrement contre les fentes
    }

    // repère de consigne : trait blanc en travers de l'anneau des fumées
    float ang = (135 + (float)pl_etat.r.consigne / FUMEES_MAX * 270) * 3.14159265f / 180;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = ui_c(UI_POIGNEE);
    l.width = 4;
    l.round_start = l.round_end = 1;
    lv_coord_t r1 = ANNEAU_D / 2 - EXT_L - 3, r2 = ANNEAU_D / 2 + 3;
    lv_point_t p1 = {(lv_coord_t)(c.x + r1 * cosf(ang)), (lv_coord_t)(c.y + r1 * sinf(ang))};
    lv_point_t p2 = {(lv_coord_t)(c.x + r2 * cosf(ang)), (lv_coord_t)(c.y + r2 * sinf(ang))};
    uc_segment(ctx, l, p1.x, p1.y, p2.x, p2.y);
}

static void animer_ventilo(int niveau)
{
    if (g_vent_anime == niveau)
        return;
    g_vent_anime = niveau;
    static const uint32_t TOUR[] = {0, 2400, 1300, 700};
    ui_helice_tourner(o_ventilo, TOUR[niveau]);
}

static void rafraichir_feu()
{
    const pl_etat_t &p = pl_etat;
    // 27/10 (audit des redessins) : l'anneau (290 px) seulement si ce qu'il montre change : fumées, corps, consigne
    static uint32_t anneau_dessine = 0;
    const int montre[] = {p.fumees, p.corps, p.r.consigne};
    const uint32_t e = ui_empreinte(montre, sizeof montre);
    if (e != anneau_dessine)
    {
        anneau_dessine = e;
        lv_obj_invalidate(o_anneau);
    }
    ui_label_texte_fmt(o_fumees, "%d", p.fumees);
    ui_style_text_color(o_fumees, ui_c(p.feu == PL_RECHARGER ? 0xFF8A7A : p.feu == PL_ETEINT ? UI_TEXTE2 : UI_TEXTE), 0);
    ui_label_texte_fmt(o_corps, TXT("poêle %d°", "stove %d°"), p.corps);

    // pastille d'état dans l'ouverture du bas
    uint32_t point = UI_TEXTE3, fond = UI_CARTE, bord = UI_BORD, texte = UI_TEXTE;
    char t[48];
    switch (p.feu)
    {
    case PL_ETEINT:
        snprintf(t, sizeof t, TXT("Feu éteint · volet ouvert", "Fire off · damper open"));
        texte = UI_TEXTE2;
        break;
    case PL_ALLUMAGE:
        snprintf(t, sizeof t, TXT("Allumage", "Ignition"));
        point = UI_ACCENT;
        break;
    case PL_REGULATION:
        snprintf(t, sizeof t, p.mode ? TXT("Régulation coupée", "Control off")
                          : TXT("Régulation · consigne %d°", "Control · setpoint %d°"), p.r.consigne);
        point = p.mode ? UI_TEXTE3 : UI_VERT;
        break;
    case PL_RECHARGER:
        snprintf(t, sizeof t, TXT("Recharger le poêle", "Refill stove"));
        point = UI_ROUGE;
        fond = 0x2A1717;
        bord = 0x5A2A26;
        texte = 0xFF8A7A;
        break;
    }
    ui_label_texte(o_puce_txt, t);
    ui_style_text_color(o_puce_txt, ui_c(texte), 0);
    ui_style_bg_color(o_puce, ui_c(fond), 0);
    ui_style_border_color(o_puce, ui_c(bord), 0);
    ui_style_bg_color(o_puce_point, ui_c(point), 0);

    // brassage
    ui_style_img_recolor(o_ventilo, ui_c(p.brassage ? UI_TEXTE : UI_TEXTE3), 0);
    animer_ventilo(p.brassage);
    for (int i = 0; i < 3; i++)
        ui_style_bg_color(o_vpoints[i], ui_c(i < p.brassage ? UI_FROID : UI_BORD), 0);

    // tirage
    const bool pid = p.fumees > 100 && p.mode == 0;
    const int tir = p.mode == 2 ? 100 : p.mode == 1 ? p.tirage_manuel : (pid ? p.tirage : 100);
    ui_label_texte(o_tir_qui, p.mode == 0 ? (pid ? "PID" : TXT("PID arrêté, ouvert", "PID off, damper open"))
                    : p.mode == 1 ? TXT("glisser pour régler", "drag to adjust")
                                  : TXT("au levier du poêle", "on the stove lever"));
    ui_label_texte_fmt(o_tir_val, "%d%%", tir);
    uint32_t c = p.mode == 1 ? UI_ACCENT : p.mode == 0 ? UI_FROID : UI_TEXTE3;
    ui_style_text_color(o_tir_val, ui_c(c), 0);
    ui_style_bg_color(o_tir, ui_c(c), LV_PART_INDICATOR);
    if (p.mode == 1)
        lv_obj_clear_state(o_tir, LV_STATE_DISABLED);
    else
        lv_obj_add_state(o_tir, LV_STATE_DISABLED);
    lv_slider_set_value(o_tir, tir, LV_ANIM_OFF);
    lv_obj_set_x(o_tir_mini, (lv_coord_t)(p.r.tirage_min * (lv_obj_get_width(o_tir) - 2) / 100));

    for (int m = 0; m < 3; m++)
        if (m == p.mode)
            lv_obj_add_state(o_modes[m], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_modes[m], LV_STATE_CHECKED);
}

static void brassage_cb(lv_event_t *)
{
    pl_etat.brassage = (pl_etat.brassage + 1) % 4;
    rafraichir_feu();
    envoyer("brassage");
}

static void tirage_cb(lv_event_t *e)
{
    pl_etat.tirage_manuel = lv_slider_get_value(o_tir);
    rafraichir_feu();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED)
        envoyer("tirage");
}

static void mode_cb(lv_event_t *e)
{
    pl_etat.mode = (int)(intptr_t)lv_event_get_user_data(e);
    rafraichir_feu();
    envoyer("mode");
}

static void construire_feu(lv_obj_t *z)
{
    o_anneau = ui_conteneur(z);
    lv_obj_set_size(o_anneau, ANNEAU_D, ANNEAU_D);
    lv_obj_set_pos(o_anneau, ANNEAU_X, ANNEAU_Y);
    lv_obj_clear_flag(o_anneau, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_anneau, anneau_cb, LV_EVENT_DRAW_MAIN, NULL);

    // 25/10 (retour d'usage : « police trop petite au centre de la vue Feu ») : fumées en 99 comme le thermostat (49 avant),
    // libellés en 16 et 20 (14 et 16 avant)
    lv_obj_t *centre = ui_conteneur(z);
    lv_obj_set_size(centre, 220, 170);
    lv_obj_set_pos(centre, ANNEAU_X + (ANNEAU_D - 220) / 2, ANNEAU_Y + (ANNEAU_D - 170) / 2);
    lv_obj_clear_flag(centre, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(centre, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(centre, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(centre, 4, 0);
    ui_label(centre, UI_POLICE_TEXTE, UI_TEXTE2, TXT("fumées", "flue"));
    ui_valeur_unite(centre, UI_POLICE_TEMP, UI_TEXTE, "°C", &o_fumees); // le « ° » de la police pixel : un gros carré en 99
    o_corps = ui_label(centre, UI_POLICE_TITRE, UI_CHAUFFE, "");

    o_puce = ui_conteneur(z);
    lv_obj_set_size(o_puce, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_radius(o_puce, 15, 0);
    ui_style_bg_opa(o_puce, LV_OPA_COVER, 0);
    ui_style_border_width(o_puce, 1, 0);
    lv_obj_set_style_pad_hor(o_puce, 14, 0);
    lv_obj_set_flex_flow(o_puce, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o_puce, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o_puce, 7, 0);
    o_puce_point = ui_conteneur(o_puce);
    lv_obj_set_size(o_puce_point, 8, 8);
    lv_obj_set_style_radius(o_puce_point, LV_RADIUS_CIRCLE, 0);
    ui_style_bg_opa(o_puce_point, LV_OPA_COVER, 0);
    o_puce_txt = ui_label(o_puce, UI_POLICE_PETITE, UI_TEXTE, "");
    lv_obj_align(o_puce, LV_ALIGN_TOP_MID, 0, ANNEAU_Y + ANNEAU_D - 36);

    // brassage : case à gauche de la ligne du tirage, un tap = niveau suivant
    lv_obj_t *vb = ui_bouton(z, 72, 58);
    lv_obj_set_pos(vb, UI_MARGE, 346);
    o_ventilo = ui_icone(vb, &icon_ventilo_rgb, UI_TEXTE);
    lv_img_set_pivot(o_ventilo, 28, 28);
    lv_img_set_zoom(o_ventilo, 155); // 56 -> 34 px
    lv_obj_align(o_ventilo, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *pts = ui_conteneur(vb);
    lv_obj_set_size(pts, LV_SIZE_CONTENT, 6);
    lv_obj_set_flex_flow(pts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pts, 4, 0);
    lv_obj_clear_flag(pts, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(pts, LV_ALIGN_BOTTOM_MID, 0, -7);
    for (int i = 0; i < 3; i++)
    {
        o_vpoints[i] = ui_conteneur(pts);
        lv_obj_set_size(o_vpoints[i], 6, 6);
        lv_obj_set_style_radius(o_vpoints[i], LV_RADIUS_CIRCLE, 0);
        ui_style_bg_opa(o_vpoints[i], LV_OPA_COVER, 0);
    }
    lv_obj_add_event_cb(vb, brassage_cb, LV_EVENT_CLICKED, NULL);

    // tirage
    lv_obj_t *c = ui_conteneur(z);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE - 80, 58);
    lv_obj_set_pos(c, UI_MARGE + 80, 346);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 6, 0);
    lv_obj_t *l = ui_label(c, UI_POLICE_PETITE, UI_TEXTE, TXT("Tirage", "Draft"));
    lv_obj_set_pos(l, 0, 0);
    o_tir_qui = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_pos(o_tir_qui, 56, 2);
    o_tir_val = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "");
    lv_obj_align(o_tir_val, LV_ALIGN_TOP_RIGHT, 0, -2);
    o_tir = lv_slider_create(c);
    lv_obj_set_size(o_tir, lv_pct(100), 6);
    lv_obj_align(o_tir, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_slider_set_range(o_tir, 0, 100);
    ui_style_bg_color(o_tir, ui_c(UI_CARTE2), LV_PART_MAIN);
    // 25/10 (retour d'usage : « tirage manu, une couleur plus foncée pour la partie droite ») : en Manuel la piste était
    // transparente (couleur de la carte) ; une piste foncée, plus sombre que la carte
    ui_style_bg_color(o_tir, ui_c(UI_FOND), LV_PART_MAIN);
    ui_style_bg_opa(o_tir, LV_OPA_COVER, LV_PART_MAIN);
    ui_style_bg_color(o_tir, ui_c(UI_POIGNEE), LV_PART_KNOB);
    lv_obj_set_style_pad_all(o_tir, 6, LV_PART_KNOB);
    ui_style_bg_opa(o_tir, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DISABLED); // pas de poignée hors Manuel
    ui_style_bg_opa(o_tir, LV_OPA_COVER, LV_STATE_DISABLED);
    ui_style_bg_opa(o_tir, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(o_tir, 14);
    ui_curseur_dans_page(o_tir); // avant tirage_cb : seul un glissé horizontal règle (et envoie) le tirage
    lv_obj_add_event_cb(o_tir, tirage_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(o_tir, tirage_cb, LV_EVENT_RELEASED, NULL);
    o_tir_mini = ui_conteneur(o_tir); // repère du tirage mini
    lv_obj_set_size(o_tir_mini, 2, 6);
    ui_style_bg_color(o_tir_mini, ui_c(UI_TEXTE3), 0);
    ui_style_bg_opa(o_tir_mini, LV_OPA_COVER, 0);
    lv_obj_add_flag(o_tir_mini, LV_OBJ_FLAG_IGNORE_LAYOUT);

    // modes
    lv_obj_t *rang = ui_conteneur(z);
    lv_obj_set_size(rang, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(rang, UI_MARGE, 412);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *noms[] = {TXT("Auto", "Auto"), TXT("Manuel", "Manual"), TXT("Levier", "Lever")},
               *sous[] = {"PID", TXT("à distance", "remote"), TXT("volet à 100 %", "damper at 100%")};
    for (int m = 0; m < 3; m++)
    {
        lv_obj_t *b = ui_bouton(rang, 121, 52);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label_texte(lv_label_create(b), noms[m]);
        lv_obj_t *s = lv_label_create(b);
        ui_label_texte(s, sous[m]);
        ui_style_text_font(s, UI_POLICE_LEGENDE, 0);
        o_modes[m] = b;
        lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)m);
    }
}

// =====================================================================  COURBE
// 24/10 (retour d'usage : « calqué sur le chauffage, même place même taille pour tout ») : la page Graphe des pièces à l'identique
// (valeur en haut à gauche, min / max à droite, courbe, bande du bas, pastilles), module commun ui_courbe ; fumées =
// valeur principale, poêle = trait secondaire, consigne / recharge = pointillés, tirage = bande du bas (dégradé).
#define G_Y 122 // objet courbe (mêmes cotes que page_graphe)
#define G_H 220
#define T_X0 44
#define T_X1 396
#define T_Y0 10
#define T_H 186
#define H_Y 348 // bande du tirage
#define H_H 52
enum { C_CONSIGNE, C_POELE, C_RECHARGE, C_TIRAGE };
static const int NB_POINTS[] = {12, 36, 144}; // points montrés : 1 h, 3 h, 12 h
static const int ECART_H[] = {15, 60, 180};   // repères d'heures : toutes les 15 min / 1 h / 3 h

static uc_cadre_t cadre_courbe(uc_cible_t *ctx, lv_obj_t *o, lv_coord_t y0, lv_coord_t h, float lo, float hi)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int n = NB_POINTS[g_echelle];
    uc_cadre_t c = {ctx, (lv_coord_t)(a.x1 + T_X0), (lv_coord_t)(a.x1 + T_X1), (lv_coord_t)(a.y1 + y0), (lv_coord_t)(a.y1 + y0 + h),
                    PL_HIST - n, n, lo, hi};
    return c;
}

static void graphe_cb(lv_event_t *e)
{
    uc_cadre_t c = cadre_courbe(uc_cible_du(e), o_graphe, T_Y0, T_H, 0, 400);
    const uc_serie_t fumees = uc_serie(pl_etat.h_fumees, PL_VIDE), corps = uc_serie(pl_etat.h_corps, PL_VIDE);
    uc_grille(c, 100, "°");
    uc_heures(c, pl_etat.h_fin, 5, ECART_H[g_echelle]);
    uc_degrade(c, fumees, UI_ACCENT);
    if (g_couches[C_RECHARGE])
        uc_niveau(c, pl_etat.r.recharge, UI_ROUGE, true);
    if (g_couches[C_CONSIGNE])
        uc_niveau(c, pl_etat.r.consigne, UI_TEXTE, false);
    if (g_couches[C_POELE])
        uc_trait(c, corps, UI_CHAUFFE, 2);
    uc_principale(c, fumees, UI_ACCENT);
    if (g_scrub >= 0)
        uc_curseur(c, g_scrub, fumees, UI_ACCENT);
}

static void bande_tirage_cb(lv_event_t *e)
{
    uc_cadre_t c = cadre_courbe(uc_cible_du(e), o_tir_bande, 10, H_H - 12, 0, 100);
    const uc_serie_t tir = uc_serie(pl_etat.h_tirage, PL_VIDE_TIRAGE);
    uc_degrade(c, tir, UI_FROID);
    uc_trait(c, tir, UI_FROID, 2);
    if (g_scrub >= 0)
        uc_curseur(c, g_scrub, tir, UI_FROID);
}

static void rafraichir_courbe()
{
    const int n = NB_POINTS[g_echelle], deb = PL_HIST - n;
    int i = g_scrub >= 0 ? g_scrub : PL_HIST - 1;
    char h[24], b[160];
    // valeur : maintenant (blanc) ou au doigt (ambre), comme le Graphe
    if (pl_etat.h_fumees[i] != PL_VIDE)
        ui_label_texte_fmt(o_cval, "%d°", pl_etat.h_fumees[i]);
    else
        ui_label_texte_fmt(o_cval, "%d°", pl_etat.fumees);
    ui_style_text_color(o_cval, ui_c(g_scrub >= 0 ? UI_ACCENT : UI_TEXTE), 0);
    // 25/10 : plus d'heure à côté de la valeur pendant la lecture au doigt (doublon de la bulle), comme le graphe
    // min / max sur ce qui est montré
    int imin, imax;
    uc_min_max(pl_etat.h_fumees, deb, PL_HIST - deb, PL_VIDE, &imin, &imax);
    if (imin >= 0)
    {
        uc_texte_heure(h, sizeof h, imin, PL_HIST - 1, pl_etat.h_fin, 5);
        ui_label_texte_fmt(o_cmin, TXT("min %d° à %s", "min %d° at %s"), pl_etat.h_fumees[imin], h);
        uc_texte_heure(h, sizeof h, imax, PL_HIST - 1, pl_etat.h_fin, 5);
        ui_label_texte_fmt(o_cmax, TXT("max %d° à %s", "max %d° at %s"), pl_etat.h_fumees[imax], h);
    }
    else
    {
        ui_label_texte(o_cmin, "");
        ui_label_texte(o_cmax, "");
    }
    ui_label_texte_fmt(o_tir_txt, pl_etat.h_tirage[i] != PL_VIDE_TIRAGE ? TXT("Tirage %d %%", "Draft %d %%") : TXT("Tirage", "Draft"), pl_etat.h_tirage[i]);
    for (int k = 0; k < 3; k++)
        if (k == g_echelle)
            lv_obj_add_state(o_echelles[k], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_echelles[k], LV_STATE_CHECKED);
    for (int k = 0; k < 4; k++)
        uc_pastille_etat(o_couches[k], g_couches[k]);
    ui_cacher(o_tir_bande, !g_couches[C_TIRAGE]);
    ui_cacher(o_tir_txt, !g_couches[C_TIRAGE]);
    if (g_scrub >= 0) // bulle : l'heure et les valeurs du point lu
    {
        char f[8] = "--", p[8] = "--";
        if (pl_etat.h_fumees[i] != PL_VIDE)
            snprintf(f, sizeof f, "%d", pl_etat.h_fumees[i]);
        if (pl_etat.h_corps[i] != PL_VIDE)
            snprintf(p, sizeof p, "%d", pl_etat.h_corps[i]);
        uc_texte_heure(h, sizeof h, i, PL_HIST - 1, pl_etat.h_fin, 5);
        snprintf(b, sizeof b, TXT("%s\n" UI_MARK_ACCENT " Fumées %s°#\nPoêle %s°\nConsigne %d°", "%s\n" UI_MARK_ACCENT " Flue %s°#\nStove %s°\nSetpoint %d°"), h, f, p, pl_etat.r.consigne);
        uc_bulle_ecrire(o_bulle, o_bulle_txt, b, cadre_courbe(nullptr, o_graphe, T_Y0, T_H, 0, 400), i, g_zone, G_Y + 4, UI_ZONE_L);
    }
    else
        ui_cacher(o_bulle, true);
    // 27/10 (audit des redessins) : la courbe et la bande du tirage seulement si ce qu'elles lisent change
    static uint32_t courbe_dessinee = 0;
    uint32_t e = ui_empreinte(pl_etat.h_fumees, sizeof pl_etat.h_fumees);
    e = ui_empreinte(pl_etat.h_corps, sizeof pl_etat.h_corps, e);
    e = ui_empreinte(pl_etat.h_tirage, sizeof pl_etat.h_tirage, e);
    e = ui_empreinte(&pl_etat.h_fin, sizeof pl_etat.h_fin, e);
    e = ui_empreinte(&pl_etat.r, sizeof pl_etat.r, e);
    const int vue[] = {g_echelle, g_scrub, g_couches[0], g_couches[1], g_couches[2], g_couches[3]};
    e = ui_empreinte(vue, sizeof vue, e);
    if (e != courbe_dessinee)
    {
        courbe_dessinee = e;
        lv_obj_invalidate(o_graphe);
        lv_obj_invalidate(o_tir_bande);
    }
}

// Chemin léger du glissé de lecture : ne repeint que les deux colonnes du curseur (avant/après) et
// l'ancienne place de la bulle (9.6 : lv_obj_set_pos n'invalidite que la nouvelle position).
static void rafraichir_scrub(int i_avant)
{
    const int i = g_scrub;
    if (pl_etat.h_fumees[i] != PL_VIDE)
        ui_label_texte_fmt(o_cval, "%d°", pl_etat.h_fumees[i]);
    else
        ui_label_texte_fmt(o_cval, "%d°", pl_etat.fumees);
    ui_style_text_color(o_cval, ui_c(UI_ACCENT), 0);
    ui_label_texte_fmt(o_tir_txt, pl_etat.h_tirage[i] != PL_VIDE_TIRAGE ? TXT("Tirage %d %%", "Draft %d %%") : TXT("Tirage", "Draft"), pl_etat.h_tirage[i]);
    if (g_scrub >= 0) // bulle : l'heure et les valeurs du point lu (mêmes textes que rafraichir_courbe)
    {
        char h[24], b[160], f[8] = "--", p[8] = "--";
        if (pl_etat.h_fumees[i] != PL_VIDE)
            snprintf(f, sizeof f, "%d", pl_etat.h_fumees[i]);
        if (pl_etat.h_corps[i] != PL_VIDE)
            snprintf(p, sizeof p, "%d", pl_etat.h_corps[i]);
        uc_texte_heure(h, sizeof h, i, PL_HIST - 1, pl_etat.h_fin, 5);
        snprintf(b, sizeof b, TXT("%s\n" UI_MARK_ACCENT " Fumées %s°#\nPoêle %s°\nConsigne %d°", "%s\n" UI_MARK_ACCENT " Flue %s°#\nStove %s°\nSetpoint %d°"), h, f, p, pl_etat.r.consigne);
        uc_bulle_poser(o_bulle, o_bulle_txt, b, cadre_courbe(nullptr, o_graphe, T_Y0, T_H, 0, 400), i, g_zone, G_Y + 4, UI_ZONE_L, o_graphe);
    }
    uc_repeindre_bandes(cadre_courbe(nullptr, o_graphe, T_Y0, T_H, 0, 400), o_graphe, i_avant, i);
    uc_repeindre_bandes(cadre_courbe(nullptr, o_tir_bande, 10, H_H - 12, 0, 100), o_tir_bande, i_avant, i); // même cadre que bande_tirage_cb
}

static void echelle_cb(lv_event_t *e)
{
    g_echelle = (int)(intptr_t)lv_event_get_user_data(e);
    g_scrub = -1;
    rafraichir_courbe();
}

static void couche_cb(lv_event_t *e)
{
    const int k = (int)(intptr_t)lv_event_get_user_data(e);
    g_couches[k] = !g_couches[k];
    rafraichir_courbe();
}

static void doigt_cb(lv_event_t *e)
{
    // 06/11 (chantier 2) : premier appui -> chemin complet ; curseur déjà posé -> léger (colonnes + bulle).
    uc_lecture_doigt(e, { &g_scrub, [] { return cadre_courbe(nullptr, o_graphe, T_Y0, T_H, 0, 400); },
                          [](int) { return true; }, rafraichir_scrub, [] { rafraichir_courbe(); } });
}

// Hook de banc : un pas de lecture au doigt sans indev — même machine à états que doigt_cb, branchement
// compris (le banc mesure le chemin réel du geste).
void page_poele_banc_scrub(int x_doigt)
{
    if (!o_graphe)
        return;
    uc_lecture_banc(x_doigt, { &g_scrub, [] { return cadre_courbe(nullptr, o_graphe, T_Y0, T_H, 0, 400); },
                               [](int) { return true; }, rafraichir_scrub, [] { rafraichir_courbe(); } });
}

// La vue Courbe du poêle s'ouvre d'habitude par un appui sur l'onglet (onglet_cb). Le banc ne peut
// pas injecter de geste : cette fonction fait EXACTEMENT ce que fait onglet_cb (g_vue = 1 puis
// construire()), sans événement de doigt. Elle n'est appelée que par /scrubpoele.
void page_poele_banc_courbe()
{
    if (g_zone && g_vue != 1)
    {
        g_vue = 1;
        construire();
    }
}

static void construire_courbe(lv_obj_t *z)
{
    // en haut : valeur (comme le Graphe des pièces), échelle au milieu, min / max à droite
    o_cval = ui_label(z, UI_POLICE_TEMP_PETITE, UI_TEXTE, "");
    lv_obj_set_pos(o_cval, 16, 58);
    o_cmin = ui_label(z, UI_POLICE_TEXTE, UI_FROID, "");
    lv_obj_align(o_cmin, LV_ALIGN_TOP_RIGHT, -14, 60);
    o_cmax = ui_label(z, UI_POLICE_TEXTE, UI_CHAUFFE, "");
    lv_obj_align(o_cmax, LV_ALIGN_TOP_RIGHT, -14, 84);
    lv_obj_t *sel = ui_conteneur(z);
    lv_obj_set_size(sel, 102, 24);
    lv_obj_set_pos(sel, 124, 58);
    lv_obj_set_flex_flow(sel, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sel, 3, 0);
    const char *noms[] = {"1 h", "3 h", "12 h"};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *b = ui_bouton(sel, 32, 24);
        lv_obj_set_style_radius(b, 7, 0);
        ui_style_bg_color(b, ui_c(UI_CARTE2), LV_STATE_CHECKED); // choisi : gris clair, l'ambre reste aux actions
        ui_style_border_color(b, ui_c(UI_TEXTE2), LV_STATE_CHECKED);
        ui_style_text_color(b, ui_c(UI_TEXTE), LV_STATE_CHECKED);
        ui_style_text_font(b, UI_POLICE_LEGENDE, 0);
        lv_obj_t *l = lv_label_create(b);
        ui_label_texte(l, noms[i]);
        lv_obj_center(l);
        lv_obj_set_ext_click_area(b, 8);
        o_echelles[i] = b;
        lv_obj_add_event_cb(b, echelle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    o_graphe = ui_conteneur(z);
    lv_obj_set_size(o_graphe, UI_ZONE_L, G_H);
    lv_obj_set_pos(o_graphe, 0, G_Y);
    lv_obj_add_flag(o_graphe, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_graphe, graphe_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(o_graphe, doigt_cb, LV_EVENT_ALL, NULL);
    o_tir_bande = ui_conteneur(z);
    lv_obj_set_size(o_tir_bande, UI_ZONE_L, H_H);
    lv_obj_set_pos(o_tir_bande, 0, H_Y);
    lv_obj_clear_flag(o_tir_bande, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_tir_bande, bande_tirage_cb, LV_EVENT_DRAW_MAIN, NULL);
    o_tir_txt = ui_label(z, UI_POLICE_LEGENDE, UI_FROID, "");
    lv_obj_set_pos(o_tir_txt, 4, H_Y);
    o_bulle = uc_bulle(z, &o_bulle_txt);

    // légende = pastilles de couches, comme le Graphe des pièces
    lv_obj_t *rang = ui_conteneur(z);
    lv_obj_set_size(rang, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(rang, UI_MARGE, 412);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static const char *const COUCHES[4] = {TXT("Consigne", "Setpoint"), TXT("Poêle", "Stove"), TXT("Recharge", "Refill"), TXT("Tirage", "Draft")};
    static const uint32_t COUL[4] = {UI_TEXTE, UI_CHAUFFE, UI_ROUGE, UI_FROID};
    for (int k = 0; k < 4; k++)
        o_couches[k] = uc_pastille(rang, COUCHES[k], COUL[k], 89, couche_cb, k);
    g_scrub = -1;
}

// =====================================================================  RÉGLAGES
struct pl_ligne_t
{
    const char *nom, *aide, *cle;
    float pas, min, max;
    int decimales;
    const char *unite;
};
static const pl_ligne_t LIGNES[7] = {
    {TXT("Consigne fumées", "Flue setpoint"), TXT("visée par le PID", "targeted by the PID"), "consigne", 5, 150, 350, 0, "°"},
    {TXT("Tirage mini", "Min draft"), TXT("le volet ne ferme jamais plus", "damper never closes further"), "tirage_min", 5, 0, 100, 0, "%"},
    {TXT("Alerte recharge", "Refill alert"), TXT("fumées sous ce seuil", "flue gas below this threshold"), "recharge", 5, 100, 250, 0, "°"},
    {TXT("Gain P", "P gain"), TXT("réaction à l'écart", "reacts to the gap"), "kp", 0.5f, 1, 10, 1, ""},
    {TXT("Gain I", "I gain"), TXT("rattrape l'écart qui dure", "fixes a lasting gap"), "ki", 0.001f, 0.001f, 0.1f, 3, ""},
    {TXT("Gain D", "D gain"), TXT("freine les variations", "damps changes"), "kd", 1, 2, 50, 1, ""},
    {TXT("Limite intégrale", "Integral limit"), TXT("anti-emballement", "anti-windup"), "limite", 5, 0, 100, 0, ""},
};

static float lire(int i)
{
    pl_reglages_t &r = pl_etat.r;
    switch (i)
    {
    case 0: return r.consigne;
    case 1: return r.tirage_min;
    case 2: return r.recharge;
    case 3: return r.kp;
    case 4: return r.ki;
    case 5: return r.kd;
    default: return r.limite;
    }
}
static void ecrire(int i, float v)
{
    pl_reglages_t &r = pl_etat.r;
    switch (i)
    {
    case 0: r.consigne = (int)v; break;
    case 1: r.tirage_min = (int)v; break;
    case 2: r.recharge = (int)v; break;
    case 3: r.kp = v; break;
    case 4: r.ki = v; break;
    case 5: r.kd = v; break;
    default: r.limite = (int)v; break;
    }
}

static void rafraichir_reglages()
{
    char b[16];
    for (int i = 0; i < 7; i++)
    {
        ui_fmt_float(b, sizeof b, lire(i), LIGNES[i].decimales, LIGNES[i].unite);
        ui_label_texte(o_vals[i], b);
    }
}

static void pas_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e); // ligne * 2 + (1 = plus)
    int i = code / 2;
    const pl_ligne_t &L = LIGNES[i];
    float v = lire(i) + (code % 2 ? L.pas : -L.pas);
    float m = 1;
    for (int d = 0; d < L.decimales + 1; d++)
        m *= 10;
    v = roundf(v * m) / m; // pas de dérive des flottants (0,001 + 0,001…)
    ecrire(i, LV_CLAMP(L.min, v, L.max));
    rafraichir_reglages();
    envoyer(L.cle);
}

static void construire_reglages(lv_obj_t *z)
{
    lv_obj_t *page = ui_conteneur_defilant(z, 0, 50, UI_ZONE_L, UI_ZONE_H - 50);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(page, UI_MARGE, 0);
    lv_obj_set_style_pad_bottom(page, 16, 0);
    lv_obj_set_style_pad_row(page, 10, 0);

    lv_obj_t *carte = nullptr;
    for (int i = 0; i < 7; i++)
    {
        if (i == 0 || i == 3)
        {
            ui_titre_section(page, i == 0 ? TXT("RÉGULATION", "CONTROL") : "PID");
            carte = ui_conteneur(page);
            ui_style_ajouter(carte, &ui_st_carte, 0);
            lv_obj_set_size(carte, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_hor(carte, 16, 0);
            lv_obj_set_style_pad_right(carte, 12, 0);
            lv_obj_set_style_pad_ver(carte, 0, 0);
            lv_obj_set_flex_flow(carte, LV_FLEX_FLOW_COLUMN);
        }
        lv_obj_t *r = ui_conteneur(carte);
        lv_obj_set_size(r, lv_pct(100), 56);
        if (i != 2 && i != 6)
        {
            lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
            ui_style_border_width(r, 1, 0);
            ui_style_border_color(r, ui_c(UI_BORD), 0);
        }
        lv_obj_t *col = ui_conteneur(r);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_align(col, LV_ALIGN_LEFT_MID, 0, 0);
        ui_label(col, UI_POLICE_TEXTE, UI_TEXTE, LIGNES[i].nom);
        ui_label(col, UI_POLICE_LEGENDE, UI_TEXTE2, LIGNES[i].aide);
        lv_obj_t *plus = ui_bouton(r, 44, 44);
        lv_obj_set_style_radius(plus, 12, 0);
        ui_style_bg_color(plus, ui_c(UI_CARTE2), 0);
        lv_obj_center(ui_label(plus, UI_POLICE_TITRE, UI_TEXTE, "+"));
        lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(plus, pas_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 2 + 1));
        o_vals[i] = ui_label(r, UI_POLICE_3X5_P, UI_ACCENT, "");
        lv_obj_set_width(o_vals[i], 74);
        lv_obj_set_style_text_align(o_vals[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(o_vals[i], LV_ALIGN_RIGHT_MID, -48, 0);
        lv_obj_t *moins = ui_bouton(r, 44, 44);
        lv_obj_set_style_radius(moins, 12, 0);
        ui_style_bg_color(moins, ui_c(UI_CARTE2), 0);
        lv_obj_center(ui_label(moins, UI_POLICE_TITRE, UI_TEXTE, "−"));
        lv_obj_align(moins, LV_ALIGN_RIGHT_MID, -126, 0);
        lv_obj_add_event_cb(moins, pas_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 2));
    }
}

// =====================================================================  PAGE
static void rafraichir()
{
    if (!g_zone)
        return;
    for (int i = 0; i < 3; i++)
        if (i == g_vue)
            lv_obj_add_state(o_onglets[i], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_onglets[i], LV_STATE_CHECKED);
    if (g_vue == 0)
        rafraichir_feu();
    else if (g_vue == 1)
        rafraichir_courbe();
    else
        rafraichir_reglages();
}

void page_poele_maj() { rafraichir(); }

static void onglet_cb(lv_event_t *e)
{
    g_vue = (int)(intptr_t)lv_event_get_user_data(e);
    construire();
}

static void construire()
{
    lv_obj_clean(g_zone);
    g_vent_anime = -1;

    static const char *const ONGLETS[] = {TXT("Feu", "Fire"), TXT("Courbe", "Chart"), TXT("Réglages", "Settings")};
    ui_onglets(g_zone, ONGLETS, 3, g_vue, onglet_cb, nullptr, o_onglets);

    if (g_vue == 0)
        construire_feu(g_zone);
    else if (g_vue == 1)
        construire_courbe(g_zone);
    else
        construire_reglages(g_zone);
    rafraichir();
}

void page_poele_creer(lv_obj_t *zone)
{
    g_zone = zone;
    g_vue = 0;
    construire();
}

void page_poele_quitter()
{
    g_zone = nullptr; // rafraichir() ne touche plus à rien
}
