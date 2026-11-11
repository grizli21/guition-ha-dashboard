// 18/10/2026 : thème sombre commun de la nouvelle interface tout LVGL (maquettes du 18/10).
// Toutes les pages prennent leurs couleurs, polices et styles ICI : aucune couleur posée à la main ailleurs.
#pragma once
#include <lvgl.h>
#include "ui_langue.h" // 10/11/2026 : TXT(fr, en) et SEP_DEC — voir ce fichier

// --- Couleurs (0xRRGGBB) ---
#define UI_FOND 0x0F1216        // fond de page
#define UI_CARTE 0x1B2027       // cartes, boutons secondaires
#define UI_CARTE2 0x262C35      // boutons dans une carte
#define UI_BORD 0x2B323C        // bordures, séparateurs
#define UI_TEXTE 0xE8EAED       // texte
#define UI_TEXTE2 0x9AA3AE      // texte secondaire, icônes au repos
#define UI_TEXTE3 0x8B95A1      // légendes discrètes (31/10 : 6B7480 -> 8B95A1, contraste 3,5 -> 5,4:1 sur une carte)
#define UI_ACCENT 0xFFB547      // élément actif, consigne
#define UI_ACCENT_FOND 0x2A2216 // fond d'un élément actif du carrousel
#define UI_SUR_ACCENT 0x15181C  // texte posé sur l'accent
#define UI_VERT 0x5BD08A        // marche, validé
#define UI_ROUGE 0xFF6B5B       // alerte, danger
#define UI_FROID 0x4FC3F7       // froid, eau
#define UI_CHAUFFE 0xFF8A4B     // chauffe en cours
// Mêmes couleurs, écrites pour le balisage LVGL (« #RRGGBB texte # » des textes riches, bulles et
// journal). Changer les deux lignes ensemble (mesuré lot 3 : 10 sites les répétaient en dur).
#define UI_MARK_TEXTE3 "#8B95A1"
#define UI_MARK_ACCENT "#FFB547"
#define UI_MARK_VERT "#5BD08A"
#define UI_MARK_FROID "#4FC3F7"
#define UI_MARK_CHAUFFE "#FF8A4B"
#define UI_MARK_RELIS "#E5484D" // « Chauffe » (relais) des bulles du ballon : rouge soutenu, pas la constante UI_ROUGE
#define UI_MARK_EXT "#7CC4F2" // « Extérieur » des bulles du graphe : pas de constante UI_ (balisage seul)
#define UI_HUMIDITE 0xF2A0B4    // humidité (goutte du chauffage) : distincte du bleu de l'extérieur et du froid
#define UI_ETEINT 0x3A424D      // 02/11 : éléments éteints ou inactifs (interrupteur coupé, points, barres), gris des dessins
#define UI_POIGNEE 0xF4F5F6     // 02/11 : poignées des interrupteurs et curseurs, traits clairs

// --- Géométrie ---
#define UI_ECRAN 480
#define UI_RAIL_L 76                     // largeur du carrousel (à droite)
#define UI_ZONE_L (UI_ECRAN - UI_RAIL_L) // 404 : largeur de la zone de page
#define UI_ZONE_H UI_ECRAN               // 480 : plus de bandeau du bas (18/10)
#define UI_RAYON 14                      // rayon des boutons et cartes
#define UI_MARGE 12

// --- Polices (Montserrat avec accents) ---
LV_FONT_DECLARE(montserrat_sb_12_ui); // légende (latin-1 complet, 22/10 ; 31/10 : SemiBold, la Regular 12 était peu lisible)
LV_FONT_DECLARE(montserrat_14_ui);    // texte secondaire
LV_FONT_DECLARE(montserrat_sb_16_fr); // texte, boutons (+ symboles LVGL)
LV_FONT_DECLARE(montserrat_b_16_fr);  // 31/10 : boutons de mode du thermostat (gras)
LV_FONT_DECLARE(montserrat_sb_20_fr); // titres (+ symboles LVGL)
LV_FONT_DECLARE(montserrat_sb_30_fr); // valeurs de bandeau
LV_FONT_DECLARE(montserrat_b_30_fr);  // 27/10 : nom de la pièce du thermostat, en gras
LV_FONT_DECLARE(chevrons_36);         // 27/10 : flèches < > de l'en-tête du thermostat (36 au lieu de 30)
LV_FONT_DECLARE(montserrat_b_58_num); // grandes valeurs (chiffres seulement)
LV_FONT_DECLARE(montserrat_sb_22_fr); // valeurs secondaires (extérieur du thermostat)

#define UI_POLICE_LEGENDE (&montserrat_sb_12_ui)
#define UI_POLICE_PETITE (&montserrat_14_ui)
#define UI_POLICE_TEXTE (&montserrat_sb_16_fr)
#define UI_POLICE_TEXTE_GRAS (&montserrat_b_16_fr)
#define UI_POLICE_TITRE (&montserrat_sb_20_fr)
#define UI_POLICE_VALEUR (&montserrat_sb_30_fr)
#define UI_POLICE_NOM_PIECE (&montserrat_b_30_fr)
#define UI_POLICE_CHEVRONS (&chevrons_36)
#define UI_POLICE_GRANDE (&montserrat_b_58_num)
#define UI_POLICE_TEXTE_22 (&montserrat_sb_22_fr)

// --- Polices des valeurs chiffrées (température, consigne, heures, grammes, pourcentages) ---
// Par défaut : Montserrat Bold (SIL OFL 1.1, fournie). Option POLICE_3X5 : la police pixel « 3x5 » de K-Type, celle des
// captures du README. Elle est gratuite pour un usage personnel seulement et n'est donc PAS fournie : la télécharger
// soi-même, générer ses fichiers (tools/polices_ui.sh) puis ajouter -D POLICE_3X5 aux build_flags (voir
// tools/fonts/README.md).
#ifdef POLICE_3X5
LV_FONT_DECLARE(k3x5_99_num);         // température du thermostat (chiffres seulement)
LV_FONT_DECLARE(k3x5_49_num);         // consigne du thermostat (carré de 8 px ; jeu latin complet)
LV_FONT_DECLARE(k3x5_25);             // petit (carré de 4 px) : heures, grammes dans les listes
LV_FONT_DECLARE(k3x5_37);             // moyen (carré de 6 px) : valeurs de carte
#define UI_POLICE_TEMP (&k3x5_99_num)
#define UI_POLICE_TEMP_PETITE (&k3x5_49_num)
#define UI_POLICE_3X5_P (&k3x5_25)
#define UI_POLICE_3X5_M (&k3x5_37)
#else
LV_FONT_DECLARE(montserrat_b_94_num); // température du thermostat (chiffres seulement)
LV_FONT_DECLARE(montserrat_b_40_fr);  // consigne du thermostat, grandes valeurs de page
LV_FONT_DECLARE(montserrat_b_24_fr);  // petit : heures, grammes dans les listes
LV_FONT_DECLARE(montserrat_b_32_fr);  // moyen : valeurs de carte
#define UI_POLICE_TEMP (&montserrat_b_94_num)
#define UI_POLICE_TEMP_PETITE (&montserrat_b_40_fr)
#define UI_POLICE_3X5_P (&montserrat_b_24_fr)
#define UI_POLICE_3X5_M (&montserrat_b_32_fr)
#endif

static inline lv_color_t ui_c(uint32_t rgb) { return lv_color_hex(rgb); }

// --- Styles partagés ---
extern lv_style_t ui_st_nu;              // conteneur invisible : ni fond, ni bord, ni marge
extern lv_style_t ui_st_carte;           // carte : fond UI_CARTE, bord UI_BORD
extern lv_style_t ui_st_bouton;          // bouton secondaire
extern lv_style_t ui_st_bouton_presse;   // ... appuyé
extern lv_style_t ui_st_bouton_principal; // bouton d'action principal (accent)
extern lv_style_t ui_st_pastille_active; // pastille / bouton sélectionné (état CHECKED)

void ui_theme_init();

// --- Mises à jour gardées (24/10, figeage de l'écran de la cuisine : un style retiré / remis à chaque mouvement du doigt ;
// retour d'usage : « fais attention aux styles ») : mêmes arguments que LVGL, mais l'objet n'est touché que si la valeur CHANGE.
// LVGL, lui, redéfinit la propriété et redessine l'objet (texte : recopié, remis en page) même à valeur égale, et
// lv_obj_add_style d'un style déjà présent le retire puis le remet. Les pages les emploient partout à la place de LVGL.
void ui_style_text_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s);
void ui_style_bg_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s);
void ui_style_border_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s);
void ui_style_img_recolor(lv_obj_t *o, lv_color_t c, lv_style_selector_t s);
void ui_style_arc_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s);
void ui_style_bg_opa(lv_obj_t *o, lv_opa_t v, lv_style_selector_t s);
void ui_style_opa(lv_obj_t *o, lv_opa_t v, lv_style_selector_t s);
void ui_style_border_width(lv_obj_t *o, lv_coord_t v, lv_style_selector_t s);
void ui_style_text_font(lv_obj_t *o, const lv_font_t *f, lv_style_selector_t s);
void ui_label_texte(lv_obj_t *label, const char *texte);
// 27/10 : lv_obj_align seulement si l'alignement ou le décalage change (LVGL redessine l'objet à chaque appel, même identique ;
// l'alignement gardé suit de lui-même un changement de taille, ex. texte plus long) ; vu à l'audit des redessins
void ui_aligner(lv_obj_t *o, lv_align_t align, lv_coord_t x, lv_coord_t y);
// 27/10 : empreinte (FNV-1a) de ce qu'un dessin lit : il n'est redessiné que si elle change (audit des redessins) ;
// à enchaîner : ui_empreinte(b, nb, ui_empreinte(a, na))
uint32_t ui_empreinte(const void *donnees, size_t n, uint32_t h = 2166136261u);
void ui_label_texte_fmt(lv_obj_t *label, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// Nombre pour les écrans : « %.*f » (decimales = dec) puis point -> SEP_CAR (virgule en français), suffixe
// optionnel collé (unité). Quatre copies identiques vivaient dans les pages (mesuré lot 3, D5).
void ui_fmt_float(char *b, size_t n, float v, int dec, const char *suffixe = "");
void ui_style_ajouter(lv_obj_t *o, lv_style_t *style, lv_style_selector_t s); // rien s'il y est déjà
void ui_style_bg_grad_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s);
void ui_style_bg_grad_dir(lv_obj_t *o, lv_grad_dir_t v, lv_style_selector_t s);
void ui_style_shadow_width(lv_obj_t *o, lv_coord_t v, lv_style_selector_t s);
void ui_style_shadow_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s);
void ui_img_src(lv_obj_t *img, const void *src); // rien si c'est déjà cette image (LVGL la relit et la redessine)
void ui_cacher(lv_obj_t *o, bool cache);
// 25/10 (retour d'usage : « un caractère cassé sur un album de Stan Getz… un truc léger ») : rend un texte venu d'ailleurs (MA)
// affichable par nos polices (Latin-1 + quelques signes), sur place, sans jamais l'allonger : lettres d'Europe centrale ->
// lettre de base (ł -> l), Windows-1252 mal converti (U+0092…) -> ce qu'il voulait dire, cyrillique sosie du latin
// (« Sу » d'une étiquette) -> latin, contrôles et octets orphelins (texte coupé au milieu d'un « é ») retirés, le reste « ? »
void ui_texte_propre(char *s);          // rien s'il l'est déjà (LVGL redessine un objet « montré » déjà visible)
void ui_style_retirer(lv_obj_t *o, lv_style_t *style, lv_style_selector_t s); // rien s'il n'y est pas

// --- Fabriques ---
lv_obj_t *ui_conteneur(lv_obj_t *parent);                                      // invisible, non défilant
// 05/11 : scroll par bande — marque un conteneur défilant pour le chemin rapide de la librairie
// (patch lv_obj_scroll.c/lv_obj_pos.c, LV_OBJ_FLAG_USER_1). A APPELER A LA CREATION du conteneur,
// jamais pendant le geste (mesuré au prototype : une exclusion déclenchée en cours de geste ne défait
// pas les images déjà sautées — 84 images / 3,7 Mpx). Refuse et explique (ui_trace) si le style lu du
// conteneur casserait le décalage : fond non transparent, bordure, rayon, ombre, contour, barre de
// défilement. Renvoie true si accepté (drapeau posé).
bool ui_defilement_rapide(lv_obj_t *o);
// Conteneur « défilable » standard des pages : taille (x,y,w,h) + défilement vertical sans barre +
// garde du scroll par bande (statue ici, à la création — voir ui_defilement_rapide). Le flux flex et
// les marges intérieures se règlent sur place : ils diffèrent d'une page à l'autre (mesuré lot 3).
lv_obj_t *ui_conteneur_defilant(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *police, uint32_t couleur, const char *texte);
// 24/10 (harmonie) : nombre en police pixel + unité en police normale collée à droite (la police pixel n'a pas de vraies
// minuscules : « 118 G », « 62 GO ») ; *valeur = le libellé du nombre ; renvoie la rangée, à placer comme l'était le libellé
// 24/10 (harmonie) : onglets en haut de page (Poêle, Café, Musique, PC, Imprimante) : segment choisi en gris clair (l'ambre
// reste aux actions) ; donnees[i] (ou i) passé à cb ; boutons[i] reçoit chaque onglet (nullptr : pas besoin)
void ui_onglets(lv_obj_t *parent, const char *const *noms, int n, int choisi, lv_event_cb_t cb, const int *donnees, lv_obj_t **boutons);
lv_obj_t *ui_valeur_unite(lv_obj_t *parent, const lv_font_t *police, uint32_t couleur, const char *unite, lv_obj_t **valeur);
lv_obj_t *ui_bouton(lv_obj_t *parent, lv_coord_t l, lv_coord_t h);              // bouton secondaire vide
// 24/10 (retour d'usage : « les sliders bougent quand je veux faire glisser la page ») : curseur horizontal dans une page qui
// défile : un glissé vertical fait défiler la page (le curseur reste à sa valeur), seul un glissé horizontal le règle ;
// un simple appui ne change rien. À appeler AVANT d'ajouter ses propres évènements (il les arrête quand il refuse).
void ui_curseur_dans_page(lv_obj_t *curseur);
lv_obj_t *ui_icone(lv_obj_t *parent, const lv_img_dsc_t *src, uint32_t couleur); // icône A4 recolorée
// 02/11 (harmonie) : titre de section en petites capitales espacées (Réglages, Infos, Imprimante)
lv_obj_t *ui_titre_section(lv_obj_t *page, const char *txt); // marges de 4 px à gauche et en haut
// 02/11 (harmonie) : couleurs d'un interrupteur (lv_switch) : éteint gris, allumé vert, poignée claire ; la taille reste
// celle de chaque page
void ui_interrupteur_couleurs(lv_obj_t *sw);
// 02/11 (harmonie) : hélice qui tourne (Air, VMC, Poêle) : un tour en tour_ms, depuis l'angle où elle est (pas d'à-coup) ;
// 0 = arrêtée, remise droite. À n'appeler que si la vitesse change (sinon l'animation repart à chaque mise à jour)
void ui_helice_tourner(lv_obj_t *img, uint32_t tour_ms);
void ui_helice_arreter(lv_obj_t *img); // son animation seule (avant la destruction de la page)
