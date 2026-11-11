#include "ui_diag.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "ui_images.h"
#ifdef ARDUINO
#include <esp_heap_caps.h>
#include <esp_jpeg_dec.h> // 27/10 : esp_new_jpeg (Espressif, fourni avec l'Arduino 3), comme la caméra
#else
#include <JPEGDEC.h> // simulateur (esp_new_jpeg n'existe que pour l'ESP)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// décodeur PNG de LVGL (lodepng, compilé en C ; son en-tête tire du C++ quand on l'inclut ici : on déclare la seule fonction)
extern "C" unsigned lodepng_decode32(unsigned char **out, unsigned *w, unsigned *h, const unsigned char *in, size_t insize);

#define UI_IMG_ALLOC(n) lv_malloc(n)
#define UI_IMG_LIBERE(p) lv_free(p)
#define UI_IMG_ALPHA_BYTE 3 // o/px (RGB565 + alpha) ; en 8.4 LV_IMG_PX_SIZE_ALPHA_BYTE = 3 aussi
typedef uint16_t px565_t;   // RGB565 sur 2 octets (le lv_color_t 9.6 en fait 3)

bool (*ui_images_telecharger)(const char *url, uint8_t **donnees, size_t *taille) = nullptr;
bool (*ui_images_en_fond)(void (*travail)(void *)) = nullptr;

struct image_t
{
    char url[224];
    int cote;
    bool attente;
    ui_image_prete_t pret;
    bool (*possible)();
    lv_img_dsc_t img;
    uint8_t *px; // image convertie (couleur de l'écran + alpha), allouée par lv_mem_alloc
};
static UI_PSRAM(image_t, g_img, UI_IMG_NB, nullptr); // 29/10 : en PSRAM (ui_psram.h)
static lv_timer_t *g_timer;

static void liberer(image_t &m)
{
    if (!m.px)
        return;
    UI_IMG_LIBERE(m.px);
    m.px = nullptr;
}

// PNG -> cote × cote en TRUE_COLOR_ALPHA, réduit par moyenne des pixels (pondérée par l'alpha : pas de liseré sombre)
static uint8_t *convertir(const uint8_t *png, size_t n, int cote, lv_img_dsc_t &dsc)
{
    unsigned w = 0, h = 0;
    // En 9.6, lodepng_decode32 ne rend pas un tampon de pixels mais un lv_draw_buf_t * (pixels dans ->data,
    // stride dans ->header.stride) ; descripteur et pixels sont DEUX allocations distinctes. La libération qui
    // rend les deux est lv_draw_buf_destroy.
    lv_draw_buf_t *buf = nullptr;
    if (lodepng_decode32((unsigned char **)&buf, &w, &h, png, n) || !w || !h)
    {
        lv_draw_buf_destroy(buf); // rend le descripteur ET les pixels ; sans effet si buf est nul
        return nullptr;
    }
    const unsigned char *rgba = (const unsigned char *)buf->data;
    const unsigned pas = buf->header.stride ? buf->header.stride : 4u * w; // ARGB8888 : 4 o/px, stride aligné
#define SRC(sy, sx) (rgba + (size_t)(sy) * pas + (size_t)(sx) * 4)
    uint8_t *px = (uint8_t *)UI_IMG_ALLOC(cote * cote * UI_IMG_ALPHA_BYTE);
    if (px)
        for (int y = 0; y < cote; y++)
            for (int x = 0; x < cote; x++)
            {
                unsigned x0 = x * w / cote, x1 = LV_MAX((x + 1) * w / cote, x0 + 1);
                unsigned y0 = y * h / cote, y1 = LV_MAX((y + 1) * h / cote, y0 + 1);
                unsigned r = 0, g = 0, b = 0, a = 0, nb = 0;
                for (unsigned sy = y0; sy < y1; sy++)
                    for (unsigned sx = x0; sx < x1; sx++)
                    {
                        const unsigned char *s = SRC(sy, sx);
                        r += s[0] * s[3];
                        g += s[1] * s[3];
                        b += s[2] * s[3];
                        a += s[3];
                        nb++;
                    }
                lv_color_t c = a ? lv_color_make(r / a, g / a, b / a) : lv_color_black();
                // RGB565A8 planaire 9.6 : plan couleur (2 o/px, RGB565 NATIF via lv_color_to_u16), puis masque A8
                uint16_t v = lv_color_to_u16(c);
                memcpy(px + 2 * (y * cote + x), &v, 2);
                px[2 * cote * cote + y * cote + x] = (uint8_t)(a / nb);
            }
    lv_draw_buf_destroy(buf); // le descripteur et les pixels, cette fois (voir le bloc plus haut)
#undef SRC
    if (!px)
        return nullptr;
    memset(&dsc, 0, sizeof dsc);
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC; // sans magic, lv_draw_image.c:206-231 classe la source en UNKNOWN
    dsc.header.cf = LV_COLOR_FORMAT_RGB565A8; // planaire : couleur (stride = cote*2) puis masque A8 (stride/2)
    dsc.header.stride = cote * 2;
    dsc.header.w = cote;
    dsc.header.h = cote;
    dsc.data_size = cote * cote * UI_IMG_ALPHA_BYTE;
    dsc.data = px;
    return px;
}

// JPEG (pochettes et logos de Music Assistant) : pixels RGB565 dans l'ordre de l'écran (big endian = LV_COLOR_16_SWAP),
// réduits par le décodeur lui-même, puis ramenés à la taille d'affichage (moyenne des pixels) au lieu du zoom de la
// production. 27/10 : sur l'ESP, esp_new_jpeg à la place de JPEGDEC (production : moode_player.h), comme la caméra
// (décodeur seul 42 ms contre 64 sur une image de 38 Ko) ; le simulateur garde JPEGDEC.
#ifdef ARDUINO
// dimensions lues dans l'en-tête (marqueur SOF) : esp_new_jpeg doit connaître la réduction avant d'ouvrir l'image
static bool taille_jpeg(const uint8_t *j, size_t n, int &w, int &h, bool &progressif)
{
    size_t i = 2;
    while (i + 9 < n)
    {
        if (j[i] != 0xFF)
            return false;
        const uint8_t m = j[i + 1];
        if (m == 0xFF) // octet de bourrage
        {
            i++;
            continue;
        }
        if (m == 0xC0 || m == 0xC1 || m == 0xC2)
        {
            h = (j[i + 5] << 8) | j[i + 6];
            w = (j[i + 7] << 8) | j[i + 8];
            progressif = m == 0xC2;
            return w > 0 && h > 0;
        }
        if (m == 0xD9 || m == 0xDA) // fin d'image, ou données sans SOF avant
            return false;
        i += 2 + ((j[i + 2] << 8) | j[i + 3]);
    }
    return false;
}

static void liberer_px(px565_t *px) { heap_caps_free(px); }

// réduction par le décodeur à la taille multiple de 8 juste au-dessus de la case (1/8 au plus, comme JPEGDEC) ;
// sans réduction si elle échoue (ou si l'image est déjà petite)
static px565_t *decoder_jpeg(const uint8_t *jpg, size_t n, int cote, int &w, int &h)
{
    int iw = 0, ih = 0;
    bool progressif = false;
    if (!taille_jpeg(jpg, n, iw, ih, progressif))
    {
        ui_trace("[image] JPEG : en-tête illisible\n");
        return nullptr;
    }
    if (progressif)
    {
        ui_trace("[image] JPEG : progressif (non géré)\n");
        return nullptr;
    }
    const int sw = LV_MAX((cote + 7) & ~7, ((iw + 7) / 8 + 7) & ~7), sh = LV_MAX((cote + 7) & ~7, ((ih + 7) / 8 + 7) & ~7);
    for (int reduire = sw < iw && sh < ih; reduire >= 0; reduire--)
    {
        jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
        cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
        if (reduire)
        {
            cfg.scale.width = sw;
            cfg.scale.height = sh;
        }
        jpeg_dec_handle_t dec = nullptr;
        jpeg_error_t e = jpeg_dec_open(&cfg, &dec);
        if (e != JPEG_ERR_OK)
        {
            ui_trace("[image] JPEG : ouverture impossible (%d)\n", (int)e);
            continue;
        }
        jpeg_dec_io_t io = {};
        io.inbuf = (uint8_t *)jpg;
        io.inbuf_len = (int)n;
        jpeg_dec_header_info_t info = {};
        int lg = 0;
        px565_t *px = nullptr;
        e = jpeg_dec_parse_header(dec, &io, &info);
        if (e == JPEG_ERR_OK)
            e = jpeg_dec_get_outbuf_len(dec, &lg);
        if (e == JPEG_ERR_OK)
        {
            px = (px565_t *)heap_caps_aligned_alloc(16, lg, MALLOC_CAP_SPIRAM); // 16 : SIMD de l'ESP32-S3
            io.outbuf = (uint8_t *)px;
            e = px ? jpeg_dec_process(dec, &io) : JPEG_ERR_NO_MEM;
        }
        jpeg_dec_close(dec);
        if (e == JPEG_ERR_OK)
        {
            w = reduire ? sw : info.width;
            h = reduire ? sh : info.height;
            return px;
        }
        heap_caps_free(px);
        ui_trace("[image] JPEG %d x %d : erreur de décodage %d (%s)\n", iw, ih, (int)e, reduire ? "réduit" : "taille réelle");
    }
    return nullptr;
}
#else
struct jpeg_dest_t
{
    px565_t *px;
    int w, h;
};

static int jpeg_dessin(JPEGDRAW *d)
{
    jpeg_dest_t *dst = (jpeg_dest_t *)d->pUser;
    int i = 0;
    for (int y = 0; y < d->iHeight; y++)
        for (int x = 0; x < d->iWidth; x++, i++)
        {
            int px = d->x + x, py = d->y + y;
            if (px < dst->w && py < dst->h)
                dst->px[py * dst->w + px] = d->pPixels[i]; // 9.6 : px565_t est un uint16_t tout court
        }
    return 1;
}

static void liberer_px(px565_t *px) { UI_IMG_LIBERE(px); } // branche simulateur (JPEGDEC) : 8.4 uniquement

// réduction 1/2, 1/4, 1/8 par le décodeur : la plus forte qui garde au moins « cote » pixels
static px565_t *decoder_jpeg(const uint8_t *jpg, size_t n, int cote, int &w, int &h)
{
    JPEGDEC *jpeg = new JPEGDEC(); // ~17 Ko : sur le tas, pas sur la pile (comme la production)
    if (!jpeg)
        return nullptr;
    jpeg_dest_t d = {nullptr, 0, 0};
    if (jpeg->openRAM((uint8_t *)jpg, (int)n, jpeg_dessin))
    {
        int iw = jpeg->getWidth(), ih = jpeg->getHeight();
        int div = 1, opt = 0;
        if (iw / 8 >= cote && ih / 8 >= cote)
            div = 8, opt = JPEG_SCALE_EIGHTH;
        else if (iw / 4 >= cote && ih / 4 >= cote)
            div = 4, opt = JPEG_SCALE_QUARTER;
        else if (iw / 2 >= cote && ih / 2 >= cote)
            div = 2, opt = JPEG_SCALE_HALF;
        d.w = (iw + div - 1) / div;
        d.h = (ih + div - 1) / div;
        d.px = (px565_t *)UI_IMG_ALLOC(d.w * d.h * (int)sizeof(px565_t));
        if (d.px)
        {
            memset(d.px, 0, d.w * d.h * (int)sizeof(px565_t));
            // 9.6 au simulateur : plus de LV_COLOR_16_SWAP dans la conf (la 8.4 du dépôt le mettait à 1, comme la
            // dalle ST7701). Le simulateur lit et écrit du RVB565 NATIF (petit boutiste) : c'est ce qui doit sortir
            // dans les captures, sinon les couleurs sont échangées (la leçon de la caméra).
            jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
            jpeg->setUserPointer(&d);
            if (!jpeg->decode(0, 0, opt))
            {
                ui_trace("[image] JPEG : erreur de décodage %d\n", jpeg->getLastError()); // 8 = JPEG progressif (non géré)
                UI_IMG_LIBERE(d.px);
                d.px = nullptr;
            }
        }
        jpeg->close();
    }
    delete jpeg;
    w = d.w;
    h = d.h;
    return d.px;
}
#endif

static uint8_t *convertir_jpeg(const uint8_t *jpg, size_t n, int cote, lv_img_dsc_t &dsc)
{
    // NOTE 9.6 : les décodeurs (esp_new_jpeg comme JPEGDEC) sortent du RGB565 BIG ENDIAN ; on permute les octets
    // pour écrire du RGB565 natif, conforme à LV_COLOR_FORMAT_RGB565 (l'écran 9.6, glue vague A). En 8.4
    // (LV_COLOR_16_SWAP = 1) l'écran était en octets inversés : le code d'avant, inchangé dans #else.
    // vrai début du JPEG (production : parfois quelques octets avant le marqueur FF D8)
    size_t debut = 0;
    while (debut + 1 < n && debut < 64 && !(jpg[debut] == 0xFF && jpg[debut + 1] == 0xD8))
        debut++;
    int w = 0, h = 0;
    px565_t *src = decoder_jpeg(jpg + debut, n - debut, cote, w, h);
    if (!src)
        return nullptr;
    uint8_t *px = (uint8_t *)UI_IMG_ALLOC(cote * cote * (int)sizeof(px565_t));
    px565_t *o = (px565_t *)px;
    for (int y = 0; px && y < cote; y++)
        for (int x = 0; x < cote; x++)
        {
            int x0 = x * w / cote, x1 = LV_MAX((x + 1) * w / cote, x0 + 1);
            int y0 = y * h / cote, y1 = LV_MAX((y + 1) * h / cote, y0 + 1);
            unsigned r = 0, g = 0, b = 0, nb = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++)
                {
                    uint16_t v = __builtin_bswap16(src[sy * w + sx]); // big endian -> valeur RGB565 réelle
                    r += (v >> 11) & 0x1F;
                    g += (v >> 5) & 0x3F;
                    b += v & 0x1F;
                    nb++;
                }
            o[y * cote + x] = (px565_t)((((r / nb) & 0x1Fu) << 11) | (((g / nb) & 0x3Fu) << 5) | ((b / nb) & 0x1Fu));
        }
    liberer_px(src);
    if (!px)
        return nullptr;
    memset(&dsc, 0, sizeof dsc);
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565; // RGB565 natif : l'écran 9.6 est en LV_COLOR_FORMAT_RGB565
    dsc.header.stride = cote * 2;
    dsc.header.w = cote;
    dsc.header.h = cote;
    dsc.data_size = cote * cote * (int)sizeof(px565_t);
    dsc.data = px;
    return px;
}

static bool est_jpeg(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i + 1 < n && i < 64; i++)
        if (d[i] == 0xFF && d[i + 1] == 0xD8)
            return true;
    return false;
}

// Le travail confié à la tâche de fond : une image à la fois (ni LVGL ni g_img : seulement ce qui est ici)
static struct
{
    int e;
    char url[224];
    int cote;
    uint8_t *px;
    lv_img_dsc_t img;
    size_t n;
    uint32_t t_tele, t_deco;
    bool tele_ok;
    char format[5]; // 27/10 : « PNG » / « JPEG » dans la trace (mesure des décodeurs)
} g_job;
static volatile bool g_occupe = false, g_fini = false;

static void travail(void *)
{
    uint8_t *brut = nullptr;
    size_t n = 0;
    const uint32_t t0 = lv_tick_get();
    g_job.px = nullptr;
    g_job.tele_ok = ui_images_telecharger(g_job.url, &brut, &n);
    const uint32_t t1 = lv_tick_get();
    if (g_job.tele_ok)
    {
        const bool png_ok = n > 8 && brut[0] == 0x89 && brut[1] == 'P';
        snprintf(g_job.format, sizeof g_job.format, "%s", png_ok ? "PNG" : est_jpeg(brut, n) ? "JPEG" : "?");
        g_job.px = png_ok ? convertir(brut, n, g_job.cote, g_job.img) : est_jpeg(brut, n) ? convertir_jpeg(brut, n, g_job.cote, g_job.img) : nullptr;
        free(brut);
    }
    g_job.n = n;
    g_job.t_tele = t1 - t0;
    g_job.t_deco = lv_tick_elaps(t1);
    g_fini = true;
    g_occupe = false;
}

// dans la boucle : l'image que la tâche de fond a préparée, posée dans son emplacement
static void poser()
{
    {
        g_fini = false;
        image_t &m = g_img[g_job.e];
        const bool encore = strcmp(m.url, g_job.url) == 0 && m.cote == g_job.cote; // l'emplacement attend toujours celle-ci
        if (!g_job.tele_ok)
            ui_trace("[image] échec : %s\n", g_job.url); // pas d'image : la page s'en passe
        else
        {
            ui_trace("[image] %s %u o, %d px : téléchargée en %u ms, décodée en %u ms (en fond)\n", g_job.format, (unsigned)g_job.n,
                     g_job.cote, (unsigned)g_job.t_tele, (unsigned)g_job.t_deco);
            if (!g_job.px)
                ui_trace("[image] image illisible : %s\n", g_job.url);
        }
        if (!encore)
            UI_IMG_LIBERE(g_job.px);
        else
        {
            liberer(m);
            m.px = g_job.px;
            m.img = g_job.img;
            if (m.pret)
                m.pret(g_job.e, m.px ? &m.img : nullptr);
        }
        g_job.px = nullptr;
    }
}

// dans la boucle : pose l'image prête, puis confie la suivante à la tâche de fond
static void tache_cb(lv_timer_t *)
{
    if (!ui_images_telecharger)
        return;
    if (g_fini)
        poser();
    if (g_occupe)
        return;
    for (int e = 0; e < UI_IMG_NB; e++)
    {
        image_t &m = g_img[e];
        if (!m.attente || (m.possible && !m.possible()))
            continue; // serveur éteint : on réessaiera au passage suivant
        m.attente = false;
        g_job.e = e;
        snprintf(g_job.url, sizeof g_job.url, "%s", m.url);
        g_job.cote = m.cote;
        g_occupe = true;
        if (ui_images_en_fond && ui_images_en_fond(travail))
            return;
        ui_diag_etape("image"); // simulateur (ou tâche impossible) : dans la boucle
        travail(nullptr);
        poser(); // posée tout de suite, une par passage (comme avant)
        return;
    }
}

void ui_images_demander(int e, const char *url, int cote, ui_image_prete_t pret, bool (*possible)())
{
    if (e < 0 || e >= UI_IMG_NB || !url || !*url)
        return;
    if (!g_timer)
        g_timer = lv_timer_create(tache_cb, 100, nullptr); // 25/10 : 100 ms (la boucle ne fait plus que poser l'image)
    image_t &m = g_img[e];
    m.pret = pret;
    m.possible = possible;
    if (strcmp(m.url, url) == 0 && m.cote == cote)
    {
        if (m.px)
            pret(e, &m.img); // déjà là
        else
            m.attente = true; // pas encore (ou échec) : on (re)demande
        return;
    }
    liberer(m);
    snprintf(m.url, sizeof m.url, "%s", url);
    m.cote = cote;
    m.attente = true;
}

void ui_images_oublier(int e)
{
    if (e < 0 || e >= UI_IMG_NB)
        return;
    liberer(g_img[e]);
    g_img[e].url[0] = 0;
    g_img[e].attente = false;
}

bool ui_images_tient(const lv_img_dsc_t *desc)
{
    if (!g_img || !desc)
        return false;
    for (int e = 0; e < UI_IMG_NB; e++)
        if (desc == &g_img[e].img)
            return true;
    return false;
}
