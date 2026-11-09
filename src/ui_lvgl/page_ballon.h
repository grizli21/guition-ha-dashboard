// 22/10/2026 : page Eau chaude. 25/10 : refaite en 3 onglets d'après les maquettes validées
// (simu/maquettes_eau.cpp) : Ballon (cuve dessinée, haut = consigne, aucun réglage), Courbe (24 h : température, consigne,
// relais de chauffe, heures creuses, litres tirés par 2 h), Réglages (cuve graduée 30-70 °C avec le curseur de consigne,
// Arrêt / Auto / Forcé, régime éco). Modes = ceux de HA depuis le 16/10 : input_select.ballon_mode OFF / AUTO / FORCE et
// input_select.ballon_regime ECO (une seule montée la nuit) / NORMAL (thermostat libre en heures creuses).
#pragma once
#include <lvgl.h>
#include <stdint.h>

enum bc_mode_t
{
    BC_ARRET, // OFF : aucune chauffe automatique
    BC_AUTO,  // AUTO : heures creuses, selon le régime
    BC_FORCE  // FORCE : chauffe jusqu'à la consigne, puis retour AUTO (HA)
};

struct bc_etat_t
{
    float temp;     // température de l'eau (sensor.ballon_eau_chaude_temperature)
    float consigne; // input_number.ballon_consigne (40-70 dans HA)
    bc_mode_t mode;
    bool eco;     // régime ECO
    bool chauffe; // relais du tableau électrique
    // 25/10 : la journée et le direct (dashboard_guition/ballon/etat)
    int chaude_jour; // L d'eau chaude depuis minuit
    int froide_jour; // L entrés dans la maison (« eau totale »)
    float kwh_jour;
    int debit;     // L/min d'eau chaude en ce moment
    int puissance; // W (tore de l'Ecocompteur)
    int combles;   // °C
};
extern bc_etat_t bc_etat;

// 25/10 : historique 24 h tenu par HA (dashboard_guition/ballon/historique), [0] = il y a 24 h, [BC_POINTS-1] = maintenant
#define BC_POINTS 288
#define BC_VIDE INT16_MIN
struct bc_historique_t
{
    int16_t temp[BC_POINTS];     // °C x10 (BC_VIDE = pas de mesure)
    int16_t consigne[BC_POINTS]; // °C x10
    uint8_t litres[BC_POINTS];   // L d'eau chaude tirés pendant les 5 min
    uint8_t chauffe[BC_POINTS / 8];
    uint8_t creuses[BC_POINTS / 8]; // heures creuses
    int fin;                        // minute du jour du dernier point ; -1 = pas encore reçu
};
extern bc_historique_t *bc_hist_p; // 29/10 : en PSRAM (ui_psram.h)
#define bc_hist (*bc_hist_p)
static inline bool bc_bit(const uint8_t *b, int i) { return b[i >> 3] & (1 << (i & 7)); }

// Appelé quand l'utilisateur change le mode, le régime ou relâche le curseur de consigne (dashboard_guition/ballon/set).
extern void (*bc_commande)(const bc_etat_t &e);

void page_ballon_creer(lv_obj_t *zone);
void page_ballon_quitter();
void page_ballon_maj();            // nouvel état (sans effet si la page n'est pas affichée)
void page_ballon_historique_maj(); // nouvel historique

// 06/11 (chantier 2, audit e58325f) : outil de mesure du banc — un pas de la lecture au doigt sur
// la courbe (x du doigt, -1 = relâcher) et l'ouverture de la vue Courbe, sans geste injecté.
void page_ballon_banc_scrub(int x_doigt);
void page_ballon_banc_courbe();
