// 18/10/2026 : page thermostat « 3 en 1 » (pièce + consigne + mode sur une seule page).
// L'anneau est la commande : on fait glisser le bouton pour régler la consigne (7-24 °C, pas de 0,5).
// Modes = préréglages réels des generic_thermostat de HA : hors-gel (away 8), éco (16), confort (20).
#pragma once
#include <lvgl.h>

enum th_mode_t
{
    TH_ARRET,
    TH_HORSGEL,
    TH_ECO,
    TH_CONFORT,
    TH_MANUEL // consigne libre (preset_mode « none » dans HA)
};

struct th_piece_t
{
    const char *nom;
    float temp;
    float consigne;
    th_mode_t mode;
    bool chauffe;
    float hum;    // reçus par MQTT (pas encore affichés : le graphe et l'alerte pile s'en serviront)
    int batterie; // %
    int signal;   // LQI Zigbee 0-255
};

#define TH_NB_PIECES 5
extern th_piece_t th_pieces[TH_NB_PIECES];
extern int th_piece_affichee;
extern float th_ext_temp;
extern int th_ext_hum;

// Appelé quand l'utilisateur change le mode ou relâche l'anneau (à brancher sur MQTT).
extern void (*th_commande)(int piece, th_mode_t mode, float consigne);

void page_thermostat_creer(lv_obj_t *zone);
void page_thermostat_quitter();
void page_thermostat_maj(); // à appeler quand les données reçues changent (sans effet si la page n'est pas affichée)

// 04/11/2026 : bancs de mesure seulement (simulateur, écran) ; aucun effet si la page n'est pas affichée
bool page_thermostat_centre_anneau(lv_point_t *c);
void page_thermostat_banc_glisse(float consigne, bool fin); // un pas du doigt sur l'anneau, sans commande vers HA
