#include "page_thermostat.h"
#include "ui_theme.h"
#include "ui_courbe.h" // 05/11/2026 portage 9.6 : uc_cible_t, uc_carre, uc_arc, uc_sur, uc_clip, uc_rejoindre
#include "icones_28.h"
#include "icones_67.h"
#ifdef ARDUINO
#include <esp_heap_caps.h> // 07/11 (ESSAI lueur) : lecture de la PSRAM libre pour [lueur-diag]
#endif
#include "ui_diag.h" // 07/11 (ESSAI lueur) : ui_trace, pour dire une seule fois si l'allocation de la lueur échoue
#include <math.h>
#include <stdio.h>
#include <string.h> // memcpy (9.6 ne le fait plus passer par lvgl.h)

#define T_MIN 7.0f
#define T_MAX 24.0f
#define ANNEAU_D 320 // diamètre de l'anneau
#define ANNEAU_L 46  // épaisseur de l'anneau (vers l'intérieur ; diamètre extérieur inchangé) ; 27/10 : 51 -> 46 (retour d'usage)
#define ARC_L 35     // bande de l'arc LVGL (bouton à faire glisser), centrée dans l'anneau
#define FIN_ANNEAU 0x0A0E14 // segment du bas : un cran (RGB565) plus foncé que le fond 0x0F1216
#define CADRE_EP 5           // demi-cercle blanc + traits vers la gauche (design Lovyan) ; 27/10 : 4 -> 5 (retour d'usage)
#define CADRE_JEU 6          // écart entre l'anneau et le demi-cercle
#define DECALAGE_X 15        // anneau + demi-cercle décalés vers la droite
#define ROUGE_CHAUD 0xFF5B4B

th_piece_t th_pieces[TH_NB_PIECES] = {
    {"Cuisine", 0, 15, TH_ARRET, false, 0, 100, 255},
    {"Séjour", 0, 15, TH_ARRET, false, 0, 100, 255},
    {"Chambre 1", 0, 15, TH_ARRET, false, 0, 100, 255},
    {"Chambre 2", 0, 15, TH_ARRET, false, 0, 100, 255},
    {"Salle de bain", 0, 20, TH_ARRET, false, 0, 100, 255},
};
int th_piece_affichee = 0;
float th_ext_temp = 0;
int th_ext_hum = 0;
void (*th_commande)(int piece, th_mode_t mode, float consigne) = nullptr;

static const char *NOMS_MODE[] = {"Arrêt", "Hors-gel", "Éco", "Confort", "Manuel"};
static const float CONSIGNE_MODE[] = {0, 8, 16, 20, 0};

// Pointeurs valides seulement quand la page est affichée (remis à nullptr par page_thermostat_quitter)
static lv_obj_t *o_ext_temp, *o_ext_hum, *o_nom, *o_points[TH_NB_PIECES];
static lv_obj_t *o_anneau, *o_arc, *o_valeur, *o_consigne, *o_modes[4]; // 07/11 (ESSAI) : icône du radiateur retirée, la lueur la remplace
static bool g_reglage = false; // doigt sur l'anneau : on affiche la consigne
// 31/10 (retour d'usage) : au toucher d'un préréglage, la consigne glisse sur l'anneau de l'ancienne valeur à la nouvelle
#define ANIM_MS 400
static float g_cons_anim = NAN; // consigne dessinée pendant la transition (NAN : pas de transition)
// 07/11 (retour d'usage) : en quittant l'arrêt, l'anneau « arrive » : un front part du bout froid et monte jusqu'à la consigne
// (jusqu'à la pièce si elle est plus chaude) ; derrière lui l'anneau a son allure de marche, devant lui celle de l'arrêt.
// En allant à l'arrêt, le front redescend jusqu'au bout froid. Même durée et même courbe que les autres passages.
static float g_front = NAN;       // position du front sur l'anneau (0..1) ; NAN : pas de balayage
static bool g_vers_arret = false; // balayage descendant : la pièce est déjà à l'arrêt, l'anneau se dessine encore en marche

static void fmt1(char *buf, size_t n, float v)
{
    snprintf(buf, n, "%.1f", v);
    for (char *p = buf; *p; p++)
        if (*p == '.')
            *p = ',';
}

static th_mode_t mode_de_consigne(float c)
{
    // même règle que HA : une consigne égale à un préréglage sélectionne ce préréglage
    for (int m = TH_HORSGEL; m <= TH_CONFORT; m++)
        if (c == CONSIGNE_MODE[m])
            return (th_mode_t)m;
    return TH_MANUEL;
}

// --- Anneau en dégradé (froid -> accent -> chaud) ---
// 31/10 (d'après la carte thermostat de HA) : le dégradé sur trois niveaux de lumière : atténué jusqu'à la
// consigne, très sombre au-delà, et pleine lumière aux bouts arrondis entre la pièce et la consigne (dans les deux
// sens : consigne au-dessus ou au-dessous de la pièce) ; point de la pièce sur l'anneau ; tout sombre à l'arrêt
static lv_color_t degrade(float t)
{
    if (t < 0.5f)
        return lv_color_mix(ui_c(UI_ACCENT), ui_c(UI_FROID), (uint8_t)(t * 2 * 255));
    return lv_color_mix(ui_c(ROUGE_CHAUD), ui_c(UI_ACCENT), (uint8_t)((t - 0.5f) * 2 * 255));
}

static float position(float temp) { return LV_CLAMP(0.0f, (temp - T_MIN) / (T_MAX - T_MIN), 1.0f); }

static float consigne_dessinee(const th_piece_t &p) { return isnan(g_cons_anim) ? p.consigne : g_cons_anim; }

#define MIX_ATTENUE 96 // ~38 % de la couleur (à 22 %, trop peu de niveaux en RGB565 : paliers)
#define MIX_SOMBRE 48

static void disque(uc_cible_t *ctx, lv_point_t c, lv_coord_t d, lv_color_t coul, lv_coord_t bord = 0, lv_color_t coul_bord = {})
{
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = LV_RADIUS_CIRCLE;
    r.bg_color = coul;
    r.border_width = bord;
    r.border_color = coul_bord;
    lv_area_t z = {(lv_coord_t)(c.x - d / 2), (lv_coord_t)(c.y - d / 2), (lv_coord_t)(c.x - d / 2 + d - 1), (lv_coord_t)(c.y - d / 2 + d - 1)};
    uc_carre(ctx, r, &z);
}

// 04/11/2026 : l'anneau décrit élément par élément (135 segments de 2°, segment du bas, 20 fondus, 2 bouts arrondis,
// point de la pièce), avec le rectangle que chacun peut toucher. Mesuré sur l'écran (cuisine) : l'anneau entier en arcs
// coûte ~46 ms ; il était redessiné EN ENTIER à chaque pas de 0,5° du glissé et à chaque image de la transition. Le
// dessin et rafraichir() lisent la même description : rafraichir() ne fait repeindre que les rectangles des éléments
// qui changent, et le dessin saute les éléments hors de la zone à repeindre. Mêmes appels de dessin, dans le même
// ordre : pixels identiques (vérifié au simulateur contre les captures d'avant).
#define NB_SEG 135 // 135 segments de 2° = 270° (5° faisait des marches visibles)
#define E_BAS NB_SEG            // segment uni du bas
#define E_FONDU (NB_SEG + 1)    // 20 fondus : rouge i, bleu i, rouge i+1…
#define E_BOUT_PIECE (E_FONDU + 20)
#define E_BOUT_CONS (E_BOUT_PIECE + 1)
#define E_POINT (E_BOUT_CONS + 1)
#define E_LUEUR (E_POINT + 1) // 07/11 (ESSAI) : lueur rouge à l'intérieur de l'anneau pendant la chauffe
#define NB_ELEM (E_LUEUR + 1)
#define LUEUR_R (ANNEAU_D / 2 - ANNEAU_L + 2) // rayon : juste sous le bord intérieur de l'anneau
#define LUEUR_D (2 * LUEUR_R)                 // 232 px de côté (ANNEAU_D 320, ANNEAU_L 46)
#define LUEUR_CENTRE 0.60f                    // part du rouge au centre (vif au centre, éteint vers l'anneau)

// 07/11 (ESSAI) : la lueur précalculée UNE fois en image. Couleur déjà mélangée au fond de la page, TRAMÉE
// (Bayer 4x4) pour que les ~20 niveaux du rouge en RGB565 ne fassent pas de marches visibles (refusées au retour d'usage sans trame) ; opaque dans le disque, transparent hors du disque (les coins du carré passent sous
// l'anneau et son cadre). 232 x 232 px = 161 472 o, soit 2 o de couleur + 1 o de masque par pixel.
//   9.6 : RGB565A8 est PLANAIRE — tout le plan couleur (stride = largeur * 2) puis tout le masque A8
//         (lib/lvgl-9.6.0/src/draw/sw/lv_draw_sw_img.c, lv_bin_decoder.c ; leçon du crash de la page Lumières,
//         où l'écriture entrelacée 4 o/px a corrompu le tas). Header avec magic + stride, comme la vignette.
//   8.4 : LV_IMG_CF_TRUE_COLOR_ALPHA est entrelacé (3 o par pixel, LV_IMG_PX_SIZE_ALPHA_BYTE).
static uint8_t *g_lueur_px = nullptr;
static bool g_lueur_echec_signale = false;

static void lueur_canaux(lv_color_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
#if LVGL_VERSION_MAJOR >= 9
    const uint32_t v = lv_color_to_u32(c); // include/lvgl/draw/lv_color.h:350
    *r = (uint8_t)(v >> 16);
    *g = (uint8_t)(v >> 8);
    *b = (uint8_t)v;
#else
    const lv_color32_t v = lv_color_to32(c);
    *r = v.ch.red;
    *g = v.ch.green;
    *b = v.ch.blue;
#endif
}

// Renvoie lv_image_dsc_t* en 9.6, lv_img_dsc_t* en 8.4, nullptr si l'allocation a échoué (dessin alors sauté)
static const void *lueur_image()
{
#if LVGL_VERSION_MAJOR >= 9
    static lv_image_dsc_t img;
#else
    static lv_img_dsc_t img;
#endif
    static bool pret = false;
    if (pret)
        return &img;
    const int total = LUEUR_D * LUEUR_D;
#ifdef ARDUINO
    // 07/11 (ESSAI lueur) : une fois par démarrage, la PSRAM libre AVANT et APRÈS l'allocation du tableau — même
    // idiomatique que [lum-diag] (page_lumieres.cpp:129). Le pointeur dit si le tampon est tombé en PSRAM (0x3C…)
    // ou en RAM interne (0x3F…).
    ui_trace("[lueur-diag] avant : PSRAM %u o ; interne %u o\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
#if LVGL_VERSION_MAJOR >= 9
    g_lueur_px = (uint8_t *)lv_malloc(total * 3);
#else
    g_lueur_px = (uint8_t *)lv_mem_alloc(total * LV_IMG_PX_SIZE_ALPHA_BYTE);
#endif
#ifdef ARDUINO
    ui_trace("[lueur-diag] %d o demandés : tampon %p ; PSRAM %u o ; interne %u o\n", total * 3, (void *)g_lueur_px,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
    if (!g_lueur_px)
    {
        if (!g_lueur_echec_signale)
        {
            g_lueur_echec_signale = true;
            ui_trace("[thermostat] allocation lueur ÉCHOUÉE (%d o) : pas de lueur pendant la chauffe\n", total * 3);
        }
        return nullptr;
    }
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    uint8_t rr, rg, rb, fr, fg, fb;
    lueur_canaux(ui_c(ROUGE_CHAUD), &rr, &rg, &rb);
    lueur_canaux(ui_c(UI_FOND), &fr, &fg, &fb);
    for (int y = 0; y < LUEUR_D; y++)
        for (int x = 0; x < LUEUR_D; x++)
        {
            const float dx = x + 0.5f - LUEUR_R, dy = y + 0.5f - LUEUR_R;
            const float r = sqrtf(dx * dx + dy * dy) / LUEUR_R;
            const int i = y * LUEUR_D + x;
            if (r >= 1.0f) // hors du disque : transparent
            {
#if LVGL_VERSION_MAJOR >= 9
                * (uint16_t *)(g_lueur_px + 2 * i) = 0;
                g_lueur_px[2 * total + i] = LV_OPA_TRANSP;
#else
                memset(g_lueur_px + i * LV_IMG_PX_SIZE_ALPHA_BYTE, 0, LV_IMG_PX_SIZE_ALPHA_BYTE);
#endif
                continue;
            }
            const float k = LUEUR_CENTRE * (1 - r) * (1 - r);     // intensité : douce vers le bord
            const float trame = (bayer[y & 3][x & 3] + 0.5f) / 16.0f; // seuil de trame, au pas du RGB565
            auto canal = [&](uint8_t chaud, uint8_t fond, int pas) // mélange puis arrondi tramé
            {
                const float v = fond + (chaud - fond) * k;
                return (uint8_t)LV_MIN(255, (int)(v + trame * pas));
            };
            const lv_color_t c = lv_color_make(canal(rr, fr, 8), canal(rg, fg, 4), canal(rb, fb, 8));
#if LVGL_VERSION_MAJOR >= 9
            const uint16_t v = lv_color_to_u16(c); // RGB565 natif, dans l'ordre d'octets de la cible
            memcpy(g_lueur_px + 2 * i, &v, sizeof v);
            g_lueur_px[2 * total + i] = LV_OPA_COVER;
#else
            uint8_t *d = g_lueur_px + i * LV_IMG_PX_SIZE_ALPHA_BYTE;
            memcpy(d, &c, sizeof c);
            d[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = LV_OPA_COVER;
#endif
        }
#if LVGL_VERSION_MAJOR >= 9
    memset(&img, 0, sizeof img);
    img.header.magic = LV_IMAGE_HEADER_MAGIC;
    img.header.cf = LV_COLOR_FORMAT_RGB565A8;
    img.header.w = LUEUR_D;
    img.header.h = LUEUR_D;
    img.header.stride = LUEUR_D * 2; // planaire : stride du plan couleur
    img.data_size = total * 3;
    img.data = g_lueur_px;
#else
    img.header.always_zero = 0;
    img.header.w = LUEUR_D;
    img.header.h = LUEUR_D;
    img.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    img.data_size = total * LV_IMG_PX_SIZE_ALPHA_BYTE;
    img.data = g_lueur_px;
#endif
    pret = true;
    return &img;
}
#define R_POINT (ANNEAU_D / 2 - ANNEAU_L / 2.0f) // rayon du milieu de l'épaisseur (bouts, point de la pièce)

struct elem_t
{
    lv_color_t c;
    bool vis;
    float t; // disques : position sur l'anneau (0..1)
};

// angles (degrés, non ramenés à 0-360) de l'arc de l'élément i
static void angles_elem(int i, int *a0, int *a1)
{
    if (i < NB_SEG)
        *a0 = 135 + i * 2, *a1 = *a0 + (i == NB_SEG - 1 ? 2 : 3); // 1° de recouvrement contre les fentes
    else if (i == E_BAS)
        *a0 = 46, *a1 = 134; // sans déborder sur le dégradé (sinon un trait sombre à 44-45°)
    else
    {
        const int k = (i - E_FONDU) / 2;
        if ((i - E_FONDU) % 2 == 0)
            *a0 = 45 + k, *a1 = 45 + k + 2;
        else
            *a0 = 134 - k, *a1 = 134 - k + 2;
    }
}

static void decrire(const th_piece_t &p, elem_t *e)
{
    const bool balayage = !isnan(g_front);
    const bool arret = p.mode == TH_ARRET && !g_vers_arret;
    const float t_piece = position(p.temp), t_cons = position(consigne_dessinee(p));
    const float bas = LV_MIN(t_piece, t_cons), haut = LV_MAX(t_piece, t_cons); // la partie en pleine lumière
    const bool ecart = !arret && bas < haut;
    auto couleur = [&](float t)
    {
        const lv_color_t c = degrade(t);
        if (ecart && t >= bas && t <= haut)
            return c;
        if (arret || t > t_cons)
            return lv_color_mix(c, ui_c(UI_FOND), MIX_SOMBRE);
        return lv_color_mix(c, ui_c(UI_FOND), MIX_ATTENUE);
    };
    auto couleur_f = [&](float t) // 07/11 : devant le front du balayage, l'allure de l'arrêt
    {
        if (balayage && t > g_front)
            return lv_color_mix(degrade(t), ui_c(UI_FOND), MIX_SOMBRE);
        return couleur(t);
    };
    for (int k = 0; k < NB_SEG; k++)
        e[k] = {couleur_f((k + 0.5f) / NB_SEG), true, 0};
    // L'ouverture du bas (45° -> 135°) est fermée par un segment uni : anneau complet sur 360°,
    // avec un dégradé de 10° à chaque jonction (rouge -> segment à 45°, segment -> bleu à 135°)
    e[E_BAS] = {ui_c(FIN_ANNEAU), true, 0};
    const lv_color_t fin_rouge = couleur_f(1), fin_bleu = couleur_f(0);
    for (int i = 0; i < 10; i++) // 10 pas de 1°
    {
        const uint8_t part = (uint8_t)(255 * (i + 0.5f) / 10); // part du segment uni
        e[E_FONDU + 2 * i] = {lv_color_mix(ui_c(FIN_ANNEAU), fin_rouge, part), true, 0};
        e[E_FONDU + 2 * i + 1] = {lv_color_mix(ui_c(FIN_ANNEAU), fin_bleu, part), true, 0};
    }
    // bouts arrondis de la partie en pleine lumière (côté consigne : autour du bouton), point de la pièce
    e[E_BOUT_PIECE] = {degrade(t_piece), ecart, t_piece};
    e[E_BOUT_CONS] = {degrade(t_cons), ecart, t_cons};
    if (balayage) // 07/11 : la bande en pleine lumière s'arrête au front, son bout arrondi du haut le suit
    {
        const bool piece_en_bas = t_piece <= t_cons;
        elem_t &eb = piece_en_bas ? e[E_BOUT_PIECE] : e[E_BOUT_CONS];
        elem_t &eh = piece_en_bas ? e[E_BOUT_CONS] : e[E_BOUT_PIECE];
        eb.vis = eh.vis = ecart && g_front > bas;
        eh.t = LV_MIN(g_front, haut);
        eh.c = degrade(eh.t);
    }
    e[E_POINT] = {ui_c(UI_FOND), true, t_piece}; // 31/10 : 14 -> 22 px
    e[E_LUEUR] = {ui_c(ROUGE_CHAUD),
#ifdef LUEUR_ESSAI_FORCE
                  true, // 07/11 build de MESURE uniquement (drapeau jamais mis dans platformio.ini) : la lueur
                        // s'affiche même sans chauffe, pour mesurer PSRAM et le coût d'une repeinte sans forcer HA
#else
                  p.chauffe && !arret,
#endif
                  0}; // 07/11 (ESSAI) : remplace le radiateur orange
}

static float angle_rad(float t) { return (135 + 270 * t) * (float)M_PI / 180; }

// rectangle (relatif au centre) que l'élément peut toucher, avec 2 px de marge (lissage des bords)
static lv_area_t zone_elem(int i, const elem_t &e)
{
    if (i == E_LUEUR) // 07/11 (ESSAI) : le disque intérieur (la lueur longe le bord intérieur de l'anneau)
        return {(lv_coord_t)-LUEUR_R, (lv_coord_t)-LUEUR_R, (lv_coord_t)(LUEUR_R - 1), (lv_coord_t)(LUEUR_R - 1)};
    if (i >= E_BOUT_PIECE)
    {
        const lv_coord_t d = i == E_POINT ? 22 : ANNEAU_L;
        const float a = angle_rad(e.t);
        const lv_coord_t x = (lv_coord_t)lroundf(R_POINT * cosf(a)), y = (lv_coord_t)lroundf(R_POINT * sinf(a));
        return {(lv_coord_t)(x - d / 2 - 2), (lv_coord_t)(y - d / 2 - 2), (lv_coord_t)(x - d / 2 + d + 1), (lv_coord_t)(y - d / 2 + d + 1)};
    }
    static lv_area_t zones[E_BOUT_PIECE];
    static bool pret = false;
    if (!pret)
    {
        for (int k = 0; k < E_BOUT_PIECE; k++)
        {
            int a0, a1;
            angles_elem(k, &a0, &a1);
            float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
            for (float a = a0; a <= a1 + 0.001f; a += 0.25f)
                for (int j = 0; j < 2; j++)
                {
                    const float r = j ? (float)(ANNEAU_D / 2 - ANNEAU_L) : (float)(ANNEAU_D / 2);
                    const float x = r * cosf(a * (float)M_PI / 180), y = r * sinf(a * (float)M_PI / 180);
                    x1 = LV_MIN(x1, x), y1 = LV_MIN(y1, y), x2 = LV_MAX(x2, x), y2 = LV_MAX(y2, y);
                }
            zones[k] = {(lv_coord_t)(floorf(x1) - 2), (lv_coord_t)(floorf(y1) - 2), (lv_coord_t)(ceilf(x2) + 2), (lv_coord_t)(ceilf(y2) + 2)};
        }
        pret = true;
    }
    return zones[i];
}

static lv_point_t centre_de(lv_obj_t *obj)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return {(lv_coord_t)(a.x1 + lv_area_get_width(&a) / 2), (lv_coord_t)(a.y1 + lv_area_get_height(&a) / 2)};
}

static lv_area_t decaler(lv_area_t z, lv_point_t c) { return {(lv_coord_t)(z.x1 + c.x), (lv_coord_t)(z.y1 + c.y), (lv_coord_t)(z.x2 + c.x), (lv_coord_t)(z.y2 + c.y)}; }

static void anneau_dessin_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e); // 9.6 : la layer du draw task (avant : lv_event_get_draw_ctx)
    const lv_point_t centre = centre_de(uc_objet_du(e));
    elem_t el[NB_ELEM];
    decrire(th_pieces[th_piece_affichee], el);
    auto a_dessiner = [&](int i)
    {
        if (!el[i].vis)
            return false;
        const lv_area_t z = decaler(zone_elem(i, el[i]), centre);
        return uc_sur(&z, uc_clip(ctx)); // rognage : 8.4 ctx->clip_area, 9.6 layer->_clip_area
    };

    if (a_dessiner(E_LUEUR)) // 07/11 (ESSAI) : lueur de chauffe, vive au centre, éteinte vers l'anneau — dessinée SOUS les segments
    {
        const void *img = lueur_image();
        if (img)
        {
            const lv_area_t z = decaler(zone_elem(E_LUEUR, el[E_LUEUR]), centre);
            uc_image(ctx, img, &z); // 9.6 : lv_draw_image(layer, dsc, aire) avec dsc.src ; 8.4 : lv_draw_img(ctx, dsc, aire, img)
        }
    }

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.width = ANNEAU_L;
    dsc.rounded = 0;
    for (int i = 0; i < E_BOUT_PIECE; i++)
        if (a_dessiner(i))
        {
            int a0, a1;
            angles_elem(i, &a0, &a1);
            dsc.color = el[i].c;
            uc_arc(ctx, dsc, &centre, ANNEAU_D / 2, a0 % 360, a1 % 360);
        }

    // point sur l'anneau (milieu de son épaisseur) à la position t
    auto sur_anneau = [&](float t)
    {
        const float ang = angle_rad(t);
        return lv_point_t{(lv_coord_t)lroundf(centre.x + R_POINT * cosf(ang)), (lv_coord_t)lroundf(centre.y + R_POINT * sinf(ang))};
    };
    if (a_dessiner(E_BOUT_PIECE))
        disque(ctx, sur_anneau(el[E_BOUT_PIECE].t), ANNEAU_L, el[E_BOUT_PIECE].c);
    if (a_dessiner(E_BOUT_CONS))
        disque(ctx, sur_anneau(el[E_BOUT_CONS].t), ANNEAU_L, el[E_BOUT_CONS].c);
    if (a_dessiner(E_POINT))
        disque(ctx, sur_anneau(el[E_POINT].t), 22, el[E_POINT].c, 4, ui_c(UI_TEXTE)); // la pièce
}

// L'anneau tel qu'il est à l'écran (rafraichir le compare à la nouvelle description) ; oublié à la création de la page
static elem_t g_affiche[NB_ELEM];
static bool g_affiche_connu = false;

// 9.6 : lv_color_t n'a plus de champ « full » (draw/lv_color.h:107-111 : blue/green/red) — comparaison champ par champ
static bool couleur_egale(lv_color_t a, lv_color_t b)
{
#if LVGL_VERSION_MAJOR >= 9
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
#else
    return a.full == b.full;
#endif
}

static bool elem_change(const elem_t &a, const elem_t &b) { return a.vis != b.vis || !couleur_egale(a.c, b.c) || (a.vis && a.t != b.t); }

// ne fait repeindre que les rectangles des éléments qui changent (segments voisins regroupés en une bande)
static void anneau_invalider(const th_piece_t &p)
{
    elem_t n[NB_ELEM];
    decrire(p, n);
    if (!g_affiche_connu)
    {
        memcpy(g_affiche, n, sizeof n);
        g_affiche_connu = true;
        lv_obj_invalidate(o_anneau);
        return;
    }
    const lv_point_t c = centre_de(o_anneau);
    lv_area_t zones[NB_ELEM];
    int nz = 0;
    bool en_cours = false; // une bande de segments contigus en cours
    for (int i = 0; i < NB_ELEM; i++)
    {
        if (!elem_change(g_affiche[i], n[i]))
        {
            en_cours = false;
            continue;
        }
        if (i >= E_BOUT_PIECE) // disque : l'ancienne place ET la nouvelle
        {
            if (g_affiche[i].vis)
                zones[nz++] = zone_elem(i, g_affiche[i]);
            if (n[i].vis)
                zones[nz++] = zone_elem(i, n[i]);
            en_cours = false;
            continue;
        }
        const lv_area_t z = zone_elem(i, n[i]);
        if (en_cours && i < NB_SEG)
            uc_rejoindre(&zones[nz - 1], &zones[nz - 1], &z);
        else
            zones[nz++] = z;
        en_cours = i < NB_SEG;
    }
    memcpy(g_affiche, n, sizeof n);
    if (nz > 8) // trop de rectangles (la liste de LVGL est bornée) : un seul, qui les contient tous
    {
        for (int k = 1; k < nz; k++)
            uc_rejoindre(&zones[0], &zones[0], &zones[k]);
        nz = 1;
    }
    for (int k = 0; k < nz; k++)
    {
        const lv_area_t z = decaler(zones[k], c);
        lv_obj_invalidate_area(o_anneau, &z);
    }
}

// --- Cadre du design Lovyan : demi-cercle blanc à droite de l'anneau + deux traits jusqu'au bord gauche ---
static void cadre_dessin_cb(lv_event_t *e)
{
    uc_cible_t *ctx = uc_cible_du(e);
    const lv_point_t *c = (const lv_point_t *)lv_event_get_user_data(e);
    lv_area_t zone;
    lv_obj_get_coords(uc_objet_du(e), &zone);
    lv_point_t centre = {(lv_coord_t)(zone.x1 + c->x), (lv_coord_t)(zone.y1 + c->y)};
    const lv_coord_t r = ANNEAU_D / 2 + CADRE_JEU + CADRE_EP; // rayon extérieur du demi-cercle

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = ui_c(UI_TEXTE);
    arc.width = CADRE_EP;
    uc_arc(ctx, arc, &centre, r, 270, 90); // moitié droite (0° = à droite, sens horaire)

    lv_draw_rect_dsc_t trait;
    lv_draw_rect_dsc_init(&trait);
    trait.bg_color = ui_c(UI_TEXTE);
    lv_area_t haut = {zone.x1, (lv_coord_t)(centre.y - r), centre.x, (lv_coord_t)(centre.y - r + CADRE_EP - 1)};
    lv_area_t bas = {zone.x1, (lv_coord_t)(centre.y + r - CADRE_EP), centre.x, (lv_coord_t)(centre.y + r - 1)}; // remonté d'1 px
    uc_carre(ctx, trait, &haut);
    uc_carre(ctx, trait, &bas);
}

// --- Rafraîchissement ---
static void rafraichir()
{
    if (!o_nom)
        return;
    const th_piece_t &p = th_pieces[th_piece_affichee];
    char v[12];

    ui_label_texte_fmt(o_ext_temp, "%.0f°C", th_ext_temp);
    ui_label_texte_fmt(o_ext_hum, "%d %%", th_ext_hum);

    ui_label_texte(o_nom, p.nom);
    for (int i = 0; i < TH_NB_PIECES; i++)
        ui_style_bg_color(o_points[i], ui_c(i == th_piece_affichee ? UI_TEXTE : UI_ETEINT), 0);

    bool arret = p.mode == TH_ARRET;
    if (arret && !g_vers_arret) // 07/11 : pendant le balayage vers l'arrêt, le bouton reste visible et descend
        lv_obj_add_state(o_arc, LV_STATE_DISABLED);
    else
        lv_obj_clear_state(o_arc, LV_STATE_DISABLED);
    if (!g_reglage)
    {
        float v_arc = consigne_dessinee(p);
        if (!isnan(g_front)) // 07/11 : le bouton suit le front, sans dépasser la consigne
            v_arc = T_MIN + LV_MIN(g_front, position(p.consigne)) * (T_MAX - T_MIN);
        lv_arc_set_value(o_arc, (int16_t)lroundf(v_arc * 10));
    }
    // 26/10 : HA publie les 5 thermostats dès qu'une valeur change dans une pièce : l'anneau (330 x 330 px, 240 ms à
    // 80 MHz sur batterie) n'est redessiné que si son dessin change ; 04/11 : et seulement là où il change
    anneau_invalider(p);

    // Le moins de texte possible : la valeur (ambiante, ou consigne en orange pendant le réglage) + le radiateur
    fmt1(v, sizeof v, g_reglage ? p.consigne : p.temp);
    ui_label_texte(o_valeur, v);
    ui_style_text_color(o_valeur, ui_c(g_reglage ? UI_ACCENT : UI_TEXTE), 0);
    // Consigne en haut à gauche (symétrique du radiateur, comme le design Lovyan) : mise à jour au relâchement
    if (!g_reglage)
    {
        fmt1(v, sizeof v, p.consigne);
        if (arret)
            ui_label_texte(o_consigne, "--"); // 07/11 (ESSAI) : « -- » remplace « OFF » (plus court, plus lisible ; « ARRÊT » toucherait l'anneau)
        else
            ui_label_texte_fmt(o_consigne, "%s°", v);
    }

    for (int m = 0; m < 4; m++)
        if (p.mode == m)
            lv_obj_add_state(o_modes[m], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_modes[m], LV_STATE_CHECKED);
}

void page_thermostat_maj() { rafraichir(); }

// --- Événements ---
static void transition_pas(void *, int32_t v)
{
    g_cons_anim = v / 10.0f;
    rafraichir(); // bouton et anneau suivent ; le reste ne change pas
}
static void transition_fin(lv_anim_t *)
{
    g_cons_anim = NAN;
    rafraichir();
}
static void transition_arreter()
{
    lv_anim_del(&g_cons_anim, nullptr);
    g_cons_anim = NAN;
    lv_anim_del(&g_front, nullptr); // 07/11 : balayage depuis / vers l'arrêt
    g_front = NAN;
    g_vers_arret = false;
}

// 07/11 : balayage du front de `de` à `a` (positions 0..1 sur l'anneau)
static void balayage_pas(void *, int32_t v)
{
    g_front = v / 1000.0f;
    rafraichir();
}
static void balayage_fin(lv_anim_t *)
{
    g_front = NAN;
    g_vers_arret = false;
    rafraichir();
}
static void balayer(float de, float a, bool vers_arret)
{
    g_front = de;
    g_vers_arret = vers_arret;
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, &g_front);
    lv_anim_set_values(&an, (int32_t)lroundf(de * 1000), (int32_t)lroundf(a * 1000));
    lv_anim_set_time(&an, ANIM_MS);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&an, balayage_pas);
    lv_anim_set_ready_cb(&an, balayage_fin);
    lv_anim_start(&an);
}

static void piece_cb(lv_event_t *e)
{
    transition_arreter();
    int sens = (int)(intptr_t)lv_event_get_user_data(e);
    th_piece_affichee = (th_piece_affichee + TH_NB_PIECES + sens) % TH_NB_PIECES;
    rafraichir();
}

static void mode_cb(lv_event_t *e)
{
    th_mode_t m = (th_mode_t)(intptr_t)lv_event_get_user_data(e);
    th_piece_t &p = th_pieces[th_piece_affichee];
    const float avant = consigne_dessinee(p);
    const th_mode_t mode_avant = p.mode;
    transition_arreter();
    p.mode = m;
    if (m != TH_ARRET)
        p.consigne = CONSIGNE_MODE[m];
    if (mode_avant == TH_ARRET && m != TH_ARRET) // 07/11 : l'anneau arrive du bout froid
        balayer(0, LV_MAX(position(p.consigne), position(p.temp)), false);
    else if (mode_avant != TH_ARRET && m == TH_ARRET) // 07/11 : et repart vers le bout froid
        balayer(LV_MAX(position(avant), position(p.temp)), 0, true);
    else if (m != TH_ARRET && avant != p.consigne)
    {
        g_cons_anim = avant;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, &g_cons_anim);
        lv_anim_set_values(&a, (int32_t)lroundf(avant * 10), (int32_t)lroundf(p.consigne * 10));
        lv_anim_set_time(&a, ANIM_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, transition_pas);
        lv_anim_set_ready_cb(&a, transition_fin);
        lv_anim_start(&a);
    }
    rafraichir();
    if (th_commande)
        th_commande(th_piece_affichee, p.mode, p.consigne);
}

static void arc_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    th_piece_t &p = th_pieces[th_piece_affichee];
    if (code == LV_EVENT_PRESSED)
    {
        transition_arreter();
        g_reglage = true;
    }
    else if (code == LV_EVENT_VALUE_CHANGED)
    {
        int v = (lv_arc_get_value(o_arc) + 2) / 5 * 5; // pas de 0,5 °C
        p.consigne = v / 10.0f;
        p.mode = mode_de_consigne(p.consigne);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        g_reglage = false;
        if (th_commande)
            th_commande(th_piece_affichee, p.mode, p.consigne);
    }
    else
        return;
    rafraichir();
}

// --- Construction ---
void page_thermostat_creer(lv_obj_t *zone)
{
    // Pièce : < nom > + points (plus de bandeau extérieur en haut : toute la surface pour la pièce, 18/10)
    lv_obj_t *prec = ui_bouton(zone, 56, 44);
    ui_style_bg_opa(prec, LV_OPA_TRANSP, 0);
    ui_style_border_width(prec, 0, 0);
    lv_obj_set_pos(prec, 8, 0);
    lv_obj_center(ui_label(prec, UI_POLICE_CHEVRONS, UI_TEXTE2, LV_SYMBOL_LEFT)); // 27/10 : 30 -> 36 (retour d'usage)
    lv_obj_add_event_cb(prec, piece_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

    lv_obj_t *suiv = ui_bouton(zone, 56, 44);
    ui_style_bg_opa(suiv, LV_OPA_TRANSP, 0);
    ui_style_border_width(suiv, 0, 0);
    lv_obj_set_pos(suiv, UI_ZONE_L - 8 - 56, 0);
    lv_obj_center(ui_label(suiv, UI_POLICE_CHEVRONS, UI_TEXTE2, LV_SYMBOL_RIGHT));
    lv_obj_add_event_cb(suiv, piece_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    o_nom = ui_label(zone, UI_POLICE_NOM_PIECE, UI_TEXTE, ""); // essai 3x5 refusé (18/10) ; 27/10 : en gras (retour d'usage)
    lv_obj_align(o_nom, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t *points = ui_conteneur(zone);
    lv_obj_set_size(points, LV_SIZE_CONTENT, 9);
    lv_obj_set_flex_flow(points, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(points, 9, 0);
    lv_obj_align(points, LV_ALIGN_TOP_MID, 0, 40);
    for (int i = 0; i < TH_NB_PIECES; i++)
    {
        o_points[i] = ui_conteneur(points);
        lv_obj_set_size(o_points[i], 9, 9);
        lv_obj_set_style_radius(o_points[i], LV_RADIUS_CIRCLE, 0);
        ui_style_bg_opa(o_points[i], LV_OPA_COVER, 0);
    }

    // Anneau dessiné + arc transparent par-dessus pour le bouton à faire glisser
    const lv_coord_t ax = (UI_ZONE_L - ANNEAU_D) / 2 + DECALAGE_X, ay = 72; // sous l'en-tête (points à ≈49), trait du bas au-dessus des pastilles (414)
    static lv_point_t centre_anneau;
    centre_anneau = {(lv_coord_t)(ax + ANNEAU_D / 2), (lv_coord_t)(ay + ANNEAU_D / 2)};
    lv_obj_t *cadre = ui_conteneur(zone);
    lv_obj_set_size(cadre, UI_ZONE_L, UI_ZONE_H);
    lv_obj_clear_flag(cadre, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cadre, cadre_dessin_cb, LV_EVENT_DRAW_MAIN, &centre_anneau);
    lv_obj_move_background(cadre);

    // Radiateur (retour d'état du chauffage) dans l'angle bas gauche, hors de l'anneau
    // 07/11 (ESSAI) : le radiateur orange du bas à gauche est retiré — c'est la lueur rouge à l'intérieur
    // de l'anneau qui dit maintenant que ça chauffe (l'espace libéré reste vide, l'anneau et son cadre ne bougent pas)
    // ... et la consigne en symétrie haute (centres à égale distance du centre de l'anneau)
    o_consigne = ui_label(zone, UI_POLICE_TEMP_PETITE, UI_TEXTE, ""); // même police que le centre
    lv_obj_set_pos(o_consigne, 14, ay + 4);

    o_anneau = ui_conteneur(zone);
    lv_obj_set_size(o_anneau, ANNEAU_D, ANNEAU_D);
    lv_obj_set_pos(o_anneau, ax, ay);
    lv_obj_clear_flag(o_anneau, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_anneau, anneau_dessin_cb, LV_EVENT_DRAW_MAIN, NULL);
    g_affiche_connu = false; // 04/11 : objet neuf, dessiné en entier ; la comparaison repart de là

    lv_obj_t *centre = ui_conteneur(zone);
    lv_obj_set_size(centre, 190, 160);
    lv_obj_set_pos(centre, ax + (ANNEAU_D - 190) / 2, ay + (ANNEAU_D - 160) / 2);
    lv_obj_clear_flag(centre, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(centre, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(centre, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(centre, 8, 0);
    o_valeur = ui_label(centre, UI_POLICE_TEMP, UI_TEXTE, "");


    // Extérieur, posé sur le segment gris du bas de l'anneau (petites icônes 28 px)
    lv_obj_t *ext = ui_conteneur(zone);
    lv_obj_set_size(ext, LV_SIZE_CONTENT, 34);
    lv_obj_set_flex_flow(ext, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ext, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ext, 4, 0);
    lv_obj_clear_flag(ext, LV_OBJ_FLAG_CLICKABLE);
    ui_icone(ext, &icon_tempext_28, 0x7FD48F);
    o_ext_temp = ui_label(ext, UI_POLICE_TEXTE_22, UI_TEXTE2, "");
    lv_obj_set_style_pad_right(o_ext_temp, 14, 0);
    ui_icone(ext, &icon_humiext_28, UI_HUMIDITE);
    o_ext_hum = ui_label(ext, UI_POLICE_TEXTE_22, UI_TEXTE2, "");
    lv_obj_align(ext, LV_ALIGN_TOP_MID, DECALAGE_X, ay + ANNEAU_D / 2 + 105); // centré dans le gris

    o_arc = lv_arc_create(zone);
    // arc plus petit que l'anneau, pour que son bouton tombe au milieu de l'épaisseur
    lv_obj_set_size(o_arc, ANNEAU_D - (ANNEAU_L - ARC_L), ANNEAU_D - (ANNEAU_L - ARC_L));
    lv_obj_set_pos(o_arc, ax + (ANNEAU_L - ARC_L) / 2, ay + (ANNEAU_L - ARC_L) / 2);
    lv_arc_set_rotation(o_arc, 135);
    lv_arc_set_bg_angles(o_arc, 0, 270);
    lv_arc_set_range(o_arc, (int16_t)(T_MIN * 10), (int16_t)(T_MAX * 10));
    lv_obj_set_style_pad_all(o_arc, 0, 0);
    ui_style_bg_opa(o_arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_opa(o_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(o_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(o_arc, ARC_L, LV_PART_MAIN);
    lv_obj_set_style_arc_width(o_arc, ARC_L, LV_PART_INDICATOR);
    ui_style_bg_color(o_arc, ui_c(UI_TEXTE), LV_PART_KNOB);
    ui_style_bg_opa(o_arc, LV_OPA_COVER, LV_PART_KNOB);
    ui_style_border_color(o_arc, ui_c(UI_FOND), LV_PART_KNOB);
    ui_style_border_width(o_arc, 4, LV_PART_KNOB);
    lv_obj_set_style_pad_all(o_arc, 2, LV_PART_KNOB);
    ui_style_shadow_width(o_arc, 0, LV_PART_KNOB);
    ui_style_bg_opa(o_arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DISABLED); // pas de bouton à l'arrêt
    lv_obj_set_style_border_opa(o_arc, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DISABLED);
    lv_obj_add_event_cb(o_arc, arc_cb, LV_EVENT_ALL, NULL);

    // Modes : 4 pastilles
    lv_obj_t *rang = ui_conteneur(zone);
    lv_obj_set_size(rang, UI_ZONE_L - 2 * UI_MARGE, 56);
    lv_obj_set_pos(rang, UI_MARGE, 414);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *sous[] = {"", "8°", "16°", "20°"};
    for (int m = 0; m < 4; m++)
    {
        lv_obj_t *b = ui_bouton(rang, 88, 56);
        o_modes[m] = b;
        lv_obj_set_style_text_font(b, UI_POLICE_TEXTE_GRAS, 0); // 31/10 (retour d'usage) : en gras, contour 3 px
        ui_style_border_width(b, 3, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label_texte(lv_label_create(b), NOMS_MODE[m]);
        if (*sous[m])
        {
            lv_obj_t *s = lv_label_create(b);
            ui_label_texte(s, sous[m]);
        }
        lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)m);
    }

    rafraichir();
}

void page_thermostat_quitter()
{
    o_nom = nullptr; // rafraichir() ne touche plus à rien
    g_reglage = false;
    transition_arreter();
}

// 04/11/2026 : bancs de mesure (simulateur, écran) — centre de l'anneau à l'écran, et un pas de glissé au doigt tel
// que le fait arc_cb (bouton déplacé + consigne + rafraîchir), SANS envoyer de commande à HA (th_commande non appelé).
bool page_thermostat_centre_anneau(lv_point_t *c)
{
    if (!o_nom)
        return false;
    lv_area_t a;
    lv_obj_get_coords(o_anneau, &a);
    *c = {(lv_coord_t)(a.x1 + lv_area_get_width(&a) / 2), (lv_coord_t)(a.y1 + lv_area_get_height(&a) / 2)};
    return true;
}

void page_thermostat_banc_glisse(float consigne, bool fin)
{
    if (!o_nom)
        return;
    th_piece_t &p = th_pieces[th_piece_affichee];
    if (fin)
        g_reglage = false;
    else
    {
        transition_arreter();
        g_reglage = true;
        lv_arc_set_value(o_arc, (int16_t)lroundf(consigne * 10));
        p.consigne = consigne;
        p.mode = mode_de_consigne(consigne);
    }
    rafraichir();
}
