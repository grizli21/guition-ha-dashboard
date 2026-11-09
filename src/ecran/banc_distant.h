// 24/10/2026 (retour d'usage : « un module qui lit les pixels de l'esp et te rapatrie l'image et un module qui simule un appui
// tactile ») : sur le BANC seulement, un petit serveur HTTP (port 8080), qui ne répond qu'au PC défini par RESEAU_PC (secrets_wifi.h) :
//   GET /ecran                                    -> l'image affichée : 480×480 RGB565 brut, octets inversés (LV_COLOR_16_SWAP)
//   GET /toucher?x=..&y=..&ms=150                 -> un appui simulé (coordonnées LVGL)
//   GET /glisser?x0=..&y0=..&x1=..&y1=..&ms=400   -> un glissé simulé
//   GET /dodo?s=20                                -> 01/11 : veille profonde (écran sur batterie), réveil par la minuterie
//                                                    ou par le radar
//   GET /battement                                -> 01/11 : trames et octets reçus du radar, silences et relances,
//                                                    broche de réveil, MHz (sans compter comme une activité web)
// L'image est relue dans la mémoire d'image de la dalle, SEULEMENT à la demande (24/10 : la copie de chaque rendu dans un
// miroir en PSRAM, faite en permanence, a été retirée).
// Les appuis simulés passent par le même chemin que le vrai tactile (réveil de l'écran éteint compris).
// ⚠️ Un appui simulé agit comme un vrai doigt : les boutons commandent VRAIMENT les appareils.
// Outil PC : tools/banc.py (capture en PNG, toucher, glisser).
#pragma once
#include <lvgl.h>
#include <stdint.h>

// lire : n lignes de l'écran à partir de y, RGB565 octets inversés (tft.readRect dans ecran.cpp)
void banc_demarrer(void (*lire)(int y, int n, uint16_t *px));
void banc_tache();                                          // dans loop(), sur le même fil que LVGL
bool banc_toucher(bool *appuye, uint16_t *x, uint16_t *y); // dans touch_cb : true = un appui simulé décide (sinon le vrai tactile)
extern volatile uint32_t banc_dodo_s;    // 01/11 : veille demandée par /dodo (lue par energie_tache)
