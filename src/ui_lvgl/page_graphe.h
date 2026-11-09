// 21/10/2026 : page graphe d'une pièce, 24 h GLISSANTES (maintenant au bord droit).
// Couches : pièce (trait épais + dégradé), consigne (escalier pointillé), extérieur (trait fin),
// périodes de chauffe (bandes), humidité (bande séparée, sa propre échelle). Glisser = lire les valeurs.
#pragma once
#include <lvgl.h>
#include "page_thermostat.h" // TH_NB_PIECES, th_pieces[].nom

#define GR_POINTS 288 // 24 h, un point toutes les 5 min
#define GR_VIDE INT16_MIN

// Historique d'une pièce, dans l'ordre chronologique : [0] = il y a 24 h, [GR_POINTS-1] = maintenant.
struct gr_historique_t
{
    int16_t temp[GR_POINTS]; // °C x10 (GR_VIDE = pas de mesure)
    int16_t cons[GR_POINTS]; // °C x10
    uint8_t hum[GR_POINTS];  // % (0 = pas de mesure)
    uint8_t chauffe[GR_POINTS / 8];
};
// 24/10 : en PSRAM (≈ 8 Ko), réservés une fois par page_graphe_preparer() ; remplis par les messages retenus de HA
// (thermostats/<pièce>/historique, maison/historique) dès qu'ils arrivent, page affichée ou non
extern gr_historique_t *gr_pieces; // [TH_NB_PIECES]
extern int16_t *gr_ext;            // [GR_POINTS] °C x10, commun à toutes les pièces
extern uint16_t gr_minute_maintenant; // minute du jour du dernier point (repères d'heures, bulle)

static inline bool gr_chauffe(const gr_historique_t &h, int i) { return h.chauffe[i >> 3] & (1 << (i & 7)); }

void page_graphe_preparer(); // avant tout accès à gr_pieces / gr_ext (sans effet la 2e fois)
void page_graphe_creer(lv_obj_t *zone);
void page_graphe_quitter();
void page_graphe_maj(); // nouvelles données
// 06/11 (mesure du « glissé horizontal dans le graph ») : un pas du doigt sur la courbe,
// sans indev ni commande vers HA — même chemin que graphe_cb (PRESSED/PRESSING). x_doigt < 0 = relâcher.
void page_graphe_banc_scrub(int x_doigt);
// 07/11 (garde-fou du calque) : changement de pièce par le code même de piece_cb — le verrou du banc
// refuse tout appui synthétique sur les boutons de pièce. sens = -1 (gauche) ou +1 (droite).
void page_graphe_banc_piece(int sens);
