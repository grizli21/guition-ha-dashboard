// Caméras (voir ui_camera.h ; flux MJPEG de go2rtc sur le NAS). Pendant qu'une image se décode (cœur 1),
// la suivante arrive (lecture continue, cœur 0). Écran : esp_new_jpeg écrit directement dans la mémoire
// d'image ; simulateur : JPEGDEC vers une image LVGL. Les doublons ne sont ni décodés ni comptés.
#include "ui_camera.h"
#include "ui_images.h"
#include "ui_diag.h"
#ifndef ARDUINO
#include <JPEGDEC.h> // simulateur seulement : sur l'ESP, esp_new_jpeg (27/10 : JPEGDEC retiré du firmware)
#endif
#include <new>
#ifdef ARDUINO
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_jpeg_dec.h> // esp_new_jpeg (Espressif, fourni avec l'Arduino 3)
#endif
#include <stdlib.h>
#include <string.h>

#define CAM_MAX_PX (480 * 480) // la plus grande zone : l'écran entier (sonnette)
#define CAM_PERIODE_MS 0       // l'image suivante dès que la précédente est posée (comme la production)
#define CAM_JPEG_MAX 250000    // MAX_JPEG_SIZE de la production
#define CAM_REESSAI_MS 2000    // la caméra ne répond pas : nouvel essai 2 s plus tard

typedef lv_draw_buf_t cam_photo_t;                  // une photo d'élément posé sur la caméra (lv_snapshot_take)
#define CAM_PHOTO_LIBERE(b) lv_draw_buf_destroy(b)  // draw/lv_draw_buf.h:283
#define CAM_PHOTO_OCTETS(b) ((size_t)(b)->data_size) // draw/lv_draw_buf.h:95
#define CAM_PHOTO_CF LV_COLOR_FORMAT_ARGB8888       // format demandé à lv_snapshot_take
#define CAM_PHOTO_OCTETS_PAR_PX 4                   // B, G, R, A
typedef uint16_t cam_px_t;                          // pixel décodé : RGB565 brut (2 octets), plus lv_color_t (3 o)
#define CAM_TAILLE_PX sizeof(cam_px_t)
#define CAM_ALLOC(n) lv_malloc(n)              // stdlib/lv_mem.h:67 (lv_mem_alloc n'existe plus en 9.6)
#define CAM_LIBERE(p) lv_free(p)               // stdlib/lv_mem.h:95

// 9.6 : « lv_area_intersect » et « lv_obj_get_ext_draw_size » ne sont plus publiés (ils sont passés dans
// src/misc/lv_area_private.h:46 et src/core/lv_obj_draw_private.h:36). Remplacés par de l'API publique :
// l'intersection est calculée ici, et la taille de dessin étendue se lit par l'événement même que
// lv_obj_refresh_ext_draw_size envoie (src/core/lv_obj_draw.c : « lv_obj_send_event(obj,
// LV_EVENT_REFR_EXT_DRAW_SIZE, &s_new) », paramètre « int32_t * » — core/lv_event.h:70). Les widgets la
// complètent eux-mêmes par lv_event_set_ext_draw_size (une étiquette y ajoute font_h / 4 :
// src/widgets/label/lv_label.c:840), donc « lv_obj_calculate_ext_draw_size » seul serait trop petit : le
// snapshot, lui, prend bien la taille étendue complète (src/draw/snapshot/lv_snapshot.c:118).
static bool cam_zone_intersecte(lv_area_t *res, const lv_area_t *a, const lv_area_t *b)
{
    res->x1 = LV_MAX(a->x1, b->x1);
    res->y1 = LV_MAX(a->y1, b->y1);
    res->x2 = LV_MIN(a->x2, b->x2);
    res->y2 = LV_MIN(a->y2, b->y2);
    return res->x1 <= res->x2 && res->y1 <= res->y2;
}
static int32_t cam_ext_draw_size(lv_obj_t *o)
{
    int32_t s = 0;
    lv_obj_send_event(o, LV_EVENT_REFR_EXT_DRAW_SIZE, &s); // core/lv_obj_event.h:48
    return s;
}

static lv_obj_t *g_zone = nullptr, *g_img = nullptr;
static char g_url[96];
static cam_px_t *g_px[2] = {nullptr, nullptr};
static lv_img_dsc_t g_dsc[2];
static int g_affichee = -1;
static uint32_t g_derniere = 0;
static uint32_t g_images = 0, g_compte_depuis = 0;
#ifndef ARDUINO
static JPEGDEC *g_jpeg = nullptr; // simulateur : décodeur de la chaîne principale
#endif
static volatile bool g_suspendue = false; // 27/10 : rail en défilement (ui_camera_suspendre)
static volatile bool g_en_pause = false;  // le décodeur est arrêté (son image en cours abandonnée) : la pause est effective
static bool g_interrompue = false;        // l'image en cours a été abandonnée (pause) : ni montrée ni comptée
static uint8_t *g_jpg[3] = {nullptr, nullptr, nullptr}; // JPEG téléchargés (PSRAM, gardés) : décodé / lus en avance (2)
bool (*ui_camera_lire)(int canal, const char *url, uint8_t *buf, size_t max, size_t *n) = nullptr;
bool (*ui_camera_lancer)(int canal, void (*travail)(void *)) = nullptr;
void (*ui_camera_pause)() = nullptr;
void (*ui_camera_couper)() = nullptr;                    // 29/10
void (*ui_camera_arreter_taches)() = nullptr;            // 08/11 : la caméra rend aussi ses tâches de fond
bool (*ui_camera_taches_rendues)(size_t *octets) = nullptr;
uint8_t **ui_camera_ecran_lignes = nullptr;
int ui_camera_ecran_rotation = 0;
void (*ui_camera_ecran_synchro)(void *debut, size_t octets) = nullptr;
uint8_t **(*ui_camera_ecran_debut)() = nullptr;
void (*ui_camera_ecran_fin)() = nullptr;
#define CAM_EXCLUS_MAX 8
static lv_area_t g_a_redessiner = {0, 0, -1, -1}; // zone supprimée pendant une écriture directe : LVGL la refait après

// 08/11 (retour d'usage : « on ne libère pas et ça me gêne, ce n'est pas propre ») : machine à rendre le matériel de la caméra à
// sa fermeture (voir rendre_tampons, plus bas, pour l'ordre d'arrêt et pourquoi il est sûr).
static bool g_rendu_demande = false;  // arrêt des tâches de fond demandé, tampons pas encore rendus
static uint32_t g_rendu_quand = 0;    // date de la demande (trace d'attente anormale)
static bool g_rendu_attente_dite = false;
static bool g_penurie_dite = false;   // 07/11 : la pénurie de PSRAM n'est dite qu'une fois ; réinitialisée au rendu

volatile uint32_t ui_camera_t_attente = 0, ui_camera_t_reception = 0;
// 27/10 : cadence réelle de la caméra, une ligne toutes les 30 s (ui_camera_tache)
static uint32_t g_somme_tele = 0, g_somme_deco = 0, g_somme_attente = 0, g_somme_reception = 0, g_somme_octets = 0; // fenêtre de 30 s
static uint32_t g_octets = 0; // taille de la dernière image décodée (elle change avec l'éclairage de la scène)

// le travail de fond : ses entrées sont figées au lancement, son résultat lu par la boucle quand fini passe à vrai
static struct
{
    char url[96];
    int w, h, tampon;
    volatile bool en_cours, fini, ok;
    uint32_t t_tele, t_deco;
    // écriture directe (figée au lancement par la boucle) : coin de la zone à l'écran, rayon des coins, éléments par-dessus
    bool direct;
    bool doublon;         // 27/10 : image identique à la précédente (ni décodée ni comptée)
    volatile bool annule; // la zone a disparu : le décodeur s'arrête, LVGL redessine
    lv_area_t zone;
    int rayon, nb_exclus;
    lv_area_t exclus[CAM_EXCLUS_MAX];
    // 27/10 : photo de chaque élément posé par-dessus (lv_snapshot, transparence comprise) et sa place à l'écran : la
    // caméra le COMPOSE sur son image (sautait son rectangle entier : coins du bouton et fond du PM1 sombres)
    const cam_photo_t *calque[CAM_EXCLUS_MAX];
    lv_area_t calque_zone[CAM_EXCLUS_MAX];
} g_job;

// un bloc décodé (bande de esp_new_jpeg sur l'ESP ; bloc de JPEGDEC au simulateur, même forme)
#ifdef ARDUINO
struct cam_bloc_t
{
    int x, y, iWidth, iHeight;
    uint16_t *pPixels;
    void *pUser;
};
#else
typedef JPEGDRAW cam_bloc_t;
#endif

struct cam_dest_t
{
    cam_px_t *px;
    int w, h, ox, oy;     // taille de la zone, décalage du recadrage dans l'image décodée
    int phy_min, phy_max; // écriture directe : lignes physiques touchées (synchronisation du cache)
    uint8_t **lignes;     // 27/10 : mémoire d'image de cette image-ci (lue une fois : l'écran peut en avoir deux)
};

#ifndef ARDUINO
// 26/10 : une ligne d'un bloc (memcpy) au lieu de pixel par pixel avec deux tests : le recadrage est calculé une fois par bloc
static int cam_dessin(JPEGDRAW *d)
{
    cam_dest_t *c = (cam_dest_t *)d->pUser;
    const int x0 = LV_MAX(d->x, c->ox), x1 = LV_MIN(d->x + d->iWidth, c->ox + c->w); // colonnes gardées, dans l'image
    if (x1 <= x0)
        return 1;
    const size_t octets = (size_t)(x1 - x0) * CAM_TAILLE_PX;
    for (int y = 0; y < d->iHeight; y++)
    {
        const int py = d->y + y - c->oy;
        if (py >= 0 && py < c->h)
            memcpy(&c->px[py * c->w + (x0 - c->ox)], &d->pPixels[y * d->iWidth + (x0 - d->x)], octets);
    }
    return 1;
}

#endif

// 27/10 : le pixel (x, y) de l'écran (logique) : celui de la caméra, composé avec la photo de l'élément posé par-dessus
// s'il y en a un ; false : ne pas l'écrire (hors d'un coin arrondi de la zone, ou élément encore sans photo)
static bool composer(int x, int y, uint16_t cam, uint16_t &out)
{
    for (int i = 0; i < g_job.nb_exclus; i++)
    {
        const lv_area_t &e = g_job.exclus[i];
        if (x < e.x1 || x > e.x2 || y < e.y1 || y > e.y2)
            continue;
        const cam_photo_t *im = g_job.calque[i];
        const lv_area_t &z = g_job.calque_zone[i];
        if (!im || x < z.x1 || x > z.x2 || y < z.y1 || y > z.y2)
            return false;
        // 9.6 : photo en ARGB8888 — 4 octets par pixel (B, G, R, A), lignes espacées de « header.stride »
        const uint8_t *p = (const uint8_t *)im->data + (size_t)(y - z.y1) * im->header.stride + (size_t)(x - z.x1) * CAM_PHOTO_OCTETS_PAR_PX;
        const uint8_t a = p[3];
        const lv_color_t c = lv_color_make(p[2], p[1], p[0]); // R, G, B (misc/lv_color.h:145)
        const uint16_t dessus = lv_color_to_u16(c);           // RGB888 -> RGB565 (misc/lv_color.h:343)
        // Le pixel du décodeur JPEG est RGB565 grand boutiste (ordre de la dalle) alors que lv_color_to_u16 rend du
        // natif : les mélanger mélange rouge et bleu. On compose tout en natif et on ne repasse dans l'ordre de la
        // dalle qu'à la sortie.
        const uint16_t dessous = __builtin_bswap16(cam);
        uint16_t melange;
        if (a == LV_OPA_COVER)
            melange = dessus;
        else if (a == 0)
            melange = dessous;
        else
            melange = lv_color_16_16_mix(dessus, dessous, a); // poids sur le 1er argument (blend_private.h:179-186)
        out = __builtin_bswap16(melange);
        return true;
    }
    const int r = g_job.rayon;
    if (r > 0)
    {
        const lv_area_t &z = g_job.zone;
        const int cx = x < z.x1 + r ? z.x1 + r : x > z.x2 - r ? z.x2 - r : x;
        const int cy = y < z.y1 + r ? z.y1 + r : y > z.y2 - r ? z.y2 - r : y;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r)
            return false;
    }
    out = cam;
    return true;
}

// le segment [a, b] (écran logique) sur la ligne / colonne fixe f touche-t-il un élément par-dessus ou un coin arrondi ?
// horizontal : f = y, a..b = x ; vertical : f = x, a..b = y
static bool segment_touche(bool horizontal, int f, int a, int b)
{
    for (int i = 0; i < g_job.nb_exclus; i++)
    {
        const lv_area_t &e = g_job.exclus[i];
        if (horizontal ? (f >= e.y1 && f <= e.y2 && b >= e.x1 && a <= e.x2) : (f >= e.x1 && f <= e.x2 && b >= e.y1 && a <= e.y2))
            return true;
    }
    const int r = g_job.rayon;
    if (r <= 0)
        return false;
    const lv_area_t &z = g_job.zone;
    if (horizontal)
        return (f < z.y1 + r || f > z.y2 - r) && (a < z.x1 + r || b > z.x2 - r);
    return (f < z.x1 + r || f > z.x2 - r) && (a < z.y1 + r || b > z.y2 - r);
}

// 26/10 : écriture directe d'un bloc décodé dans la mémoire d'image (rotation comme Lovyan : drawPixelPreclipped).
// Rotation 90° (chambre 1) : colonne par colonne, une colonne d'un bloc de 16 lignes = 16 pixels contigus d'une ligne de
// la mémoire d'image (32 octets, une ligne de cache) ; sans rotation : une ligne = un memcpy. Le test pixel par pixel
// (éléments par-dessus, coins arrondis) seulement là où ils sont touchés.
static int cam_ecran_bloc(cam_bloc_t *d);
static int cam_ecran(cam_bloc_t *d) { return cam_ecran_bloc(d); }

static int cam_ecran_bloc(cam_bloc_t *d)
{
    if (g_job.annule)
        return 0; // la zone a disparu : arrêt du décodage
    cam_dest_t *c = (cam_dest_t *)d->pUser;
    const int r = ui_camera_ecran_rotation;
    const int y0 = LV_MAX(0, c->oy - d->y), y1 = LV_MIN(d->iHeight, c->oy + c->h - d->y); // lignes du bloc gardées
    const int x0 = LV_MAX(0, c->ox - d->x), x1 = LV_MIN(d->iWidth, c->ox + c->w - d->x);  // colonnes du bloc gardées
    if (y1 <= y0 || x1 <= x0)
        return 1;
    const int sx0 = g_job.zone.x1 + d->x + x0 - c->ox, sy0 = g_job.zone.y1 + d->y + y0 - c->oy; // coin à l'écran (logique)
    const int nx = x1 - x0, ny = y1 - y0;
    uint16_t **lignes = (uint16_t **)c->lignes;
    if (r == 0)
    {
        for (int y = 0; y < ny; y++)
        {
            const int sy = sy0 + y;
            const uint16_t *src = &d->pPixels[(y0 + y) * d->iWidth + x0];
            uint16_t *dst = &lignes[sy][sx0];
            if (!segment_touche(true, sy, sx0, sx0 + nx - 1))
                memcpy(dst, src, nx * 2);
            else
                for (int x = 0; x < nx; x++)
                {
                    uint16_t o;
                    if (composer(sx0 + x, sy, src[x], o))
                        dst[x] = o;
                }
        }
        c->phy_min = LV_MIN(c->phy_min, sy0);
        c->phy_max = LV_MAX(c->phy_max, sy0 + ny - 1);
        return 1;
    }
    if (r == 1) // (x, y) -> ligne x, colonne 479 - y
    {
        for (int x = 0; x < nx; x++)
        {
            const int sx = sx0 + x;
            uint16_t *ligne = lignes[sx];
            const uint16_t *src = &d->pPixels[y0 * d->iWidth + x0 + x];
            const bool tester = segment_touche(false, sx, sy0, sy0 + ny - 1);
            for (int y = 0; y < ny; y++, src += d->iWidth)
            {
                uint16_t o = *src;
                if (!tester || composer(sx, sy0 + y, *src, o))
                    ligne[479 - (sy0 + y)] = o;
            }
        }
        c->phy_min = LV_MIN(c->phy_min, sx0);
        c->phy_max = LV_MAX(c->phy_max, sx0 + nx - 1);
        return 1;
    }
    for (int y = 0; y < ny; y++) // autres rotations : pixel par pixel
        for (int x = 0; x < nx; x++)
        {
            const int sx = sx0 + x, sy = sy0 + y;
            uint16_t o;
            if (!composer(sx, sy, d->pPixels[(y0 + y) * d->iWidth + x0 + x], o))
                continue;
            int fx = sx, fy = sy;
            if ((1u << r) & 0b10010110)
                fy = 479 - fy;
            if (r & 2)
                fx = 479 - fx;
            if (r & 1)
            {
                const int t = fx;
                fx = fy;
                fy = t;
            }
            lignes[fy][fx] = o;
            c->phy_min = LV_MIN(c->phy_min, fy);
            c->phy_max = LV_MAX(c->phy_max, fy);
        }
    return 1;
}

// 27/10 : esp_new_jpeg (Espressif) en mode bloc (bandes de 16 lignes sur toute la largeur), recadré à la main et arrêté
// au bas de la zone ; chaque bande passe par cam_ecran. Mesuré en A/B sur la chambre 1, même image de 38 Ko : décodeur seul
// 42 ms (bande en RAM interne) contre 64 ms pour JPEGDEC ; l'objet JPEGDEC (17,9 Ko de RAM interne) n'est alors plus créé
#ifdef ARDUINO
// 08/11 : la bande de décodage est passée au niveau du fichier (elle était statique dans decoder_esp) pour que la
// fermeture de la caméra puisse la rendre : ~15 ko de RAM interne retenus après la fermeture, mesuré. Elle n'est utilisée
// que par la tâche de décodage, jamais par la boucle de l'écran.
static uint8_t *g_bande = nullptr;
static int g_bande_lg = 0;
static bool decoder_esp(uint8_t *jpg, size_t n)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
    cfg.block_enable = true;
    jpeg_dec_handle_t dec = nullptr;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK)
        return false;
    jpeg_dec_io_t io = {};
    io.inbuf = jpg;
    io.inbuf_len = (int)n;
    jpeg_dec_header_info_t info = {};
    bool ok = jpeg_dec_parse_header(dec, &io, &info) == JPEG_ERR_OK;
    int lg = 0, nb = 0;
    ok = ok && jpeg_dec_get_outbuf_len(dec, &lg) == JPEG_ERR_OK && jpeg_dec_get_process_count(dec, &nb) == JPEG_ERR_OK && lg > 0;
    static uint8_t *bande = nullptr;
    static int bande_lg = 0;
    if (ok && lg > g_bande_lg)
    {
        heap_caps_free(g_bande);
        g_bande = (uint8_t *)heap_caps_aligned_alloc(16, lg, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!g_bande)
            g_bande = (uint8_t *)heap_caps_aligned_alloc(16, lg, MALLOC_CAP_SPIRAM);
        g_bande_lg = g_bande ? lg : 0;
        ui_diag_ram_etape("bande caméra allouée"); // 28/10 : AUDIT RAM
        ui_trace("[camera] esp_new_jpeg : bande de %d octets (%d lignes) en %s\n", lg, lg / (info.width * 2),
                 g_bande && esp_ptr_internal(g_bande) ? "RAM interne" : "PSRAM");
    }
    if (ok && g_bande)
    {
        const int hb = lg / (info.width * 2); // hauteur d'une bande (16 en 4:2:0)
        cam_dest_t c = {nullptr, g_job.w, g_job.h, LV_MAX(0, (info.width - g_job.w) / 2), LV_MAX(0, (info.height - g_job.h) / 2), 479, -1,
                        ui_camera_ecran_debut ? ui_camera_ecran_debut() : ui_camera_ecran_lignes};
        io.outbuf = g_bande;
        for (int i = 0; i < nb && ok; i++)
        {
            if (g_suspendue) // 27/10 : rail en défilement : l'image est abandonnée tout de suite (la finir le faisait sauter)
            {
                g_interrompue = true;
                break;
            }
            if (g_job.annule || i * hb >= c.oy + c.h) // zone disparue, ou bas de la zone atteint
                break;
            ok = jpeg_dec_process(dec, &io) == JPEG_ERR_OK;
            if (!ok)
                ui_trace("[camera] esp_new_jpeg : erreur à la bande %d\n", i);
            cam_bloc_t d = {};
            d.x = 0;
            d.y = i * hb;
            d.iWidth = info.width;
            d.iHeight = hb;
            d.pPixels = (uint16_t *)g_bande;
            d.pUser = &c;
            if (ok && i * hb + hb > c.oy)
                cam_ecran(&d);
        }
        if (g_job.direct && c.phy_max >= c.phy_min && ui_camera_ecran_synchro)
            ui_camera_ecran_synchro(c.lignes[c.phy_min], (size_t)(c.phy_max - c.phy_min + 1) * 480 * 2);
        if (ui_camera_ecran_fin && !g_interrompue)
            ui_camera_ecran_fin(); // 27/10 : image complète dans la mémoire cachée : à montrer (pas une image abandonnée)
    }
    jpeg_dec_close(dec);
    return ok;
}
#endif

// 26/10 : décodage d'une image : la plus forte réduction du décodeur qui couvre encore la zone,
// recadrage au centre (le décodeur ne traite que la zone, blocs de 16 px, et s'arrête à son bas) ; écriture directe à
// l'écran, ou dans px (image LVGL, simulateur)
static bool decoder(uint8_t *jpg, size_t n, cam_px_t *px)
{
#ifdef ARDUINO
    // 27/10 : JPEGDEC retiré du firmware : l'écran a toujours l'écriture directe (esp_new_jpeg en bandes)
    if (g_job.direct)
        return decoder_esp(jpg, n);
    ui_trace("[camera] pas d'écriture directe : pas de décodeur\n");
    return false;
#else
    JPEGDEC *jpeg = g_jpeg;
    if (!jpeg || !jpeg->openRAM(jpg, (int)n, g_job.direct ? cam_ecran : cam_dessin))
    {
        ui_trace("[camera] image illisible (%u octets)\n", (unsigned)n);
        return false;
    }
    const int w = jpeg->getWidth(), h = jpeg->getHeight();
    int div = 1, opt = 0;
    if (w / 2 >= g_job.w && h / 2 >= g_job.h)
        div = 2, opt = JPEG_SCALE_HALF;
    cam_dest_t c = {px, g_job.w, g_job.h, LV_MAX(0, (w / div - g_job.w) / 2), LV_MAX(0, (h / div - g_job.h) / 2), 479, -1,
                    g_job.direct && ui_camera_ecran_debut ? ui_camera_ecran_debut() : ui_camera_ecran_lignes};
    if (!g_job.direct && (w / div < g_job.w || h / div < g_job.h)) // image plus petite que la zone : fond noir autour
        memset(c.px, 0, g_job.w * g_job.h * CAM_TAILLE_PX);
    // 27/10 : plus de setCropArea (simulateur seulement) : JPEGDEC arrondit le recadrage aux blocs de 16 px et laissait
    // des colonnes non écrites au bord droit d'une zone de 350 px ; image entière décodée, cam_dessin recadre
    // idem ui_images.cpp:293 : au simulateur, sous 9.6, le tampon est lu en RVB565 natif (pas de LV_COLOR_16_SWAP)
    jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
    jpeg->setUserPointer(&c);
    const bool ok = jpeg->decode(0, 0, opt);
    if (g_job.direct && c.phy_max >= c.phy_min && ui_camera_ecran_synchro) // une synchronisation par image
        ui_camera_ecran_synchro(c.lignes[c.phy_min], (size_t)(c.phy_max - c.phy_min + 1) * 480 * 2);
    if (g_job.direct && ui_camera_ecran_fin)
        ui_camera_ecran_fin(); // 27/10 : image complète dans la mémoire cachée : à montrer
    if (!ok && !g_job.annule)
        ui_trace("[camera] décodage impossible (erreur %d)\n", jpeg->getLastError());
    jpeg->close();
    return ok;
#endif
}

// 27/10 : écriture directe : LECTURE CONTINUE (tâche 1, cœur 0) : le lecteur télécharge sans attendre le décodeur, écarte
// lui-même les doublons et laisse prête la dernière image nouvelle ; le décodeur prend la plus récente. Avant, le lecteur
// ne repartait qu'à la prise de son image : un doublon faisait attendre au décodeur un téléchargement entier (~51 ms)
static struct
{
    volatile bool actif, en_route;    // demandé / tâche dans sa boucle
    volatile bool panne;              // lecture impossible : le décodeur s'arrête (nouvel essai par la boucle)
    volatile int pret = -1, dec = -1; // tampon de la dernière image nouvelle / tampon pris par le décodeur
    uint32_t sig_avant;
    size_t n[3];
    uint32_t t_tele[3], t_attente[3], t_reception[3];
} g_flux;
#ifdef ARDUINO
static portMUX_TYPE g_flux_verrou = portMUX_INITIALIZER_UNLOCKED;
#define FLUX_PRENDRE() portENTER_CRITICAL(&g_flux_verrou)
#define FLUX_RENDRE() portEXIT_CRITICAL(&g_flux_verrou)
#else
#define FLUX_PRENDRE()
#define FLUX_RENDRE()
#endif
static uint32_t g_doublons = 0; // 27/10 : images identiques à la précédente (la source n'en avait pas encore de nouvelle)

// doublon : même taille et mêmes octets (milieu et fin) que la précédente
static uint32_t signature(const uint8_t *jpg, size_t n)
{
    uint32_t sig = (uint32_t)n * 2654435761u;
    for (size_t i = 0; i < 64 && i < n; i++)
        sig = sig * 31 + jpg[n / 2 + i / 2 < n ? n / 2 + i : i] + jpg[n - 1 - i];
    return sig;
}

static void lecture_continue(void *)
{
    while (g_flux.actif)
    {
        if (g_suspendue) // rail en défilement : ni lecture (Wi-Fi, PSRAM) ni décodage
        {
            ui_camera_pause();
            continue;
        }
        FLUX_PRENDRE(); // le tampon libre : ni celui du décodeur ni celui qui attend
        int b = 0;
        while (b == g_flux.dec || b == g_flux.pret)
            b++;
        FLUX_RENDRE();
        const uint32_t t0 = lv_tick_get();
        size_t n = 0;
        if (!ui_camera_lire(0, g_job.url, g_jpg[b], CAM_JPEG_MAX, &n))
        {
            g_flux.panne = true;
            break;
        }
        const uint32_t sig = signature(g_jpg[b], n);
        if (sig == g_flux.sig_avant)
        {
            g_doublons++;
            continue;
        }
        g_flux.sig_avant = sig;
        g_flux.n[b] = n;
        g_flux.t_tele[b] = lv_tick_get() - t0;
        g_flux.t_attente[b] = ui_camera_t_attente;
        g_flux.t_reception[b] = ui_camera_t_reception;
        FLUX_PRENDRE(); // l'image prête qui n'a pas été prise est remplacée : seule la plus récente compte
        g_flux.pret = b;
        FLUX_RENDRE();
    }
    g_flux.en_route = false;
}

static void arreter_flux()
{
    g_flux.actif = false;
    while (g_flux.en_route)
        ui_camera_pause();
    g_flux.pret = -1;
    g_flux.dec = -1;
}

static volatile bool g_enchainer = false; // écriture directe : la tâche enchaîne les images tant que la zone est là
void ui_camera_suspendre(bool oui) { g_suspendue = oui; }
bool ui_camera_zone_en_pause(lv_area_t *zone)
{
    if (!g_zone || !g_job.en_cours || !g_job.direct || !g_en_pause)
        return false;
    *zone = g_job.zone;
    return true;
}

static void une_image()
{
    uint8_t *telecharge = nullptr, *jpg = g_jpg[0];
    size_t n = 0;
    const uint32_t t0 = lv_tick_get();
    g_job.ok = false;
    g_job.doublon = false;
    if (g_job.direct && ui_camera_lire && ui_camera_lancer) // 27/10 : lecture continue (voir lecture_continue)
    {
        if (!g_flux.en_route)
        {
            g_flux.pret = -1;
            g_flux.dec = -1;
            g_flux.sig_avant = 0;
            g_flux.panne = false;
            g_flux.actif = true;
            g_flux.en_route = true;
            if (!ui_camera_lancer(1, lecture_continue))
            {
                g_flux.actif = false;
                g_flux.en_route = false;
            }
        }
        int b = -1;
        while (g_suspendue && !g_job.annule && g_enchainer) // rail en défilement : on laisse le cœur 1 à LVGL
        {
            g_en_pause = true;
            ui_camera_pause();
        }
        g_en_pause = false;
        for (;;)
        {
            FLUX_PRENDRE();
            b = g_flux.pret;
            if (b >= 0)
            {
                g_flux.dec = b;
                g_flux.pret = -1;
            }
            FLUX_RENDRE();
            if (b >= 0 || !g_flux.en_route || g_job.annule || !g_enchainer)
                break;
            ui_camera_pause();
        }
        if (b < 0)
        {
            if (g_flux.panne || !g_flux.en_route)
                ui_trace("[camera] lecture impossible : %s\n", g_job.url);
            else // arrêt demandé (zone changée ou partie) : pas une panne, la boucle relance sans attendre
                g_job.ok = g_job.doublon = true;
            return;
        }
        n = g_flux.n[b];
        g_job.t_tele = g_flux.t_tele[b];
        ui_camera_t_attente = g_flux.t_attente[b];
        ui_camera_t_reception = g_flux.t_reception[b];
        g_octets = n;
        const uint32_t t1 = lv_tick_get();
        g_interrompue = false;
        g_job.ok = decoder(g_jpg[b], n, g_px[g_job.tampon]);
        g_job.t_deco = lv_tick_get() - t1;
        if (g_interrompue) // abandonnée pour la pause : ni montrée ni comptée, la chaîne continue
            g_job.ok = g_job.doublon = true;
        return;
    }
    // une image à la fois (simulateur : images locales, ou téléchargées)
    bool lu = false;
    if (ui_camera_lire)
        lu = ui_camera_lire(0, g_job.url, g_jpg[0], CAM_JPEG_MAX, &n);
    else if (ui_images_telecharger && ui_images_telecharger(g_job.url, &telecharge, &n))
        lu = true, jpg = telecharge;
    g_job.t_tele = lv_tick_get() - t0;
    if (lu)
    {
        // doublon : même taille et mêmes octets (milieu et fin) que la précédente : ni décodée ni comptée
        static uint32_t sig_avant = 0;
        const uint32_t sig = signature(jpg, n);
        if (sig == sig_avant)
        {
            g_job.ok = true;
            g_job.doublon = true;
            g_doublons++;
            free(telecharge);
            return;
        }
        sig_avant = sig;
        g_octets = n;
        const uint32_t t1 = lv_tick_get();
        g_job.ok = decoder(jpg, n, g_px[g_job.tampon]);
        g_job.t_deco = lv_tick_get() - t1;
    }
    else
        ui_trace("[camera] lecture impossible : %s\n", g_job.url);
    free(telecharge);
}

// 26/10 : en écriture directe, la tâche enchaîne les images elle-même (la boucle de l'écran n'a rien à poser : attendre
// qu'elle repasse coûtait ~10 ms par image) ; elle les compte aussi
static void travail(void *)
{
    for (;;)
    {
        une_image();
        if (!(g_job.direct && g_job.ok && !g_job.annule && g_enchainer))
            break;
        if (g_job.doublon)
            continue;
        g_somme_tele += g_job.t_tele;
        g_somme_deco += g_job.t_deco;
        g_somme_attente += ui_camera_t_attente;
        g_somme_reception += ui_camera_t_reception;
        g_somme_octets += g_octets;
        g_images++;
    }
    if (g_flux.en_route) // le lecteur s'arrête avant que la boucle ne relance (autre caméra, autre zone)
        arreter_flux();
    g_job.fini = true;
}

static void zone_supprimee(lv_event_t *e)
{
    if (lv_event_get_target(e) != g_zone) // une ancienne zone (page reconstruite) : la caméra montre déjà la nouvelle
        return;
    g_zone = nullptr;
    g_img = nullptr;
    g_enchainer = false;
    if (g_job.en_cours && g_job.direct) // l'écriture directe en cours s'arrête ; LVGL refera la zone quand elle aura fini
    {
        g_job.annule = true;
        g_a_redessiner = g_job.zone;
    }
}

// pose l'image du tampon i dans la zone (créée au besoin, sous les étiquettes de la zone)
static void poser(int i)
{
    lv_img_dsc_t &d = g_dsc[i];
    if (!g_img)
    {
        g_img = lv_img_create(g_zone);
        lv_obj_center(g_img);
        lv_obj_move_background(g_img); // sous les étiquettes de la zone (pourcentage de l'impression…)
    }
    // 9.6 : « lv_img_cache_invalidate_src » n'a PAS d'équivalent public — le cache image est interne
    // (src/misc/cache/instance/lv_image_cache.c, aucune en-tête de cache dans include/lvgl). Rien à invalider ici,
    // mesuré : (1) il est éteint dans notre conf (.banc_v9_esp/conf_v9/lv_conf.h:892 « LV_CACHE_DEF_SIZE 0 », et
    // lv_cache_is_enabled teste « cache->max_size > 0 » — src/misc/cache/lv_cache.c) ; (2) de toute façon la 9.6
    // relit le descripteur à chaque pose : lv_image_set_src rappelle lv_image_decoder_get_info
    // (src/widgets/image/lv_image.c:181) et, pour une source en RAM, recopie son en-tête sans jamais passer par le
    // cache d'en-têtes (réservé aux fichiers : « is_header_cache_enabled && src_type == LV_IMAGE_SRC_FILE »,
    // src/image/lv_image_decoder.c). Les pixels, eux, sont lus sur place.
    lv_img_set_src(g_img, &d);
    g_affichee = i;
}

// 08/11 : où est réellement le bloc — les traces le mesurent au lieu de le supposer. Lu dans ui_mem_v9.cpp:26 : le backend
// LVGL 9.6 (LV_STDLIB_CUSTOM) passe d'abord par heap_caps_malloc(MALLOC_CAP_SPIRAM), avec repli en RAM interne si la
// PSRAM manque ; la bande de décodage, elle, est demandée en MALLOC_CAP_INTERNAL en priorité (decoder_esp).
static bool tampon_en_psram(const void *p)
{
#ifdef ARDUINO
    return esp_ptr_external_ram(p); // esp_memory_utils.h:328 (atteint par esp_heap_caps.h)
#else
    (void)p;
    return true; // simulateur : rien à mesurer, l'étiquette du firmware est conservée
#endif
}

// le chemin d'allocation de la caméra (il existait déjà dans ui_camera_afficher) : il sert à l'ouverture ET au tour qui
// suit un rendu si la zone est revenue pendant l'arrêt des tâches. Les traces d'échec sont conservées (07/11 : sans
// elles, la caméra était muette et on ne voyait qu'un écran noir).
static void allouer_tampons()
{
    size_t psram = 0, interne = 0;
    auto compter = [&](void *p, size_t n)
    {
        if (tampon_en_psram(p))
            psram += n;
        else
            interne += n;
    };
    for (int i = 0; i < 2; i++)
        if (!g_px[i]) // l'écran entier : 2 x 460 800 o. Rendus à la fermeture depuis le 08/11 (voir rendre_tampons) :
                      // avant, ils étaient « gardés une fois alloués » et ne revenaient jamais au tas
        {
            g_px[i] = (cam_px_t *)CAM_ALLOC(CAM_MAX_PX * CAM_TAILLE_PX);
            if (!g_px[i]) // 07/11 : sans cette trace, la caméra était muette et on ne voyait qu'un écran noir
                ui_trace("[camera] échec allocation tampon d'image %d (%d o de PSRAM demandés)\n", i, CAM_MAX_PX * CAM_TAILLE_PX);
            else
                compter(g_px[i], CAM_MAX_PX * CAM_TAILLE_PX);
        }
    for (int i = 0; i < 3; i++)
        if (!g_jpg[i]) // 3 x 250 000 o : le JPEG décodé en ce moment + deux d'avance (lecture continue, cœur 0)
        {
            g_jpg[i] = (uint8_t *)CAM_ALLOC(CAM_JPEG_MAX);
            if (!g_jpg[i])
                ui_trace("[camera] échec allocation tampon JPEG %d (%d o de PSRAM demandés)\n", i, CAM_JPEG_MAX);
            else
                compter(g_jpg[i], CAM_JPEG_MAX);
        }
    if (psram || interne)
    {
        ui_trace("[camera] tampons alloués (%u o PSRAM, %u o interne : 2 images + 3 JPEG)\n", (unsigned)psram, (unsigned)interne);
        ui_diag_ram_etape("tampons caméra alloués"); // 28/10 : AUDIT RAM, commande telnet « ram »
    }
}

void ui_camera_afficher(lv_obj_t *zone, const char *url)
{
    g_enchainer = false; // la tâche finit son image et s'arrête : la boucle relance avec la zone (taille, recadrage) à jour
    const bool meme = zone && url && strcmp(url, g_url) == 0;
    g_zone = zone;
    g_img = nullptr;
    if (!zone || !url)
    {
        g_zone = nullptr;
        g_affichee = -1;
        return;
    }
    if (!meme)
    {
        snprintf(g_url, sizeof g_url, "%s", url);
        g_affichee = -1;
        g_derniere = 0;
        g_images = 0;
        g_compte_depuis = lv_tick_get();
    }
    lv_obj_add_event_cb(zone, zone_supprimee, LV_EVENT_DELETE, NULL);
    allouer_tampons();
#ifndef ARDUINO
    if (!g_jpeg) // simulateur : gardé une fois créé
    {
        void *m = malloc(sizeof(JPEGDEC));
        if (m)
            g_jpeg = new (m) JPEGDEC();
    }
#endif
    // 08/11 : un rendu est en cours (les deux tâches de fond se sont vu demander l'arrêt, leurs tampons ne sont pas encore
    // rendus) : plus rien ne doit pointer dans les anciens tampons pendant ce temps. La caméra repartira au tour suivant,
    // avec des tampons neufs — une image de retard, pas une image fausse.
    if (g_rendu_demande)
        g_affichee = -1;
    // 26/10 : page reconstruite (nouvel état de l'imprimante…) : la dernière image tout de suite, pas de cadre vide
    lv_obj_update_layout(zone);
    if (meme && g_affichee >= 0 && g_dsc[g_affichee].header.w == lv_obj_get_content_width(zone) &&
        g_dsc[g_affichee].header.h == lv_obj_get_content_height(zone))
        poser(g_affichee);
    else
        g_affichee = -1;
}

// écriture directe : coin de la zone à l'écran, coins arrondis, éléments dessinés par-dessus (ses enfants, et ses frères
// créés après elle, s'ils la recouvrent) ; refait à chaque tour de boucle pendant l'enchaînement (une étiquette qui change)
// 27/10 : photos des éléments posés sur la caméra (reprises toutes les 300 ms : un texte qui change) ; une photo remplacée
// n'est libérée qu'une seconde plus tard (le décodeur, sur l'autre tâche, a pu la prendre pour l'image en cours)
static struct
{
    lv_obj_t *obj;
    cam_photo_t *img;
    lv_area_t zone;
    uint32_t quand;
    uint32_t sig; // 13/11 : empreinte de l'élément photographié (position, visibilité, textes, images, valeurs)
} g_calques[CAM_EXCLUS_MAX];
// 13/11 (PSRAM à zéro sur la page Imprimante, caméra vide, à l'allumage de l'imprimante) : les photos étaient reprises
// toutes les 300 ms, changées ou non, en ARGB8888, l'ancienne gardée 1 s (jusqu'à 24 vivantes). Désormais reprises
// seulement quand l'élément a changé (empreinte), ou au plus tard toutes les 5 s (un changement que l'empreinte ne voit pas).
#define CALQUE_SECOURS_MS 5000
static uint32_t empreinte(lv_obj_t *o, uint32_t h = 2166136261u)
{
    auto mel = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    mel((uint32_t)a.x1), mel((uint32_t)a.y1), mel((uint32_t)a.x2), mel((uint32_t)a.y2);
    mel(lv_obj_is_hidden(o));
    mel(lv_color_to_u32(lv_obj_get_style_bg_color(o, LV_PART_MAIN))), mel(lv_obj_get_style_bg_opa(o, LV_PART_MAIN));
    mel(lv_color_to_u32(lv_obj_get_style_text_color(o, LV_PART_MAIN))), mel(lv_obj_get_style_opa(o, LV_PART_MAIN));
    if (lv_obj_check_type(o, &lv_label_class))
        for (const char *t = lv_label_get_text(o); t && *t; t++)
            mel((uint8_t)*t);
    else if (lv_obj_check_type(o, &lv_image_class))
        mel((uint32_t)(uintptr_t)lv_image_get_src(o));
    else if (lv_obj_check_type(o, &lv_arc_class))
        mel((uint32_t)lv_arc_get_value(o)), mel((uint32_t)lv_arc_get_angle_end(o));
    else if (lv_obj_check_type(o, &lv_bar_class))
        mel((uint32_t)lv_bar_get_value(o));
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
        h = empreinte(lv_obj_get_child(o, i), h);
    return h;
}
static struct
{
    cam_photo_t *img;
    uint32_t quand;
} g_a_liberer[2 * CAM_EXCLUS_MAX];
static void retirer_calque(cam_photo_t *img)
{
    if (!img)
        return;
    for (auto &l : g_a_liberer)
        if (!l.img)
        {
            l.img = img;
            l.quand = lv_tick_get();
            return;
        }
    CAM_PHOTO_LIBERE(img); // file pleine (jamais vu) : tant pis pour l'image en cours
}
static void liberer_calques_anciens()
{
    for (auto &l : g_a_liberer)
        if (l.img && lv_tick_elaps(l.quand) >= 1000)
        {
            CAM_PHOTO_LIBERE(l.img);
            l.img = nullptr;
        }
}

static void figer_zone()
{
    lv_obj_get_content_coords(g_zone, &g_job.zone);
    g_job.rayon = lv_obj_get_style_clip_corner(g_zone, LV_PART_MAIN) ? lv_obj_get_style_radius(g_zone, LV_PART_MAIN) : 0;
    lv_area_t exclus[CAM_EXCLUS_MAX];
    int nb = 0;
    liberer_calques_anciens();
    auto ajouter = [&](lv_obj_t *o)
    {
        if (nb >= CAM_EXCLUS_MAX || lv_obj_is_hidden(o)) // core/lv_obj.h:561 (lv_obj_has_flag est marqué déprécié en 9.6)
            return;
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        if (!cam_zone_intersecte(&a, &a, &g_job.zone))
            return;
        // sa photo : reprise si c'est un autre élément, s'il a changé (empreinte), ou au bout de CALQUE_SECOURS_MS
        auto &k = g_calques[nb];
        const uint32_t sig = empreinte(o);
        if (k.obj != o || !k.img || k.sig != sig || lv_tick_elaps(k.quand) >= CALQUE_SECOURS_MS)
        {
            cam_photo_t *img = lv_snapshot_take(o, CAM_PHOTO_CF);
            if (img)
            {
                retirer_calque(k.img);
                const int32_t ext = cam_ext_draw_size(o);
                lv_obj_get_coords(o, &k.zone);
                lv_area_increase(&k.zone, ext, ext);
                k.img = img;
                k.obj = o;
                k.sig = sig;
                k.quand = lv_tick_get();
            }
        }
        g_job.calque[nb] = k.obj == o ? k.img : nullptr;
        g_job.calque_zone[nb] = k.zone;
        exclus[nb++] = a;
    };
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(g_zone); i++)
        ajouter(lv_obj_get_child(g_zone, i));
    lv_obj_t *parent = lv_obj_get_parent(g_zone);
    if (parent)
        for (uint32_t i = lv_obj_get_index(g_zone) + 1; i < lv_obj_get_child_cnt(parent); i++)
            ajouter(lv_obj_get_child(parent, i));
    memcpy(g_job.exclus, exclus, sizeof exclus);
    g_job.nb_exclus = nb;
}

// 27/10 : écran à deux mémoires d'image : la zone où la caméra écrit en ce moment (coordonnées de l'écran), à recopier
// dans la mémoire où LVGL va dessiner (sinon la bascule remontrerait une ancienne image de la caméra)
bool ui_camera_zone_directe(lv_area_t *zone)
{
    // 27/10 : en pause (rail en défilement), la caméra ne bascule plus l'écran : LVGL doit basculer seul, à pleine cadence
    // (il attendait la caméra 150 ms par image : le rail saccadait)
    if (!g_zone || !g_job.en_cours || !g_job.direct || !g_enchainer || g_en_pause)
        return false;
    *zone = g_job.zone;
    return true;
}

// ── Fermeture de la caméra : TOUT rendre (tampons, bande, tâches, calques) ──────────────────────────
// Sûr car, au point de fermeture, les deux tâches de fond sont au repos sur leur notification (décodeur
// rentré, lecture sortie de recv) : on leur demande de se rendre et elles font vTaskDelete(NULL) ; les
// tampons ne sont rendus qu'après ui_camera_taches_rendues(). Analyse complète du 08/11.
static bool camera_tient_une_chose()
{
    for (int i = 0; i < 2; i++)
        if (g_px[i])
            return true;
    for (int i = 0; i < 3; i++)
        if (g_jpg[i])
            return true;
#ifdef ARDUINO
    if (g_bande)
        return true;
#endif
    for (auto &k : g_calques)
        if (k.img)
            return true;
    for (auto &l : g_a_liberer)
        if (l.img)
            return true;
    return false;
}

static bool taches_rendues(size_t *octets)
{
    if (!ui_camera_taches_rendues) // simulateur : pas de tâches de fond, rien ne tient les tampons
    {
        if (octets)
            *octets = 0;
        return true;
    }
    return ui_camera_taches_rendues(octets);
}

// tout rendre — appelé seulement quand les deux tâches de fond sont parties (piles_rendues : leurs octets de pile)
static void rendre_tampons(size_t piles_rendues)
{
    size_t psram = 0, interne = 0;
    auto compter = [&](const void *p, size_t n)
    {
        if (tampon_en_psram(p))
            psram += n;
        else
            interne += n;
    };
    for (int i = 0; i < 2; i++)
        if (g_px[i])
        {
            compter(g_px[i], CAM_MAX_PX * CAM_TAILLE_PX);
            CAM_LIBERE(g_px[i]);
            g_px[i] = nullptr;
        }
    for (int i = 0; i < 3; i++)
        if (g_jpg[i])
        {
            compter(g_jpg[i], CAM_JPEG_MAX);
            CAM_LIBERE(g_jpg[i]);
            g_jpg[i] = nullptr;
        }
#ifdef ARDUINO
    if (g_bande)
    {
        compter(g_bande, (size_t)g_bande_lg);
        heap_caps_free(g_bande); // la contrepartie de heap_caps_aligned_alloc, comme à la réallocation (decoder_esp)
        g_bande = nullptr;
        g_bande_lg = 0;
    }
#endif
    // les calques et leur file d'attente : le décodeur qui les lisait est parti, plus rien n'attend 1 s
    size_t nb_calques = 0, octets_calques = 0;
    auto rendre_photo = [&](cam_photo_t *im)
    {
        if (!im)
            return;
        nb_calques++;
        octets_calques += CAM_PHOTO_OCTETS(im);
        CAM_PHOTO_LIBERE(im);
    };
    for (auto &k : g_calques)
    {
        rendre_photo(k.img);
        k.img = nullptr;
        k.obj = nullptr;
    }
    for (auto &l : g_a_liberer)
    {
        rendre_photo(l.img);
        l.img = nullptr;
    }
    // plus aucun descripteur, plus aucune référence du travail figé ne pointent dans du rendu
    for (int i = 0; i < 2; i++)
        memset(&g_dsc[i], 0, sizeof g_dsc[i]);
    g_affichee = -1;
    g_job.nb_exclus = 0;
    for (int i = 0; i < CAM_EXCLUS_MAX; i++)
    {
        g_job.calque[i] = nullptr;
        g_job.calque_zone[i] = {0, 0, -1, -1};
        g_job.exclus[i] = {0, 0, -1, -1};
    }
    g_penurie_dite = false; // un échec d'allocation futur doit se dire de nouveau (trace du 07/11 conservée)
    g_rendu_demande = false;
    g_rendu_attente_dite = false;
    ui_trace("[camera] tampons rendus (%u o PSRAM, %u o interne)\n", (unsigned)psram, (unsigned)(interne + piles_rendues));
    if (nb_calques)
        ui_trace("[camera] %u calque(s) de la caméra rendu(s) (%u o)\n", (unsigned)nb_calques, (unsigned)octets_calques);
    ui_diag_ram_etape("tampons caméra rendus"); // 28/10 : AUDIT RAM, relu par la commande telnet « ram »
    if (g_zone) // la zone est revenue pendant l'arrêt des tâches : tampons neufs dès ce tour, pas de fausse pénurie
        allouer_tampons();
}

void ui_camera_tache()
{
    // un travail fini : on pose son image si la zone attend toujours celle-là
    if (g_job.en_cours && g_job.fini)
    {
        g_job.en_cours = false;
        if (g_a_redessiner.x2 >= g_a_redessiner.x1) // écriture directe arrêtée en route : LVGL refait ce qu'elle a touché
        {
            lv_obj_invalidate(lv_scr_act());
            g_a_redessiner = {0, 0, -1, -1};
        }
        if (g_zone && g_job.ok && strcmp(g_job.url, g_url) == 0 && g_job.w == lv_obj_get_content_width(g_zone) &&
            g_job.h == lv_obj_get_content_height(g_zone))
        {
        if (!g_job.direct && !g_job.doublon) // image LVGL (simulateur) ; en écriture directe, elle est déjà à l'écran
        {
            lv_img_dsc_t &d = g_dsc[g_job.tampon];
            memset(&d, 0, sizeof d);
            // 9.6 : le descripteur doit se reconnaître lui-même — « lv_image_src_get_type » classe la source d'après
            // le premier octet du descripteur, qui doit valoir LV_IMAGE_HEADER_MAGIC (0x19) sous peine de
            // « LV_IMAGE_SRC_UNKNOWN » et d'aucune image affichée (src/draw/lv_draw_image.c:206-231, draw/lv_image_dsc.h:25).
            // Le format natif s'écrit en toutes lettres : nos pixels décodés sont du RGB565 (2 octets), et « lv_color_t »
            // n'a plus rien à voir avec eux (3 octets) — d'où CAM_TAILLE_PX au lieu de sizeof(lv_color_t).
            d.header.magic = LV_IMAGE_HEADER_MAGIC;
            d.header.cf = LV_COLOR_FORMAT_RGB565;
            d.header.stride = (uint32_t)g_job.w * CAM_TAILLE_PX; // la 9.6 le calculerait (draw/lv_image_dsc.h) : posé ici, pas de surprise
            d.header.w = g_job.w;
            d.header.h = g_job.h;
            d.data_size = g_job.w * g_job.h * CAM_TAILLE_PX;
            d.data = (const uint8_t *)g_px[g_job.tampon];
            poser(g_job.tampon);
        }
            g_somme_tele += g_job.t_tele;
            g_somme_deco += g_job.t_deco;
            g_somme_attente += ui_camera_t_attente;
            g_somme_reception += ui_camera_t_reception;
            g_somme_octets += g_octets;
            if (++g_images == 1)
                ui_trace("[camera] 1re image : téléchargée en %u ms, décodée en %u ms\n", (unsigned)g_job.t_tele, (unsigned)g_job.t_deco);
        }
        else if (g_zone && !g_job.ok)
            g_derniere = lv_tick_get() + CAM_REESSAI_MS - CAM_PERIODE_MS; // caméra muette : on attend avant de réessayer
    }
    // 29/10 : caméra quittée et plus rien en route : on coupe les flux (ui_camera_couper)
    if (!g_zone && !g_job.en_cours && !g_flux.en_route && ui_camera_couper)
        ui_camera_couper();
    // 08/11 : au même point, mais en deux temps — demander aux deux tâches de fond de se rendre, puis rendre les tampons
    // une fois leurs piles libérées (les deux tâches sont au repos ici : voir le commentaire de rendre_tampons).
    if (!g_zone && !g_job.en_cours && !g_flux.en_route && !g_rendu_demande && camera_tient_une_chose())
    {
        g_rendu_demande = true;
        g_rendu_quand = lv_tick_get();
        g_rendu_attente_dite = false;
        if (ui_camera_arreter_taches)
            ui_camera_arreter_taches();
        else
            rendre_tampons(0); // simulateur : pas de tâche de fond, rien ne tient les tampons
    }
    else if (g_rendu_demande && !g_job.en_cours && !g_flux.en_route)
    {
        size_t piles = 0;
        if (taches_rendues(&piles))
            rendre_tampons(piles);
        else if (!g_rendu_attente_dite && lv_tick_elaps(g_rendu_quand) >= 5000) // anormal : le dire, ne rien rendre
        {
            g_rendu_attente_dite = true;
            ui_trace("[camera] rendu des tampons en attente : les tâches de fond ne se sont pas rendues après 5 s\n");
        }
    }
    if (g_zone && g_job.en_cours && g_job.direct && g_enchainer)
        figer_zone();
    if (g_zone && lv_tick_elaps(g_compte_depuis) >= 30000) // cadence réelle, mesurée sur l'écran : une ligne / 30 s
    {
        const unsigned n = LV_MAX(1u, (unsigned)g_images);
        ui_trace("[camera] %s : %u images en 30 s ; par image : %u o, téléchargement %u ms (attente %u + réception %u), décodage %u ms "
                 "(les deux en même temps) ; %u doublons écartés\n",
#ifdef ARDUINO
                 "ESP_NEW_JPEG",
#else
                 "JPEGDEC",
#endif
                 (unsigned)g_images, (unsigned)(g_somme_octets / n), (unsigned)(g_somme_tele / n), (unsigned)(g_somme_attente / n),
                 (unsigned)(g_somme_reception / n), (unsigned)(g_somme_deco / n), (unsigned)g_doublons);
        { // 13/11 : photos des éléments posés sur la caméra (vivantes + en attente de libération) et PSRAM libre
            unsigned nb = 0, octets = 0;
            for (auto &k : g_calques)
                if (k.img)
                    nb++, octets += (unsigned)CAM_PHOTO_OCTETS(k.img);
            for (auto &l : g_a_liberer)
                if (l.img)
                    nb++, octets += (unsigned)CAM_PHOTO_OCTETS(l.img);
#ifdef ARDUINO
            ui_trace("[camera] calques : %u photos, %u Ko ; PSRAM libre %u Ko\n", nb, octets / 1024,
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
#endif
        }
        g_doublons = 0;
        g_images = 0;
        g_somme_tele = g_somme_deco = g_somme_attente = g_somme_reception = g_somme_octets = 0;
        g_compte_depuis = lv_tick_get();
    }
    // 07/11 : la garde qui suit sort sans rien dire ; si c'est la PSRAM qui manque, le dire une fois (pas à chaque
    // battement, l'anneau de traces ne garde qu'une quarantaine de lignes).
    // 08/11 : seulement quand une zone réclame la caméra — depuis ce soir les tampons sont RENDUS à la fermeture, ils sont
    // donc normalement absents caméra fermée (et pendant l'arrêt des tâches) : ce n'est plus une pénurie.
    if (!g_penurie_dite && g_zone && !g_rendu_demande && (!g_px[0] || !g_px[1] || !g_jpg[0] || !g_jpg[1] || !g_jpg[2]))
    {
        g_penurie_dite = true;
        ui_trace("[camera] tampons absents (px %p %p, jpg %p %p %p) : la caméra ne démarrera pas\n", (void *)g_px[0],
                 (void *)g_px[1], (void *)g_jpg[0], (void *)g_jpg[1], (void *)g_jpg[2]);
    }
    // g_rendu_demande : pas de travail neuf tant que les tâches de fond ne se sont pas rendues et que les tampons n'ont
    // pas été rendus (sinon on confierait un décodage à une tâche qui va se détruire, ou un tampon qui va être libéré)
    if (!g_zone || g_job.en_cours || g_rendu_demande || !g_px[0] || !g_px[1] || !g_jpg[0] || !g_jpg[1] || !g_jpg[2] || (g_derniere && lv_tick_elaps(g_derniere) < CAM_PERIODE_MS))
        return;
    const int w = lv_obj_get_content_width(g_zone), h = lv_obj_get_content_height(g_zone);
    if (w <= 0 || h <= 0 || w * h > CAM_MAX_PX)
        return;
    snprintf(g_job.url, sizeof g_job.url, "%s", g_url);
    g_job.w = w;
    g_job.h = h;
    g_job.tampon = g_affichee == 0 ? 1 : 0; // celle qui n'est pas à l'écran
    g_job.direct = ui_camera_ecran_lignes != nullptr;
    g_job.annule = false;
    if (g_job.direct)
        figer_zone();
    g_enchainer = g_job.direct;
    g_job.fini = false;
    g_job.en_cours = true;
    g_derniere = lv_tick_get();
    if (ui_camera_lancer ? !ui_camera_lancer(0, travail) : (!ui_images_en_fond || !ui_images_en_fond(travail)))
        travail(nullptr); // simulateur (ou tâche refusée) : tout de suite, dans la boucle
}
