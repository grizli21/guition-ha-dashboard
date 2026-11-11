#include "ui_anneau.h"
#include "ui_courbe.h" // 05/11/2026 portage 9.6 : uc_arc() — centre/rayon/angles passent dans le descripteur en v9
#include "ui_theme.h"

#define FIN_ANNEAU 0x0A0E14 // segment du bas : un cran (RGB565) plus foncé que le fond 0x0F1216
#define ROUGE_CHAUD 0xFF5B4B

using anneau_cible_t = lv_layer_t; // 9.6 : la layer du draw task en cours (avant : lv_draw_ctx_t)

static lv_color_t degrade(float t)
{
    if (t < 0.5f)
        return lv_color_mix(ui_c(UI_ACCENT), ui_c(UI_FROID), (uint8_t)(t * 2 * 255));
    return lv_color_mix(ui_c(ROUGE_CHAUD), ui_c(UI_ACCENT), (uint8_t)((t - 0.5f) * 2 * 255));
}

void ui_anneau_dessiner(anneau_cible_t *ctx, const lv_point_t *centre, lv_coord_t diametre, lv_coord_t epaisseur,
                        float t_coupure, bool ouverture_vide)
{
    auto couleur = [&](float t)
    {
        lv_color_t c = degrade(t);
        return t > t_coupure ? lv_color_mix(c, ui_c(UI_FOND), 96) : c; // ~38 % (à 22 %, paliers en RGB565)
    };

    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.width = epaisseur;
    dsc.rounded = 0;
    const lv_coord_t r = diametre / 2;
    const int NB = 135; // 135 segments de 2° = 270° (5° faisait des marches visibles)
    for (int k = 0; k < NB; k++)
    {
        dsc.color = couleur((k + 0.5f) / NB);
        if (ouverture_vide && (k < 5 || k >= NB - 5)) // 10 derniers degrés de chaque bout : vers le fond
        {
            const int bord = k < 5 ? k : NB - 1 - k; // 0 au bout
            dsc.color = lv_color_mix(dsc.color, ui_c(UI_FOND), (uint8_t)(255 * (bord + 0.5f) / 5));
        }
        uint16_t deb = 135 + k * 2;
        uint16_t fin = deb + (k == NB - 1 ? 2 : 3); // 1° de recouvrement contre les fentes
        uc_arc(ctx, dsc, centre, r, deb % 360, fin % 360);
    }
    if (ouverture_vide)
        return;
    dsc.color = ui_c(FIN_ANNEAU);
    uc_arc(ctx, dsc, centre, r, 46, 134); // sans déborder sur le dégradé (sinon un trait sombre à 44-45°)
    const int PAS = 10;
    lv_color_t fin_rouge = couleur(1), fin_bleu = couleur(0);
    for (int i = 0; i < PAS; i++)
    {
        uint8_t part = (uint8_t)(255 * (i + 0.5f) / PAS);
        dsc.color = lv_color_mix(ui_c(FIN_ANNEAU), fin_rouge, part);
        uc_arc(ctx, dsc, centre, r, 45 + i, 45 + i + 2);
        dsc.color = lv_color_mix(ui_c(FIN_ANNEAU), fin_bleu, part);
        uc_arc(ctx, dsc, centre, r, 134 - i, 134 - i + 2);
    }
}
