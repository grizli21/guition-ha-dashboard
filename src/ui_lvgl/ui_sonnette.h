// 27/10/2026 : vue caméra de l'entrée quand on sonne, comme la production (loop.h + lovyan_doorbell_cam.h) : plein écran,
// rien par-dessus (retour d'usage : « plein écran nu »), image 640 x 480 recadrée au centre ; fermée au toucher, à l'ouverture
// de la porte (sonne:0) ou au bout de 60 s ; seulement si le réglage « Caméra à la sonnette » est actif.
// Message : dashboard_guition/sonnette/etat {"sonne":1, "depuis":…} (HA 1791000000040 / 041).
#pragma once

void ui_sonnette_recevoir(bool sonne);
bool ui_sonnette_affichee();
