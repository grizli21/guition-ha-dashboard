#include "page_pc.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "ui_theme.h"
#include "ui_courbe.h" // 05/11 (vague D) : wrappers uc_* — graphe de charge sous 9.6
#include "icones_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

UI_PSRAM(pc_etat_t, pc_etat_p, 1, nullptr); // 29/10 : en PSRAM (ui_psram.h)
void (*pc_commande)(pc_action_t a) = nullptr;

static lv_obj_t *g_zone = nullptr;
static int g_vue = 0; // 0 Système, 1 GPU, 2 Processus, 3 IA
static int g_tri = 0; // processus : 0 CPU, 1 mémoire, 2 VRAM

static void construire();

// couleur de charge façon btop, au thème : froid -> ambre -> rouge
static uint32_t charge(int v) { return v < 40 ? UI_FROID : v < 75 ? UI_ACCENT : UI_ROUGE; }

static void virgule(char *b, size_t n, float v, int dec)
{
    snprintf(b, n, "%.*f", dec, v);
    for (char *p = b; *p; p++)
        if (*p == '.')
            *p = SEP_CAR;
}

static lv_obj_t *carte(lv_obj_t *z, lv_coord_t y, lv_coord_t h)
{
    lv_obj_t *c = ui_conteneur(z);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, h);
    lv_obj_set_pos(c, UI_MARGE, y);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_set_style_pad_ver(c, 0, 0);
    return c;
}

// 27/10 (audit des redessins) : la page n'était reconstruite que pour être à jour (toute la page, à chaque état de
// l'agent, toutes les 2 s : 85 % de l'écran redessiné) ; désormais construite quand sa STRUCTURE change (veille,
// onglet, tri, nombre de processus), et ses valeurs posées sur place, seulement si elles changent
static void taille(lv_obj_t *o, lv_coord_t w, lv_coord_t h) // lv_obj_set_size redessine même à l'identique
{
    if (lv_obj_get_style_width(o, LV_PART_MAIN) != w || lv_obj_get_style_height(o, LV_PART_MAIN) != h)
        lv_obj_set_size(o, w, h);
}

// barre horizontale fine : fond, partie pleine (et partie grise à la suite, pour le cache) ; valeurs : barre_maj
static lv_obj_t *barre(lv_obj_t *p, lv_coord_t l, lv_coord_t h, bool avec_cache = false)
{
    lv_obj_t *b = ui_conteneur(p);
    lv_obj_set_size(b, l, h);
    lv_obj_set_style_radius(b, h / 2, 0);
    ui_style_bg_color(b, ui_c(UI_CARTE2), 0);
    ui_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(b, true, 0);
    lv_obj_t *f = ui_conteneur(b);
    lv_obj_set_size(f, 0, h);
    ui_style_bg_opa(f, LV_OPA_COVER, 0);
    if (avec_cache)
    {
        lv_obj_t *g = ui_conteneur(b);
        lv_obj_set_size(g, 0, h);
        ui_style_bg_color(g, ui_c(UI_ETEINT), 0);
        ui_style_bg_opa(g, LV_OPA_COVER, 0);
    }
    return b;
}
static void barre_maj(lv_obj_t *b, int pct, uint32_t c, int pct2 = 0)
{
    const lv_coord_t l = lv_obj_get_style_width(b, LV_PART_MAIN), h = lv_obj_get_style_height(b, LV_PART_MAIN);
    lv_obj_t *f = lv_obj_get_child(b, 0);
    taille(f, l * LV_CLAMP(0, pct, 100) / 100, h);
    ui_style_bg_color(f, ui_c(c), 0);
    if (lv_obj_get_child_cnt(b) > 1)
    {
        lv_obj_t *g = lv_obj_get_child(b, 1);
        taille(g, l * LV_CLAMP(0, pct2, 100) / 100, h);
        if (lv_obj_get_style_x(g, LV_PART_MAIN) != l * LV_CLAMP(0, pct, 100) / 100)
            lv_obj_set_x(g, l * LV_CLAMP(0, pct, 100) / 100);
    }
}

// les objets dont les valeurs changent (nullptr : pas dans la vue affichée)
static lv_obj_t *o_veille_txt, *o_cpu_info, *o_cpu_val, *o_graphe, *o_coeurs[PC_COEURS], *o_ram_val, *o_ram_barre, *o_pied;
static uint32_t g_graphe_n; // point du graphe déjà dessiné (pc_etat.n_cpu)
static struct
{
    lv_obj_t *nom, *charge, *temp, *vram, *barre, *seg[PC_BLOCS], *legende[PC_BLOCS], *go[PC_BLOCS], *carre[PC_BLOCS];
    lv_obj_t *freq_ico[2], *freq_val[2]; // 08/11 maquette GPU : puce + fréquence GPU, barrette + fréquence mémoire
} o_gpu[PC_GPUS];
static lv_obj_t *o_io_val[2], *o_io_col[2][PC_HIST];
static lv_obj_t *o_veille_btn, *o_veille_lbl;
static bool g_confirme_veille = false; // 31/10 (retour d'usage) : « Mettre en veille » demande confirmation sur place
static struct
{
    lv_obj_t *nom, *role, *barre, *cpu, *mem, *vram;
} o_proc[PC_PROCS];
static lv_coord_t g_io_bl = 1; // largeur d'une colonne d'historique

// =====================================================================  VEILLE
static void veille_style()
{
    ui_style_bg_color(o_veille_btn, ui_c(g_confirme_veille ? UI_ROUGE : UI_CARTE), 0);
    ui_style_border_color(o_veille_btn, ui_c(g_confirme_veille ? UI_ROUGE : UI_BORD), 0);
    ui_style_text_color(o_veille_lbl, ui_c(g_confirme_veille ? UI_SUR_ACCENT : UI_TEXTE), 0);
    ui_label_texte(o_veille_lbl, g_confirme_veille ? TXT("Toucher encore", "Tap again") : TXT("Mettre en veille", "Sleep"));
}

static void action_cb(lv_event_t *e)
{
    pc_action_t a = (pc_action_t)(intptr_t)lv_event_get_user_data(e);
    if (a == PC_VEILLE && !g_confirme_veille)
    {
        g_confirme_veille = true; // 1er appui : on demande confirmation
        veille_style();
        return;
    }
    g_confirme_veille = false;
    if (pc_commande)
        pc_commande(a);
}

static void construire_veille(lv_obj_t *z)
{
    lv_obj_t *col = ui_conteneur(z);
    lv_obj_set_size(col, UI_ZONE_L, 412);
    lv_obj_set_pos(col, 0, 52);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 12, 0);
    ui_icone(col, &icon_pc, UI_ETEINT); // 64 px : pas de zoom, LVGL 8.4 ne transforme pas les icônes alpha (vu au simulateur)
    ui_label(col, UI_POLICE_TITRE, UI_TEXTE2, TXT("PC en veille", "PC asleep"));
    o_veille_txt = ui_label(col, UI_POLICE_PETITE, UI_TEXTE3, "");
    lv_obj_t *b = ui_bouton(col, 240, 56);
    ui_style_ajouter(b, &ui_st_bouton_principal, 0);
    lv_obj_center(ui_label(b, UI_POLICE_TEXTE, UI_SUR_ACCENT, TXT("Réveiller", "Wake")));
    lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *)PC_REVEILLER);
}

// =====================================================================  SYSTÈME
// 31/10 (maquette validée) : comme le btop d'origine, la charge sur 5 min en graphe (3/4 de la largeur) et les 16 fils
// en jauges seules sur le quart restant ; quadrillage 25/50/75, aire pâle sous la courbe, couleur selon la charge
static void graphe_cb(lv_event_t *e)
{
    const uint8_t *h = pc_etat.h_cpu;
    uc_cible_t * ctx = uc_cible_du(e); // 9.6 : la layer de l'événement (avant : lv_event_get_draw_ctx)
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const lv_coord_t w = lv_area_get_width(&a), ht = lv_area_get_height(&a);
    auto x = [&](int i) { return (lv_coord_t)(a.x1 + (w - 1) * i / (PC_HIST_CPU - 1)); };
    auto y = [&](int v) { return (lv_coord_t)(a.y2 - (ht - 1) * v / 100); };

    lv_draw_line_dsc_t q;
    lv_draw_line_dsc_init(&q);
    q.color = ui_c(UI_BORD);
    q.width = 1;
    q.dash_width = 3;
    q.dash_gap = 3;
    for (int v = 25; v < 100; v += 25)
        uc_segment(ctx, q, a.x1, y(v), a.x2, y(v)); // 9.6 : p1/p2 dans le descripteur (uc_segment)
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_20;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.width = 2;
    l.round_start = l.round_end = 1;
    for (int pass = 0; pass < 2; pass++) // l'aire d'abord, la courbe par-dessus
        for (int i = 0; i < PC_HIST_CPU - 1; i++)
        {
            const lv_color_t c = ui_c(charge(h[i + 1]));
            if (pass == 0)
            {
                r.bg_color = c;
                lv_area_t z = {(lv_coord_t)(x(i) + 1), y((h[i] + h[i + 1]) / 2), x(i + 1), a.y2};
                uc_carre(ctx, r, &z); // NOTE coût 9.6 : aire = une task par segment (PC_HIST_CPU-1), ajout en O(n)
                continue;
            }
            l.color = c;
            uc_segment(ctx, l, x(i), y(h[i]), x(i + 1), y(h[i + 1]));
        }
}

static void construire_systeme(lv_obj_t *z)
{
    const lv_coord_t L = UI_ZONE_L - 2 * UI_MARGE - 24; // largeur utile d'une carte

    // CPU : en-tête, graphe, fils
    lv_obj_t *c = carte(z, 52, 212);
    lv_obj_set_style_pad_ver(c, 8, 0);
    lv_obj_t *n = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "CPU #8B95A1 7800X3D#");
    lv_label_set_recolor(n, true);
    o_cpu_info = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_align(o_cpu_info, LV_ALIGN_TOP_MID, 10, 3);
    o_cpu_val = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "");
    lv_obj_align(o_cpu_val, LV_ALIGN_TOP_RIGHT, 0, -2);
    const lv_coord_t gw = (L - 10) * 3 / 4, gx = gw + 10, jl = (L - gx - 6) / 2;
    lv_obj_t *g = ui_conteneur(c);
    lv_obj_set_size(g, gw, 164);
    lv_obj_set_pos(g, 0, 30);
    lv_obj_set_style_radius(g, 6, 0);
    ui_style_bg_color(g, ui_c(UI_FOND), 0);
    ui_style_bg_opa(g, LV_OPA_COVER, 0);
    o_graphe = ui_conteneur(g);
    lv_obj_set_size(o_graphe, gw - 8, 164 - 8);
    lv_obj_center(o_graphe);
    lv_obj_add_event_cb(o_graphe, graphe_cb, LV_EVENT_DRAW_MAIN, NULL);
    g_graphe_n = pc_etat.n_cpu;
    lv_obj_align(ui_label(g, UI_POLICE_LEGENDE, UI_TEXTE3, "5 min"), LV_ALIGN_TOP_LEFT, 6, 2);
    for (int i = 0; i < PC_COEURS; i++) // 2 colonnes de 8, sans étiquette
    {
        o_coeurs[i] = barre(c, jl, 12);
        lv_obj_set_pos(o_coeurs[i], gx + (i & 1) * (jl + 6), 35 + (i >> 1) * 20);
    }

    // mémoire
    c = carte(z, 272, 40);
    lv_obj_align(ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "RAM"), LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(ui_valeur_unite(c, UI_POLICE_3X5_P, UI_TEXTE, TXT("Go", "GB"), &o_ram_val), LV_ALIGN_RIGHT_MID, 0, 0);
    o_ram_barre = barre(c, 120, 10, true);
    lv_obj_align(o_ram_barre, LV_ALIGN_LEFT_MID, 44, 0);

    // réseau + disque, avec leur historique
    const lv_coord_t demi = (UI_ZONE_L - 2 * UI_MARGE - 8) / 2;
    g_io_bl = (demi - 20 - (PC_HIST - 1) * 2) / PC_HIST;
    for (int k = 0; k < 2; k++)
    {
        lv_obj_t *cc = carte(z, 320, 96);
        lv_obj_set_width(cc, demi);
        lv_obj_set_x(cc, UI_MARGE + k * (demi + 8));
        lv_obj_set_style_pad_hor(cc, 10, 0);
        lv_obj_set_style_pad_ver(cc, 6, 0);
        ui_label(cc, UI_POLICE_LEGENDE, UI_TEXTE, k ? TXT("Disque", "Disk") : TXT("Réseau", "Network"));
        o_io_val[k] = ui_label(cc, UI_POLICE_LEGENDE, UI_TEXTE, "");
        lv_label_set_recolor(o_io_val[k], true);
        lv_obj_align(o_io_val[k], LV_ALIGN_TOP_RIGHT, 0, 0);
        for (int i = 0; i < PC_HIST; i++)
        {
            lv_obj_t *col = o_io_col[k][i] = ui_conteneur(cc);
            lv_obj_set_size(col, g_io_bl, 1);
            ui_style_bg_color(col, ui_c(k ? UI_ACCENT : UI_FROID), 0);
            ui_style_bg_opa(col, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(col, 1, 0);
        }
    }

    // pied : depuis quand, et veille
    o_pied = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_pos(o_pied, UI_MARGE, 442);
    o_veille_btn = ui_bouton(z, 136, 34);
    lv_obj_set_style_radius(o_veille_btn, 12, 0);
    lv_obj_align(o_veille_btn, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 432);
    o_veille_lbl = ui_label(o_veille_btn, UI_POLICE_PETITE, UI_TEXTE, "");
    lv_obj_center(o_veille_lbl);
    lv_obj_add_event_cb(o_veille_btn, action_cb, LV_EVENT_CLICKED, (void *)PC_VEILLE);
    veille_style();
}

static void maj_systeme()
{
    const pc_etat_t &p = pc_etat;
    char t[64], a[16], b[16];
    virgule(a, sizeof a, p.freq_mhz / 1000.0f, 1);
    ui_label_texte_fmt(o_cpu_info, "%s GHz · %d° · %d W", a, p.cpu_temp, p.cpu_watts);
    ui_label_texte_fmt(o_cpu_val, "%d%%", p.cpu);
    ui_style_text_color(o_cpu_val, ui_c(charge(p.cpu) == UI_FROID ? UI_TEXTE : charge(p.cpu)), 0);
    if (g_graphe_n != p.n_cpu) // un point toutes les 2 s
    {
        g_graphe_n = p.n_cpu;
        lv_obj_invalidate(o_graphe);
    }
    for (int i = 0; i < PC_COEURS; i++)
        barre_maj(o_coeurs[i], p.coeurs[i], charge(p.coeurs[i]));
    virgule(a, sizeof a, p.ram, 1);
    ui_label_texte_fmt(o_ram_val, "%s / %d", a, (int)p.ram_total);
    barre_maj(o_ram_barre, (int)(p.ram * 100 / p.ram_total), UI_ACCENT, (int)(p.cache * 100 / p.ram_total));
    for (int k = 0; k < 2; k++)
    {
        virgule(a, sizeof a, k ? p.disque_l : p.net_bas, 1);
        virgule(b, sizeof b, k ? p.disque_e : p.net_haut, 1);
        snprintf(t, sizeof t, k ? TXT("#4FC3F7 L %sM#  #FFB547 É %sM#", "#4FC3F7 R %sM#  #FFB547 W %sM#")
                            : "#4FC3F7 ↓ %sM#  #FFB547 ↑ %sM#", a, b);
        ui_label_texte(o_io_val[k], t);
        const uint8_t *h = k ? p.h_disque : p.h_net;
        for (int i = 0; i < PC_HIST; i++)
        {
            const lv_coord_t hh = LV_MAX(1, 52 * h[i] / 100);
            taille(o_io_col[k][i], g_io_bl, hh);
            if (lv_obj_get_style_x(o_io_col[k][i], LV_PART_MAIN) != i * (g_io_bl + 2) ||
                lv_obj_get_style_y(o_io_col[k][i], LV_PART_MAIN) != 80 - hh)
                lv_obj_set_pos(o_io_col[k][i], i * (g_io_bl + 2), 80 - hh);
        }
    }
    ui_label_texte_fmt(o_pied, TXT("allumé depuis %s · swap %d %%", "on since %s · swap %d %%"), p.allume_depuis, p.swap);
}

// =====================================================================  GPU
// 31/10 (maquette validée) : par carte, la VRAM en blocs empilés aux couleurs du hub IA, et leur légende sur une
// colonne (noms complets) ; au-delà de 4 lignes, la dernière regroupe le reste
#define GPU_LIGNES 4

static void construire_gpu(lv_obj_t *z)
{
    const lv_coord_t L = UI_ZONE_L - 2 * UI_MARGE - 24;
    for (int g = 0; g < PC_GPUS; g++)
    {
        auto &o = o_gpu[g];
        lv_obj_t *c = carte(z, 52 + g * 136, 132);
        lv_obj_set_style_pad_ver(c, 8, 0);
        o.nom = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "");
        o.charge = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "");
        lv_obj_set_pos(o.charge, 52, -2);
        o.temp = ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, "");
        lv_obj_set_pos(o.temp, 104, 2);
        o.vram = ui_label(c, UI_POLICE_PETITE, UI_TEXTE, "");
        lv_obj_align(o.vram, LV_ALIGN_TOP_RIGHT, 0, 2);
        o.barre = ui_conteneur(c);
        lv_obj_set_size(o.barre, L, 14);
        lv_obj_set_pos(o.barre, 0, 26);
        lv_obj_set_style_radius(o.barre, 4, 0);
        ui_style_bg_color(o.barre, ui_c(UI_CARTE2), 0);
        ui_style_bg_opa(o.barre, LV_OPA_COVER, 0);
        lv_obj_set_style_clip_corner(o.barre, true, 0);
        // 08/11 (maquette GPU) : fréquence GPU sur la première ligne de légende, fréquence mémoire sur la
        // deuxième — les « lignes 3 et 4 » de la carte. Chacune précédée d'une icône de bibliothèque de 16x16
        // (Material Design Icons mdi:chip et mdi:memory), couleur UI_TEXTE3, à la hauteur de la police de légende
        // (montserrat_sb_12_ui, line_height 16). La valeur (72 px, alignée à droite) finit à L, le bord droit.
        // 2e tour (retour d'usage) : la consommation en Go passe à gauche, juste après le carré de couleur, alignée à droite
        // dans sa colonne, puis vient le nom tronqué. Les fréquences restent à droite des lignes 3 et 4.
        // 3e tour (08/11 19:45) : icônes inversées (la carrée à pattes sur les quatre côtés = mémoire ->
        // fréquence GPU ; la rectangulaire à pattes en haut et en bas = puce -> fréquence mémoire) ; Go collées à la
        // pastille (même écart de 6 px que la pastille et son texte ailleurs dans l'interface) et alignées à gauche ;
        // le nom ne peut jamais atteindre la colonne des icônes (largeur calculée depuis FREQ_X) ; écart icône ->
        // valeur ramené à 4 px, comme les autres paires icône + valeur de l'interface.
        const lv_coord_t FREQ_X = L - 92;          // 264 : icône 16 px (264..280)
        const lv_coord_t ECU_ICONE = 4;            // écart icône -> valeur
        const lv_coord_t GO_L = 16, GO_W = 28;     // pastille 0..10, Go 16..44, alignées à gauche
        const lv_coord_t NOM_X = GO_L + GO_W + 4;  // 48 : le nom part après les Go
        const lv_coord_t MARGE_NOM = 12;           // le nom s'arrête 12 px avant l'icône, jamais dessus
        for (int i = 0; i < 2; i++)
        {
            const lv_coord_t ly = 48 + i * 17;
            o.freq_ico[i] = ui_icone(c, i == 0 ? &icon_mdi_memory : &icon_mdi_chip, UI_TEXTE3);
            lv_obj_set_pos(o.freq_ico[i], FREQ_X, ly); // 16 px de haut = la hauteur exacte de la ligne
            o.freq_val[i] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE3, "");
            // Mesuré sur capture 8.4 : « 9626 MHz » fait 60 px d'encre, il ne rentrait pas dans 60 px et passait sur
            // deux lignes, par-dessus la ligne de légende du dessous. Découpe (jamais de retour à la ligne).
            // Aligné à gauche contre l'icône (et non à droite) : c'est ce qui ramène l'écart vu à 4 px, les valeurs
            // ayant toutes la même longueur (« 1500 MHz », « 9626 MHz »), elles restent alignées entre elles.
            lv_label_set_long_mode(o.freq_val[i], LV_LABEL_LONG_CLIP);
            lv_obj_set_width(o.freq_val[i], L - (FREQ_X + 16 + ECU_ICONE));
            lv_obj_set_style_text_align(o.freq_val[i], LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_pos(o.freq_val[i], FREQ_X + 16 + ECU_ICONE, ly);
        }
        const int nb = pc_etat.gpus[g].nb_blocs;
        for (int i = 0; i < PC_BLOCS; i++)
        {
            o.seg[i] = o.legende[i] = o.go[i] = o.carre[i] = nullptr;
            if (i >= nb)
                continue;
            o.seg[i] = ui_conteneur(o.barre);
            lv_obj_set_size(o.seg[i], 0, 14);
            ui_style_bg_opa(o.seg[i], LV_OPA_COVER, 0);
            if (i >= GPU_LIGNES)
                continue;
            const lv_coord_t ly = 48 + i * 17;
            const bool etroite = i < 2; // ces deux lignes cèdent la droite aux fréquences
            o.carre[i] = ui_conteneur(c);
            lv_obj_set_size(o.carre[i], 10, 10);
            lv_obj_set_pos(o.carre[i], 0, ly + 3);
            lv_obj_set_style_radius(o.carre[i], 3, 0);
            ui_style_bg_opa(o.carre[i], LV_OPA_COVER, 0);
            o.go[i] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE, "");
            lv_obj_set_width(o.go[i], GO_W);
            lv_obj_set_style_text_align(o.go[i], LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_pos(o.go[i], GO_L, ly);
            o.legende[i] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
            lv_label_set_long_mode(o.legende[i], LV_LABEL_LONG_DOT);
            lv_obj_set_size(o.legende[i], etroite ? FREQ_X - MARGE_NOM - NOM_X : L - NOM_X, 16);
            lv_obj_set_pos(o.legende[i], NOM_X, ly);
        }
    }
}

// « 1500 MHz », ou « - » si la fréquence n'est pas publiée (08/11, maquette GPU — même règle que la bulle du graphe :
// un champ absent ne s'invente pas)
static void freq(char *dst, size_t n, int f)
{
    if (f <= 0)
        snprintf(dst, n, "-");
    else
        snprintf(dst, n, "%d MHz", f);
}

static void maj_gpu()
{
    const lv_coord_t L = UI_ZONE_L - 2 * UI_MARGE - 24;
    char a[16], b[16]; // b : « 9626 MHz » (08/11 maquette GPU)
    for (int g = 0; g < PC_GPUS; g++)
    {
        const pc_gpu_t &x = pc_etat.gpus[g];
        auto &o = o_gpu[g];
        ui_label_texte(o.nom, x.nom);
        ui_label_texte_fmt(o.charge, "%d%%", x.charge);
        ui_style_text_color(o.charge, ui_c(x.charge >= 75 ? 0xFF8A7A : UI_TEXTE), 0);
        ui_label_texte_fmt(o.temp, "%d° · %d W · %s", x.temp, x.puissance, x.ps[0] ? x.ps : "-");
        ui_style_text_color(o.temp, ui_c(x.temp >= 75 ? UI_ROUGE : x.temp >= 65 ? UI_ACCENT : UI_TEXTE2), 0);
        virgule(a, sizeof a, x.vram, 1);
        ui_label_texte_fmt(o.vram, TXT("%s / %d Go", "%s / %d GB"), a, (int)(x.vram_total + 0.5f));
        // 08/11 (maquette GPU) : fréquence actuelle seule — la fréquence GPU sur la ligne 3, la mémoire sur la ligne 4
        if (o.freq_val[0])
        {
            freq(b, sizeof b, x.f);
            ui_label_texte(o.freq_val[0], b);
            freq(b, sizeof b, x.fm);
            ui_label_texte(o.freq_val[1], b);
        }
        lv_coord_t px = 0;
        uint32_t reste = 0; // Mo des blocs au-delà de la dernière ligne de légende
        for (int i = 0; i < x.nb_blocs && i < PC_BLOCS; i++)
        {
            const pc_bloc_t &b = x.blocs[i];
            const lv_coord_t w = x.vram_total > 0 ? (lv_coord_t)(L * b.mo / (x.vram_total * 1024) + 0.5f) : 0;
            taille(o.seg[i], LV_MAX(w - 2, 1), 14);
            if (lv_obj_get_style_x(o.seg[i], LV_PART_MAIN) != px)
                lv_obj_set_x(o.seg[i], px);
            ui_style_bg_color(o.seg[i], ui_c(b.couleur), 0);
            px += w;
            const bool groupe = x.nb_blocs > GPU_LIGNES && i >= GPU_LIGNES - 1;
            if (groupe)
                reste += b.mo;
            if (groupe ? i < x.nb_blocs - 1 : i >= GPU_LIGNES) // le groupe s'écrit à son dernier bloc
                continue;
            const int l = LV_MIN(i, GPU_LIGNES - 1);
            ui_style_bg_color(o.carre[l], ui_c(groupe ? UI_TEXTE3 : b.couleur), 0);
            if (groupe)
                ui_label_texte_fmt(o.legende[l], TXT("%d autres", "%d more"), x.nb_blocs - GPU_LIGNES + 1);
            else
                ui_label_texte(o.legende[l], b.nom);
            virgule(a, sizeof a, (groupe ? reste : b.mo) / 1024.0f, 1);
            ui_label_texte(o.go[l], a);
        }
    }
}

// =====================================================================  PROCESSUS
static void tri_cb(lv_event_t *e)
{
    g_tri = (int)(intptr_t)lv_event_get_user_data(e);
    construire();
}

static int compare(const void *x, const void *y)
{
    const pc_proc_t *a = (const pc_proc_t *)x, *b = (const pc_proc_t *)y;
    float va = g_tri == 0 ? a->cpu : g_tri == 1 ? a->mem : a->vram;
    float vb = g_tri == 0 ? b->cpu : g_tri == 1 ? b->mem : b->vram;
    return va < vb ? 1 : va > vb ? -1 : 0;
}

static void construire_processus(lv_obj_t *z)
{
    lv_obj_t *t = ui_label(z, UI_POLICE_PETITE, UI_TEXTE2, TXT("trié par", "sorted by"));
    lv_obj_set_pos(t, UI_MARGE + 4, 60);
    lv_obj_t *rang = ui_conteneur(z); // pastilles de tri, à droite
    lv_obj_set_size(rang, LV_SIZE_CONTENT, 32);
    lv_obj_align(rang, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 54);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(rang, 6, 0);
    const char *noms[] = {"CPU", TXT("Mémoire", "Memory"), "VRAM"};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *b = ui_bouton(rang, i == 1 ? 86 : 64, 32);
        lv_obj_set_style_radius(b, 10, 0);
        ui_style_bg_color(b, ui_c(i == g_tri ? UI_CARTE2 : UI_CARTE), 0);
        ui_style_border_color(b, ui_c(i == g_tri ? UI_ACCENT : UI_BORD), 0);
        lv_obj_t *l = ui_label(b, UI_POLICE_PETITE, i == g_tri ? UI_ACCENT : UI_TEXTE2, noms[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, tri_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *l = ui_conteneur(z);
    lv_obj_set_size(l, UI_ZONE_L, UI_ZONE_H - 92);
    lv_obj_set_pos(l, 0, 92);
    lv_obj_add_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
    ui_defilement_rapide(l); // 05/11 : scroll par bande (garde = style lu a la creation)
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(l, UI_MARGE, 0);
    lv_obj_set_style_pad_row(l, 4, 0);
    lv_obj_set_style_pad_bottom(l, 8, 0);
    const int n = LV_MIN(pc_etat.nb_procs, PC_PROCS);
    for (int i = 0; i < n; i++) // une ligne par rang : son processus peut changer d'une fois sur l'autre (tri)
    {
        auto &o = o_proc[i];
        lv_obj_t *r = carte(l, 0, 40);
        lv_obj_set_width(r, lv_pct(100));
        lv_obj_set_style_radius(r, 10, 0);
        lv_obj_set_style_pad_hor(r, 10, 0);
        o.nom = ui_label(r, UI_POLICE_PETITE, UI_TEXTE, "");
        lv_label_set_long_mode(o.nom, LV_LABEL_LONG_DOT);
        lv_obj_set_size(o.nom, 146, 17);
        lv_obj_set_pos(o.nom, 0, 3);
        o.role = ui_label(r, UI_POLICE_LEGENDE, UI_TEXTE3, "");
        lv_obj_set_pos(o.role, 0, 20);
        o.barre = barre(r, 40, 8);
        lv_obj_align(o.barre, LV_ALIGN_LEFT_MID, 152, 0);
        const lv_coord_t larg[] = {56, 50, 50}, dx[] = {-104, -52, 0};
        lv_obj_t **v[] = {&o.cpu, &o.mem, &o.vram};
        for (int k = 0; k < 3; k++)
        {
            *v[k] = ui_label(r, UI_POLICE_PETITE, k ? UI_TEXTE2 : UI_TEXTE, "");
            lv_obj_set_width(*v[k], larg[k]);
            lv_obj_set_style_text_align(*v[k], LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(*v[k], LV_ALIGN_RIGHT_MID, dx[k], 0);
        }
    }
}

static void maj_processus()
{
    pc_proc_t tri[PC_PROCS];
    const int n = LV_MIN(pc_etat.nb_procs, PC_PROCS);
    memcpy(tri, pc_etat.procs, sizeof(pc_proc_t) * n);
    qsort(tri, n, sizeof(pc_proc_t), compare);
    float max_cpu = 1;
    for (int i = 0; i < n; i++)
        max_cpu = LV_MAX(max_cpu, tri[i].cpu);
    char a[16];
    for (int i = 0; i < n; i++)
    {
        const pc_proc_t &p = tri[i];
        auto &o = o_proc[i];
        ui_label_texte(o.nom, p.nom);
        ui_label_texte(o.role, p.role);
        barre_maj(o.barre, (int)(p.cpu * 100 / max_cpu), charge((int)LV_MIN(100, p.cpu)));
        virgule(a, sizeof a, p.cpu, p.cpu < 10 ? 1 : 0);
        ui_label_texte_fmt(o.cpu, "%s %%", a);
        virgule(a, sizeof a, p.mem, p.mem < 10 ? 1 : 0);
        ui_label_texte_fmt(o.mem, TXT("%s Go", "%s GB"), a);
        virgule(a, sizeof a, p.vram, p.vram < 10 ? 1 : 0);
        if (p.vram > 0)
            ui_label_texte_fmt(o.vram, TXT("%s Go", "%s GB"), a);
        else
            ui_label_texte(o.vram, "–");
    }
}

// =====================================================================  SERVICES IA
// 31/10 (retour d'usage) : les deux comptes Claude, comme l'écran btop : usage 5 h et semaine, avec l'heure de réinitialisation
// et le temps qui reste ; valeurs retenues sur le broker : montrées aussi PC en veille, avec leur âge
static struct
{
    lv_obj_t *nom, *maj, *barre[2], *val[2], *reset[2];
} o_ia[PC_IA];

static void construire_ia(lv_obj_t *z)
{
    static const char *const FENETRES[] = {TXT("5 heures", "5 hours"), TXT("Semaine", "Week")};
    for (int i = 0; i < PC_IA; i++)
    {
        lv_obj_t *c = carte(z, 56 + i * 208, 196);
        auto &o = o_ia[i];
        o.nom = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "");
        lv_obj_set_pos(o.nom, 0, 14);
        o.maj = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE3, "");
        lv_obj_align(o.maj, LV_ALIGN_TOP_RIGHT, 0, 17);
        for (int k = 0; k < 2; k++)
        {
            const lv_coord_t y = 50 + k * 72;
            lv_obj_set_pos(ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, FENETRES[k]), 0, y);
            o.val[k] = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "");
            lv_obj_align(o.val[k], LV_ALIGN_TOP_RIGHT, 0, y - 4);
            o.barre[k] = barre(c, UI_ZONE_L - 2 * UI_MARGE - 24, 10);
            lv_obj_set_pos(o.barre[k], 0, y + 24);
            o.reset[k] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
            lv_obj_set_pos(o.reset[k], 0, y + 42);
        }
    }
}

// « dans 2 h 13 », « dans 35 min », « dans 1 j 4 h »
static void dans(char *b, size_t n, int32_t s)
{
    if (s <= 0)
        snprintf(b, n, TXT("maintenant", "now"));
    else if (s >= 86400)
        snprintf(b, n, TXT("dans %d j %d h", "in %d d %d h"), (int)(s / 86400), (int)(s % 86400 / 3600));
    else if (s >= 3600)
        snprintf(b, n, TXT("dans %d h %02d", "in %d h %02d"), (int)(s / 3600), (int)(s % 3600 / 60));
    else
        snprintf(b, n, TXT("dans %d min", "in %d min"), (int)(s / 60 + 1));
}

static void maj_ia()
{
    static const char *const JOURS[] = {TXT("dim.", "Sun"), TXT("lun.", "Mon"), TXT("mar.", "Tue"), TXT("mer.", "Wed"), TXT("jeu.", "Thu"), TXT("ven.", "Fri"), TXT("sam.", "Sat")};
    const uint32_t maintenant = (uint32_t)time(nullptr);
    for (int i = 0; i < PC_IA; i++)
    {
        auto &o = o_ia[i];
        if (i >= pc_etat.nb_ia)
        {
            ui_label_texte(o.nom, i ? "" : TXT("en attente de l'agent du PC", "waiting for the PC agent"));
            continue;
        }
        const pc_ia_t &a = pc_etat.ia[i];
        ui_label_texte(o.nom, a.nom);
        const int32_t age = a.maj && maintenant > 1700000000u ? (int32_t)(maintenant - a.maj) : 0;
        char t[48], d[24];
        if (age < 600) // lu toutes les 5 min par l'agent
            ui_label_texte(o.maj, "");
        else
        {
            dans(d, sizeof d, age);
            ui_label_texte_fmt(o.maj, TXT("lu il y a %s", "read %s ago"), d + 5); // « 3 h 05 » (sans « dans »)
        }
        const int pct[] = {a.h5, a.semaine};
        const uint32_t reset[] = {a.h5_reset, a.semaine_reset};
        for (int k = 0; k < 2; k++)
        {
            ui_label_texte_fmt(o.val[k], "%d%%", pct[k]);
            ui_style_text_color(o.val[k], ui_c(pct[k] >= 90 ? UI_ROUGE : pct[k] >= 75 ? UI_ACCENT : UI_TEXTE), 0);
            barre_maj(o.barre[k], pct[k], pct[k] >= 90 ? UI_ROUGE : k ? UI_FROID : UI_ACCENT);
            if (!reset[k] || reset[k] <= maintenant)
            {
                ui_label_texte(o.reset[k], k ? TXT("réinitialisation inconnue", "unknown reset") : TXT("pas de fenêtre en cours", "no current session"));
                continue;
            }
            struct tm lt;
            const time_t tt = reset[k];
            localtime_r(&tt, &lt);
            if (k == 0 || reset[k] - maintenant < 86400)
                snprintf(t, sizeof t, TXT("%02dh%02d", "%02d:%02d"), lt.tm_hour, lt.tm_min);
            else
                snprintf(t, sizeof t, TXT("%s %02d/%02d %02dh%02d", "%s %02d/%02d %02d:%02d"),
                     JOURS[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1, lt.tm_hour, lt.tm_min);
            dans(d, sizeof d, (int32_t)(reset[k] - maintenant));
            ui_label_texte_fmt(o.reset[k], TXT("réinitialisation %s · %s", "reset %s · %s"), t, d);
        }
    }
}

// =====================================================================  PAGE
static void onglet_cb(lv_event_t *e)
{
    g_confirme_veille = false;
    g_vue = (int)(intptr_t)lv_event_get_user_data(e);
    construire();
}

static char g_cle[48];
static void cle_structure(char *b, size_t n)
{
    const pc_gpu_t *g = pc_etat.gpus;
    snprintf(b, n, "%d %d %d %d %d", pc_etat.en_veille && g_vue != 3, g_vue, g_tri,
             g_vue == 2 ? LV_MIN(pc_etat.nb_procs, PC_PROCS) : 0,
             g_vue == 1 ? g[0].nb_blocs * 100 + g[1].nb_blocs * 10 + g[2].nb_blocs : 0); // GPU : un bloc de plus ou de moins
}

static void maj_valeurs()
{
    if (g_vue == 3) // retenu : aussi PC en veille
        maj_ia();
    else if (pc_etat.en_veille)
        ui_label_texte_fmt(o_veille_txt, TXT("plus de nouvelles depuis %s", "no news for %s"), pc_etat.derniere_nouvelle);
    else if (g_vue == 0)
        maj_systeme();
    else if (g_vue == 1)
        maj_gpu();
    else
        maj_processus();
}

static void construire()
{
    lv_obj_clean(g_zone);
    static const char *const ONGLETS[] = {TXT("Système", "System"), "GPU", TXT("Processus", "Processes"), TXT("IA", "AI")};
    ui_onglets(g_zone, ONGLETS, 4, g_vue, onglet_cb, nullptr, nullptr);
    if (g_vue == 3)
        construire_ia(g_zone);
    else if (pc_etat.en_veille)
        construire_veille(g_zone);
    else if (g_vue == 0)
        construire_systeme(g_zone);
    else if (g_vue == 1)
        construire_gpu(g_zone);
    else
        construire_processus(g_zone);
    cle_structure(g_cle, sizeof g_cle);
    maj_valeurs();
}

void page_pc_maj()
{
    if (!g_zone)
        return;
    char cle[sizeof g_cle];
    cle_structure(cle, sizeof cle);
    if (strcmp(cle, g_cle))
        construire();
    else
        maj_valeurs(); // une fois par seconde au plus (rythme de l'agent), sur place
}

void page_pc_creer(lv_obj_t *zone)
{
    g_zone = zone;
    construire();
}

void page_pc_quitter()
{
    g_confirme_veille = false;
    g_zone = nullptr;
}
