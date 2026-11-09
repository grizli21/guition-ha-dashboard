// 22/10/2026 : page Croquettes (distributeur de croquettes pour animal) (maquettes validées le 21/10) : bol, stock, 4 repas (actif / heure /
// quantité), servir, tare ; sous-page « semaine » : 7 barres glissantes (jour 0 = aujourd'hui), ligne d'objectif =
// somme des repas actifs, points = repas servis / manqués, détail du jour touché.
// Données : distributeur ESPHome .209 (via HA -> dashboard/croquettes/etat).
#pragma once
#include <lvgl.h>

struct cq_repas_t
{
    int minute; // heure, en minutes depuis minuit
    int g;      // quantité
    bool actif;
};
struct cq_jour_t
{
    int g;     // consommé
    int repas; // repas servis
};

struct cq_etat_t
{
    float bol;          // g dans le bol
    int stock;          // % du réservoir
    int autonomie;      // jours
    cq_repas_t repas[4];
    cq_jour_t jours[7]; // [0] = aujourd'hui, [6] = il y a 6 jours
    int jour_semaine;   // aujourd'hui : 0 = lundi ... 6 = dimanche
    int maintenant;     // minutes depuis minuit
};
extern cq_etat_t cq_etat;

enum cq_action_t
{
    CQ_REPAS, // repas[idx] modifié (actif, heure ou quantité)
    CQ_SERVIR,
    CQ_TARE
};
extern void (*cq_commande)(cq_action_t action, int idx);

void page_croquettes_creer(lv_obj_t *zone);
void page_croquettes_quitter();
void page_croquettes_maj();
