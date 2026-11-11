// 23/10/2026 : images venues du réseau (miniatures de l'imprimante, pochettes et logos de Music Assistant) : les
// exceptions HTTP du contrat (l'écran ne parle qu'à HA, sauf caméra, images et Music Assistant : ui_musique). Une image téléchargée toutes les
// 300 ms (l'écran ne fige pas), décodée UNE fois et réduite à la taille d'affichage (moyenne des pixels) :
// JPEG (pochettes / logos de MA) = esp_new_jpeg (27/10 ; simulateur : JPEGDEC, comme la production) -> TRUE_COLOR ;
// PNG (miniatures Moonraker, seul format servi) = décodeur de LVGL -> TRUE_COLOR_ALPHA.
#pragma once
#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

// emplacements : chaque page a les siens
#define UI_IMG_IMPRIMANTE 0 // 12 fichiers × (48 px, 160 px) = 0..23
#define UI_IMG_POCHETTES 24 // 16 albums = 24..39
#define UI_IMG_LECTURE 40   // pochette du morceau en cours (page Musique)
#define UI_IMG_RADIOS 41    // 25/10 : logos des radios favorites de MA = 41..80
#define UI_IMG_NB 81

// Fourni par la plateforme (ESP32 : ui_images_reseau.cpp ; simulateur : fichiers locaux). Alloue *donnees (libéré par
// free), false si échec.
extern bool (*ui_images_telecharger)(const char *url, uint8_t **donnees, size_t *taille);
// 25/10 : téléchargement + décodage dans une tâche de fond (ESP32 : cœur 0) : la boucle ne fait plus que poser l'image
// prête (avant : ~250 + 90 ms figés par pochette, 16 pour un artiste). nullptr (simulateur) : tout de suite, dans la boucle
extern bool (*ui_images_en_fond)(void (*travail)(void *));

// Appelée quand l'image d'un emplacement est prête (img), ou abandonnée (nullptr)
typedef void (*ui_image_prete_t)(int emplacement, const lv_img_dsc_t *img);

// Demande l'image url (PNG) réduite à cote × cote pour cet emplacement. Même url déjà chargée : pret() tout de suite.
// possible : nullptr, ou une fonction qui dit si le serveur répond (imprimante éteinte : on n'attend pas pour rien).
void ui_images_demander(int emplacement, const char *url, int cote, ui_image_prete_t pret, bool (*possible)() = nullptr);
void ui_images_oublier(int emplacement); // libère l'image (la page ne doit plus l'afficher)
// vrai si ce descripteur EST celui d'un de nos emplacements (le pointeur passé aux callbacks pret). Une page qui
// oublie ses emplacements ne doit nullifier QUE ses pointeurs-là : le simulateur y met des icônes statiques, à lui.
bool ui_images_tient(const lv_img_dsc_t *desc);
