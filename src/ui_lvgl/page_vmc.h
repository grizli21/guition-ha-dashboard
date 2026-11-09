// 22/10/2026 : page VMC (maquette validée le 21/10) : 2 VMC du Sonoff 4CH (.225, ESPHome), fan.vmc_maison et
// fan.vmc_sous_sol, 3 vitesses chacune (0 = arrêt, 1 = petite 50 %, 2 = grande 100 %). Le ventilateur tourne
// d'autant plus vite que la vitesse est haute ; l'humidité rappelle pourquoi la grande vitesse s'est mise toute seule.
#pragma once
#include <lvgl.h>

struct vmc_zone_t
{
    const char *nom;
    int vitesse;        // 0 arrêt, 1 petite, 2 grande
    int hum;            // humidité de référence (%)
    const char *lieu;   // d'où vient l'humidité affichée
    int seuil;          // seuil de la grande vitesse automatique (0 = pas d'automatisme)
    bool auto_en_cours; // l'automatisme humidité (ou un boost) tient la grande vitesse
};

#define VMC_NB 2
extern vmc_zone_t vmc_zones[VMC_NB];

// Appelé quand l'utilisateur choisit une vitesse (à brancher sur MQTT -> fan.set_percentage 0 / 50 / 100).
extern void (*vmc_commande)(int zone, int vitesse);

void page_vmc_creer(lv_obj_t *zone);
void page_vmc_quitter();
void page_vmc_maj(); // à appeler quand les données reçues changent (sans effet si la page n'est pas affichée)
