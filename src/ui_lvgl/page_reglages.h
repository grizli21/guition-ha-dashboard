// 22/10/2026 : page Réglages (maquette validée le 21/10) : une page qui défile, en 4 cartes (écran, accueil,
// maison, réseau). Reprend les réglages de l'ancienne page de configuration : luminosité jour / nuit, veille
// (« Jamais » = l'ancien Always On), pièce de l'écran, retour à l'accueil, vue caméra à la sonnette,
// éclairage terrasse, mélodie et volume du carillon, état du Wi-Fi.
#pragma once
#include <lvgl.h>

struct rg_reglages_t
{
    int lum_jour, lum_nuit; // %
    int veille;             // index dans 30 s / 1 min / 5 min / Jamais
    int piece;              // pièce affichée par défaut (index de th_pieces)
    int retour;             // index dans 30 s / 60 s / 2 min / Jamais
    bool camera;            // vue caméra de l'entrée quand on sonne
    // maison (24/10 : tenus par HA, dashboard_guition/maison/reglages, les mêmes sur tous les écrans)
    bool terrasse_porte;     // éclairage terrasse à l'ouverture de la porte, la nuit
    bool terrasse_mouvement; // éclairage terrasse sur mouvement, la nuit
    char melodie[16];        // sonnerie choisie, parmi melodies (liste de HA)
    char melodies[6][16];
    int nb_melodies;
    int volume; // % (carillon)
    // lecture seule
    char ssid[33];
    char ip[16];
    int rssi;
    // 24/10 : niveau de REPOS (après la veille), comme la production (backlight.day/night.low) ; lum_jour / lum_nuit = actif
    int lum_jour_repos, lum_nuit_repos;
};
extern rg_reglages_t rg_reglages;

// Appelé à chaque changement (à brancher : mémoire locale + MQTT) ; quoi = nom du réglage changé.
extern void (*rg_commande)(const char *quoi);
// Aperçu de luminosité : pendant qu'on tire un curseur, le niveau (%) que l'écran prend tout de suite (fin = false) ;
// au relâcher, le vrai niveau réglé, montré un instant (fin = true)
extern void (*rg_apercu)(int pct, bool fin);

void page_reglages_creer(lv_obj_t *zone);
void page_reglages_quitter();
void page_reglages_maj(); // Wi-Fi et valeurs reçues (sans effet si la page n'est pas affichée)
