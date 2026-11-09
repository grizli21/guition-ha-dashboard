// 22/10/2026 : page Poêle (maquette validée le 22/10), 3 vues : Feu | Courbe | Réglages.
// Feu : anneau des fumées 0-500 °C (dégradé, repère de consigne) + anneau intérieur du corps 0-180 °C, état
// (éteint / allumage / régulation / recharger), brassage (ventilateur animé), tirage, modes Auto (PID) /
// Manuel (à distance) / Levier (volet à 100 %). Données : poêle ESPHome .226 via HA (number.poele_*).
#pragma once
#include <lvgl.h>

#define PL_HIST 144 // historique : 12 h, un point toutes les 5 min (le plus récent en dernier)
#define PL_VIDE (-1) // point sans mesure (poêle hors ligne, historique pas encore rempli) : la courbe le saute
#define PL_VIDE_TIRAGE 255

enum pl_feu_t
{
    PL_ETEINT,
    PL_ALLUMAGE,
    PL_REGULATION,
    PL_RECHARGER
};

struct pl_reglages_t
{
    int consigne;   // fumées visées (°C)
    int tirage_min; // %
    int recharge;   // alerte recharge sous ce seuil (°C)
    float kp, ki, kd;
    int limite; // limite intégrale
};

struct pl_etat_t
{
    int fumees, corps; // °C
    int tirage;        // % (sortie du PID en Auto)
    int mode;          // 0 Auto, 1 Manuel, 2 Levier
    int tirage_manuel; // %
    int brassage;      // 0 arrêt, 1..3
    pl_feu_t feu;
    pl_reglages_t r;
    int16_t h_fumees[PL_HIST], h_corps[PL_HIST];
    uint8_t h_tirage[PL_HIST];
    int h_fin; // minute du jour du dernier point (24/10 : repères en heures) ; -1 = inconnue
};
extern pl_etat_t *pl_etat_p; // 29/10 : en PSRAM (ui_psram.h)
#define pl_etat (*pl_etat_p)

// quoi : "mode", "tirage", "brassage" ou le nom du réglage changé
extern void (*pl_commande)(const char *quoi);

void page_poele_creer(lv_obj_t *zone);
void page_poele_quitter();
void page_poele_maj();

// 06/11 (chantier 2, audit e58325f) : outil de mesure du banc — un pas de la lecture au doigt sur
// la courbe (x du doigt, -1 = relâcher) et l'ouverture de la vue Courbe, sans geste injecté.
void page_poele_banc_scrub(int x_doigt);
void page_poele_banc_courbe();
