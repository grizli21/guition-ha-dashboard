// 18/10/2026 : coque de la nouvelle interface : carrousel d'icônes à droite (toujours visible) + zone de page 404×480.
// Un seul aiguilleur de pages (remplace page/lastPage, tactile_fonctions.h et les résolutions 405/480).
#pragma once
#include <lvgl.h>

struct ui_page_t
{
    const lv_img_dsc_t *icone;
    const char *nom;
    void (*creer)(lv_obj_t *zone); // construit la page dans la zone (vide)
    void (*quitter)();             // optionnel : oublier ses pointeurs avant que la zone soit vidée
    bool epinglee;                 // 26/10 : icône fixe en bas du rail, hors du défilement (la pile de l'écran sur batterie)
};

void ui_shell_creer(const ui_page_t *pages, int nb, int page_accueil);
void ui_shell_aller(int index);          // change de page (sans effet si c'est déjà elle)
int ui_shell_page();                      // page affichée
int ui_shell_nb();                          // nombre de pages (banc de mesure)
const char *ui_shell_nom(int index);        // nom de la page (banc de mesure, NULL hors plage)
void ui_shell_tache();                    // à appeler dans loop() : retours automatiques après inactivité
// 06/11 diagnostic page Réglages : arme les sondes de tas du rendu pendant les 3 images qui suivent une
// navigation (« sonder » = vrai tant qu'une image doit être sondée ; « image_sondee » en décrémente une).
bool ui_shell_sonder_dessin();
void ui_shell_image_sondee();
void ui_shell_retour_accueil(uint32_t ms); // délai de retour à la page d'accueil (0 = jamais)
// 26/10 : change l'icône d'une page (pile vivante) ; couleur au repos (0 = celle des autres icônes ; page affichée : accent)
void ui_shell_icone(int index, const lv_img_dsc_t *icone, uint32_t couleur);
