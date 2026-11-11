#include "page_infos.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "ui_theme.h"
#include "ui_diag.h" // 06/11 (audit 9.6) : ui_trace sur échec d'allocation
#include <string.h>
#include <stdio.h>

if_valeur_t if_systeme[IF_NB_SYSTEME] = {
    // 25/10 (retour d'usage : « le libellé PSRAM déborde sur Processeur, ajoute une ligne ») : mémoire en deux cases
    {TXT("Allumé depuis", "On since"), ""}, {TXT("Dernier redémarrage", "Last restart"), ""},
    {TXT("RAM libre", "Free RAM"), ""}, {TXT("PSRAM libre", "Free PSRAM"), ""},
    {TXT("Processeur", "Processor"), ""}, {TXT("Fréquence", "Clock"), ""}, {TXT("Boucle la plus lente", "Slowest loop"), ""},
    {TXT("Émission Wi-Fi", "Wi-Fi TX"), ""}, {TXT("Version", "Version"), ""}, {TXT("Adresse IP", "IP address"), ""},
};
if_liaison_t if_liaisons[IF_NB_LIAISONS] = {
    {"Wi-Fi", "", false},
    {"Home Assistant (MQTT)", "", false},
};
UI_PSRAM(if_appareil_t, if_appareils, IF_NB_APPAREILS, nullptr); // 29/10 : en PSRAM (ui_psram.h)
int if_nb_appareils = 0;
void (*if_action)(if_action_t a) = nullptr;

static UI_PSRAM(if_ligne_t, g_journal, IF_NB_JOURNAL, nullptr); // 29/10 : en PSRAM (ui_psram.h)
static int g_nb_journal = 0;

// Pointeurs valides seulement quand la page est affichée (remis à nullptr par page_infos_quitter)
static lv_obj_t *o_page, *o_sys[IF_NB_SYSTEME], *o_lien_point[IF_NB_LIAISONS], *o_lien_detail[IF_NB_LIAISONS];
static lv_obj_t *o_journal, *o_appareils, *o_raz, *o_raz_txt;
static bool g_confirme_raz = false;
static bool g_ligne_echec_signale = false; // 06/11 (audit 9.6) : l'échec d'allocation ne se dit qu'une fois

void if_journal_ajouter(const char *heure, const char *texte, bool alerte)
{
    memmove(&g_journal[1], &g_journal[0], sizeof(if_ligne_t) * (IF_NB_JOURNAL - 1));
    snprintf(g_journal[0].heure, sizeof g_journal[0].heure, "%s", heure);
    snprintf(g_journal[0].texte, sizeof g_journal[0].texte, "%s", texte);
    g_journal[0].alerte = alerte;
    if (g_nb_journal < IF_NB_JOURNAL)
        g_nb_journal++;
}

void if_journal_effacer() { g_nb_journal = 0; }

static void rafraichir()
{
    if (!o_page)
        return;
    for (int i = 0; i < IF_NB_SYSTEME; i++)
        ui_label_texte(o_sys[i], if_systeme[i].valeur[0] ? if_systeme[i].valeur : "–");
    for (int i = 0; i < IF_NB_LIAISONS; i++)
    {
        ui_style_bg_color(o_lien_point[i], ui_c(if_liaisons[i].ok ? UI_VERT : UI_ROUGE), 0);
        ui_label_texte(o_lien_detail[i], if_liaisons[i].detail);
    }

    // appareils : les hors ligne un par un (point rouge, pièce à droite), les autres en une ligne de noms par pièce
    lv_obj_clean(o_appareils);
    int hs = 0;
    for (int i = 0; i < if_nb_appareils; i++)
        hs += !if_appareils[i].ok;
    lv_obj_t *t = ui_label(o_appareils, UI_POLICE_PETITE, hs ? UI_ACCENT : UI_TEXTE2, "");
    if (!if_nb_appareils)
        ui_label_texte(t, TXT("en attente de Home Assistant…", "waiting for Home Assistant…"));
    else if (hs)
        ui_label_texte_fmt(t, TXT("%d appareils · %d hors ligne", "%d devices · %d offline"), if_nb_appareils, hs);
    else
        ui_label_texte_fmt(t, TXT("%d appareils · tous en ligne", "%d devices · all online"), if_nb_appareils);
    for (int i = 0; i < if_nb_appareils; i++)
    {
        const if_appareil_t &a = if_appareils[i];
        if (a.ok)
            continue;
        lv_obj_t *r = ui_conteneur(o_appareils);
        lv_obj_set_size(r, lv_pct(100), 30);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(r, 10, 0);
        lv_obj_t *p = ui_conteneur(r);
        lv_obj_set_size(p, 10, 10);
        lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
        ui_style_bg_opa(p, LV_OPA_COVER, 0);
        ui_style_bg_color(p, ui_c(UI_ROUGE), 0);
        lv_obj_set_flex_grow(ui_label(r, UI_POLICE_TEXTE, UI_TEXTE, a.nom), 1);
        ui_label(r, UI_POLICE_LEGENDE, UI_TEXTE3, a.piece[0] ? a.piece : TXT("sans pièce", "no room"));
    }
    for (int i = 0; i < if_nb_appareils; i++) // en ligne, par pièce (HA les donne triés par pièce)
    {
        const if_appareil_t &a = if_appareils[i];
        if (!a.ok)
            continue;
        bool deja = false;
        for (int k = 0; k < i && !deja; k++)
            deja = if_appareils[k].ok && !strcmp(if_appareils[k].piece, a.piece);
        if (deja)
            continue;
#define LIGNE 640
        // 06/11 (audit 9.6, chantier 3) : le calloc n'était pas testé et le snprintf qui suit écrivait
        // immédiatement à travers le nul. En cas d'échec : la liste des appareils par pièce n'est pas
        // dessinée (on sort de la boucle, le reste de la page — journal, système — tourne), dit UNE fois
        // dans les traces ; le tampon est retenté au rafraîchissement suivant.
        static char *ligne = nullptr;
        if (!ligne)
            ligne = (char *)ui_psram_calloc(1, LIGNE); // 29/10 : en PSRAM (ui_psram.h)
        if (!ligne)
        {
            if (!g_ligne_echec_signale)
            {
                g_ligne_echec_signale = true;
                ui_trace("[infos] allocation ligne pièce ÉCHOUÉE (%d o) : liste des appareils non dessinée\n", LIGNE);
            }
            break;
        }
        int n = snprintf(ligne, LIGNE, UI_MARK_VERT " %s :# ", a.piece[0] ? a.piece : TXT("Sans pièce", "No room"));
        const char *sep = "";
        for (int k = i; k < if_nb_appareils && n < (int)LIGNE - 30; k++)
            if (if_appareils[k].ok && !strcmp(if_appareils[k].piece, a.piece))
            {
                n += snprintf(ligne + n, LIGNE - n, "%s%s", sep, if_appareils[k].nom);
                sep = ", ";
            }
        lv_obj_t *l = ui_label(o_appareils, UI_POLICE_LEGENDE, UI_TEXTE2, "");
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_label_set_recolor(l, true);
        ui_label_texte(l, ligne);
    }

    // journal : une étiquette par ligne, refaite à chaque fois (IF_NB_JOURNAL lignes au plus)
    lv_obj_clean(o_journal);
    if (!g_nb_journal)
        ui_label(o_journal, UI_POLICE_LEGENDE, UI_TEXTE3, TXT("Journal vide", "No entries"));
    for (int i = 0; i < g_nb_journal; i++)
    {
        lv_obj_t *l = ui_label(o_journal, UI_POLICE_LEGENDE, UI_TEXTE2, "");
        lv_label_set_recolor(l, true);
        ui_label_texte_fmt(l, UI_MARK_TEXTE3 " %s#  %s%s%s", g_journal[i].heure, g_journal[i].alerte ? UI_MARK_ACCENT " " : "",
                              g_journal[i].texte, g_journal[i].alerte ? "#" : "");
    }

    // remise à zéro : le bouton demande confirmation sur place
    ui_style_bg_color(o_raz, ui_c(g_confirme_raz ? UI_ROUGE : 0x2A1717), 0);
    ui_style_border_color(o_raz, ui_c(g_confirme_raz ? UI_ROUGE : 0x5A2A26), 0);
    ui_style_text_color(o_raz_txt, ui_c(g_confirme_raz ? UI_SUR_ACCENT : 0xFF8A7A), 0);
    ui_label_texte(o_raz_txt, g_confirme_raz ? TXT("Toucher encore pour tout effacer", "Tap again to erase all") : TXT("Remise à zéro de la mémoire…", "Memory reset…"));
}

void page_infos_maj() { rafraichir(); }

static void action_cb(lv_event_t *e)
{
    if_action_t a = (if_action_t)(intptr_t)lv_event_get_user_data(e);
    if (a == IF_RAZ_MEMOIRE && !g_confirme_raz)
    {
        g_confirme_raz = true; // 1er appui : on demande confirmation
        rafraichir();
        return;
    }
    g_confirme_raz = false;
    if (a == IF_EFFACER_JOURNAL)
        if_journal_effacer();
    rafraichir();
    if (if_action)
        if_action(a);
}

static lv_obj_t *carte(lv_obj_t *page)
{
    lv_obj_t *c = ui_conteneur(page);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 16, 0);
    return c;
}

static lv_obj_t *bouton_action(lv_obj_t *parent, const char *txt, if_action_t a, lv_coord_t l, lv_obj_t **o_txt)
{
    lv_obj_t *b = ui_bouton(parent, l, 52);
    lv_obj_t *t = lv_label_create(b);
    ui_label_texte(t, txt);
    lv_obj_center(t);
    lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)a);
    if (o_txt)
        *o_txt = t;
    return b;
}

void page_infos_creer(lv_obj_t *zone)
{
    lv_obj_t *titre = ui_label(zone, UI_POLICE_VALEUR, UI_TEXTE, TXT("Informations", "Info"));
    lv_obj_align(titre, LV_ALIGN_TOP_MID, 0, 2);

    o_page = ui_conteneur_defilant(zone, 0, 46, UI_ZONE_L, UI_ZONE_H - 46);
    lv_obj_set_flex_flow(o_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(o_page, UI_MARGE, 0);
    lv_obj_set_style_pad_top(o_page, 4, 0);
    lv_obj_set_style_pad_bottom(o_page, 16, 0);
    lv_obj_set_style_pad_row(o_page, 10, 0);

    // CET ÉCRAN : 2 colonnes de « libellé / valeur »
    ui_titre_section(o_page, TXT("CET ÉCRAN", "THIS SCREEN"));
    lv_obj_t *c = carte(o_page);
    lv_obj_set_style_pad_ver(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(c, 10, 0);
    for (int i = 0; i < IF_NB_SYSTEME; i++)
    {
        lv_obj_t *cel = ui_conteneur(c);
        lv_obj_set_size(cel, lv_pct(50), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cel, LV_FLEX_FLOW_COLUMN);
        ui_label(cel, UI_POLICE_LEGENDE, UI_TEXTE2, if_systeme[i].cle);
        o_sys[i] = ui_label(cel, UI_POLICE_TEXTE, UI_TEXTE, "");
    }

    // LIAISONS : point vert / rouge + détail
    ui_titre_section(o_page, TXT("LIAISONS", "LINKS"));
    c = carte(o_page);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_ver(c, 0, 0);
    for (int i = 0; i < IF_NB_LIAISONS; i++)
    {
        lv_obj_t *r = ui_conteneur(c);
        lv_obj_set_size(r, lv_pct(100), 44);
        if (i < IF_NB_LIAISONS - 1)
        {
            lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
            ui_style_border_width(r, 1, 0);
            ui_style_border_color(r, ui_c(UI_BORD), 0);
        }
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(r, 10, 0);
        o_lien_point[i] = ui_conteneur(r);
        lv_obj_set_size(o_lien_point[i], 10, 10);
        lv_obj_set_style_radius(o_lien_point[i], LV_RADIUS_CIRCLE, 0);
        ui_style_bg_opa(o_lien_point[i], LV_OPA_COVER, 0);
        lv_obj_t *nom = ui_label(r, UI_POLICE_TEXTE, UI_TEXTE, if_liaisons[i].nom);
        lv_obj_set_flex_grow(nom, 1);
        o_lien_detail[i] = ui_label(r, UI_POLICE_PETITE, UI_TEXTE2, "");
    }

    // APPAREILS (de HA)
    ui_titre_section(o_page, TXT("APPAREILS", "DEVICES"));
    o_appareils = carte(o_page);
    lv_obj_set_style_pad_ver(o_appareils, 12, 0);
    lv_obj_set_flex_flow(o_appareils, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o_appareils, 6, 0);

    // JOURNAL : carte plus sombre, une ligne par évènement
    ui_titre_section(o_page, TXT("JOURNAL", "LOG"));
    o_journal = carte(o_page);
    ui_style_bg_color(o_journal, ui_c(0x0A0E14), 0);
    lv_obj_set_style_pad_hor(o_journal, 12, 0);
    lv_obj_set_style_pad_ver(o_journal, 10, 0);
    lv_obj_set_flex_flow(o_journal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o_journal, 4, 0);

    // ACTIONS
    ui_titre_section(o_page, TXT("ACTIONS", "ACTIONS"));
    lv_obj_t *r = ui_conteneur(o_page);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(r, 8, 0);
    const lv_coord_t demi = (UI_ZONE_L - 2 * UI_MARGE - 8) / 2;
    bouton_action(r, TXT("Redémarrer", "Restart"), IF_REDEMARRER, demi, nullptr);
    bouton_action(r, TXT("Effacer le journal", "Clear log"), IF_EFFACER_JOURNAL, demi, nullptr);
    o_raz = bouton_action(r, "", IF_RAZ_MEMOIRE, UI_ZONE_L - 2 * UI_MARGE, &o_raz_txt);

    g_confirme_raz = false;
    rafraichir();
}

void page_infos_quitter()
{
    o_page = nullptr; // rafraichir() ne touche plus à rien
    g_confirme_raz = false;
}
