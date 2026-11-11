#include "page_musique.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "ui_theme.h"
#include "ui_courbe.h" // 05/11 (vague D) : wrappers uc_* — dessin du bas d'anneau sous 9.6
#include "ui_diag.h"
#include "ui_musique.h" // mu_appui_ms (latence des commandes)
#include <stdio.h>
#include <string.h>
#if LVGL_VERSION_MAJOR >= 9
// 05/11 (vague D) : en 9.6 la STRUCTURE lv_hit_test_info_t est devenue privée (src/core/lv_obj_event_private.h,
// l'en-tête public ne garde que le typedef). Mêmes champs qu'en 8.4 (point : le point écran, res : le résultat) —
// on inclut l'en-tête privé (le dépôt a déjà -Ilib/lvgl-9.6.0/src, comme pour <src/draw/sw/lv_draw_sw.h>).
#include <src/core/lv_obj_event_private.h>
#endif

UI_PSRAM(mu_etat_t, mu_etat_p, 1, nullptr); // 29/10 : en PSRAM (ui_psram.h)
void (*mu_commande)(mu_action_t action, int arg) = nullptr;

// vues : 0 Lecture, 1 Playlist (bouton ☰, rattachée à Lecture), 2 Biblio, 3 Radios, 4 Sortie
static int g_vue = 0;
static int g_artiste = -1;   // Biblio : artiste ouvert (-1 = liste des artistes)
static int g_album = 0;      // Biblio : album choisi
static bool g_vider = false; // Playlist : « Vider » demande confirmation sur place
static bool g_eteindre = false; // Sortie : l'arrêt de la chaîne demande confirmation sur place
static bool g_temps_restant = false;
static lv_coord_t g_x_appui;
// 24/10 (retour d'usage : « pourquoi tu vérifies pas si la pochette est la même avant de redessiner ») : la pochette affichée
// (image, ou nom peint), pour ne la redessiner et ne faire le fondu que quand ELLE change, pas à chaque morceau du même album
static char g_pochette_affichee[72];
static lv_obj_t *o_cases[MU_LISTE_MAX]; // cases de la grille d'albums (Biblio) : une pochette arrivée s'y pose seule
static lv_obj_t *o_logos[MU_RADIOS_MAX]; // 25/10 : carré du logo de chaque radio (vue Radios)

static lv_obj_t *g_zone = nullptr;
static lv_obj_t *o_onglets[4];
// Lecture
static lv_obj_t *o_vol_arc, *o_prog, *o_pochette, *o_poch_txt, *o_poch_img, *o_temps, *o_vol_val, *o_sortie;
static lv_obj_t *o_titre, *o_sous, *o_alea, *o_lecture_txt;
// listes
static lv_obj_t *o_liste;

// teintes des pochettes dessinées (dégradé), choisies par le nom
static const uint32_t TEINTES[][2] = {{0x3B6E8F, 0x1C2F45}, {0xB85C38, 0x4A2116}, {0x6A4C93, 0x261A3A}, {0x2E8B6F, 0x123A2E},
                                      {0xB0893A, 0x463614}, {0x8F3B5C, 0x3A1726}, {0x4C6A93, 0x1A263A}};
static int teinte_de(const char *s)
{
    unsigned h = 0;
    while (*s)
        h = h * 31 + (uint8_t)*s++;
    return h % 7;
}
static void peindre(lv_obj_t *o, const char *nom)
{
    int t = teinte_de(nom);
    ui_style_bg_opa(o, LV_OPA_COVER, 0);
    ui_style_bg_color(o, ui_c(TEINTES[t][0]), 0);
    ui_style_bg_grad_color(o, ui_c(TEINTES[t][1]), 0);
    ui_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}

static void envoyer(mu_action_t a, int arg = 0)
{
    if (mu_commande)
        mu_commande(a, arg);
}
static void construire();

static void mmss(char *b, size_t n, int s) { snprintf(b, n, "%d:%02d", s / 60, s % 60); }

// =====================================================================  LECTURE
static void opa_cb(void *o, int32_t v) { ui_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void fondu()
{
    lv_anim_t a; // 40 % -> 100 % en 0,6 s
    lv_anim_init(&a);
    lv_anim_set_var(&a, o_pochette);
    lv_anim_set_exec_cb(&a, opa_cb);
    lv_anim_set_values(&a, 102, 255);
    lv_anim_set_time(&a, 600);
    lv_anim_start(&a);
}

static void rafraichir_lecture()
{
    const mu_etat_t &m = mu_etat;
    const bool radio = m.duree == 0 && m.artiste[0]; // durée 0 sans radio : rien en lecture
    char t[64], a[16];
    char cle[72];
    if (m.pochette)
        snprintf(cle, sizeof cle, "%p", (const void *)m.pochette);
    else
        snprintf(cle, sizeof cle, "%s", radio ? m.artiste : m.album);
    if (strcmp(g_pochette_affichee, cle))
    {
        if (g_pochette_affichee[0])
            fondu();
        snprintf(g_pochette_affichee, sizeof g_pochette_affichee, "%s", cle);
    }
    ui_label_texte(o_titre, m.titre);
    if (!m.duree && !radio)
        t[0] = 0;
    else if (radio)
        snprintf(t, sizeof t, TXT("%s · en direct", "%s · live"), m.artiste);
    else if (m.nb_file)
        snprintf(t, sizeof t, "%s · %s · %d / %d", m.artiste, m.album, m.premiere + m.piste + 1, LV_MAX(m.nb_total, m.nb_file));
    else
        snprintf(t, sizeof t, "%s · %s", m.artiste, m.album);
    ui_label_texte(o_sous, t);

    // pochette : image fournie, sinon dégradé + nom de l'album
    if (m.pochette)
    {
        ui_img_src(o_poch_img, m.pochette);
        ui_cacher(o_poch_img, false);
        ui_cacher(o_poch_txt, true);
    }
    else
    {
        ui_cacher(o_poch_img, true);
        ui_cacher(o_poch_txt, false);
        ui_label_texte(o_poch_txt, radio ? m.artiste : m.album);
        peindre(o_pochette, radio ? m.artiste : m.album);
    }

    if (!lv_obj_has_state(o_vol_arc, LV_STATE_PRESSED))
        lv_arc_set_value(o_vol_arc, (int16_t)m.volume);
    ui_label_texte_fmt(o_vol_val, "%d", m.volume);
    ui_style_text_color(o_vol_val, ui_c(lv_obj_has_state(o_vol_arc, LV_STATE_PRESSED) ? UI_ACCENT : UI_TEXTE), 0);

    lv_arc_set_value(o_prog, radio ? 1000 : (int16_t)(m.duree ? m.position * 1000 / m.duree : 0));
    ui_style_arc_color(o_prog, ui_c(radio ? UI_ETEINT : m.lecture ? UI_TEXTE : UI_TEXTE2), LV_PART_INDICATOR);
    if (radio)
        ui_label_texte(o_temps, TXT("DIRECT", "LIVE"));
    else if (!m.duree)
        ui_label_texte(o_temps, "");
    else
    {
        mmss(t, sizeof t, m.position);
        if (g_temps_restant)
        {
            mmss(a, sizeof a, m.duree - m.position);
            ui_label_texte_fmt(o_temps, "%s -%s", t, a);
        }
        else
        {
            mmss(a, sizeof a, m.duree);
            ui_label_texte_fmt(o_temps, "%s / %s", t, a);
        }
    }
    ui_style_text_color(o_temps, ui_c(g_temps_restant ? UI_ACCENT : UI_TEXTE2), 0);

    const char *sortie = m.nb_lecteurs ? m.lecteurs[m.lecteur].nom : "–";
    ui_label_texte(o_sortie, sortie);
    // nom long (« PC-musique », « Projecteur ») : plus petit, pour ne pas mordre sur l'anneau du volume
    ui_style_text_font(o_sortie, uc_largeur_texte(sortie, UI_POLICE_TEXTE_22) > 100 ? UI_POLICE_TEXTE : UI_POLICE_TEXTE_22, 0); // 9.6 : lv_txt_get_width mort, voir ui_courbe.h
    ui_label_texte(o_lecture_txt, m.lecture ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    ui_style_border_color(o_alea, ui_c(m.alea ? UI_ACCENT : UI_BORD), 0);
    ui_style_bg_color(o_alea, ui_c(m.alea ? UI_ACCENT_FOND : UI_CARTE), 0);
    ui_style_text_color(o_alea, ui_c(m.alea ? UI_ACCENT : UI_TEXTE2), 0);
}

static void volume_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
        mu_etat.volume = lv_arc_get_value(o_vol_arc);
    else if (code == LV_EVENT_RELEASED)
        envoyer(MU_VOLUME, mu_etat.volume);
    else if (code != LV_EVENT_PRESSED)
        return;
    rafraichir_lecture();
}

// 25/10 (retour d'usage : « une transition sur la jauge du volume en bas, comme le thermostat ») : l'ouverture du bas fermée par
// un segment un cran plus sombre que le fond, avec un fondu de 10° depuis la couleur de chaque bout (ui_anneau.cpp)
#define VOL_FIN 0x0A0E14
static void volume_bas_cb(lv_event_t *e)
{
    uc_cible_t * ctx = uc_cible_du(e); // 9.6 : la layer de l'événement (avant : lv_event_get_draw_ctx)
    lv_area_t a;
    lv_obj_get_coords(o_vol_arc, &a);
    const lv_point_t c = {(lv_coord_t)(a.x1 + lv_area_get_width(&a) / 2), (lv_coord_t)(a.y1 + lv_area_get_height(&a) / 2)};
    const lv_coord_t r = lv_area_get_width(&a) / 2;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.width = lv_obj_get_style_arc_width(o_vol_arc, LV_PART_MAIN);
    d.rounded = 0;
    d.color = ui_c(VOL_FIN);
    uc_arc(ctx, d, &c, r, 46, 134); // 9.6 : centre/rayon/angles passent dans le descripteur (uc_arc)
    const int v = lv_arc_get_value(o_vol_arc);
    const lv_color_t ind = lv_obj_get_style_arc_color(o_vol_arc, LV_PART_INDICATOR), fond = lv_obj_get_style_arc_color(o_vol_arc, LV_PART_MAIN);
    const lv_color_t gauche = v > 0 ? ind : fond, droite = v >= 100 ? ind : fond; // l'anneau part d'en bas à gauche
    for (int i = 0; i < 10; i++)
    {
        const uint8_t part = (uint8_t)(255 * (i + 0.5f) / 10);
        d.color = lv_color_mix(ui_c(VOL_FIN), droite, part);
        uc_arc(ctx, d, &c, r, 45 + i, 45 + i + 2);
        d.color = lv_color_mix(ui_c(VOL_FIN), gauche, part);
        uc_arc(ctx, d, &c, r, 134 - i, 134 - i + 2);
    }
    // NOTE coût 9.6 : 21 tasks (1 segment + 20 fondus) par redraw de l'anneau, ajout en O(n) — cf. vague B.
}

// 24/10 (retour d'usage : « j'ai du mal avec le volume, il visait le bouton source ») : seul l'anneau attrape le doigt (LVGL
// n'exclut que le disque intérieur : les coins du carré attrapaient aussi), avec une marge pour le doigt
#define VOL_MARGE 14
static void volume_touche_cb(lv_event_t *e)
{
    lv_hit_test_info_t *info = (lv_hit_test_info_t *)lv_event_get_param(e);
    lv_area_t a;
    lv_obj_get_coords(o_vol_arc, &a);
    const int cx = a.x1 + lv_area_get_width(&a) / 2, cy = a.y1 + lv_area_get_height(&a) / 2, r = lv_area_get_width(&a) / 2;
    const int dx = info->point->x - cx, dy = info->point->y - cy, d2 = dx * dx + dy * dy;
    const int r_in = r - lv_obj_get_style_arc_width(o_vol_arc, LV_PART_MAIN) - VOL_MARGE, r_out = r + VOL_MARGE;
    info->res = d2 >= r_in * r_in && d2 <= r_out * r_out;
}

static void pochette_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED)
        g_x_appui = p.x;
    else if (lv_event_get_code(e) == LV_EVENT_RELEASED)
    {
        int dx = p.x - g_x_appui; // glisser : vers la gauche = suivant, vers la droite = précédent
        if (dx < -40)
            envoyer(MU_SUIVANT);
        else if (dx > 40)
            envoyer(MU_PRECEDENT);
    }
}

static void bouton_cb(lv_event_t *e)
{
    int b = (int)(intptr_t)lv_event_get_user_data(e);
    switch (b)
    {
    case 0: mu_etat.alea = !mu_etat.alea; envoyer(MU_ALEA); break;
    case 1: envoyer(MU_PRECEDENT); break;
    case 2: mu_etat.lecture = !mu_etat.lecture; envoyer(MU_LECTURE_PAUSE); break;
    case 3: envoyer(MU_SUIVANT); break;
    case 4: g_vue = 1; construire(); return; // playlist
    case 5: g_temps_restant = !g_temps_restant; break;
    case 6: g_vue = 4; construire(); return; // sortie
    }
    rafraichir_lecture();
}

#define ROND_D 56 // boutons ronds de la lecture
static lv_obj_t *rond(lv_obj_t *parent, lv_coord_t d, const char *symb, int id, bool principal)
{
    lv_obj_t *b = ui_bouton(parent, d, d);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    if (principal)
        ui_style_ajouter(b, &ui_st_bouton_principal, 0);
    lv_obj_t *t = ui_label(b, principal ? UI_POLICE_TITRE : UI_POLICE_TEXTE, principal ? UI_SUR_ACCENT : UI_TEXTE, symb);
    lv_obj_center(t);
    // 25/10 (retour d'usage : « 1 s de retard sur next ») : précédent, lecture et suivant partent dès l'APPUI (mesuré : le relâchement
    // coûtait 120 à 140 ms de plus) ; les autres boutons au clic, comme partout
    lv_obj_add_event_cb(b, [](lv_event_t *) { mu_appui_ms = lv_tick_get(); }, LV_EVENT_PRESSED, nullptr); // latence (25/10)
    lv_obj_add_event_cb(b, bouton_cb, id >= 1 && id <= 3 ? LV_EVENT_PRESSED : LV_EVENT_CLICKED, (void *)(intptr_t)id);
    return b;
}

static void construire_lecture(lv_obj_t *z)
{
    const lv_coord_t bx = 52, by = 48, D = 300;
    // volume : arc épais 270°, ouvert en bas
    o_vol_arc = lv_arc_create(z);
    lv_obj_set_size(o_vol_arc, D, D);
    lv_obj_set_pos(o_vol_arc, bx, by);
    lv_arc_set_rotation(o_vol_arc, 135);
    lv_arc_set_bg_angles(o_vol_arc, 0, 270);
    lv_arc_set_range(o_vol_arc, 0, 100);
    lv_obj_set_style_arc_width(o_vol_arc, 19, LV_PART_MAIN);
    lv_obj_set_style_arc_width(o_vol_arc, 19, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(o_vol_arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(o_vol_arc, false, LV_PART_INDICATOR);
    ui_style_arc_color(o_vol_arc, ui_c(UI_CARTE2), LV_PART_MAIN);
    ui_style_arc_color(o_vol_arc, ui_c(0xC98F3A), LV_PART_INDICATOR);
    ui_style_arc_color(o_vol_arc, ui_c(UI_ACCENT), LV_PART_INDICATOR | LV_STATE_PRESSED);
    ui_style_bg_color(o_vol_arc, ui_c(UI_POIGNEE), LV_PART_KNOB);
    ui_style_border_color(o_vol_arc, ui_c(UI_FOND), LV_PART_KNOB);
    ui_style_border_width(o_vol_arc, 4, LV_PART_KNOB);
    lv_obj_set_style_pad_all(o_vol_arc, 6, LV_PART_KNOB);
    ui_style_shadow_width(o_vol_arc, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(o_vol_arc, volume_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(o_vol_arc, LV_OBJ_FLAG_ADV_HITTEST); // sans lui, LVGL n'appelle pas le test : tout le carré attrape
    lv_obj_add_event_cb(o_vol_arc, volume_bas_cb, LV_EVENT_DRAW_MAIN_BEGIN, NULL); // sous l'anneau et sa poignée
    lv_obj_add_event_cb(o_vol_arc, volume_touche_cb, LV_EVENT_HIT_TEST, NULL);

    // progression : liseré fin autour de la pochette, depuis le haut
    const lv_coord_t dp = 244;
    o_prog = lv_arc_create(z);
    lv_obj_set_size(o_prog, dp, dp);
    lv_obj_set_pos(o_prog, bx + (D - dp) / 2, by + (D - dp) / 2);
    lv_arc_set_rotation(o_prog, 270);
    lv_arc_set_bg_angles(o_prog, 0, 360);
    lv_arc_set_range(o_prog, 0, 1000);
    lv_obj_set_style_arc_width(o_prog, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(o_prog, 4, LV_PART_INDICATOR);
    ui_style_arc_color(o_prog, ui_c(UI_CARTE2), LV_PART_MAIN);
    lv_obj_remove_style(o_prog, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(o_prog, LV_OBJ_FLAG_CLICKABLE);

    // pochette ronde (glisser = changer de morceau)
    const lv_coord_t dc = 228;
    o_pochette = ui_conteneur(z);
    lv_obj_set_size(o_pochette, dc, dc);
    lv_obj_set_pos(o_pochette, bx + (D - dc) / 2, by + (D - dc) / 2);
    lv_obj_set_style_radius(o_pochette, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(o_pochette, true, 0);
    lv_obj_add_flag(o_pochette, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_pochette, pochette_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(o_pochette, pochette_cb, LV_EVENT_RELEASED, NULL);
    o_poch_img = lv_img_create(o_pochette);
    lv_obj_center(o_poch_img);
    o_poch_txt = ui_label(o_pochette, UI_POLICE_TITRE, 0xD9DCE0, "");
    lv_obj_set_width(o_poch_txt, dc - 60);
    lv_obj_set_style_text_align(o_poch_txt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(o_poch_txt);

    // temps dans l'ouverture du bas (tap = temps restant)
    lv_obj_t *tb = lv_btn_create(z);
    lv_obj_remove_style_all(tb);
    // 25/10 (retour d'usage : « le temps touche le disque de l'écoulement ») : 8 px plus bas (l'anneau de progression descend à
    // y 320, le temps commençait à 314) ; 26 px de haut pour ne pas toucher le titre (y 352)
    lv_obj_set_size(tb, 130, 26);
    lv_obj_set_pos(tb, bx + (D - 130) / 2, by + 274);
    o_temps = ui_label(tb, UI_POLICE_3X5_P, UI_TEXTE2, "");
    lv_obj_center(o_temps);
    lv_obj_add_event_cb(tb, bouton_cb, LV_EVENT_CLICKED, (void *)5);

    // volume en haut à gauche, sortie en face à droite
    o_vol_val = ui_label(z, UI_POLICE_3X5_M, UI_TEXTE, "");
    lv_obj_set_pos(o_vol_val, UI_MARGE, 54);
    lv_obj_set_pos(ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE2, TXT("volume", "volume")), UI_MARGE, 92);
    lv_obj_t *sb = lv_btn_create(z);
    lv_obj_remove_style_all(sb);
    lv_obj_set_size(sb, 110, 58);
    lv_obj_align(sb, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 50);
    o_sortie = ui_label(sb, UI_POLICE_TEXTE_22, UI_TEXTE, "");
    lv_obj_align(o_sortie, LV_ALIGN_TOP_RIGHT, 0, 4);
    lv_obj_align(ui_label(sb, UI_POLICE_LEGENDE, UI_TEXTE2, TXT("sortie", "output")), LV_ALIGN_TOP_RIGHT, 0, 40);
    lv_obj_add_event_cb(sb, bouton_cb, LV_EVENT_CLICKED, (void *)6);
    // l'anneau passe devant le bouton de sortie (la poignée, volume haut, était dessous) et devant la pochette (son carré
    // attrape le doigt) ; 25/10 : le temps repasse devant l'anneau (son segment du bas le recouvrait)
    lv_obj_move_foreground(o_vol_arc);
    lv_obj_move_foreground(tb);

    o_titre = ui_label(z, UI_POLICE_TITRE, UI_TEXTE, "");
    lv_label_set_long_mode(o_titre, LV_LABEL_LONG_DOT);
    lv_obj_set_size(o_titre, UI_ZONE_L - 2 * UI_MARGE, 26);
    lv_obj_set_style_text_align(o_titre, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(o_titre, UI_MARGE, 352);
    o_sous = ui_label(z, UI_POLICE_PETITE, UI_TEXTE2, "");
    lv_label_set_long_mode(o_sous, LV_LABEL_LONG_DOT);
    lv_obj_set_size(o_sous, UI_ZONE_L - 2 * UI_MARGE, 18);
    lv_obj_set_style_text_align(o_sous, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(o_sous, UI_MARGE, 379);

    lv_obj_t *r = ui_conteneur(z);
    lv_obj_set_size(r, UI_ZONE_L - 2 * UI_MARGE, 66);
    lv_obj_set_pos(r, UI_MARGE, 404);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 14, 0);
    // 25/10 (retour d'usage) : les 5 de la même taille (44 / 56 / 66 avant)
    o_alea = rond(r, ROND_D, LV_SYMBOL_SHUFFLE, 0, false);
    rond(r, ROND_D, LV_SYMBOL_PREV, 1, false);
    lv_obj_t *lp = rond(r, ROND_D, LV_SYMBOL_PAUSE, 2, true);
    o_lecture_txt = lv_obj_get_child(lp, 0);
    rond(r, ROND_D, LV_SYMBOL_NEXT, 3, false);
    rond(r, ROND_D, LV_SYMBOL_LIST, 4, false);
}

// =====================================================================  LISTES
// conteneur qui défile, sous les onglets (laisse la place à une barre d'actions en bas si besoin)
static lv_obj_t *liste(lv_obj_t *z, lv_coord_t y, lv_coord_t h, bool grille)
{
    lv_obj_t *l = ui_conteneur(z);
    lv_obj_set_size(l, UI_ZONE_L, h);
    lv_obj_set_pos(l, 0, y);
    lv_obj_add_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
    ui_defilement_rapide(l); // 05/11 : scroll par bande (garde = style lu a la creation)
    lv_obj_set_flex_flow(l, grille ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(l, UI_MARGE, 0);
    lv_obj_set_style_pad_bottom(l, 8, 0);
    lv_obj_set_style_pad_row(l, grille ? 8 : 4, 0);
    lv_obj_set_style_pad_column(l, 8, 0);
    return l;
}

// ligne de liste : bouton plein largeur, titre + sous-titre à gauche, valeur à droite
static lv_obj_t *ligne(lv_obj_t *l, const char *titre, const char *sous, const char *droite, bool choisie, lv_event_cb_t cb, int id)
{
    lv_obj_t *b = ui_bouton(l, lv_pct(100), 52);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_hor(b, 14, 0);
    if (choisie)
    {
        ui_style_bg_color(b, ui_c(UI_ACCENT_FOND), 0);
        ui_style_border_color(b, ui_c(UI_ACCENT), 0);
    }
    lv_obj_t *t = ui_label(b, UI_POLICE_TEXTE, choisie ? UI_ACCENT : UI_TEXTE, titre);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_size(t, 270, 20);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 7);
    lv_obj_align(ui_label(b, UI_POLICE_LEGENDE, UI_TEXTE2, sous), LV_ALIGN_BOTTOM_LEFT, 0, -7);
    lv_obj_align(ui_label(b, UI_POLICE_3X5_P, UI_TEXTE2, droite), LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    return b;
}

static lv_obj_t *barre_actions(lv_obj_t *z, const char *a, const char *b, bool b_danger, lv_event_cb_t cb)
{
    lv_obj_t *r = ui_conteneur(z);
    lv_obj_set_size(r, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(r, UI_MARGE, 412);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int k = 0; k < 2; k++)
    {
        lv_obj_t *x = ui_bouton(r, (UI_ZONE_L - 2 * UI_MARGE - 8) / 2, 52);
        if (k == 1 && b_danger)
        {
            ui_style_bg_color(x, ui_c(g_vider ? UI_ROUGE : 0x2A1717), 0);
            ui_style_border_color(x, ui_c(g_vider ? UI_ROUGE : 0x5A2A26), 0);
            ui_style_text_color(x, ui_c(g_vider ? UI_SUR_ACCENT : 0xFF8A7A), 0);
        }
        else if (k == 0 && !b_danger)
            ui_style_ajouter(x, &ui_st_bouton_principal, 0);
        lv_obj_t *t = lv_label_create(x);
        ui_label_texte(t, k ? b : a);
        lv_obj_center(t);
        lv_obj_add_event_cb(x, cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
    }
    return r;
}

// --- Playlist ---
static void piste_cb(lv_event_t *e)
{
    mu_etat.piste = (int)(intptr_t)lv_event_get_user_data(e);
    envoyer(MU_JOUER_PISTE, mu_etat.piste);
    construire();
}
static void modes_file_cb(lv_event_t *e)
{
    if ((int)(intptr_t)lv_event_get_user_data(e))
        mu_etat.repete = !mu_etat.repete, envoyer(MU_REPETE);
    else
        mu_etat.alea = !mu_etat.alea, envoyer(MU_ALEA);
    construire();
}
static void actions_file_cb(lv_event_t *e)
{
    if (!(int)(intptr_t)lv_event_get_user_data(e))
    {
        g_vider = false;
        g_vue = 0;
    }
    else if (!g_vider)
        g_vider = true; // 1er appui : confirmation
    else
    {
        g_vider = false;
        envoyer(MU_VIDER_FILE);
    }
    construire();
}

static void construire_playlist(lv_obj_t *z)
{
    lv_obj_t *t = ui_label(z, UI_POLICE_TITRE, UI_TEXTE, TXT("Playlist", "Playlist"));
    lv_obj_set_pos(t, UI_MARGE + 2, 58);
    const char *noms[] = {LV_SYMBOL_SHUFFLE " " TXT("Aléatoire", "Shuffle"), LV_SYMBOL_LOOP " " TXT("Répéter", "Repeat")};
    const bool etats[] = {mu_etat.alea, mu_etat.repete};
    for (int k = 0; k < 2; k++)
    {
        lv_obj_t *b = ui_bouton(z, k ? 110 : 120, 36);
        lv_obj_align(b, LV_ALIGN_TOP_RIGHT, k ? -UI_MARGE : -UI_MARGE - 118, 52);
        lv_obj_set_style_radius(b, 12, 0);
        ui_style_bg_color(b, ui_c(etats[k] ? UI_ACCENT_FOND : UI_CARTE), 0);
        ui_style_border_color(b, ui_c(etats[k] ? UI_ACCENT : UI_BORD), 0);
        lv_obj_t *l = ui_label(b, UI_POLICE_TEXTE, etats[k] ? UI_ACCENT : UI_TEXTE2, noms[k]); // police avec les symboles LVGL
        lv_obj_center(l);
        lv_obj_add_event_cb(b, modes_file_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
    }
    o_liste = liste(z, 96, 308, false);
    char d[12], n[8];
    for (int i = 0; i < mu_etat.nb_file; i++)
    {
        const mu_piste_t &p = mu_etat.file[i];
        mmss(d, sizeof d, p.duree);
        snprintf(n, sizeof n, "%d", mu_etat.premiere + i + 1);
        lv_obj_t *b = ligne(o_liste, p.titre, p.artiste, d, i == mu_etat.piste, piste_cb, i);
        lv_obj_set_style_pad_left(b, 40, 0);
        // numéro, ou égaliseur pour le morceau en cours
        lv_obj_t *num = ui_label(b, UI_POLICE_3X5_P, i == mu_etat.piste ? UI_ACCENT : UI_TEXTE3,
                                 i == mu_etat.piste && mu_etat.lecture ? LV_SYMBOL_AUDIO : n);
        if (i == mu_etat.piste && mu_etat.lecture)
            ui_style_text_font(num, UI_POLICE_TEXTE, 0);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, -28, 0);
    }
    barre_actions(z, LV_SYMBOL_LEFT " " TXT("Lecture", "Play"),
                  g_vider ? TXT("Toucher encore : vider", "Tap again to clear") : TXT("Vider…", "Clear…"),
                  true, actions_file_cb);
}

// --- Biblio ---
static void artiste_cb(lv_event_t *e)
{
    g_artiste = (int)(intptr_t)lv_event_get_user_data(e);
    g_album = 0;
    envoyer(MU_OUVRIR_ARTISTE, g_artiste); // l'appelant remplit les albums
    construire();
}
static void poser_pochette(lv_obj_t *p, const lv_img_dsc_t *img)
{
    lv_obj_set_style_clip_corner(p, true, 0);
    lv_obj_set_style_border_post(p, true, 0); // le liseré de l'album choisi par-dessus la pochette
    lv_obj_t *i = lv_img_create(p);
    lv_obj_center(i);
    ui_img_src(i, img);
}

void page_musique_pochette_album(int i)
{
    if (!g_zone || i < 0 || i >= MU_LISTE_MAX || !o_cases[i] || !mu_etat.albums[i].img)
        return;
    lv_obj_t *p = o_cases[i];
    lv_obj_t *e = lv_obj_get_child(p, 0); // le nom écrit sur la case peinte, ou l'image déjà posée
#if LVGL_VERSION_MAJOR >= 9
    if (e && lv_obj_check_type(e, &lv_image_class)) // 9.6 : lv_img_class -> lv_image_class (widgets/lv_image.h)
#else
    if (e && lv_obj_check_type(e, &lv_img_class))
#endif
    {
        ui_img_src(e, mu_etat.albums[i].img);
        return;
    }
    if (e)
        lv_obj_del(e);
    poser_pochette(p, mu_etat.albums[i].img);
}

static void retour_artistes_cb(lv_event_t *)
{
    g_artiste = -1;
    construire();
}
// 29/10 (retour d'usage : « je sélectionne un album ça me remet en haut de page ») : le liseré change de case, la grille reste
// (avant : toute la Biblio reconstruite, la liste repartait en haut)
static void album_cb(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_album >= 0 && g_album < MU_LISTE_MAX && o_cases[g_album])
        ui_style_border_width(o_cases[g_album], 0, 0);
    g_album = i;
    if (i >= 0 && i < MU_LISTE_MAX && o_cases[i])
        ui_style_border_width(o_cases[i], 3, 0);
}
static void actions_album_cb(lv_event_t *e)
{
    if ((int)(intptr_t)lv_event_get_user_data(e))
        envoyer(MU_AJOUTER_ALBUM, g_album);
    else
    {
        envoyer(MU_LIRE_ALBUM, g_album);
        g_vue = 0;
        construire();
    }
}

// 23/10 : bande alphabétique, 537 artistes dans MA, une lettre à la fois. 24/10 (retour d'usage) : à DROITE, plus large, de la
// hauteur de la première carte au bas de l'écran ; elle ne montre que la moitié de l'alphabet (lettres plus espacées) et
// glisse progressivement vers l'autre moitié quand le doigt approche de son bout (d'autant plus vite qu'il en est près) ;
// la lettre survolée s'affiche à gauche de la bande, à hauteur du doigt
static const char LETTRES[] = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";
#define NB_LETTRES 27
#define BANDE_L 40
#define BANDE_X (UI_ZONE_L - BANDE_L - 4)
#define BANDE_Y 88 // la hauteur de la première carte
#define BANDE_PAS 27
#define BANDE_H (14 * BANDE_PAS)                        // 378 px : la moitié de l'alphabet
#define BANDE_DEC_MAX (NB_LETTRES * BANDE_PAS - BANDE_H) // 351 px
#define BANDE_BORD 60                                   // zone, en haut et en bas, où la bande glisse
#define BANDE_VITESSE 450                               // px/s, le doigt tout au bout
static lv_obj_t *o_lettres[NB_LETTRES], *o_bulle, *o_bande_int, *o_fleche_haut, *o_fleche_bas;
static int g_bande_dec = 0; // décalage de la bande (0 : #..M visibles ; BANDE_DEC_MAX : M..Z), gardé d'une construction à l'autre
static uint32_t g_bande_t;

static bool lettre_dispo(char c) { return strchr(mu_etat.lettres, c) != nullptr; }

// 24/10 (retour d'usage : « une flèche pour dire que l'alphabet monte ou descend ») : flèche en haut s'il reste des lettres
// au-dessus, en bas s'il en reste dessous
static void bande_placer()
{
    lv_obj_set_y(o_bande_int, -g_bande_dec);
    ui_cacher(o_fleche_haut, g_bande_dec <= 0);
    ui_cacher(o_fleche_bas, g_bande_dec >= BANDE_DEC_MAX);
}

static lv_obj_t *fleche(lv_obj_t *bande, const char *symbole, lv_align_t ou)
{
    lv_obj_t *f = ui_conteneur(bande); // fond de la bande : les lettres glissent dessous
    lv_obj_set_size(f, BANDE_L, 20);
    lv_obj_align(f, ou, 0, 0);
    ui_style_bg_color(f, ui_c(UI_CARTE), 0);
    ui_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE); // l'appui reste à la bande (c'est sa zone de glisse)
    lv_obj_center(ui_label(f, UI_POLICE_TEXTE, UI_ACCENT, symbole)); // la police de 16 a les symboles (pas celle de 12)
    return f;
}

static void construire_async(void *) { construire(); }

static void bande_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a); // 9.6 : lv_event_get_target renvoie void* (uc_objet_du fait le cast)
    const int y = p.y - a.y1;
    if (code == LV_EVENT_PRESSED)
        g_bande_t = lv_tick_get();
    if (code == LV_EVENT_PRESSING) // près d'un bout : la bande glisse vers l'autre moitié
    {
        const uint32_t dt = lv_tick_elaps(g_bande_t);
        g_bande_t = lv_tick_get();
        int v = 0; // px/s, signé
        if (y > BANDE_H - BANDE_BORD)
            v = BANDE_VITESSE * LV_MIN(y - (BANDE_H - BANDE_BORD), BANDE_BORD) / BANDE_BORD;
        else if (y < BANDE_BORD)
            v = -BANDE_VITESSE * LV_MIN(BANDE_BORD - y, BANDE_BORD) / BANDE_BORD;
        const int dec = LV_CLAMP(0, g_bande_dec + v * (int)dt / 1000, BANDE_DEC_MAX);
        if (dec != g_bande_dec)
        {
            g_bande_dec = dec;
            bande_placer();
        }
    }
    const int i = LV_CLAMP(0, (y + g_bande_dec) / BANDE_PAS, NB_LETTRES - 1);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING)
    {
        // bulle avec la lettre survolée, à gauche de la bande à hauteur du doigt (la liste ne change qu'au relâcher)
        ui_label_texte_fmt(lv_obj_get_child(o_bulle, 0), "%c", LETTRES[i]);
        ui_style_text_color(lv_obj_get_child(o_bulle, 0), ui_c(lettre_dispo(LETTRES[i]) ? UI_TEXTE : UI_TEXTE3), 0);
        lv_obj_set_y(o_bulle, LV_CLAMP(BANDE_Y, p.y - 28, UI_ZONE_H - 64));
        ui_cacher(o_bulle, false);
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        ui_cacher(o_bulle, true);
        if (code == LV_EVENT_RELEASED && lettre_dispo(LETTRES[i]) && LETTRES[i] != mu_etat.lettre)
        {
            mu_etat.lettre = LETTRES[i];
            mu_etat.nb_artistes = 0; // en attendant les artistes
            envoyer(MU_LETTRE, LETTRES[i]);
            lv_async_call(construire_async, nullptr); // pas dans l'évènement de la bande, qu'on va détruire
        }
    }
}

static void construire_biblio(lv_obj_t *z)
{
    if (g_artiste < 0)
    {
        lv_obj_t *titre = ui_label(z, UI_POLICE_TEXTE, UI_TEXTE, "");
        if (mu_etat.nb_artistes)
            ui_label_texte_fmt(titre, TXT("%c  ·  %d artiste%s", "%c  ·  %d artist%s"), mu_etat.lettre, mu_etat.nb_artistes,
                             mu_etat.nb_artistes > 1 ? "s" : "");
        else
            ui_label_texte_fmt(titre, TXT("%c  ·  chargement…", "%c  ·  loading…"), mu_etat.lettre);
        lv_obj_set_pos(titre, UI_MARGE + 4, 58);

        o_liste = liste(z, BANDE_Y, UI_ZONE_H - BANDE_Y, false);
        lv_obj_set_width(o_liste, BANDE_X - 4);
        lv_obj_set_style_pad_right(o_liste, 4, 0);
        for (int i = 0; i < mu_etat.nb_artistes; i++)
        {
            lv_obj_t *b = ligne(o_liste, mu_etat.artistes[i].nom, "", mu_etat.artistes[i].detail, false, artiste_cb, i);
            lv_obj_set_height(b, 46);
            lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_LEFT_MID, 0, 0);
            ui_style_text_font(lv_obj_get_child(b, 2), UI_POLICE_LEGENDE, 0);
        }

        lv_obj_t *bande = ui_conteneur(z);
        lv_obj_set_size(bande, BANDE_L, BANDE_H);
        lv_obj_set_pos(bande, BANDE_X, BANDE_Y);
        ui_style_bg_color(bande, ui_c(UI_CARTE), 0);
        ui_style_bg_opa(bande, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bande, 14, 0);
        lv_obj_set_style_clip_corner(bande, true, 0);
        lv_obj_add_flag(bande, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(bande, bande_cb, LV_EVENT_ALL, NULL);
        o_bande_int = ui_conteneur(bande); // les 27 lettres ; la bande n'en montre que 14 (décalage g_bande_dec)
        lv_obj_set_size(o_bande_int, BANDE_L, NB_LETTRES * BANDE_PAS);
        lv_obj_clear_flag(o_bande_int, LV_OBJ_FLAG_CLICKABLE);
        int choisie_i = 0;
        for (int i = 0; i < NB_LETTRES; i++)
        {
            char c[2] = {LETTRES[i], 0};
            const bool choisie = LETTRES[i] == mu_etat.lettre;
            if (choisie)
                choisie_i = i;
            o_lettres[i] = ui_label(o_bande_int, UI_POLICE_TEXTE, choisie ? UI_ACCENT : lettre_dispo(LETTRES[i]) ? UI_TEXTE2 : UI_ETEINT, c);
            lv_obj_align(o_lettres[i], LV_ALIGN_TOP_MID, 0, i * BANDE_PAS + (BANDE_PAS - 18) / 2);
        }
        // la lettre en cours visible, hors des zones de glisse (sinon la moitié qui la contient)
        if (choisie_i * BANDE_PAS < g_bande_dec + BANDE_BORD || (choisie_i + 1) * BANDE_PAS > g_bande_dec + BANDE_H - BANDE_BORD)
            g_bande_dec = LV_CLAMP(0, choisie_i * BANDE_PAS + BANDE_PAS / 2 - BANDE_H / 2, BANDE_DEC_MAX);
        o_fleche_haut = fleche(bande, LV_SYMBOL_UP, LV_ALIGN_TOP_MID);
        o_fleche_bas = fleche(bande, LV_SYMBOL_DOWN, LV_ALIGN_BOTTOM_MID);
        bande_placer();
        o_bulle = ui_conteneur(z);
        lv_obj_set_size(o_bulle, 56, 56);
        lv_obj_set_x(o_bulle, BANDE_X - 56 - 10);
        lv_obj_set_style_radius(o_bulle, 14, 0);
        ui_style_bg_color(o_bulle, ui_c(UI_CARTE2), 0);
        ui_style_bg_opa(o_bulle, LV_OPA_COVER, 0);
        ui_style_border_color(o_bulle, ui_c(UI_ACCENT), 0);
        ui_style_border_width(o_bulle, 2, 0);
        lv_obj_center(ui_label(o_bulle, UI_POLICE_TITRE, UI_TEXTE, ""));
        ui_cacher(o_bulle, true);
        return;
    }
    lv_obj_t *ret = ui_bouton(z, LV_SIZE_CONTENT, 36);
    lv_obj_set_pos(ret, UI_MARGE, 52);
    lv_obj_set_style_pad_hor(ret, 12, 0);
    lv_obj_set_style_radius(ret, 12, 0);
    ui_label_texte_fmt(lv_label_create(ret), LV_SYMBOL_LEFT " %s", mu_etat.artistes[g_artiste].nom);
    lv_obj_center(lv_obj_get_child(ret, 0));
    lv_obj_add_event_cb(ret, retour_artistes_cb, LV_EVENT_CLICKED, NULL);

    o_liste = liste(z, 96, 308, true);
    const lv_coord_t c = (UI_ZONE_L - 2 * UI_MARGE - 16) / 3;
    for (int i = 0; i < mu_etat.nb_albums; i++)
    {
        const mu_element_t &a = mu_etat.albums[i];
        lv_obj_t *b = lv_btn_create(o_liste);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, c, c + 36);
        lv_obj_add_event_cb(b, album_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *p = ui_conteneur(b);
        o_cases[i] = p;
        lv_obj_set_size(p, c, c);
        lv_obj_set_style_radius(p, 12, 0);
        peindre(p, a.nom);
        ui_style_border_width(p, i == g_album ? 3 : 0, 0);
        ui_style_border_color(p, ui_c(UI_ACCENT), 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
        if (a.img) // pochette de Music Assistant (réduite à la taille de la case au téléchargement)
            poser_pochette(p, a.img);
        else
        {
            lv_obj_t *pt = ui_label(p, UI_POLICE_PETITE, 0xD9DCE0, a.nom);
            lv_obj_set_width(pt, c - 16);
            lv_obj_align(pt, LV_ALIGN_BOTTOM_LEFT, 8, -8);
        }
        lv_obj_t *n = ui_label(b, UI_POLICE_LEGENDE, UI_TEXTE, a.nom);
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_set_size(n, c, 16);
        lv_obj_set_pos(n, 0, c + 4);
        lv_obj_set_pos(ui_label(b, UI_POLICE_LEGENDE, UI_TEXTE3, a.detail), 0, c + 19);
    }
    barre_actions(z, TXT("Lire l'album", "Play Album"), TXT("Ajouter à la file", "Add to Queue"), false, actions_album_cb);
}

// --- Radios ---
static void radio_cb(lv_event_t *e)
{
    envoyer(MU_JOUER_RADIO, (int)(intptr_t)lv_event_get_user_data(e));
    g_vue = 0;
    construire();
}

static void poser_logo(lv_obj_t *c, const lv_img_dsc_t *img)
{
    lv_obj_t *i = lv_obj_get_child(c, 0);
    if (!i)
    {
        i = lv_img_create(c);
        lv_obj_center(i);
    }
    ui_img_src(i, img);
}

void page_musique_logo_radio(int i)
{
    if (g_zone && i >= 0 && i < MU_RADIOS_MAX && o_logos[i] && mu_etat.radios[i].img)
        poser_logo(o_logos[i], mu_etat.radios[i].img);
}

// 23/10 (retour d'usage) : une liste comme les artistes ; la radio en cours en orange. 25/10 : les radios FAVORITES de Music
// Assistant, avec leur logo quand MA sait le servir (« la source est MA sinon rien ») : carré vide sinon
static void construire_radios(lv_obj_t *z)
{
    lv_obj_t *titre = ui_label(z, UI_POLICE_TEXTE, UI_TEXTE, "");
    if (mu_etat.nb_radios)
        ui_label_texte_fmt(titre, TXT("Radios favorites  ·  %d", "Favorite radios  ·  %d"), mu_etat.nb_radios);
    else
        ui_label_texte(titre, TXT("Pas de radio favorite dans Music Assistant", "No favorite radios in Music Assistant"));
    lv_obj_set_pos(titre, UI_MARGE + 4, 58);
    o_liste = liste(z, 88, UI_ZONE_H - 88, false);
    for (int i = 0; i < mu_etat.nb_radios; i++)
    {
        const mu_element_t &r = mu_etat.radios[i];
        const bool joue = mu_etat.duree == 0 && !strcmp(mu_etat.artiste, r.nom);
        lv_obj_t *b = ligne(o_liste, r.nom, "", joue ? TXT("EN COURS", "PLAYING") : "", joue, radio_cb, i);
        lv_obj_set_height(b, 46);
        lv_obj_set_style_pad_left(b, 6, 0);
        lv_obj_t *t = lv_obj_get_child(b, 0);
        lv_obj_set_width(t, 270 - 44);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 44, 0);
        ui_style_text_font(lv_obj_get_child(b, 2), UI_POLICE_LEGENDE, 0);
        ui_style_text_color(lv_obj_get_child(b, 2), ui_c(UI_ACCENT), 0);
        lv_obj_t *c = ui_conteneur(b); // le logo (ou rien)
        lv_obj_set_size(c, MU_LOGO_LISTE, MU_LOGO_LISTE);
        lv_obj_align(c, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(c, 8, 0);
        lv_obj_set_style_clip_corner(c, true, 0);
        ui_style_bg_color(c, ui_c(UI_CARTE2), 0);
        ui_style_bg_opa(c, LV_OPA_COVER, 0);
        o_logos[i] = c;
        if (r.img)
            poser_logo(c, r.img);
    }
    envoyer(MU_LOGOS); // ceux qui manquent (une fois) ; ils arrivent par page_musique_logo_radio
}

// --- Sortie ---
static void lecteur_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e); // lecteur * 2 + (1 = « + » groupe)
    int i = code / 2;
    mu_lecteur_t &l = mu_etat.lecteurs[i];
    if (!l.en_ligne && (i != 0 || code % 2))
        return; // le Salon se choisit même éteint : une lecture l'allume
    if (code % 2)
    {
        l.groupe = !l.groupe;
        envoyer(MU_GROUPE, i);
    }
    else
    {
        mu_etat.lecteur = i;
        l.groupe = false;
        envoyer(MU_LECTEUR, i);
    }
    construire();
}
static void ampli_cb(lv_event_t *)
{
    if (mu_etat.chaine != MU_CH_PRETE && mu_etat.chaine != MU_CH_DEMARRAGE)
        return; // l'ampli est sur la prise de la chaîne
    mu_etat.ampli = !mu_etat.ampli;
    envoyer(MU_AMPLI);
    construire();
}
// 24/10 : marche tout de suite ; arrêt au 2e appui (la production demandait confirmation : un arrêt dure ~30 s)
static void chaine_cb(lv_event_t *)
{
    if (mu_etat.chaine == MU_CH_ETEINTE)
        envoyer(MU_CHAINE, 1);
    else if (mu_etat.chaine == MU_CH_ARRET)
        return;
    else if (!g_eteindre)
        g_eteindre = true;
    else
    {
        g_eteindre = false;
        envoyer(MU_CHAINE, 0);
    }
    construire();
}
static void entree_cb(lv_event_t *e)
{
    if (!mu_etat.ampli)
        return;
    mu_etat.entree = (int)(intptr_t)lv_event_get_user_data(e);
    envoyer(MU_ENTREE, mu_etat.entree);
    construire();
}

static lv_obj_t *interrupteur(lv_obj_t *parent, bool on)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 48, 28);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    ui_interrupteur_couleurs(sw);
    if (on)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    return sw;
}

static void construire_sortie(lv_obj_t *z)
{
    lv_obj_t *aide = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE2, TXT("un tap = écouter sur ce lecteur · « + » = joue aussi en même temps",
          "tap = play on this player · « + » = play along too"));
    lv_obj_align(aide, LV_ALIGN_TOP_MID, 0, 52);
    o_liste = liste(z, 72, 212, false);
    lv_obj_set_style_pad_row(o_liste, 6, 0);
    const mu_etat_t &m = mu_etat;
    for (int i = 0; i < m.nb_lecteurs; i++)
    {
        const mu_lecteur_t &l = m.lecteurs[i];
        const bool actif = i == m.lecteur;
        char etat[64];
        if (i == 0 && !l.en_ligne) // le Salon : l'état de la chaîne piCore
            snprintf(etat, sizeof etat, "%s", m.chaine == MU_CH_ETEINTE ? TXT("chaîne éteinte · une lecture l'allume", "chain off · playing turns it on")
                                              : m.chaine == MU_CH_ARRET ? TXT("s'éteint…", "turning off…") : TXT("démarre…", "starting…"));
        else if (actif)
            snprintf(etat, sizeof etat, m.lecture ? TXT("joue : %s", "playing: %s") : TXT("en pause", "paused"), m.titre);
        else if (l.groupe)
            snprintf(etat, sizeof etat, TXT("synchronisé avec %s", "synced with %s"), m.lecteurs[m.lecteur].nom);
        else
            snprintf(etat, sizeof etat, "%s", l.en_ligne ? TXT("libre", "free") : TXT("hors ligne", "offline"));
        lv_obj_t *b = ligne(o_liste, l.nom, etat, "", actif, lecteur_cb, i * 2);
        lv_obj_set_height(b, 60);
        ui_style_text_color(lv_obj_get_child(b, 1), ui_c(actif ? UI_ACCENT : l.groupe ? UI_VERT : UI_TEXTE3), 0);
        ui_style_opa(b, l.en_ligne || i == 0 ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_t *app = ui_label(b, UI_POLICE_LEGENDE, UI_TEXTE3, l.appareil);
        lv_obj_align_to(app, lv_obj_get_child(b, 0), LV_ALIGN_OUT_RIGHT_BOTTOM, 0, 0);
        lv_obj_align(app, LV_ALIGN_TOP_LEFT, (lv_coord_t)(uc_largeur_texte(l.nom, UI_POLICE_TEXTE) + 8), 10);
        if (!actif && l.en_ligne)
        {
            lv_obj_t *g = ui_bouton(b, 52, 46);
            lv_obj_align(g, LV_ALIGN_RIGHT_MID, 6, 0);
            lv_obj_set_style_radius(g, 12, 0);
            ui_style_bg_color(g, ui_c(l.groupe ? 0x173A2A : UI_CARTE2), 0);
            ui_style_border_color(g, ui_c(l.groupe ? UI_VERT : UI_BORD), 0);
            lv_obj_center(ui_label(g, UI_POLICE_TITRE, l.groupe ? UI_VERT : UI_TEXTE, l.groupe ? LV_SYMBOL_OK : "+"));
            lv_obj_add_event_cb(g, lecteur_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 2 + 1));
        }
    }

    // chaîne piCore : état venu de HA ; marche / arrêt (pause, ampli, prise)
    // 26/10 : piCore apparaît dans MA 25 s après l'allumage (moOde : 65 à 90 s)
    static const char *const ETATS[] = {TXT("éteinte", "off"), TXT("s'éteint…", "turning off…"), TXT("démarre… (30 s environ)", "starting… (about 30 s)"), TXT("prête", "ready")};
    const bool allumee = m.chaine == MU_CH_DEMARRAGE || m.chaine == MU_CH_PRETE;
    lv_obj_t *c = ui_bouton(z, UI_ZONE_L - 2 * UI_MARGE, 56); // ligne à interrupteur : 56 px partout (harmonie)
    lv_obj_set_pos(c, UI_MARGE, 290);
    lv_obj_set_style_pad_hor(c, 14, 0);
    if (g_eteindre)
    {
        ui_style_bg_color(c, ui_c(0x2A1717), 0);
        ui_style_border_color(c, ui_c(UI_ROUGE), 0);
    }
    lv_obj_align(ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, TXT("Chaîne piCore", "piCore chain")), LV_ALIGN_TOP_LEFT, 0, 8);
    lv_obj_align(ui_label(c, UI_POLICE_LEGENDE, g_eteindre ? UI_ROUGE : m.chaine == MU_CH_PRETE ? UI_VERT : m.chaine == MU_CH_ETEINTE ? UI_TEXTE3 : UI_ACCENT,
                          g_eteindre ? TXT("toucher encore : éteindre", "tap again: turn off") : ETATS[m.chaine]),
                 LV_ALIGN_BOTTOM_LEFT, 0, -8);
    lv_obj_align(interrupteur(c, allumee), LV_ALIGN_RIGHT_MID, 0, 0);
    ui_style_opa(c, m.chaine == MU_CH_ARRET ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_add_event_cb(c, chaine_cb, LV_EVENT_CLICKED, NULL);

    // ampli (sur la prise de la chaîne) : marche / arrêt et entrée
    lv_obj_t *a = ui_conteneur(z);
    ui_style_ajouter(a, &ui_st_carte, 0);
    lv_obj_set_size(a, UI_ZONE_L - 2 * UI_MARGE, 118);
    lv_obj_set_pos(a, UI_MARGE, 356);
    lv_obj_set_style_pad_all(a, 0, 0);
    ui_style_opa(a, allumee ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_t *l = lv_btn_create(a);
    lv_obj_remove_style_all(l); // avant la taille : elle est elle-même un style
    lv_obj_set_size(l, lv_pct(100), 56);
    lv_obj_set_style_pad_hor(l, 14, 0);
    lv_obj_align(ui_label(l, UI_POLICE_TEXTE, UI_TEXTE, TXT("Ampli", "Amp")), LV_ALIGN_TOP_LEFT, 0, 8);
    lv_obj_align(ui_label(l, UI_POLICE_LEGENDE, UI_TEXTE2, !allumee ? TXT("éteint avec la chaîne", "off with chain") : m.ampli ? TXT("allumé", "on") : TXT("éteint", "off")),
                 LV_ALIGN_BOTTOM_LEFT, 0, -6);
    lv_obj_align(interrupteur(l, m.ampli && allumee), LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(l, ampli_cb, LV_EVENT_CLICKED, NULL);
    static const char *const ENTREES[] = {"1 · piCore", "2", "3"};
    for (int k = 0; k < 3; k++)
    {
        const bool choisie = m.entree == k + 1 && m.ampli;
        lv_obj_t *b = ui_bouton(a, (UI_ZONE_L - 2 * UI_MARGE - 28 - 16) / 3, 44);
        lv_obj_set_pos(b, 14 + k * ((UI_ZONE_L - 2 * UI_MARGE - 28 - 16) / 3 + 8), 60);
        lv_obj_set_style_radius(b, 12, 0);
        ui_style_bg_color(b, ui_c(choisie ? UI_ACCENT_FOND : UI_CARTE2), 0);
        ui_style_border_color(b, ui_c(choisie ? UI_ACCENT : UI_BORD), 0);
        ui_style_opa(b, m.ampli ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_center(ui_label(b, UI_POLICE_PETITE, choisie ? UI_ACCENT : UI_TEXTE, ENTREES[k]));
        lv_obj_add_event_cb(b, entree_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(k + 1));
    }
}

// =====================================================================  PAGE
static void onglet_cb(lv_event_t *e)
{
    g_vue = (int)(intptr_t)lv_event_get_user_data(e);
    g_vider = g_eteindre = false;
    construire();
}

static uint32_t compter(lv_obj_t *o)
{
    uint32_t n = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
        n += compter(lv_obj_get_child(o, i));
    return n;
}

// 27/10 (audit des redessins) : hors de Lecture, la vue était reconstruite à CHAQUE état reçu (musique/etat toutes les
// 5 s, et MA) : 85 % de l'écran, et la bibliothèque entière pendant qu'on la parcourt ; elle ne l'est plus que si ce
// qu'elle montre change (empreinte de ce que lit sa construction)
static uint32_t g_empreinte = 0;
static uint32_t empreinte_vue()
{
    const mu_etat_t &m = mu_etat;
    uint32_t e = ui_empreinte(&g_vue, sizeof g_vue);
    switch (g_vue)
    {
    case 1:
    {
        e = ui_empreinte(m.file, sizeof m.file[0] * LV_CLAMP(0, m.nb_file, MU_FILE_MAX), e);
        const int v[] = {m.nb_file, m.piste, m.premiere, m.lecture, m.alea, m.repete};
        return ui_empreinte(v, sizeof v, e);
    }
    case 2:
    {
        e = ui_empreinte(m.artistes, sizeof m.artistes[0] * LV_CLAMP(0, m.nb_artistes, MU_ARTISTES_MAX), e);
        e = ui_empreinte(m.albums, sizeof m.albums[0] * LV_CLAMP(0, m.nb_albums, MU_LISTE_MAX), e);
        e = ui_empreinte(m.lettres, sizeof m.lettres, e);
        const int v[] = {m.nb_artistes, m.nb_albums, m.lettre};
        return ui_empreinte(v, sizeof v, e);
    }
    case 3:
    {
        e = ui_empreinte(m.radios, sizeof m.radios[0] * LV_CLAMP(0, m.nb_radios, MU_RADIOS_MAX), e);
        e = ui_empreinte(m.artiste, sizeof m.artiste, e);
        const int v[] = {m.nb_radios, m.duree};
        return ui_empreinte(v, sizeof v, e);
    }
    default:
    {
        e = ui_empreinte(m.lecteurs, sizeof m.lecteurs[0] * LV_CLAMP(0, m.nb_lecteurs, 6), e);
        const int v[] = {m.nb_lecteurs, m.lecteur, m.chaine, m.ampli, m.entree};
        return ui_empreinte(v, sizeof v, e);
    }
    }
}

static void construire()
{
    const uint32_t t0 = lv_tick_get();
    g_empreinte = empreinte_vue();
    lv_obj_clean(g_zone);
    o_vol_arc = nullptr;
    memset(o_cases, 0, sizeof o_cases);
    memset(o_logos, 0, sizeof o_logos);
    static const char *const ONGLETS[] = {TXT("Lecture", "Play"), TXT("Biblio", "Library"), TXT("Radios", "Radios"), TXT("Sortie", "Output")};
    static const int VUES[] = {0, 2, 3, 4};
    int choisi = 0; // la playlist (vue 1) fait partie de Lecture
    for (int i = 0; i < 4; i++)
        if (VUES[i] == g_vue)
            choisi = i;
    ui_onglets(g_zone, ONGLETS, 4, choisi, onglet_cb, VUES, o_onglets);
    switch (g_vue)
    {
    case 0: construire_lecture(g_zone); rafraichir_lecture(); break;
    case 1: construire_playlist(g_zone); break;
    case 2: ui_musique_demander_biblio(); construire_biblio(g_zone); break; // 26/10 : sur batterie, lue maintenant
    case 3: ui_musique_demander_biblio(); construire_radios(g_zone); break;
    default: construire_sortie(g_zone); break;
    }
    // 24/10 : durée de construction de la vue (le dessin vient après, dans le rendu de LVGL) et nombre d'objets
    ui_trace("[musique] vue %d construite en %u ms, %u objets\n", g_vue, (unsigned)lv_tick_elaps(t0), (unsigned)compter(g_zone));
}

void page_musique_maj()
{
    if (!g_zone)
        return;
    if (g_vue == 0 && o_vol_arc)
        rafraichir_lecture(); // la lecture se met à jour sans reconstruire (le doigt peut être sur le volume)
    else if (g_vue == 0 || empreinte_vue() != g_empreinte)
        construire();
}

void page_musique_maj_lecture()
{
    if (g_zone && g_vue == 0 && o_vol_arc)
        rafraichir_lecture();
}

bool page_musique_affichee() { return g_zone != nullptr; }

void page_musique_creer(lv_obj_t *zone)
{
    g_zone = zone;
    g_vue = 0;
    g_artiste = -1;
    g_pochette_affichee[0] = 0;
    construire();
}

void page_musique_quitter()
{
    g_zone = nullptr; // plus de mises à jour
    ui_musique_quitter(); // 11/11 : rendre pochettes, pochette en lecture et logos de radios (voir ui_musique.cpp)
}
