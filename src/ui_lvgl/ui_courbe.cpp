#include "ui_courbe.h"
#include "ui_theme.h"
#include <math.h>
#include <stdio.h>

static lv_draw_line_dsc_t trait_dsc(uint32_t couleur, lv_coord_t largeur)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = ui_c(couleur);
    d.width = largeur;
    d.round_start = d.round_end = 1;
    return d;
}

void uc_grille(const uc_cadre_t &c, float pas, const char *unite)
{
    lv_draw_line_dsc_t ld = trait_dsc(UC_GRILLE, 1);
    ld.round_start = ld.round_end = 0;
    lv_draw_label_dsc_t td;
    lv_draw_label_dsc_init(&td);
    td.font = UI_POLICE_LEGENDE;
    td.color = ui_c(UI_TEXTE3);
    td.align = LV_TEXT_ALIGN_RIGHT;
    char buf[12];
    for (float v = ceilf(c.lo / pas) * pas; v <= c.hi + 0.001f; v += pas)
    {
        uc_segment(c.ctx, ld, c.x0, c.y(v), c.x1, c.y(v));
        snprintf(buf, sizeof buf, "%g%s", v, unite);
        lv_area_t ta = {(lv_coord_t)(c.x0 - 44), (lv_coord_t)(c.y(v) - 7), (lv_coord_t)(c.x0 - 5), (lv_coord_t)(c.y(v) + 8)};
        uc_texte(c.ctx, td, &ta, buf, true); // « buf » est local : en 9.6 la task est rendue après, LVGL doit le copier
    }
}

static int minute_du_point(int i, int dernier, int minute_fin, int mpp)
{
    return ((minute_fin - (dernier - i) * mpp) % 1440 + 1440) % 1440;
}

void uc_heures(const uc_cadre_t &c, int minute_fin, int mpp, int ecart)
{
    if (minute_fin < 0)
        return; // heure inconnue : pas de repères plutôt que des faux
    lv_draw_line_dsc_t ld = trait_dsc(UC_GRILLE, 1);
    ld.round_start = ld.round_end = 0;
    lv_draw_label_dsc_t td;
    lv_draw_label_dsc_init(&td);
    td.font = UI_POLICE_LEGENDE;
    td.color = ui_c(UI_TEXTE3);
    td.align = LV_TEXT_ALIGN_CENTER;
    const int dernier = c.premier + c.n - 1;
    char buf[8];
    for (int i = c.premier; i <= dernier; i++)
    {
        const int m = minute_du_point(i, dernier, minute_fin, mpp);
        if (m % ecart >= mpp) // le point qui tombe sur le repère (ou juste après)
            continue;
        const int r = m - m % ecart;
        uc_segment(c.ctx, ld, c.x(i), c.haut, c.x(i), c.bas);
        if (c.x(i) > c.x1 - 18 || c.x(i) < c.x0 + 12)
            continue; // étiquette coupée au bord : le trait suffit
        if (r % 60)
            snprintf(buf, sizeof buf, TXT("%02dh%02d", "%02d:%02d"), r / 60, r % 60);
        else
            snprintf(buf, sizeof buf, TXT("%02dh", "%02d:00"), r / 60);
        lv_area_t ta = {(lv_coord_t)(c.x(i) - 22), (lv_coord_t)(c.bas + 5), (lv_coord_t)(c.x(i) + 22), (lv_coord_t)(c.bas + 20)};
        uc_texte(c.ctx, td, &ta, buf, true);
    }
}

static bool valeur_en_x(const uc_cadre_t &c, const uc_serie_t &s, lv_coord_t x, float &v);
static void uc_ancres(const uc_cadre_t &c, const uc_serie_t &s); // construit les ancres lissées (plus bas)
static float y_exact(const uc_cadre_t &c, float v) { return c.bas - (LV_CLAMP(c.lo, v, c.hi) - c.lo) / (c.hi - c.lo) * (c.bas - c.haut); }

// uc_trait et uc_degrade créent des tasks lv_draw_fill : en 9.6 le rendu n'est pas synchrone et le tampon
// n'est plus accessible — écrire « à la main » comme en 8.4 serait effacé par le fond de la carte. Une task
// par plage de colonnes donnant le même haut/bas + bords partiels ; le nombre de tasks dépend de la courbe
// (lv_draw_add_task coûte en n²/2 — risque mesuré depuis : acceptable).
static void plage_trait(uc_cible_t *ctx, lv_coord_t xa, lv_coord_t xb, float haut, float bas, lv_color_t col,
                        const lv_area_t &clip)
{
    const lv_coord_t ya = (lv_coord_t)haut, yb = (lv_coord_t)bas; // mêmes troncatures qu'en 8.4
    const lv_coord_t ca = LV_MAX(ya, clip.y1), cb = LV_MIN(yb, clip.y2);
    if (ca > cb || xa > xb)
        return;
    if (ya == yb) // une seule ligne, couverte de (bas - haut)
    {
        const float couvert = LV_MIN((float)(ya + 1), bas) - LV_MAX((float)ya, haut);
        if (couvert >= 0.99f)
            uc_aplat(ctx, {xa, ca, xb, cb}, col, LV_OPA_COVER);
        else if (couvert > 0.02f)
            uc_aplat(ctx, {xa, ca, xb, cb}, col, (lv_opa_t)(couvert * 255));
        return;
    }
    const float haut_c = LV_MIN((float)(ya + 1), bas) - haut; // part du pixel du haut (comme « couvert » en 8.4)
    if (ca == ya && haut_c < 0.99f && haut_c > 0.02f)
        uc_aplat(ctx, {xa, ya, xb, ya}, col, (lv_opa_t)(haut_c * 255));
    const lv_coord_t pa = LV_MAX(ya + 1, clip.y1), pb = LV_MIN(yb - 1, clip.y2);
    if (pa <= pb)
        uc_aplat(ctx, {xa, pa, xb, pb}, col, LV_OPA_COVER);
    const float bas_c = bas - LV_MAX((float)yb, haut); // part du pixel du bas
    if (cb == yb && bas_c < 0.99f && bas_c > 0.02f)
        uc_aplat(ctx, {xa, yb, xb, yb}, col, (lv_opa_t)(bas_c * 255));
}

void uc_trait(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur, lv_coord_t largeur)
{
    uc_ancres(c, s); // lissage des paliers (valeur_en_x lit les ancres construites ici)
    const lv_color_t col = ui_c(couleur);
    const lv_area_t &clip = *uc_clip(c.ctx);
    const float demi = largeur / 2.0f;
    const lv_coord_t xa = LV_MAX(c.x0, clip.x1), xb = LV_MIN(c.x1, clip.x2);
    lv_coord_t rg_a = 0; // début de la plage en cours
    float rg_haut = 0, rg_bas = 0;
    bool rg = false; // une plage est-elle en cours ? (x peut valoir 0 : on ne peut pas s'en servir de sentinelle)
    for (lv_coord_t x = xa; x <= xb + 1; x++)
    {
        float haut = 0, bas = 0;
        bool ok = false;
        if (x <= xb)
        {
            float v, vg, vd;
            if (valeur_en_x(c, s, x, v))
            {
                const float y = y_exact(c, v);
                haut = bas = y;
                if (x > c.x0 && valeur_en_x(c, s, x - 1, vg))
                {
                    const float m = (y + y_exact(c, vg)) / 2;
                    haut = LV_MIN(haut, m), bas = LV_MAX(bas, m);
                }
                if (x < c.x1 && valeur_en_x(c, s, x + 1, vd))
                {
                    const float m = (y + y_exact(c, vd)) / 2;
                    haut = LV_MIN(haut, m), bas = LV_MAX(bas, m);
                }
                haut -= demi, bas += demi;
                ok = true;
            }
        }
        if (rg && (!ok || haut != rg_haut || bas != rg_bas))
        {
            plage_trait(c.ctx, rg_a, x - 1, rg_haut, rg_bas, col, clip);
            rg = false;
        }
        if (ok && !rg)
            rg_a = x, rg_haut = haut, rg_bas = bas, rg = true;
    }
}

// ---- 13/11/2026 : lissage des paliers (« effet escalier » de la courbe de pièce) --------------------
// La sonde de la chambre ne remonte un changement (0,1 à 0,3 °C) qu'une fois par heure environ : les
// données enchaînent 10-12 points identiques puis un saut (mesuré sur le message HA réel du 13/11).
// L'interpolation brute dessinait des marches : palier horizontal, saut vertical. La VALEUR est
// quantifiée, pas le phénomène : quand deux paliers LONGS (>= UC_PALIER_MIN points) sont contigus et
// séparés par un petit saut (<= 3 unités brutes : 0,3 °C en x10, 3 % en humidité), on les relie par
// leurs MILIEUX et la dérive lente devient une pente. Reste aux points mesurés : paliers courts
// (1-2 points = mouvement réel, chauffe qui démarre), gros sauts (>= 0,4 °C, fenêtre ouverte), trous
// (-999 : colonne touchant un trou non peinte, comme avant), pastille du bout et curseur (inchangés,
// ils lisent la valeur brute). Construit une fois par appel de uc_trait / uc_degrade ; au plus une
// ancre par point, tampon statique (le graphe et le ballon ont 288 points, les autres pages moins).
#define UC_PALIER_MIN 3  // palier « long » : au moins 3 points identiques de suite
#define UC_PETIT_SAUT 3  // petit saut entre paliers : 3 unités de la série (0,3 °C x10, 3 %)
#define UC_ANCRE_MAX 288 // plus grande série dessinée (GR_POINTS) ; au-delà : interpolation brute
static float s_ax[UC_ANCRE_MAX];   // ancres : abscisses en unités de point (demi-points possibles)
static int16_t s_av[UC_ANCRE_MAX]; // valeurs brutes (avant diviseur)
static int s_an = 0;               // 0 : pas d'ancres -> interpolation brute (comme avant)

static int uc_brut(const uc_serie_t &s, int i)
{
    return s.octets ? (int)((const uint8_t *)s.donnees)[i] : (int)((const int16_t *)s.donnees)[i];
}

static void uc_ancres(const uc_cadre_t &c, const uc_serie_t &s)
{
    s_an = 0;
    const int fin = c.premier + c.n - 1;
    if (c.n < 2 || c.n > UC_ANCRE_MAX)
        return;
    int a0 = c.premier;
    int pv = 0;     // valeur du run précédent (adjacent par construction)
    bool p_long = false; // run précédent : non trou ET long
    while (a0 <= fin)
    {
        const bool vide = s.trou(a0);
        const int v = uc_brut(s, a0);
        int a1 = a0;
        while (a1 + 1 <= fin && s.trou(a1 + 1) == vide && (vide || uc_brut(s, a1 + 1) == v))
            a1++;
        // run suivant en avant : valeur et longueur (un trou casse la chaîne des paliers)
        int nv = 0;
        bool n_long = false;
        if (a1 + 1 <= fin && !s.trou(a1 + 1))
        {
            nv = uc_brut(s, a1 + 1);
            int b1 = a1 + 1;
            while (b1 + 1 <= fin && !s.trou(b1 + 1) && uc_brut(s, b1 + 1) == nv)
                b1++;
            n_long = (b1 - a1 >= UC_PALIER_MIN);
        }
        if (!vide)
        {
            const bool long_l = (a1 - a0 + 1 >= UC_PALIER_MIN);
            const bool a_gauche = long_l && p_long && abs(v - pv) <= UC_PETIT_SAUT;
            const bool a_droite = long_l && n_long && abs(v - nv) <= UC_PETIT_SAUT;
            const float milieu = 0.5f * (float)(a0 + a1);
            float xs[2] = {a_gauche ? milieu : (float)a0, a_droite ? milieu : (float)a1};
            const int nx = (xs[0] == xs[1]) ? 1 : 2; // point unique (palier court d'un point, milieu double)
            if (s_an + nx > UC_ANCRE_MAX)
            {
                s_an = 0; // ne devrait pas arriver (ancres <= points) : repli sur l'interpolation brute
                return;
            }
            for (int k = 0; k < nx; k++)
                s_ax[s_an + k] = xs[k], s_av[s_an + k] = (int16_t)v;
            s_an += nx;
        }
        p_long = (!vide) && (a1 - a0 + 1 >= UC_PALIER_MIN);
        pv = v;
        a0 = a1 + 1;
    }
}

// Valeur interpolée sous la colonne de pixels x (false : trou de mesure de part et d'autre)
static bool valeur_en_x(const uc_cadre_t &c, const uc_serie_t &s, lv_coord_t x, float &v)
{
    const float f = c.premier + (float)(x - c.x0) * (c.n - 1) / (c.x1 - c.x0);
    const int i0 = LV_CLAMP(c.premier, (int)f, c.premier + c.n - 1), i1 = LV_MIN(i0 + 1, c.premier + c.n - 1);
    if (s.trou(i0) || s.trou(i1))
        return false;
    if (s_an >= 2)
    {
        // segment d'ancres [j, j+1] contenant f (abscisses strictement croissantes, bornées par
        // premier et fin : toute colonne est dans un segment)
        int j = 0, k = s_an - 1;
        while (j < k)
        {
            const int m = (j + k + 1) >> 1;
            if (s_ax[m] <= f)
                j = m;
            else
                k = m - 1;
        }
        if (j >= s_an - 1)
            v = (float)s_av[s_an - 1] / s.diviseur;
        else
        {
            const float dx = s_ax[j + 1] - s_ax[j];
            const float kk = dx > 0.0001f ? (f - s_ax[j]) / dx : 0.0f;
            v = ((float)s_av[j] + ((float)s_av[j + 1] - (float)s_av[j]) * kk) / s.diviseur;
        }
        return true;
    }
    const float k = f - i0; // pas d'ancres (construction non faite ou série trop longue) : comme avant
    v = s.lire(i0) * (1 - k) + s.lire(i1) * k;
    return true;
}

// 9.6 : même calcul que la version 8.4 ci-dessous (qui écrivait dans le tampon), rendu par des tasks de remplissage.
// Une plage = des colonnes consécutives dont le HAUT tombe exactement au même endroit (données plates) : une task de
// dégradé + une task pour la ligne du haut partiellement couverte. Le dégradé est calé sur la première ligne
// entièrement couverte (y0+1) alors qu'en 8.4 il partait du haut exact yf : écart < 1 pixel de rampe, soit < 0,5 % de
// la couleur (non visible, mais écrit ici plutôt que noyé). Voir le bloc de commentaires de uc_trait.
static void plage_degrade(uc_cible_t *ctx, lv_coord_t xa, lv_coord_t xb, float yf, lv_coord_t bas, lv_color_t haut_c,
                          lv_color_t fond)
{
    if (xa > xb || bas <= yf)
        return;
    const lv_coord_t y0 = (lv_coord_t)yf; // même troncature qu'en 8.4
    const float couvert = y0 + 1.0f - yf; // part de la ligne du haut (8.4 : « if (y == y0) »)
    if (couvert > 0.02f)
        uc_aplat(ctx, {xa, y0, xb, y0}, haut_c, (lv_opa_t)(couvert * 255)); // sa couleur : k écrasé à 0, comme en 8.4
    if (bas >= y0 + 1)
        uc_aplat_degrade(ctx, {xa, (lv_coord_t)(y0 + 1), xb, bas}, haut_c, fond); // LVGL rogne la task lui-même
}

void uc_degrade(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur)
{
    uc_ancres(c, s); // dégradé sous la MÊME courbe lissée que uc_trait (mêmes ancres)
    const lv_color_t haut_c = lv_color_mix(ui_c(couleur), ui_c(UI_FOND), 105), fond = ui_c(UI_FOND);
    const lv_area_t &clip = *uc_clip(c.ctx);
    const lv_coord_t xa = LV_MAX(c.x0, clip.x1), xb = LV_MIN(c.x1, clip.x2);
    lv_coord_t rg_a = 0;
    float rg_yf = 0, v;
    bool rg = false; // (x peut valoir 0 : on ne peut pas s'en servir de sentinelle)
    for (lv_coord_t x = xa; x <= xb + 1; x++)
    {
        const bool ok = x <= xb && valeur_en_x(c, s, x, v);
        const float yf = ok ? y_exact(c, v) : 0.0f; // « haut exact » de la colonne, comme en 8.4
        if (rg && (!ok || yf != rg_yf))
        {
            plage_degrade(c.ctx, rg_a, x - 1, rg_yf, c.bas, haut_c, fond);
            rg = false;
        }
        if (ok && !rg)
            rg_a = x, rg_yf = yf, rg = true;
    }
}

void uc_principale(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur)
{
    uc_trait(c, s, couleur, 3);
    const int dernier = c.premier + c.n - 1;
    if (!s.trou(dernier)) // point au bout : la valeur de maintenant
    {
        lv_draw_rect_dsc_t pd;
        lv_draw_rect_dsc_init(&pd);
        pd.radius = LV_RADIUS_CIRCLE;
        pd.bg_color = ui_c(couleur);
        pd.border_color = ui_c(UI_FOND);
        pd.border_width = 2;
        const lv_coord_t x = c.x(dernier), y = c.y(s.lire(dernier));
        lv_area_t pa = {(lv_coord_t)(x - 5), (lv_coord_t)(y - 5), (lv_coord_t)(x + 5), (lv_coord_t)(y + 5)};
        uc_carre(c.ctx, pd, &pa);
    }
}

// pointillé clair (le pointillé LVGL ne marche que sur l'horizontal et le vertical : parfait pour un escalier.
// 9.6 : dash_width/dash_gap sont toujours dans le descripteur (draw/lv_draw_line.h:60-64) et le rendu sw ne les
// applique que sur les lignes horizontales (src/draw/sw/lv_draw_sw_line.c:139) et verticales (220) : comme en 8.4)
static lv_draw_line_dsc_t pointille(uint32_t couleur, bool seuil)
{
    lv_draw_line_dsc_t d = trait_dsc(couleur, seuil ? 1 : 2);
    d.round_start = d.round_end = 0;
    d.color = lv_color_mix(ui_c(couleur), ui_c(UI_FOND), seuil ? 200 : 190);
    d.dash_width = seuil ? 2 : 5;
    d.dash_gap = 4;
    return d;
}

void uc_escalier(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur)
{
    lv_draw_line_dsc_t d = pointille(couleur, false);
    const int fin = c.premier + c.n - 1;
    int debut = -1;
    for (int i = c.premier; i <= fin; i++)
    {
        if (s.trou(i))
        {
            debut = -1;
            continue;
        }
        if (debut < 0)
            debut = i;
        if (i == fin || s.trou(i + 1) || s.lire(i + 1) != s.lire(i))
        {
            uc_segment(c.ctx, d, c.x(debut), c.y(s.lire(i)), c.x(i < fin ? i + 1 : i), c.y(s.lire(i)));
            if (i < fin && !s.trou(i + 1))
                uc_segment(c.ctx, d, c.x(i < fin ? i + 1 : i), c.y(s.lire(i)), c.x(i < fin ? i + 1 : i), c.y(s.lire(i + 1)));
            debut = i + 1;
        }
    }
}

void uc_niveau(const uc_cadre_t &c, float v, uint32_t couleur, bool seuil)
{
    lv_draw_line_dsc_t d = pointille(couleur, seuil);
    uc_segment(c.ctx, d, c.x0, c.y(v), c.x1, c.y(v));
}

void uc_bandes(const uc_cadre_t &c, bool (*actif)(int i), uint32_t couleur)
{
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = ui_c(couleur);
    rd.bg_opa = 34;
    int debut = -1;
    const int fin = c.premier + c.n;
    for (int i = c.premier; i <= fin; i++)
    {
        const bool a = i < fin && actif(i);
        if (a && debut < 0)
            debut = i;
        if (!a && debut >= 0)
        {
            lv_area_t ra = {c.x(debut), c.haut, c.x(i - 1), c.bas};
            uc_carre(c.ctx, rd, &ra);
            debut = -1;
        }
    }
}

void uc_curseur(const uc_cadre_t &c, int i, const uc_serie_t &s, uint32_t couleur)
{
    lv_draw_line_dsc_t d = trait_dsc(UI_TEXTE, 1);
    d.opa = 150;
    const lv_coord_t px = c.x(i);
    uc_segment(c.ctx, d, px, c.haut, px, c.bas);
    if (s.trou(i))
        return;
    lv_draw_rect_dsc_t pd;
    lv_draw_rect_dsc_init(&pd);
    pd.radius = LV_RADIUS_CIRCLE;
    pd.bg_color = ui_c(couleur);
    pd.border_color = ui_c(UI_FOND);
    pd.border_width = 2;
    const lv_coord_t y = c.y(s.lire(i));
    lv_area_t pa = {(lv_coord_t)(px - 6), (lv_coord_t)(y - 6), (lv_coord_t)(px + 6), (lv_coord_t)(y + 6)};
    uc_carre(c.ctx, pd, &pa);
}

int uc_point_sans_doigt(const uc_cadre_t &c, int x)
{
    const int i = c.premier + (int)lroundf((float)(x - c.x0) * (c.n - 1) / (c.x1 - c.x0));
    return LV_CLAMP(c.premier, i, c.premier + c.n - 1);
}

int uc_point_du_doigt(const uc_cadre_t &c)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    return uc_point_sans_doigt(c, p.x);
}

// Machine à états du glissé (D1a) : le doigt pose/déplace le curseur, la release le retire.
// À la release, pas de garde « curseur posé » : LVGL envoie toujours RELEASED après PRESSED, le curseur
// est donc toujours posé à ce moment-là (le hook de banc, lui, peut recevoir un relâcher sans pas avant).
void uc_lecture_doigt(lv_event_t *e, const uc_lecture_t &L)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING)
    {
        const int i = uc_point_du_doigt(L.cadre());
        if (i != *L.scrub)
        {
            const int avant = *L.scrub;
            *L.scrub = i;
            // premier appui (avant < 0) ou chemin léger refusé -> chemin complet ; sinon léger
            if (avant >= 0 && L.leger_ok(avant))
                L.leger(avant);
            else
                L.complet();
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        *L.scrub = -1;
        L.complet();
    }
}

// Pas du banc (hook ESP sans indev) : même branchement que le doigt — le banc mesure le chemin réel du geste.
void uc_lecture_banc(int x_doigt, const uc_lecture_t &L)
{
    if (x_doigt < 0) // relâcher, comme LV_EVENT_RELEASED / PRESS_LOST — avec garde : le banc peut relâcher sans pas avant
    {
        if (*L.scrub != -1)
        {
            *L.scrub = -1;
            L.complet();
        }
        return;
    }
    const int borne = uc_point_sans_doigt(L.cadre(), x_doigt);
    if (borne != *L.scrub)
    {
        const int avant = *L.scrub;
        *L.scrub = borne;
        if (avant >= 0 && L.leger_ok(avant))
            L.leger(avant);
        else
            L.complet();
    }
}

void uc_min_max(const int16_t *valeurs, int debut, int n, int16_t sentinelle, int *imin, int *imax)
{
    *imin = *imax = -1;
    for (int k = debut; k < debut + n; k++)
        if (valeurs[k] != sentinelle)
        {
            if (*imin < 0 || valeurs[k] < valeurs[*imin])
                *imin = k;
            if (*imax < 0 || valeurs[k] > valeurs[*imax])
                *imax = k;
        }
}

void uc_repeindre_bandes(const uc_cadre_t &c, lv_obj_t *o, int i_avant, int i_apres)
{
    for (int k = 0; k < 2; k++)
    {
        const int j = k == 0 ? i_avant : i_apres;
        if (j < 0)
            continue;
        const lv_coord_t px = c.x(j);
        lv_area_t bande = {(lv_coord_t)(px - CURSEUR_BANDE), c.haut, (lv_coord_t)(px + CURSEUR_BANDE), c.bas};
        lv_obj_invalidate_area(o, &bande);
    }
}

void uc_texte_heure(char *buf, size_t n, int i, int dernier, int minute_fin, int mpp)
{
    if (minute_fin < 0)
    {
        snprintf(buf, n, TXT("il y a %d min", "%d min ago"), (dernier - i) * mpp);
        return;
    }
    const int m = minute_du_point(i, dernier, minute_fin, mpp);
    snprintf(buf, n, TXT("%s%02dh%02d", "%s%02d:%02d"),
             (dernier - i) * mpp > minute_fin ? TXT("hier ", "Yesterday ") : "", m / 60, m % 60);
}

lv_obj_t *uc_pastille(lv_obj_t *parent, const char *nom, uint32_t couleur, lv_coord_t largeur, lv_event_cb_t cb, int couche)
{
    lv_obj_t *b = ui_bouton(parent, largeur, 52);
    // allumée = carte + texte clair ; éteinte = fond + texte discret (pas l'accent : ce n'est pas un mode)
    lv_obj_set_style_bg_color(b, ui_c(UI_FOND), 0);
    lv_obj_set_style_border_color(b, ui_c(UI_CARTE), 0);
    lv_obj_set_style_text_color(b, ui_c(UI_TEXTE3), 0);
    lv_obj_set_style_bg_color(b, ui_c(UI_CARTE), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(b, ui_c(UI_BORD), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, ui_c(UI_TEXTE), LV_STATE_CHECKED);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 5, 0);
    lv_obj_t *trait = ui_conteneur(b);
    lv_obj_set_size(trait, 22, 3);
    lv_obj_set_style_radius(trait, 2, 0);
    lv_obj_set_style_bg_color(trait, ui_c(couleur), 0);
    lv_obj_set_style_bg_opa(trait, LV_OPA_COVER, 0);
    lv_obj_clear_flag(trait, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, nom);
    lv_obj_set_style_text_font(l, UI_POLICE_PETITE, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)couche);
    return b;
}

void uc_pastille_etat(lv_obj_t *p, bool allumee)
{
    if (allumee)
        lv_obj_add_state(p, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(p, LV_STATE_CHECKED);
}

lv_obj_t *uc_bulle(lv_obj_t *zone, lv_obj_t **texte)
{
    lv_obj_t *b = lv_obj_create(zone);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &ui_st_carte, 0);
    lv_obj_set_style_pad_all(b, 8, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE); // 9.6 : le drapeau est un type, deux drapeaux en « | » ne passent plus
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    *texte = ui_label(b, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_label_set_recolor(*texte, true);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}

void uc_bulle_placer(lv_obj_t *bulle, lv_coord_t x_point, lv_coord_t y, lv_coord_t largeur_zone)
{
    lv_obj_clear_flag(bulle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(bulle);
    const lv_coord_t l = lv_obj_get_width(bulle);
    lv_obj_set_pos(bulle, LV_CLAMP(4, x_point - l / 2, largeur_zone - 4 - l), y);
}

void uc_bulle_ecrire(lv_obj_t *bulle, lv_obj_t *texte, const char *msg, const uc_cadre_t &c, int i,
                     lv_obj_t *repere, lv_coord_t y, lv_coord_t largeur_zone)
{
    ui_label_texte(texte, msg);
    lv_area_t z;
    lv_obj_get_coords(repere, &z);
    uc_bulle_placer(bulle, c.x(i) - z.x1, y, largeur_zone);
}

void uc_bulle_poser(lv_obj_t *bulle, lv_obj_t *texte, const char *msg, const uc_cadre_t &c, int i,
                    lv_obj_t *repere, lv_coord_t y, lv_coord_t largeur_zone, lv_obj_t *graph)
{
    lv_area_t avant;
    lv_obj_get_coords(bulle, &avant);
    const bool etait_visible = !lv_obj_has_flag(bulle, LV_OBJ_FLAG_HIDDEN);
    uc_bulle_ecrire(bulle, texte, msg, c, i, repere, y, largeur_zone);
    if (etait_visible)
        lv_obj_invalidate_area(graph, &avant); // repeindre sous l'ancienne place de la bulle
}
