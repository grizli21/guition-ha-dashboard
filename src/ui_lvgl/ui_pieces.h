// Choix de pièce partagé entre les pages Chauffage et Graphes (lot 5 de l'audit) : la table des 5 pièces
// vient de HA (th_pieces, reçue par thermostat/) et l'en-tête « flèches + nom + points + bande » est construit ici.
// Comportement (d'abord sur la page Chauffage, étendu aux Graphes sur retour d'usage — seul changement de
// toucher autorisé du chantier) : toute la bande d'en-tête choisit la pièce, à l'APPUI ; moitié gauche =
// précédente, moitié droite = suivante, un appui sur la rangée de points saute à ce point ; les flèches ne
// sont que des repères.
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
extern int th_piece_affichee; // pièce montrée par la page Chauffage

struct ui_tete_piece_t
{
    lv_obj_t *nom;
    lv_obj_t *points[TH_NB_PIECES];
    void (*cb)(bool saut, int valeur, void *donnees); // rempli par ui_tete_piece
    void *donnees;
};

// Construit l'en-tête dans « zone ». cb(bool saut, int valeur, donnees) : saut = appui sur un point
// (valeur = index du point), sinon valeur = direction (-1 / +1). L'état de la pièce appartient à la page
// (th_piece_affichee pour chauffage, copie propre pour Graphes) : c'est elle qui applique. La structure
// « tete » doit vivre aussi longtemps que la page (statique de page).
void ui_tete_piece(lv_obj_t *zone, ui_tete_piece_t *tete,
                   void (*cb)(bool saut, int valeur, void *donnees), void *donnees);
// Nom de la pièce et points : celui de la pièce montrée en blanc, les autres éteints.
void ui_tete_piece_maj(const ui_tete_piece_t *tete, int piece);
