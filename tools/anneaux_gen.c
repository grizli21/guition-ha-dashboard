// 14/11 (ANNEAUX_PISTES, piste-flash) : génère les deux images d'anneau PRÉ-RENDUES à la compilation.
// Copie conforme de la boucle de anneau_image() (src/ui_lvgl/page_thermostat.cpp, commit a953dc0) :
// mêmes constantes, mêmes opérations flottantes dans le même ordre, mêmes packings que lvgl-9.6.0
// (lv_color.c : lv_color_hex garde 8 bits/canal ; to_u32 = 0xff<<24|r<<16|g<<8|b ; to_u16 =
// ((r&0xF8)<<8)+((g&0xFC)<<3)+((b&0xF8)>>3)). L'identité OCTET à OCTET avec le calcul de l'écran est
// VÉRIFIÉE sur l'écran (build « verif » : memcmp runtime contre const), pas supposée.
// Usage : gcc -O2 -ffp-contract=off -lm tools/anneaux_gen.c -o /tmp/anneaux_gen && /tmp/anneaux_gen > src/ui_lvgl/anneaux_const.c
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ANNEAU_D 320
#define ANNEAU_L 46
#define MIX_ATTENUE 96
#define MIX_SOMBRE 48
#define UI_FOND 0x0F1216
#define UI_FROID 0x4FC3F7
#define UI_ACCENT 0xFFB547
#define ROUGE_CHAUD 0xFF5B4B

static uint16_t color_to_u16(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) + ((g & 0xFC) << 3) + ((b & 0xF8) >> 3));
}

static void calc(int niveau, uint8_t *out)
{
    const int total = ANNEAU_D * ANNEAU_D;
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    // lueur_canaux(ui_c(X)) = les trois octets bruts de la constante (lv_color_hex 8 bits/canal)
    float fr = (UI_FOND >> 16) & 0xff, fg = (UI_FOND >> 8) & 0xff, fb = UI_FOND & 0xff;
    float cr[3], cg[3], cb[3];
    const uint32_t cs[3] = {UI_FROID, UI_ACCENT, ROUGE_CHAUD};
    for (int k = 0; k < 3; k++)
        cr[k] = (cs[k] >> 16) & 0xff, cg[k] = (cs[k] >> 8) & 0xff, cb[k] = cs[k] & 0xff;
    const float RO = ANNEAU_D / 2.0f, RI = ANNEAU_D / 2.0f - ANNEAU_L, m = (niveau ? MIX_ATTENUE : MIX_SOMBRE) / 255.0f;
    for (int y = 0; y < ANNEAU_D; y++)
        for (int x = 0; x < ANNEAU_D; x++)
        {
            const int i = y * ANNEAU_D + x;
            const float dx = x + 0.5f - RO, dy = y + 0.5f - RO, r = sqrtf(dx * dx + dy * dy);
            float a = atan2f(dy, dx) * 180 / (float)M_PI;
            if (a < 0)
                a += 360;
            float c0 = RO - r + 0.5f; if (c0 < 0) c0 = 0; if (c0 > 1) c0 = 1;
            float c1 = r - RI + 0.5f; if (c1 < 0) c1 = 0; if (c1 > 1) c1 = 1;
            const float cov = c0 * c1;
            if (cov <= 0 || (a > 45 && a < 135))
            {
                *(uint16_t *)(out + 2 * i) = 0;
                out[2 * total + i] = 0; // LV_OPA_TRANSP
                continue;
            }
            const float t = fmodf(a - 135 + 360, 360) / 270;
            const int k = t < 0.5f ? 0 : 1;
            const float u = t < 0.5f ? t * 2 : (t - 0.5f) * 2;
            const float r0 = cr[k] + (cr[k + 1] - cr[k]) * u, g0 = cg[k] + (cg[k + 1] - cg[k]) * u, b0 = cb[k] + (cb[k + 1] - cb[k]) * u;
            const float trame = (bayer[y & 3][x & 3] + 0.5f) / 16.0f;
            // canal() : (uint8_t)MIN(255, (int)(c*m + f*(1-m) + trame*pas)) — 8 bits, comme lv_color_make
            #define CANAL(c, f, pas) (uint8_t)(((int)(c * m + f * (1 - m) + trame * pas) < 255 ? (int)(c * m + f * (1 - m) + trame * pas) : 255))
            const uint8_t pr = CANAL(r0, fr, 8), pg = CANAL(g0, fg, 4), pb = CANAL(b0, fb, 8);
            const uint16_t v = color_to_u16(pr, pg, pb);
            memcpy(out + 2 * i, &v, sizeof v);
            out[2 * total + i] = (uint8_t)lroundf(cov * 255);
        }
}

static void emit(const char *nom, const uint8_t *b, int n)
{
    printf("const uint8_t %s[%d] = {\n", nom, n);
    for (int i = 0; i < n; i++)
    {
        if (i % 16 == 0)
            printf("    ");
        printf("%u%s", b[i], i + 1 < n ? "," : "");
        if (i % 16 == 15 || i + 1 == n)
            printf("\n");
    }
    printf("};\n");
}

int main(void)
{
    const int n = ANNEAU_D * ANNEAU_D * 3;
    uint8_t *a = (uint8_t *)malloc(n), *b = (uint8_t *)malloc(n);
    calc(0, a);
    calc(1, b);
    printf("// Généré par tools/anneaux_gen.c — ne pas éditer. %d o par anneau (RGB565 planaire 2 o/px + alpha 1 o/px).\n", n);
    printf("#include <stdint.h>\n");
    emit("anneau_sombre_dat", a, n);
    emit("anneau_attenue_dat", b, n);
    return 0;
}
