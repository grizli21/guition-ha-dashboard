// 23/10/2026 : miniatures des fichiers de l'imprimante (PNG du slicer, servis par Moonraker), chargées par ui_images :
// 48 px pour la liste (pr_etat.fichiers[k].mini), 300 px réduite à PR_VIGNETTE pour l'anneau (.vignette).
#pragma once

// base = adresse du dossier .thumbs/ (donnée par HA), fichier = nom Moonraker (« x.gcode »). Sans effet si ce fichier est
// déjà chargé au rang k.
void ui_vignettes_demander(int k, const char *base, const char *fichier);
