#include "ui_vignettes.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "ui_images.h"
#include "page_imprimante.h"
#include <stdio.h>
#include <string.h>

typedef char fichier_t[96];
static UI_PSRAM(fichier_t, g_fichier, PR_FICHIERS_MAX, nullptr); // 29/10 : en PSRAM (ui_psram.h) // fichier dont les miniatures sont demandées au rang k

// adresse : caractères réservés encodés (les noms d'OrcaSlicer contiennent des espaces)
static void url(char *dst, size_t n, const char *base, const char *fichier, int taille)
{
    size_t i = snprintf(dst, n, "%s", base);
    size_t lf = strlen(fichier);
    if (lf > 6 && strcmp(fichier + lf - 6, ".gcode") == 0)
        lf -= 6;
    for (size_t k = 0; k < lf && i + 4 < n; k++)
    {
        unsigned char c = (unsigned char)fichier[k];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            dst[i++] = (char)c;
        else
            i += snprintf(dst + i, n - i, "%%%02X", c);
    }
    snprintf(dst + i, n - i, "-%dx%d.png", taille, taille);
}

static bool allumee() { return pr_etat.etat != PR_ETEINTE; } // éteinte : Moonraker ne répond pas, on n'attend pas
// (Bissection du 07/11 qui avait coupé ce chemin : la PSRAM utilisée tombait de 6 930 068 à 1 100 076 o — c'est
// bien ici que les douze blocs de 360 448 o étaient pris. Cause corrigée dans ui_images.cpp, pas ici.)

static void prete(int e, const lv_img_dsc_t *img)
{
    int k = (e - UI_IMG_IMPRIMANTE) / 2, t = (e - UI_IMG_IMPRIMANTE) % 2;
    pr_fichier_t &f = pr_etat.fichiers[k];
    if (strcmp(f.fichier, g_fichier[k]) != 0)
        return; // la liste a changé entre-temps
    (t ? f.vignette : f.mini) = img;
    page_imprimante_maj();
}

void ui_vignettes_demander(int k, const char *base, const char *fichier)
{
    if (k < 0 || k >= PR_FICHIERS_MAX || !*fichier || !*base)
        return;
    pr_fichier_t &f = pr_etat.fichiers[k];
    if (strcmp(g_fichier[k], fichier) != 0)
    {
        f.mini = f.vignette = nullptr; // l'ancienne image n'est plus la bonne
        snprintf(g_fichier[k], sizeof g_fichier[k], "%s", fichier);
    }
    char u[256];
    url(u, sizeof u, base, fichier, 48);
    ui_images_demander(UI_IMG_IMPRIMANTE + 2 * k, u, 48, prete, allumee);
    url(u, sizeof u, base, fichier, 300);
    ui_images_demander(UI_IMG_IMPRIMANTE + 2 * k + 1, u, PR_VIGNETTE, prete, allumee);
}
