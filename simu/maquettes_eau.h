// 25/10/2026 (retour d'usage : « eau chaude, fais-moi des maquettes avec suivi du compteur d'eau ») : trois maquettes de la
// page Eau chaude, dessinées par-dessus la zone de page (le bandeau d'icônes reste visible), avec les VRAIES valeurs de
// HA du 25/10 matin. Simulateur seulement : rien n'est branché dans le firmware.
#pragma once
#include <lvgl.h>

// v = 1 (ballon dessiné), 2 (anneau + compteurs), 3 (douches disponibles) ; renvoie l'objet à supprimer après capture
lv_obj_t *maquette_eau(int v);
// 25/10 (retour d'usage : « 3 pages, mais un ballon avec un curseur pour régler la consigne au lieu de l'anneau ») :
// onglet 0 Ballon, 1 Courbe, 2 Réglages, 3 Ballon pendant la chauffe (résistance rouge)
lv_obj_t *maquette_eau_onglet(int onglet);
