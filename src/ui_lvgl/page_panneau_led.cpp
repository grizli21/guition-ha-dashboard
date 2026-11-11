#include "page_panneau_led.h"
#include "ui_theme.h"
#include <stdio.h>
#include <string.h>

// valeurs de départ : panneau arrêté, tout réglé au maximum (le scénario du simulateur et MQTT les remplacent)
pnl_etat_t pnl_etat = {false, false, PNL_TOUTES, PNL_MINUTERIE, 15, PNL_RESTANT_INCONNU,
                       {100, 100, 100, 100, 0, 0, 0, 0}, 24, 0};
const char *const PNL_CLES_INT[PNL_NB_INT] = {"toutes", "i590", "i630", "i660", "i810", "i830", "i850", "i940"};
void (*pnl_commande)(const char *quoi) = nullptr;
static const int PNL_NM[PNL_NB_INT] = {0, 590, 630, 660, 810, 830, 850, 940}; // 0 : toutes les LED
// couleur du point devant chaque longueur d'onde (teintes du panneau, comme les couleurs locales du Ballon)
static const uint32_t PNL_CNM[PNL_NB_INT] = {UI_ACCENT, 0xF0B429, 0xFF4B3E, 0xD22B2B, 0x9C2B2B, 0x8C2424, 0x7C1F1F, 0x6E1A1A};

// 07/11 (, alignements mesurés sur capture .193) : le grand nombre passe en police pixel 3x5, plus courte que
// le Montserrat 58 d'avant ; les trois y du haut sont resserrés pour que l'écart d'encre entre le nombre, sa légende,
// la pastille et la légende « LONGUEURS D'ONDE » soit le même partout (~20 px mesurés sur la capture après build).
#define GROS_Y 43       // le grand nombre (temps restant en séance, durée choisie sinon)
#define LECTURE_Y 100   // sa légende, centrée
#define PASTILLE_Y 133  // pastille d'état (séance en cours / LED réellement allumées)
#define LON_Y 180       // longueurs d'onde : légende + 3 boutons
#define CARTE_Y 254     // durée de la minuterie + radiateur / ventilateurs
#define CARTE_H 92
#define MODE_Y 354      // Minuterie / Illimité
#define ACTION_Y 412    // Démarrer / Arrêter

static const char *const ONGLETS[] = {TXT("Séance", "Session"), TXT("Intensités", "Levels")};

// Pointeurs valides seulement quand la page est affichée (remis à nullptr par page_panneau_led_quitter)
static lv_obj_t *g_zone = nullptr;
static int g_vue = 0; // 0 Séance, 1 Intensités
static lv_obj_t *o_onglets[2];

// Séance
static lv_obj_t *o_gros, *o_gros_u, *o_lecture, *o_puce_point, *o_puce_txt;
static lv_obj_t *o_longueurs[3], *o_modes[2];
static lv_obj_t *o_duree, *o_duree_v, *o_radiateur, *o_ventilos, *o_action, *o_action_txt;
// Intensités
static lv_obj_t *o_int_val[PNL_NB_INT], *o_int_sl[PNL_NB_INT];

static void envoyer(const char *quoi)
{
    if (pnl_commande)
        pnl_commande(quoi);
}

static void titre_section(lv_obj_t *z, lv_coord_t y, const char *txt)
{
    lv_obj_t *l = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE2, txt);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    // 07/11 (retour d'usage) : « + 4 » retiré. Mesuré sur la capture de référence : le cartouche commence à x 12-13, la
    // légende « LONGUEURS D'ONDE » à x 17 -> 5 px à droite. Aligné sur UI_MARGE, l'encre tombe à x 13, le bord du
    // cartouche.
    lv_obj_set_pos(l, UI_MARGE, y);
}

// =====================================================================  SÉANCE
static void rafraichir_seance()
{
    const pnl_etat_t &e = pnl_etat;

    // le grand nombre : minutes restantes pendant la séance ; sinon la durée choisie (ce que la séance durera)
    const bool reste = e.seance && e.restant != PNL_RESTANT_INCONNU;
    if (e.mode == PNL_ILLIMITE)
    {
        // 07/11 (retour d'usage) : « Illimité » écrit dans la même police pixel 3x5 que le nombre. Attention, relevé dans les
        // fontes : à 99 px — la taille exacte de la grande valeur du thermostat (k3x5_99_num) — cette police ne contient
        // AUCUNE lettre (plages générées : 32, 37, 44-46, 48-57, 176, soit espace, %, virgule, point, tiret, chiffres,
        // degré). La plus grande 3x5 qui ait les lettres, é compris, est k3x5_49_num (49 px, plages 32-126 et 160-255) :
        // c'est elle qui sert ici, pour le nombre comme pour le mot.
        ui_label_texte(o_gros, TXT("Illimité", "Unlimited"));
        ui_label_texte(o_gros_u, "");
        ui_label_texte(o_lecture, e.seance ? TXT("sans minuterie", "no timer") : TXT("séance sans minuterie", "session without timer"));
    }
    else
    {
        ui_label_texte_fmt(o_gros, "%d", LV_MAX(0, reste ? e.restant : e.duree));
        ui_label_texte(o_gros_u, "min");
        ui_label_texte(o_lecture, reste ? TXT("minutes restantes", "minutes left") : TXT("durée de la séance", "session length"));
    }
    ui_style_text_color(o_gros, ui_c(e.seance ? UI_TEXTE : UI_TEXTE2), 0);

    // pastille : ce que le panneau fait VRAIMENT (le switch et le capteur des LED peuvent diverger un instant)
    const char *etat;
    uint32_t point;
    if (e.seance && e.allumees)
        etat = TXT("Séance en cours  ·  LED allumées", "Session running  ·  LEDs on"), point = UI_VERT;
    else if (e.seance)
        etat = TXT("Séance en cours  ·  LED pas encore allumées", "Session running  ·  LEDs not on yet"), point = UI_ACCENT;
    else if (e.allumees)
        etat = TXT("LED allumées hors séance", "LEDs on, no session"), point = UI_ACCENT;
    else
        etat = TXT("Arrêt", "Off"), point = UI_TEXTE3;
    ui_label_texte(o_puce_txt, etat);
    ui_style_bg_color(o_puce_point, ui_c(point), 0);

    for (int k = 0; k < 3; k++)
        if (e.longueurs == k)
            lv_obj_add_state(o_longueurs[k], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_longueurs[k], LV_STATE_CHECKED);
    for (int k = 0; k < 2; k++)
        if (e.mode == k)
            lv_obj_add_state(o_modes[k], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_modes[k], LV_STATE_CHECKED);

    // durée : réglable seulement en minuterie (en illimité la poignée disparaît, comme le tirage du Poêle hors Manuel)
    const bool min = e.mode == PNL_MINUTERIE;
    if (lv_slider_get_value(o_duree) != e.duree)
        lv_slider_set_value(o_duree, e.duree, LV_ANIM_OFF);
    if (min)
        lv_obj_clear_state(o_duree, LV_STATE_DISABLED);
    else
        lv_obj_add_state(o_duree, LV_STATE_DISABLED);
    ui_label_texte_fmt(o_duree_v, "%d", e.duree);
    ui_style_text_color(o_duree_v, ui_c(min ? UI_ACCENT : UI_TEXTE3), 0);

    // radiateur : ambre quand les ventilateurs sont à fond, rouge à la coupure de sécurité
    ui_label_texte_fmt(o_radiateur, "%d °C", e.radiateur);
    ui_style_text_color(o_radiateur, ui_c(e.radiateur >= PNL_SECURITE ? UI_ROUGE
                                             : e.radiateur >= PNL_VENTI_MAX ? UI_CHAUFFE : UI_TEXTE), 0);
    ui_label_texte_fmt(o_ventilos, "%d %%", e.ventilos);
    ui_style_text_color(o_ventilos, ui_c(e.radiateur >= PNL_VENTI_MAX ? UI_CHAUFFE : UI_TEXTE), 0);

    // démarrer (ambre) / arrêter (bouton ordinaire), comme la séance des Ouvrants
    ui_label_texte(o_action_txt, e.seance ? LV_SYMBOL_STOP TXT("  Arrêter la séance", "  Stop session") : LV_SYMBOL_PLAY TXT("  Démarrer la séance", "  Start session"));
    ui_style_text_color(o_action_txt, ui_c(e.seance ? UI_TEXTE : UI_SUR_ACCENT), 0);
    if (e.seance)
        ui_style_retirer(o_action, &ui_st_bouton_principal, 0);
    else
        ui_style_ajouter(o_action, &ui_st_bouton_principal, 0);
}

static void seance_cb(lv_event_t *)
{
    pnl_etat.seance = !pnl_etat.seance; // la réalité (LED allumées, temps restant) sera confirmée par HA
    rafraichir_seance();
    envoyer("seance");
}

static void longueurs_cb(lv_event_t *e)
{
    pnl_etat.longueurs = (int)(intptr_t)lv_event_get_user_data(e);
    rafraichir_seance();
    envoyer("longueurs");
}

static void mode_cb(lv_event_t *e)
{
    pnl_etat.mode = (int)(intptr_t)lv_event_get_user_data(e);
    rafraichir_seance();
    envoyer("mode");
}

// glisser la durée : le chiffre suit le doigt, la valeur est envoyée au relâchement (comme le tirage du Poêle)
static void duree_cb(lv_event_t *e)
{
    const int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e)); // 9.6 : « lv_event_get_target » renvoie « void* » (8.4 : « lv_obj_t* ») ; cast neutre en 8.4
    if (v != pnl_etat.duree)
    {
        pnl_etat.duree = v;
        ui_label_texte_fmt(o_duree_v, "%d", v);
        if (!pnl_etat.seance) // le grand nombre montre la durée choisie : il suit le doigt aussi
            rafraichir_seance();
    }
    if (lv_event_get_code(e) == LV_EVENT_RELEASED)
        envoyer("duree");
}

static void construire_seance(lv_obj_t *z)
{
    // le temps restant, en grand, centré — 07/11 (retour d'usage) : police pixel 3x5 de 49 px, la même famille que la valeur
    // du thermostat et que « PRÊTE » de l'Imprimante (à 99 px, la taille du thermostat, cette fonte n'a pas de lettre)
    lv_obj_t *rang = ui_valeur_unite(z, UI_POLICE_TEMP_PETITE, UI_TEXTE, "min", &o_gros);
    o_gros_u = lv_obj_get_child(rang, -1);
    lv_obj_align(rang, LV_ALIGN_TOP_MID, 0, GROS_Y);
    o_lecture = ui_label(z, UI_POLICE_LEGENDE, UI_TEXTE3, "");
    lv_obj_set_width(o_lecture, UI_ZONE_L);
    lv_obj_set_style_text_align(o_lecture, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(o_lecture, 0, LECTURE_Y);

    // pastille d'état (même gabarit que celle du Poêle)
    lv_obj_t *puce = ui_conteneur(z);
    lv_obj_set_size(puce, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_radius(puce, 15, 0);
    ui_style_bg_color(puce, ui_c(UI_CARTE), 0);
    ui_style_bg_opa(puce, LV_OPA_COVER, 0);
    ui_style_border_color(puce, ui_c(UI_BORD), 0);
    ui_style_border_width(puce, 1, 0);
    lv_obj_set_style_pad_hor(puce, 14, 0);
    lv_obj_set_flex_flow(puce, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(puce, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(puce, 7, 0);
    o_puce_point = ui_conteneur(puce);
    lv_obj_set_size(o_puce_point, 8, 8);
    lv_obj_set_style_radius(o_puce_point, LV_RADIUS_CIRCLE, 0);
    ui_style_bg_opa(o_puce_point, LV_OPA_COVER, 0);
    o_puce_txt = ui_label(puce, UI_POLICE_PETITE, UI_TEXTE, "");
    lv_obj_align(puce, LV_ALIGN_TOP_MID, 0, PASTILLE_Y);

    // longueurs d'onde : ALL / RED-YELLOW / NIR de HA, sous leurs noms français et leurs plages de nm
    titre_section(z, LON_Y, TXT("LONGUEURS D'ONDE", "WAVELENGTHS"));
    lv_obj_t *rang3 = ui_conteneur(z);
    lv_obj_set_size(rang3, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(rang3, UI_MARGE, LON_Y + 16);
    lv_obj_set_flex_flow(rang3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang3, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *noms[] = {TXT("Toutes", "All"), TXT("Rouge-jaune", "Red-yellow"), TXT("Infrarouge", "Infrared")}, *sous[] = {"590-940 nm", "590-660 nm", "810-940 nm"};
    for (int k = 0; k < 3; k++)
    {
        lv_obj_t *b = ui_bouton(rang3, 121, 52);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label_texte(lv_label_create(b), noms[k]);
        lv_obj_t *s = lv_label_create(b);
        ui_label_texte(s, sous[k]);
        ui_style_text_font(s, UI_POLICE_LEGENDE, 0);
        o_longueurs[k] = b;
        lv_obj_add_event_cb(b, longueurs_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
    }

    // carte du milieu : durée de la minuterie en curseur, et en bas radiateur / ventilateurs
    lv_obj_t *c = ui_conteneur(z);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, CARTE_H);
    lv_obj_set_pos(c, UI_MARGE, CARTE_Y);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 10, 0);
    ui_label(c, UI_POLICE_PETITE, UI_TEXTE, TXT("Durée de la minuterie", "Timer length"));
    lv_obj_t *ru = ui_valeur_unite(c, UI_POLICE_3X5_M, UI_ACCENT, "min", &o_duree_v);
    const lv_coord_t ecart_base = (UI_POLICE_PETITE->line_height - UI_POLICE_PETITE->base_line) -
                                  (UI_POLICE_3X5_M->line_height - UI_POLICE_3X5_M->base_line);
    lv_obj_align(ru, LV_ALIGN_TOP_RIGHT, 0, ecart_base);
    o_duree = lv_slider_create(c);
    lv_obj_set_size(o_duree, lv_pct(100), 6);
    lv_obj_align(o_duree, LV_ALIGN_TOP_MID, 0, 28);
    lv_slider_set_range(o_duree, PNL_DUREE_MIN, PNL_DUREE_MAX);
    lv_slider_set_value(o_duree, pnl_etat.duree, LV_ANIM_OFF);
    ui_style_bg_color(o_duree, ui_c(UI_FOND), LV_PART_MAIN);
    ui_style_bg_opa(o_duree, LV_OPA_COVER, LV_PART_MAIN);
    ui_style_bg_color(o_duree, ui_c(0xF4F5F6), LV_PART_KNOB);
    lv_obj_set_style_pad_all(o_duree, 6, LV_PART_KNOB);
    ui_style_bg_opa(o_duree, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DISABLED); // pas de poignée en Illimité
    ui_style_bg_opa(o_duree, LV_OPA_COVER, LV_STATE_DISABLED);
    ui_style_bg_opa(o_duree, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(o_duree, 14);
    lv_obj_add_event_cb(o_duree, duree_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(o_duree, duree_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *bas = ui_conteneur(c); // la ligne du bas : radiateur à gauche, ventilateurs à droite
    lv_obj_set_size(bas, lv_pct(100), 22);
    lv_obj_align(bas, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_flex_flow(bas, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bas, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int k = 0; k < 2; k++)
    {
        lv_obj_t *g = ui_conteneur(bas);
        lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(g, 8, 0);
        if (k == 0)
        {
            ui_label(g, UI_POLICE_LEGENDE, UI_TEXTE3, TXT("radiateur", "radiator"));
            o_radiateur = ui_label(g, UI_POLICE_PETITE, UI_TEXTE, "");
        }
        else
        {
            ui_label(g, UI_POLICE_LEGENDE, UI_TEXTE3, TXT("ventilateurs", "fans"));
            o_ventilos = ui_label(g, UI_POLICE_PETITE, UI_TEXTE, "");
        }
    }

    // modes : deux boutons comme ceux du Poêle (titre + dessous explicatif)
    lv_obj_t *rm = ui_conteneur(z);
    lv_obj_set_size(rm, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(rm, UI_MARGE, MODE_Y);
    lv_obj_set_flex_flow(rm, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rm, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *mnoms[] = {TXT("Minuterie", "Timer"), TXT("Illimité", "Unlimited")}, *msous[] = {TXT("1 à 29 min", "1 to 29 min"), TXT("sans arrêt automatique", "no auto stop")};
    for (int k = 0; k < 2; k++)
    {
        lv_obj_t *b = ui_bouton(rm, 185, 52);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label_texte(lv_label_create(b), mnoms[k]);
        lv_obj_t *s = lv_label_create(b);
        ui_label_texte(s, msous[k]);
        ui_style_text_font(s, UI_POLICE_LEGENDE, 0);
        o_modes[k] = b;
        lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
    }

    // tout en bas : l'action principale (ambre quand on peut démarrer, ordinaire quand la séance tourne)
    o_action = ui_bouton(z, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(o_action, UI_MARGE, ACTION_Y);
    o_action_txt = ui_label(o_action, UI_POLICE_TEXTE, UI_SUR_ACCENT, "");
    lv_obj_center(o_action_txt);
    lv_obj_add_event_cb(o_action, seance_cb, LV_EVENT_CLICKED, NULL);
}

// =====================================================================  INTENSITÉS
// le glissé de « toutes les LED » entraîne les 7 longueurs d'onde avec lui, comme le fait le panneau ;
// l'écho de HA est gardé de côté pendant 3 s (ui_mqtt), rien n'écrase ce que l'utilisateur règle
static void appliquer_intensite(int k, int v)
{
    pnl_etat.intensites[k] = v;
    if (k == 0)
        for (int j = 1; j < PNL_NB_INT; j++)
            pnl_etat.intensites[j] = v;
    for (int j = 0; j < PNL_NB_INT; j++)
    {
        if (lv_slider_get_value(o_int_sl[j]) != pnl_etat.intensites[j])
            lv_slider_set_value(o_int_sl[j], pnl_etat.intensites[j], LV_ANIM_OFF);
        ui_label_texte_fmt(o_int_val[j], "%d %%", pnl_etat.intensites[j]);
    }
}

static void inten_cb(lv_event_t *e)
{
    const int k = (int)(intptr_t)lv_event_get_user_data(e);
    const int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e)); // 9.6 : « lv_event_get_target » renvoie « void* » (8.4 : « lv_obj_t* ») ; cast neutre en 8.4
    if (v != pnl_etat.intensites[k])
        appliquer_intensite(k, v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED)
        envoyer(PNL_CLES_INT[k]); // une seule commande au relâchement, pas à chaque pixel du doigt
}

static void rafraichir_intensites()
{
    for (int k = 0; k < PNL_NB_INT; k++)
    {
        if (lv_slider_get_value(o_int_sl[k]) != pnl_etat.intensites[k])
            lv_slider_set_value(o_int_sl[k], pnl_etat.intensites[k], LV_ANIM_OFF);
        ui_label_texte_fmt(o_int_val[k], "%d %%", pnl_etat.intensites[k]);
    }
}

static void construire_intensites(lv_obj_t *z)
{
    lv_obj_t *page = ui_conteneur(z); // liste défilante, comme les Réglages (8 curseurs, ça peut déborder en bas)
    lv_obj_set_size(page, UI_ZONE_L, UI_ZONE_H - 46);
    lv_obj_set_pos(page, 0, 46);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(page, UI_MARGE, 0);
    lv_obj_set_style_pad_top(page, 4, 0);
    lv_obj_set_style_pad_bottom(page, 16, 0);

    lv_obj_t *c = ui_conteneur(page);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_set_style_pad_ver(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);

    for (int k = 0; k < PNL_NB_INT; k++)
    {
        lv_obj_t *r = ui_conteneur(c); // libellé (point coloré + nom) à gauche, valeur ambre à droite
        lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *nom = ui_conteneur(r);
        lv_obj_set_size(nom, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(nom, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(nom, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(nom, 8, 0);
        lv_obj_t *point = ui_conteneur(nom); // le point de la couleur de la longueur d'onde
        lv_obj_set_size(point, 8, 8);
        lv_obj_set_style_radius(point, LV_RADIUS_CIRCLE, 0);
        ui_style_bg_color(point, ui_c(PNL_CNM[k]), 0);
        ui_style_bg_opa(point, LV_OPA_COVER, 0);
        if (k)
            ui_label_texte_fmt(lv_label_create(nom), "%d nm", PNL_NM[k]);
        else
            ui_label(nom, UI_POLICE_TEXTE, UI_TEXTE, TXT("Toutes les LED", "All LEDs"));
        ui_style_text_font(lv_obj_get_child(nom, 1), UI_POLICE_TEXTE, 0); // le « 590 nm » à la même police que « Toutes les LED »
        o_int_val[k] = ui_label(r, UI_POLICE_TEXTE, UI_ACCENT, "");

        lv_obj_t *s = lv_slider_create(c); // le curseur occupe la rangée entière, dessous
        o_int_sl[k] = s;
        lv_obj_set_size(s, lv_pct(100), 8);
        lv_obj_set_style_pad_ver(s, 2, 0);
        lv_slider_set_range(s, 0, 100);
        lv_slider_set_value(s, pnl_etat.intensites[k], LV_ANIM_OFF);
        ui_style_bg_color(s, ui_c(0x3A424D), LV_PART_MAIN);
        ui_style_bg_color(s, ui_c(UI_ACCENT), LV_PART_INDICATOR);
        ui_style_bg_color(s, ui_c(UI_ACCENT), LV_PART_KNOB);
        lv_obj_set_style_pad_all(s, 7, LV_PART_KNOB);
        lv_obj_set_ext_click_area(s, 16);
        ui_curseur_dans_page(s); // avant inten_cb : un glissé vertical fait défiler la page, un glissé horizontal règle
        lv_obj_add_event_cb(s, inten_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)k);
        lv_obj_add_event_cb(s, inten_cb, LV_EVENT_RELEASED, (void *)(intptr_t)k);
    }
}

// =====================================================================  PAGE
static void rafraichir()
{
    if (!g_zone)
        return;
    if (g_vue == 0)
        rafraichir_seance();
    else
        rafraichir_intensites();
}

void page_panneau_led_maj() { rafraichir(); }

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
    lv_obj_clean(g_zone);
    ui_onglets(g_zone, ONGLETS, 2, g_vue, onglet_cb, nullptr, o_onglets);
    if (g_vue == 0)
        construire_seance(g_zone);
    else
        construire_intensites(g_zone);
    rafraichir();
}

void page_panneau_led_creer(lv_obj_t *zone)
{
    g_zone = zone;
    g_vue = 0;
    construire();
}

void page_panneau_led_quitter()
{
    g_zone = nullptr; // rafraichir() ne touche plus à rien
}
