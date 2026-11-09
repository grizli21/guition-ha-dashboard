// 26/10/2026 : réglages du radar LD2410S de l'écran sur batterie (maquette validée, simu/maquettes_batterie.cpp),
// ouverts depuis la page « Batterie et capteur » : les 16 zones (0,7 m chacune), seuil de détection / de maintien en
// colonnes, signal mesuré en direct par-dessus ; lire / envoyer au radar, réglage automatique (pièce vide). Le radar garde
// ses seuils lui-même (comme la production : relus au démarrage à froid). ESP32 : ui_radar_esp.cpp.
#pragma once
#include <lvgl.h>
#include <stdint.h>

#define RD_ZONES 16

struct rd_etat_t
{
    bool present;          // radar branché et répond
    bool presence;         // quelqu'un détecté
    uint16_t distance;     // cm
    bool pc;               // trames « PC » : énergie par zone disponible
    uint8_t energie[RD_ZONES]; // en dB (0-100), comme les seuils
    uint8_t detection[RD_ZONES]; // seuils de déclenchement, en dB (absolus, 10-95, firmware 1.1.1)
    uint8_t maintien[RD_ZONES];
    bool lu; // 31/10 : seuils du capteur connus (relus, ou gardés en mémoire RTC depuis) : l'onglet ne relit pas
    bool auto_en_cours;
    float progression; // %
    uint8_t depart_s;  // 28/10 : réglage auto demandé, secondes avant le départ (le temps de sortir), 0 = aucun
    uint8_t marge;     // 30/10 : dB ajoutés aux seuils de déclenchement après un réglage auto (réglée sur la page web)
    char message[48];  // dernier résultat (« seuils envoyés », « échec »…)
    // 26/10 : diag radar : l'écran ne s'endort pas (rétroéclairage éteint) et note chaque détection dans le journal de HA
    bool diag;
    uint32_t diag_fin;  // heure Unix de l'arrêt automatique
    uint16_t diag_nb;   // détections notées depuis le lancement
};
extern rd_etat_t rd_etat;

// branchés par la plateforme (ESP32 : ui_radar_esp.cpp)
extern void (*rd_lire)();
extern void (*rd_envoyer)();
extern void (*rd_auto)(uint16_t secondes); // 0 = annuler le départ en attente
extern void (*rd_diag)(bool actif);
extern void (*rd_marge)(uint8_t db); // 31/10 : marge du réglage auto (onglet Réglages de la page Batterie)

void page_radar_creer(lv_obj_t *zone); // 31/10 : onglet Radar de la page Batterie (sous ses onglets)
void page_radar_quitter();
void page_radar_maj(); // nouvelles mesures (sans effet si la page n'est pas affichée)

// 06/11 (chantier 2, audit e58325f) : outil de mesure du banc — un pas du glissé horizontal qui
// change la zone choisie (x du doigt). Mesurable seulement sur un écran sur batterie (.192, hors
// périmètre) : la page n'existe pas sur `.193`.
void page_radar_banc_scrub(int x_doigt);
