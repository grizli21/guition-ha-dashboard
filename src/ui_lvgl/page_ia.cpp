// 11/11/2026 : page « IA » (maquette validée le 10/11, voir page_ia.h). Une seule carte de
// 380 x 456 : scène pixel (images générées par tools/gen_scene_ia.py) collée en BAS de la carte — sa
// première ligne de ciel est exactement UI_CARTE en RVB565, la soudure en haut est invisible —, jauges
// posées sur le ciel, bande d'activité sur la terre, Clawd et ses marques en petits objets séparés.
// Comme le 27/10 pour la page PC : la structure n'est construite qu'une fois, les valeurs sont posées
// SUR PLACE et seulement si elles changent (jamais le fond à chaque message).
#include "page_ia.h"
#include "ui_psram.h"
#include "ui_theme.h"
#include "ui_mqtt.h" // ui_maison.nuit : la phase publiée par HA (maison/etat) choisit le ciel
#include "icones_ia.h"
#include "ui_appareil.h" // 12/11 : ui_appareil_eteint (la pluie s'arrête écran éteint)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

UI_PSRAM(ia_compte_t, ia_compte_p, 1, nullptr);    // en PSRAM, comme pc_etat (ui_psram.h)
UI_PSRAM(ia_activite_t, ia_activite_p, 1, nullptr);

static lv_obj_t *g_zone;
static lv_timer_t *g_tic; // 1 s : les compte à rebours (« dans 2 h 13 », « lu il y a… », péremption 2 min)

// --- géométrie de la carte (pixels dans la carte ; scène 380 x 412 posée en bas, à y = 44) ---
#define CARTE_L 380
#define CARTE_H 456
#define SCENE_Y 44                       // haut de la scène dans la carte (456 - 412 : le ciel gagné en haut)
#define SCENE_H 412                      // hauteur de la scène = hauteur de la colonne répétée (95 x 103 cases x4)
#define JAUGE_Y(k) (50 + (k) * 72)       // « 5 heures » puis « Semaine » (mêmes hauteurs que l'onglet IA du 31/10)
#define JAUGE_L 356                      // largeur des barres (comme l'onglet IA de la page PC)
#define Y0 344                           // haut de la bande d'activité (44 + 75 cases x 4 : sous l'herbe)
#define HISTO_BASE (Y0 + 94)             // ligne de base de l'histogramme (438) ; les barres s'arrêtent juste au-dessus
#define HISTO_H 32                       // hauteur max d'une barre
#define HISTO_X0 14                      // « −5 h » / « maintenant » sous cette ligne, dans les marges de la carte
// positions dans la carte (repères de tools/gen_scene_ia.py : cases de scène x4, scène à y = 44)
#define CLAWD_X (26 * 4)            // Clawd : case (26, 57) de la scène
#define CLAWD_Y (SCENE_Y + 57 * 4)
#define PENSEE_X (144)              // « + » et carrés de la pensée : coin (36, 48)
#define PENSEE_Y (SCENE_Y + 192)
#define EXCL_X (148)                // le « ! » (attente) : coin (37, 47)
#define EXCL_Y (SCENE_Y + 188)
#define Z_X (156)                   // les deux « z » (repos) : coin (39, 47)
#define Z_Y (SCENE_Y + 188)

// objets dont les valeurs changent (structure construite une fois)
static lv_obj_t *o_ciel, *o_etoiles, *o_lune, *o_soleil, *o_nuage1, *o_nuage2, *o_touffes, *o_buisson;
static lv_obj_t *o_nom, *o_maj, *o_val[2], *o_barre[2], *o_reset[2];
static lv_obj_t *o_etat, *o_tokens, *o_sessions, *o_outils;
static lv_obj_t *o_clawd, *o_pensee, *o_excl, *o_z, *o_histo[IA_HISTO];
// 12/11 : ciel animé (maison/ciel) — voir le module CIEL ANIMÉ
#define NUAGES 6
#define COULOIRS 20
static lv_obj_t *o_nuage[NUAGES], *o_scint[3], *o_lune_c, *o_etoiles_haut, *o_voile, *o_sol_neige, *o_neige_buisson;
static lv_obj_t *o_brouillard = nullptr; // 13/11 : brume du sol, en dégradé (voir brume())
static lv_obj_t *o_couloir[COULOIRS], *o_motif[COULOIRS];
static lv_timer_t *g_precip_tic;

static uint32_t charge_couleur(int pct) { return pct >= 90 ? UI_ROUGE : pct >= 75 ? UI_ACCENT : UI_TEXTE; }

// 27/10 (audit des redessins, page PC) : lv_obj_set_size redessine même à l'identique — on ne taille que si ça change
static void taille(lv_obj_t *o, lv_coord_t w, lv_coord_t h)
{
    if (lv_obj_get_style_width(o, LV_PART_MAIN) != w || lv_obj_get_style_height(o, LV_PART_MAIN) != h)
        lv_obj_set_size(o, w, h);
}

// barre horizontale fine (fond + partie pleine) : même dessin que la page PC, valeurs : barre_maj
static lv_obj_t *barre(lv_obj_t *p, lv_coord_t l, lv_coord_t h)
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
    return b;
}
static void barre_maj(lv_obj_t *b, int pct, uint32_t c)
{
    const lv_coord_t l = lv_obj_get_style_width(b, LV_PART_MAIN), h = lv_obj_get_style_height(b, LV_PART_MAIN);
    taille(lv_obj_get_child(b, 0), l * LV_CLAMP(0, pct, 100) / 100, h);
    ui_style_bg_color(lv_obj_get_child(b, 0), ui_c(c), 0);
}

// « dans 2 h 13 », « dans 35 min », « dans 1 j 4 h » (repris de l'onglet IA du 31/10, page PC)
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

// nombre à la française, séparateur de milliers par une espace : 12345 -> « 12 345 »
static void milliers(char *b, size_t n, uint32_t v)
{
    char s[12];
    snprintf(s, sizeof s, "%u", v);
    const int l = (int)strlen(s);
    int k = 0;
    for (int i = 0; i < l && k < (int)n - 1; i++)
    {
        if (i && (l - i) % 3 == 0)
            b[k++] = ' ';
        b[k++] = s[i];
    }
    b[k] = 0;
}

// =====================================================================  CONSTRUCTION
// =====================================================================  CIEL ANIMÉ (12/11, maison/ciel)
// HA publie les PROCHAINS aube / lever / coucher / crépuscule (soleil), la lune (âge en jours, % éclairé)
// — voir ha_propositions/ciel_dashboard_guition.yaml — et la météo OpenWeatherMap. L'écran en déduit la phase et place lui-même soleil et lune sur leur arc.
// Sans ce sujet (ou périmé), la page garde l'ancien ciel jour/nuit de maison/etat.
//
// COÛT (12/11 au soir, après une 1re version qui redessinait tout l'écran à chaque image : 28 gouttes animées une à
// une = jusqu'à 56 zones par image ; au-delà de LV_INV_BUF_SIZE = 32 zones, LVGL redessine l'écran ENTIER, 151 ms) :
//  - pluie / neige : au plus COULOIRS couloirs de 4 px de large ; dans chacun un motif (2 gouttes ou 2 flocons sur
//    24 cases, répété) glisse par pas d'une case. Le motif déborde du couloir, qui le coupe : ancienne et nouvelle
//    position couvrent le couloir entier = UNE zone par couloir. Un seul minuteur (66 ms) les fait tous avancer ;
//  - étoiles qui scintillent : opacité d'IMAGE (pas l'opacité d'objet, qui passe par un calque), par paliers ;
//  - éclair : allumé / éteint (2 redessins du ciel par éclair, pas un fondu) ;
//  - teintes et opacités posées seulement quand elles changent (un style reposé redessine même à l'identique) ;
//  - soleil, lune : lv_obj_set_pos ne redessine rien si la case n'a pas changé ; nuages : 1 case toutes les 4-6 s ;
//  - écran éteint : la pluie s'arrête (rien à montrer).
uint32_t page_ia_heure_test = 0; // simulateur : heure imposée (0 = l'horloge de l'écran)
static uint32_t maintenant() { return page_ia_heure_test ? page_ia_heure_test : (uint32_t)time(nullptr); }

enum
{
    C_NUIT,
    C_AUBE,
    C_JOUR,
    C_CREPUSCULE
};
// arc du soleil et de la lune, en cases de scène (4 px) : centre au lever (EST, HORIZON) -> au plus haut (ZENITH)
#define ARC_EST 6
#define ARC_OUEST 88
#define ARC_HORIZON 64     // les rais du soleil (± 7 cases) s'arrêtent juste au-dessus de l'herbe (case 73)
#define ARC_ZENITH 42      // le rai du haut (centre - 7) s'arrête à CIEL_HAUT (case 35), sous la jauge du bas
#define ARC_ZENITH_LUNE 40 // la lune (rayon 5) : son haut aussi à la case 35
#define LUNE_R 5           // rayon de la lune en cases (11 x 11 cases, comme la lune fixe d'avant)
#define JOUR_S 86400u
#define LUNAISON_J 29.530588f
#define CIEL_HAUT 35       // 12/11 (retour d'usage) : nuages, pluie, neige, étoiles commencent sous la jauge du bas (case 35)
#define PRECIP_H ((73 - CIEL_HAUT) * 4) // pluie et neige : de la case 35 jusqu'à l'herbe
#define MOTIF_H (2 * PRECIP_H + 24) // motif : deux fois le couloir + 6 cases, la goutte à PRECIP_H + 8 (tools/gen_scene_ia.py)
#define MOTIF_DEPART (-(PRECIP_H + 8) - 12) // goutte 12 px au-dessus du couloir
#define MOTIF_FIN (-8)                      // goutte sortie par le bas : le couloir change de colonne

// état posé (pour ne reposer un style que s'il change) ; remis à -1 à chaque construction de la page
static int g_teinte_nuages = -1, g_precip = -1, g_voile = -1, g_neige_sol = -1, g_brume = -1, g_lune_dessin = -1;
static int g_nb_couloirs; // couloirs actifs
static uint32_t g_tics;
// chaque couloir porte UNE goutte (ou un flocon) ; quand elle sort par le bas, le couloir — vide à ce moment —
// change de colonne, de goutte, de vitesse, et attend un peu (retour d'usage : « rien d'aléatoire », « couloirs fixes »)
static struct
{
    int16_t pos;                  // y du motif dans le couloir (MOTIF_DEPART -> MOTIF_FIN)
    uint8_t col, pas, attente, pause; // colonne (case), pas en px, tous les « attente » tics, tics avant de repartir
} g_c[COULOIRS];
static bool g_neige_tombe;
static uint32_t g_hasard = 12345;
static uint32_t hasard(uint32_t n)
{
    g_hasard = g_hasard * 1103515245u + 12345u;
    return (g_hasard >> 8) % n;
}

static bool ciel_valide(uint32_t n)
{
    const ui_ciel_t &c = ui_ciel;
    return c.recu && n > 1700000000u && c.lever + JOUR_S > n && c.coucher + JOUR_S > n; // pas périmé (> 1 jour)
}

// la phase d'après l'ORDRE des prochains événements (pas d'heure locale à calculer) : prochain coucher avant
// prochain lever = il fait jour ; crépuscule entre coucher et fin du crépuscule ; aube entre l'aube et le lever
static int ciel_phase()
{
    const ui_ciel_t &c = ui_ciel;
    if (c.coucher < c.lever)
        return C_JOUR;
    if (c.crepuscule && c.crepuscule < c.aube && c.crepuscule < c.coucher)
        return C_CREPUSCULE;
    if (c.aube && c.lever < c.aube)
        return C_AUBE;
    return C_NUIT;
}

// position sur l'arc pour t dans [0, 1] (0 = lever à l'est, 1 = coucher à l'ouest)
static void arc(float t, int zenith, int &cx, int &cy)
{
    cx = ARC_EST + (int)lroundf(t * (ARC_OUEST - ARC_EST));
    cy = ARC_HORIZON - (int)lroundf((ARC_HORIZON - zenith) * sinf(3.14159265f * t));
}

// lune en cases 4 x 4 : partie éclairée à droite quand elle croît (hémisphère nord), le reste à peine
// visible (lumière cendrée). Redessinée au changement seul.
static lv_draw_buf_t *g_lune_buf;
static void lune_dessiner(int pct, bool croissante)
{
    const int cle = pct * 2 + croissante;
    if (cle == g_lune_dessin)
        return;
    g_lune_dessin = cle;
    const int n = 2 * LUNE_R + 1;
    lv_canvas_fill_bg(o_lune_c, lv_color_black(), LV_OPA_TRANSP);
    const float t = 2 * (pct / 100.0f) - 1, R = LUNE_R + 0.5f;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
        {
            const int dx = i - LUNE_R, dy = j - LUNE_R;
            if (dx * dx + dy * dy > LUNE_R * LUNE_R + 2) // même disque que la lune fixe de la maquette
                continue;
            const float xn = dx / R, yn = dy / R, lx = sqrtf(fmaxf(0.0f, 1 - yn * yn));
            const bool eclaire = croissante ? (xn >= -t * lx) : (xn <= t * lx);
            const lv_color_t col = eclaire ? lv_color_hex(0xEDE6CC) : lv_color_hex(0x3A3658);
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                    lv_canvas_set_px(o_lune_c, i * 4 + x, j * 4 + y, col, eclaire ? LV_OPA_COVER : LV_OPA_40);
        }
}

// scintillement par paliers de 64 : l'image n'est redessinée qu'au changement de palier (pas à chaque image)
static void scint_anim(void *o, int32_t v)
{
    const lv_opa_t p = (lv_opa_t)LV_MIN(255, (v + 32) / 64 * 64);
    if (lv_obj_get_style_image_opa((lv_obj_t *)o, LV_PART_MAIN) != p)
        lv_obj_set_style_image_opa((lv_obj_t *)o, p, 0);
}

static lv_obj_t *image(lv_obj_t *c, const lv_image_dsc_t *src, int x, int y)
{
    lv_obj_t *o = lv_img_create(c);
    lv_img_set_src(o, src);
    lv_obj_set_pos(o, x, y);
    return o;
}

static void voile_teinte(uint32_t c, lv_opa_t bas)
{
    ui_style_bg_color(o_voile, ui_c(c), 0);
    ui_style_bg_grad_color(o_voile, ui_c(c), 0);
    lv_obj_set_style_bg_grad_opa(o_voile, bas, 0);
}

static void precip_tic(lv_timer_t *);

// objets du ciel, posés DERRIÈRE Clawd et le buisson (appelé dans construire(), après les deux nuages d'origine)
static void ciel_construire(lv_obj_t *c)
{
    g_teinte_nuages = g_precip = g_voile = g_neige_sol = g_brume = g_lune_dessin = -1;
    g_nb_couloirs = 0;
    const int n = (2 * LUNE_R + 1) * 4;
    if (!g_lune_buf) // gardé d'une ouverture de page à l'autre (7,7 Ko)
        g_lune_buf = lv_draw_buf_create(n, n, LV_COLOR_FORMAT_ARGB8888, 0);
    o_lune_c = lv_canvas_create(c);
    lv_canvas_set_draw_buf(o_lune_c, g_lune_buf);
    ui_cacher(o_lune_c, true);
    lv_obj_move_to_index(o_lune_c, lv_obj_get_index(o_soleil)); // derrière les nuages, comme la lune fixe
    // étoiles du haut du ciel (cases 6..46, derrière les jauges) : A8 teintée en blanc
    o_etoiles_haut = image(c, &etoiles_haut_ia, 8, SCENE_Y + 140); // boîte x 2..89, y 35..45 (tools/gen_scene_ia.py)
    lv_obj_set_style_image_recolor(o_etoiles_haut, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(o_etoiles_haut, LV_OPA_COVER, 0);
    lv_obj_move_to_index(o_etoiles_haut, lv_obj_get_index(o_etoiles) + 1);
    ui_cacher(o_etoiles_haut, true);
    // trois étoiles qui scintillent, sur les trois croix du calque d'étoiles (cases (70,50), (52,62), (88,58))
    static const int16_t CROIX[3][2] = {{70, 50}, {52, 62}, {88, 58}};
    static const uint16_t PERIODE[3] = {2300, 3100, 4200}; // décalées : jamais ensemble
    for (int k = 0; k < 3; k++)
    {
        o_scint[k] = image(c, &scintille_ia, (CROIX[k][0] - 1) * 4, SCENE_Y + (CROIX[k][1] - 1) * 4);
        lv_obj_set_style_image_opa(o_scint[k], LV_OPA_TRANSP, 0);
        lv_obj_move_to_index(o_scint[k], lv_obj_get_index(o_etoiles_haut) + 1);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, o_scint[k]);
        lv_anim_set_exec_cb(&a, scint_anim);
        lv_anim_set_values(&a, 0, 255);
        lv_anim_set_duration(&a, 700);
        lv_anim_set_reverse_duration(&a, 900);
        lv_anim_set_repeat_delay(&a, PERIODE[k]);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
        ui_cacher(o_scint[k], true);
    }
    // 4 nuages de plus (6 en tout par ciel très couvert), étagés entre les cases 38 et 56
    static const struct { const lv_image_dsc_t *src; int16_t x, y; } NX[NUAGES - 2] = {
        {&nuage3_ia, 40, 49}, {&nuage2_ia, 120, 39}, {&nuage1_ia, 300, 42}, {&nuage3_ia, 170, 54}};
    o_nuage[0] = o_nuage1;
    o_nuage[1] = o_nuage2;
    for (int k = 2; k < NUAGES; k++)
    {
        o_nuage[k] = image(c, NX[k - 2].src, NX[k - 2].x, SCENE_Y + NX[k - 2].y * 4);
        ui_cacher(o_nuage[k], true);
    }
    // voile du ciel : dégradé vertical, transparent en haut (sous les jauges), plus sombre vers l'herbe (averse,
    // orage) ; l'éclair le passe au blanc un instant
    o_voile = ui_conteneur(c);
    lv_obj_set_size(o_voile, CARTE_L, 73 * 4);
    lv_obj_set_pos(o_voile, 0, SCENE_Y);
    ui_style_bg_opa(o_voile, LV_OPA_COVER, 0);
    ui_style_bg_grad_dir(o_voile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(o_voile, LV_OPA_TRANSP, 0);
    voile_teinte(0x252B36, 120);
    ui_cacher(o_voile, true);
    // pluie et neige : couloirs (voir COÛT en tête du module)
    for (int k = 0; k < COULOIRS; k++)
    {
        o_couloir[k] = ui_conteneur(c);
        lv_obj_set_size(o_couloir[k], 4, PRECIP_H);
        lv_obj_set_y(o_couloir[k], SCENE_Y + CIEL_HAUT * 4);
        o_motif[k] = lv_img_create(o_couloir[k]);
        lv_obj_set_size(o_motif[k], 4, MOTIF_H);
        lv_obj_set_style_image_recolor_opa(o_motif[k], LV_OPA_COVER, 0);
        ui_cacher(o_couloir[k], true);
    }
    o_sol_neige = ui_conteneur(c); // les deux rangs d'herbe passent au blanc quand il neige
    lv_obj_set_size(o_sol_neige, CARTE_L, 2 * 4);
    lv_obj_set_pos(o_sol_neige, 0, SCENE_Y + 73 * 4);
    ui_style_bg_color(o_sol_neige, ui_c(0xEEF2F6), 0);
    ui_style_bg_opa(o_sol_neige, LV_OPA_COVER, 0);
    ui_cacher(o_sol_neige, true);
    g_precip_tic = lv_timer_create(precip_tic, 66, nullptr);
}

// une colonne libre au hasard, sans voisine collée (deux couloirs côte à côte feraient un trait épais)
static uint8_t colonne_libre(int moi)
{
    for (;;)
    {
        const int x = (int)hasard(95);
        bool ok = true;
        for (int k = 0; k < g_nb_couloirs && ok; k++)
            ok = k == moi || abs((int)g_c[k].col - x) > 1;
        if (ok)
            return (uint8_t)x;
    }
}

// nouveau départ d'un couloir (vide) : colonne, goutte, vitesse, attente au hasard
static void couloir_relancer(int k, int pause_max)
{
    static const lv_image_dsc_t *const GOUTTE[3] = {&goutte_ia_0, &goutte_ia_1, &goutte_ia_2};
    static const lv_image_dsc_t *const FLOCON[2] = {&flocon_ia_0, &flocon_ia_1};
    g_c[k].col = colonne_libre(k);
    lv_obj_set_x(o_couloir[k], g_c[k].col * 4);
    lv_img_set_src(o_motif[k], g_neige_tombe ? FLOCON[hasard(2)] : GOUTTE[hasard(3)]);
    g_c[k].pos = MOTIF_DEPART;
    g_c[k].pas = (uint8_t)(g_neige_tombe ? 4 : (g_precip == 3 ? 16 : 12) + hasard(3) * 4); // pluie 12-20 px par tic, averse 16-24
    g_c[k].attente = (uint8_t)(g_neige_tombe ? 2 + hasard(2) : 1);                          // neige : 4 px tous les 2 ou 3 tics
    g_c[k].pause = (uint8_t)hasard((uint32_t)pause_max + 1);
    lv_obj_set_y(o_motif[k], MOTIF_DEPART);
}

// un seul minuteur pour toute la pluie (ou neige) et l'éclair ; rien quand l'écran est éteint
static void precip_tic(lv_timer_t *)
{
    if (ui_appareil_eteint())
        return;
    g_tics++;
    for (int k = 0; k < g_nb_couloirs; k++)
    {
        if (g_c[k].pause)
        {
            g_c[k].pause--;
            continue;
        }
        if (g_tics % g_c[k].attente)
            continue;
        g_c[k].pos = (int16_t)(g_c[k].pos + g_c[k].pas);
        if (g_c[k].pos >= MOTIF_FIN)
            couloir_relancer(k, g_neige_tombe ? 20 : 8);
        else
            lv_obj_set_y(o_motif[k], g_c[k].pos);
    }
    if (g_voile == 2) // orage : un éclair de 2 tics toutes les ~9 s
    {
        const uint32_t t = g_tics % 136;
        if (t == 0)
            voile_teinte(0xE8ECFF, 200);
        else if (t == 2)
            voile_teinte(0x252B36, 120);
    }
}

// 0 = rien, 1-3 = pluie faible / moyenne / forte, 4 = neige
static void precip(int type)
{
    if (type == g_precip)
        return;
    g_precip = type;
    static const uint8_t NB[5] = {0, 8, 13, COULOIRS, 14};
    g_neige_tombe = type == 4;
    g_nb_couloirs = 0;
    for (int k = 0; k < COULOIRS; k++)
        ui_cacher(o_couloir[k], true);
    for (int k = 0; k < NB[type]; k++)
    {
        g_nb_couloirs = k + 1;
        couloir_relancer(k, g_neige_tombe ? 60 : 15); // départs étalés : pas tous en même temps
        lv_obj_set_style_image_recolor(o_motif[k], lv_color_hex(g_neige_tombe ? 0xF4F7FA : 0x9DB8D2), 0);
        ui_cacher(o_couloir[k], false);
    }
}

static void voile(int mode) // 0 = rien, 1 = sombre (averse), 2 = orage (sombre + éclairs)
{
    if (mode == g_voile)
        return;
    g_voile = mode;
    ui_cacher(o_voile, mode == 0);
    voile_teinte(0x252B36, 120);
}

// paysage enneigé pendant qu'il neige : herbe blanche, touffes blanchies, neige sur le buisson
static void paysage_neige(bool oui)
{
    if (oui == g_neige_sol)
        return;
    g_neige_sol = oui;
    ui_cacher(o_sol_neige, !oui);
    ui_cacher(o_neige_buisson, !oui);
    lv_obj_set_style_image_recolor(o_touffes, lv_color_hex(0xEEF2F6), 0);
    lv_obj_set_style_image_recolor_opa(o_touffes, oui ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

// brouillard (13/11, retour d'usage : « il s'étend plus haut mais dégressif ») : une brume qui part du sol, épaisse sur l'herbe,
// et s'éclaircit en montant jusqu'à disparaître sous les jauges ; posée DEVANT Clawd et le buisson (leurs pieds plus
// voilés que leur tête). Claire le jour, sombre la nuit. Remplace l'estompage de Clawd et du buisson du 12/11.
#define BRUME_BAS 190      // opacité au sol, le jour
#define BRUME_BAS_NUIT 235 // la nuit
static void brume(bool oui, bool nuit)
{
    const int cle = oui ? 1 + nuit : 0;
    if (cle == g_brume)
        return;
    g_brume = cle;
    ui_cacher(o_brouillard, !oui);
    if (!oui)
        return;
    // la nuit, plus dense et un peu plus claire que le ciel : les étoiles s'éteignent en descendant (13/11)
    const lv_color_t c = ui_c(nuit ? 0x48526A : 0xD3D9E0);
    ui_style_bg_color(o_brouillard, c, 0);
    ui_style_bg_grad_color(o_brouillard, c, 0);
    lv_obj_set_style_bg_grad_opa(o_brouillard, nuit ? BRUME_BAS_NUIT : BRUME_BAS, 0);
}

static void ciel_nuages(int nb, int teinte, bool gris)
{
    for (int k = 0; k < NUAGES; k++)
        ui_cacher(o_nuage[k], k >= nb);
    // la nuit, sombres devant les étoiles ; à l'aube et au crépuscule, teinte de l'horizon ; sous la pluie, gris
    const int cle = teinte * 2 + gris;
    if (cle == g_teinte_nuages)
        return;
    g_teinte_nuages = cle;
    const uint32_t c = teinte == C_NUIT ? 0x2B2A45 : teinte == C_AUBE ? 0xE7A08C : teinte == C_CREPUSCULE ? 0xC8607A : 0x7D8794;
    const lv_opa_t o = teinte == C_NUIT ? 170 : teinte != C_JOUR ? (gris ? 150 : 110) : gris ? 120 : 0;
    for (lv_obj_t *n : o_nuage)
    {
        lv_obj_set_style_image_recolor(n, lv_color_hex(c), 0);
        lv_obj_set_style_image_recolor_opa(n, o, 0);
    }
}

// dérive des nuages (tic de 1 s) : une case (4 px) toutes les 4 à 6 s, chacun à son pas, retour à gauche en sortant
static void ciel_deriver()
{
    static uint8_t compte;
    compte++;
    static const uint8_t PAS[NUAGES] = {4, 6, 5, 7, 5, 6};
    for (int k = 0; k < NUAGES; k++)
        if (!lv_obj_has_flag(o_nuage[k], LV_OBJ_FLAG_HIDDEN) && compte % PAS[k] == 0)
        {
            int x = lv_obj_get_x(o_nuage[k]) + 4;
            if (x >= CARTE_L)
                x = -lv_obj_get_width(o_nuage[k]);
            lv_obj_set_x(o_nuage[k], x);
        }
}

// ancien ciel jour/nuit (sans maison/ciel) : comme avant le 12/11
static void ciel_repli()
{
    const bool nuit = ui_maison.nuit;
    ui_img_src(o_ciel, nuit ? &colonne_ia_nuit : &colonne_ia_jour);
    ui_img_src(o_touffes, nuit ? &touffes_ia_nuit : &touffes_ia_jour);
    ui_cacher(o_etoiles, !nuit);
    ui_cacher(o_etoiles_haut, !nuit);
    for (lv_obj_t *o : o_scint)
        ui_cacher(o, !nuit);
    ui_cacher(o_lune, !nuit);
    ui_cacher(o_lune_c, true);
    ui_cacher(o_soleil, nuit);
    lv_obj_set_pos(o_soleil, 24, SCENE_Y + 196);
    ciel_nuages(nuit ? 0 : 2, nuit ? C_NUIT : C_JOUR, false);
    precip(0);
    voile(0);
    paysage_neige(false);
    brume(false, false);
}

// pose tout le ciel d'après maison/ciel (appelé chaque seconde : seuls les changements redessinent)
static void ciel_maj()
{
    const uint32_t n = maintenant();
    if (!ciel_valide(n))
    {
        ciel_repli();
        return;
    }
    const ui_ciel_t &c = ui_ciel;
    const int phase = ciel_phase();
    const bool couvert = c.nuages >= 85; // ciel bouché : ni soleil ni lune (les nuages restent)
    // soleil : de jour seulement, sur l'arc entre le lever du jour (prochain lever - 24 h) et le prochain coucher
    float ts = -1;
    if (phase == C_JOUR)
    {
        const uint32_t l = c.lever - JOUR_S;
        ts = c.coucher > l ? fminf(1.0f, fmaxf(0.0f, (float)(n - l) / (float)(c.coucher - l))) : 0.5f;
    }
    // ciel : orangé aux 6 premiers et derniers % du jour (soleil bas), sinon celui de la phase
    const int teinte = phase == C_JOUR ? (ts < 0.06f ? C_AUBE : ts > 0.94f ? C_CREPUSCULE : C_JOUR) : phase;
    static const lv_image_dsc_t *const COLONNE[4] = {&colonne_ia_nuit, &colonne_ia_aube, &colonne_ia_jour, &colonne_ia_crepuscule};
    ui_img_src(o_ciel, COLONNE[teinte]);
    ui_img_src(o_touffes, teinte == C_JOUR ? &touffes_ia_jour : &touffes_ia_nuit);
    ui_cacher(o_lune, true); // la lune fixe de la maquette laisse la place à la vraie
    ui_cacher(o_soleil, ts < 0 || couvert);
    if (ts >= 0)
    {
        int cx, cy;
        arc(ts, ARC_ZENITH, cx, cy);
        lv_obj_set_pos(o_soleil, (cx - 7) * 4, SCENE_Y + (cy - 7) * 4); // soleil_ia : 15 x 15 cases, centre (7, 7)
    }
    // lune : se lève environ 50 min plus tard chaque jour -> lever ≈ lever du soleil + âge x 24 h / lunaison, visible
    // ~12,4 h ; montrée hors du plein jour
    const uint32_t lever_soleil = c.lever > n ? c.lever - JOUR_S : c.lever;
    int64_t lever_lune = (int64_t)lever_soleil + (int64_t)(c.lune_age / LUNAISON_J * JOUR_S);
    if (lever_lune > (int64_t)n)
        lever_lune -= 89400; // 24 h 50 : le lever de la veille
    const float tl = (float)((int64_t)n - lever_lune) / (12.4f * 3600);
    const bool lune = phase != C_JOUR && tl >= 0 && tl <= 1 && !couvert;
    ui_cacher(o_lune_c, !lune);
    if (lune)
    {
        lune_dessiner(c.lune_pct, c.lune_age < LUNAISON_J / 2);
        int cx, cy;
        arc(tl, ARC_ZENITH_LUNE, cx, cy);
        lv_obj_set_pos(o_lune_c, (cx - LUNE_R) * 4, SCENE_Y + (cy - LUNE_R) * 4);
    }
    // météo
    const bool neige = c.neige > 0 || strstr(c.meteo, "snow");
    const bool pluie = c.pluie > 0 || strstr(c.meteo, "rain") || strstr(c.meteo, "pouring") || strstr(c.meteo, "lightning-rainy");
    const bool orage = strstr(c.meteo, "lightning") != nullptr;
    const bool forte = c.pluie >= 4 || strstr(c.meteo, "pouring");
    const bool gris = neige || pluie || orage || strstr(c.meteo, "hail");
    // étoiles : la nuit (pas à l'aube ni au crépuscule), ni sous un ciel couvert, ni quand il pleut ou neige
    const bool etoiles = phase == C_NUIT && c.nuages < 75 && !gris;
    ui_cacher(o_etoiles, !etoiles);
    ui_cacher(o_etoiles_haut, !etoiles);
    for (lv_obj_t *o : o_scint)
        ui_cacher(o, !etoiles);
    // nuages : de 0 à 6 selon la couverture
    const int nb = c.nuages < 15 ? 0 : c.nuages < 30 ? 1 : c.nuages < 50 ? 2 : c.nuages < 65 ? 3 : c.nuages < 80 ? 4 : c.nuages < 90 ? 5 : 6;
    ciel_nuages(nb, teinte, gris);
    precip(neige ? 4 : !pluie ? 0 : forte ? 3 : c.pluie >= 1 ? 2 : 1);
    paysage_neige(neige);
    voile(orage ? 2 : forte ? 1 : 0);
    brume(strstr(c.meteo, "fog") != nullptr, teinte == C_NUIT);
}

static void construire()
{
    lv_obj_t *c = ui_conteneur(g_zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, CARTE_L, CARTE_H);
    lv_obj_set_pos(c, UI_MARGE, UI_MARGE);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_clip_corner(c, true, 0); // la scène est rectangulaire : elle s'arrête aux coins ronds

    // fond (soir du 11/11, -488 Ko de flash) : plus de deux images 380 x 412 mais une COLONNE 1 x 412
    // (bandes horizontales ciel/herbe/terre) répétée sur toute la largeur par LVGL — LV_IMAGE_ALIGN_TILE,
    // supporté en 9.6 (lv_image.c:908 : l'objet est rempli de carreaux ; lv_draw_image.c:276 : le décodeur
    // s'ouvre UNE fois puis boucle sur les carreaux, chaque carreau 1 x 412 est un blend direct).
    // Par-dessus, les petits décors posés aux positions affichées par tools/gen_scene_ia.py (mêmes pixels
    // que l'ancien fond : l'outil le vérifie à la génération). Clawd et marques restent au-dessus.
    o_ciel = lv_img_create(c);
    lv_obj_set_pos(o_ciel, 0, SCENE_Y);
    lv_obj_set_size(o_ciel, CARTE_L, SCENE_H); // le tuilage remplit ce rectangle (l'image elle-même fait 1 px de large)
    lv_image_set_inner_align(o_ciel, LV_IMAGE_ALIGN_TILE);
    o_etoiles = lv_img_create(c); // nuit : 4 luminosités + 3 croix, calque unique (boîte x 2..94, y 48..66)
    lv_img_set_src(o_etoiles, &etoiles_ia);
    lv_obj_set_pos(o_etoiles, 8, SCENE_Y + 192);
    o_lune = lv_img_create(c);
    lv_img_set_src(o_lune, &lune_ia);
    lv_obj_set_pos(o_lune, 32, SCENE_Y + 204);
    o_soleil = lv_img_create(c); // jour : disque + rais (boîte x 6..20, y 49..63)
    lv_img_set_src(o_soleil, &soleil_ia);
    lv_obj_set_pos(o_soleil, 24, SCENE_Y + 196);
    o_nuage1 = lv_img_create(c); // nuages (déplacés le 11/11 hors de la zone des marques)
    lv_img_set_src(o_nuage1, &nuage1_ia);
    lv_obj_set_pos(o_nuage1, 208, SCENE_Y + 180);
    o_nuage2 = lv_img_create(c);
    lv_img_set_src(o_nuage2, &nuage2_ia);
    lv_obj_set_pos(o_nuage2, 264, SCENE_Y + 224);
    ciel_construire(c);
    o_touffes = lv_img_create(c); // touffes d'herbe : couleur selon la phase (deux morceaux)
    lv_obj_set_pos(o_touffes, 24, SCENE_Y + 284);
    o_buisson = lv_img_create(c); // buisson : mêmes verts jour et nuit, posé après les nuages (il les masque)
    lv_img_set_src(o_buisson, &buisson_ia);
    lv_obj_set_pos(o_buisson, 272, SCENE_Y + 228);
    o_neige_buisson = lv_img_create(c); // 12/11 : neige sur le buisson (boîte x 68..94, y 57..66)
    lv_img_set_src(o_neige_buisson, &neige_buisson_ia);
    lv_obj_set_pos(o_neige_buisson, 272, SCENE_Y + 228);
    ui_cacher(o_neige_buisson, true);
    // 12/11 (retour d'usage) : la pluie et la neige tombent DEVANT le buisson. La neige du buisson est ici le dernier objet :
    // « au premier plan » = juste après elle ; Clawd, ses marques et les textes, créés ensuite, restent devant
    for (lv_obj_t *o : o_couloir)
        lv_obj_move_foreground(o);

    // haut de carte : adresse du compte, âge de la donnée, jauges 5 h et semaine (décalques de l'onglet IA du 31/10)
    o_nom = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "");
    // mode DOT avec hauteur AUTO = piège (vu capture 182 du 11/11) : l'étiquette grandit et passe sur
    // les jauges. Une hauteur fixe d'une ligne (sb16) force l'ellipsize sur un seul rang.
    lv_label_set_long_mode(o_nom, LV_LABEL_LONG_DOT);
    lv_obj_set_size(o_nom, 230, 22);
    lv_obj_set_pos(o_nom, 14, 14);
    o_maj = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE3, "");
    lv_obj_align(o_maj, LV_ALIGN_TOP_RIGHT, -14, 17);
    static const char *const FENETRES[] = {TXT("5 heures", "5 hours"), TXT("Semaine", "Week")};
    for (int k = 0; k < 2; k++)
    {
        lv_obj_set_pos(ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, FENETRES[k]), 14, JAUGE_Y(k));
        o_val[k] = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "");
        lv_obj_align(o_val[k], LV_ALIGN_TOP_RIGHT, -14, JAUGE_Y(k) - 4);
        o_barre[k] = barre(c, JAUGE_L, 10);
        lv_obj_set_pos(o_barre[k], 14, JAUGE_Y(k) + 24);
        o_reset[k] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
        lv_obj_set_pos(o_reset[k], 14, JAUGE_Y(k) + 42);
    }

    // Clawd et ses marques : petits objets séparés (échange par ui_img_src / ui_cacher, jamais le fond)
    o_clawd = lv_img_create(c);
    lv_obj_set_pos(o_clawd, CLAWD_X, CLAWD_Y);
    o_pensee = lv_img_create(c);
    lv_img_set_src(o_pensee, &marque_ia_reflechit);
    lv_obj_set_pos(o_pensee, PENSEE_X, PENSEE_Y);
    o_excl = lv_img_create(c);
    lv_img_set_src(o_excl, &marque_ia_attente);
    lv_obj_set_pos(o_excl, EXCL_X, EXCL_Y);
    o_z = lv_img_create(c);
    lv_img_set_src(o_z, &marque_ia_repos);
    lv_obj_set_pos(o_z, Z_X, Z_Y);
    // brume du sol (brouillard) : devant Clawd, ses marques et le buisson, de la case 35 (transparente) à l'herbe comprise
    o_brouillard = ui_conteneur(c);
    lv_obj_set_size(o_brouillard, CARTE_L, Y0 - (SCENE_Y + CIEL_HAUT * 4));
    lv_obj_set_pos(o_brouillard, 0, SCENE_Y + CIEL_HAUT * 4);
    ui_style_bg_opa(o_brouillard, LV_OPA_COVER, 0);
    ui_style_bg_grad_dir(o_brouillard, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(o_brouillard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(o_brouillard, BRUME_BAS, 0);
    ui_cacher(o_brouillard, true);

    // bande de terre = panneau d'activité (maquette du 10/11) : état et tokens à gauche,
    // sessions/prompts et outils à droite, histogramme 30 tranches et ses légendes dessous.
    // Largeurs mesurées dans les TTF du maquette : « autorisation demandée » 193 px en sb16,
    // « 12 sessions · 987 prompts » ~150 px en sb12 — et LVGL coupe plus tôt que PIL (vu sur les captures
    // 177/180 du 11/11 : retour à la ligne et chevauchement). Les deux colonnes sont donc larges et CLIP
    // (jamais de retour à la ligne) : les zones se recouvrent, pas les textes (193+14 < 216 ≤ 366-150).
    o_etat = ui_label(c, UI_POLICE_TEXTE, UI_IA_ORANGE, "");
    lv_obj_set_width(o_etat, 234);
    lv_label_set_long_mode(o_etat, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(o_etat, 14, Y0 + 6);
    o_tokens = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_obj_set_pos(o_tokens, 14, Y0 + 28);
    o_sessions = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_width(o_sessions, 165);
    lv_label_set_long_mode(o_sessions, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(o_sessions, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(o_sessions, CARTE_L - 14 - 165, Y0 + 8);
    o_outils = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_width(o_outils, 165);
    lv_label_set_long_mode(o_outils, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(o_outils, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(o_outils, CARTE_L - 14 - 165, Y0 + 30);
    for (int i = 0; i < IA_HISTO; i++)
    {
        o_histo[i] = ui_conteneur(c);
        lv_obj_set_size(o_histo[i], 9, 0);
        ui_style_bg_opa(o_histo[i], LV_OPA_COVER, 0);
        // la dernière tranche (la tranche en cours) est la seule orange, les autres « terre claire »
        ui_style_bg_color(o_histo[i], ui_c(i == IA_HISTO - 1 ? UI_IA_ORANGE : UI_HISTO), 0);
        lv_obj_set_pos(o_histo[i], HISTO_X0 + (i * 354 + 15) / 30, HISTO_BASE - 1); // pas = (368-14)/30 = 11,8
    }
    lv_obj_t *ligne = ui_conteneur(c); // le trait de base de l'histogramme
    lv_obj_set_size(ligne, 354, 1);
    ui_style_bg_color(ligne, ui_c(UI_HISTO_LIGNE), 0);
    ui_style_bg_opa(ligne, LV_OPA_COVER, 0);
    lv_obj_set_pos(ligne, HISTO_X0, HISTO_BASE);
    lv_obj_set_pos(ui_label(c, UI_POLICE_LEGENDE, UI_IA_LEGENDE, TXT("−5 h", "−5 h")), HISTO_X0, HISTO_BASE + 2);
    lv_obj_t *lg2 = ui_label(c, UI_POLICE_LEGENDE, UI_IA_LEGENDE, TXT("maintenant", "now"));
    lv_obj_set_width(lg2, 90);
    lv_obj_set_style_text_align(lg2, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(lg2, CARTE_L - 14 - 90, HISTO_BASE + 2);
}

// =====================================================================  MISES À JOUR (sur place)
static void maj_jauges()
{
    static const char *const JOURS[] = {TXT("dim.", "Sun"), TXT("lun.", "Mon"), TXT("mar.", "Tue"), TXT("mer.", "Wed"), TXT("jeu.", "Thu"), TXT("ven.", "Fri"), TXT("sam.", "Sat")};
    const uint32_t maintenant = (uint32_t)time(nullptr);
    if (!ia_compte.recu) // pire cas demandé : rien de l'agent du PC (capture 176)
    {
        ui_label_texte(o_nom, TXT("en attente de l'agent du PC", "waiting for the PC agent"));
        ui_label_texte(o_maj, "");
        for (int k = 0; k < 2; k++)
        {
            ui_label_texte(o_val[k], "");
            ui_label_texte(o_reset[k], "");
            barre_maj(o_barre[k], 0, UI_TEXTE);
        }
        return;
    }
    const ia_compte_t &a = ia_compte;
    ui_label_texte(o_nom, a.nom);
    const int32_t age = a.maj && maintenant > 1700000000u ? (int32_t)(maintenant - a.maj) : 0;
    char t[48], d[24];
    if (age < 600) // lue il y a moins de 10 min : rien à dire (règle de l'onglet IA du 31/10)
        ui_label_texte(o_maj, "");
    else
    {
        dans(d, sizeof d, age);
        ui_label_texte_fmt(o_maj, TXT("lu il y a %s", "read %s ago"), d + 5); // « 3 h 05 » (sans « dans »)
    }
    const int pct[] = {a.h5, a.semaine};
    const uint32_t reset[] = {a.h5_reset, a.semaine_reset};
    for (int k = 0; k < 2; k++)
    {
        ui_label_texte_fmt(o_val[k], "%d%%", pct[k]);
        ui_style_text_color(o_val[k], ui_c(charge_couleur(pct[k])), 0);
        barre_maj(o_barre[k], pct[k], pct[k] >= 90 ? UI_ROUGE : k ? UI_FROID : UI_ACCENT);
        if (!reset[k] || reset[k] <= maintenant)
        {
            ui_label_texte(o_reset[k], k ? TXT("réinitialisation inconnue", "unknown reset") : TXT("pas de fenêtre en cours", "no current session"));
            continue;
        }
        struct tm lt;
        const time_t tt = reset[k];
        localtime_r(&tt, &lt);
        if (k == 0 || reset[k] - maintenant < 86400)
            snprintf(t, sizeof t, TXT("%02dh%02d", "%02d:%02d"), lt.tm_hour, lt.tm_min);
        else
            snprintf(t, sizeof t, TXT("%s %02d/%02d %02dh%02d", "%s %02d/%02d %02d:%02d"), JOURS[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1, lt.tm_hour, lt.tm_min);
        dans(d, sizeof d, (int32_t)(reset[k] - maintenant));
        ui_label_texte_fmt(o_reset[k], TXT("réinitialisation %s · %s", "reset %s · %s"), t, d);
    }
}

// l'état de l'agent, sa pose et ses chiffres ; « au repos » si le message est absent ou vieux de plus de 2 min
static void maj_activite()
{
    const ia_activite_t &a = ia_activite;
    const uint32_t maintenant = (uint32_t)time(nullptr);
    const bool frais = a.recu && (maintenant < a.maj + 2 || (int32_t)(maintenant - a.maj) <= 120);
    const char *txt = TXT("au repos", "idle");
    uint32_t col = UI_TEXTE2;
    int pose = 0; // 0 repos (yeux fermés + « z »), 1 réfléchit (pensée), 2 attente (« ! »)
    if (frais)
    {
        if (!strcmp(a.alerte, "autorisation"))
            txt = TXT("autorisation demandée", "authorization requested"), col = UI_ROUGE, pose = 2;
        else if (!strcmp(a.alerte, "question"))
            txt = TXT("question pour toi", "question for you"), col = UI_ROUGE, pose = 2;
        else if (!strcmp(a.etat, "reflechit"))
            txt = TXT("réfléchit…", "thinking…"), col = UI_IA_ORANGE, pose = 1;
        else if (!strcmp(a.etat, "attente"))
            txt = TXT("en attente…", "waiting…"), col = UI_IA_ORANGE, pose = 2;
    }
    ui_label_texte(o_etat, txt);
    ui_style_text_color(o_etat, ui_c(col), 0);
    ui_img_src(o_clawd, pose == 0 ? &clawd_ia_repos : &clawd_ia);
    ui_cacher(o_pensee, pose != 1);
    ui_cacher(o_excl, pose != 2);
    ui_cacher(o_z, pose != 0);

    char b[16];
    milliers(b, sizeof b, (a.tokens + 500) / 1000);
    ui_label_texte_fmt(o_tokens, TXT("%s k tokens produits", "%s k tokens produced"), b);
    ui_label_texte_fmt(o_sessions, TXT("%d sessions · %d prompts", "%d sessions · %d prompts"), a.sessions, a.prompts);
    ui_label_texte_fmt(o_outils, TXT("%d outils", "%d tools"), a.outils);

    uint32_t max = 1;
    for (int i = 0; i < IA_HISTO; i++)
        if (a.histo[i] > max)
            max = a.histo[i];
    for (int i = 0; i < IA_HISTO; i++)
    {
        const lv_coord_t hh = (lv_coord_t)((uint64_t)a.histo[i] * HISTO_H / max);
        taille(o_histo[i], 9, hh);
        if (lv_obj_get_style_y(o_histo[i], LV_PART_MAIN) != HISTO_BASE - hh)
            lv_obj_set_y(o_histo[i], HISTO_BASE - hh);
    }
}

static void maj_valeurs()
{
    // ciel du jour ou de la nuit (phase publiée par HA, maison/etat) : un échange de colonne +
    // affichage/masquage des décors de la phase, tout gardé (ui_img_src / ui_cacher ne touchent rien si identique)
    ciel_maj(); // 12/11 : ciel animé si maison/ciel est reçu ; sinon l'ancien ciel jour/nuit
    maj_jauges();
    maj_activite();
}

static void tic_cb(lv_timer_t *)
{
    if (!g_zone)
        return;
    maj_valeurs(); // compte à rebours et péremption : sur place, seulement ce qui change
    ciel_deriver();
}

void page_ia_maj()
{
    if (g_zone)
        maj_valeurs();
}

void page_ia_creer(lv_obj_t *zone)
{
    g_zone = zone;
    construire();
    maj_valeurs();
    g_tic = lv_timer_create(tic_cb, 1000, nullptr);
}

void page_ia_quitter()
{
    if (g_tic)
        lv_timer_del(g_tic);
    g_tic = nullptr;
    if (g_precip_tic) // 12/11 : minuteur de la pluie
        lv_timer_del(g_precip_tic);
    g_precip_tic = nullptr;
    g_zone = nullptr;
}
