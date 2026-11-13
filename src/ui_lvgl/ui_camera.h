// 26/10/2026 : caméras : l'imprimante (page Imprimante, bouton « Caméra ») et l'entrée (sonnette). 28/10 : flux MJPEG de
// go2rtc sur le NAS ; images lues en continu et décodées en fond, écrites directement dans la mémoire d'image (ESP32).
// Rafraîchie tant que la zone est affichée ; arrêt tout seul quand la zone disparaît (changement de page ou de vue).
#pragma once
#include <lvgl.h>

#ifndef UI_CAM_IMPRIMANTE // 27/10 : remplaçable à la compilation (banc : source fixe rejouée depuis le PC, mesures A/B)
// Caméra au-dessus de l'imprimante (sous-flux recadré par go2rtc), en FLUX MJPEG : 384 x 304 aux bonnes proportions pour la
// zone 375 x 300 de la page Imprimante, ~30 Ko par image, préchargé ; ≈ 18,7 images/s mesurées sur un écran
// (13/11, 558 et 561 images en 30 s, décodage 52 ms — la source est réglée à 20/s et l'écran la suit). ADAPTER : hôte et port de ton go2rtc.
#define UI_CAM_IMPRIMANTE "http://192.168.1.12:1984/api/stream.mjpeg?src=imprimante_ecran"
#endif
// 08/11/2026 (retour d'usage) : la seconde caméra de l'imprimante, la face du PLATEAU (caméra USB « bed » de Klipper, recadrée
// au centre par go2rtc sur le NAS). 384 x 304, 14 à 29 Ko par image selon la scène et l'éclairage,
// ≈ 14,4 images/s mesurées sur un écran (13/11, 430 et 433 images en 30 s, décodage 39 ms, source autour de 14-15/s) : même taille et même format que
// imprimante_ecran, préchargée. Mêmes remarques que ci-dessus : le recadrage de la page (375 x 300) prend 4 px de chaque
// côté et 2 px en haut et en bas, sans mise à l'échelle (ui_camera.cpp:434).
#ifndef UI_CAM_PLATEAU // remplaçable à la compilation, comme la caméra du caisson (banc : source fixe rejouée)
#define UI_CAM_PLATEAU "http://192.168.1.12:1984/api/stream.mjpeg?src=plateau_ecran"
#endif
// 28/10 : l'entrée (caméra Terrasse) passe par go2rtc sur le NAS, en FLUX MJPEG : 480 x 480 aux bonnes
// proportions (sous-flux recadré sur la puce graphique), ~28 Ko par image, préchargée (1re image en 0,03 s)
#define UI_CAM_ENTREE "http://192.168.1.12:1984/api/stream.mjpeg?src=terrasse_carre"

// plateforme (ESP32 : ui_camera_esp.cpp) : lit l'image suivante du flux dans buf (max octets), par une connexion gardée
// ouverte, Nagle coupé ; simulateur : images locales ; nullptr : ui_images_telecharger
#include <stddef.h>
#include <stdint.h>
// canal : la connexion (0 = chaîne principale, 1 = seconde chaîne, 26/10), chacune gardée ouverte
extern bool (*ui_camera_lire)(int canal, const char *url, uint8_t *buf, size_t max, size_t *n);
// plateforme : lance travail dans la tâche de fond PERMANENTE n° canal de la caméra (ESP32 : 0 = décodage, cœur 1 ;
// 1 = lecture continue, cœur 0) ; nullptr : ui_images_en_fond, une image à la fois
extern bool (*ui_camera_lancer)(int canal, void (*travail)(void *));
extern void (*ui_camera_pause)(); // plateforme : laisse la main ~1 ms (attente de la lecture en avance)
// 29/10 (panne « chambre 1 sourde », retour d'usage : « évidemment il faut couper les flux, la prod le faisait ») : plateforme :
// ferme les connexions de la caméra. Appelée par la boucle quand la caméra n'est plus affichée et que plus aucune tâche ne
// lit : un flux MJPEG laissé ouvert et non lu garde jusqu'à une fenêtre TCP de paquets, chacun dans un des 32 tampons de
// réception du Wi-Fi (chambre 1 : fenêtre de 32 Ko = ~22 tampons) ; nullptr : rien à fermer (simulateur)
extern void (*ui_camera_couper)();
// 08/11 (retour d'usage : « on ne libère pas et ça me gêne, ce n'est pas propre ») : plateforme : demande aux deux tâches de
// fond de la caméra de se rendre ELLES-MÊMES (vTaskDelete(NULL)) dès qu'elles ont fini l'image en cours : elles tiennent
// 8 192 + 4 096 octets de RAM interne de pile. Jamais tuées de l'extérieur : elles seraient détruites en plein recv() ou
// en plein décodage, la main sur un tampon. nullptr (simulateur) : pas de tâches de fond.
extern void (*ui_camera_arreter_taches)();
// true quand plus aucune des deux n'est en vie — seule condition à laquelle la boucle de l'écran a le droit de rendre
// les tampons de la caméra ; *octets (peut être nullptr) : les octets de pile rendus depuis la demande
extern bool (*ui_camera_taches_rendues)(size_t *octets);
// détail de la dernière lecture (ESP32) : attente du serveur jusqu'au début du JPEG, puis réception du JPEG, en ms
extern volatile uint32_t ui_camera_t_attente, ui_camera_t_reception;

// plateforme (ESP32 : src/ecran/ecran.cpp, qui tient l'écran) : ÉCRITURE DIRECTE dans la mémoire d'image, comme la production
// pour la sonnette (pushImageDMA depuis le décodeur) : plus de dessin de l'image par LVGL (80 ms par image mesurés sur la
// chambre 1). Lignes physiques de la mémoire d'image, rotation (convention Lovyan), synchronisation du cache ;
// nullptr (simulateur) : image LVGL
extern uint8_t **ui_camera_ecran_lignes;
extern int ui_camera_ecran_rotation;
extern void (*ui_camera_ecran_synchro)(void *debut, size_t octets);
// 27/10 : écran à deux mémoires d'image : la mémoire (cachée) où écrire l'image qui commence, puis l'image finie (la
// plateforme la montre à la fin de la trame) ; nullptr : ui_camera_ecran_lignes, écrite telle quelle
extern uint8_t **(*ui_camera_ecran_debut)();
extern void (*ui_camera_ecran_fin)();

void ui_camera_afficher(lv_obj_t *zone, const char *url); // l'image remplit la zone, centrée, recadrée ; nullptr : arrêt
void ui_camera_tache();                                    // dans la boucle
bool ui_camera_zone_directe(lv_area_t *zone);              // 27/10 : zone écrite directement en ce moment (écran à 2 mémoires)
// 27/10 (retour d'usage : « le bandeau fait des siennes quand la caméra est active ») : caméra en pause, ni lecture ni décodage,
// pendant que le rail défile (le décodage partage le cœur 1 et la PSRAM avec le rendu de LVGL)
void ui_camera_suspendre(bool oui);
// la zone de la caméra quand sa pause est effective (son image en cours finie) : l'écran à deux mémoires la recopie dans
// celle où LVGL va dessiner (sinon l'image alternerait entre les deux dernières : elle « tremblait » pendant le défilement)
bool ui_camera_zone_en_pause(lv_area_t *zone);
