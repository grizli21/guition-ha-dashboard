#include "page_reglages.h"
#include "ui_theme.h"
#include "page_thermostat.h" // noms des pièces
#include <stdio.h>
#include <string.h>

// luminosités par défaut = celles de la production (jour 75 / 50, nuit 40 / 5)
rg_reglages_t rg_reglages = {75, 40, 1, 0, 1, true, true, true, "", {}, 0, 50, "", "", 0, 50, 5};
void (*rg_commande)(const char *quoi) = nullptr;
void (*rg_apercu)(int pct, bool fin) = nullptr;

static const char *VEILLES[] = {TXT("30 s", "30 s"), TXT("1 min", "1 min"), TXT("5 min", "5 min"), TXT("Jamais", "Never")};
static const char *RETOURS[] = {TXT("après 30 s", "after 30 s"), TXT("après 60 s", "after 60 s"), TXT("après 2 min", "after 2 min"), TXT("jamais", "never")};

// Pointeurs valides seulement quand la page est affichée (remis à nullptr par page_reglages_quitter)
static lv_obj_t *o_page, *o_lum_jour_v, *o_lum_nuit_v, *o_lum_jour_r_v, *o_lum_nuit_r_v, *o_lum_s[4], *o_volume_v, *o_veilles[4], *o_piece_v, *o_retour_v;
static lv_obj_t *o_cam, *o_ecl_porte, *o_ecl_mouv, *o_melodie_v, *o_ssid, *o_ip, *o_barres[4];


static void signaler(const char *quoi)
{
    if (rg_commande)
        rg_commande(quoi);
}

static void inter_etat(lv_obj_t *sw, bool on)
{
    if (on)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

static void rafraichir()
{
    if (!o_page)
        return;
    const rg_reglages_t &r = rg_reglages;
    ui_label_texte_fmt(o_lum_jour_v, "%d %%", r.lum_jour);
    ui_label_texte_fmt(o_lum_jour_r_v, "%d %%", r.lum_jour_repos);
    ui_label_texte_fmt(o_lum_nuit_v, "%d %%", r.lum_nuit);
    ui_label_texte_fmt(o_lum_nuit_r_v, "%d %%", r.lum_nuit_repos);
    ui_label_texte_fmt(o_volume_v, "%d %%", r.volume);
    for (int i = 0; i < 4; i++)
        if (i == r.veille)
            lv_obj_add_state(o_veilles[i], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_veilles[i], LV_STATE_CHECKED);
    ui_label_texte_fmt(o_piece_v, "%s  " LV_SYMBOL_RIGHT, th_pieces[r.piece].nom);
    ui_label_texte_fmt(o_retour_v, "%s  " LV_SYMBOL_RIGHT, RETOURS[r.retour]);
    ui_label_texte_fmt(o_melodie_v, "%s  " LV_SYMBOL_RIGHT, r.melodie[0] ? r.melodie : "–");
    inter_etat(o_cam, r.camera);
    inter_etat(o_ecl_porte, r.terrasse_porte);
    inter_etat(o_ecl_mouv, r.terrasse_mouvement);

    bool connecte = r.ssid[0] != 0;
    ui_label_texte(o_ssid, connecte ? r.ssid : TXT("Wi-Fi déconnecté", "Wi-Fi disconnected"));
    if (connecte)
        ui_label_texte_fmt(o_ip, "%s · %d dBm", r.ip, r.rssi);
    else
        ui_label_texte(o_ip, "");
    int nb = !connecte ? 0 : r.rssi > -55 ? 4 : r.rssi > -65 ? 3 : r.rssi > -75 ? 2 : 1;
    for (int i = 0; i < 4; i++)
        ui_style_bg_color(o_barres[i], ui_c(i < nb ? UI_VERT : UI_ETEINT), 0);
}

void page_reglages_maj() { rafraichir(); }

// --- Fabriques de lignes ---
static lv_obj_t *carte(lv_obj_t *page, lv_coord_t pad_ver)
{
    lv_obj_t *c = ui_conteneur(page);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_set_style_pad_ver(c, pad_ver, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);
    return c;
}

// libellé à gauche, valeur à droite
static lv_obj_t *rangee(lv_obj_t *parent, lv_coord_t h)
{
    lv_obj_t *r = ui_conteneur(parent);
    lv_obj_set_size(r, lv_pct(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
}

// Luminosités (24/10, 4 curseurs simples plutôt qu'un curseur à 2 poignées : sur 4 pouces, 2 poignées proches se gênent) :
// en service >= au repos, l'autre suit ; pas de 5 comme la production ; aperçu sur l'écran pendant qu'on tire
static int *const LUMS[4] = {&rg_reglages.lum_jour, &rg_reglages.lum_jour_repos, &rg_reglages.lum_nuit, &rg_reglages.lum_nuit_repos};
static const char *const LUM_NOMS[4] = {"lum_jour", "lum_jour_repos", "lum_nuit", "lum_nuit_repos"};
#define APERCU_MIN 15 // pendant qu'on tire : jamais plus sombre, sinon on ne voit plus ce qu'on règle

static void slider_cb(lv_event_t *e)
{
    int *valeur = (int *)lv_event_get_user_data(e);
    lv_obj_t *s = (lv_obj_t *)lv_event_get_target(e); // 9.6 : « lv_event_get_target » renvoie « void* » (8.4 : « lv_obj_t* ») ; cast neutre en 8.4
    int k = 0;
    while (k < 4 && LUMS[k] != valeur)
        k++;
    *valeur = k < 4 ? (lv_slider_get_value(s) + 2) / 5 * 5 : lv_slider_get_value(s);
    if (k < 4)
    {
        const int autre = k ^ 1; // le couple en service / au repos de la même période
        const bool repos = k & 1;
        if (repos ? *valeur > *LUMS[autre] : *valeur < *LUMS[autre])
        {
            *LUMS[autre] = *valeur;
            lv_slider_set_value(o_lum_s[autre], *valeur, LV_ANIM_OFF);
        }
    }
    rafraichir();
    const bool fin = lv_event_get_code(e) == LV_EVENT_RELEASED;
    if (k < 4 && rg_apercu)
        rg_apercu(fin ? *valeur : LV_MAX(*valeur, APERCU_MIN), fin);
    if (fin)
    {
        if (k < 4)
        {
            lv_slider_set_value(s, *valeur, LV_ANIM_OFF); // arrondi au pas de 5
            signaler(LUM_NOMS[k]); // un seul signal : tous les réglages de l'écran sont gardés et publiés ensemble
        }
        else
            signaler("volume");
    }
}

// curseur avec son libellé et sa valeur en ambre au-dessus
static lv_obj_t *curseur(lv_obj_t *c, const char *txt, int *valeur, int mini, lv_obj_t **o_valeur)
{
    lv_obj_t *r = rangee(c, LV_SIZE_CONTENT);
    ui_label(r, UI_POLICE_TEXTE, UI_TEXTE, txt);
    *o_valeur = ui_label(r, UI_POLICE_TEXTE, UI_ACCENT, "");
    lv_obj_t *s = lv_slider_create(c);
    lv_obj_set_size(s, lv_pct(100), 8);
    lv_obj_set_style_pad_ver(s, 0, 0);
    lv_slider_set_range(s, mini, 100);
    lv_slider_set_value(s, *valeur, LV_ANIM_OFF);
    ui_style_bg_color(s, ui_c(UI_ETEINT), LV_PART_MAIN);
    ui_style_bg_color(s, ui_c(UI_ACCENT), LV_PART_INDICATOR);
    ui_style_bg_color(s, ui_c(UI_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 7, LV_PART_KNOB);
    lv_obj_set_ext_click_area(s, 16); // facile à attraper au doigt malgré ses 8 px
    ui_curseur_dans_page(s);         // avant slider_cb : un glissé vertical fait défiler la page
    lv_obj_add_event_cb(s, slider_cb, LV_EVENT_VALUE_CHANGED, valeur);
    lv_obj_add_event_cb(s, slider_cb, LV_EVENT_RELEASED, valeur);
    return s;
}

static void veille_cb(lv_event_t *e)
{
    rg_reglages.veille = (int)(intptr_t)lv_event_get_user_data(e);
    rafraichir();
    signaler("veille");
}

// ligne « libellé ... valeur › » : un tap passe à la valeur suivante
static void choix_cb(lv_event_t *e)
{
    const char *quoi = (const char *)lv_event_get_user_data(e);
    rg_reglages_t &r = rg_reglages;
    if (!strcmp(quoi, "piece"))
        r.piece = (r.piece + 1) % TH_NB_PIECES;
    else if (!strcmp(quoi, "retour"))
        r.retour = (r.retour + 1) % 4;
    else if (!strcmp(quoi, "melodie")) // la suivante dans la liste de HA
    {
        if (!r.nb_melodies)
            return;
        int i = 0;
        while (i < r.nb_melodies && strcmp(r.melodies[i], r.melodie))
            i++;
        snprintf(r.melodie, sizeof r.melodie, "%s", r.melodies[(i + 1) % r.nb_melodies]);
    }
    rafraichir();
    signaler(quoi);
}

static lv_obj_t *ligne_choix(lv_obj_t *c, const char *txt, const char *quoi, bool trait)
{
    lv_obj_t *b = lv_btn_create(c);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, lv_pct(100), 50);
    ui_style_bg_color(b, ui_c(UI_CARTE2), LV_STATE_PRESSED);
    ui_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    if (trait)
    {
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        ui_style_border_width(b, 1, 0);
        ui_style_border_color(b, ui_c(UI_BORD), 0);
    }
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(b, UI_POLICE_TEXTE, UI_TEXTE, txt);
    lv_obj_t *v = ui_label(b, UI_POLICE_TEXTE, UI_TEXTE2, "");
    lv_obj_add_event_cb(b, choix_cb, LV_EVENT_CLICKED, (void *)quoi);
    return v;
}

static void inter_cb(lv_event_t *e)
{
    bool *valeur = (bool *)lv_event_get_user_data(e);
    *valeur = !*valeur;
    rafraichir();
    signaler(valeur == &rg_reglages.camera ? "camera" : valeur == &rg_reglages.terrasse_porte ? "terrasse_porte" : "terrasse_mouvement");
}

// ligne « titre + explication ... interrupteur » : toute la ligne est le bouton
static lv_obj_t *ligne_inter(lv_obj_t *c, const char *txt, const char *detail, bool *valeur)
{
    lv_obj_t *b = lv_btn_create(c);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, lv_pct(100), 56);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
    ui_style_border_width(b, 1, 0);
    ui_style_border_color(b, ui_c(UI_BORD), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *col = ui_conteneur(b);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    ui_label(col, UI_POLICE_TEXTE, UI_TEXTE, txt);
    ui_label(col, UI_POLICE_LEGENDE, UI_TEXTE2, detail);
    lv_obj_t *sw = lv_switch_create(b);
    lv_obj_set_size(sw, 52, 30);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    ui_interrupteur_couleurs(sw);
    lv_obj_add_event_cb(b, inter_cb, LV_EVENT_CLICKED, valeur);
    return sw;
}

// --- Construction ---
void page_reglages_creer(lv_obj_t *zone)
{
    lv_obj_t *titre = ui_label(zone, UI_POLICE_VALEUR, UI_TEXTE, TXT("Réglages", "Settings"));
    lv_obj_align(titre, LV_ALIGN_TOP_MID, 0, 2);

    o_page = ui_conteneur_defilant(zone, 0, 46, UI_ZONE_L, UI_ZONE_H - 46);
    lv_obj_set_flex_flow(o_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(o_page, UI_MARGE, 0);
    lv_obj_set_style_pad_top(o_page, 4, 0);
    lv_obj_set_style_pad_bottom(o_page, 16, 0);
    lv_obj_set_style_pad_row(o_page, 10, 0);

    // ÉCRAN
    ui_titre_section(o_page, TXT("ÉCRAN", "SCREEN"));
    lv_obj_t *c = carte(o_page, 12);
    ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, TXT("LE JOUR", "DAY"));
    o_lum_s[0] = curseur(c, TXT("En service", "Active"), &rg_reglages.lum_jour, 5, &o_lum_jour_v);
    o_lum_s[1] = curseur(c, TXT("Au repos, après la veille", "Idle, after sleep"), &rg_reglages.lum_jour_repos, 0, &o_lum_jour_r_v);
    ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, TXT("LA NUIT", "NIGHT"));
    o_lum_s[2] = curseur(c, TXT("En service", "Active"), &rg_reglages.lum_nuit, 5, &o_lum_nuit_v);
    o_lum_s[3] = curseur(c, TXT("Au repos, après la veille", "Idle, after sleep"), &rg_reglages.lum_nuit_repos, 0, &o_lum_nuit_r_v);
    lv_obj_t *r = rangee(c, 40);
    ui_label(r, UI_POLICE_TEXTE, UI_TEXTE, TXT("Veille après", "Sleep after"));
    lv_obj_t *puces = ui_conteneur(r);
    lv_obj_set_size(puces, LV_SIZE_CONTENT, 40);
    lv_obj_set_flex_flow(puces, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(puces, 5, 0);
    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *b = ui_bouton(puces, i == 3 ? 62 : 52, 40);
        lv_obj_set_style_radius(b, 12, 0);
        ui_style_bg_color(b, ui_c(UI_FOND), 0);
        o_veilles[i] = b;
        lv_obj_t *l = ui_label(b, UI_POLICE_LEGENDE, UI_TEXTE, VEILLES[i]);
        ui_style_text_color(l, ui_c(UI_SUR_ACCENT), LV_STATE_CHECKED);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, veille_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    // ACCUEIL
    ui_titre_section(o_page, TXT("ACCUEIL", "HOME"));
    c = carte(o_page, 0);
    lv_obj_set_style_pad_row(c, 0, 0);
    o_piece_v = ligne_choix(c, TXT("Pièce de cet écran", "This screen's room"), "piece", true);
    o_retour_v = ligne_choix(c, TXT("Retour à l'accueil", "Return home"), "retour", false);

    // MAISON
    ui_titre_section(o_page, TXT("MAISON", "HOUSE"));
    c = carte(o_page, 0);
    lv_obj_set_style_pad_row(c, 0, 0);
    o_cam = ligne_inter(c, TXT("Caméra à la sonnette", "Doorbell camera"), TXT("Affiche l'entrée quand on sonne", "Shows the entrance when ringing"), &rg_reglages.camera);
    o_ecl_porte = ligne_inter(c, TXT("Terrasse : porte", "Patio door"), TXT("Allume à l'ouverture de la porte, la nuit", "Turns on when the door opens, at night"), &rg_reglages.terrasse_porte);
    o_ecl_mouv = ligne_inter(c, TXT("Terrasse : mouvement", "Patio motion"), TXT("Allume sur mouvement, la nuit", "Turns on on motion, at night"), &rg_reglages.terrasse_mouvement);
    o_melodie_v = ligne_choix(c, TXT("Sonnerie du carillon", "Chime melody"), "melodie", true);
    lv_obj_t *vol = ui_conteneur(c);
    lv_obj_set_size(vol, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(vol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(vol, 10, 0);
    lv_obj_set_style_pad_ver(vol, 12, 0);
    curseur(vol, TXT("Volume du carillon", "Chime volume"), &rg_reglages.volume, 0, &o_volume_v);

    // RÉSEAU
    ui_titre_section(o_page, TXT("RÉSEAU", "NETWORK"));
    c = carte(o_page, 12);
    r = rangee(c, LV_SIZE_CONTENT);
    lv_obj_t *col = ui_conteneur(r);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    o_ssid = ui_label(col, UI_POLICE_TEXTE, UI_TEXTE, "");
    o_ip = ui_label(col, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_t *barres = ui_conteneur(r);
    lv_obj_set_size(barres, LV_SIZE_CONTENT, 20);
    lv_obj_set_flex_flow(barres, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(barres, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(barres, 3, 0);
    for (int i = 0; i < 4; i++)
    {
        o_barres[i] = ui_conteneur(barres);
        lv_obj_set_size(o_barres[i], 5, 6 + i * 5);
        lv_obj_set_style_radius(o_barres[i], 2, 0);
        ui_style_bg_opa(o_barres[i], LV_OPA_COVER, 0);
    }

    rafraichir();
}

void page_reglages_quitter()
{
    o_page = nullptr; // rafraichir() ne touche plus à rien
}
