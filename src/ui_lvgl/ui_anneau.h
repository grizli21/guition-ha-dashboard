// 22/10/2026 : anneau de réglage commun (dessin du thermostat validé le 18/10, repris pour l'eau chaude) :
// 270° en dégradé froid -> accent -> rouge, ouverture du bas fermée par un segment uni un cran (RGB565) plus
// sombre que le fond, avec un fondu de 10° à chaque jonction.
// (Le thermostat garde pour l'instant sa propre copie de ce dessin : il est validé et flashé.)
#pragma once
#include <lvgl.h>

// t_coupure : position (0..1) au-delà de laquelle l'anneau est assombri ; > 1 = rien d'assombri, < 0 = tout.
// ouverture_vide (25/10, poêle : « le dégradé de l'anneau déborde sur le texte ») : rien dans l'ouverture du bas (un texte
// y est posé), les deux bouts s'estompent vers le fond sur leurs 10 derniers degrés
// 05/11/2026 portage 9.6 (étape 2-vague B) : lv_draw_ctx_t a disparu en v9, le dessin se fait sur la LAYER du draw
// task — voir les correspondances (avec les lignes des en-têtes 9.6) documentées dans ui_courbe.h.
void ui_anneau_dessiner(lv_layer_t *layer, const lv_point_t *centre, lv_coord_t diametre, lv_coord_t epaisseur,
                        float t_coupure, bool ouverture_vide = false);
