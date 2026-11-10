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
static float y_exact(const uc_cadre_t &c, float v) { return c.bas - (LV_CLAMP(c.lo, v, c.hi) - c.lo) / (c.hi - c.lo) * (c.bas - c.haut); }

#if LVGL_VERSION_MAJOR >= 9
// ---- 05/11/2026 (étape 2-vague B) : uc_trait et uc_degrade n'écrivent plus dans le tampon, ils créent des tasks.
// En 8.4 ces deux rendus allaient pixel par pixel dans ctx->buf / ctx->clip_area / ctx->buf_area (c'était LE gain de
// vitesse : 270 ms -> quelques ms, voir le commentaire d'origine ci-dessous). En 9.6 ce n'est plus faisable :
//   1) le contexte n'expose plus de tampon : une layer a draw_buf + buf_area + _clip_area (draw/lv_draw.h:98-134),
//      format variable (layer->color_format) et tampon morcelé ;
//   2) surtout le RENDU n'est pas synchrone : lv_refr.c empile les tasks de toute la zone (lv_obj_redraw,
//      core/lv_refr.c:128-133), pose layer->all_tasks_added (core/lv_refr.c:946) puis seulement dispatche (982-983).
//      Avec LV_USE_OS = LV_OS_NONE (conf 9.6 hors dépôt, lv_conf.h:104) rien n'est rendu pendant la création des
//      tasks : une écriture directe dans le tampon pendant DRAW_MAIN serait donc effacée par le fond de la carte,
//      dont la task de remplissage a été créée avant la nôtre.
// Le calcul reste IDENTIQUE (mêmes colonnes couvertes, mêmes lignes, mêmes parts de pixel), mais exprimé en
// lv_draw_fill : une task par plage de colonnes consécutives qui donnent exactement le même haut/bas (cas des
// données plates), plus une task par bord partiel. NOMBRE DE TASKS DONC DÉPEND DE LA COURBE (pire cas : une colonne
// = une plage = 3 tasks, ~1000 tasks par courbe ; lv_draw_add_task cherche le bout de la liste à chaque ajout,
// src/draw/lv_draw.c:112-121, donc coût en n²/2) -> À MESURER sur l'écran, c'est le principal risque de
// régression de cette étape. Le seul moyen de retrouver la vitesse 8.4 serait une unité de dessin personnalisée
// (lv_draw_create_unit, draw/lv_draw.h:150) — hors de portée ici, elle demande lv_draw_private.h (non public).
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
#else
// 25/10 (retour d'usage : « l'aliasing sur le haut du graphe ») : la courbe était tracée en segments LVGL entre points arrondis au
// pixel (288 points sur ~350 px : des segments d'un pixel, que le lissage de LVGL ne rattrape pas -> marches). Elle est
// maintenant dessinée colonne par colonne depuis sa position exacte : chaque colonne couvre le trait sur son épaisseur et
// jusqu'à mi-chemin des colonnes voisines (pentes raides continues), bords lissés selon la part de pixel couverte.
void uc_trait(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur, lv_coord_t largeur)
{
    lv_draw_ctx_t *ctx = c.ctx;
    const lv_area_t &clip = *ctx->clip_area, &zone = *ctx->buf_area;
    lv_color_t *buf = (lv_color_t *)ctx->buf;
    const lv_coord_t larg = lv_area_get_width(&zone);
    const lv_color_t col = ui_c(couleur);
    const float demi = largeur / 2.0f;
    const lv_coord_t xa = LV_MAX(c.x0, clip.x1), xb = LV_MIN(c.x1, clip.x2);
    for (lv_coord_t x = xa; x <= xb; x++)
    {
        float v, vg, vd;
        if (!valeur_en_x(c, s, x, v))
            continue;
        const float y = y_exact(c, v);
        float haut = y, bas = y;
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
        const lv_coord_t ya = LV_MAX((lv_coord_t)haut, clip.y1), yb = LV_MIN((lv_coord_t)bas, clip.y2);
        lv_color_t *px = buf + (lv_coord_t)(ya - zone.y1) * larg + (x - zone.x1);
        for (lv_coord_t yy = ya; yy <= yb; yy++, px += larg)
        {
            const float couvert = LV_MIN(yy + 1.0f, bas) - LV_MAX((float)yy, haut); // part du pixel dans le trait
            if (couvert >= 0.99f)
                *px = col;
            else if (couvert > 0.02f)
                *px = lv_color_mix(col, *px, (uint8_t)(couvert * 255));
        }
    }
}
#endif

// Valeur interpolée sous la colonne de pixels x (false : trou de mesure de part et d'autre)
static bool valeur_en_x(const uc_cadre_t &c, const uc_serie_t &s, lv_coord_t x, float &v)
{
    const float f = c.premier + (float)(x - c.x0) * (c.n - 1) / (c.x1 - c.x0);
    const int i0 = LV_CLAMP(c.premier, (int)f, c.premier + c.n - 1), i1 = LV_MIN(i0 + 1, c.premier + c.n - 1);
    if (s.trou(i0) || s.trou(i1))
        return false;
    const float k = f - i0;
    v = s.lire(i0) * (1 - k) + s.lire(i1) * k;
    return true;
}

#if LVGL_VERSION_MAJOR >= 9
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
#else
// 25/10 (retour d'usage : « page graphe lente, quel est le consommateur ? ») : mesuré sur Chambre 1, ce dégradé prenait 86 % du
// dessin du graphe (~270 ms sur ~310 par image : un rectangle en dégradé LVGL par colonne, refait à chaque bande de 80
// lignes). Les pixels sont maintenant calculés directement dans le tampon de dessin, même rendu (couleur de la courbe
// estompée en haut de chaque colonne -> fond en bas) ; le pixel du haut est mélangé selon la position exacte de la courbe
// (« l'aliasing sur le haut du graphe » : le remplissage commençait sur un pixel entier, en marches).
void uc_degrade(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur)
{
    lv_draw_ctx_t *ctx = c.ctx;
    const lv_area_t &clip = *ctx->clip_area, &zone = *ctx->buf_area;
    lv_color_t *buf = (lv_color_t *)ctx->buf;
    const lv_coord_t larg = lv_area_get_width(&zone);
    const lv_color_t haut_c = lv_color_mix(ui_c(couleur), ui_c(UI_FOND), 105), fond = ui_c(UI_FOND);
    const lv_coord_t xa = LV_MAX(c.x0, clip.x1), xb = LV_MIN(c.x1, clip.x2);
    float v;
    for (lv_coord_t x = xa; x <= xb; x++)
    {
        if (!valeur_en_x(c, s, x, v))
            continue;
        const float yf = c.bas - (LV_CLAMP(c.lo, v, c.hi) - c.lo) / (c.hi - c.lo) * (c.bas - c.haut); // haut exact
        const float h = LV_MAX(c.bas - yf, 1.0f);
        const lv_coord_t y0 = (lv_coord_t)yf; // pixel du haut, partiellement couvert
        const lv_coord_t ya = LV_MAX(y0, clip.y1), yb = LV_MIN(c.bas, clip.y2);
        lv_color_t *px = buf + (lv_coord_t)(ya - zone.y1) * larg + (x - zone.x1);
        for (lv_coord_t y = ya; y <= yb; y++, px += larg)
        {
            const float k = LV_CLAMP(0.0f, (y - yf) / h, 1.0f); // 0 en haut, 1 en bas
            const lv_color_t col = lv_color_mix(fond, haut_c, (uint8_t)(k * 255));
            if (y == y0) // couvert de (y0 + 1 - yf)
                *px = lv_color_mix(col, *px, (uint8_t)((y0 + 1 - yf) * 255));
            else
                *px = col;
        }
    }
}
#endif

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

int uc_point_du_doigt(const uc_cadre_t &c)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    const int i = c.premier + (int)lroundf((float)(p.x - c.x0) * (c.n - 1) / (c.x1 - c.x0));
    return LV_CLAMP(c.premier, i, c.premier + c.n - 1);
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
