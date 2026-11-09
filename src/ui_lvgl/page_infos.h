// 22/10/2026 : page Informations (maquette validée le 21/10) : une page qui défile, en 4 parties : cet écran
// (santé), liaisons (point vert / rouge), journal des derniers évènements, actions (redémarrer, effacer le
// journal, remise à zéro de la mémoire avec confirmation sur place).
#pragma once
#include <lvgl.h>

#define IF_NB_SYSTEME 10 // 25/10 : mémoire en deux cases (RAM, PSRAM) + adresse IP ; 31/10 : fréquence, émission Wi-Fi
#define IF_NB_LIAISONS 2 // ce que l'écran voit lui-même (Wi-Fi, HA) ; le reste : la liste des appareils de HA
#define IF_NB_APPAREILS 96
#define IF_NB_JOURNAL 12 // 24/10 (retour d'usage) : 12 lignes ; l’historique complet est dans le journal de bord de HA

struct if_valeur_t
{
    const char *cle;
    char valeur[32];
};
struct if_liaison_t
{
    const char *nom;
    char detail[40];
    bool ok;
};
struct if_ligne_t
{
    char heure[8];
    char texte[48];
    bool alerte; // en ambre
};

extern if_valeur_t if_systeme[IF_NB_SYSTEME];  // allumé depuis, redémarrage, RAM, PSRAM, processeur, fréquence, boucle la plus lente, émission Wi-Fi, version, IP
extern if_liaison_t if_liaisons[IF_NB_LIAISONS]; // Wi-Fi, Home Assistant

// 24/10 : appareils de la maison, tenus par HA (dashboard_guition/maison/appareils : ESPHome + Zigbee, sauf l'étiquette
// « Écran : ignorer ») ; hors ligne en tête, puis par pièce (ordre de HA)
struct if_appareil_t
{
    char nom[31];
    char piece[20];
    bool ok;
};
extern if_appareil_t *if_appareils; // IF_NB_APPAREILS cases // 29/10 : en PSRAM (ui_psram.h)
extern int if_nb_appareils;

enum if_action_t
{
    IF_REDEMARRER,
    IF_EFFACER_JOURNAL,
    IF_RAZ_MEMOIRE
};
extern void (*if_action)(if_action_t a);

void if_journal_ajouter(const char *heure, const char *texte, bool alerte); // la plus récente en haut
void if_journal_effacer();

void page_infos_creer(lv_obj_t *zone);
void page_infos_quitter();
void page_infos_maj(); // sans effet si la page n'est pas affichée
