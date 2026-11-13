// 11/11/2026 : page « IA » séparée, dans le rail juste après PC (maquette validée le 10/11). Une seule carte : adresse du compte Claude et jauges « 5 heures » /
// « Semaine » en haut, scène pixel (Clawd, ciel jour/nuit selon ui_maison.nuit) derrière, bande de terre =
// panneau d'activité en bas (état du PC-agent, tokens, sessions/prompts/outils, histogramme 30 tranches).
// Données : dashboard_guition/pc/ia (INCHANGÉ mais UN SEUL compte, le premier affiché) et le nouveau
// dashboard_guition/pc/ia_activite (retenu). Message absent ou vieux de plus de 2 min : « au repos »,
// derniers chiffres gardés. L'onglet « IA » de la page PC (31/10) est retiré : c'est cette page qui le remplace.
#pragma once
#include <lvgl.h>

#define IA_HISTO 30 // tokens produits par tranche de 10 min sur 5 h, la plus ancienne d'abord

// dashboard_guition/pc/ia : le compte Claude en cours (le format garde « comptes », l'écran lit le premier)
struct ia_compte_t
{
    char nom[64];                    // l'adresse du compte
    int h5, semaine;                 // % utilisés
    uint32_t h5_reset, semaine_reset; // heure Unix de la réinitialisation (0 = pas de fenêtre en cours)
    uint32_t maj;                    // heure Unix de la dernière lecture par l'agent du PC
    bool recu;                       // au moins un message reçu depuis le démarrage de l'écran
};

// dashboard_guition/pc/ia_activite (11/11) : ce que fait l'agent Claude sur le PC, et ce qu'il a produit
struct ia_activite_t
{
    char etat[16];  // « reflechit » | « attente » | « repos » (vide = jamais reçu)
    char alerte[16]; // « autorisation » | « question » | (vide = aucune) — « autorisation » fait 12 lettres + NUL
    uint32_t tokens; // tokens produits au total
    int outils, prompts, sessions;
    uint32_t histo[IA_HISTO]; // tokens par tranche de 10 min, la plus ancienne d'abord, la dernière = en cours
    uint32_t maj;             // heure Unix du message (0 reçu = celle de la réception écran)
    bool recu;
};

// en PSRAM (ui_psram.h) : inscrits avant setup(), comme pc_etat
extern ia_compte_t *ia_compte_p;
extern ia_activite_t *ia_activite_p;
extern uint32_t page_ia_heure_test; // 12/11/2026 : simulateur, heure imposée au ciel animé (0 = horloge)
#define ia_compte (*ia_compte_p)
#define ia_activite (*ia_activite_p)

void page_ia_creer(lv_obj_t *zone);
void page_ia_quitter();
void page_ia_maj(); // appelée par ui_mqtt (pc/ia, pc/ia_activite) et par le timer de la page : sur place
