#include "ui_pieces.h"
#include "ui_theme.h"

th_piece_t th_pieces[TH_NB_PIECES] = {
    {TXT("Cuisine", "Kitchen"), 0, 15, TH_ARRET, false, 0, 100, 255},
    {TXT("Séjour", "Living Room"), 0, 15, TH_ARRET, false, 0, 100, 255},
    {TXT("Chambre 1", "Bedroom 1"), 0, 15, TH_ARRET, false, 0, 100, 255},
    {TXT("Chambre 2", "Bedroom 2"), 0, 15, TH_ARRET, false, 0, 100, 255},
    {TXT("Salle de bain", "Bathroom"), 0, 20, TH_ARRET, false, 0, 100, 255},
};
int th_piece_affichee = 0;

// Géométrie de l'en-tête (mesurée lot 5 : identique sur les deux pages).
#define BANDE_PIECE_H 60 // en-tête : nom + points (≈49) ; le contenu de page commence en dessous
#define POINTS_Y 40      // haut de la rangée de points (lv_obj_align … 0, 40)
#define POINT_PAS 18     // point de 9 px + écart de 9 px

static void fleche_repere(lv_obj_t *zone, const char *sym, lv_coord_t x)
{
    lv_obj_t *b = ui_bouton(zone, 56, 44);
    ui_style_bg_opa(b, LV_OPA_TRANSP, 0);
    ui_style_border_width(b, 0, 0);
    lv_obj_set_pos(b, x, 0);
    lv_obj_center(ui_label(b, UI_POLICE_CHEVRONS, UI_TEXTE2, sym)); // 27/10 : 30 -> 36 (retour d'usage)
    // 11/11 : les flèches ne sont que des repères ; la bande transparente reçoit l'appui
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
}

static void bande_piece_cb(lv_event_t *e)
{
    const ui_tete_piece_t *tete = (const ui_tete_piece_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t z;
    lv_obj_get_coords((lv_obj_t *)lv_event_get_target(e), &z);
    const int x = p.x - z.x1, y = p.y - z.y1, l = lv_area_get_width(&z);
    const int rangee = TH_NB_PIECES * POINT_PAS, x0 = (l - rangee) / 2;
    if (y >= POINTS_Y - 10 && x >= x0 - 4 && x < x0 + rangee + 4)
        tete->cb(true, LV_CLAMP(0, (x - x0) / POINT_PAS, TH_NB_PIECES - 1), tete->donnees);
    else
        tete->cb(false, x < l / 2 ? -1 : 1, tete->donnees);
}

void ui_tete_piece(lv_obj_t *zone, ui_tete_piece_t *tete,
                   void (*cb)(bool saut, int valeur, void *donnees), void *donnees)
{
    tete->cb = cb;
    tete->donnees = donnees;
    fleche_repere(zone, LV_SYMBOL_LEFT, 8);
    fleche_repere(zone, LV_SYMBOL_RIGHT, UI_ZONE_L - 8 - 56);
    tete->nom = ui_label(zone, UI_POLICE_NOM_PIECE, UI_TEXTE, ""); // 31/10 : en gras, comme le thermostat
    lv_obj_align(tete->nom, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t *points = ui_conteneur(zone); // 24/10 : vu au banc : les points manquaient sur une page
    lv_obj_set_size(points, LV_SIZE_CONTENT, 9);
    lv_obj_set_flex_flow(points, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(points, 9, 0);
    lv_obj_align(points, LV_ALIGN_TOP_MID, 0, POINTS_Y);
    for (int i = 0; i < TH_NB_PIECES; i++)
    {
        tete->points[i] = ui_conteneur(points);
        lv_obj_set_size(tete->points[i], 9, 9);
        lv_obj_set_style_radius(tete->points[i], LV_RADIUS_CIRCLE, 0);
        ui_style_bg_opa(tete->points[i], LV_OPA_COVER, 0);
    }
    lv_obj_t *bande = ui_conteneur(zone); // 11/11 : toute l'en-tête choisit la pièce, à l'appui
    lv_obj_set_size(bande, UI_ZONE_L, BANDE_PIECE_H);
    lv_obj_set_pos(bande, 0, 0);
    lv_obj_add_flag(bande, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bande, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(bande, bande_piece_cb, LV_EVENT_PRESSED, tete);
}

void ui_tete_piece_maj(const ui_tete_piece_t *tete, int piece)
{
    ui_label_texte(tete->nom, th_pieces[piece].nom);
    for (int i = 0; i < TH_NB_PIECES; i++)
        ui_style_bg_color(tete->points[i], ui_c(i == piece ? UI_TEXTE : UI_ETEINT), 0);
}
