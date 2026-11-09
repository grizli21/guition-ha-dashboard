#include "page_radar.h"
#include "ui_theme.h"
#include "ui_courbe.h" // 05/11 (vague D) : wrappers uc_* — graphe radar sous 9.6
#include <stdio.h>
#include <string.h>

rd_etat_t rd_etat = {};
void (*rd_lire)() = nullptr;
void (*rd_envoyer)() = nullptr;
void (*rd_auto)(uint16_t secondes) = nullptr;
void (*rd_diag)(bool actif) = nullptr;
void (*rd_marge)(uint8_t db) = nullptr;

#define G_Y 104
#define G_H 200
#define G_X 34
#define SCAN_S 60 // durée du réglage automatique

static lv_obj_t *g_zone, *o_onglets[2], *o_graphe, *o_point, *o_presence, *o_aide, *o_zone, *o_plage, *o_info, *o_val, *o_auto_txt;
static int g_vue = 0, g_choisie = 3;
static bool g_confirme_auto = false;
static uint8_t g_dessine[RD_ZONES * 2 + 2]; // ce que montre le graphe (redessiné seulement si ça change)

static uint8_t *seuils() { return g_vue ? rd_etat.maintien : rd_etat.detection; }

static void graphe_cb(lv_event_t *e)
{
    uc_cible_t * ctx = uc_cible_du(e); // 9.6 : la layer de l'événement (avant : lv_event_get_draw_ctx)
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const uint8_t *s = seuils();
    const lv_coord_t x0 = a.x1 + G_X, x1 = a.x2 - 6, bas = a.y1 + G_H, pas = (x1 - x0) / RD_ZONES;
    auto y = [&](int v) { return (lv_coord_t)(bas - LV_CLAMP(0, v, 100) * (G_H - 10) / 100); };
    lv_draw_line_dsc_t gl;
    lv_draw_line_dsc_init(&gl);
    gl.color = ui_c(0x1F252D);
    gl.width = 1;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = UI_POLICE_LEGENDE;
    ld.color = ui_c(UI_TEXTE3);
    for (int v = 0; v <= 100; v += 25)
    {
        uc_segment(ctx, gl, x0, y(v), x1, y(v)); // 9.6 : p1/p2 dans le descripteur (uc_segment)
        char t[8];
        snprintf(t, sizeof t, "%d", v);
        const lv_area_t z = {a.x1, (lv_coord_t)(y(v) - 7), (lv_coord_t)(x0 - 6), (lv_coord_t)(y(v) + 7)};
        ld.align = LV_TEXT_ALIGN_RIGHT;
        uc_texte(ctx, ld, &z, t, true); // texte sur la pile : 9.6 doit le copier (text_local)
    }
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    for (int i = 0; i < RD_ZONES; i++)
    {
        const lv_coord_t bx = x0 + i * pas + 3, bx2 = x0 + (i + 1) * pas - 3;
        d.bg_color = ui_c(i == g_choisie ? UI_ACCENT : UI_ETEINT);
        d.bg_opa = i == g_choisie ? LV_OPA_COVER : LV_OPA_80;
        const lv_area_t z = {bx, y(s[i]), bx2, bas};
        uc_carre(ctx, d, &z); // NOTE coût 9.6 : 2 tasks par zone (barre + signal), 16 zones, ajout en O(n)
        if (rd_etat.pc) // signal mesuré : vert au-dessus du seuil (la zone détecte), bleu en dessous
        {
            lv_draw_rect_dsc_t m;
            lv_draw_rect_dsc_init(&m);
            m.radius = 2;
            m.bg_color = ui_c(rd_etat.energie[i] >= s[i] ? UI_VERT : UI_FROID);
            const lv_area_t zs = {(lv_coord_t)(bx - 2), (lv_coord_t)(y(rd_etat.energie[i]) - 2), (lv_coord_t)(bx2 + 2),
                                  (lv_coord_t)(y(rd_etat.energie[i]) + 2)};
            uc_carre(ctx, m, &zs);
        }
    }
    ld.align = LV_TEXT_ALIGN_CENTER;
    for (int m = 0; m <= 9; m += 3) // 0,7 m par zone (Hi-Link) : les 16 zones couvrent 11,2 m
    {
        char t[8];
        snprintf(t, sizeof t, "%d m", m);
        const lv_coord_t xm = x0 + m * pas * 10 / 7;
        const lv_area_t z = {(lv_coord_t)(xm - 20), (lv_coord_t)(bas + 6), (lv_coord_t)(xm + 20), (lv_coord_t)(bas + 20)};
        uc_texte(ctx, ld, &z, t, true);
    }
    if (rd_etat.presence) // la présence : point vert à sa distance
    {
        lv_draw_rect_dsc_t p;
        lv_draw_rect_dsc_init(&p);
        p.radius = LV_RADIUS_CIRCLE;
        p.bg_color = ui_c(UI_VERT);
        const lv_coord_t xp = x0 + (lv_coord_t)(LV_MIN(1200, rd_etat.distance) * pas * RD_ZONES / 1200);
        const lv_area_t z = {(lv_coord_t)(xp - 5), (lv_coord_t)(bas - 5), (lv_coord_t)(xp + 5), (lv_coord_t)(bas + 5)};
        uc_carre(ctx, p, &z);
    }
}

static void rafraichir()
{
    if (!g_zone)
        return;
    for (int i = 0; i < 2; i++)
        if (i == g_vue)
            lv_obj_add_state(o_onglets[i], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(o_onglets[i], LV_STATE_CHECKED);
    ui_style_bg_color(o_point, ui_c(rd_etat.presence ? UI_VERT : UI_TEXTE3), 0);
    if (!rd_etat.present)
        ui_label_texte(o_presence, "Radar muet");
    else if (rd_etat.auto_en_cours)
        ui_label_texte_fmt(o_presence, "Réglage auto : %d %%", (int)rd_etat.progression);
    else if (rd_etat.presence)
        ui_label_texte_fmt(o_presence, "Présence à %d,%02d m", rd_etat.distance / 100, rd_etat.distance % 100);
    else
        ui_label_texte(o_presence, "Personne");
    if (g_confirme_auto) // 28/10 : l'explication ici, le bouton garde un mot court (« Pièce vide ? toucher » débordait)
        ui_label_texte_fmt(o_aide, "retoucher : 30 s pour sortir · marge +%u dB", (unsigned)rd_etat.marge);
    else
        ui_label_texte(o_aide, rd_etat.message[0] ? rd_etat.message
                               : g_vue ? "colonne = seuil pour rester « présent » · trait = signal mesuré"
                                       : "colonne = seuil pour détecter · trait = signal (vert : détecte)");
    ui_label_texte_fmt(o_zone, "Zone %d", g_choisie);
    ui_label_texte_fmt(o_plage, "%d,%d – %d,%d m", g_choisie * 7 / 10, g_choisie * 7 % 10, (g_choisie + 1) * 7 / 10,
                       (g_choisie + 1) * 7 % 10); // 0,7 m par zone (Hi-Link)
    if (rd_etat.pc)
        ui_label_texte_fmt(o_info, "signal %d dB", rd_etat.energie[g_choisie]);
    else
        ui_label_texte(o_info, "signal : trames PC désactivées");
    ui_label_texte_fmt(o_val, "%d", seuils()[g_choisie]);
    ui_label_texte(o_auto_txt, rd_etat.depart_s ? "Annuler" : g_confirme_auto ? "Confirmer" : "Réglage auto");
    // le graphe : seulement si ce qu'il montre a changé
    uint8_t d[sizeof g_dessine];
    for (int i = 0; i < RD_ZONES; i++)
    {
        d[i] = seuils()[i];
        d[RD_ZONES + i] = rd_etat.pc ? rd_etat.energie[i] : 0;
    }
    d[2 * RD_ZONES] = (uint8_t)(g_choisie | (g_vue << 5) | (rd_etat.presence << 6));
    d[2 * RD_ZONES + 1] = (uint8_t)(rd_etat.distance / 10);
    if (memcmp(d, g_dessine, sizeof d))
    {
        memcpy(g_dessine, d, sizeof d);
        lv_obj_invalidate(o_graphe);
    }
}

void page_radar_maj() { rafraichir(); }

static void onglet_cb(lv_event_t *e)
{
    g_vue = (int)(intptr_t)lv_event_get_user_data(e);
    rafraichir();
}

static void graphe_doigt_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    lv_area_t a;
    lv_obj_get_coords(o_graphe, &a);
    const lv_coord_t x0 = a.x1 + G_X, x1 = a.x2 - 6, pas = (x1 - x0) / RD_ZONES;
    const int i = (p.x - x0) / pas;
    if (i >= 0 && i < RD_ZONES && i != g_choisie)
    {
        g_choisie = i;
        rafraichir();
    }
    (void)e;
}

// 06/11 (mesure du chantier 2, audit e58325f) : un pas du glissé horizontal sur le graphe radar,
// sans indev ni /glisser ni /toucher ni forcer — précédent : /scrubgraph (4f41058). Le geste ne
// lit pas un point : il change la ZONE choisie à chaque franchissement (16 zones, ~21 px de
// course par zone). Emprunte le chemin de graphe_doigt_cb avec la MÊME correspondance x -> zone
// (recopiée : lv_indev_get_point exige un événement de doigt — seul écart forcé par le verrou).
// Fonctions appelées par le pas (celles du doigt, rien de plus) : rafraichir() = étiquettes
// (auto/seuil/zone), puis empreinte (seuils + énergie + g_choisie + g_vue + présence + distance)
// -> lv_obj_invalidate(o_graphe) EN ENTIER (page_radar.cpp:138, 90 496 px — l'objet mesuré).
// NOTE : sur `.193` (chambre1, sur secteur), la page « Batterie et capteur » — donc l'onglet
// Radar — n'existe pas (réservée aux écrans sur batterie, ecran.cpp:1662) : cet outil
// est commité pour la suite, il n'a pas pu être mesuré sur `.193`.
void page_radar_banc_scrub(int x_doigt)
{
    if (!o_graphe)
        return;
    if (x_doigt < 0) // relâcher : comme le doigt, la zone choisie reste choisie (aucun chemin de relâcher)
        return;
    lv_area_t a;
    lv_obj_get_coords(o_graphe, &a);
    const lv_coord_t x0 = a.x1 + G_X, x1 = a.x2 - 6, pas = (x1 - x0) / RD_ZONES;
    const int i = (x_doigt - x0) / pas;
    if (i >= 0 && i < RD_ZONES && i != g_choisie)
    {
        g_choisie = i;
        rafraichir(); // le chemin réel du doigt à ce stade
    }
}

static void pas_cb(lv_event_t *e)
{
    const int pas = (int)(intptr_t)lv_event_get_user_data(e);
    uint8_t &v = seuils()[g_choisie];
    // 26/10 : firmware 1.1.1 du capteur (manuel Hi-Link V1.04) : les 16 zones ont un seuil absolu de 10 à 95 dB
    // (le rapport signal / bruit 5-63 des zones 8-15 de la page de la production venait du manuel V1.2, plus ancien)
    v = (uint8_t)LV_CLAMP(10, v + pas, 95);
    snprintf(rd_etat.message, sizeof rd_etat.message, "modifié, à envoyer au radar");
    rafraichir();
}

static void action_cb(lv_event_t *e)
{
    const int a = (int)(intptr_t)lv_event_get_user_data(e);
    if (a != 2)
        g_confirme_auto = false;
    if (a == 0 && rd_lire)
        rd_lire();
    else if (a == 1 && rd_envoyer)
        rd_envoyer();
    else if (a == 2)
    {
        if (rd_etat.depart_s) // 28/10 : pendant le compte à rebours, le bouton annule
        {
            g_confirme_auto = false;
            if (rd_auto)
                rd_auto(0);
        }
        else if (!g_confirme_auto) // 1er appui : la pièce doit être vide pendant tout le scan (comme la page web)
            g_confirme_auto = true;
        else
        {
            g_confirme_auto = false;
            if (rd_auto)
                rd_auto(SCAN_S);
        }
    }
    rafraichir();
}

void page_radar_creer(lv_obj_t *zone)
{
    g_zone = zone;
    g_confirme_auto = false;
    memset(g_dessine, 0xFF, sizeof g_dessine);
    rd_etat.message[0] = 0;

    // 31/10 : onglet de la page Batterie (ses onglets sont en haut) : Détection / Maintien en petit sélecteur, à droite
    // de la présence ; l'aide (ou le dernier message) prend la ligne de la légende
    o_point = ui_conteneur(zone);
    lv_obj_set_size(o_point, 10, 10);
    lv_obj_set_style_radius(o_point, LV_RADIUS_CIRCLE, 0);
    ui_style_bg_opa(o_point, LV_OPA_COVER, 0);
    lv_obj_set_pos(o_point, UI_MARGE + 2, 64);
    o_presence = ui_label(zone, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_obj_set_pos(o_presence, UI_MARGE + 18, 58);
    lv_obj_t *sel = ui_conteneur(zone);
    ui_style_ajouter(sel, &ui_st_carte, 0);
    lv_obj_set_size(sel, 2 * 92 + 6, 34);
    lv_obj_align(sel, LV_ALIGN_TOP_RIGHT, -UI_MARGE, 52);
    lv_obj_set_style_radius(sel, 10, 0);
    lv_obj_set_style_pad_all(sel, 3, 0);
    lv_obj_set_flex_flow(sel, LV_FLEX_FLOW_ROW);
    static const char *const NOMS[] = {"Détection", "Maintien"};
    for (int i = 0; i < 2; i++)
    {
        lv_obj_t *b = o_onglets[i] = ui_bouton(sel, 92, 28);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, ui_c(UI_CARTE2), LV_STATE_CHECKED);
        lv_obj_center(ui_label(b, UI_POLICE_PETITE, UI_TEXTE, NOMS[i]));
        lv_obj_add_event_cb(b, onglet_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    o_aide = ui_label(zone, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_pos(o_aide, UI_MARGE, 88);

    o_graphe = ui_conteneur(zone);
    lv_obj_set_size(o_graphe, UI_ZONE_L, G_H + 24);
    lv_obj_set_pos(o_graphe, 0, G_Y);
    lv_obj_add_flag(o_graphe, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_graphe, graphe_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(o_graphe, graphe_doigt_cb, LV_EVENT_PRESSING, NULL);

    lv_obj_t *c = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 72);
    lv_obj_set_pos(c, UI_MARGE, 336);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 0, 0);
    o_zone = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_obj_align(o_zone, LV_ALIGN_TOP_LEFT, 0, 12);
    o_plage = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_align(o_plage, LV_ALIGN_TOP_LEFT, 64, 15);
    o_info = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE3, "");
    lv_obj_align(o_info, LV_ALIGN_BOTTOM_LEFT, 0, -12);
    lv_obj_t *plus = ui_bouton(c, 44, 44);
    lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(ui_label(plus, UI_POLICE_TITRE, UI_TEXTE, "+"));
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(plus, pas_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *rv = ui_valeur_unite(c, UI_POLICE_3X5_P, UI_ACCENT, "", &o_val);
    lv_obj_align(rv, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_obj_t *moins = ui_bouton(c, 44, 44);
    lv_obj_set_style_radius(moins, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(ui_label(moins, UI_POLICE_TITRE, UI_TEXTE, "−"));
    lv_obj_align(moins, LV_ALIGN_RIGHT_MID, -116, 0);
    lv_obj_add_event_cb(moins, pas_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

    const char *act[] = {"Lire le radar", "Envoyer au radar", "Réglage auto"};
    const lv_coord_t l = (UI_ZONE_L - 2 * UI_MARGE - 16) / 3;
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *b = ui_bouton(zone, l, 52);
        lv_obj_set_pos(b, UI_MARGE + i * (l + 8), 416);
        if (i == 1)
            ui_style_ajouter(b, &ui_st_bouton_principal, 0);
        lv_obj_t *t = ui_label(b, UI_POLICE_PETITE, i == 1 ? UI_SUR_ACCENT : UI_TEXTE, act[i]);
        lv_obj_center(t);
        if (i == 2)
            o_auto_txt = t;
        lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    // 26/10 : les seuils vrais du capteur, une fois la vue construite (la relecture la met à jour) ; 31/10 : seulement s'ils
    // ne sont pas déjà connus (« Lire le radar » relit à la demande)
    if (rd_lire && !rd_etat.lu)
        rd_lire();
    rafraichir();
}

void page_radar_quitter() { g_zone = nullptr; }
