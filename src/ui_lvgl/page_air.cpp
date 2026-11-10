#include "page_air.h"
#include "ui_theme.h"
#include "ui_courbe.h"
#include "icones_ventilo_rgb.h" // l'hélice de la VMC (en couleur : LVGL 8.4 ne fait tourner que celles-là)

aq_purificateur_t aq_purificateurs[AQ_NB] = {
    {TXT("Atelier", "Workshop"), "ESPurifier", 0, {}, "",
     {TXT("Arrêt", "Off"), "Fan", "1", "2", "3"}, 5, 0, true},          // 22/10 : à l'atelier
    {TXT("Séjour", "Living Room"), TXT("Purificateur Xiaomi", "Xiaomi Purifier"), 0, {}, "",
     {TXT("Arrêt", "Off"), TXT("Auto", "Auto"), TXT("Silence", "Silent"), TXT("Fan", "Fan")}, 4, 0, true}, // derrière le poêle
};
void (*aq_commande)(int purificateur, int mode) = nullptr;

#define CARTE_H 196 // comme la VMC
#define COURBE_L 120
#define COURBE_H 46

static lv_obj_t *o_titre, *o_pm[AQ_NB], *o_niveau[AQ_NB], *o_courbe[AQ_NB], *o_info[AQ_NB], *o_modes[AQ_NB][5];
static lv_obj_t *o_ventilo[AQ_NB], *o_etat[AQ_NB]; // 24/10 : l'état de la ventilation, comme la VMC (retour d'usage)
static int g_periode[AQ_NB] = {-1, -1};         // hélice : durée d'un tour en ms (0 arrêtée, -1 pas encore réglée)

// l'hélice tourne à la vitesse réelle du ventilateur quand HA la donne (25/10 : 1 tour/s pour 1 000 tr/min, entre 0,3 et
// 3 tours/s, arrêtée à 0) ; sinon à vitesse fixe quand le purificateur marche. Relancée seulement si la vitesse change de
// plus de 10 %, depuis l'angle où elle est (pas d'à-coup)
static int periode_de(const aq_purificateur_t &p, bool marche)
{
    if (!marche)
        return 0;
    if (!p.rpm_connu)
        return 1800;
    return p.rpm <= 0 ? 0 : LV_CLAMP(333, 1000000 / p.rpm, 3000);
}

static void animer(int i, int periode)
{
    const int avant = g_periode[i];
    if (avant == periode || (avant > 0 && periode > 0 && LV_ABS(periode - avant) * 10 < avant))
        return;
    g_periode[i] = periode;
    ui_helice_tourner(o_ventilo[i], periode);
}

static uint32_t couleur_niveau(int pm) { return pm < 12 ? UI_VERT : pm < 35 ? UI_ACCENT : UI_ROUGE; }
static const char *mot_niveau(int pm) { return pm < 12 ? TXT("Bon", "Good") : pm < 35 ? TXT("Moyen", "Moderate") : TXT("Mauvais", "Poor"); }

// courbe des 24 h (module commun ui_courbe : même trait, trous non tracés) ; échelle 0 -> max des 24 h (au moins
// 20 µg/m³, pour qu'un air propre reste plat) ; couleur = niveau de l'air maintenant
static void courbe_cb(lv_event_t *e)
{
    const aq_purificateur_t &p = aq_purificateurs[(int)(intptr_t)lv_event_get_user_data(e)];
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a); // 9.6 : lv_event_get_target renvoie void*
    int haut = 20;
    for (int k = 0; k < AQ_HIST; k++)
        if (p.hist[k] != AQ_VIDE)
            haut = LV_MAX(haut, p.hist[k]);
    const uc_cadre_t c = {uc_cible_du(e), a.x1, (lv_coord_t)(a.x1 + COURBE_L - 1), (lv_coord_t)(a.y2 - COURBE_H + 2), (lv_coord_t)(a.y2 - 2),
                          0, AQ_HIST, 0, (float)haut};
    uc_trait(c, uc_serie(p.hist, AQ_VIDE), couleur_niveau(p.pm25), 2);
}

static void rafraichir()
{
    if (!o_titre)
        return;
    for (int i = 0; i < AQ_NB; i++)
    {
        const aq_purificateur_t &p = aq_purificateurs[i];
        ui_label_texte_fmt(o_pm[i], "%d", p.pm25);
        ui_style_text_color(o_pm[i], ui_c(couleur_niveau(p.pm25)), 0);
        ui_label_texte_fmt(o_niveau[i], TXT("PM2,5 · %s", "PM2.5 · %s"), mot_niveau(p.pm25));
        ui_style_text_color(o_niveau[i], ui_c(couleur_niveau(p.pm25)), 0);
        // ventilation : le mode en cours, en gros (« Silence », « 1 », « Arrêt »), et l'hélice
        const bool marche = p.en_ligne && p.mode != 0;
        ui_label_texte(o_etat[i], !p.en_ligne ? TXT("Hors ligne", "Offline") : p.mode >= 0 && p.mode < p.nb_modes ? p.modes[p.mode] : TXT("Favori", "Favorite"));
        ui_style_text_color(o_etat[i], ui_c(marche ? UI_TEXTE : UI_TEXTE3), 0);
        ui_style_img_recolor(o_ventilo[i], ui_c(marche ? UI_TEXTE : UI_TEXTE3), 0);
        animer(i, periode_de(p, marche));
        ui_label_texte_fmt(o_info[i], p.info[0] ? "%s · %s" : "%s", p.appareil, p.info);
        // 27/10 (audit des redessins) : la courbe seulement si son historique ou sa couleur change
        static uint32_t dessinee[AQ_NB];
        const uint32_t teinte = couleur_niveau(p.pm25);
        const uint32_t e = ui_empreinte(p.hist, sizeof p.hist, ui_empreinte(&teinte, sizeof teinte));
        if (e != dessinee[i])
        {
            dessinee[i] = e;
            lv_obj_invalidate(o_courbe[i]);
        }
        for (int m = 0; m < p.nb_modes; m++)
            if (m == p.mode)
                lv_obj_add_state(o_modes[i][m], LV_STATE_CHECKED);
            else
                lv_obj_clear_state(o_modes[i][m], LV_STATE_CHECKED);
    }
}

void page_air_maj() { rafraichir(); }

static void mode_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e); // purificateur * 8 + mode
    int i = code / 8, m = code % 8;
    aq_purificateurs[i].mode = m;
    rafraichir();
    if (aq_commande)
        aq_commande(i, m);
}

void page_air_creer(lv_obj_t *zone)
{
    o_titre = ui_label(zone, UI_POLICE_VALEUR, UI_TEXTE, TXT("Qualité de l'air", "Air Quality"));
    lv_obj_align(o_titre, LV_ALIGN_TOP_MID, 0, 2);

    for (int i = 0; i < AQ_NB; i++)
    {
        const aq_purificateur_t &p = aq_purificateurs[i];
        lv_obj_t *c = ui_conteneur(zone);
        ui_style_ajouter(c, &ui_st_carte, 0);
        lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, CARTE_H);
        lv_obj_set_pos(c, UI_MARGE, 50 + i * (CARTE_H + 10));

        // 24/10 : la carte de la VMC : hélice + pièce + état de la ventilation à gauche, qualité de l'air à droite,
        // ligne d'aide et courbe des 24 h dessous, modes en bas
        o_ventilo[i] = ui_icone(c, &icon_ventilo_rgb, UI_TEXTE);
        lv_obj_set_pos(o_ventilo[i], 0, 2);
        lv_img_set_pivot(o_ventilo[i], 28, 28);
        g_periode[i] = -1;
        lv_obj_set_pos(ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, p.piece), 70, 2);
        o_etat[i] = ui_label(c, UI_POLICE_TEXTE_22, UI_TEXTE, "");
        lv_obj_set_pos(o_etat[i], 70, 26);
        lv_obj_align(ui_valeur_unite(c, UI_POLICE_TEMP_PETITE, UI_TEXTE, "µg/m³", &o_pm[i]), LV_ALIGN_TOP_RIGHT, 0, 0);
        o_niveau[i] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, ""); // « PM2,5 · Bon », comme « salle de bain » sous la VMC
        lv_obj_align(o_niveau[i], LV_ALIGN_TOP_RIGHT, 0, 48);
        o_info[i] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, ""); // appareil et mesure secondaire : la ligne d'aide de la VMC
        lv_obj_set_pos(o_info[i], 0, 74);
        o_courbe[i] = ui_conteneur(c);
        lv_obj_set_size(o_courbe[i], COURBE_L, COURBE_H);
        lv_obj_align(o_courbe[i], LV_ALIGN_TOP_RIGHT, 0, 66);
        lv_obj_add_event_cb(o_courbe[i], courbe_cb, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)i);

        // modes
        lv_obj_t *rang = ui_conteneur(c);
        lv_obj_set_size(rang, lv_pct(100), 52);
        lv_obj_align(rang, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(rang, 6, 0);
        for (int m = 0; m < p.nb_modes; m++)
        {
            lv_obj_t *b = ui_bouton(rang, 10, 52);
            lv_obj_set_flex_grow(b, 1);
            ui_style_bg_color(b, ui_c(UI_CARTE2), 0); // bouton posé dans une carte (comme la VMC)
            lv_obj_t *t = lv_label_create(b);
            ui_label_texte(t, p.modes[m]);
            lv_obj_center(t);
            o_modes[i][m] = b;
            lv_obj_add_event_cb(b, mode_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 8 + m));
        }
    }
    rafraichir();
}

void page_air_quitter()
{
    for (int i = 0; i < AQ_NB; i++)
        ui_helice_arreter(o_ventilo[i]); // les hélices vont être détruites avec la zone
    o_titre = nullptr; // rafraichir() ne touche plus à rien
}
