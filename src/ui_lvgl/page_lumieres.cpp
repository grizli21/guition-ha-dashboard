#include "page_lumieres.h"
#include "ui_theme.h"
#include "ui_courbe.h" // 05/11 (vague D) : wrappers uc_* — dessin de la jauge sous 9.6
#include "ui_diag.h"  // 08/11 : sortie du bloc #ifdef ARDUINO — la page trace aussi au simulateur (ligne 163 :
                      // « allocation disque ÉCHOUÉE »), et ui_diag.h est portable (ui_diag.cpp au simulateur)
#include <math.h>
#include <string.h>

#ifdef ARDUINO
// 06/11 diagnostic p10 (OPTI_JOURNAL « Diagnostic des redémarrages 9.6 ») : le parcours du tas interne
// meurt juste après page_lumieres_creer (trace page-diag v4, r6) et le tampon du disque est en PSRAM
// (0x3c… en r8). v6 : on vérifie aussi le tas PSRAM — si sa chaîne de blocs est déjà corrompue AVANT
// le malloc de 145 200 o, c'est le malloc lui-même qui, en suivant un pointeur faux, écrase les
// métadonnées du tas interne. Chaque vérification a sa trace : la dernière émise nomme l'appel tué.
#include <Arduino.h> // uxTaskGetStackHighWaterMark
#include <esp_heap_caps.h>
static void lum_diag(const char *etape)
{
    ui_diag_point(etape);
    ui_trace("[lum-diag] apres %s : integrite interne %d\n", etape,
             heap_caps_check_integrity_all(MALLOC_CAP_INTERNAL) ? 1 : 0);
    ui_trace("[lum-diag] apres %s : integrite PSRAM %d ; libre interne %u o ; libre PSRAM %u o ; pile %u mots\n",
             etape, heap_caps_check_integrity_all(MALLOC_CAP_SPIRAM) ? 1 : 0,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
#else
static void lum_diag(const char *) {}
#endif

lu_lampe_t lu_lampes[LU_NB] = {
    {TXT("Lampadaire 1", "Floor Lamp 1"), "Séjour", true, false, 60, false, 2700, 30, 100, false},
    {TXT("Lampadaire 2", "Floor Lamp 2"), "Séjour", true, false, 60, false, 2700, 30, 100, false},
    {TXT("Lampadaire 3", "Floor Lamp 3"), "Séjour", true, false, 60, false, 2700, 30, 100, false},
    {TXT("Lampe fenêtre", "Window Lamp"), "Séjour", true, false, 40, false, 3000, 28, 100, false},
    {TXT("Globe lunaire", "Moon Globe"), "Séjour", false, false, 50, false, 2700, 0, 0, false},
    {TXT("Bandeau WLED", "WLED Strip"), TXT("Atelier", "Workshop"), true, false, 100, false, 5000, 200, 100, false},
    {TXT("Bandeau Lidl", "Lidl Strip"), TXT("Atelier", "Workshop"), true, false, 80, false, 4000, 200, 100, false},
    {TXT("Panneau LED", "LED Panel"), TXT("Atelier", "Workshop"), true, false, 100, false, 5000, 0, 0, false},
    {TXT("Bandeau bureau", "Desk Strip"), TXT("Bureau", "Office"), true, false, 80, true, 4000, 220, 100, false},
    {TXT("Lampe de bureau", "Desk Lamp"), TXT("Bureau", "Office"), false, false, 70, false, 4500, 0, 0, false},
    {TXT("Chevet", "Bedside Lamp"), TXT("Chambre", "Bedroom"), true, false, 20, false, 2400, 0, 0, false},
};
void (*lu_commande)(int lampe) = nullptr;

// PIECES : comparé aux noms de pièces venant de HA (protocole) → jamais traduit ; l'affichage : NOMS_PIECES
static const char *PIECES[] = {"Toutes", "Séjour", "Atelier", "Bureau", "Chambre"};
static const char *NOMS_PIECES[] = {TXT("Toutes", "All"), TXT("Séjour", "Living"), TXT("Atelier", "Workshop"), TXT("Bureau", "Office"), TXT("Chambre", "Bedroom")};
#define DISQUE 220
// 24/10 (retour d'usage : « la pipette va plus loin que le disque ») : rayon parcouru par le CENTRE de la pipette (26 px + son
// liseré) pour qu'elle reste dedans ; le doigt au bord du disque vaut toujours 100 % de saturation
#define RAYON_PIPETTE (DISQUE / 2.0f - 15)
#define LISTE_L 152
#define NOM_L (LISTE_L - 5 - 6 - 36) // place du nom dans la tuile (après le rond)
#define DROITE_X (UI_MARGE + LISTE_L + 8)
#define DROITE_L (UI_ZONE_L - UI_MARGE - DROITE_X)

static int g_piece = 0;
// 25/10 (retour d'usage : « pas de présélection, et la dernière sélection ») : rien au démarrage, puis gardée d'une visite à l'autre
static bool g_sel[LU_NB] = {};
static bool g_long = false; // un appui long vient d'avoir lieu : le relâcher ne fait pas un tap
static uint8_t *g_disque_buf = nullptr; // image du disque, calculée une fois (PSRAM sur l'écran)
static bool g_disque_echec_signale = false; // 06/11 (audit 9.6) : l'échec d'allocation ne se dit qu'une fois

static lv_obj_t *o_liste, *o_pieces[5], *o_tuiles[LU_NB], *o_ronds[LU_NB], *o_noms[LU_NB], *o_vals[LU_NB], *o_coches[LU_NB];
static lv_obj_t *o_disque, *o_bouton, *o_lum, *o_lum_cache, *o_lum_trait, *o_lum_val, *o_temp, *o_temp_val, *o_temp_trait;

// --- couleurs ---
static lv_color_t hsv(float h, float s)
{
    h = fmodf(h, 360) / 60;
    int i = (int)h;
    float f = h - i, p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f);
    float r, g, b;
    switch (i)
    {
    case 0: r = 1, g = t, b = p; break;
    case 1: r = q, g = 1, b = p; break;
    case 2: r = p, g = 1, b = t; break;
    case 3: r = p, g = q, b = 1; break;
    case 4: r = t, g = p, b = 1; break;
    default: r = 1, g = p, b = q; break;
    }
    return lv_color_make((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

// blanc de 2200 K (orangé) à 6500 K (bleuté), comme la maquette
static lv_color_t blanc(int k)
{
    static const float T[] = {0, 0.45f, 0.7f, 1};
    static const uint8_t C[][3] = {{255, 159, 69}, {255, 217, 168}, {244, 245, 246}, {207, 224, 255}};
    float t = LV_CLAMP(0.0f, (k - 2200) / 4300.0f, 1.0f);
    int i = 0;
    while (i < 2 && t > T[i + 1])
        i++;
    float f = (t - T[i]) / (T[i + 1] - T[i]);
    return lv_color_make((uint8_t)(C[i][0] + (C[i + 1][0] - C[i][0]) * f), (uint8_t)(C[i][1] + (C[i + 1][1] - C[i][1]) * f),
                         (uint8_t)(C[i][2] + (C[i + 1][2] - C[i][2]) * f));
}

static lv_color_t couleur(const lu_lampe_t &l) { return l.mode_hs ? hsv(l.teinte, l.sat / 100.0f) : blanc(l.kelvin); }

// première lampe sélectionnée en ligne : c'est elle que montrent le disque et les jauges
static int reference()
{
    for (int i = 0; i < LU_NB; i++)
        if (g_sel[i] && !lu_lampes[i].hors_ligne)
            return i;
    return -1;
}
static bool selection_couleur()
{
    for (int i = 0; i < LU_NB; i++)
        if (g_sel[i] && !lu_lampes[i].hors_ligne && lu_lampes[i].couleur)
            return true;
    return false;
}

static void envoyer(int i)
{
    if (lu_commande)
        lu_commande(i);
}

// --- disque : teinte autour (rouge en haut, sens horaire), saturation du centre (blanc) au bord, bord lissé ---
static void calculer_disque()
{
    if (g_disque_buf)
        return;
#if LVGL_VERSION_MAJOR >= 9
    // 05/11 (vague D) : lv_mem_alloc a disparu (allocateurs publics 9.6 : lv_malloc/lv_free, stdlib/lv_mem.h:67,95)
    // et LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA aussi — même contenu : 3 octets/px (RGB565 puis A8), tampon serré
    // (LV_DRAW_BUF_STRIDE_ALIGN = 1 dans conf_v9). NOTE : en 8.4 lv_mem_alloc = ps_malloc (PSRAM d'office) ;
    // lv_malloc en 9.6 suit le malloc d'Arduino (interne d'abord, PSRAM au-delà du seuil) — non mesuré ici.
    // 06/11 CORRECTIF CRASH (page Lumières) : en 9.6, LV_COLOR_FORMAT_RGB565A8 est PLANAIRE — plan couleur
    // (stride = largeur * 2) puis masque A8 (stride / 2 par ligne) : src/draw/sw/lv_draw_sw_img.c:254-261 et
    // src/image/lv_bin_decoder.c:506-519. La 8.4, elle, entrelaçait couleur+alpha (3 o/px). Le remplissage
    // ci-dessous écrivait « sizeof(lv_color_t) + 1 » par pixel, soit 4 o/px en 9.6 (lv_color_t = bleu, vert,
    // rouge : include/lvgl/draw/lv_color.h:108-113) dans un tampon dimensionné pour 3 o/px : 220*220*4 =
    // 193 600 octets écrits dans 145 200, soit 48 400 octets par-dessus — corruption du tas, redémarrage sur
    // chien de garde (mesuré par la campagne de diagnostic : 3/3, traces « disque rempli » jamais émises).
    // Le total ne change pas : 2 o de couleur + 1 o de masque par pixel = 145 200 octets.
    g_disque_buf = (uint8_t *)lv_malloc(DISQUE * DISQUE * 3);
#ifdef ARDUINO
    // 06/11 diagnostic : la valeur du pointeur dit si le tampon est en PSRAM (0x3C…) ou en interne (0x3F…)
    ui_trace("[lum-diag] tampon %p ; libre total %u o ; PSRAM %u o\n", (const void *)g_disque_buf,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    // v6 : le tas PSRAM est-il encore intègre APRÈS ce malloc de 145 200 o, AVANT qu'on le remplisse ?
    ui_trace("[lum-diag] apres malloc : integrite PSRAM %d\n", heap_caps_check_integrity_all(MALLOC_CAP_SPIRAM) ? 1 : 0);
#endif
#else
    g_disque_buf = (uint8_t *)lv_mem_alloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(DISQUE, DISQUE));
#endif
    if (!g_disque_buf)
    {
        // 06/11 (audit 9.6, chantier 3) : l'allocation de 145 200 o n'était pas vérifiée et la boucle de
        // remplissage qui suit écrivait immédiatement à travers le nul — la famille du dépassement déjà
        // réparé ici même (72210b8). En cas d'échec : pas de remplissage, pas de disque dessiné (la
        // source de l'objet image n'est pas posée, l'objet est caché), dit UNE fois dans les traces ;
        // le tampon n'étant jamais libéré, la visite suivante de la page retente une fois.
        if (!g_disque_echec_signale)
        {
            g_disque_echec_signale = true;
            ui_trace("[lumieres] allocation disque ÉCHOUÉE (%d o) : disque chromatique non dessiné\n", DISQUE * DISQUE * 3);
        }
        return;
    }
    const float R = DISQUE / 2.0f;
    uint8_t *p = g_disque_buf;
    for (int y = 0; y < DISQUE; y++)
        for (int x = 0; x < DISQUE; x++)
        {
            float dx = x + 0.5f - R, dy = y + 0.5f - R, r = sqrtf(dx * dx + dy * dy);
            float h = atan2f(dx, -dy) * 180 / 3.14159265f;
            if (h < 0)
                h += 360;
            lv_color_t c = hsv(h, LV_MIN(1.0f, r / R));
            const uint8_t alpha = (uint8_t)(255 * LV_CLAMP(0.0f, R - r + 0.5f, 1.0f));
#if LVGL_VERSION_MAJOR >= 9
            // Plan couleur d'abord : RGB565 NATIF (lv_color_to_u16, draw/lv_color.h:343), 2 o par pixel.
            const uint16_t v = lv_color_to_u16(c);
            memcpy(p + 2 * (y * DISQUE + x), &v, sizeof v);
            // Masque A8 à part, après tout le plan couleur : 1 o par pixel, même ordre de lignes.
            g_disque_buf[2 * DISQUE * DISQUE + y * DISQUE + x] = alpha;
#else
            memcpy(p, &c, sizeof c);
            p[sizeof c] = alpha;
            p += sizeof c + 1;
#endif
        }
#ifdef ARDUINO
    // v6 : le remplissage (145 200 o en PSRAM) vient d'écrire — les deux tas sont-ils intègres ?
    ui_trace("[lum-diag] disque rempli : integrite interne %d\n", heap_caps_check_integrity_all(MALLOC_CAP_INTERNAL) ? 1 : 0);
    ui_trace("[lum-diag] disque rempli : integrite PSRAM %d\n", heap_caps_check_integrity_all(MALLOC_CAP_SPIRAM) ? 1 : 0);
#endif
}

// --- jauge de température : une colonne par pixel, couleur du blanc correspondant ---
static void temp_cb(lv_event_t *e)
{
    uc_cible_t * ctx = uc_cible_du(e); // 9.6 : la layer de l'événement (avant : lv_event_get_draw_ctx)
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    const int l = lv_area_get_width(&a);
    for (int x = 0; x < l; x++)
    {
        d.bg_color = blanc(2200 + 4300 * x / (l - 1));
        lv_area_t c = {(lv_coord_t)(a.x1 + x), a.y1, (lv_coord_t)(a.x1 + x), a.y2};
        uc_carre(ctx, d, &c); // NOTE coût 9.6 : une task par colonne (l tasks/redessin), ajout en O(n) — cf. vague B
    }
}

static void rafraichir()
{
    if (!o_liste)
        return;
    int nb_sel = 0;
    for (bool s : g_sel)
        nb_sel += s;
    for (int i = 0; i < 5; i++)
        if (i == g_piece)
            lv_obj_add_state(o_pieces[i], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_pieces[i], LV_STATE_CHECKED);

    for (int i = 0; i < LU_NB; i++)
    {
        const lu_lampe_t &l = lu_lampes[i];
        const bool visible = g_piece == 0 || !strcmp(l.piece, PIECES[g_piece]);
        if (!visible)
        {
            ui_cacher(o_tuiles[i], true);
            continue;
        }
        ui_label_texte(o_noms[i], l.nom); // le nom vient de HA (renommer une lampe sans reflasher)
        // 24/10 (retour d'usage : « réduire la police quand les noms sont trop longs ») : 14 s'il tient, sinon 12 (« … » au-delà)
        const bool tient = uc_largeur_texte(l.nom, UI_POLICE_PETITE) <= NOM_L; // 9.6 : lv_txt_get_width mort, voir ui_courbe.h
        ui_style_text_font(o_noms[i], tient ? UI_POLICE_PETITE : UI_POLICE_LEGENDE, 0);
        ui_cacher(o_tuiles[i], false);
        const bool on = l.on && !l.hors_ligne;
        const lv_color_t c = couleur(l);
        ui_style_bg_color(o_ronds[i], on ? c : ui_c(UI_CARTE2), 0);
        ui_style_border_color(o_ronds[i], on ? c : ui_c(UI_ETEINT), 0);
        ui_style_shadow_width(o_ronds[i], on ? 12 : 0, 0);
        ui_style_shadow_color(o_ronds[i], c, 0);
        // le pourcentage en police pixel ; les mots (« éteinte », « hors ligne ») en petit Montserrat, en gris lisible
        // (24/10, harmonie : « OFF » en pixel gris foncé ne se lisait pas sur la tuile)
        ui_style_text_font(o_vals[i], on ? UI_POLICE_3X5_P : UI_POLICE_LEGENDE, 0);
        if (l.hors_ligne)
            ui_label_texte(o_vals[i], TXT("hors ligne", "offline"));
        else if (on)
            ui_label_texte_fmt(o_vals[i], "%d%%", l.lum);
        else
            ui_label_texte(o_vals[i], TXT("éteinte", "off"));
        ui_style_text_color(o_vals[i], ui_c(on ? UI_TEXTE : UI_TEXTE2), 0);
        ui_style_bg_color(o_tuiles[i], ui_c(g_sel[i] ? UI_ACCENT_FOND : UI_CARTE), 0);
        ui_style_border_color(o_tuiles[i], ui_c(g_sel[i] ? UI_ACCENT : UI_BORD), 0);
        ui_style_border_width(o_tuiles[i], g_sel[i] && nb_sel > 1 ? 2 : 1, 0);
        ui_label_texte(o_coches[i], g_sel[i] && nb_sel > 1 ? LV_SYMBOL_OK : "");
        ui_style_opa(o_tuiles[i], l.hors_ligne ? LV_OPA_50 : LV_OPA_COVER, 0);
    }

    // réglages de la lampe de référence
    const int r = reference();
    const bool coul = selection_couleur();
    ui_style_opa(o_disque, coul ? LV_OPA_COVER : LV_OPA_30, 0);
    if (r < 0)
    {
        ui_cacher(o_bouton, true);
        ui_style_opa(o_lum, LV_OPA_40, 0);
        ui_style_opa(o_temp, LV_OPA_40, 0);
        ui_label_texte(o_lum_val, "");
        ui_label_texte(o_temp_val, "");
        return;
    }
    const lu_lampe_t &l = lu_lampes[r];
    const bool on = l.on;
    if (coul && l.mode_hs)
    {
        ui_cacher(o_bouton, false);
        float ang = l.teinte * 3.14159265f / 180, d = RAYON_PIPETTE * l.sat / 100;
        lv_obj_set_pos(o_bouton, (lv_coord_t)(DISQUE / 2 + d * sinf(ang) - 13), (lv_coord_t)(DISQUE / 2 - d * cosf(ang) - 13));
        ui_style_bg_color(o_bouton, couleur(l), 0);
    }
    else
        ui_cacher(o_bouton, true);

    ui_style_opa(o_lum, LV_OPA_COVER, 0);
    ui_style_bg_grad_color(o_lum, couleur(l), 0);
    const int plein = on ? l.lum : 0;
    lv_obj_set_width(o_lum_cache, (lv_coord_t)((DROITE_L - 2) * (100 - plein) / 100));
    lv_obj_set_x(o_lum_trait, (lv_coord_t)((DROITE_L - 2) * plein / 100 - 2));
    if (on)
        ui_label_texte_fmt(o_lum_val, "%d%%", l.lum);
    else
        ui_label_texte(o_lum_val, "OFF");
    // la valeur se met du côté opposé au repère, pour ne jamais être coupée par lui
    ui_aligner(o_lum_val, plein > 50 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, plein > 50 ? 10 : -10, 0);

    ui_style_opa(o_temp, LV_OPA_COVER, 0);
    if (l.mode_hs)
    {
        ui_label_texte(o_temp_val, TXT("BLANC", "WHITE"));
        ui_aligner(o_temp_val, LV_ALIGN_LEFT_MID, 10, 0);
        ui_cacher(o_temp_trait, true); // 06/11 : pointeur direct — l'enfant 1 est le dégradé depuis le correctif clip_corner
    }
    else
    {
        ui_label_texte_fmt(o_temp_val, "%d K", l.kelvin);
        lv_obj_t *t = o_temp_trait;
        ui_cacher(t, false);
        const int pos = (l.kelvin - 2200) * 100 / 4300;
        lv_obj_set_x(t, (lv_coord_t)((DROITE_L - 2) * pos / 100 - 2));
        ui_aligner(o_temp_val, pos > 50 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, pos > 50 ? 10 : -10, 0);
    }
}

void page_lumieres_maj() { rafraichir(); }

// applique un changement à toute la sélection (lampes en ligne ; la couleur seulement aux lampes couleur)
// 24/10 : pendant un glissé (disque, curseurs), au plus un envoi toutes les ENVOI_GLISSE_MS, la lampe suit quand même le
// doigt ; le dernier réglage part au lâcher (fin_glisse). Avant : un envoi par mouvement du doigt, ~50 par seconde et par lampe.
#define ENVOI_GLISSE_MS 300
static uint32_t g_dernier_envoi = 0;
static bool g_envoi_en_retard = false;

template <typename F> static void sur_selection(bool couleur_seule, F f, bool glisse = false)
{
    const bool envoi = !glisse || lv_tick_elaps(g_dernier_envoi) >= ENVOI_GLISSE_MS;
    for (int i = 0; i < LU_NB; i++)
        if (g_sel[i] && !lu_lampes[i].hors_ligne && (!couleur_seule || lu_lampes[i].couleur))
        {
            f(lu_lampes[i]);
            if (envoi)
                envoyer(i);
        }
    g_envoi_en_retard = !envoi;
    if (envoi)
        g_dernier_envoi = lv_tick_get();
    rafraichir();
}

// lâcher d'un glissé : envoie le dernier réglage s'il n'est pas parti ; true si l'évènement était un lâcher
static bool fin_glisse(lv_event_code_t code, bool couleur_seule)
{
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST)
        return false;
    if (g_envoi_en_retard)
        sur_selection(couleur_seule, [](lu_lampe_t &) {});
    return true;
}

// --- Événements ---
// 25/10 (retour d'usage : « un appui long sur la pièce sélectionne tout, puis rien ») : toutes les lampes en ligne de la pièce ;
// si elles l'étaient déjà toutes : plus rien
static bool dans_piece(int i) { return g_piece == 0 || !strcmp(lu_lampes[i].piece, PIECES[g_piece]); }
static void piece_long_cb(lv_event_t *e)
{
    g_piece = (int)(intptr_t)lv_event_get_user_data(e);
    bool toutes = true, une = false;
    for (int i = 0; i < LU_NB; i++)
        if (dans_piece(i) && !lu_lampes[i].hors_ligne)
        {
            une = true;
            toutes = toutes && g_sel[i];
        }
    for (int i = 0; i < LU_NB; i++)
        g_sel[i] = une && !toutes && dans_piece(i) && !lu_lampes[i].hors_ligne;
    rafraichir();
}

static void piece_cb(lv_event_t *e)
{
    g_piece = (int)(intptr_t)lv_event_get_user_data(e);
    rafraichir();
    // 24/10 (retour d'usage : « la liste des ampoules ne remonte pas quand je change de pièce ») : une autre pièce = la liste en haut
    lv_obj_update_layout(o_liste);
    lv_obj_scroll_to_y(o_liste, 0, LV_ANIM_OFF);
}

static void rond_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (lu_lampes[i].hors_ligne)
        return;
    lu_lampes[i].on = !lu_lampes[i].on;
    rafraichir();
    envoyer(i);
}

static void tuile_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_LONG_PRESSED) // appui long : ajouter / retirer de la sélection
    {
        g_long = true;
        g_sel[i] = !g_sel[i];
        rafraichir();
    }
    else if (code == LV_EVENT_CLICKED)
    {
        if (g_long)
        {
            g_long = false;
            return;
        }
        memset(g_sel, 0, sizeof g_sel); // tap court : régler cette lampe seule
        g_sel[i] = true;
        rafraichir();
    }
}

static void disque_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (fin_glisse(code, true) || (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING))
        return;
    if (!selection_couleur())
        return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    lv_area_t a;
    lv_obj_get_coords(o_disque, &a);
    float dx = p.x - a.x1 - DISQUE / 2.0f, dy = p.y - a.y1 - DISQUE / 2.0f;
    float h = atan2f(dx, -dy) * 180 / 3.14159265f;
    if (h < 0)
        h += 360;
    int s = (int)(LV_MIN(1.0f, sqrtf(dx * dx + dy * dy) / RAYON_PIPETTE) * 100);
    sur_selection(true, [&](lu_lampe_t &l) { l.on = true; l.mode_hs = true; l.teinte = (int)h; l.sat = s; }, true);
}

static int valeur_x(lv_obj_t *obj, int mini, int maxi)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return mini + LV_CLAMP(0, p.x - a.x1, lv_area_get_width(&a)) * (maxi - mini) / lv_area_get_width(&a);
}

static void lum_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (fin_glisse(code, false) || (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) || reference() < 0)
        return;
    int v = LV_MAX(1, valeur_x(o_lum, 0, 100));
    sur_selection(false, [&](lu_lampe_t &l) { l.on = true; l.lum = v; }, true); // glisser allume
}

static void temp_cb_touche(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (fin_glisse(code, false) || (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) || reference() < 0)
        return;
    int k = (valeur_x(o_temp, 2200, 6500) + 50) / 100 * 100;
    sur_selection(false, [&](lu_lampe_t &l) { l.on = true; l.mode_hs = false; l.kelvin = k; }, true);
}

static void marche_cb(lv_event_t *e)
{
    bool on = (bool)(intptr_t)lv_event_get_user_data(e);
    sur_selection(false, [&](lu_lampe_t &l) { l.on = on; });
}

// --- Construction ---
static lv_obj_t *jauge(lv_obj_t *zone, lv_coord_t y)
{
    lv_obj_t *j = ui_conteneur(zone);
    lv_obj_set_size(j, DROITE_L, 46);
    lv_obj_set_pos(j, DROITE_X, y);
    lv_obj_set_style_radius(j, 12, 0);
    ui_style_border_width(j, 1, 0);
    ui_style_border_color(j, ui_c(UI_BORD), 0);
    lv_obj_set_style_clip_corner(j, true, 0);
    lv_obj_add_flag(j, LV_OBJ_FLAG_CLICKABLE);
    return j;
}

static lv_obj_t *trait(lv_obj_t *j, uint32_t c)
{
    lv_obj_t *t = ui_conteneur(j);
    lv_obj_set_size(t, 4, 38);
    lv_obj_set_y(t, 3);
    lv_obj_set_style_radius(t, 2, 0);
    ui_style_bg_color(t, ui_c(c), 0);
    ui_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
    return t;
}

void page_lumieres_creer(lv_obj_t *zone)
{
    // pièces
    lv_obj_t *sel = ui_conteneur(zone);
    ui_style_ajouter(sel, &ui_st_carte, 0);
    lv_obj_set_size(sel, UI_ZONE_L - 2 * UI_MARGE, 40);
    lv_obj_set_pos(sel, UI_MARGE, 4);
    lv_obj_set_style_radius(sel, 12, 0);
    lv_obj_set_style_pad_all(sel, 3, 0);
    lv_obj_set_flex_flow(sel, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < 5; i++)
    {
        lv_obj_t *b = ui_bouton(sel, 10, 32);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 9, 0);
        ui_style_border_width(b, 0, 0);
        ui_style_bg_opa(b, LV_OPA_TRANSP, 0);
        ui_style_text_color(b, ui_c(UI_TEXTE2), 0);
        ui_style_text_font(b, UI_POLICE_PETITE, 0);
        ui_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        ui_style_bg_color(b, ui_c(UI_CARTE2), LV_STATE_CHECKED);
        ui_style_text_color(b, ui_c(UI_TEXTE), LV_STATE_CHECKED);
        lv_obj_t *l = lv_label_create(b);
        ui_label_texte(l, NOMS_PIECES[i]);
        lv_obj_center(l);
        o_pieces[i] = b;
        lv_obj_add_event_cb(b, piece_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, piece_long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
    }

    // lampes (colonne qui défile)
    o_liste = ui_conteneur(zone);
    lv_obj_set_size(o_liste, LISTE_L, UI_ZONE_H - 52 - 10);
    lv_obj_set_pos(o_liste, UI_MARGE, 52);
    lv_obj_add_flag(o_liste, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(o_liste, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(o_liste, LV_SCROLLBAR_MODE_OFF);
    ui_defilement_rapide(o_liste); // 05/11 : scroll par bande (garde = style lu a la creation)
    lv_obj_set_flex_flow(o_liste, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o_liste, 5, 0);
    for (int i = 0; i < LU_NB; i++)
    {
        lv_obj_t *t = lv_btn_create(o_liste);
        lv_obj_remove_style_all(t);
        // 24/10 (retour d'usage : « le pourcentage mord sur la bordure du bas ») : 50 px (46 avant) ; le pourcentage en police
        // pixel (26 px de hauteur de ligne) sous un nom en 14 ne tenait pas : il débordait en bas, ou touchait le nom
        lv_obj_set_size(t, LISTE_L, 50);
        lv_obj_set_style_radius(t, 12, 0);
        ui_style_bg_opa(t, LV_OPA_COVER, 0);
        ui_style_border_width(t, 1, 0);
        lv_obj_set_style_pad_left(t, 5, 0);
        lv_obj_set_style_pad_right(t, 6, 0);
        lv_obj_add_event_cb(t, tuile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(t, tuile_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        o_tuiles[i] = t;

        lv_obj_t *r = lv_btn_create(t); // interrupteur rapide de la lampe
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 30, 30);
        lv_obj_align(r, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
        ui_style_bg_opa(r, LV_OPA_COVER, 0);
        ui_style_border_width(r, 2, 0);
        lv_obj_set_style_shadow_spread(r, 1, 0);
        lv_obj_set_ext_click_area(r, 6);
        lv_obj_add_event_cb(r, rond_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        o_ronds[i] = r;

        lv_obj_t *nom = ui_label(t, UI_POLICE_PETITE, UI_TEXTE, lu_lampes[i].nom);
        lv_label_set_long_mode(nom, LV_LABEL_LONG_DOT);
        lv_obj_set_size(nom, NOM_L, 17); // une seule ligne (hauteur fixée, sinon le texte passe à la ligne)
        lv_obj_set_pos(nom, 36, 4);
        o_noms[i] = nom;
        o_vals[i] = ui_label(t, UI_POLICE_3X5_P, UI_TEXTE, "");
        lv_obj_set_pos(o_vals[i], 36, 24);
        o_coches[i] = ui_label(t, UI_POLICE_TEXTE, UI_ACCENT, ""); // coche de la sélection multiple, en bas à droite
        lv_obj_align(o_coches[i], LV_ALIGN_BOTTOM_RIGHT, 0, -3);
    }
    lum_diag("lignes");

    // disque chromatique (image calculée) + bouton qui montre la couleur réglée
    calculer_disque();
    lum_diag("disque");
#if LVGL_VERSION_MAJOR >= 9
    // 9.6 : lv_img_dsc_t -> lv_image_dsc_t (draw/lv_image_dsc.h:141-149) ; le header perd always_zero et gagne
    // magic + stride (:29-50) ; LV_IMG_CF_TRUE_COLOR_ALPHA -> LV_COLOR_FORMAT_RGB565A8, mais PAS la même
    // disposition : la 9.6 est PLANAIRE (tout le plan couleur, stride = DISQUE*2, puis tout le masque A8),
    // la 8.4 entrelaçait couleur+alpha (3 o/px, stride = DISQUE*3). Corrigé le 06/11 : l'ancienne note, qui
    // affirmait « même disposition », est la cause du dépassement d'écriture et du redémarrage sur chien de garde.
    static lv_image_dsc_t img;
    memset(&img, 0, sizeof img);
    img.header.magic = LV_IMAGE_HEADER_MAGIC;
    img.header.cf = LV_COLOR_FORMAT_RGB565A8;
    img.header.w = DISQUE;
    img.header.h = DISQUE;
    img.header.stride = DISQUE * 2; // 06/11 : planaire en 9.6 -> stride du plan couleur = largeur * 2 (et non * 3)
    img.data_size = DISQUE * DISQUE * 3;
    img.data = g_disque_buf;
#else
    static lv_img_dsc_t img;
    img.header.always_zero = 0;
    img.header.w = DISQUE;
    img.header.h = DISQUE;
    img.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    img.data_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(DISQUE, DISQUE);
    img.data = g_disque_buf;
#endif
    o_disque = lv_img_create(zone);
    if (g_disque_buf)
        ui_img_src(o_disque, &img);
    else
        ui_cacher(o_disque, true); // allocation échouée (voir calculer_disque) : disque non dessiné — 06/11 audit 9.6
    // 24/10 (retour d'usage : « en visant le bas du disque, un bout de disque rouge apparaît dessous ») : taille fixe ; en taille
    // automatique, le repère qui dépassait du bord agrandissait l'image, que LVGL répète alors (le haut, rouge, dessous).
    // Le repère reste visible en entier au bord.
    lv_obj_set_size(o_disque, DISQUE, DISQUE);
    lv_obj_add_flag(o_disque, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_pos(o_disque, DROITE_X, 52);
    lv_obj_add_flag(o_disque, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_disque, disque_cb, LV_EVENT_ALL, NULL);
    o_bouton = ui_conteneur(o_disque);
    lv_obj_set_size(o_bouton, 26, 26);
    lv_obj_set_style_radius(o_bouton, LV_RADIUS_CIRCLE, 0);
    ui_style_bg_opa(o_bouton, LV_OPA_COVER, 0);
    ui_style_border_width(o_bouton, 4, 0);
    ui_style_border_color(o_bouton, ui_c(UI_POIGNEE), 0);
    lv_obj_set_style_outline_width(o_bouton, 1, 0);
    lv_obj_set_style_outline_color(o_bouton, ui_c(UI_FOND), 0);
    lv_obj_clear_flag(o_bouton, LV_OBJ_FLAG_CLICKABLE);

    // luminosité : noir -> couleur de la lampe, partie au-delà de la valeur assombrie
    o_lum = jauge(zone, 282);
    ui_style_bg_opa(o_lum, LV_OPA_COVER, 0);
    ui_style_bg_color(o_lum, lv_color_black(), 0);
    ui_style_bg_grad_dir(o_lum, LV_GRAD_DIR_HOR, 0);
    o_lum_cache = ui_conteneur(o_lum);
    lv_obj_set_height(o_lum_cache, lv_pct(100));
    lv_obj_align(o_lum_cache, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_style_bg_color(o_lum_cache, ui_c(UI_FOND), 0);
    ui_style_bg_opa(o_lum_cache, LV_OPA_80, 0);
    lv_obj_clear_flag(o_lum_cache, LV_OBJ_FLAG_CLICKABLE);
    o_lum_trait = trait(o_lum, UI_POIGNEE);
    o_lum_val = ui_label(o_lum, UI_POLICE_3X5_P, UI_POIGNEE, "");
    lv_obj_align(o_lum_val, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(o_lum, lum_cb, LV_EVENT_ALL, NULL);
    lum_diag("lum");

    // température de blanc
    o_temp = jauge(zone, 336);
    // 06/11 (audit 9.6, chantier 3) : le dégradé (temp_cb, une colonne par pixel, pleine hauteur)
    // peignait les angles arrondis de la jauge en carré. En 8.4 le masque de clip_corner couvrait
    // aussi les callbacks maison (8.4 core/lv_obj.c:554-566 + core/lv_event.c:452-470) ; en 9.6 il
    // ne rogne plus que les ENFANTS (9.6 core/lv_refr.c:171-263). Mesuré capture avant
    // (.banc_v9_esp/grandes_repeintes/cc_avant_lumieres.raw) : aux 4 coins de la boîte
    // (172,336)-(391,381), obtenu (255,157,65) à gauche et (205,226,255) à droite, attendu le fond
    // de page (8,16,16). Le dégradé passe dans un ENFANT de la jauge, patron éprouvé du ballon
    // (1ce763d) : l'enfant est rogné dans les deux lignes. Il n'est pas cliquable : le contact — et
    // temp_cb_touche, qui commande les lampes réelles — restent sur la jauge, inchangés.
    lv_obj_t *temp_grad = ui_conteneur(o_temp);
    lv_obj_set_size(temp_grad, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(temp_grad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(temp_grad, temp_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_set_style_border_post(o_temp, true, 0); // bordure visible par-dessus le dégradé rogné
    o_temp_val = ui_label(o_temp, UI_POLICE_3X5_P, UI_SUR_ACCENT, ""); // enfant 1
    lv_obj_align(o_temp_val, LV_ALIGN_LEFT_MID, 10, 0);
    o_temp_trait = trait(o_temp, UI_SUR_ACCENT); // enfant 2
    lv_obj_add_event_cb(o_temp, temp_cb_touche, LV_EVENT_ALL, NULL);
    lum_diag("temp");

    // allumer / éteindre la sélection
    lv_obj_t *rang = ui_conteneur(zone);
    lv_obj_set_size(rang, DROITE_L, 52);
    lv_obj_set_pos(rang, DROITE_X, 412);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int k = 0; k < 2; k++)
    {
        lv_obj_t *b = ui_bouton(rang, (DROITE_L - 8) / 2, 52);
        if (k == 0)
            ui_style_ajouter(b, &ui_st_bouton_principal, 0);
        lv_obj_t *l = lv_label_create(b);
        ui_label_texte(l, k == 0 ? TXT("Allumer", "Turn On") : TXT("Éteindre", "Turn Off"));
        lv_obj_center(l);
        lv_obj_add_event_cb(b, marche_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(k == 0));
    }
    lum_diag("boutons");
    rafraichir();
}

void page_lumieres_quitter()
{
    o_liste = nullptr; // rafraichir() ne touche plus à rien (l'image du disque est gardée pour la prochaine fois)
}
