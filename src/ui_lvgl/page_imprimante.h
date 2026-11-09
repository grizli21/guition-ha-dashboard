// 22/10/2026 : page Imprimante 3D (maquette validée le 21/10), 3 vues : Suivi | Réglages | Fichiers.
// Suivi : anneau de progression 270° autour de la MINIATURE du slicer (PNG 300×300 de Moonraker), qui se REMPLIT
// de bas en haut avec les couches ; la caméra (flux MJPEG existant) seulement sur demande ; températures ; actions
// selon l'état (pause / reprendre / arrêter avec confirmation sur place / lancer / éteindre / allumer).
// 27/10 (retour d'usage) : 4 vues : Suivi (sans ligne d'actions : anneau et caméra agrandis, recentrés) | Réglages | Fichiers
// (un appui ouvre l'aperçu : grande miniature, détails, « Lancer ») | Marche (allumer, éteindre, pause, arrêter).
#pragma once
#include <lvgl.h>

enum pr_etat_t
{
    PR_ETEINTE,
    PR_PRETE,
    PR_IMPRESSION,
    PR_PAUSE
};

#define PR_FICHIERS_MAX 12
#define PR_VIGNETTE 190 // côté de la miniature dans l'anneau et l'aperçu (27/10 : 160 -> 190, anneau 236 -> 270)
struct pr_fichier_t
{
    char nom[48];     // nom court (« piege souris »)
    char fichier[96]; // nom Moonraker (« piege souris_0.2mm_PLA_Generic Klipper Printer_2h2m.gcode »), pour lancer
    char matiere[24]; // « PETG · 38 g »
    char date[12];
    int minutes;      // durée estimée
    const lv_img_dsc_t *vignette; // miniature du slicer (300 px de Moonraker, réduite à PR_VIGNETTE px au téléchargement)
    const lv_img_dsc_t *mini;     // petite miniature pour la liste : la 48×48 que fait déjà OrcaSlicer
};

struct pr_etat_imp_t
{
    pr_etat_t etat;
    int progression;         // %
    int couche, couches;
    int reste_min;           // minutes restantes
    int fin_minute;          // heure de fin (minutes depuis minuit)
    int buse, buse_c, plateau, plateau_c, caisson, caisson_c; // actuelles / consignes (°C, consigne 0 = arrêt)
    bool lumiere, filtration, chauffage, carte_mere;
    int puissance = -1; // 27/10 : W, wattmètre de la prise de l'imprimante (onglet Marche) ; -1 = pas encore reçue
    float pm1 = -1;     // 27/10 : particules fines PM1 de l'AQM (µg/m³), pastille du Suivi ; -1 = pas encore reçue
    pr_fichier_t fichiers[PR_FICHIERS_MAX];
    int nb_fichiers, travail, choix; // travail = fichier imprimé ; choix = fichier sélectionné
};
extern pr_etat_imp_t *pr_etat_p; // 29/10 : en PSRAM (ui_psram.h)
#define pr_etat (*pr_etat_p)

enum pr_action_t
{
    PR_ALLUMER,
    PR_ETEINDRE,
    PR_PAUSE_REPRISE,
    PR_ARRETER,
    PR_LANCER,     // arg = fichier
    PR_CONSIGNES,  // buse_c / plateau_c / caisson_c ont changé
    PR_INTER,      // arg = 0 lumière, 1 filtration, 2 chauffage, 3 carte mère
    PR_ORIGINE
};
extern void (*pr_commande)(pr_action_t action, int arg);

// la caméra : conteneur où le code du flux MJPEG pose son canevas (nullptr si la caméra n'est pas affichée)
lv_obj_t *page_imprimante_zone_camera();

void page_imprimante_creer(lv_obj_t *zone);
void page_imprimante_quitter();
void page_imprimante_maj();
