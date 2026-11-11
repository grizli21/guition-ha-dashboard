#include "page_ballon.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "ui_courbe.h"
#include "ui_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bc_etat_t bc_etat = {0, 50, BC_AUTO, true, false};
static const bc_historique_t bc_hist_depart = {{0}, {0}, {0}, {0}, {0}, -1}; // valeur de départ (en flash)
UI_PSRAM(bc_historique_t, bc_hist_p, 1, &bc_hist_depart); // 29/10 : en PSRAM (ui_psram.h)
void (*bc_commande)(const bc_etat_t &e) = nullptr;

#define VOLUME 150 // L (25/10)
#define T_MIN 30   // échelle de la cuve (°C) : bas
#define T_MAX 70   //                           haut (Réglages)
#define C_MIN 40   // consigne permise par HA (input_number.ballon_consigne)
#define C_MAX 70
#define CUVE_X 40 // même place dans Ballon et Réglages
#define CUVE_Y 66
#define CUVE_L 120
#define CUVE_H 250
#define COL_X 186 // colonne de droite (lignes d'information)
#define C_RELAIS 0xE5484D // chauffe : même rouge que le Graphe des pièces
#define C_CREUSES 0x2F7D57
#define C_RESISTANCE_CHAUDE 0xFF4A2A

static lv_obj_t *g_zone = nullptr;
static int g_vue = 0; // 0 Ballon, 1 Courbe, 2 Réglages
static lv_obj_t *o_onglets[3];

// =====================================================================  CUVE (Ballon et Réglages)
// Bulles : elles montent tout le temps (de la résistance quand elle chauffe) ; à chaque pas, seules les petites zones
// autour des bulles sont redessinées
#define NB_BULLES 9
struct bulle_t
{
    float y;  // dans la cuve, depuis le haut
    int16_t x; // idem, depuis la gauche
    uint8_t r;
    float v; // px par pas
};
static bulle_t g_bulles[NB_BULLES];
static lv_obj_t *o_cuve;
static lv_timer_t *g_minuterie;
static float g_niveau;      // part remplie de la cuve, 0..1
static lv_coord_t g_niv_px; // dernier niveau dessiné (px) : on ne redessine la cuve que s'il change
static bool g_cuve_chauffe;

static lv_coord_t surface() { return (lv_coord_t)(CUVE_H - g_niveau * CUVE_H); }

static void bulle_neuve(bulle_t &b, bool partout)
{
    const bool resistance = bc_etat.chauffe && rand() % 3; // en chauffe, 2 bulles sur 3 partent du serpentin
    b.r = 2 + rand() % 3;
    b.v = 0.5f + (rand() % 10) / 10.0f;
    if (resistance)
    {
        b.x = CUVE_L / 2 - 14 + rand() % 28;
        b.y = CUVE_H - 100 - rand() % 8;
    }
    else
    {
        b.x = 14 + rand() % (CUVE_L - 28);
        b.y = partout ? surface() + 10 + rand() % LV_MAX(1, CUVE_H - surface() - 20) : CUVE_H - 14;
    }
}

static void zone_bulle(const bulle_t &b, lv_area_t &z, const lv_area_t &a)
{
    z.x1 = a.x1 + b.x - b.r - 1;
    z.x2 = a.x1 + b.x + b.r + 1;
    z.y1 = (lv_coord_t)(a.y1 + b.y) - b.r - 1;
    z.y2 = (lv_coord_t)(a.y1 + b.y) + b.r + 1;
}

static void bulles_cb(lv_timer_t *)
{
    if (!o_cuve)
        return;
    lv_area_t a, z;
    lv_obj_get_coords(o_cuve, &a);
    const lv_coord_t s = surface();
    for (bulle_t &b : g_bulles)
    {
        zone_bulle(b, z, a);
        lv_obj_invalidate_area(o_cuve, &z); // l'ancienne place
        b.y -= b.v;
        if (b.y - b.r < s + 3)
            bulle_neuve(b, false);
        zone_bulle(b, z, a);
        lv_obj_invalidate_area(o_cuve, &z); // la nouvelle
    }
}

// résistance plongeante : bride au fond, serpentin à 4 branches et doigt de gant du thermostat ; grise au repos,
// rouge incandescent avec un halo quand le relais chauffe
static void resistance(uc_cible_t *ctx, const lv_area_t &a, bool chauffe)
{
    const lv_coord_t cx = (a.x1 + a.x2) / 2, bas = a.y2 - 10, haut = a.y2 - 96, creux = a.y2 - 26;
    for (int passe = chauffe ? 0 : 1; passe < 2; passe++)
    {
        lv_draw_line_dsc_t l;
        lv_draw_line_dsc_init(&l);
        l.color = ui_c(passe ? (chauffe ? C_RESISTANCE_CHAUDE : 0x8A929C) : 0xFF7A3D);
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
            uc_segment(ctx, l, x[j], y0[j], x[j], haut);
        const lv_point_t h1 = {(lv_coord_t)(cx - 12), haut}, h2 = {(lv_coord_t)(cx + 12), haut}, b1 = {cx, creux};
        uc_arc(ctx, c, &h1, 6 + l.width / 2, 180, 360);
        uc_arc(ctx, c, &h2, 6 + l.width / 2, 180, 360);
        uc_arc(ctx, c, &b1, 6 + l.width / 2, 0, 180);
    }
    lv_draw_rect_dsc_t g; // doigt de gant (thermostat), à côté du serpentin
    lv_draw_rect_dsc_init(&g);
    g.bg_color = ui_c(0x5A626C);
    g.radius = 3;
    lv_area_t z = {(lv_coord_t)(cx + 30), (lv_coord_t)(a.y2 - 70), (lv_coord_t)(cx + 35), bas};
    uc_carre(ctx, g, &z);
    g.bg_color = ui_c(UI_ETEINT); // bride : tout le fond de la cuve (25/10)
    g.radius = 0;
    z = {a.x1, (lv_coord_t)(a.y2 - 10), a.x2, a.y2};
    uc_carre(ctx, g, &z);
}

// eau (dégradé chaud en haut, tiède en bas) jusqu'au niveau, vide au-dessus ; résistance ; bulles dans l'eau
static void cuve_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = ui_c(0x161A20);
    uc_carre(ctx, d, &a);
    d.bg_grad.dir = LV_GRAD_DIR_VER;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = ui_c(0xFF6A3D);
    d.bg_grad.stops[0].frac = 0;
    d.bg_grad.stops[1].color = ui_c(0x6E8FA8);
    d.bg_grad.stops[1].frac = 255;
    // 9.6 : chaque palier porte EN PLUS son opacité (draw/lv_grad.h:52-56) et l'init la laisse à 0 -> sans ça le
    // dégradé est entièrement transparent (src/draw/lv_draw_rect.c:220-223 et src/draw/sw/lv_draw_sw_grad.c:213).
    d.bg_grad.stops[0].opa = LV_OPA_COVER;
    d.bg_grad.stops[1].opa = LV_OPA_COVER;
    const lv_area_t eau = {a.x1, (lv_coord_t)(a.y1 + surface()), a.x2, a.y2};
    if (eau.y1 < eau.y2)
        uc_carre(ctx, d, &eau);
    resistance(ctx, a, g_cuve_chauffe);
    lv_draw_rect_dsc_t b;
    lv_draw_rect_dsc_init(&b);
    b.radius = LV_RADIUS_CIRCLE;
    b.bg_opa = LV_OPA_TRANSP;
    b.border_color = ui_c(0xFFE2B0);
    b.border_width = 1;
    b.border_opa = LV_OPA_60;
    for (const bulle_t &bu : g_bulles)
    {
        lv_area_t z;
        zone_bulle(bu, z, a);
        lv_area_t c = {(lv_coord_t)(z.x1 + 1), (lv_coord_t)(z.y1 + 1), (lv_coord_t)(z.x2 - 1), (lv_coord_t)(z.y2 - 1)};
        if (c.y1 > eau.y1)
            uc_carre(ctx, b, &c);
    }
}

// niveau = part de la cuve remplie au départ (les bulles naissent dans l'eau)
static void cuve_creer(lv_obj_t *z, float niveau)
{
    g_niveau = LV_CLAMP(0.0f, niveau, 1.0f);
    lv_obj_t *cadre = ui_conteneur(z);
    lv_obj_set_size(cadre, CUVE_L, CUVE_H);
    lv_obj_set_pos(cadre, CUVE_X, CUVE_Y);
    lv_obj_clear_flag(cadre, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(cadre, 36, 0);
    lv_obj_set_style_clip_corner(cadre, true, 0);
    ui_style_border_color(cadre, ui_c(UI_BORD), 0);
    ui_style_border_width(cadre, 3, 0);
    lv_obj_set_style_border_post(cadre, true, 0);
    o_cuve = ui_conteneur(cadre);
    lv_obj_set_size(o_cuve, CUVE_L, CUVE_H);
    lv_obj_clear_flag(o_cuve, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_cuve, cuve_cb, LV_EVENT_DRAW_MAIN, NULL);
    g_niv_px = -1;
    for (bulle_t &b : g_bulles)
        bulle_neuve(b, true);
    g_minuterie = lv_timer_create(bulles_cb, 50, nullptr);
}

// niveau = part de la cuve remplie ; redessinée seulement si le niveau (au pixel près) ou la résistance change
static void cuve_niveau(float niveau)
{
    g_niveau = LV_CLAMP(0.0f, niveau, 1.0f);
    const lv_coord_t px = (lv_coord_t)(g_niveau * CUVE_H);
    if (px != g_niv_px || bc_etat.chauffe != g_cuve_chauffe)
    {
        g_niv_px = px;
        g_cuve_chauffe = bc_etat.chauffe;
        lv_obj_invalidate(o_cuve);
    }
}

// =====================================================================  communs
static void ligne_info(lv_obj_t *z, lv_coord_t y, const char *nom, lv_obj_t **valeur)
{
    lv_obj_set_pos(ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE3, nom), COL_X, y + 2);
    *valeur = ui_label(z, UI_POLICE_PETITE, UI_TEXTE2, "");
    lv_obj_align(*valeur, LV_ALIGN_TOP_RIGHT, -UI_MARGE, y);
}

static void trait_h(lv_obj_t *z, lv_coord_t y)
{
    lv_obj_t *s = ui_conteneur(z);
    lv_obj_set_size(s, UI_ZONE_L - COL_X - UI_MARGE, 1);
    ui_style_bg_color(s, ui_c(UI_BORD), 0);
    ui_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_pos(s, COL_X, y);
}

static void envoyer()
{
    if (bc_commande)
        bc_commande(bc_etat);
}

// part remplie de la cuve : jusqu'à la consigne dans Ballon (haut = consigne), sur l'échelle 30-70 dans Réglages
static float niveau_ballon() { return (bc_etat.temp - T_MIN) / LV_MAX(1.0f, bc_etat.consigne - T_MIN); }
static float niveau_reglages() { return (bc_etat.temp - T_MIN) / (T_MAX - T_MIN); }

// =====================================================================  BALLON
// cuve pleine = à la consigne ; à droite la température, les douches possibles, l'état ; dessous la journée
static lv_obj_t *o_haut, *o_temp, *o_douches, *o_douches_u, *o_chauffe, *o_mode, *o_combles, *o_jour[3], *o_debit, *o_debit_txt;

// douches à 38 °C (eau froide à 12 °C, 50 L par douche) avec 80 % du ballon : une estimation, la stratification n'est pas
// mesurée (une seule sonde)
static int douches()
{
    if (bc_etat.temp <= 38)
        return 0;
    return (int)(0.8f * VOLUME * (bc_etat.temp - 12) / (38 - 12) / 50);
}

static void rafraichir_ballon()
{
    cuve_niveau(niveau_ballon());
    ui_label_texte_fmt(o_haut, "%d°", (int)bc_etat.consigne);
    ui_label_texte_fmt(o_temp, "%d", (int)(bc_etat.temp + 0.5f));
    const int n = douches();
    ui_label_texte_fmt(o_douches, "%d", n);
    ui_label_texte(o_douches_u, n > 1 ? TXT("douches", "showers") : TXT("douche", "shower"));
    if (bc_etat.mode == BC_ARRET)
    {
        ui_label_texte(o_chauffe, TXT("arrêt", "off"));
        ui_style_text_color(o_chauffe, ui_c(UI_TEXTE3), 0);
    }
    else if (bc_etat.chauffe)
    {
        if (bc_etat.puissance >= 100)
            ui_label_texte_fmt(o_chauffe, TXT("chauffe · %d" SEP_DEC "%d kW", "heating · %d" SEP_DEC "%d kW"),
                                bc_etat.puissance / 1000, bc_etat.puissance % 1000 / 100);
        else
            ui_label_texte(o_chauffe, TXT("chauffe", "heating"));
        ui_style_text_color(o_chauffe, ui_c(C_RESISTANCE_CHAUDE), 0);
    }
    else
    {
        ui_label_texte(o_chauffe, TXT("au repos", "idle"));
        ui_style_text_color(o_chauffe, ui_c(UI_TEXTE2), 0);
    }
    ui_label_texte(o_mode, bc_etat.mode == BC_ARRET   ? TXT("Arrêt", "Off")
                            : bc_etat.mode == BC_FORCE ? TXT("Forcé", "Boost")
                            : bc_etat.eco              ? TXT("Auto · éco", "Auto · eco")
                                                       : TXT("Auto · normal", "Auto · normal"));
    ui_label_texte_fmt(o_combles, "%d °C", bc_etat.combles);
    ui_label_texte_fmt(o_jour[0], "%d", bc_etat.chaude_jour);
    ui_label_texte_fmt(o_jour[1], "%d", bc_etat.froide_jour);
    ui_label_texte_fmt(o_jour[2], "%d" SEP_DEC "%d", (int)bc_etat.kwh_jour, (int)(bc_etat.kwh_jour * 10 + 0.5f) % 10);
    // toujours là (pas de vide en bas) : « -- » et grisé quand l'eau chaude ne coule pas (25/10)
    const bool coule = bc_etat.debit > 0;
    if (coule)
        ui_label_texte_fmt(o_debit_txt, LV_SYMBOL_DOWNLOAD "  " TXT("Eau chaude en cours · %d L/min", "Hot water running · %d L/min"), bc_etat.debit);
    else
        ui_label_texte(o_debit_txt, LV_SYMBOL_DOWNLOAD "  " TXT("Eau chaude en cours · -- L/min", "Hot water running · -- L/min"));
    ui_style_text_color(o_debit_txt, ui_c(coule ? UI_FROID : UI_TEXTE3), 0);
    ui_style_border_color(o_debit, ui_c(coule ? UI_FROID : UI_BORD), 0);
}

static void construire_ballon(lv_obj_t *z)
{
    cuve_creer(z, niveau_ballon());
    o_haut = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_pos(o_haut, 6, CUVE_Y + 4);
    lv_obj_t *bas = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE3, "");
    ui_label_texte_fmt(bas, "%d°", T_MIN);
    lv_obj_set_pos(bas, 6, CUVE_Y + CUVE_H - 18);

    lv_obj_t *r = ui_valeur_unite(z, UI_POLICE_TEMP, UI_TEXTE, "°C", &o_temp);
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 62);
    lv_obj_t *rd = ui_valeur_unite(z, UI_POLICE_3X5_M, UI_ACCENT, TXT("douches", "showers"), &o_douches);
    o_douches_u = lv_obj_get_child(rd, -1);
    lv_obj_align(rd, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 174);
    trait_h(z, 222);
    ligne_info(z, 234, TXT("Chauffe", "Heating"), &o_chauffe);
    ligne_info(z, 260, TXT("Mode", "Mode"), &o_mode);
    ligne_info(z, 286, TXT("Combles", "Attic"), &o_combles);

    // la journée : trois chiffres posés sur le fond (pas de cartes : ce ne sont pas des boutons)
    lv_obj_set_pos(ui_titre_section(z, TXT("AUJOURD'HUI", "TODAY")), UI_MARGE, 332);
    const char *noms[] = {TXT("eau chaude", "hot water"), TXT("eau totale", "total water"), TXT("énergie", "energy")},
               *unites[] = {"L", "L", "kWh"};
    const uint32_t coul[] = {0xFF9A5B, UI_FROID, UI_ACCENT};
    const lv_coord_t l = (UI_ZONE_L - 2 * UI_MARGE) / 3;
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *c = ui_conteneur(z);
        lv_obj_set_size(c, l, 52);
        lv_obj_set_pos(c, UI_MARGE + i * l, 354);
        lv_obj_t *v = ui_valeur_unite(c, UI_POLICE_3X5_M, coul[i], unites[i], &o_jour[i]);
        lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_align(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, noms[i]), LV_ALIGN_BOTTOM_MID, 0, 0);
        if (i)
        {
            lv_obj_t *s = ui_conteneur(z); // trait de séparation
            lv_obj_set_size(s, 1, 40);
            ui_style_bg_color(s, ui_c(UI_BORD), 0);
            ui_style_bg_opa(s, LV_OPA_COVER, 0);
            lv_obj_set_pos(s, UI_MARGE + i * l, 360);
        }
    }

    // bandeau « eau chaude en cours » (le débit en direct)
    o_debit = ui_conteneur(z);
    ui_style_ajouter(o_debit, &ui_st_carte, 0);
    lv_obj_set_size(o_debit, UI_ZONE_L - 2 * UI_MARGE, 36);
    lv_obj_set_pos(o_debit, UI_MARGE, 428);
    ui_style_border_color(o_debit, ui_c(UI_FROID), 0);
    lv_obj_set_style_pad_hor(o_debit, 12, 0);
    o_debit_txt = ui_label(o_debit, UI_POLICE_TEXTE, UI_FROID, ""); // la police de 16 a les symboles
    lv_obj_align(o_debit_txt, LV_ALIGN_LEFT_MID, 0, 0);
}

// =====================================================================  COURBE
#define G_Y 118
#define G_HAUT 8
#define G_BAS 200
#define B_BAS 344 // barres d'eau tirée (dans le graphe)
#define B_HMAX 46
static lv_obj_t *o_graphe, *o_cval, *o_cmin, *o_cmax, *o_bulle, *o_bulle_txt;
static int g_scrub = -1;

static uc_cadre_t cadre(uc_cible_t *ctx)
{
    lv_area_t a;
    lv_obj_get_coords(o_graphe, &a);
    uc_cadre_t c = {ctx, (lv_coord_t)(a.x1 + 40), (lv_coord_t)(a.x2 - 8), (lv_coord_t)(a.y1 + G_HAUT), (lv_coord_t)(a.y1 + G_BAS),
                    0, BC_POINTS, T_MIN, T_MAX};
    return c;
}

static bool relais(int i) { return bc_bit(bc_hist.chauffe, i); }

static void graphe_cb(lv_event_t *e)
{
    const uc_cadre_t c = cadre(uc_cible_du(e));
    uc_grille(c, 10, "°");
    uc_bandes(c, relais, C_RELAIS);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    if (bc_hist.fin >= 0)
    {
        uc_heures(c, bc_hist.fin, 5, 360);
        // heures creuses / pleines : fine bande sous l'axe des heures, une case par suite de points de même tarif
        for (int i = 0; i < BC_POINTS;)
        {
            const bool hc = bc_bit(bc_hist.creuses, i);
            int j = i + 1;
            while (j < BC_POINTS && bc_bit(bc_hist.creuses, j) == hc)
                j++;
            d.bg_color = ui_c(hc ? C_CREUSES : UI_BORD);
            const lv_area_t z = {c.x(i), (lv_coord_t)(c.bas + 22), j < BC_POINTS ? c.x(j) : c.x1, (lv_coord_t)(c.bas + 27)};
            uc_carre(c.ctx, d, &z);
            i = j;
        }
    }
    const uc_serie_t t = uc_serie(bc_hist.temp, BC_VIDE, 10);
    uc_degrade(c, t, 0xFF6A3D);
    uc_escalier(c, uc_serie(bc_hist.consigne, BC_VIDE, 10), UI_TEXTE);
    uc_principale(c, t, UI_CHAUFFE);
    if (g_scrub >= 0)
        uc_curseur(c, g_scrub, t, UI_CHAUFFE);

    // eau chaude tirée par tranche de 2 h (24 points), alignée sur l'axe des heures : une case discrète par tranche,
    // barre + litres au-dessus si on a tiré
    d.radius = 3;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = UI_POLICE_LEGENDE;
    ld.color = ui_c(UI_FROID);
    ld.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t a;
    lv_obj_get_coords(o_graphe, &a);
    const lv_coord_t bas = a.y1 + B_BAS;
    for (int k = 0; k < BC_POINTS / 24; k++)
    {
        int litres = 0;
        for (int i = k * 24; i < k * 24 + 24; i++)
            litres += bc_hist.litres[i];
        const lv_coord_t x0 = c.x(k * 24) + 3, x1 = c.x(LV_MIN(k * 24 + 24, BC_POINTS - 1)) - 3;
        d.bg_color = ui_c(UI_BORD);
        lv_area_t z = {x0, (lv_coord_t)(bas - 2), x1, bas};
        uc_carre(c.ctx, d, &z);
        if (litres == 0)
            continue;
        d.bg_color = ui_c(UI_FROID);
        z.y1 = (lv_coord_t)(bas - LV_MAX(4, LV_MIN(B_HMAX, litres * B_HMAX / 50)));
        uc_carre(c.ctx, d, &z);
        char s[8];
        snprintf(s, sizeof s, "%d", litres);
        const lv_area_t zt = {(lv_coord_t)(x0 - 8), (lv_coord_t)(z.y1 - 16), (lv_coord_t)(x1 + 8), (lv_coord_t)(z.y1 - 2)};
        uc_texte(c.ctx, ld, &zt, s, true); // « s » est local : 9.6 doit le copier (task rendue plus tard)
    }
}

// en-tête (maintenant, ou le point lu au doigt) et bulle ; « graphe » : redessiner aussi la courbe
static void rafraichir_courbe(bool graphe)
{
    const int i = g_scrub >= 0 ? g_scrub : BC_POINTS - 1;
    const int v = bc_hist.temp[i] != BC_VIDE ? (bc_hist.temp[i] + 5) / 10 : (int)(bc_etat.temp + 0.5f);
    ui_label_texte_fmt(o_cval, "%d", v);
    ui_style_text_color(o_cval, ui_c(g_scrub >= 0 ? UI_CHAUFFE : UI_TEXTE), 0);
    int imin, imax;
    uc_min_max(bc_hist.temp, 0, BC_POINTS, BC_VIDE, &imin, &imax);
    char h[24];
    if (imin >= 0 && bc_hist.fin >= 0)
    {
        uc_texte_heure(h, sizeof h, imin, BC_POINTS - 1, bc_hist.fin, 5);
        ui_label_texte_fmt(o_cmin, TXT("min %d° à %s", "min %d° at %s"), (bc_hist.temp[imin] + 5) / 10, h);
        uc_texte_heure(h, sizeof h, imax, BC_POINTS - 1, bc_hist.fin, 5);
        ui_label_texte_fmt(o_cmax, TXT("max %d° à %s", "max %d° at %s"), (bc_hist.temp[imax] + 5) / 10, h);
    }
    else
    {
        ui_label_texte(o_cmin, "");
        ui_label_texte(o_cmax, "");
    }
    if (g_scrub >= 0 && bc_hist.fin >= 0)
    {
        char b[160], t[8] = "--";
        if (bc_hist.temp[i] != BC_VIDE)
            snprintf(t, sizeof t, "%d", (bc_hist.temp[i] + 5) / 10);
        uc_texte_heure(h, sizeof h, i, BC_POINTS - 1, bc_hist.fin, 5);
        snprintf(b, sizeof b, TXT("%s\n" UI_MARK_CHAUFFE " Eau %s°#\nConsigne %d°%s%s", "%s\n" UI_MARK_CHAUFFE " Water %s°#\nSetpoint %d°%s%s"), h, t, (bc_hist.consigne[i] + 5) / 10,
                 relais(i) ? TXT("\n" UI_MARK_RELIS " Chauffe#", "\n" UI_MARK_RELIS " Heating#") : "", bc_bit(bc_hist.creuses, i) ? TXT("\nHeures creuses", "\nOff-peak") : TXT("\nHeures pleines", "\nPeak"));
        uc_bulle_ecrire(o_bulle, o_bulle_txt, b, cadre(nullptr), i, g_zone, G_Y + 4, UI_ZONE_L);
    }
    else
        ui_cacher(o_bulle, true);
    // 27/10 (audit des redessins) : la courbe seulement si ce qu'elle lit change (historique, point lu au doigt)
    static uint32_t dessinee = 0;
    const uint32_t e = ui_empreinte(&bc_hist, sizeof bc_hist, ui_empreinte(&g_scrub, sizeof g_scrub));
    if (graphe && e != dessinee)
    {
        dessinee = e;
        lv_obj_invalidate(o_graphe);
    }
}

// Chemin léger du glissé de lecture : ne repeint que les deux colonnes du curseur (avant/après) et l'ancienne
// place de la bulle — en 9.6 lv_obj_set_pos n'invalidite que la NOUVELLE position (core/lv_obj_pos.c:68-90),
// c'est ici qu'on efface l'ancienne.
static void rafraichir_scrub(int i_avant)
{
    const int i = g_scrub;
    const int v = bc_hist.temp[i] != BC_VIDE ? (bc_hist.temp[i] + 5) / 10 : (int)(bc_etat.temp + 0.5f);
    ui_label_texte_fmt(o_cval, "%d", v);
    ui_style_text_color(o_cval, ui_c(UI_CHAUFFE), 0);
    if (bc_hist.fin >= 0)
    {
        char b[160], t[8] = "--", h[24];
        if (bc_hist.temp[i] != BC_VIDE)
            snprintf(t, sizeof t, "%d", (bc_hist.temp[i] + 5) / 10);
        uc_texte_heure(h, sizeof h, i, BC_POINTS - 1, bc_hist.fin, 5);
        snprintf(b, sizeof b, TXT("%s\n" UI_MARK_CHAUFFE " Eau %s°#\nConsigne %d°%s%s", "%s\n" UI_MARK_CHAUFFE " Water %s°#\nSetpoint %d°%s%s"), h, t, (bc_hist.consigne[i] + 5) / 10,
                 relais(i) ? TXT("\n" UI_MARK_RELIS " Chauffe#", "\n" UI_MARK_RELIS " Heating#") : "", bc_bit(bc_hist.creuses, i) ? TXT("\nHeures creuses", "\nOff-peak") : TXT("\nHeures pleines", "\nPeak"));
        uc_bulle_poser(o_bulle, o_bulle_txt, b, cadre(nullptr), i, g_zone, G_Y + 4, UI_ZONE_L, o_graphe);
    }
    uc_repeindre_bandes(cadre(nullptr), o_graphe, i_avant, i);
}

static void doigt_cb(lv_event_t *e)
{
    // 06/11 (chantier 2) : premier appui ou historique absent -> chemin complet ; sinon léger (colonnes + bulle).
    uc_lecture_doigt(e, { &g_scrub, [] { return cadre(nullptr); },
                          [](int) { return bc_hist.fin >= 0; }, rafraichir_scrub, [] { rafraichir_courbe(true); } });
}

// Hook de banc : un pas de lecture au doigt sans indev — même machine à états que doigt_cb, branchement
// compris (le banc mesure le chemin réel du geste).
void page_ballon_banc_scrub(int x_doigt)
{
    if (!o_graphe)
        return;
    uc_lecture_banc(x_doigt, { &g_scrub, [] { return cadre(nullptr); },
                               [](int) { return bc_hist.fin >= 0; }, rafraichir_scrub, [] { rafraichir_courbe(true); } });
}

static void construire(); // déclarée plus bas (section PAGE) : l'outil du banc l'emprunte comme onglet_cb

// La vue Courbe du ballon s'ouvre d'habitude par un appui sur l'onglet (onglet_cb). Le banc ne peut
// pas injecter de geste : cette fonction fait EXACTEMENT ce que fait onglet_cb (même code :
// g_vue = 1 puis construire()), sans événement de doigt. Elle n'est appelée que par /scrubballon.
void page_ballon_banc_courbe()
{
    if (g_zone && g_vue != 1)
    {
        g_vue = 1;
        construire();
    }
}

static void construire_courbe(lv_obj_t *z)
{
    lv_obj_t *r = ui_valeur_unite(z, UI_POLICE_TEMP_PETITE, UI_TEXTE, "°C", &o_cval);
    lv_obj_set_pos(r, 16, 54);
    o_cmin = ui_label(z, UI_POLICE_TEXTE, UI_FROID, "");
    lv_obj_align(o_cmin, LV_ALIGN_TOP_RIGHT, -14, 58);
    o_cmax = ui_label(z, UI_POLICE_TEXTE, UI_CHAUFFE, "");
    lv_obj_align(o_cmax, LV_ALIGN_TOP_RIGHT, -14, 82);

    o_graphe = ui_conteneur(z);
    lv_obj_set_size(o_graphe, UI_ZONE_L, UI_ZONE_H - G_Y - 4);
    lv_obj_set_pos(o_graphe, 0, G_Y);
    lv_obj_add_flag(o_graphe, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_graphe, graphe_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(o_graphe, doigt_cb, LV_EVENT_ALL, NULL);

    // légende : chauffe, heures creuses / pleines ; puis le titre des barres
    const char *noms[] = {TXT("chauffe", "heating"), TXT("heures creuses", "off-peak"), TXT("heures pleines", "peak")};
    const uint32_t coul[] = {C_RELAIS, C_CREUSES, UI_BORD};
    lv_coord_t x = 4;
    for (int k = 0; k < 3; k++)
    {
        lv_obj_t *p = ui_conteneur(z);
        lv_obj_set_size(p, 10, 10);
        lv_obj_set_style_radius(p, 2, 0);
        ui_style_bg_color(p, ui_c(coul[k]), 0);
        ui_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_pos(p, x, G_Y + 234);
        lv_obj_t *l = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE2, noms[k]);
        lv_obj_set_pos(l, x + 14, G_Y + 231);
        // 9.6 : lv_txt_get_width n'existe plus (l'alias api_map/lv_api_map_v8.h:172 renvoie à lv_text_get_width, qui
        // n'est déclaré nulle part) — même mesure avec lv_text_get_size (font/lv_text.h:117)
        lv_point_t sz;
        lv_text_get_size(&sz, noms[k], UI_POLICE_LEGENDE, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        x += 14 + sz.x + 12;
    }
    lv_obj_set_pos(ui_label(z, UI_POLICE_LEGENDE, UI_FROID, TXT("Eau chaude tirée · litres par 2 h", "Hot water drawn · litres per 2 h")), 4, G_Y + 262);
    o_bulle = uc_bulle(z, &o_bulle_txt);
    g_scrub = -1;
}

// =====================================================================  RÉGLAGES
// la même cuve, à la même place, graduée 30-70 °C : le curseur de consigne glisse dessus ; les modes et le régime éco en bas
static lv_obj_t *o_trait, *o_bouton, *o_rconsigne, *o_reau, *o_modes[3], *o_eco_inter;
static bool g_glisse = false; // doigt sur le curseur : la consigne affichée est celle du doigt

static lv_coord_t y_de(float t) { return (lv_coord_t)(CUVE_H - (LV_CLAMP(T_MIN, t, T_MAX) - T_MIN) / (T_MAX - T_MIN) * CUVE_H); }

static void placer_curseur()
{
    const lv_coord_t yc = CUVE_Y + y_de(bc_etat.consigne);
    lv_obj_set_y(o_trait, yc - 1);
    lv_obj_set_y(o_bouton, yc - 15);
    ui_label_texte_fmt(o_rconsigne, "%d", (int)bc_etat.consigne);
}

static void rafraichir_reglages()
{
    cuve_niveau(niveau_reglages());
    if (!g_glisse)
        placer_curseur();
    const bool arret = bc_etat.mode == BC_ARRET;
    ui_style_text_color(o_rconsigne, ui_c(arret ? UI_TEXTE3 : UI_ACCENT), 0);
    ui_label_texte_fmt(o_reau, "%d °C", (int)(bc_etat.temp + 0.5f));
    for (int m = 0; m < 3; m++)
        if (bc_etat.mode == m)
            lv_obj_add_state(o_modes[m], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_modes[m], LV_STATE_CHECKED);
    if (bc_etat.eco)
        lv_obj_add_state(o_eco_inter, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(o_eco_inter, LV_STATE_CHECKED);
}

// glisser sur la cuve (ou le bouton rond) : la consigne suit le doigt au degré près ; envoyée au relâchement
static void curseur_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING)
    {
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        lv_area_t z;
        lv_obj_get_coords(g_zone, &z);
        const float t = T_MIN + (float)(CUVE_Y + CUVE_H - (p.y - z.y1)) / CUVE_H * (T_MAX - T_MIN);
        const int c = LV_CLAMP(C_MIN, (int)(t + 0.5f), C_MAX);
        g_glisse = true;
        if (c != (int)bc_etat.consigne)
        {
            bc_etat.consigne = c;
            placer_curseur(); // pendant le glissé, seuls le trait, le bouton et le chiffre bougent
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        g_glisse = false;
        envoyer();
    }
}

static void mode_cb(lv_event_t *e)
{
    bc_etat.mode = (bc_mode_t)(intptr_t)lv_event_get_user_data(e);
    rafraichir_reglages();
    envoyer();
}

static void eco_cb(lv_event_t *)
{
    bc_etat.eco = !bc_etat.eco;
    rafraichir_reglages();
    envoyer();
}

static void construire_reglages(lv_obj_t *z)
{
    for (int tg = T_MIN; tg <= T_MAX; tg += 10)
    {
        lv_obj_t *l = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE3, "");
        ui_label_texte_fmt(l, "%d°", tg);
        lv_obj_set_pos(l, 6, CUVE_Y + y_de(tg) - 7 + (tg == T_MAX ? 6 : tg == T_MIN ? -6 : 0));
    }
    cuve_creer(z, niveau_reglages());
    o_trait = ui_conteneur(z);
    lv_obj_set_size(o_trait, CUVE_L + 10, 3);
    ui_style_bg_color(o_trait, ui_c(UI_ACCENT), 0);
    ui_style_bg_opa(o_trait, LV_OPA_COVER, 0);
    lv_obj_set_x(o_trait, CUVE_X);
    o_bouton = ui_conteneur(z);
    lv_obj_set_size(o_bouton, 30, 30);
    lv_obj_set_style_radius(o_bouton, LV_RADIUS_CIRCLE, 0);
    ui_style_bg_color(o_bouton, ui_c(UI_ACCENT), 0);
    ui_style_bg_opa(o_bouton, LV_OPA_COVER, 0);
    lv_obj_set_x(o_bouton, CUVE_X + CUVE_L + 4);
    // zone tactile : la cuve et le bouton (comme l'arc d'un anneau, elle garde le glissé pour elle)
    lv_obj_t *tactile = ui_conteneur(z);
    lv_obj_set_size(tactile, CUVE_L + 50, CUVE_H + 30);
    lv_obj_set_pos(tactile, CUVE_X - 6, CUVE_Y - 15);
    lv_obj_add_flag(tactile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tactile, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(tactile, curseur_cb, LV_EVENT_ALL, NULL);

    // à droite, comme la température dans Ballon : la consigne en grand
    lv_obj_align(ui_titre_section(z, TXT("CONSIGNE", "SETPOINT")), LV_ALIGN_TOP_RIGHT, -UI_MARGE, 56);
    lv_obj_t *r = ui_valeur_unite(z, UI_POLICE_TEMP, UI_ACCENT, "°C", &o_rconsigne);
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 78);
    lv_obj_t *aide = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE3, TXT("glisser le curseur\nsur le ballon", "drag the slider\non the tank"));
    lv_obj_set_style_text_align(aide, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(aide, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 186);
    trait_h(z, 232);
    ligne_info(z, 244, TXT("Eau", "Water"), &o_reau);
    lv_obj_t *v;
    ligne_info(z, 270, TXT("Contenance", "Capacity"), &v);
    ui_label_texte_fmt(v, "%d L", VOLUME);
    ligne_info(z, 296, TXT("Chauffe", "Heating"), &v);
    ui_label_texte(v, TXT("relais tableau", "panel relay"));

    static const char *NOMS_MODE[] = {TXT("Arrêt", "Off"), TXT("Auto", "Auto"), TXT("Forcé", "Boost")};
    lv_obj_t *rang = ui_conteneur(z);
    lv_obj_set_size(rang, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(rang, UI_MARGE, 334);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int m = 0; m < 3; m++)
    {
        lv_obj_t *b = ui_bouton(rang, 120, 52);
        o_modes[m] = b;
        lv_obj_t *l = lv_label_create(b);
        ui_label_texte(l, NOMS_MODE[m]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)m);
    }

    // régime éco : toute la carte est le bouton, l'interrupteur ne fait que montrer l'état
    lv_obj_t *eco = ui_bouton(z, UI_ZONE_L - 2 * UI_MARGE, 56); // ligne à interrupteur : 56 px partout (harmonie)
    lv_obj_set_pos(eco, UI_MARGE, 398);
    lv_obj_set_style_pad_hor(eco, 16, 0);
    lv_obj_set_flex_flow(eco, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(eco, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *txt = ui_conteneur(eco);
    lv_obj_clear_flag(txt, LV_OBJ_FLAG_CLICKABLE); // le toucher doit arriver à la carte, pas au bloc de texte
    lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    ui_label(txt, UI_POLICE_TEXTE, UI_TEXTE, TXT("Régime éco", "Eco schedule"));
    ui_label(txt, UI_POLICE_LEGENDE, UI_TEXTE2, TXT("une seule montée la nuit, en heures creuses",
                                                  "one heat-up per night, off-peak"));
    o_eco_inter = lv_switch_create(eco);
    lv_obj_set_size(o_eco_inter, 52, 30);
    lv_obj_clear_flag(o_eco_inter, LV_OBJ_FLAG_CLICKABLE);
    ui_interrupteur_couleurs(o_eco_inter);
    lv_obj_add_event_cb(eco, eco_cb, LV_EVENT_CLICKED, NULL);
    g_glisse = false;
}

// =====================================================================  PAGE
static void rafraichir()
{
    if (!g_zone)
        return;
    if (g_vue == 0)
        rafraichir_ballon();
    else if (g_vue == 1)
        rafraichir_courbe(false);
    else
        rafraichir_reglages();
}

void page_ballon_maj() { rafraichir(); }

void page_ballon_historique_maj()
{
    if (g_zone && g_vue == 1)
        rafraichir_courbe(true);
}

static void arreter_bulles()
{
    if (g_minuterie)
        lv_timer_del(g_minuterie);
    g_minuterie = nullptr;
    o_cuve = nullptr;
}

static void construire();
static void onglet_cb(lv_event_t *e)
{
    const int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v == g_vue)
        return;
    g_vue = v;
    construire();
}

static void construire()
{
    arreter_bulles();
    lv_obj_clean(g_zone);
    static const char *const ONGLETS[] = {TXT("Ballon", "Tank"), TXT("Courbe", "Chart"), TXT("Réglages", "Settings")};
    ui_onglets(g_zone, ONGLETS, 3, g_vue, onglet_cb, nullptr, o_onglets);
    if (g_vue == 0)
        construire_ballon(g_zone);
    else if (g_vue == 1)
        construire_courbe(g_zone);
    else
        construire_reglages(g_zone);
    rafraichir();
    if (g_vue == 1)
        lv_obj_invalidate(o_graphe);
}

void page_ballon_creer(lv_obj_t *zone)
{
    g_zone = zone;
    g_vue = 0;
    construire();
}

void page_ballon_quitter()
{
    arreter_bulles();
    g_zone = nullptr; // rafraichir() ne touche plus à rien
    g_glisse = false;
}
