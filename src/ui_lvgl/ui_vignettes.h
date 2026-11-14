// 23/10/2026 : miniatures des fichiers de l'imprimante (PNG du slicer, servis par Moonraker), chargées par ui_images :
// 48 px pour la liste (pr_etat.fichiers[k].mini), 300 px réduite à PR_VIGNETTE pour l'anneau (.vignette).
#pragma once

// base = adresse du dossier .thumbs/ (donnée par HA), fichier = nom Moonraker (« x.gcode »). Sans effet si ce fichier est
// déjà chargé au rang k. 11/11 : rien n'est demandé tant que la page Imprimante n'est pas ouverte (les 24 images
// décodées pèsent 1,38 Mo de PSRAM et HA republie la liste toutes les 15 min) ; noms et base restent enregistrés.
void ui_vignettes_demander(int k, const char *base, const char *fichier);
void ui_vignettes_voir(int k, int grande); // 14/11 : la page demande SEULEMENT l'image qu'elle affiche (0 : 48 px, 1 : grande)
void ui_vignettes_renouer(); // entrée de page : re-demander les fichiers enregistrés (slots oubliés à la sortie)
void ui_vignettes_oublier(); // sortie de page : libérer les emplacements 0..23 et nullifier mini/vignette
