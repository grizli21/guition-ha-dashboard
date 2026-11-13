/**
 * @file lv_refr.h
 *
 */

#ifndef LV_REFR_H
#define LV_REFR_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lv_obj.h"
#include "../display/lv_display.h"
#include "../lv_types.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Redraw the invalidated areas now.
 * Normally the redrawing is periodically executed in `lv_timer_handler` but a long blocking process
 * can prevent the call of `lv_timer_handler`. In this case if the GUI is updated in the process
 * (e.g. progress bar) this function can be called when the screen should be updated.
 * @param disp pointer to display to refresh. @nullable When NULL all displays are refreshed.
 */
void lv_refr_now(lv_display_t * disp);

/**
 * Redrawn on object and all its children using the passed draw context
 * @param layer pointer to a layer where to draw.
 * @param obj   the start object from the redraw should start
 */
void lv_obj_redraw(lv_layer_t * layer, lv_obj_t * obj);

/**
 * Called periodically to handle the refreshing
 * @param timer pointer to the timer itself. @nullable When NULL the default display is refreshed
 */
void lv_display_refr_timer(lv_timer_t * timer);

/* Patch guition (13/11/2026) : élagage du parcours de l'arbre, pour le rendu des petits
 * couloirs de pluie de la page IA (coût fixe par zone mesuré : parcours de ~100 objets,
 * ~2,7 kcycles/visite). Activé par défaut. lv_obj_refr() teste les coordonnées (+ ext_draw_size)
 * de l'objet contre l'aire de clip de la zone AVANT toute recherche de style, et sort tout de
 * suite si l'objet ne peut rien peindre dans la zone ; lv_draw_border() saute la tâche quand la
 * zone ne touche que l'intérieur de l'objet (la bordure est un anneau le long du bord). */
void lv_refr_set_elagage(bool on);
bool lv_refr_get_elagage(void);

/**********************
 *   STATIC FUNCTIONS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_REFR_H*/
