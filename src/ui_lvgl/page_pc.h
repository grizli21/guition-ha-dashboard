// 22/10/2026 : page PC (maquette validée le 22/10) : l'ancien écran « btop » (dépôt « monitoring cachyOS guition
// 4848s040 ») devenu une page. Données de host/btop_agent.py, à faire passer par MQTT (dashboard/pc/...) au lieu
// de l'UDP vers une seule IP, et à compléter : 3 cartes graphiques (tableau « gpus »), plus une seule.
// Vues : Système | GPU | Processus (« PC en veille » + Réveiller (WoL de HA) quand plus rien n'arrive).
// 11/11/2026 : l'onglet « IA » des comptes Claude (ajouté le 31/10) est devenu la page séparée « IA » du rail
// (page_ia.h/.cpp) ; pc_etat ne porte plus les comptes, et PC_IA/pc_ia_t quittent cet en-tête.
#pragma once
#include <lvgl.h>

#define PC_COEURS 16
#define PC_GPUS 3
#define PC_PROCS 12
#define PC_HIST 24
#define PC_HIST_CPU 150 // 31/10 : 5 min de charge CPU, un point toutes les 2 s (moyenne des états reçus, ~1 par seconde)
#define PC_BLOCS 8      // 31/10 : processus (fusionnés par nom) dans la VRAM d'une carte

// 31/10 : un bloc de la VRAM, nommé comme le hub (« Whisper (STT) ») mais avec le modèle complet d'un llama (son .gguf)
struct pc_bloc_t
{
    char nom[48];
    uint16_t mo;
    uint32_t couleur; // celle du hub
};
struct pc_gpu_t
{
    char nom[12];
    int charge, temp, puissance; // %, °C, W
    float vram, vram_total;      // Go
    // 08/11 (maquette GPU) : état d'alimentation et fréquences publiés par telemetrie-pc.
    // « ps » vide et fréquences à 0 ou moins quand le champ est absent, null ou jamais reçu ; maxima à 0 = inconnu.
    char ps[5];                  // "P0".."P12"
    int f, fm, f_max, fm_max;    // MHz (GPU, mémoire) et maxima de la carte
    pc_bloc_t blocs[PC_BLOCS];   // du plus gros au plus petit
    int nb_blocs;
};
struct pc_proc_t
{
    char nom[32], role[28];
    float cpu, mem, vram; // %, Go, Go
};

// 11/11/2026 : pc_ia_t (comptes Claude, 31/10) est parti dans page_ia.h avec l'onglet « IA » devenu page.

struct pc_etat_t
{
    bool en_veille;
    char derniere_nouvelle[8]; // « 21:47 »
    int cpu, freq_mhz, cpu_temp, cpu_watts;
    int coeurs[PC_COEURS];
    float ram, cache, ram_total; // Go
    int swap;                     // %
    pc_gpu_t gpus[PC_GPUS];
    float net_bas, net_haut;   // Mo/s
    float disque_l, disque_e;  // Mo/s
    uint8_t h_net[PC_HIST], h_disque[PC_HIST]; // historiques normalisés 0..100
    uint8_t h_cpu[PC_HIST_CPU]; // %, le plus récent à droite
    uint32_t n_cpu;             // points ajoutés depuis le démarrage (le graphe n'est redessiné que s'il change)
    char allume_depuis[16];
    pc_proc_t procs[PC_PROCS];
    int nb_procs;
};
extern pc_etat_t *pc_etat_p; // 29/10 : en PSRAM (ui_psram.h)
#define pc_etat (*pc_etat_p)

enum pc_action_t
{
    PC_REVEILLER,
    PC_VEILLE
};
extern void (*pc_commande)(pc_action_t a);

void page_pc_creer(lv_obj_t *zone);
void page_pc_quitter();
void page_pc_maj();
