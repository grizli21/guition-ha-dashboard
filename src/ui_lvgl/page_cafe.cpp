#include "page_cafe.h"
#include "ui_theme.h"
#include <stdio.h>

cf_programme_t cf_programmes[2] = {{6 * 60 + 45, false}, {7 * 60, false}};
int cf_recentes[2] = {6 * 60 + 45, 7 * 60 + 15};
int cf_temp = 2;
bool cf_bouilloire_chauffe = false;
int cf_maintenant = 0;
void (*cf_commande)(int appareil, cf_action_t action) = nullptr;
const int CF_TEMPERATURES[5] = {50, 70, 80, 90, 100};

static int g_app = CF_CAFE;

// Pointeurs valides seulement quand la page est affichée (remis à nullptr par page_cafe_quitter)
static lv_obj_t *o_onglets[2], *o_titre, *o_heure, *o_restant, *o_rang, *o_puces[5], *o_puces_txt[5];
static lv_obj_t *o_prog, *o_prog_txt, *o_direct, *o_direct_txt;

static void envoyer(cf_action_t a)
{
    if (cf_commande)
        cf_commande(g_app, a);
}

static int dans_15_min() { return ((cf_maintenant + 15 + 4) / 5 * 5) % 1440; }

static void hhmm(char *b, size_t n, int m) { snprintf(b, n, "%02d:%02d", m / 60, m % 60); }

static void rafraichir()
{
    if (!o_titre)
        return;
    const bool bouil = g_app == CF_BOUILLOIRE;
    const cf_programme_t &p = cf_programmes[g_app];
    char h[8];

    for (int i = 0; i < 2; i++)
        if (i == g_app)
            lv_obj_add_state(o_onglets[i], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_onglets[i], LV_STATE_CHECKED);

    // prochaine occurrence de l'heure choisie
    const bool demain = p.minute <= cf_maintenant;
    const int delta = (demain ? 1440 : 0) + p.minute - cf_maintenant;
    const char *jour = demain ? "demain" : "aujourd'hui";
    if (p.actif)
    {
        if (bouil)
            ui_label_texte_fmt(o_titre, "Eau à %d °C %s à", CF_TEMPERATURES[cf_temp], jour);
        else
            ui_label_texte_fmt(o_titre, "Café prêt %s à", jour);
        ui_label_texte_fmt(o_restant, "dans %d h %02d", delta / 60, delta % 60);
    }
    else
    {
        ui_label_texte_fmt(o_titre, bouil ? "Programmer la bouilloire %s à" : "Programmer un café %s à", jour);
        ui_label_texte(o_restant, "Non programmé");
    }
    hhmm(h, sizeof h, p.minute);
    ui_label_texte(o_heure, h);
    ui_style_text_color(o_heure, ui_c(p.actif ? UI_ACCENT : UI_TEXTE), 0);

    // café : 3 raccourcis d'heure ; bouilloire : 5 températures
    const int nb = bouil ? 5 : 3;
    const lv_coord_t l = (UI_ZONE_L - 2 * UI_MARGE - (nb - 1) * 8) / nb;
    for (int i = 0; i < 5; i++)
    {
        if (i >= nb)
        {
            ui_cacher(o_puces[i], true);
            continue;
        }
        ui_cacher(o_puces[i], false);
        lv_obj_set_width(o_puces[i], l);
        bool choisie;
        if (bouil)
        {
            ui_label_texte_fmt(o_puces_txt[i], "%d°", CF_TEMPERATURES[i]);
            choisie = i == cf_temp;
        }
        else
        {
            int m = i == 0 ? dans_15_min() : cf_recentes[i - 1];
            if (i == 0)
                ui_label_texte(o_puces_txt[i], "Dans 15 min");
            else if (m < 0)
                ui_label_texte(o_puces_txt[i], "–");
            else
            {
                hhmm(h, sizeof h, m);
                ui_label_texte(o_puces_txt[i], h);
            }
            choisie = m == p.minute;
        }
        // pastille choisie : fond un cran plus clair et bord clair (pas l'ambre, réservé à l'action)
        ui_style_bg_color(o_puces[i], ui_c(choisie ? UI_CARTE2 : UI_FOND), 0);
        ui_style_border_color(o_puces[i], ui_c(choisie ? UI_TEXTE2 : UI_BORD), 0);
        ui_style_text_color(o_puces_txt[i], ui_c(choisie ? UI_TEXTE : UI_TEXTE2), 0);
    }

    ui_label_texte(o_prog_txt, p.actif ? "Annuler la programmation" : "Programmer");
    if (p.actif)
        ui_style_retirer(o_prog, &ui_st_bouton_principal, 0);
    else
        ui_style_ajouter(o_prog, &ui_st_bouton_principal, 0);
    ui_style_text_color(o_prog_txt, ui_c(p.actif ? UI_TEXTE : UI_SUR_ACCENT), 0);

    const bool chauffe = bouil && cf_bouilloire_chauffe;
    if (!bouil)
        ui_label_texte(o_direct_txt, "Lancer un café maintenant");
    else if (chauffe)
        ui_label_texte_fmt(o_direct_txt, "Arrêter (en chauffe, %d °C)", CF_TEMPERATURES[cf_temp]);
    else
        ui_label_texte_fmt(o_direct_txt, "Chauffer à %d °C maintenant", CF_TEMPERATURES[cf_temp]);
    ui_style_bg_color(o_direct, ui_c(chauffe ? 0x2A1A12 : UI_CARTE), 0);
    ui_style_border_color(o_direct, ui_c(chauffe ? UI_CHAUFFE : UI_BORD), 0);
    ui_style_text_color(o_direct_txt, ui_c(chauffe ? UI_CHAUFFE : UI_TEXTE), 0);
}

void page_cafe_maj() { rafraichir(); }

static void onglet_cb(lv_event_t *e)
{
    g_app = (int)(intptr_t)lv_event_get_user_data(e);
    rafraichir();
}

// 24/10 (retour d'usage : « programmer un café par incrément de 5 minutes ») : − / + s'accrochent aux multiples de 5 (6:43 -> 6:45
// ou 6:40, puis de 5 en 5 ; avant, 6:43 -> 6:48) ; appui long = répétition « expo avec un plafond » (retour d'usage) : le pas
// double toutes les 4 répétitions (~0,4 s), 5 -> 10 -> 20, plafonné à 30 min ; envoyée une fois au lâcher
#define PAS_PLAFOND 30
static void decaler_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    const int d = (int)(intptr_t)lv_event_get_user_data(e); // -5 ou +5
    cf_programme_t &p = cf_programmes[g_app];
    static bool repete = false;
    static int n = 0; // répétitions de cet appui long
    if (code == LV_EVENT_PRESSED)
        n = 0;
    if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT)
    {
        const int pas = LV_MIN(5 << (n++ / 4), PAS_PLAFOND);
        int m;
        if (p.minute % 5) // pas encore sur la grille de 5 : on s'y accroche d'abord
            m = d > 0 ? (p.minute / 5 + 1) * 5 : p.minute / 5 * 5;
        else
            m = p.minute + (d > 0 ? pas : -pas);
        p.minute = (m + 1440) % 1440;
        rafraichir();
        if (code != LV_EVENT_SHORT_CLICKED)
        {
            repete = true; // envoyé au lâcher
            return;
        }
    }
    else if (code == LV_EVENT_RELEASED && repete)
        repete = false;
    else
        return;
    if (p.actif)
        envoyer(CF_PROGRAMMER); // une programmation active suit l'heure
}

static void puce_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    cf_programme_t &p = cf_programmes[g_app];
    if (g_app == CF_BOUILLOIRE)
        cf_temp = i;
    else
    {
        int m = i == 0 ? dans_15_min() : cf_recentes[i - 1];
        if (m < 0)
            return;
        p.minute = m;
    }
    rafraichir();
    if (p.actif)
        envoyer(CF_PROGRAMMER);
}

static void prog_cb(lv_event_t *)
{
    cf_programme_t &p = cf_programmes[g_app];
    p.actif = !p.actif;
    if (p.actif && g_app == CF_CAFE && p.minute != cf_recentes[0])
    {
        cf_recentes[1] = cf_recentes[0]; // la nouvelle heure devient la plus récente
        cf_recentes[0] = p.minute;
    }
    rafraichir();
    envoyer(CF_PROGRAMMER);
}

static void direct_cb(lv_event_t *)
{
    envoyer(g_app == CF_BOUILLOIRE && cf_bouilloire_chauffe ? CF_ARRETER : CF_DIRECT);
}

static lv_obj_t *bouton_carre(lv_obj_t *parent, const char *txt, int pas)
{
    lv_obj_t *b = ui_bouton(parent, 60, 60);
    ui_style_bg_color(b, ui_c(UI_CARTE2), 0);
    lv_obj_t *t = ui_label(b, UI_POLICE_VALEUR, UI_TEXTE, txt);
    lv_obj_center(t);
    lv_obj_add_event_cb(b, decaler_cb, LV_EVENT_ALL, (void *)(intptr_t)pas);
    return b;
}

void page_cafe_creer(lv_obj_t *zone)
{
    // sélecteur Café | Bouilloire (24/10 : segment choisi en gris clair comme les autres onglets ; l'ambre reste aux actions)
    static const char *const ONGLETS[] = {"Café", "Bouilloire"};
    ui_onglets(zone, ONGLETS, 2, -1, onglet_cb, nullptr, o_onglets);

    // carte de l'heure : - HH:MM +
    lv_obj_t *c = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 196);
    lv_obj_set_pos(c, UI_MARGE, 52);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 10, 0);
    o_titre = ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, "");
    lv_obj_t *r = ui_conteneur(c);
    lv_obj_set_size(r, LV_SIZE_CONTENT, 64);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 18, 0);
    bouton_carre(r, "−", -5);
    o_heure = ui_label(r, UI_POLICE_TEMP_PETITE, UI_TEXTE, "");
    bouton_carre(r, "+", 5);
    o_restant = ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, "");

    // raccourcis / températures
    o_rang = ui_conteneur(zone);
    lv_obj_set_size(o_rang, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(o_rang, UI_MARGE, 260);
    lv_obj_set_flex_flow(o_rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(o_rang, 8, 0);
    for (int i = 0; i < 5; i++)
    {
        o_puces[i] = ui_bouton(o_rang, 60, 52);
        o_puces_txt[i] = ui_label(o_puces[i], UI_POLICE_TEXTE, UI_TEXTE2, "");
        lv_obj_center(o_puces_txt[i]);
        lv_obj_add_event_cb(o_puces[i], puce_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    // 24/10 (harmonie) : boutons d'action à la norme commune (52 px, texte moyen, rayon du thème), en bas comme ailleurs
    o_prog = ui_bouton(zone, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(o_prog, UI_MARGE, 352);
    o_prog_txt = ui_label(o_prog, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_obj_center(o_prog_txt);
    lv_obj_add_event_cb(o_prog, prog_cb, LV_EVENT_CLICKED, NULL);

    o_direct = ui_bouton(zone, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(o_direct, UI_MARGE, 412);
    o_direct_txt = ui_label(o_direct, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_obj_center(o_direct_txt);
    lv_obj_add_event_cb(o_direct, direct_cb, LV_EVENT_CLICKED, NULL);

    rafraichir();
}

void page_cafe_quitter()
{
    o_titre = nullptr; // rafraichir() ne touche plus à rien
}
