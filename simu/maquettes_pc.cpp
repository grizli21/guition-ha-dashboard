// 08/11/2026 — passage à LVGL 9.6 : ces maquettes dessinaient avec le contexte de dessin de la 8.4
// (lv_draw_ctx_t, lv_event_get_draw_ctx, géométrie de la ligne en arguments). Elles utilisent maintenant
// les wrappers déjà dans src/ui_lvgl/ui_courbe.h (uc_cible_du, uc_objet_du, uc_segment, uc_texte, uc_arc,
// uc_carre) : un seul code pour les deux versions, comme les pages.
#include "maquettes_pc.h"
#include "../src/ui_lvgl/ui_theme.h"
#include "../src/ui_lvgl/ui_courbe.h" // 08/11 : wrappers de dessin pour les deux versions de LVGL
#include <stdio.h>
#include <string.h>

// valeurs de la maquette : PC au repos avec un pic (2 min d'historique, un point toutes les 2 s) ; VRAM relevée au hub
// (:11500/stats) le 31/10 vers 13h, noms tirés de la ligne de commande (modèle .gguf, script python), couleurs du hub
static const int HIST = 150; // 5 min
static int g_hist[HIST];
static const int COEURS[16] = {68, 14, 9, 31, 6, 4, 22, 3, 12, 2, 5, 8, 1, 3, 17, 2};
static const int CPU = 13;

static uint32_t charge(int v) { return v < 40 ? UI_FROID : v < 75 ? UI_ACCENT : UI_ROUGE; }

static void rien_cb(lv_event_t *) {}

static lv_obj_t *fond(int onglet)
{
    lv_obj_t *f = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, UI_ZONE_L, UI_ZONE_H);
    ui_style_bg_color(f, ui_c(UI_FOND), 0);
    ui_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    static const char *const noms[] = {"Système", "GPU", "Processus", "IA"};
    ui_onglets(f, noms, 4, onglet, rien_cb, nullptr, nullptr);
    return f;
}

static lv_obj_t *carte(lv_obj_t *z, lv_coord_t y, lv_coord_t h)
{
    lv_obj_t *c = ui_conteneur(z);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, h);
    lv_obj_set_pos(c, UI_MARGE, y);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_set_style_pad_ver(c, 8, 0);
    return c;
}

static lv_obj_t *rect(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, uint32_t c, lv_coord_t r)
{
    lv_obj_t *o = ui_conteneur(p);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, r, 0);
    ui_style_bg_color(o, ui_c(c), 0);
    ui_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// barre horizontale fine (comme page_pc.cpp) : fond, partie pleine, et partie grise à la suite (cache)
static void barre(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t l, lv_coord_t h, int pct, uint32_t c, int pct2 = 0)
{
    lv_obj_t *b = rect(p, x, y, l, h, UI_CARTE2, h / 2);
    lv_obj_set_style_clip_corner(b, true, 0);
    rect(b, l * pct / 100, 0, l * pct2 / 100, h, 0x3A424D, 0);
    rect(b, 0, 0, l * pct / 100, h, c, 0);
}

// graphe de charge : quadrillage 25/50/75, aire pâle sous la courbe, courbe colorée par la charge du moment
static void graphe_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const lv_coord_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    auto x = [&](int i) { return (lv_coord_t)(a.x1 + (w - 1) * i / (HIST - 1)); };
    auto y = [&](int v) { return (lv_coord_t)(a.y2 - (h - 1) * v / 100); };

    lv_draw_line_dsc_t q;
    lv_draw_line_dsc_init(&q);
    q.color = ui_c(UI_BORD);
    q.width = 1;
    q.dash_width = 3;
    q.dash_gap = 3;
    for (int v = 25; v < 100; v += 25)
    {
        lv_point_t p1 = {a.x1, y(v)}, p2 = {a.x2, y(v)};
        uc_segment(ctx, q, p1.x, p1.y, p2.x, p2.y);
    }
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_20;
    for (int i = 0; i < HIST - 1; i++)
    {
        r.bg_color = ui_c(charge(g_hist[i + 1]));
        lv_area_t z = {(lv_coord_t)(x(i) + 1), y((g_hist[i] + g_hist[i + 1]) / 2), x(i + 1), a.y2};
        lv_draw_rect(ctx, &r, &z);
    }
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.width = 2;
    l.round_start = l.round_end = 1;
    for (int i = 0; i < HIST - 1; i++)
    {
        l.color = ui_c(charge(g_hist[i + 1]));
        lv_point_t p1 = {x(i), y(g_hist[i])}, p2 = {x(i + 1), y(g_hist[i + 1])};
        uc_segment(ctx, l, p1.x, p1.y, p2.x, p2.y);
    }
}

static void historique()
{
    for (int i = 0; i < HIST; i++)
    {
        int v = 8 + (i * 7) % 9;          // repos
        if (i >= 40 && i < 52)            // une génération de 25 s
            v = 55 + (i * 13) % 30;
        if (i >= 95 && i < 100)           // un pic de compilation
            v = 70 + (i * 11) % 25;
        if (i >= 120)
            v = 10 + (i * 5) % 8;
        g_hist[i] = v;
    }
    g_hist[HIST - 1] = CPU;
}

static lv_obj_t *graphe(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *g = rect(p, x, y, w, h, UI_FOND, 6);
    lv_obj_t *t = ui_conteneur(g);
    lv_obj_set_size(t, w - 8, h - 8);
    lv_obj_center(t);
    lv_obj_add_event_cb(t, graphe_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_t *e = ui_label(g, UI_POLICE_LEGENDE, UI_TEXTE3, "5 min");
    lv_obj_align(e, LV_ALIGN_TOP_LEFT, 6, 2);
    return g;
}

static void entete_cpu(lv_obj_t *c)
{
    lv_obj_t *n = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "CPU #6B7480 7800X3D#");
    lv_label_set_recolor(n, true);
    lv_obj_t *i = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "3,6 GHz · 56° · 40 W");
    lv_obj_align(i, LV_ALIGN_TOP_MID, 10, 3);
    char t[8];
    snprintf(t, sizeof t, "%d%%", CPU);
    lv_obj_align(ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, t), LV_ALIGN_TOP_RIGHT, 0, -2);
}

// bas de l'onglet Système, commun aux deux variantes : RAM (+ swap), réseau, disque, pied
static void bas_systeme(lv_obj_t *f, lv_coord_t y)
{
    const lv_coord_t L = UI_ZONE_L - 2 * UI_MARGE - 24;
    lv_obj_t *c = carte(f, y, 40);
    lv_obj_set_style_pad_ver(c, 0, 0);
    lv_obj_align(ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "RAM"), LV_ALIGN_LEFT_MID, 0, 0);
    barre(c, 44, 15, 120, 10, 45, UI_ACCENT, 20);
    lv_obj_t *v = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "27,9 / 62");
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -24, 0);
    lv_obj_align(ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, "Go"), LV_ALIGN_RIGHT_MID, 0, 2);
    (void)L;

    const lv_coord_t demi = (UI_ZONE_L - 2 * UI_MARGE - 8) / 2;
    const int bl = (demi - 20 - 23 * 2) / 24;
    static const uint8_t H[2][24] = {{2, 3, 2, 8, 30, 12, 4, 2, 2, 3, 2, 2, 5, 60, 72, 40, 6, 3, 2, 2, 4, 3, 2, 2},
                                     {1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 9, 1, 1, 1, 1, 1, 1, 1, 30, 1, 1, 1, 1, 6}};
    for (int k = 0; k < 2; k++)
    {
        lv_obj_t *cc = carte(f, y + 48, 96);
        lv_obj_set_width(cc, demi);
        lv_obj_set_x(cc, UI_MARGE + k * (demi + 8));
        lv_obj_set_style_pad_hor(cc, 10, 0);
        lv_obj_set_style_pad_ver(cc, 6, 0);
        ui_label(cc, UI_POLICE_LEGENDE, UI_TEXTE, k ? "Disque" : "Réseau");
        lv_obj_t *val = ui_label(cc, UI_POLICE_LEGENDE, UI_TEXTE, k ? "#4FC3F7 L 0,0M#  #FFB547 É 0,5M#" : "#4FC3F7 ↓ 1,2M#  #FFB547 ↑ 0,1M#");
        lv_label_set_recolor(val, true);
        lv_obj_align(val, LV_ALIGN_TOP_RIGHT, 0, 0);
        for (int i = 0; i < 24; i++)
        {
            const lv_coord_t hh = LV_MAX(1, 52 * H[k][i] / 100);
            rect(cc, i * (bl + 2), 76 - hh + 4, bl, hh, k ? UI_ACCENT : UI_FROID, 1);
        }
    }
    lv_obj_t *p = ui_label(f, UI_POLICE_LEGENDE, UI_TEXTE2, "allumé depuis 1 j 15 h · swap 23 %");
    lv_obj_set_pos(p, UI_MARGE, 442);
    lv_obj_t *vb = ui_bouton(f, 136, 34);
    lv_obj_set_style_radius(vb, 12, 0);
    lv_obj_align(vb, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 432);
    lv_obj_center(ui_label(vb, UI_POLICE_PETITE, UI_TEXTE, "Mettre en veille"));
}

lv_obj_t *maquette_pc_systeme(int variante)
{
    historique();
    lv_obj_t *f = fond(0);
    const lv_coord_t L = UI_ZONE_L - 2 * UI_MARGE - 24; // 356
    lv_obj_t *c = carte(f, 52, 212);
    entete_cpu(c);
    if (variante == 0)
    {
        // btop d'origine : graphe sur les 3/4, les 16 fils en jauges seules (2 colonnes de 8) sur le quart restant
        const lv_coord_t gw = (L - 10) * 3 / 4, gx = gw + 10, jl = (L - gx - 6) / 2;
        graphe(c, 0, 30, gw, 164);
        for (int i = 0; i < 16; i++)
            barre(c, gx + (i & 1) * (jl + 6), 32 + (i >> 1) * 20 + 3, jl, 12, COEURS[i], charge(COEURS[i]));
    }
    else
    {
        // graphe en largeur, les 16 fils en barres verticales dessous (comme aujourd'hui)
        graphe(c, 0, 30, L, 110);
        const lv_coord_t cl = (L - 15 * 3) / 16;
        for (int i = 0; i < 16; i++)
        {
            lv_obj_t *fd = rect(c, i * (cl + 3), 150, cl, 44, UI_CARTE2, 3);
            lv_obj_set_style_clip_corner(fd, true, 0);
            const lv_coord_t h = 44 * COEURS[i] / 100;
            rect(fd, 0, 44 - h, cl, h, charge(COEURS[i]), 0);
        }
    }
    bas_systeme(f, 272);
    return f;
}

// ---------------------------------------------------------------------------------------------------- GPU
struct bloc_t
{
    const char *nom;
    float go;
    uint32_t couleur;
};
struct carte_gpu_t
{
    const char *nom;
    int charge, temp, w;
    float total;
    bloc_t blocs[5];
};
static const carte_gpu_t GPUS[3] = {
    {"3090", 0, 28, 7, 24, {{"ReadyArt_gemma-4-31B-it-scotoma-2-Q4_K_M", 13.3f, 0x22D3EE}, {"Whisper (STT)", 4.4f, 0x35C46A}, {"gemma-4-E4B-it-qat-UD-Q4_K_XL", 3.4f, 0xFF6B9D}, {"PaddleOCR-VL-1.6-Q8_0", 1.4f, 0xE879F9}}},
    {"3060", 0, 27, 5, 12, {{"gemma-4-12B-it-qat-UD-Q4_K_XL", 6.8f, 0xF87171}, {"Qwen3-TTS (voix)", 4.6f, 0xF0A83C}}},
    {"3090", 0, 29, 8, 24, {{"ReadyArt_gemma-4-31B-it-scotoma-2-Q4_K_M", 15.1f, 0x22D3EE}, {"Qwen3-Embedding-4B-Q4_K_M", 3.5f, 0xF59E0B}, {"gemma-4-E4B-it-qat-UD-Q4_K_XL", 3.4f, 0xFF6B9D}, {"bge-reranker-v2-m3-Q8_0", 0.6f, 0x10B981}}},
};

static void virgule(char *b, size_t n, float v)
{
    snprintf(b, n, "%.1f", v);
    for (char *p = b; *p; p++)
        if (*p == '.')
            *p = ',';
}

lv_obj_t *maquette_pc_gpu()
{
    lv_obj_t *f = fond(1);
    const lv_coord_t L = UI_ZONE_L - 2 * UI_MARGE - 24;
    for (int g = 0; g < 3; g++)
    {
        const carte_gpu_t &G = GPUS[g];
        lv_obj_t *c = carte(f, 52 + g * 136, 130);
        lv_obj_set_style_pad_ver(c, 8, 0);
        ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, G.nom);
        char t[48], a[12], b[12];
        snprintf(t, sizeof t, "%d%%", G.charge);
        lv_obj_set_pos(ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, t), 52, -2);
        snprintf(t, sizeof t, "%d° · %d W", G.temp, G.w);
        lv_obj_set_pos(ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, t), 104, 2);
        float pris = 0;
        for (const bloc_t &k : G.blocs)
            pris += k.go;
        virgule(a, sizeof a, pris);
        snprintf(t, sizeof t, "%s / %d Go", a, (int)G.total);
        lv_obj_t *v = ui_label(c, UI_POLICE_PETITE, UI_TEXTE, t);
        lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, 2);

        // la VRAM en blocs empilés, dans l'ordre de taille, le libre en fond
        lv_obj_t *bar = rect(c, 0, 26, L, 14, UI_CARTE2, 4);
        lv_obj_set_style_clip_corner(bar, true, 0);
        lv_coord_t x = 0;
        for (const bloc_t &k : G.blocs)
        {
            if (!k.nom)
                break;
            const lv_coord_t w = (lv_coord_t)(L * k.go / G.total + 0.5f);
            rect(bar, x, 0, LV_MAX(w - 2, 1), 14, k.couleur, 0);
            x += w;
        }
        // légende sur une colonne : carré de couleur, nom en entier, Go
        int i = 0;
        for (const bloc_t &k : G.blocs)
        {
            if (!k.nom)
                break;
            const lv_coord_t ly = 48 + i * 17;
            rect(c, 0, ly + 3, 10, 10, k.couleur, 3);
            lv_obj_t *n = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, k.nom);
            lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
            lv_obj_set_size(n, L - 70, 16);
            lv_obj_set_pos(n, 16, ly);
            virgule(b, sizeof b, k.go);
            lv_obj_t *gv = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE, b);
            lv_obj_set_width(gv, 40);
            lv_obj_set_style_text_align(gv, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_pos(gv, L - 40, ly);
            i++;
        }
    }
    return f;
}
