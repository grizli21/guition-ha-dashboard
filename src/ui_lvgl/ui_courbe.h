// 24/10/2026 : dessin COMMUN des courbes (Graphe des pièces, Poêle, Air) : la même grammaire partout (retour d'usage : « harmonie
// sur tout et entre graph aussi »). Valeur principale = trait épais ambre + dégradé + point au bout ; secondaire = trait moyen ;
// extérieur = trait fin bleu ; consigne = escalier pointillé clair ; seuil = pointillé rouge fin ; grandeurs « de fond »
// (humidité, tirage) = bande du bas avec son dégradé, chauffe = bandes verticales ; graduations avec leur unité ; temps en HEURES DU JOUR (maintenant au bord
// droit) ; les trous de mesure ne sont jamais tracés ; légende = pastilles qui allument / éteignent les couches.
#pragma once
#include <lvgl.h>
#include <stdint.h>
#include <string.h> // 05/11 (vague D) : strlen dans uc_largeur_texte

// Une série de points : entiers sur 8 ou 16 bits, une valeur « vide » (pas de mesure), un diviseur (°C x10 -> 10)
struct uc_serie_t
{
    const void *donnees;
    bool octets; // uint8_t (sinon int16_t)
    int vide;
    float diviseur;
    float lire(int i) const { return (octets ? ((const uint8_t *)donnees)[i] : ((const int16_t *)donnees)[i]) / diviseur; }
    bool trou(int i) const { return (octets ? ((const uint8_t *)donnees)[i] : ((const int16_t *)donnees)[i]) == vide; }
};
static inline uc_serie_t uc_serie(const int16_t *d, int vide, float div = 1) { return {d, false, vide, div}; }
static inline uc_serie_t uc_serie(const uint8_t *d, int vide, float div = 1) { return {d, true, vide, div}; }

// ---- 05/11/2026 portage LVGL 9.6 du dessin personnalisé (étape 2-vague B) -------------------------------
// Correspondances 8.4 -> 9.6 lues dans les en-têtes de lib/lvgl-9.6.0/include/lvgl :
//   * lv_draw_ctx_t a DISPARU (plus aucune déclaration dans include/lvgl) ; on dessine sur une LAYER :
//     lv_draw_rect(lv_layer_t*, const lv_draw_rect_dsc_t*, const lv_area_t*)  -> draw/lv_draw_rect.h:229
//     lv_draw_line(lv_layer_t*, const lv_draw_line_dsc_t*)                    -> draw/lv_draw_line.h:102
//     lv_draw_arc (lv_layer_t*, const lv_draw_arc_dsc_t*)                     -> draw/lv_draw_arc.h:84
//     lv_draw_label(lv_layer_t*, const lv_draw_label_dsc_t*, const lv_area_t*) -> draw/lv_draw_label.h:188
//   * un objet dessine toujours via LV_EVENT_DRAW_MAIN/_BEGIN/_END (core/lv_event.h:71-75) ; l'événement porte la
//     layer : lv_obj_send_event(obj, LV_EVENT_DRAW_MAIN, layer) (core/lv_refr.c:131-133), et on la récupère avec
//     lv_event_get_layer(e) (core/lv_obj_event.h:124, core/lv_obj_event.c:225). lv_event_get_draw_ctx n'existe plus.
//   * LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS (core/lv_obj.h:72) + LV_EVENT_DRAW_TASK_ADDED (core/lv_event.h:77) et
//     lv_event_get_draw_task (core/lv_obj_event.h:197) servent à INTERCEPTER une task déjà créée : inutile ici,
//     on crée nos propres tasks.
//   * la géométrie est DÉSORMAIS DANS le descripteur pour la ligne (p1/p2, draw/lv_draw_line.h:36-42) et l'arc
//     (center/radius/start_angle/end_angle, draw/lv_draw_arc.h:37-58) ; elle reste en argument pour le rectangle
//     et le texte.
//   * le rognage courant d'une layer est layer->_clip_area (draw/lv_draw.h:129-134) : équivalent de ctx->clip_area.
// Les petits wrappers ci-dessous gardent UN SEUL code pour les deux versions.
using uc_cible_t = lv_layer_t; // 9.6 : la layer du draw task en cours

// Le cadre d'un dessin : zone de la courbe à l'écran, points montrés, échelle verticale
struct uc_cadre_t
{
    uc_cible_t *ctx; // 9.6 : lv_layer_t* (avant : lv_draw_ctx_t*) — le nom reste « ctx » pour les 19 sites
    lv_coord_t x0, x1, haut, bas; // zone de la courbe (les graduations sont à gauche de x0, les heures sous bas)
    int premier, n;               // points montrés : premier .. premier + n - 1 (le dernier au bord droit)
    float lo, hi;                 // échelle verticale
    lv_coord_t x(int i) const { return (lv_coord_t)(x0 + (long)(i - premier) * (x1 - x0) / (n - 1)); }
    lv_coord_t y(float v) const { return (lv_coord_t)(bas - (LV_CLAMP(lo, v, hi) - lo) / (hi - lo) * (bas - haut)); }
};

// Récupérer LÀ OÙ dessiner depuis l'événement de dessin : lv_event_get_draw_ctx en 8.4, lv_event_get_layer en 9.6
// (core/lv_obj_event.h:124 ; l'événement DRAW_MAIN est bien envoyé avec la layer, core/lv_refr.c:131-133).
static inline uc_cible_t *uc_cible_du(lv_event_t *e)
{
    return lv_event_get_layer(e);
}

// L'objet qui a reçu l'événement : lv_event_get_target renvoie void* en 9.6 (lv_obj_t* en 8.4) — cast dans les deux cas.
static inline lv_obj_t *uc_objet_du(lv_event_t *e) { return (lv_obj_t *)lv_event_get_target(e); }

// Aire de rognage de ce qui est en train d'être dessiné (clip_area en 8.4, _clip_area en 9.6)
static inline const lv_area_t *uc_clip(const uc_cible_t *c)
{
    return &c->_clip_area;
}

// Deux aires ont-elles au moins un pixel commun ? (_lv_area_is_on en 8.4 ; lv_area_is_on est resté dans
// src/misc/lv_area_private.h en 9.6, donc plus visible d'une application : petit équivalent local)
static inline bool uc_sur(const lv_area_t *a, const lv_area_t *b)
{
    return a->x1 <= b->x2 && a->x2 >= b->x1 && a->y1 <= b->y2 && a->y2 >= b->y1;
}

// Enveloppe des deux aires (_lv_area_join en 8.4, idem src/misc/lv_area_private.h:63 en 9.6)
static inline void uc_rejoindre(lv_area_t *res, const lv_area_t *a, const lv_area_t *b)
{
    res->x1 = LV_MIN(a->x1, b->x1);
    res->y1 = LV_MIN(a->y1, b->y1);
    res->x2 = LV_MAX(a->x2, b->x2);
    res->y2 = LV_MAX(a->y2, b->y2);
}

// Arc : 8.4 passait centre/rayon/angles en arguments, 9.6 les met dans le descripteur
static inline void uc_arc(uc_cible_t *c, lv_draw_arc_dsc_t &d, const lv_point_t *centre, lv_coord_t rayon, int debut, int fin)
{
    d.center = *centre;
    d.radius = (uint16_t)rayon;
    d.start_angle = (lv_value_precise_t)debut;
    d.end_angle = (lv_value_precise_t)fin;
    lv_draw_arc(c, &d);
}

// Segment : idem, p1/p2 passent dans le descripteur en 9.6
static inline void uc_segment(uc_cible_t *c, lv_draw_line_dsc_t &d, lv_coord_t xa, lv_coord_t ya, lv_coord_t xb, lv_coord_t yb)
{
    d.p1.x = xa; d.p1.y = ya;
    d.p2.x = xb; d.p2.y = yb;
    lv_draw_line(c, &d);
}

// Texte : 9.6 met le texte dans le descripteur et l'aire en argument. « local » = le tampon est une variable de
// la pile : la task n'étant pas rendue tout de suite, LVGL doit le copier (text_local, src/draw/lv_draw_label.c:137).
static inline void uc_texte(uc_cible_t *c, lv_draw_label_dsc_t &d, const lv_area_t *zone, const char *t, bool local)
{
    d.text = t;
    d.text_local = local ? 1 : 0;
    lv_draw_label(c, &d, zone);
}

// Rectangle plein (fond, pastille, bande) : même appel dans les deux versions
static inline void uc_carre(uc_cible_t *c, const lv_draw_rect_dsc_t &d, const lv_area_t *zone)
{
    lv_draw_rect(c, &d, zone);
}

// Aplat d'une couleur opaque ou semi-transparente (9.6 : lv_draw_fill, draw/lv_draw_rect.h:178 ; 8.4 : lv_draw_rect
// avec bg_color/bg_opa — même résultat, le fond d'un rectangle sans bordure ni rayon)
static inline void uc_aplat(uc_cible_t *c, const lv_area_t &zone, lv_color_t couleur, lv_opa_t opa)
{
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.color = couleur;
    f.opa = opa;
    lv_draw_fill(c, &f, &zone);
}

// Aplat en dégradé vertical (9.6 : les deux couleurs aux deux bords de l'aire, draw/lv_grad.h:52-56 ; 8.4 : bg_grad).
// ATTENTION 9.6 : chaque palier porte aussi son opacité, laissée à 0 par lv_draw_fill_dsc_init -> il faut LA METTRE,
// sinon le dégradé est entièrement transparent.
static inline void uc_aplat_degrade(uc_cible_t *c, const lv_area_t &zone, lv_color_t haut, lv_color_t bas)
{
    lv_draw_fill_dsc_t f;
    lv_draw_fill_dsc_init(&f);
    f.grad.dir = LV_GRAD_DIR_VER;
    f.grad.stops_count = 2;
    f.grad.stops[0].color = haut;
    f.grad.stops[0].frac = 0;
    f.grad.stops[0].opa = LV_OPA_COVER;
    f.grad.stops[1].color = bas;
    f.grad.stops[1].frac = 255;
    f.grad.stops[1].opa = LV_OPA_COVER;
    lv_draw_fill(c, &f, &zone);
}

// Image déjà en mémoire (banc de l'anneau) : 8.4 appelait lv_draw_img avec un lv_img_dsc_t et un dsc de dessin ;
// 9.6 met LA SOURCE dans le descripteur (src, draw/lv_draw_image.h:31-33) et lv_draw_image ne prend plus que
// (layer, dsc, aire) (:141) — le décodage est fait par l'unité de dessin au moment du rendu. src = lv_image_dsc_t*
// (9.6, draw/lv_image_dsc.h:141-149) ou lv_img_dsc_t* (8.4), passé en void* pour garder UN SEUL code.
static inline void uc_image(uc_cible_t *c, const void *src, const lv_area_t *zone)
{
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = src;
    lv_draw_image(c, &d, zone);
}

// Largeur d'un texte pour la mise en page (hors rendu) : lv_txt_get_width en 8.4 ; en 9.6 l'alias
// api_map/lv_api_map_v8.h:172 renvoie à lv_text_get_width, qui n'est déclaré nulle part — même mesure avec
// lv_text_get_size (font/lv_text.h:117), comme l'a fait la vague B dans page_ballon.cpp:531-537.
static inline lv_coord_t uc_largeur_texte(const char *t, const lv_font_t *police)
{
    lv_point_t sz;
    lv_text_get_size(&sz, t, police, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return (lv_coord_t)sz.x;
}

// Couleurs et épaisseurs communes
#define UC_EXTERIEUR 0x7CC4F2
#define UC_GRILLE 0x1F252D

void uc_grille(const uc_cadre_t &c, float pas, const char *unite); // traits horizontaux + graduations « 20° » à gauche
// Repères d'heures : minute_fin = minute du jour du DERNIER point, minutes_par_point, un repère toutes les « ecart » minutes
void uc_heures(const uc_cadre_t &c, int minute_fin, int minutes_par_point, int ecart);
// Valeur principale en deux temps, pour que les autres couches passent entre les deux : le dégradé dessous D'ABORD,
// puis (après les couches secondaires) le trait épais et le point au bout
void uc_degrade(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur);
void uc_principale(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur);
void uc_trait(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur, lv_coord_t largeur);
void uc_escalier(const uc_cadre_t &c, const uc_serie_t &s, uint32_t couleur); // consigne qui change par paliers (pointillé)
void uc_niveau(const uc_cadre_t &c, float v, uint32_t couleur, bool seuil);      // consigne fixe (pointillé) ou seuil (fin)
void uc_bandes(const uc_cadre_t &c, bool (*actif)(int i), uint32_t couleur);    // périodes (chauffe) : bandes verticales
void uc_curseur(const uc_cadre_t &c, int i, const uc_serie_t &s, uint32_t couleur); // lecture au doigt : trait vertical + point

// Lecture au doigt : index du point sous le doigt (dans [premier, premier + n - 1])
int uc_point_du_doigt(const uc_cadre_t &c);
// Même correspondance x -> point SANS événement de doigt (hooks de banc : mesuré lot 4c, le calcul était
// recopié mot pour mot dans les trois pages — le code le disait lui-même).
int uc_point_sans_doigt(const uc_cadre_t &c, int x);
// ——— Machine à états du glissé de lecture (mesuré lot 4 (a) : trois pages la réécrivaient à l'identique).
// « scrub » = la g_scrub de la page (-1 : pas de curseur) ; « cadre » recalcule le cadre à chaque pas ;
// « leger_ok » dit si le chemin léger est permis (historique présent, etc.) ; « leger » repeint les
// colonnes du curseur et la bulle ; « complet » repeint la courbe entière.
struct uc_lecture_t
{
    int *scrub;
    uc_cadre_t (*cadre)();
    bool (*leger_ok)(int avant);
    void (*leger)(int avant);
    void (*complet)();
};
// Événements du doigt : PRESSED/PRESSING -> pas sous le doigt ; RELEASED/PRESS_LOST -> repos.
void uc_lecture_doigt(lv_event_t *e, const uc_lecture_t &L);
// Pas du banc (hook sans indev, route « /defiler ») : curseur à l'abscisse x ; x < 0 = relâcher avec
// garde « si curseur posé » (le banc peut relâcher sans pas avant, contrairement au doigt).
void uc_lecture_banc(int x_doigt, const uc_lecture_t &L);
// Index du min et du max d'un historique (sentinelle = pas de valeur), entre debut et debut + n - 1 ;
// -1 s'il n'y a aucune valeur (mesuré lot 4d : boucle recopiée dans ballon, poele et graphe).
void uc_min_max(const int16_t *valeurs, int debut, int n, int16_t sentinelle, int *imin, int *imax);
// Bande ±8 autour du curseur : trait 1 px + pastille rayon 6 (mesuré lot 4e : demi-largeur et commentaire
// recopiés mot pour mot dans les trois pages).
#define CURSEUR_BANDE 8
// Efface l'ancienne et la nouvelle place du curseur (index < 0 = rien à effacer de ce côté-là).
void uc_repeindre_bandes(const uc_cadre_t &c, lv_obj_t *o, int i_avant, int i_apres);
// Texte d'heure d'un point : « 14h05 » (« hier 23h40 » s'il est d'avant minuit)
void uc_texte_heure(char *buf, size_t n, int i, int dernier, int minute_fin, int minutes_par_point);

// Légende : pastille qui allume / éteint une couche (trait de la couleur de la couche, pointillé si consigne)
lv_obj_t *uc_pastille(lv_obj_t *parent, const char *nom, uint32_t couleur, lv_coord_t largeur, lv_event_cb_t cb, int couche);
void uc_pastille_etat(lv_obj_t *p, bool allumee);
// Bulle de lecture au doigt (cachée au départ) ; uc_bulle_placer la met au-dessus du point, dans la zone
lv_obj_t *uc_bulle(lv_obj_t *zone, lv_obj_t **texte);
void uc_bulle_placer(lv_obj_t *bulle, lv_coord_t x_point, lv_coord_t y, lv_coord_t largeur_zone);
// Écrire et placer la bulle au point i du cadre (mesuré lot 4b : six sites, deux par page).
// « repere » = l'objet dont on aligne le bord gauche (la zone de la page).
void uc_bulle_ecrire(lv_obj_t *bulle, lv_obj_t *texte, const char *msg, const uc_cadre_t &c, int i,
                     lv_obj_t *repere, lv_coord_t y, lv_coord_t largeur_zone);
// Chemin léger : idem, en effaçant l'ancienne place sur « graph » — en 9.6 lv_obj_set_pos n'invalidite que
// la NOUVELLE position (core/lv_obj_pos.c:68-90), c'est ici qu'on repeint sous l'ancienne.
void uc_bulle_poser(lv_obj_t *bulle, lv_obj_t *texte, const char *msg, const uc_cadre_t &c, int i,
                    lv_obj_t *repere, lv_coord_t y, lv_coord_t largeur_zone, lv_obj_t *graph);
