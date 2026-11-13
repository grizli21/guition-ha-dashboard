// 22/10/2026 : simulateur PC de la nouvelle interface (src/ui_lvgl/), sans écran : le VRAI code des pages,
// avec la même configuration LVGL (include/lv_conf.h : RGB565 octets inversés), rendu dans une image
// puis enregistré en PNG. Les couleurs sont donc déjà réduites au 16 bits de la dalle (mêmes paliers de gris).
// Construire et lancer : make -C simu   (captures dans simu/captures/)
#include <lvgl.h>
#include <zlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "../src/ui_lvgl/ui_theme.h"
#include "../src/ui_lvgl/ui_shell.h"
#include "../src/ui_lvgl/page_thermostat.h"
#include "../src/ui_lvgl/page_graphe.h"
#include "../src/ui_lvgl/page_ballon.h"
#include "../src/ui_lvgl/page_vmc.h"
#include "../src/ui_lvgl/page_reglages.h"
#include "../src/ui_lvgl/page_infos.h"
#include "../src/ui_lvgl/page_ouvrants.h"
#include "../src/ui_lvgl/ui_demarrage.h"
#include "maquettes_eau.h"
#include "maquettes_batterie.h"
#include "maquettes_pc.h"
#include "../src/ui_lvgl/page_batterie.h"
#include "../src/ui_lvgl/page_radar.h"
#include "../src/ui_lvgl/page_cafe.h"
#include "../src/ui_lvgl/page_croquettes.h"
#include "../src/ui_lvgl/page_air.h"
#include "../src/ui_lvgl/page_poele.h"
#include "../src/ui_lvgl/page_lumieres.h"
#include "../src/ui_lvgl/page_panneau_led.h" // 02/11/2026 : panneau de luminothérapie de l'atelier
#include "../src/ui_lvgl/page_musique.h"
#include "../src/ui_lvgl/page_imprimante.h"
#include "../src/ui_lvgl/page_pc.h"
#include "../src/ui_lvgl/page_ia.h" // 11/11/2026 : page « IA » séparée, après PC dans le rail
#include "../src/ui_lvgl/ui_mqtt.h"
#include "../src/ui_lvgl/ui_demo.h"
#include "../src/ui_lvgl/ui_images.h"
#include "../src/ui_lvgl/ui_camera.h"
#include "../src/ui_lvgl/ui_musique.h"
#include "../src/ui_lvgl/ui_appareil.h"
#include <ArduinoJson.h>
#include <map>
#if LVGL_VERSION_MAJOR >= 9
// 08/11/2026 : l'audit des redessins (fin de fichier) lit les aires marquées à redessiner ; sous 9.6 elles sont dans
// l'en-tête privé du display. Le simulateur est un outil de mesure, il les lit directement (comme le moniteur px_num
// de la 8.4). Rien dans src/ ne fait ça.
#include "../lib/lvgl-9.6.0/src/display/lv_display_private.h"
#endif
#ifdef AUDIT_PILE
// 27/10 : variante « make trace » : chaque zone marquée à redessiner garde sa pile d'appels (qui l'a demandée)
#include <execinfo.h>
#include <vector>
static bool g_audit_pile = false;
static std::vector<std::vector<void *>> g_piles;
extern "C" void __real__lv_inv_area(lv_disp_t *, const lv_area_t *);
extern "C" void __wrap__lv_inv_area(lv_disp_t *d, const lv_area_t *a)
{
    if (g_audit_pile && a)
    {
        void *p[12];
        const int n = backtrace(p, 12);
        g_piles.emplace_back(p + 1, p + n);
    }
    __real__lv_inv_area(d, a);
}
// la première fonction hors de LVGL de chaque pile (fichier:ligne), sans doublons
static std::string origines()
{
    std::string res;
    for (auto &pile : g_piles)
    {
        std::string cmd = "addr2line -f -C -e ./simu-trace";
        char b[32];
        for (void *a : pile)
        {
            snprintf(b, sizeof b, " %p", (void *)((char *)a - 1));
            cmd += b;
        }
        FILE *f = popen(cmd.c_str(), "r");
        char fn[512], lieu[512];
        std::string trouve;
        while (f && fgets(fn, sizeof fn, f) && fgets(lieu, sizeof lieu, f))
        {
            fn[strcspn(fn, "\n")] = 0;
            lieu[strcspn(lieu, "\n")] = 0;
            if (strstr(lieu, "lvgl-8.4.0") || strstr(lieu, "ui_theme.cpp") || !strcmp(fn, "??"))
                continue;
            const char *court = strrchr(lieu, '/');
            trouve = std::string(fn) + " (" + (court ? court + 1 : lieu) + ")";
            break;
        }
        if (f)
            pclose(f);
        if (!trouve.empty() && res.find(trouve) == std::string::npos)
            res += (res.empty() ? "" : " ; ") + trouve;
    }
    g_piles.clear();
    return res;
}
#endif
#include <string>

static bool g_simu_picore = false; // piCore présent dans MA (scénario de la lecture en attente)
#include "../src/ui_lvgl/page_musique.h"
#include "donnees/vignette_test.h"
#include "donnees/vignette_test_52.h"
#include "icones.h"
#include "icones_ventilo.h"
#include "icones_pc.h"
#include "icones_ia.h" // 11/11/2026 : page « IA » (captures et planche des 3 icônes)

static const int L = 480, H = 480;

#if defined(PAGES_BANC) && LVGL_VERSION_MAJOR >= 9
// 08/11/2026 : le banc du scroll par bande (pages_banc.cpp, display en direct_mode avec deux tampons plein écran et
// moniteur branché au driver) est écrit pour LVGL 8.4 : ses wraps au lien portent sur _lv_disp_refr_timer,
// _lv_inv_area, lv_disp_drv_register. Il reste dans la ligne morte « make -C simu -f Makefile.84 pages ».
#error "PAGES_BANC est de la ligne 8.4 : make -C simu -f Makefile.84 pages"
#endif

// --- horloge virtuelle (remplace millis() d'Arduino) ---
static uint32_t horloge_ms = 0;
extern "C" uint32_t millis(void) { return horloge_ms; }
// 08/11 : sous 9.6, LVGL ne prend plus l'horloge par la conf (LV_TICK_CUSTOM) mais par une fonction :
// lv_tick_set_cb(horloge_lvgl) dans main(). Le temps n'avance toujours qu'à travers avancer().
extern "C" uint32_t horloge_lvgl(void) { return horloge_ms; }

// --- écran virtuel ---
// 08/11/2026 : RVB565 BRUT, tel que LVGL 9.6 l'écrit (octet de poids faible d'abord). La capture le décode à la main
// (capture()) : c'est ainsi que le harnais 9.6 de simu/v9 lit l'écran, et c'est ce qui doit correspondre aux captures
// prises sur la dalle (leçon de la caméra : l'ordre des octets décide de la couleur).
static uint16_t ecran[L * H];
#ifdef PAGES_BANC
// 05/11 : banc scroll par bande — display en direct_mode avec deux tampons plein écran, comme la
// cuisine en rotation 0 : le tampon reçu AU FLUSH EST l'image. Recopier les 921 ko de l'écran à
// chaque flush (le geste ci-dessous) ajouterait un coût PC fixe par image et masquerait l'écart
// de rendu entre marqué et témoin : on ne recopie que quand une capture est demandée.
extern "C" void banc_montrer_tampon(void * tampon);
extern "C" void banc_brancher_monitor(lv_disp_drv_t * drv);
static lv_color_t * tampon_shown = nullptr;
static void flush_cb(lv_disp_drv_t *disp, const lv_area_t *a, lv_color_t *px)
{
    (void)a; // en direct_mode, l'aire reçue est l'écran entier
    tampon_shown = px;
    banc_montrer_tampon(px);
    lv_disp_flush_ready(disp);
}
#else
static uint64_t g_flush_px; // 12/11 : pixels redessinés depuis la dernière remise à zéro
static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    // 9.6 en mode PARTIEL (lu, core/lv_refr.c:1430 et get_max_row :1354-1364) : px_map = le DÉBUT du tampon,
    // qui contient l'aire à partir de son coin haut gauche, une ligne = largeur de l'aire (stride calculé sur la
    // largeur de l'AIRe, pas de l'écran). Écrire comme en direct_mode (y*stride_écran + x1) déborde : premier
    // essai ici, plantage dans memcpy (gdb, 08/11 21 h 05).
    const int w = lv_area_get_width(a), h = lv_area_get_height(a);
    g_flush_px += (uint64_t)w * h; // 12/11 : surface redessinée (mesure du coût du ciel animé)
    const size_t stride = lv_draw_buf_width_to_stride(w, lv_display_get_color_format(disp));
    for (int y = 0; y < h; y++)
        memcpy(&ecran[(size_t)(a->y1 + y) * L + a->x1], px + (size_t)y * stride, (size_t)w * 2);
    lv_display_flush_ready(disp);
}
#endif

// --- doigt virtuel ---
static bool doigt_pose = false;
static int doigt_x = 0, doigt_y = 0;
static void touch_cb(lv_indev_t *, lv_indev_data_t *d)
{
    d->point.x = doigt_x;
    d->point.y = doigt_y;
    d->state = doigt_pose ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// fait tourner LVGL pendant ms millisecondes de temps virtuel
static void avancer(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5)
    {
        horloge_ms += 5;
        lv_timer_handler();
        ui_camera_tache(); // 26/10 : caméras (images locales : donnees/cameras/<n>.jpg)
    }
}
static void toucher(int x, int y, uint32_t ms = 80) { doigt_x = x; doigt_y = y; doigt_pose = true; avancer(ms); }
static void lacher() { doigt_pose = false; avancer(100); }
static void glisser(int x0, int y0, int x1, int y1, int pas = 12)
{
    toucher(x0, y0);
    for (int i = 1; i <= pas; i++)
    {
        doigt_x = x0 + (x1 - x0) * i / pas;
        doigt_y = y0 + (y1 - y0) * i / pas;
        avancer(30);
    }
}

#if LVGL_VERSION_MAJOR >= 9
#define UI_CLASSE_BOUTON lv_button_class // 9.6 : lv_btn_class -> lv_button_class
#define UI_CLASSE_IMAGE  lv_image_class // idem : lv_img_class -> lv_image_class
static void envoyer_clic(lv_obj_t *o) { lv_obj_send_event(o, LV_EVENT_CLICKED, NULL); } // lv_event_send -> lv_obj_send_event
#else
#define UI_CLASSE_BOUTON lv_btn_class
#define UI_CLASSE_IMAGE  lv_img_class
static void envoyer_clic(lv_obj_t *o) { envoyer_clic(o); }
#endif

// --- PNG (zlib) ---
static void png_bloc(FILE *f, const char *type, const std::vector<uint8_t> &d)
{
    uint8_t n[4] = {uint8_t(d.size() >> 24), uint8_t(d.size() >> 16), uint8_t(d.size() >> 8), uint8_t(d.size())};
    fwrite(n, 1, 4, f);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    fwrite(type, 1, 4, f);
    if (!d.empty())
    {
        fwrite(d.data(), 1, d.size(), f);
        crc = crc32(crc, d.data(), d.size());
    }
    uint8_t c[4] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)};
    fwrite(c, 1, 4, f);
}
#ifdef COMPTES
extern "C" void ui_comptes_marquer(const char * nom);   // simu/comptes.cpp : compteurs par primitive de dessin
#endif
static void capture(const char *nom)
{
#ifdef PAGES_BANC
    if (tampon_shown) // direct_mode : l'image EST dans le dernier tampon montré
        for (int i = 0; i < L * H; i++)
            ecran[i] = tampon_shown[i];
#endif
    std::vector<uint8_t> brut;
    brut.reserve((L * 3 + 1) * H);
    for (int y = 0; y < H; y++)
    {
        brut.push_back(0); // pas de filtre
        for (int x = 0; x < L; x++)
        {
            // RVB565 -> RVB888 comme le harnais 9.6 (simu/v9/harnais_v9.cpp:207-211) : 5 bits montants vers 8,
            // pas un décalage brut (sinon le blanc devient gris et les captures ne ressemblent plus à l'écran).
            const uint16_t c = ecran[y * L + x];
            brut.push_back(uint8_t(((c >> 11) * 255 + 16) / 32));
            brut.push_back(uint8_t(((((c >> 5) & 0x3F) * 255 + 32) / 64)));
            brut.push_back(uint8_t((((c & 0x1F) * 255 + 16) / 32)));
        }
    }
    uLongf taille = compressBound(brut.size());
    std::vector<uint8_t> z(taille);
    compress2(z.data(), &taille, brut.data(), brut.size(), 9);
    z.resize(taille);

    std::string chemin = std::string("captures/") + nom + ".png";
    FILE *f = fopen(chemin.c_str(), "wb");
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr = {0, 0, uint8_t(L >> 8), uint8_t(L), 0, 0, uint8_t(H >> 8), uint8_t(H), 8, 2, 0, 0, 0};
    png_bloc(f, "IHDR", ihdr);
    png_bloc(f, "IDAT", z);
    png_bloc(f, "IEND", {});
    fclose(f);
#ifdef COMPTES
    ui_comptes_marquer(nom);   // ce que cette vue a dessine depuis la precedente
#endif
    printf("capture : %s\n", chemin.c_str());
}

// --- mêmes pages et mêmes données fictives que le banc (src/ecran/ecran.cpp) ---
static void page_a_venir(lv_obj_t *zone, const char *nom)
{
    lv_obj_t *t = ui_label(zone, UI_POLICE_TITRE, UI_TEXTE, nom);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_t *s = ui_label(zone, UI_POLICE_TEXTE, UI_TEXTE3, "Page à venir");
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 0);
}
#define A_VENIR(fn, nom) \
    static void fn(lv_obj_t *z) { page_a_venir(z, nom); }

static const ui_page_t pages[] = {
    {&icon_room, "Chauffage", page_thermostat_creer, page_thermostat_quitter},
    {&icon_graphic, "Graphes", page_graphe_creer, page_graphe_quitter},
    {&icon_pm25, "Qualité de l'air", page_air_creer, page_air_quitter},
    {&icon_store, "Ouvrants", page_ouvrants_creer, page_ouvrants_quitter},
    {&icon_cat, "Croquettes", page_croquettes_creer, page_croquettes_quitter},
    {&icon_cafe2, "Café", page_cafe_creer, page_cafe_quitter},
    {&icon_robinet, "Eau chaude", page_ballon_creer, page_ballon_quitter},
    {&icon_ventilo, "VMC", page_vmc_creer, page_vmc_quitter},
    {&icon_flamme, "Poêle", page_poele_creer, page_poele_quitter},
    {&icon_music, "Musique", page_musique_creer, page_musique_quitter},
    {&icon_ampoule, "Lumières", page_lumieres_creer, page_lumieres_quitter},
    {&icon_panneau_led, "Panneau LED", page_panneau_led_creer, page_panneau_led_quitter}, // 02/11/2026
    {&icon_imprimante, "Imprimante 3D", page_imprimante_creer, page_imprimante_quitter},
    {&icon_pc, "PC", page_pc_creer, page_pc_quitter},
    {&icon_ia_etincelle, "IA", page_ia_creer, page_ia_quitter}, // 11/11/2026 : juste après PC ; icône 2 (Clawd + étincelle), la retenue
    {&icon_config, "Réglages", page_reglages_creer, page_reglages_quitter},
    {&icon_info, "Informations", page_infos_creer, page_infos_quitter},
    {&icon_bat3, "Batterie et capteur", page_batterie_creer, page_batterie_quitter, true}, // 26/10 : écran sur batterie, épinglée
};

// index d'une page par son nom (le scénario ne dépend pas de l'ordre du carrousel)
static void aller(const char *nom)
{
    for (int i = 0; i < (int)(sizeof(pages) / sizeof(pages[0])); i++)
        if (!strcmp(pages[i].nom, nom))
        {
            ui_shell_aller(i);
            return;
        }
    printf("page inconnue : %s\n", nom);
}

#ifdef PAGES_BANC
// =====================================================================
// 05/11/2026 : banc « gain du scroll par bande, page par page ».
// Pages énumérées depuis `pages[]` (le rail de l'application, pas une liste inventée), ouvertes
// par le code de navigation (ui_shell_aller), glissé vertical réel (glisser = le faux pavé
// tactile), compteurs par image de rendu dans simu/pages_banc.cpp (px_num du monitor_cb + µs PC).
// Le chemin rapide n'est pas simulé ici : c'est celui de la librairie, branché par les adaptateurs
// du simulateur (direct_mode + deux tampons plein écran, comme la cuisine en rotation 0).
// =====================================================================
extern "C" {
void banc_cadre_debut(void);
void banc_phase(int p);
void banc_page_debut(void);
void banc_garde_compte(int *appels, int *acceptees);
void banc_cadre_fin(const char *nom, const char *zone, int marque, int a_change, long delta_scroll, int defilable);
}

// premier conteneur défilable dont le contenu dépasse, dans la zone demandée :
// page (x < UI_ZONE_L) ou rail du carrousel (x >= UI_ZONE_L, marqué par ui_shell.cpp:115)
static lv_obj_t *defilable_zone(lv_obj_t *o, bool rail)
{
    if (o != lv_scr_act() && lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) &&
        !lv_obj_has_flag(o, LV_OBJ_FLAG_FLOATING) && lv_obj_get_scroll_bottom(o) > 0 &&
        ((rail && lv_obj_get_x(o) >= UI_ZONE_L) || (!rail && lv_obj_get_x(o) < UI_ZONE_L)))
        return o;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
    {
        lv_obj_t *r = defilable_zone(lv_obj_get_child(o, (int32_t)i), rail);
        if (r)
            return r;
    }
    return nullptr;
}

// diagnostic de marquage : ce que la page a posé comme drapeau USER_1 et ce qui déborde vraiment
static void decrire_arbre(lv_obj_t *o, int &marques, int &defilables)
{
    const bool scrollable = lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    const bool marque = lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1);
    if (o != lv_scr_act() && (scrollable || marque))
    {
        const lv_coord_t bas = scrollable ? lv_obj_get_scroll_bottom(o) : 0;
        if (marque)
            marques++;
        if (scrollable && bas > 0 && !lv_obj_has_flag(o, LV_OBJ_FLAG_FLOATING) && lv_obj_get_x(o) < UI_ZONE_L)
            defilables++;
        if (marque || (scrollable && bas > 0))
            printf("[banc-marque] obj=%p x=%d y=%d %dx%d scroll_bas=%d scroll_dir=%d barre=%d user1=%d\n",
                   (void *)o, lv_obj_get_x(o), lv_obj_get_y(o), lv_obj_get_width(o), lv_obj_get_height(o),
                   bas, (int)lv_obj_get_scroll_dir(o), (int)lv_obj_get_scrollbar_mode(o), marque ? 1 : 0);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
        decrire_arbre(lv_obj_get_child(o, (int32_t)i), marques, defilables);
}

#ifdef DEFILERAPIDE_OFF
// Témoin : le drapeau de compilation ne sort que page_infos de la marque (src/ui_lvgl/page_infos.cpp).
// Les autres pages appellent ui_defilement_rapide() sans condition, donc ce banc retire lui-même la
// marque LV_OBJ_FLAG_USER_1 — c'est exactement la seule condition que regarde la librairie (lv_obj_pos.c).
static void defaire_marquage(lv_obj_t *o)
{
    lv_obj_clear_flag(o, LV_OBJ_FLAG_USER_1);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
        defaire_marquage(lv_obj_get_child(o, (int32_t)i));
}
#endif

// fenêtre de mesure par page : un glissé vertical, puis le relâchement et la réparation
static void mesurer_page(int i, bool rail)
{
    lv_obj_t *c = defilable_zone(lv_scr_act(), rail);
#ifdef PAGES_FORCER
    // ANNEXE (cible « pages_force ») : le banc demande lui-même la marque, avec la garde du helper
    // (ui_defilement_rapide) telle qu'elle est écrite dans src/. Ce n'est PAS l'application telle
    // qu'elle est livrée : ça ne chiffre que ce qu'on gagnerait à ce que la garde accepte.
    if (c && !rail && !lv_obj_has_flag(c, LV_OBJ_FLAG_USER_1))
        printf("[banc-forcer] %s : marquage demande par le banc -> %s\n",
               pages[i].nom, ui_defilement_rapide(c) ? "MARQUE" : "REFUSE");
#endif
    const int marque = (c && lv_obj_has_flag(c, LV_OBJ_FLAG_USER_1)) ? 1 : 0;
    const int page_avant = ui_shell_page();
    const lv_coord_t y_avant = c ? lv_obj_get_scroll_y(c) : 0;

    banc_cadre_debut();
    banc_phase(1);
    // Le geste vise le conteneur qui PEUT défiler (relevé dans l'arbre), pas une coordonnée fixe :
    // sinon, sur Lumières par exemple, le doigt tombe à côté de la liste (152 px de large) et ne
    // fait rien défiler — la page passerait alors pour « identique » alors que le test ne teste rien.
    {
        lv_area_t a;
        if (c)
        {
            lv_obj_get_coords(c, &a);
            const int cx = (a.x1 + a.x2) / 2;
            const int y0 = a.y2 - 20, y1 = a.y1 + 40;
            glisser(cx, y0, cx, y1, 16); // le doigt remonte la page : 16 pas
        }
        else
            glisser(200, 420, 200, 120, 16); // pas de conteneur défilable : geste standard au centre
    }
    banc_phase(2);
    lacher();
    avancer(1200); // réparation de fin de geste (minuterie 400 ms) + stabilisation
    banc_phase(0);

    const int a_change = (ui_shell_page() != page_avant) ? 1 : 0;
    const long delta = a_change ? 0 : long((c ? lv_obj_get_scroll_y(c) : 0) - y_avant);
    banc_cadre_fin(pages[i].nom, rail ? "rail" : "page", marque, a_change, delta, c ? 1 : 0);
}

static void banc_pages()
{
#ifdef DEFILERAPIDE_OFF
    printf("\n[banc] ===== run TEMOIN : marquage enleve (-DDEFILERAPIDE_OFF + USER_1 retire partout) =====\n");
#else
    printf("\n[banc] ===== run MARQUE : marquage tel que pose par les pages =====\n");
#endif
    printf("[banc] format : page, defilable, marque, a_change (le geste a change de page), delta (px defiles),\n");
    printf("[banc]          img_glisse / px_med_glisse / us_med_glisse, idem fenetre entiere, decisions du chemin rapide\n");

    const int nb = int(sizeof(pages) / sizeof(pages[0]));
    for (int i = 0; i < nb; i++)
    {
        printf("\n[banc] --- page %d/%d : %s ---\n", i + 1, nb, pages[i].nom);
        banc_page_debut();
        ui_shell_aller(i); // navigation de l'application (le rail)
        avancer(900);      // création + stabilisation : hors fenêtre de mesure
        int marques = 0, defilables = 0;
        decrire_arbre(lv_scr_act(), marques, defilables); // ce que la page a posé comme marque
        int g_appels = 0, g_acceptees = 0;
        banc_garde_compte(&g_appels, &g_acceptees);
        printf("[banc] garde appelee %d fois, marques acceptees %d ; USER_1 poses = %d ; "
               "conteneurs defilables de la page (contenu qui deborde) = %d\n",
               g_appels, g_acceptees, marques, defilables);
#ifdef DEFILERAPIDE_OFF
        defaire_marquage(lv_scr_act());
#endif
        mesurer_page(i, false); // la page elle-même (x = 200)

        // le rail du carrousel : marqué partout (ui_shell.cpp:115), il déborde de 622 px — même
        // geste, autre zone, même fenêtre de mesure
        ui_shell_aller(i); // au cas où le glissé précédent aurait changé de page
        avancer(400);
#ifdef DEFILERAPIDE_OFF
        defaire_marquage(lv_scr_act());
#endif
        mesurer_page(i, true);
    }
    fflush(stdout);
}
#endif

static void donnees_fictives()
{
    th_ext_temp = 7;
    th_ext_hum = 81;
    const float temps[] = {18.3f, 19.6f, 17.2f, 16.8f, 19.5f};
    for (int i = 0; i < TH_NB_PIECES; i++)
        th_pieces[i].temp = temps[i];
    th_pieces[1].mode = TH_CONFORT;
    th_pieces[1].consigne = 20;
    th_pieces[1].chauffe = true;
    th_pieces[2].mode = TH_ECO;
    th_pieces[2].consigne = 16;
}

static void historique_fictif()
{
    gr_minute_maintenant = 21 * 60 + 30;
    for (int k = 0; k < TH_NB_PIECES; k++)
    {
        gr_historique_t &g = gr_pieces[k];
        memset(g.chauffe, 0, sizeof g.chauffe);
        float t = 17.5f + k * 0.4f;
        bool chauf = false;
        for (int i = 0; i < GR_POINTS; i++)
        {
            int m = (gr_minute_maintenant - (GR_POINTS - 1 - i) * 5 + 1440) % 1440;
            float h = m / 60.0f;
            float c = ((h >= 6.5f && h < 8.5f) || (h >= 17 && h < 22)) ? 20 : 16;
            float e = 7.5f + 3.5f * sinf((h - 9) / 24 * 2 * (float)M_PI) + sinf(i * 0.7f + k) * 0.25f;
            if (t < c - 0.3f)
                chauf = true;
            if (t > c + 0.3f)
                chauf = false;
            t += chauf ? 0.045f : -0.006f * (t - e) / 4;
            t += sinf(i * 1.3f + k) * 0.02f;
            g.temp[i] = (int16_t)lroundf(t * 10);
            g.cons[i] = (int16_t)lroundf(c * 10);
            g.hum[i] = (uint8_t)lroundf(58 + 6 * sinf((h - 3) / 24 * 2 * (float)M_PI + k) + sinf(i * 0.9f) * 1.2f);
            if (chauf)
                g.chauffe[i >> 3] |= 1 << (i & 7);
            if (k == 0)
                gr_ext[i] = (int16_t)lroundf(e * 10);
        }
        th_pieces[k].temp = t;
    }
}

// 25/10 : 24 h du ballon jusqu'à 9h (comme la maquette validée) : refroidissement lent, puisages (douches, vaisselle),
// relais 00h24-03h54, heures creuses 00h24-05h24 et 13h54-16h54
static void journee_ballon(bc_historique_t &h)
{
    float t = 57;
    memset(&h, 0, sizeof h);
    static const uint8_t L24[24] = {0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 4, 0, 46, 0, 0, 0, 0, 0, 0, 0, 0, 0, 38, 6};
    for (int i = 0; i < BC_POINTS; i++)
    {
        const float hr = fmodf(9 + i * 5 / 60.0f, 24), pas = 5 / 60.0f;
        const float q = 8 * expf(-powf((hr - 7.3f) / 0.18f, 2)) + 4 * expf(-powf((hr - 12.6f) / 0.2f, 2)) +
                        9 * expf(-powf((hr - 21.2f) / 0.15f, 2));
        t -= 0.25f * pas + q * 0.12f;
        const bool relais = hr > 0.4f && hr < 3.9f, hc = (hr > 0.4f && hr < 5.4f) || (hr > 13.9f && hr < 16.9f);
        if (relais)
            t = fminf(60.0f, t + 4.5f * pas);
        h.temp[i] = (int16_t)(t * 10);
        h.consigne[i] = 550;
        if (i % 12 == 6) // les litres de l'heure, sur un point
            h.litres[i] = L24[i / 12];
        if (relais)
            h.chauffe[i >> 3] |= 1 << (i & 7);
        if (hc)
            h.creuses[i >> 3] |= 1 << (i & 7);
    }
    h.fin = 9 * 60 - 5;
}

// 26/10 : batterie et réveils fictifs (mêmes que la maquette validée), à l'heure du PC
static void batterie_fictive(bool charge)
{
    const uint32_t m = (uint32_t)time(nullptr);
    struct { float h, min; int cause; } ev[] = {{0.6f, 9, 0},  {1.4f, 3, 0},  {3.2f, 22, 1}, {4.0f, 6, 0},  {6.1f, 14, 0},
                                                 {8.9f, 2, 0},  {11.3f, 1, 2}, {11.4f, 1, 2}, {15.7f, 2, 0}, {17.1f, 8, 0},
                                                 {17.6f, 19, 1}, {18.4f, 5, 0}, {21.9f, 12, 0}, {22.6f, 4, 1}, {23.5f, 3, 0}};
    bt_etat.present = true;
    bt_etat.nb_eveils = 0;
    for (auto &e : ev)
        bt_etat.eveils[bt_etat.nb_eveils++] = {(uint32_t)(m - 86400 + e.h * 3600), (uint16_t)(e.min * 60), (uint8_t)e.cause};
    float pct = 92;
    for (int i = 0; i < BT_COURBE; i++)
    {
        const float h = (i + 1) / 4.0f;
        float allume = 0;
        for (auto &e : ev)
            allume += fmaxf(0.0f, fminf(h, e.h + e.min / 60) - fmaxf(h - 0.25f, e.h));
        pct -= 0.06f + allume * 9.0f;
        bt_etat.courbe[i] = (uint8_t)(pct + 0.5f);
    }
    bt_etat.courbe_fin = m;
    bt_etat.pourcent = 78;
    bt_etat.tension = charge ? 4.12f : 3.92f;
    bt_etat.charge = charge;
    bt_etat.depuis = charge ? m - 35 * 60 : m - (86400 + 19 * 3600);
    bt_etat.cause = BT_RADAR;
    bt_etat.reveil = m - 3 * 60;
    static const uint32_t ETAPES[BT_ETAPES] = {0, 412, 574, 1386, 1452}; // 31/10 : réveil radar, Wi-Fi coupé après la synchro
    memcpy(bt_etat.etapes, ETAPES, sizeof ETAPES);
    int niveau;
    uint32_t couleur;
    bt_icone(niveau, couleur);
    static const lv_img_dsc_t *const PILES[] = {&icon_bat0, &icon_bat1, &icon_bat2, &icon_bat3};
    ui_shell_icone((int)(sizeof(pages) / sizeof(pages[0])) - 1, PILES[niveau], couleur);
    page_batterie_maj();
}

static void commande_ballon(const bc_etat_t &e)
{
    bc_etat.chauffe = e.mode == BC_FORCE || (e.mode == BC_AUTO && e.temp < e.consigne - 2); // simule HA
    printf("[ballon] mode %d consigne %.0f eco %d\n", (int)e.mode, e.consigne, (int)e.eco);
    page_ballon_maj();
}

static void commande_fictive(int piece, th_mode_t mode, float consigne)
{
    th_piece_t &p = th_pieces[piece];
    p.chauffe = mode != TH_ARRET && p.temp < consigne - 0.3f;
    printf("[thermostat] %s : mode %d consigne %.1f\n", p.nom, (int)mode, consigne);
    page_thermostat_maj();
}

// 13/11 : serviceur d'images locales du simulateur — même code que le lambda installé le 23/10 avant la
// section MQTT : imageproxy de Music Assistant -> donnees/pochettes/<clé>.jpg ; vignettes Moonraker ->
// donnees/vignettes/<fichier, %XX décodés>. Installé dès la page Musique (la pochette en lecture en a
// besoin), et réinstallé plus bas avant la section MQTT — fonction nommée pour ne pas dupliquer le corps.
static bool servir_images_locale(const char *url, uint8_t **donnees, size_t *taille)
{
    const char *nom = strrchr(url, '/') + 1;
    std::string chemin = "donnees/vignettes/";
    if (strstr(url, "/imageproxy/")) // pochette de Music Assistant : donnees/pochettes/<clé>.jpg
    {
        std::string c = std::string("donnees/pochettes/") + std::string(nom, strcspn(nom, "?")) + ".jpg"; // JPEG, comme MA les sert
        FILE *f = fopen(c.c_str(), "rb");
        if (!f)
            return false;
        fseek(f, 0, SEEK_END);
        *taille = ftell(f);
        fseek(f, 0, SEEK_SET);
        *donnees = (uint8_t *)malloc(*taille);
        bool ok = fread(*donnees, 1, *taille, f) == *taille;
        fclose(f);
        return ok;
    }
    for (const char *c = nom; *c; c++) // décodage des %XX
        if (*c == '%' && c[1] && c[2])
        {
            char h[3] = {c[1], c[2], 0};
            chemin += (char)strtol(h, nullptr, 16);
            c += 2;
        }
        else
            chemin += *c;
    FILE *f = fopen(chemin.c_str(), "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    *taille = ftell(f);
    fseek(f, 0, SEEK_SET);
    *donnees = (uint8_t *)malloc(*taille);
    bool ok = fread(*donnees, 1, *taille, f) == *taille;
    fclose(f);
    return ok;
}

// 13/11 : séquence servie par la caméra du simulateur (GIF « navigation » du README) : nom du dossier
// donnees/cameras/<dossier>/NNN.jpg, vide = image fixe ; compteur d'image, bouclé sur 12.
static char g_seq_cam[64] = "";
static int g_seq_i = 0;

int main()
{
#ifdef MODE_DEMO
    // MODE DÉMO : pas de caméras simulées (donnees/cameras), pas de tests de texte — les zones caméra
    // restent vides, comme sur l'écran de démo sans réseau.
#else
    // 26/10 : caméras : donnees/cameras/<n>.jpg (vraies images relevées le 26/10 : 2 = entrée, 3 = imprimante) ; 02/11 : les
    // flux go2rtc (depuis le 28/10, la zone restait vide au simulateur)
    ui_camera_lire = [](int, const char *url, uint8_t *buf, size_t max, size_t *n)
    {
        // 08/11 : le plateau a son propre flux (UI_CAM_PLATEAU, src=plateau_ecran) : image fixe rejouée, prise
        // sur le NAS (go2rtc) le 07/11 — c'est la même que celle de la maquette du banc (banc du 08/11).
        const char *cam = strstr(url, "src=imprimante") ? "3" : strstr(url, "src=terrasse") ? "2"
                          : strstr(url, "src=plateau_ecran") ? "plateau" : nullptr;
        if (!cam)
            return false;
        // 13/11 (GIF « navigation » + vignette du README) : g_seq_cam = dossier de la séquence servie
        // image par image (donnees/cameras/<dossier>/NNN.jpg), vide = image fixe. Les images de la
        // séquence de l'imprimante sont de la VRAIE caméra du caisson de l'auteur (publiées avec son
        // accord), prises pendant un « park » à 20 images/s : le flux simulé, tiré ~2 fois par pas de
        // 10 ms, consomme une frame toutes les 10 tirages (g_seq_i/10) pour tenir la cadence réelle.
        if (g_seq_cam[0])
        {
            char seq[128];
            snprintf(seq, sizeof seq, "donnees/cameras/%s/%03d.jpg", g_seq_cam, g_seq_i++ / 10 % 10);
            FILE *fs = fopen(seq, "rb");
            if (fs)
            {
                *n = fread(buf, 1, max, fs);
                fclose(fs);
                if (*n > 0)
                    return true; // la séquence boucle (% 10) ; si le fichier manque, on retombe sur l'image fixe ci-dessous
            }
        }
        FILE *f = fopen((std::string("donnees/cameras/") + cam + ".jpg").c_str(), "rb");
        if (!f)
            return false;
        *n = fread(buf, 1, max, f);
        fclose(f);
        return *n > 0;
    };
    // 25/10 : ui_texte_propre sur les vrais cas de l'audit de la bibliothèque de MA
    {
        const char *cas[] = {"Samba de Uma Nota Sу", "Faces а Faces", "Rafał Blechacz", "Prospekt\u0092s March",
                             "Beyoncé – Déjà vu…", "Caf\xc3", "\x7f", "Hi \U0001F600", "Stan Getz & Luiz Bonfá"};
        for (const char *c : cas)
        {
            char b[64];
            snprintf(b, sizeof b, "%s", c);
            ui_texte_propre(b);
            printf("[texte] « %s » -> « %s »\n", c, b);
        }
    }
#endif
    mkdir("captures", 0755);
    lv_init();
    // 08/11 : sous 9.6 l'horloge se donne par une fonction (il n'y a plus de LV_TICK_CUSTOM dans la conf).
    // Oublié au port : les temporisants ne tournaient pas, LVGL ne rendait qu'une fois — toutes les captures
    // identiques au octet près (18 018 o), retrouvé au sha256 le 08/11 21 h 20.
    lv_tick_set_cb(horloge_lvgl);

    // 24/10 : comme l'ESP32 (ecran.cpp) : deux tampons de 80 lignes, alloués (AddressSanitizer voit un débordement)
    // -> le rendu par bandes est le même ; avant, l'écran entier d'un coup ne découpait jamais un objet
#ifdef PAGES_BANC
    static lv_disp_draw_buf_t draw_buf; // (8.4 : le banc « pages », voir la garde en tête de fichier)
    // 05/11 : banc scroll par bande — le display est configuré comme la CUISINE EN ROTATION 0
    // (src/ecran/ecran.cpp, le seul écran où le chemin rapide de la librairie
    // s'active) : direct_mode + deux tampons PLEIN ÉCRAN 480×480. Sans ces trois conditions
    // (direct_mode, buf2, adaptateur ui_ecran_memoire_active non NULL), _lv_obj_scroll_fast_shift
    // refuse et le banc ne mesurerait que le chemin d'origine.
    lv_color_t *b1 = (lv_color_t *)malloc(L * H * sizeof(lv_color_t)), *b2 = (lv_color_t *)malloc(L * H * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&draw_buf, b1, b2, L * H);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = L;
    disp_drv.ver_res = H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.direct_mode = 1;
    banc_brancher_monitor(&disp_drv); // px_num par image : la métrique identique à celle de l'écran
    lv_disp_drv_register(&disp_drv);  // (avant l'enregistrement : le driver est recopié)
#else
    // 08/11/2026 — API 9.6. Comme l'ESP32 (src/ecran/ecran.cpp) : deux tampons de 80 lignes, alloués (AddressSanitizer
    // voit un débordement) -> le rendu par bandes est le même ; avant, l'écran entier d'un coup ne découpait jamais un
    // objet. Format par défaut de la conf : RVB565 (LV_COLOR_DEPTH 16), comme la dalle.
    uint8_t *b1 = (uint8_t *)malloc((size_t)L * 80 * 2), *b2 = (uint8_t *)malloc((size_t)L * 80 * 2);
    lv_display_t *disp = lv_display_create(L, H);
    lv_display_set_buffers(disp, b1, b2, (uint32_t)((size_t)L * 80 * 2), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
#endif

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_cb);
    lv_indev_set_display(indev, lv_display_get_default());

    ui_theme_init();
    // SIMU_VIDE=1 ./simu : écran sans aucune donnée (ni MQTT ni Home Assistant) : chaque page est ouverte, tapée au
    // hasard et capturée (captures/vide_*.png). Rien ne doit planter : à passer sous make asan (10/11 : 0 erreur).
    const bool vide = getenv("SIMU_VIDE") != nullptr;
#ifdef MODE_DEMO
    // MODE DÉMO : aucune donnée fictive du simulateur. Seulement l'allocation des tableaux du graphe, puis
    // les branches réelles : commandes des pages -> publier() (affichée, interceptée par l'écho de ui_demo),
    // musique -> faux Music Assistant embarqué. Les données viennent de la table ui_demo, par ui_mqtt_recevoir.
    page_graphe_preparer();
    ui_musique_brancher(); // mu_commande + synchro de la page musique (le transport, juste après)
    ui_demo_brancher_ma();
    ui_mqtt_publier = [](const char *sujet, const char *message, bool)
    {
        printf("[demo][commande] %s : %s\n", sujet, message); // ce qui partirait au broker
        return true;
    };
    ui_mqtt_brancher();
#else
    if (!vide) donnees_fictives();
    page_graphe_preparer();
    if (!vide) historique_fictif();
    th_commande = commande_fictive;
#endif
    ui_shell_creer(pages, sizeof(pages) / sizeof(pages[0]), 0);
    avancer(600);
    if (vide)
    {
        const int n = (int)(sizeof(pages) / sizeof(pages[0]));
        for (int i = 0; i < n; i++)
        {
            ui_shell_aller(i);
            avancer(1500);
            char nom[32];
            snprintf(nom, sizeof nom, "vide_%02d", i);
            capture(nom);
            for (int y = 60; y < 480; y += 70) // quelques appuis au hasard : boutons et onglets sans données
                for (int x = 40; x < 400; x += 90) { toucher(x, y); lacher(); avancer(120); }
            avancer(600);
            snprintf(nom, sizeof nom, "vide_%02d_apres", i);
            capture(nom);
            printf("[vide] page %d (%s) ok\n", i, pages[i].nom);
        }
        printf("[vide] fin\n");
        return 0;
    }

    // 13/11 (tâche « effet escalier de la courbe orange ») : SIMU_GRAPHE_JSON=<fichier> rejoue un
    // historique RÉEL (format « Graphes » de CONTRAT_MQTT) sur la chambre 1 par le vrai chemin MQTT
    // (ui_mqtt_recevoir), SIMU_GRAPHE_NOM nomme la capture, et seul le scénario Graphes est joué.
    if (const char *chemin = getenv("SIMU_GRAPHE_JSON"))
    {
        const char *nom = getenv("SIMU_GRAPHE_NOM");
        FILE *f = fopen(chemin, "r");
        if (!f)
            printf("[graphe] SIMU_GRAPHE_JSON introuvable : %s\n", chemin);
        else
        {
            std::string payload;
            char lu[2048];
            size_t n;
            while ((n = fread(lu, 1, sizeof lu, f)) > 0)
                payload.append(lu, n);
            fclose(f);
            ui_mqtt_recevoir(UI_MQTT_PREFIXE "thermostats/chambre1/historique",
                              reinterpret_cast<const uint8_t *>(payload.data()), static_cast<unsigned>(payload.size()));
            aller("Graphes");
            page_graphe_banc_piece(1); // cuisine -> séjour -> chambre 1 (ordre PIECES de ui_mqtt.cpp)
            page_graphe_banc_piece(1);
            avancer(600);
            const char *base = (nom && *nom) ? nom : "90_graphe_reel";
            capture(base);
            toucher(250, 220, 300); // lecture au doigt : le curseur doit garder les valeurs MESURÉES
            std::string nd = std::string(base) + "_doigt";
            capture(nd.c_str());
            lacher();
        }
        return 0;
    }

#ifdef PAGES_BANC
    // banc du scroll par bande : le scénario de capture n'est pas joué (compteurs, pas images)
    banc_pages();
    return 0;
#endif

#ifdef MODE_DEMO
    // --- scénario de démo : rejou de la table, une capture par page, puis quelques appuis (avant/après) ---
    ui_demo_lancer();
    avancer(600);
    // Ciel et page IA : la démo calcule ses horodatages à l'allumage, en relatif par rapport à l'horloge
    // de l'écran — ici celle du PC : tout est cohérent sans rien imposer (sur l'écran réel, maison/etat
    // règle l'horloge sur le 17/11/2026 15:30 de la démo, cf. ui_demo.cpp).
    // heure courante des pages (café, croquettes) : 15:30, l'heure fixe de la démo. Et la page Informations,
    // comme sur l'écran réel où ui_appareil la remplit elle-même (ici : valeurs neutres, aucune donnée personnelle)
    cf_maintenant = 15 * 60 + 30;
    {
        const char *sys[] = {TXT("0 j 00 h", "0 d 00 h"), TXT("mise sous tension", "power-on"), TXT("218 Ko", "218 KB"),
                             TXT("6" SEP_DEC "1 Mo", "6" SEP_DEC "1 MB"), "44 °C", "240 MHz", "300 ms",
                             TXT("arrêté", "off"), TXT("mode démo", "demo mode"), TXT("aucune (pas de Wi-Fi)", "none (no Wi-Fi)")};
        for (int i = 0; i < IF_NB_SYSTEME; i++)
            snprintf(if_systeme[i].valeur, sizeof if_systeme[i].valeur, "%s", sys[i]);
        snprintf(if_liaisons[0].detail, sizeof if_liaisons[0].detail, "%s", TXT("coupé (mode démo)", "off (demo mode)"));
        if_liaisons[0].ok = false;
        snprintf(if_liaisons[1].detail, sizeof if_liaisons[1].detail, "%s", TXT("reçu il y a 2 s", "received 2 s ago"));
        if_liaisons[1].ok = true;
    }
    static const char *const DEMO_PAGES[] = {"Chauffage", "Graphes", "Qualité de l'air", "Ouvrants", "Croquettes",
                                             "Café", "Eau chaude", "VMC", "Poêle", "Musique", "Lumières", "Panneau LED",
                                             "Imprimante 3D", "PC", "IA", "Réglages", "Informations"};
    for (int i = 0; i < (int)(sizeof(DEMO_PAGES) / sizeof(DEMO_PAGES[0])); i++)
    {
        aller(DEMO_PAGES[i]);
        // Musique : la synchro se fait au premier tic (1 s) après l'entrée — laisser passer le tic
        avancer(strcmp(DEMO_PAGES[i], "Musique") ? 600 : 1500);
        char nom[48];
        snprintf(nom, sizeof nom, "demo_%02d", i + 1);
        capture(nom);
    }
    // les onglets de Musique (bibliothèque et radios lues par le faux Music Assistant) et de l'imprimante
    aller("Musique");
    avancer(1500); // premier tic : artistes, radios, lecteurs, file
    toucher(130, 24); // Biblio
    lacher();
    avancer(1500);
    capture("demo_17_musique_biblio");
    toucher(150, 100); // premier artiste : ses albums
    lacher();
    avancer(1200);
    capture("demo_18_musique_albums");
    toucher(220, 24); // Radios
    lacher();
    avancer(1200);
    capture("demo_19_musique_radios");
    aller("Imprimante 3D");
    avancer(600);
    toucher(245, 24); // Fichiers
    lacher();
    capture("demo_20_imprimante_fichiers");

    // --- appuis : l'écho renvoie l'état que Home Assistant publierait (avant / après, garde de 3 s passée) ---
    aller("Chauffage");
    avancer(600);
    capture("demo_21_chauffage_avant");
    toucher(250, 442); // Éco sur la pièce affichée
    lacher();
    avancer(3600);
    capture("demo_22_chauffage_apres");

    aller("VMC");
    avancer(600);
    capture("demo_23_vmc_avant");
    toucher(323, 220); // maison : Grande vitesse (troisième bouton de la carte du haut)
    lacher();
    avancer(3600);
    capture("demo_24_vmc_apres");

    aller("Lumières");
    avancer(600);
    capture("demo_25_lumieres_avant");
    toucher(24, 77); // rond de la première lampe (Lampadaire 1, allumée) : l'éteindre
    lacher();
    avancer(3600);
    capture("demo_26_lumieres_apres");

    aller("Ouvrants");
    avancer(600);
    capture("demo_27_ouvrants_avant");
    glisser(300, 100, 300, 197, 10); // volet cuisine : le curseur au doigt vers ~70 %
    lacher();
    avancer(3600);
    capture("demo_28_ouvrants_apres");

    aller("Café");
    avancer(600);
    toucher(268, 24); // onglet Bouilloire
    lacher();
    capture("demo_29_bouilloire_avant");
    toucher(200, 434); // « maintenant » : la bouilloire se met en chauffe (l'écho le confirme)
    lacher();
    avancer(3600);
    capture("demo_30_bouilloire_apres");
    return 0;
#else
    // --- scénario ---
    capture("01_thermostat");
    // 31/10 : anneau façon HA : chauffe en cours, consigne atteinte, arrêt (la pièce affichée, remise ensuite)
    {
        th_piece_t &p = th_pieces[th_piece_affichee];
        const th_piece_t garde = p;
        const struct { th_mode_t mode; float consigne, temp; bool chauffe; const char *nom; } cas[] = {
            {TH_CONFORT, 20, 16.4f, true, "01b_thermostat_chauffe"},
            {TH_MANUEL, 18.5f, 19.2f, false, "01c_thermostat_atteinte"},
            {TH_ARRET, 15, 17.3f, false, "01d_thermostat_arret"}};
        for (const auto &c : cas)
        {
            p.mode = c.mode, p.consigne = c.consigne, p.temp = c.temp, p.chauffe = c.chauffe;
            page_thermostat_maj();
            avancer(200);
            capture(c.nom);
        }
        // transition entre deux préréglages : Hors-gel (8°) puis Confort (20°), images à 0, 100, 200 et 400 ms
        p.mode = TH_HORSGEL, p.consigne = 8, p.temp = 16.4f, p.chauffe = false;
        page_thermostat_maj();
        avancer(200);
        toucher(348, 442); // Confort
        lacher();
        capture("01e_thermostat_transition_0");
        for (int i = 1; i <= 3; i++)
        {
            avancer(i == 3 ? 200 : 100);
            char nom[48];
            snprintf(nom, sizeof nom, "01e_thermostat_transition_%d", i == 3 ? 400 : i * 100);
            capture(nom);
        }
        // 07/11 (prod 8.4 du bureau, 814f4c7 + 83531a0) et reporté ici le 09/11 : balayage de l'anneau depuis
        // l'arrêt puis vers l'arrêt, une image toutes les 50 ms, dans les deux cas : consigne AU-DESSUS de la pièce
        // (Confort 20°, pièce 16,4° -> 01g) et AU-DESSOUS (Éco 16°, pièce 19,2° -> 01h). Le front est dans
        // page_thermostat.cpp (g_front, :50-51 et :267-302) : sans lui ces captures ne montreraient rien.
        const struct { int x; float temp; const char *nom; } balayages[] = {{348, 16.4f, "01g"}, {250, 19.2f, "01h"}};
        for (const auto &b : balayages)
            for (int sens = 0; sens < 2; sens++)
            {
                if (sens == 0)
                {
                    p.mode = TH_ARRET, p.consigne = 20, p.temp = b.temp, p.chauffe = false;
                    page_thermostat_maj();
                    avancer(600);
                }
                toucher(sens == 0 ? b.x : 56, 442); // Confort ou Éco, puis Arrêt
                doigt_pose = false;                 // relâché : le clic part, l'animation démarre
                for (int t = 0; t <= 450; t += 50)
                {
                    avancer(t == 0 ? 10 : 50);
                    char nom[48];
                    snprintf(nom, sizeof nom, "%s_thermostat_%s_%03d", b.nom, sens == 0 ? "marche" : "arret", t);
                    capture(nom);
                }
                avancer(300);
            }
        // 04/11 : glissé au doigt le long de l'anneau, Éco 16° -> ~21°, par le vrai chemin du toucher (arc LVGL) :
        // captures en cours de glissé et après le lâcher (comparaison au pixel près avant/après l'optimisation de l'anneau)
        {
            p.mode = TH_ECO, p.consigne = 16, p.temp = 16.4f, p.chauffe = false;
            page_thermostat_maj();
            avancer(600); // fin de transition éventuelle
            lv_point_t c;
            if (page_thermostat_centre_anneau(&c))
            {
                auto angle = [](float temp) { return (135 + 270 * (temp - 7.0f) / (24.0f - 7.0f)) * (float)M_PI / 180; };
                const float r = 137;
                const int PAS = 20;
                for (int i = 0; i <= PAS; i++)
                {
                    const float a = angle(16 + 5.0f * i / PAS);
                    doigt_x = (int)lroundf(c.x + r * cosf(a));
                    doigt_y = (int)lroundf(c.y + r * sinf(a));
                    if (i == 0)
                        toucher(doigt_x, doigt_y);
                    else
                        avancer(30);
                    if (i == 5 || i == 10 || i == 20)
                    {
                        char nom[48];
                        snprintf(nom, sizeof nom, "01f_thermostat_glisse_%02d", i);
                        capture(nom);
                    }
                }
                lacher();
                avancer(200);
                capture("01f_thermostat_glisse_lache");
            }
        }
        p = garde;
        page_thermostat_maj();
        avancer(200);
    }

    aller("Graphes");
    avancer(600);
    capture("02_graphe");

    // doigt posé sur le graphe : curseur + bulle
    toucher(250, 220, 300);
    capture("03_graphe_doigt");
    lacher();


    // --- eau chaude (25/10 : 3 onglets) : Ballon au repos puis en chauffe, Courbe (+ lecture au doigt), Réglages
    // (consigne glissée à 62°, Forcé, Arrêt) ---
    bc_etat = {54.8f, 55, BC_AUTO, true, false, 11, 54, 4.4f, 0, 0, 15};
    journee_ballon(bc_hist);
    bc_commande = commande_ballon;
    aller("Eau chaude");
    avancer(600);
    capture("05_eau_chaude");
    bc_etat.chauffe = true;
    bc_etat.puissance = 2400;
    bc_etat.debit = 6;
    page_ballon_maj();
    avancer(400);
    capture("05b_eau_chaude_chauffe");
    bc_etat.chauffe = false;
    bc_etat.puissance = 0;
    bc_etat.debit = 0;
    toucher(200, 23); // onglet Courbe
    lacher();
    avancer(300);
    capture("05c_eau_chaude_courbe");
    toucher(250, 118 + 100, 200); // lecture au doigt
    capture("05d_eau_chaude_courbe_doigt");
    lacher();
    toucher(301, 23); // onglet Réglages
    lacher();
    avancer(300);
    capture("06_eau_chaude_reglages");
    glisser(100, 66 + 250 - (int)((55 - 30) / 40.0f * 250), 100, 66 + 250 - (int)((62 - 30) / 40.0f * 250));
    capture("06b_eau_chaude_reglage_62");
    lacher();
    toucher(332, 360); // Forcé
    lacher();
    capture("07_eau_chaude_force");
    toucher(72, 360); // Arrêt
    lacher();
    capture("08_eau_chaude_arret");
    toucher(99, 23); // retour sur Ballon pour la suite
    lacher();

    // --- VMC : maison en grande vitesse par l'humidité, sous-sol en petite ; l'hélice tourne ---
    vmc_zones[0].vitesse = 2; vmc_zones[0].hum = 82; vmc_zones[0].auto_en_cours = true;
    vmc_zones[1].vitesse = 1; vmc_zones[1].hum = 64;
    aller("VMC");
    avancer(600);
    capture("09_vmc");
    avancer(150); // 150 ms plus tard : les hélices ont tourné (plus vite en haut)
    capture("10_vmc_150ms");
    toucher(70, 50 + 196 + 10 + 196 - 12 - 26); // sous-sol : Arrêt
    lacher();
    capture("11_vmc_sous_sol_arret");

    // 09/11 : GIF « hélices » du README (docs/images/helices-vmc.gif). Le sous-sol vient de passer à
    // l'arrêt : seule tourne la grande vitesse de la maison, TOUR_MS[2] = 900 ms par tour
    // (src/ui_lvgl/page_vmc.cpp:12 ; animation à répétition infinie ui_helice_tourner,
    // src/ui_lvgl/ui_theme.cpp:511-528). Une image toutes les 50 ms de temps virtuel pendant un tour
    // exactement : la boucle du GIF retombe sur la première image au tour près.
    {
        char nom[48];
        capture("anim_helice_000");
        for (int t = 50; t <= 850; t += 50)
        {
            avancer(50);
            snprintf(nom, sizeof nom, "anim_helice_%03d", t);
            capture(nom);
        }
    }

    // --- Réglages : haut de la page, puis défilée jusqu'en bas ---
    strcpy(rg_reglages.ssid, "Maison");
    strcpy(rg_reglages.ip, "192.168.1.41");
    rg_reglages.rssi = -60;
    aller("Réglages");
    avancer(600);
    capture("12_reglages");
    glisser(200, 420, 200, 80, 16); // le doigt remonte la page
    lacher();
    avancer(800);
    capture("13_reglages_bas");

    // --- Informations : haut, bas, puis remise à zéro (confirmation sur place) ---
    const char *sys[] = {TXT("2 j 04 h", "2 d 04 h"), TXT("mise sous tension", "power-on"), TXT("221 Ko", "221 KB"), TXT("6" SEP_DEC "1 Mo", "6" SEP_DEC "1 MB"),
                       "36 °C", "240 MHz", "312 ms", TXT("19" SEP_DEC "5 dBm", "19" SEP_DEC "5 dBm"), "94bbeaf (21/10)", "192.168.1.43"};
    for (int i = 0; i < IF_NB_SYSTEME; i++)
        snprintf(if_systeme[i].valeur, sizeof if_systeme[i].valeur, "%s", sys[i]);
    const char *det[] = {"Maison  -60 dBm", TXT("reçu il y a 2 s", "received 2 s ago"), "motionEye", TXT("hors ligne", "offline")};
    for (int i = 0; i < IF_NB_LIAISONS; i++)
    {
        snprintf(if_liaisons[i].detail, sizeof if_liaisons[i].detail, "%s", det[i]);
        if_liaisons[i].ok = i < 3;
    }
    if_journal_ajouter("hier", TXT("Redémarrage : mise sous tension", "Startup: power-on"), false);
    if_journal_ajouter("07:02", TXT("Coupure MQTT 14 s", "MQTT down 14 s"), true);
    if_journal_ajouter("19:40", TXT("Retour à l'accueil", "Back to home"), false);
    if_journal_ajouter("21:12", "Wi-Fi Maison -60 dBm", false);
    if_journal_ajouter("21:12", TXT("MQTT connecté en 2" SEP_DEC "3 s", "MQTT connected in 2" SEP_DEC "3 s"), false);
    aller("Informations");
    avancer(600);
    capture("14_infos");
    glisser(200, 420, 200, 60, 16);
    lacher();
    avancer(800);
    capture("15_infos_bas");
    toucher(200, 452); // remise à zéro : 1er appui = confirmation demandée
    lacher();
    capture("16_infos_confirmation");

    // --- Ouvrants : store à 60 %, réglage du volet au doigt, séance (HA descend la toile) ---
    ov_ouvrants[0].pos = 60;
    ov_commande = [](int idx, ov_action_t a, int pos)
    {
        printf("[ouvrants] %d action %d pos %d\n", idx, (int)a, pos);
        if (a == OV_SEANCE) // simule l'automatisation 1791000000163 : la toile suit la Fire TV
        {
            ov_toile = pos ? 0 : 100;
            page_ouvrants_maj();
        }
    };
    aller("Ouvrants");
    avancer(600);
    capture("17_ouvrants");
    // volet : le doigt descend du haut à 70 % de la hauteur du curseur (carte 2 : x 208..392, curseur y 50+10+32)
    glisser(300, 100, 300, 92 + 105, 10);
    capture("18_ouvrants_glisse");
    lacher();
    toucher(200, 332 + 136 - 10 - 20); // Séance
    lacher();
    capture("19_ouvrants_seance");

    // --- Café : à 21h30 ; programmé pour 6h45 (donc demain) ; bouilloire en chauffe à 80 ° ---
    cf_maintenant = 15 * 60 + 30;
    cf_commande = [](int app, cf_action_t a)
    {
        printf("[cafe] appareil %d action %d\n", app, (int)a);
        if (app == CF_BOUILLOIRE && a == CF_DIRECT) { cf_bouilloire_chauffe = true; page_cafe_maj(); }
        if (app == CF_BOUILLOIRE && a == CF_ARRETER) { cf_bouilloire_chauffe = false; page_cafe_maj(); }
    };
    aller("Café");
    avancer(600);
    capture("20_cafe");
    toucher(200, 360); // Programmer
    lacher();
    capture("21_cafe_programme");
    toucher(268, 24); // onglet Bouilloire
    lacher();
    toucher(200, 434); // chauffer maintenant
    lacher();
    capture("22_bouilloire_chauffe");

    // --- Croquettes : mercredi 21h30, 3 repas de 15 g + un 4e inactif ; semaine avec 2 jours à 2 repas ---
    cq_etat.bol = 21.0f; cq_etat.stock = 31; cq_etat.autonomie = 9;
    cq_etat.jour_semaine = 2; cq_etat.maintenant = 21 * 60 + 30;
    const int g7[] = {31, 47, 44, 29, 46, 52, 45}, n7[] = {2, 3, 3, 2, 3, 3, 3};
    for (int k = 0; k < 7; k++) cq_etat.jours[k] = {g7[k], n7[k]};
    cq_commande = [](cq_action_t a, int i) { printf("[croquettes] action %d repas %d\n", (int)a, i); };
    aller("Croquettes");
    avancer(600);
    capture("23_croquettes");
    toucher(160, 132 + 68 + 31); // heure du 2e repas : réglage de l'heure
    lacher();
    toucher(347, 132 + 68 + 31); // + 15 min
    lacher();
    capture("24_croquettes_heure");
    toucher(330, 438); // Semaine
    lacher();
    capture("25_croquettes_semaine");
    toucher(12 + 3 * 54 + 20, 250); // dimanche (2 repas : un point rouge)
    lacher();
    capture("26_croquettes_dimanche");

    // --- Qualité de l'air : un pic de cuisson en fin de journée dans la première pièce ---
    for (int k = 0; k < AQ_NB; k++)
    {
        aq_purificateur_t &p = aq_purificateurs[k];
        int base = k == 0 ? 18 : 6;
        for (int i = 0; i < AQ_HIST; i++)
            p.hist[i] = (uint8_t)LV_MAX(0, base + 6 * sinf(i / 7.0f + k) + (k == 0 && i > 34 && i < 40 ? 14 : 0) + sinf(i * 1.7f) * 1.5f);
        p.pm25 = p.hist[AQ_HIST - 1];
    }
    snprintf(aq_purificateurs[0].info, 32, TXT("COV 112 · 24 h", "VOC 112 · 24 h"));
    snprintf(aq_purificateurs[1].info, 32, TXT("Filtre 63 %% · 24 h", "Filter 63 %% · 24 h"));
    aq_purificateurs[0].mode = 2;
    aq_purificateurs[1].mode = 1;
    aller("Qualité de l'air");
    avancer(600);
    capture("27_air");

    // --- Poêle : feu de 3 h 20 avec une recharge oubliée (même simulation que la maquette) ---
    {
        auto feu = [](float t, float &f, float &c, float &ti)
        {
            const float u = t + 200;
            if (u < 0) { f = 22; c = 24; ti = 100; return; }
            if (u < 20) { f = 22 + u * 14; ti = 100; }
            else if (u < 40) { f = 302 - (u - 20) * 3; ti = 100 - (u - 20) * 3; }
            else if (u < 120) { f = 240 + 7 * sinf(u / 6) + 3 * sinf(u * 1.7f); ti = 45 + 8 * sinf(u / 9 + 1); }
            else if (u < 142) { f = 240 - (u - 120) * 3.1f + 2 * sinf(u * 1.3f); ti = LV_MIN(100, 45 + (u - 120) * 3); }
            else if (u < 156) { f = 172 + (u - 142) * 6.5f; ti = 100 - (u - 142) * 4.5f; }
            else { f = 263 - LV_MIN(23, (u - 156) * 1.4f) + 5 * sinf(u / 5) + 2 * sinf(u * 1.9f); ti = 38 + 6 * sinf(u / 8); }
            c = 24 + 140 * (1 - expf(-u / 40)) - (u > 125 && u < 160 ? 14 * sinf((u - 125) / 35 * (float)M_PI) : 0);
            ti = LV_CLAMP(30, ti, 100);
        };
        for (int i = 0; i < PL_HIST; i++)
        {
            float f, c, ti;
            feu(-(PL_HIST - 1 - i) * 5.0f, f, c, ti);
            pl_etat.h_fumees[i] = (int16_t)f;
            pl_etat.h_corps[i] = (int16_t)c;
            pl_etat.h_tirage[i] = (uint8_t)ti;
        }
        pl_etat.h_fin = 21 * 60 + 30; // dernier point à 21h30 (HA l'envoie : « fin »)
        pl_etat.fumees = 243; pl_etat.corps = 162; pl_etat.tirage = 42;
        pl_etat.feu = PL_REGULATION; pl_etat.brassage = 2; pl_etat.mode = 0;
        pl_commande = [](const char *q) { printf("[poele] %s\n", q); };
        aller("Poêle");
        avancer(600);
        capture("28_poele");
        pl_etat.fumees = 176; pl_etat.corps = 151; pl_etat.tirage = 100; pl_etat.feu = PL_RECHARGER;
        page_poele_maj();
        toucher(332, 438); // mode Levier
        lacher();
        capture("29_poele_recharger");
        toucher(202, 24); // onglet Courbe
        lacher();
        capture("30_poele_courbe");
        toucher(300, 160, 200); // doigt sur la courbe : curseur + bulle
        capture("30b_poele_courbe_doigt");
        lacher();
        toucher(140, 70); // 1 h (sélecteur entre la valeur et le min / max)
        lacher();
        capture("31_poele_courbe_1h");
        toucher(304, 24); // onglet Réglages
        lacher();
        capture("32_poele_reglages");
    }

    // --- Panneau LED (02/11/2026) : séance de 15 min en cours, 7 min restantes ; rouge-jaune à fond, infrarouge coupé ;
    // radiateur à 43 °C (les ventilateurs suivent) ---
    {
        pnl_etat = {true, true, PNL_ROUGE_JAUNE, PNL_MINUTERIE, 15, 7, {80, 100, 100, 100, 0, 0, 0, 0}, 43, 80};
        pnl_commande = [](const char *q) { printf("[panneau_led] %s\n", q); };
        aller("Panneau LED");
        avancer(600);
        capture("163_panneau_led_seance");
        toucher(266, 24); // onglet Intensités
        lacher();
        avancer(300);
        capture("164_panneau_led_intensites");
        glisser(60, 96, 260, 96, 10); // curseur « toutes les LED » (1re rangée) : les 8 curseurs suivent
        lacher();
        capture("165_panneau_led_reglage");
        glisser(440, 380, 440, 40); // le carrousel défile jusqu'en bas : icône du panneau, page active (ambre)
        lacher();
        avancer(300);
        capture("166_panneau_led_rail");
        glisser(440, 40, 440, 380); // le carrousel remonte pour la suite du scénario
        lacher();
        avancer(300);
    }

    // --- Lumières : lampadaires allumés et réglés ensemble, lampe fenêtre orange, bandeau WLED hors ligne ---
    for (int i = 0; i < 3; i++) lu_lampes[i].on = true;
    lu_lampes[3].on = true; lu_lampes[3].mode_hs = true;
    lu_lampes[5].hors_ligne = true;
    lu_commande = [](int i) { printf("[lumieres] %s\n", lu_lampes[i].nom); };
    aller("Lumières");
    avancer(600);
    capture("33_lumieres");
    // 25/10 : plus de présélection ; appui long sur « Séjour » = toutes ses lampes en ligne, un 2e = plus rien
    toucher(118, 24, 700);
    lacher();
    avancer(200);
    capture("127_lumieres_appui_long_piece");
    toucher(118, 24, 700);
    lacher();
    avancer(200);
    capture("128_lumieres_appui_long_rien");
    toucher(118, 24, 700); // de nouveau toutes, pour la suite du scénario (disque, luminosité)
    lacher();
    avancer(200);
    toucher(172 + 110 + 60, 52 + 110 + 50, 120); // disque : bleu-violet assez saturé
    lacher();
    glisser(172 + 30, 305, 172 + 160, 305, 8); // luminosité vers 75 %
    lacher();
    capture("34_lumieres_couleur");
    toucher(80, 52 + 3 * 51 + 23, 700); // appui long sur la lampe fenêtre : ajoutée à la sélection
    lacher();
    capture("35_lumieres_appui_long");
    toucher(160, 24); // pièce Atelier
    lacher();
    capture("36_lumieres_atelier");
    // 13/11 (galerie du README) : « une lumière sélectionnée, le curseur posé sur une couleur de la roue ».
    // Tap court sur une lampe = régler cette lampe seule ; appui sur la roue = teinte/saturation (le rond
    // de 26 px, la pipette, se pose à la couleur réglée — src/ui_lvgl/page_lumieres.cpp:236-239).
    toucher(40, 24); // onglet « Toutes » : la liste complète, Lampadaire 1 en première ligne
    lacher();
    avancer(200);
    toucher(80, 52 + 23); // tap court : Lampadaire 1 seule sélectionnée
    lacher();
    avancer(200);
    toucher(329, 81, 120); // roue (centre 282,162, rayon 110) : teinte 30° (orange chaud) à ~85 % de saturation
    lacher();
    glisser(172 + 30, 305, 172 + 160, 305, 8); // luminosité vers 75 %
    lacher();
    capture("208_lumieres_galerie");

    // --- Musique : CommoNasm (TenDJiz), piste 4 ; bibliothèque, radios et lecteurs de Music Assistant ---
    // 13/11 : l'album en lecture et sa pochette sont libres de droit (voir plus bas et docs/CREDITS.md) ;
    // les durées ci-dessous restent des valeurs de démonstration.
    {
        mu_etat_t &m = mu_etat;
        const char *pistes[][2] = {{"Geto Heaven Can", "429"}, {"Shootouts 1999", "298"}, {"Nastra Dooinit", "268"}, {"Gladiator Made U Look", "224"},
                                   {"U Gotta Love The Corner", "256"}, {"Purple Sun God", "154"}, {"You Gowe Me", "241"}, {"Gold Moment", "218"},
                                   {"Reminding Me of Rule", "340"}, {"OneMic", "190"}};
        m.nb_file = 10;
        for (int i = 0; i < 10; i++)
        {
            snprintf(m.file[i].titre, 48, "%s", pistes[i][0]);
            snprintf(m.file[i].artiste, 32, "TenDJiz");
            m.file[i].duree = atoi(pistes[i][1]);
        }
        m.piste = 3;
        snprintf(m.titre, 64, "%s", m.file[3].titre);
        snprintf(m.artiste, 40, "TenDJiz");
        snprintf(m.album, 48, "CommoNasm");
        m.duree = 224; m.position = 85; m.lecture = true; m.volume = 42;
        const char *art[][2] = {{"Air", "6 albums"}, {"Bonobo", "5 albums"}, {"Daft Punk", "4 albums"}, {"Ibrahim Maalouf", "7 albums"},
                                {"Jain", "3 albums"}, {"Massive Attack", "5 albums"}, {"Nina Simone", "9 albums"}, {"Portishead", "3 albums"}};
        m.nb_artistes = 8;
        for (int i = 0; i < 8; i++) { snprintf(m.artistes[i].nom, 40, "%s", art[i][0]); snprintf(m.artistes[i].detail, 24, "%s", art[i][1]); }
        const char *alb[][2] = {{"Moon Safari", "1998"}, {"The Virgin Suicides", "2000"}, {"10 000 Hz Legend", "2001"},
                                {"Talkie Walkie", "2004"}, {"Pocket Symphony", "2007"}, {"Le voyage dans la lune", "2012"}};
        m.nb_albums = 6;
        for (int i = 0; i < 6; i++) { snprintf(m.albums[i].nom, 40, "%s", alb[i][0]); snprintf(m.albums[i].detail, 24, "%s", alb[i][1]); }
        const char *rad[][2] = {{"FIP", "fip"}, {"France Inter", "inter"}, {"France Culture", "culture"}, {"TSF Jazz", "TSF"}, {"Radio Nova", "nova"},
                                {"Radio Paradise", "RP"}, {"KEXP", "KEXP"}, {"Nostalgie", "N"}, {"Jazz Radio", "jazz"}};
        m.nb_radios = 9;
        for (int i = 0; i < 9; i++) { snprintf(m.radios[i].nom, 40, "%s", rad[i][0]); snprintf(m.radios[i].detail, 24, "%s", rad[i][1]); }
        const char *lec[][2] = {{"Salon", "piCore"}, {"Bureau", "PC bureau"}, {"Salon TV", "TV LG"}, {"Satellite", "reSpeaker"}};
        m.nb_lecteurs = 4;
        for (int i = 0; i < 4; i++) { snprintf(m.lecteurs[i].nom, 24, "%s", lec[i][0]); snprintf(m.lecteurs[i].appareil, 24, "%s", lec[i][1]); m.lecteurs[i].en_ligne = i < 3; }
        m.lecteur = 0; m.ampli = true;
        mu_commande = [](mu_action_t a, int arg)
        {
            printf("[musique] action %d (%d)\n", (int)a, arg);
            if (a == MU_SUIVANT && mu_etat.piste < mu_etat.nb_file - 1)
            {
                mu_etat.piste++;
                snprintf(mu_etat.titre, 64, "%s", mu_etat.file[mu_etat.piste].titre);
                mu_etat.duree = mu_etat.file[mu_etat.piste].duree; mu_etat.position = 0;
                page_musique_maj();
            }
        };
    }
    aller("Musique");
    avancer(600);
    // 13/11 (galerie du README) : pochette d'album LIBRE DE DROIT à la place de la pochette dessinée :
    // « CommoNasm » (TenDJiz, 2012), visuel CC0 par le producteur lui-même, Wikimedia Commons
    // (source, auteur et licence : docs/CREDITS.md). Servie comme Music Assistant le fait :
    // imageproxy/<proxy_id>?size=256 -> donnees/pochettes/<proxy_id>.jpg (le simulateur lit le fichier).
    ui_images_telecharger = servir_images_locale; // le serviceur sert aussi les pochettes (installé ici depuis 13/11)
    ui_images_demander(UI_IMG_LECTURE,
                       "http://192.168.1.11:8095/imageproxy/6db41126bd6024f1c6bc9b5d83024bd91e0f7717a08ff3398d7adaa2ff26de84?size=256",
                       228, // POCHETTE_LECTURE (src/ui_lvgl/ui_musique.cpp) : taille d'affichage de la pochette en cours
                       [](int, const lv_img_dsc_t *img)
                       {
                           if (!img)
                               return;
                           mu_etat.pochette = img;
                           page_musique_maj_lecture();
                       });
    avancer(1200); // l'image arrive par le minuteur de 100 ms de ui_images, le fondu de pose se termine
    capture("37_musique");
    glisser(202 + 120, 200 - 60, 202 + 130, 200 + 20, 8); // doigt sur l'anneau de volume (côté droit)
    capture("38_musique_volume");
    lacher();
    glisser(260, 190, 140, 190, 6); // glisser vers la gauche sur la pochette : morceau suivant
    lacher();
    avancer(200);
    capture("39_musique_suivant");
    toucher(341, 437); // ☰ playlist
    lacher();
    capture("40_musique_playlist");
    toucher(130, 24); // Biblio
    lacher();
    capture("41_musique_biblio");
    toucher(150, 80); // Air
    lacher();
    capture("42_musique_albums");
    toucher(220, 24); // Radios
    lacher();
    capture("43_musique_radios");
    toucher(308, 24); // Sortie
    lacher();
    capture("44_musique_sortie");

    // --- Imprimante : support_camera_v3 à 62 % (couche 84 / 136), PETG ---
    {
        pr_etat_imp_t &p = pr_etat;
        const char *f[][4] = {{"support_camera_v3", "PETG · 38 g", "hier", "161"}, {"couvercle_croquettes", "PETG · 112 g", "18/10", "245"},
                              {"clip_cable_x10", "PLA · 9 g", "15/10", "52"}, {"engrenage_store", "ABS · 21 g", "12/10", "97"},
                              {"pied_ecran_guition", "PETG · 64 g", "09/10", "192"}};
        p.nb_fichiers = 5;
        for (int i = 0; i < 5; i++)
        {
            snprintf(p.fichiers[i].nom, 48, "%s", f[i][0]);
            snprintf(p.fichiers[i].matiere, 24, "%s", f[i][1]);
            snprintf(p.fichiers[i].date, 12, "%s", f[i][2]);
            p.fichiers[i].minutes = atoi(f[i][3]);
            p.fichiers[i].vignette = &icon_vignette_test;
            p.fichiers[i].mini = &icon_vignette_test_52;
        }
        p.etat = PR_IMPRESSION; p.progression = 62; p.couche = 84; p.couches = 136; p.reste_min = 62; p.fin_minute = 22 * 60 + 40;
        p.buse = 234; p.buse_c = 235; p.plateau = 70; p.plateau_c = 70; p.caisson = 27; p.caisson_c = 30;
        p.lumiere = true; p.chauffage = true; p.carte_mere = true; p.travail = 0; p.choix = 1;
        p.puissance = 312; // 27/10 : wattmètre de la prise (onglet Marche)
        p.pm1 = 0.6f;      // 27/10 : AQM (pastille du Suivi)
        pr_commande = [](pr_action_t a, int arg)
        {
            printf("[imprimante] action %d (%d)\n", (int)a, arg);
            if (a == PR_PAUSE_REPRISE) { pr_etat.etat = pr_etat.etat == PR_PAUSE ? PR_IMPRESSION : PR_PAUSE; page_imprimante_maj(); }
        };
    }
    aller("Imprimante 3D");
    avancer(600);
    capture("45_imprimante");
    // 13/11 (vignette de la galerie) : la vue caisson montre de vraies images de la caméra du caisson
    // (donnees/cameras/park_caisson/, publiées avec l'accord de l'auteur) ; retirée juste après la capture.
    snprintf(g_seq_cam, sizeof g_seq_cam, "park_caisson");
    g_seq_i = 0;
    toucher(359, 71); // 26/10 : bouton « Caméra » : les images de motionEye (ui_camera)
    lacher();
    avancer(300);
    capture("45b_imprimante_camera");
    g_seq_cam[0] = 0; // la suite (plateau puis anneau) garde les images de test habituelles
    // 08/11/2026 : le bouton a TROIS vues depuis la page Imprimante en 9.6 (STL -> caisson -> plateau).
    // Le scénario s'arrétait au caisson ; on pousse le cycle complet, capture du plateau comprise.
    toucher(359, 71); // 2e appui : le PLATEAU (bouton « Plateau » -> vue plateau)
    lacher();
    avancer(900); // le flux motionEye met ~1 s à afficher sa première image
    capture("45c_imprimante_plateau");
    toucher(359, 71); // 3e appui : retour à la miniature du fichier (l'anneau)
    lacher();
    avancer(200);
    // 27/10 : 4 onglets (centres 69, 157, 245, 333) ; les commandes de la machine dans « Marche »
    toucher(333, 24); // Marche
    lacher();
    capture("46_imprimante_marche");
    toucher(200, 202); // Pause
    lacher();
    capture("46b_imprimante_marche_pause");
    toucher(200, 278); // Arrêter : confirmation sur place
    lacher();
    capture("46c_imprimante_marche_confirmation");
    toucher(200, 202); // Annuler
    lacher();
    toucher(200, 202); // Reprendre
    lacher();
    toucher(157, 24); // Réglages
    lacher();
    capture("47_imprimante_reglages");
    toucher(245, 24); // Fichiers
    lacher();
    capture("48_imprimante_fichiers");
    toucher(200, 150); // 2e fichier : aperçu (une impression en cours : « Lancer » indisponible)
    lacher();
    capture("48b_imprimante_apercu");
    pr_etat.etat = PR_PRETE; // prête : « Lancer l'impression »
    pr_etat.puissance = 41;
    page_imprimante_maj();
    avancer(100);
    capture("48c_imprimante_apercu_prete");
    toucher(333, 24); // Marche, imprimante prête
    lacher();
    capture("48d_imprimante_marche_prete");
    pr_etat.etat = PR_IMPRESSION;
    page_imprimante_maj();

    // --- PC : chiffres inspirés du vrai PC (16 fils, 61 Go, 3 cartes NVIDIA) ---
    {
        pc_etat_t &p = pc_etat;
        const int c[] = {34, 12, 78, 22, 55, 9, 41, 18, 64, 7, 28, 92, 15, 33, 47, 11};
        int s = 0;
        for (int i = 0; i < PC_COEURS; i++) { p.coeurs[i] = c[i]; s += c[i]; }
        p.cpu = s / PC_COEURS; p.freq_mhz = 4800; p.cpu_temp = 61; p.cpu_watts = 48;
        p.ram = 17.4f; p.cache = 41.6f; p.ram_total = 61; p.swap = 0;
        const char *g[] = {"3090", "3060", "3090'"};
        const int ch[] = {87, 12, 81}, te[] = {68, 45, 64}, pw[] = {288, 34, 279};
        const float vr[] = {22.5f, 11.4f, 22.7f}, vt[] = {24, 12, 24};
        // 08/11 (maquette GPU) : état d'alimentation et fréquences relevés sur le PC le 08/11 vers 19 h
        // (champs « ps », « f », « fm », « f_max », « fm_max » de dashboard_guition/pc/etat)
        const char *ps[] = {"P2", "P2", "P2"};
        const int fg[] = {1500, 1500, 1500}, fmem[] = {9626, 8401, 9626}, fgmax[] = {2115, 2310, 2100}, fmemmax[] = {9876, 8601, 9876};
        for (int i = 0; i < PC_GPUS; i++)
        {
            snprintf(p.gpus[i].nom, 12, "%s", g[i]);
            p.gpus[i].charge = ch[i]; p.gpus[i].temp = te[i]; p.gpus[i].puissance = pw[i]; p.gpus[i].vram = vr[i]; p.gpus[i].vram_total = vt[i];
            snprintf(p.gpus[i].ps, sizeof p.gpus[i].ps, "%s", ps[i]);
            p.gpus[i].f = fg[i]; p.gpus[i].fm = fmem[i]; p.gpus[i].f_max = fgmax[i]; p.gpus[i].fm_max = fmemmax[i];
        }
        p.net_bas = 1.2f; p.net_haut = 0.08f; p.disque_l = 30; p.disque_e = 5;
        for (int i = 0; i < PC_HIST; i++)
        {
            p.h_net[i] = (uint8_t)LV_CLAMP(4, 30 + 25 * sinf(i / 3.0f + 7) + 20 * sinf(i * 2.1f), 100);
            p.h_disque[i] = (uint8_t)LV_CLAMP(4, 20 + 25 * sinf(i / 3.0f + 3) + 15 * sinf(i * 1.7f), 100);
        }
        // 31/10 : VRAM par carte (relevé du 31/10, couleurs du hub), dont une carte à 6 blocs (ligne « autres ») ; 5 min de CPU
        const pc_bloc_t b0[] = {{"ReadyArt_gemma-4-31B-it-scotoma-2-Q4_K_M", 13612, 0x22D3EE}, {"Whisper (STT)", 4464, 0x35C46A},
                                {"gemma-4-E4B-it-qat-UD-Q4_K_XL", 3502, 0x4C8DFF}, {"PaddleOCR-VL-1.6-Q8_0", 1392, 0xE879F9}};
        const pc_bloc_t b1[] = {{"gemma-4-12B-it-qat-UD-Q4_K_XL", 6978, 0xF87171}, {"Qwen3-TTS (voix)", 4664, 0xF0A83C}};
        const pc_bloc_t b2[] = {{"ReadyArt_gemma-4-31B-it-scotoma-2-Q4_K_M", 15510, 0x22D3EE}, {"Qwen3-Embedding-4B-Q4_K_M", 3552, 0xF59E0B},
                                {"gemma-4-E4B-it-qat-UD-Q4_K_XL", 3502, 0xFF6B9D}, {"bge-reranker-v2-m3-Q8_0", 662, 0x10B981},
                                {"Zonos (TTS)", 400, 0xA06BFF}, {"python3", 120, 0xF0A83C}};
        memcpy(p.gpus[0].blocs, b0, sizeof b0); p.gpus[0].nb_blocs = 4;
        memcpy(p.gpus[1].blocs, b1, sizeof b1); p.gpus[1].nb_blocs = 2;
        memcpy(p.gpus[2].blocs, b2, sizeof b2); p.gpus[2].nb_blocs = 6;
        for (int i = 0; i < PC_HIST_CPU; i++)
            p.h_cpu[i] = (uint8_t)(i >= 40 && i < 52 ? 55 + (i * 13) % 30 : i >= 95 && i < 100 ? 70 + (i * 11) % 25 : 8 + (i * 7) % 9);
        snprintf(p.allume_depuis, 16, TXT("2 j 04 h", "2 d 04 h"));
        snprintf(p.derniere_nouvelle, 8, "21:47");
        const char *pr[][2] = {{"llama-server", "Assistant · LLM 31B"}, {"python3 ollama-capture.py", "proxy assistant"}, {"trtllm-serve", "reconnaissance vocale"},
                               {"zonos", "synthèse vocale"}, {"firefox", "navigateur"}, {"postgres", "base de l'assistant"}, {"code", "VS Code"},
                               {"btop_agent.py", "cet écran"}, {"kwin_wayland", "bureau"}, {"pipewire", "son"}};
        const float pc[] = {104, 23, 18, 9, 7, 4, 3, 1, 1, 0.6f}, pm[] = {6.2f, 1.4f, 3.1f, 2.6f, 2.2f, 0.9f, 1.7f, 0.1f, 0.4f, 0.1f},
                    pv[] = {44.9f, 0, 5.2f, 4.8f, 0.3f, 0, 0.2f, 0, 0.2f, 0};
        p.nb_procs = 10;
        for (int i = 0; i < 10; i++)
        {
            snprintf(p.procs[i].nom, 32, "%s", pr[i][0]); snprintf(p.procs[i].role, 28, "%s", pr[i][1]);
            p.procs[i].cpu = pc[i]; p.procs[i].mem = pm[i]; p.procs[i].vram = pv[i];
        }
        pc_commande = [](pc_action_t a)
        {
            printf("[pc] action %d\n", (int)a);
            pc_etat.en_veille = a == PC_VEILLE;
            page_pc_maj();
        };
    }
    aller("PC");
    avancer(600);
    capture("49_pc");
    toucher(200, 24); // GPU (11/11 : 3 onglets — l'IA est devenue page du rail — centres ~100 / 200 / 301)
    lacher();
    capture("49b_pc_gpu");
    toucher(301, 24); // Processus
    lacher();
    capture("50_pc_processus");
    toucher(340, 70); // tri par VRAM
    lacher();
    capture("51_pc_vram");
    // 08/11 (maquette GPU, pire cas demandé) : les trois cartes à 4 modèles aux noms longs, pour vérifier que la
    // colonne des fréquences ne touche jamais les noms tronqués ni les Go. Changer le nombre de blocs veut
    // reconstruire la page : on passe par l'onglet Processus et on revient sur GPU.
    {
        const char *longs[] = {"lamina-vision-encodeur-des-images-en-retard-30", "lamina-audio-separateur-de-pistes",
                               "ReadyArt_gemma-4-31B-it-scotoma-2-Q4_K_M", "bge-reranker-v2-m3-Q8_0"};
        for (int i = 0; i < PC_GPUS; i++)
        {
            for (int b = 0; b < 4; b++)
            {
                snprintf(pc_etat.gpus[i].blocs[b].nom, sizeof pc_etat.gpus[i].blocs[b].nom, "%s", longs[b]);
                pc_etat.gpus[i].blocs[b].mo = 6000 - 900 * b;
            }
            pc_etat.gpus[i].nb_blocs = 4;
        }
    }
    toucher(301, 24); // Processus (11/11 : 3 onglets)
    lacher();
    avancer(200);
    toucher(200, 24); // GPU, avec les quatre lignes longues partout
    lacher();
    avancer(300);
    capture("51b_pc_gpu_pire_cas");
    toucher(100, 24); // Système
    lacher();
    toucher(330, 449); // Mettre en veille : 1er appui, confirmation
    lacher();
    capture("52a_pc_veille_confirmer");
    toucher(330, 449); // 2e appui : veille
    lacher();
    capture("52_pc_veille");
    // --- MQTT (contrat dashboard_guition/) : relevé RÉEL du broker du 22/10, puis commandes ---
    ui_mqtt_publier = [](const char *sujet, const char *message, bool)
    {
        printf("[mqtt] -> %s %s\n", sujet, message);
        return true;
    };
    ui_mqtt_brancher();
    // 27/10 : le dernier message de chaque sujet, rejoué à l'identique par l'audit des redessins (fin du scénario)
    static std::map<std::string, std::string> derniers;
    auto recevoir = [](const char *sujet, const char *json)
    {
        derniers[sujet] = json;
        ui_mqtt_recevoir(sujet, (const uint8_t *)json, strlen(json));
    };
    auto rejouer = [&](const char *chemin)
    {
        FILE *f = fopen(chemin, "r");
        char ligne[4096];
        while (f && fgets(ligne, sizeof ligne, f))
        {
            char *tab = strchr(ligne, '\t');
            if (!tab)
                continue;
            *tab = 0;
            ligne[strcspn(tab + 1, "\n") + (tab + 1 - ligne)] = 0;
            recevoir(ligne, tab + 1);
        }
        if (f)
            fclose(f);
    };
    rejouer("donnees/releve_broker_22-10.tsv");
    recevoir("dashboard_guition/thermostats/cuisine/etat", "{\"temp\":"); // JSON tronqué : ignoré
    recevoir("dashboard_guition/thermostats/garage/etat", "{\"temp\":30}"); // pièce inconnue : ignorée
    th_piece_affichee = 0;
    aller("Chauffage");
    avancer(600);
    capture("53_mqtt_cuisine");
    th_piece_affichee = 4;
    page_thermostat_maj();
    avancer(300);
    capture("54_mqtt_sdb");
    th_piece_affichee = 0;
    page_thermostat_maj();
    toucher(348, 442); // Confort
    lacher();
    // HA rallume le chauffage PUIS règle le préréglage : deux états publiés, le premier encore en « manuel 15 »
    recevoir("dashboard_guition/thermostats/cuisine/etat",
             "{\"temp\":14.5,\"hum\":69.6,\"consigne\":15.0,\"mode\":\"manuel\",\"chauffe\":0,\"batterie\":60,\"signal\":160}");
    avancer(300);
    capture("55_mqtt_confort_garde");
    recevoir("dashboard_guition/thermostats/cuisine/etat",
             "{\"temp\":14.5,\"hum\":69.6,\"consigne\":20.0,\"mode\":\"confort\",\"chauffe\":1,\"batterie\":60,\"signal\":160}");
    avancer(3500);
    capture("56_mqtt_confort_final");
    printf("[mqtt] cuisine : mode %d consigne %.1f chauffe %d hum %.1f batterie %d\n", (int)th_pieces[0].mode,
           th_pieces[0].consigne, (int)th_pieces[0].chauffe, th_pieces[0].hum, th_pieces[0].batterie);
    th_commande(0, TH_MANUEL, 19.5f); // ce qu'envoie l'anneau au relâchement sur une consigne libre
    toucher(56, 442); // Arrêt
    lacher();
    // Eau chaude : état réel déjà reçu avec le relevé (44,9 °C, consigne 50, AUTO, régime ECO, relais coupé) ; 25/10 :
    // état étendu et historique au format exact de HA (1792200000004 / 1792200000033)
    recevoir("dashboard_guition/ballon/etat", "{\"chaude_jour\": 11, \"chauffe\": 0, \"combles\": 15, \"consigne\": 50, \"debit\": 0, "
                                              "\"eco\": 1, \"froide_jour\": 54, \"kwh_jour\": 4.4, \"mode\": \"auto\", \"puissance\": 0, "
                                              "\"temp\": 44.9}");
    {
        static bc_historique_t h;
        journee_ballon(h);
        std::string m = "{\"fin\":540,\"t\":[";
        for (int i = 48; i < BC_POINTS; i++) // HA n'a que 20 h : la courbe doit être calée à droite
            m += std::to_string(h.temp[i]) + (i < BC_POINTS - 1 ? "," : "");
        m += "],\"c\":[[0,55],[150,50]],\"l\":[";
        bool premier = true;
        for (int i = 48; i < BC_POINTS; i++)
            if (h.litres[i])
            {
                m += std::string(premier ? "" : ",") + "[" + std::to_string(i - 48) + "," + std::to_string(h.litres[i]) + "]";
                premier = false;
            }
        char hx[80];
        m += "],\"ch\":\"";
        for (int k = 0; k < BC_POINTS / 8; k++)
            snprintf(hx + 2 * k, 3, "%02x", h.chauffe[k]);
        m += hx;
        m += "\",\"hc\":\"";
        for (int k = 0; k < BC_POINTS / 8; k++)
            snprintf(hx + 2 * k, 3, "%02x", h.creuses[k]);
        m += hx;
        m += "\"}";
        recevoir("dashboard_guition/ballon/historique", m.c_str());
    }
    aller("Eau chaude");
    avancer(600);
    capture("57_mqtt_eau_chaude");
    toucher(200, 23); // Courbe : historique reçu, calé à droite
    lacher();
    avancer(300);
    capture("57b_mqtt_eau_chaude_courbe");
    toucher(301, 23); // Réglages
    lacher();
    toucher(332, 360); // Forcé : un seul message (mode)
    lacher();
    // écho de HA pendant la garde : relais collé PUIS mode encore « auto » (ordre des déclencheurs) -> l'écran reste sur Forcé
    recevoir("dashboard_guition/ballon/etat", "{\"temp\":44.9,\"consigne\":50,\"mode\":\"auto\",\"eco\":1,\"chauffe\":1}");
    avancer(300);
    capture("58_mqtt_eau_chaude_force");
    recevoir("dashboard_guition/ballon/etat", "{\"temp\":44.9,\"consigne\":50,\"mode\":\"force\",\"eco\":1,\"chauffe\":1}");
    avancer(3500);
    toucher(200, 426); // carte du régime : ECO -> normal
    lacher();
    glisser(100, 66 + 250 - (int)((50 - 30) / 40.0f * 250), 100, 66 + 250 - (int)((55 - 30) / 40.0f * 250)); // consigne 55
    lacher();
    avancer(3500);
    capture("59_mqtt_eau_chaude_reglee");
    printf("[mqtt] ballon : mode %d eco %d consigne %.0f chauffe %d\n", (int)bc_etat.mode, (int)bc_etat.eco, bc_etat.consigne,
           (int)bc_etat.chauffe);
    // VMC : état réel déjà reçu avec le relevé (maison petite vitesse, SdB 80 % ; sous-sol à l'arrêt, 83 %)
    aller("VMC");
    avancer(600);
    capture("60_mqtt_vmc");
    toucher(330, 414); // sous-sol : Grande
    lacher();
    // écho pendant la garde (le ventilateur est encore annoncé à l'arrêt) : l'écran reste sur Grande
    recevoir("dashboard_guition/vmc/sous_sol/etat", "{\"auto\": 0, \"hum\": 83, \"hum_lieu\": \"" TXT("sous-sol", "basement") "\", \"seuil\": 0, \"vitesse\": 0}");
    avancer(300);
    capture("61_mqtt_vmc_garde");
    recevoir("dashboard_guition/vmc/sous_sol/etat", "{\"auto\": 0, \"hum\": 83, \"hum_lieu\": \"" TXT("sous-sol", "basement") "\", \"seuil\": 0, \"vitesse\": 2}");
    // maison : la pièce la plus humide devient le séjour (accent échappé é, comme peut l'envoyer tojson), boost en cours
    recevoir("dashboard_guition/vmc/maison/etat",
             "{\"auto\": 1, \"hum\": 84, \"hum_lieu\": \"s\\u00e9jour\", \"seuil\": 80, \"vitesse\": 2}");
    avancer(3500);
    capture("62_mqtt_vmc_final");
    printf("[mqtt] vmc : maison v%d auto %d %d%% %s / sous-sol v%d %d%% %s\n", vmc_zones[0].vitesse, (int)vmc_zones[0].auto_en_cours,
           vmc_zones[0].hum, vmc_zones[0].lieu, vmc_zones[1].vitesse, vmc_zones[1].hum, vmc_zones[1].lieu);
    // état réel déjà reçu avec le relevé du broker
    aller("Qualité de l'air");
    avancer(600);
    capture("63_mqtt_air_demarrage"); // aucune courbe : HA n'a pas encore envoyé l'historique
    // historique tenu par HA (48 points d'une demi-heure, 255 = pas de mesure) : poêle allumé au séjour en soirée,
    // ESPurifier coupé du réseau 1 h 30 en milieu de journée
    {
        std::string at = "{\"pas\":30,\"points\":[", se = at;
        for (int k = 0; k < 48; k++)
        {
            int a = (k >= 20 && k < 23) ? 255 : 3 + (k * 7) % 5;
            int s = k < 34 ? 2 + k % 2 : (k < 38 ? 18 + (k - 34) * 9 : 45 - (k - 37) * 3);
            at += std::to_string(a) + (k < 47 ? "," : "]}");
            se += std::to_string(LV_MAX(s, 4)) + (k < 47 ? "," : "]}");
        }
        recevoir("dashboard_guition/air/atelier/historique", at.c_str());
        recevoir("dashboard_guition/air/sejour/historique", se.c_str());
        recevoir("dashboard_guition/air/sejour/etat", "{\"en_ligne\": 1, \"info\": \"" TXT("Filtre 59 %", "Filter 59 %") "\", \"mode\": 1, \"pm25\": 12}");
    }
    avancer(300);
    capture("64_mqtt_air_historique");
    toucher(274, 216); // atelier : niveau 2 (index 3)
    lacher();
    toucher(247, 428); // séjour : Silence (index 2)
    lacher();
    // le Xiaomi passe d'abord par son ancien préréglage (Auto) : gardé de côté, l'écran reste sur Silence
    recevoir("dashboard_guition/air/sejour/etat", "{\"en_ligne\": 1, \"info\": \"" TXT("Filtre 59 %", "Filter 59 %") "\", \"mode\": 1, \"pm25\": 15}");
    avancer(300);
    capture("65_mqtt_air_commandes");
    // état réel déjà reçu avec le relevé du broker
    aller("Ouvrants");
    avancer(600);
    capture("66_mqtt_ouvrants");
    glisser(300, 220, 300, 92 + 105, 10); // volet : remonté jusqu'à 30 % (il était fermé)
    lacher();
    toucher(200, 332 + 136 - 10 - 20); // Séance : allume la Fire TV
    lacher();
    // HA : la Fire TV s'allume, l'automatisation 163 fait descendre la toile ; positions publiées pendant le mouvement
    recevoir("dashboard_guition/ouvrants/toile/etat", "{\"en_ligne\": 1, \"firetv\": 0, \"mouvement\": \"arret\", \"position\": 100}");
    avancer(3500);
    recevoir("dashboard_guition/ouvrants/toile/etat", "{\"en_ligne\": 1, \"firetv\": 1, \"mouvement\": \"ferme\", \"position\": 60}");
    recevoir("dashboard_guition/ouvrants/volet_cuisine/etat", "{\"en_ligne\": 1, \"mouvement\": \"ouvre\", \"position\": 20}");
    avancer(300);
    capture("67_mqtt_ouvrants_mouvement");
    recevoir("dashboard_guition/ouvrants/toile/etat", "{\"en_ligne\": 1, \"firetv\": 1, \"mouvement\": \"arret\", \"position\": 0}");
    recevoir("dashboard_guition/ouvrants/volet_cuisine/etat", "{\"en_ligne\": 1, \"mouvement\": \"arret\", \"position\": 30}");
    avancer(300);
    capture("68_mqtt_ouvrants_seance");
    toucher(160, 290); // store banne : flèche du bas = sortir
    lacher();
    recevoir("dashboard_guition/ouvrants/store_terrasse/etat", "{\"en_ligne\": 1, \"mouvement\": \"arret\", \"position\": 0}");
    avancer(3500);
    recevoir("dashboard_guition/ouvrants/store_terrasse/etat", "{\"en_ligne\": 1, \"mouvement\": \"ouvre\", \"position\": 60}");
    avancer(300);
    capture("69_mqtt_store_banne_sort");
    // Croquettes : automatisation proposée (ha_propositions) -> message calculé par HA (API de rendu, 22/10 au soir : réservoir
    // annoncé vide, statistiques du distributeur douteuses : 0 g pour 6 repas aujourd'hui, 595 g hier)
    const char *cq_reel = "{\"autonomie\": 0, \"bol\": 63.0, \"jour_semaine\": 3, \"jours\": [{\"g\": 0, \"n\": 6}, {\"g\": 595, \"n\": 7}, "
                          "{\"g\": 16, \"n\": 4}, {\"g\": 44, \"n\": 10}, {\"g\": 0, \"n\": 9}, {\"g\": 50, \"n\": 8}, {\"g\": 1, \"n\": 10}], "
                          "\"repas\": [{\"a\": 1, \"h\": 390, \"q\": 15}, {\"a\": 1, \"h\": 750, \"q\": 15}, {\"a\": 1, \"h\": 1170, \"q\": 15}, "
                          "{\"a\": 0, \"h\": 1205, \"q\": 10}], \"stock\": 0, \"vide\": 1}";
    recevoir("dashboard_guition/croquettes/etat", cq_reel);
    aller("VMC");
    aller("Croquettes");
    avancer(600);
    capture("70_mqtt_croquettes");
    toucher(160, 132 + 68 + 31); // heure du 2e repas
    lacher();
    toucher(347, 132 + 68 + 31); // + 15 min : 12h30 -> 12h45
    lacher();
    recevoir("dashboard_guition/croquettes/etat", cq_reel); // état intermédiaire de HA (minute pas encore changée)
    avancer(300);
    capture("71_mqtt_croquettes_garde");
    std::string fin = cq_reel;
    fin.replace(fin.find("\"h\": 750"), 8, "\"h\": 765");
    recevoir("dashboard_guition/croquettes/etat", fin.c_str());
    avancer(3500);
    printf("[mqtt] croquettes : repas 2 à %02d:%02d, %d g, actif %d\n", cq_etat.repas[1].minute / 60, cq_etat.repas[1].minute % 60,
           cq_etat.repas[1].g, (int)cq_etat.repas[1].actif);
    toucher(330, 438); // Semaine
    lacher();
    capture("72_mqtt_croquettes_semaine");
    // Café : automatisations proposées (ha_propositions) ; format du rendu HA du 23/10 (cafetière et bouilloire hors ligne),
    // avec un café programmé à 6h45 et deux heures récentes pour l'essai
    cf_maintenant = 15 * 60 + 30;
    recevoir("dashboard_guition/cafe/etat", "{\"bouilloire\": {\"chauffe\": 0, \"en_ligne\": 1, \"h\": 420, \"prog\": 0, \"temp\": 70}, "
                                           "\"cafe\": {\"allumee\": 0, \"en_ligne\": 1, \"h\": 405, \"marche\": 0, \"prog\": 1}, \"recentes\": [405, 435]}");
    aller("VMC");
    aller("Café");
    toucher(140, 24); // onglet Café
    lacher();
    avancer(600);
    capture("73_mqtt_cafe");
    toucher(200, 360); // Programmer (raccourci « dans 15 min » choisi par défaut ?)
    lacher();
    // HA : l'heure change avant le booléen et les heures récentes -> état intermédiaire gardé de côté
    recevoir("dashboard_guition/cafe/etat", "{\"bouilloire\": {\"chauffe\": 0, \"en_ligne\": 1, \"h\": 420, \"prog\": 0, \"temp\": 70}, "
                                           "\"cafe\": {\"allumee\": 0, \"en_ligne\": 1, \"h\": 405, \"marche\": 0, \"prog\": 1}, \"recentes\": [405, 435]}");
    avancer(300);
    capture("74_mqtt_cafe_garde");
    avancer(3500);
    toucher(268, 24); // onglet Bouilloire
    lacher();
    toucher(200, 434); // chauffer maintenant (70 °C reçu de HA)
    lacher();
    recevoir("dashboard_guition/cafe/etat", "{\"bouilloire\": {\"chauffe\": 1, \"en_ligne\": 1, \"h\": 420, \"prog\": 0, \"temp\": 70}, "
                                           "\"cafe\": {\"allumee\": 0, \"en_ligne\": 1, \"h\": 405, \"marche\": 0, \"prog\": 1}, \"recentes\": [405, 435]}");
    avancer(300);
    capture("75_mqtt_bouilloire");
    // Poêle : automatisations proposées (ha_propositions) -> état rendu par HA le 23/10 (poêle éteint, 19 °C) ; historique
    // fabriqué au format du contrat : une soirée de feu, poêle hors ligne 40 min au milieu (-1 = pas de mesure)
    recevoir("dashboard_guition/poele/etat", "{\"brassage\": 0, \"consigne\": 240, \"corps\": 24.1, \"en_ligne\": 1, \"feu\": \"eteint\", "
                                            "\"fumees\": 19, \"kd\": 10.0, \"ki\": 0.01, \"kp\": 3.0, \"limite\": 30, \"mode\": \"auto\", "
                                            "\"recharge\": 180, \"tirage\": 100, \"tirage_manuel\": 100, \"tirage_min\": 30}");
    {
        std::string fu = "{\"pas\":5,\"fumees\":[", co = "\"corps\":[", ti = "\"tirage\":[";
        for (int k = 0; k < 144; k++)
        {
            bool trou = k >= 70 && k < 78;
            int f = k < 30 ? 19 : (k < 50 ? 19 + (k - 30) * 12 : 260 - (k - 50) * 2);
            int c = k < 30 ? 22 : LV_MIN(22 + (k - 30) * 4, 150) - (k > 90 ? (k - 90) : 0);
            int ti_ = k < 30 ? 100 : (k < 50 ? 100 : 40 + (k % 7) * 3);
            const char *sep = k < 143 ? "," : "]";
            fu += (trou ? std::string("-1") : std::to_string(LV_MAX(f, 19))) + sep;
            co += (trou ? std::string("-1") : std::to_string(c)) + sep;
            ti += (trou ? std::string("-1") : std::to_string(ti_)) + sep;
        }
        std::string h = fu + "," + co + "," + ti + "}";
        printf("[mqtt] historique poêle simulé : %zu octets\n", h.size());
        recevoir("dashboard_guition/poele/historique", h.c_str());
    }
    aller("VMC");
    aller("Poêle");
    toucher(100, 24); // onglet Feu
    lacher();
    avancer(600);
    capture("76_mqtt_poele");
    toucher(200, 438); // mode Manuel
    lacher();
    avancer(300);
    toucher(202, 24); // onglet Courbe
    lacher();
    toucher(332, 438); // 12 h
    lacher();
    avancer(300);
    capture("77_mqtt_poele_courbe");
    // Panneau LED (02/11/2026) : l'intégration ESPHome « panneau_led_rouge » est dans HA, l'automatisation d'état
    // pas encore écrite -> état FABRIQUÉ au format du contrat : séance en cours, rouge-jaune, 9 min restantes
    recevoir("dashboard_guition/panneau_led/etat",
             "{\"allumees\": 1, \"duree\": 15, \"i590\": 100, \"i630\": 100, \"i660\": 100, \"i810\": 0, \"i830\": 0, \"i850\": 0, "
             "\"i940\": 0, \"longueurs\": \"rouge_jaune\", \"mode\": \"minuterie\", \"radiateur\": 44, \"restant\": 9, \"seance\": 1, "
             "\"toutes\": 80, \"ventilos\": 85}");
    aller("VMC");
    aller("Panneau LED");
    avancer(600);
    capture("167_mqtt_panneau_led");
    // arrêt demandé à l'écran : le retenu encore en route (LED allumées) arrive pendant la garde, l'écran garde l'arrêt
    // demandé, puis le vrai état publié par HA s'applique
    toucher(200, 438); // « Arrêter la séance »
    lacher();
    recevoir("dashboard_guition/panneau_led/etat",
             "{\"allumees\": 1, \"duree\": 15, \"i590\": 100, \"i630\": 100, \"i660\": 100, \"i810\": 0, \"i830\": 0, \"i850\": 0, "
             "\"i940\": 0, \"longueurs\": \"rouge_jaune\", \"mode\": \"minuterie\", \"radiateur\": 44, \"restant\": 9, \"seance\": 1, "
             "\"toutes\": 80, \"ventilos\": 85}");
    avancer(300);
    recevoir("dashboard_guition/panneau_led/etat",
             "{\"allumees\": 0, \"duree\": 15, \"i590\": 100, \"i630\": 100, \"i660\": 100, \"i810\": 0, \"i830\": 0, \"i850\": 0, "
             "\"i940\": 0, \"longueurs\": \"rouge_jaune\", \"mode\": \"minuterie\", \"radiateur\": 43, \"restant\": -1, \"seance\": 0, "
             "\"toutes\": 80, \"ventilos\": 40}");
    avancer(3500);
    capture("168_mqtt_panneau_led_arret");
    printf("[mqtt] panneau led : seance %d allumees %d longueurs %d mode %d duree %d restant %d radiateur %d ventilos %d\n",
           (int)pnl_etat.seance, (int)pnl_etat.allumees, pnl_etat.longueurs, pnl_etat.mode, pnl_etat.duree, pnl_etat.restant,
           pnl_etat.radiateur, pnl_etat.ventilos);
    // Lumières : automatisations proposées (ha_propositions) -> états rendus par HA le 23/10 (tout éteint ; lampadaires, bandeaux
    // WLED / Lidl et panneau de l'atelier hors ligne)
    {
        const char *ids[] = {"lampadaire_1", "lampadaire_2", "lampadaire_3", "lampe_fenetre", "globe_lunaire", "bandeau_wled",
                             "bandeau_lidl", "panneau_led", "bandeau_bureau", "lampe_bureau", "chevet"};
        const bool en_ligne[] = {false, false, false, true, true, false, false, false, true, true, true};
        for (int i = 0; i < 11; i++)
        {
            char sujet[80], msg[48];
            snprintf(sujet, sizeof sujet, "dashboard_guition/lumieres/%s/etat", ids[i]);
            snprintf(msg, sizeof msg, "{\"en_ligne\": %d, \"on\": 0}", (int)en_ligne[i]);
            recevoir(sujet, msg);
        }
    }
    aller("VMC");
    aller("Lumières");
    toucher(40, 24); // pièce : Toutes
    lacher();
    avancer(600);
    capture("78_mqtt_lumieres");
    toucher(30, 52 + 3 * 51 + 23); // rond de la lampe fenêtre : allumer
    lacher();
    recevoir("dashboard_guition/lumieres/lampe_fenetre/etat", "{\"en_ligne\": 1, \"on\": 0}"); // écho tardif : gardé de côté
    avancer(3500);
    recevoir("dashboard_guition/lumieres/lampe_fenetre/etat",
             "{\"en_ligne\": 1, \"on\": 1, \"lum\": 40, \"mode\": \"blanc\", \"teinte\": 28, \"sat\": 60, \"kelvin\": 3000}");
    avancer(300);
    capture("79_mqtt_lumieres_fenetre");
    // renommée dans HA (sans reflasher) : nom et pièce viennent du message
    recevoir("dashboard_guition/lumieres/globe_lunaire/etat",
             "{\"couleur\": 0, \"en_ligne\": 1, \"nom\": \"Lune du salon\", \"on\": 0, \"piece\": \"S\\u00e9jour\"}");
    avancer(300);
    capture("80_mqtt_lumieres_renommee");
    // Imprimante : automatisation proposée (ha_propositions) ; impression FABRIQUÉE au format du rendu HA (le capteur de Klipper
    // n'existe pas encore et l'imprimante est éteinte), puis l'état réel du 23/10 : éteinte
    recevoir("dashboard_guition/imprimante/etat",
             "{\"buse\": 235, \"buse_c\": 235, \"caisson\": 27, \"caisson_c\": 30, \"carte_mere\": 0, \"chauffage\": 1, \"couche\": 84, "
             "\"couches\": 136, \"etat\": \"impression\", \"fichier\": \"support_camera_v3\", \"filtration\": 0, \"fin\": 912, \"lumiere\": 1, "
             "\"plateau\": 70, \"plateau_c\": 70, \"progression\": 62, \"reste_min\": 38, \"vignette\": \"\"}");
    aller("VMC");
    aller("Imprimante 3D");
    toucher(100, 24); // onglet Suivi
    lacher();
    avancer(600);
    capture("81_mqtt_imprimante_impression");
    toucher(70, 438); // Pause
    lacher();
    recevoir("dashboard_guition/imprimante/etat",
             "{\"buse\": 22, \"buse_c\": 0, \"caisson\": 22, \"caisson_c\": 0, \"carte_mere\": 0, \"chauffage\": 0, \"couche\": 0, \"couches\": 0, "
             "\"etat\": \"eteinte\", \"fichier\": \"\", \"filtration\": 0, \"fin\": 874, \"lumiere\": 0, \"plateau\": 21, \"plateau_c\": 0, "
             "\"progression\": 0, \"reste_min\": 0, \"vignette\": \"\"}");
    avancer(300);
    capture("82_mqtt_imprimante_eteinte");
    // Imprimante, 2e temps : liste rendue par HA sur les VRAIES métadonnées de Moonraker (23/10) ; les miniatures sont
    // les vraies, téléchargées dans donnees/vignettes/ (le simulateur lit le fichier au lieu du réseau)
    // (13/11 : le serviceur, nommé depuis, est déjà installé avant la page Musique — réinstallation identique)
    ui_images_telecharger = servir_images_locale;
    for (int k = 0; k < PR_FICHIERS_MAX; k++) // oublie les fiches fictives du début du scénario
    {
        pr_etat.fichiers[k] = pr_fichier_t{};
    }
    pr_etat.nb_fichiers = 0;
    pr_etat.choix = -1;
    rejouer("donnees/fichiers_imprimante_23-10.tsv");
    // impression du fichier le plus récent (état fabriqué au format du rendu HA), à 62 %
    recevoir("dashboard_guition/imprimante/etat",
             "{\"buse\": 205, \"buse_c\": 205, \"caisson\": 24, \"caisson_c\": 0, \"carte_mere\": 1, \"chauffage\": 1, \"couche\": 84, "
             "\"couches\": 136, \"etat\": \"impression\", \"fichier\": \"respeaker3_0.2mm_PLA_Generic Klipper Printer_1h44m\", \"filtration\": 0, "
             "\"fin\": 912, \"lumiere\": 1, \"plateau\": 60, \"plateau_c\": 60, \"progression\": 62, \"reste_min\": 38, \"vignette\": \"\"}");
    avancer(8000); // 24 miniatures, une toutes les 300 ms
    aller("VMC");
    aller("Imprimante 3D");
    toucher(100, 24); // Suivi
    lacher();
    avancer(600);
    capture("83_mqtt_imprimante_vignette");
    toucher(245, 24); // Fichiers
    lacher();
    avancer(600);
    capture("84_mqtt_imprimante_fichiers");
    toucher(200, 220); // 3e fichier : aperçu, vraie miniature de Moonraker
    lacher();
    avancer(300);
    capture("84b_mqtt_imprimante_apercu");
    printf("[mqtt] imprimante : %d fichiers, travail %d (%s)\n", pr_etat.nb_fichiers, pr_etat.travail,
           pr_etat.travail >= 0 ? pr_etat.fichiers[pr_etat.travail].nom : "-");
    // Maquette Biblio / Artistes (23/10) : VRAIS artistes de Music Assistant (537 ; les 37 en M), bande alphabétique
    {
        static const char *m_reels[] = {"Machito", "Macklemore", "Macklemore & Ryan Lewis", "Madeleine Peyroux", "Madonna", "Makaveli", "Makaveli & Dillinger", "Mariah Carey", "Maria Joao Pires", "Maria Yudina", "Marilyn Manson", "Martha Argerich I", "Martha Argerich II", "Marty Robbins", "Marvin Gaye", "Massive Attack", "Maurizio Pollini", "Maurizio Pollini I", "MC5", "Megadeth", "Merle Haggard", "Metallica", "Michael Jackson", "Mikhail Pletnev", "Miles Davis", "Miriam Makeba", "Mitsuko Uchida", "Moby", "Moby Grape", "Modest Mouse", "Motörhead", "Muddy Waters", "Multi-interprètes", "Murray Perahia", "Muse", "My Chemical Romance", "Myra Hess"};
        mu_etat.nb_artistes = (int)(sizeof m_reels / sizeof *m_reels);
        for (int i = 0; i < mu_etat.nb_artistes; i++)
        {
            snprintf(mu_etat.artistes[i].nom, sizeof mu_etat.artistes[i].nom, "%s", m_reels[i]);
            mu_etat.artistes[i].detail[0] = 0;
        }
        mu_etat.lettre = 'M';
        snprintf(mu_etat.lettres, sizeof mu_etat.lettres, "%s", "#ABCDEFGHIJKLMNOPQRSTUVWYZ");
        mu_commande = [](mu_action_t a, int arg) { printf("[musique] action %d arg %d (%c)\n", (int)a, arg, a == MU_LETTRE ? arg : ' '); };
    }
    aller("VMC");
    aller("Musique");
    toucher(130, 24); // Biblio
    lacher();
    avancer(300);
    capture("85_maquette_artistes");
    toucher(19, 52 + 20 * 15 + 7, 200); // doigt sur la bande (à gauche) : T (rang 20)
    capture("86_maquette_artistes_bande");
    lacher();
    avancer(200);
    capture("87_maquette_artistes_T_chargement");
    // Maquette des radios (23/10, retour d'usage : « une liste comme artiste pour l'instant ») : les 33 radios choisies
    // (françaises, les plus écoutées, genres variés, + BFM Business), ordre alphabétique ; FIP en cours d'écoute
    {
        static const char *noms[] = {"100% Radio 80", "BFM Business", "Chérie FM", "Eurodance 90", "Europe 1", "Europe 2", "FIP", "FIP Jazz", "FIP Monde", "FIP Rock", "France Culture", "France Info", "France Inter", "France Musique", "Fun Radio", "Jazz Radio", "Jazz Radio Blues", "La Grosse Radio Reggae", "M Radio", "Nostalgie", "NRJ Hits", "OUI FM", "Radio Classique", "Radio Meuh", "Radio Nova", "RFM", "Rire et Chansons", "RMC", "RTL", "RTL 2", "Skyrock", "Skyrock Klassics", "TSF Jazz"};
        mu_etat.nb_radios = (int)(sizeof noms / sizeof *noms);
        for (int i = 0; i < mu_etat.nb_radios; i++)
        {
            snprintf(mu_etat.radios[i].nom, sizeof mu_etat.radios[i].nom, "%s", noms[i]);
            mu_etat.radios[i].detail[0] = 0;
            mu_etat.radios[i].img = nullptr;
        }
        snprintf(mu_etat.artiste, sizeof mu_etat.artiste, "FIP"); // radio en cours (duree 0 = direct)
        mu_etat.duree = 0;
        aller("VMC");
        aller("Musique");
        toucher(220, 24); // Radios
        lacher();
        avancer(300);
        capture("90_maquette_radios");
        glisser(200, 400, 200, 120, 10);
        lacher();
        avancer(600);
        capture("91_maquette_radios_suite");
    }
    // 24/10 : Musique branchée DIRECTEMENT sur Music Assistant (ui_musique), sur de VRAIES réponses de MA enregistrées le
    // 24/10 (donnees/ma/, lecture seule) : seule la file du Projecteur et les albums d'ABBA sont enregistrés. Les commandes
    // ne partent pas : elles sont affichées.
    {
        ui_ma_serveur = "http://192.168.1.11:8095";
        ui_ma_requete = [](const char *corps, char **reponse, size_t *taille)
        {
            JsonDocument q;
            deserializeJson(q, corps);
            std::string c = q["command"] | "";
            const char *file = q["args"]["queue_id"] | "upbddd0d2cbad1";
            const char *artiste = q["args"]["item_id"] | "216";
            const bool lecture = c == "players/all" || c == "player_queues/get" || c == "player_queues/items" ||
                                 c == "music/artists/library_items" || c == "music/artists/artist_albums" ||
                                 c == "music/radios/library_items";
            if (!lecture)
            {
                printf("[MA] commande (non envoyée) : %s\n", corps);
                *reponse = strdup("null");
                *taille = 4;
                return true;
            }
            if (strcmp(file, "upbddd0d2cbad1") || strcmp(artiste, "216")) // pas enregistré : comme une erreur de MA
                return false;
            for (char &x : c)
                if (x == '/')
                    x = '_';
            FILE *f = fopen(("donnees/ma/" + c + ".json").c_str(), "rb");
            if (!f)
                return false;
            fseek(f, 0, SEEK_END);
            *taille = ftell(f);
            fseek(f, 0, SEEK_SET);
            *reponse = (char *)malloc(*taille + 1);
            bool ok = fread(*reponse, 1, *taille, f) == *taille;
            (*reponse)[*taille] = 0;
            fclose(f);
            if (ok && g_simu_picore && c == "players_all") // piCore vient de démarrer : il apparaît dans MA
            {
                JsonDocument d;
                deserializeJson(d, (const char *)*reponse, *taille);
                JsonObject m = d.add<JsonObject>();
                m["player_id"] = "aa:bb:cc:dd:ee:01";
                m["available"] = true;
                m["playback_state"] = "idle";
                m["volume_level"] = 30;
                free(*reponse);
                *taille = measureJson(d);
                *reponse = (char *)malloc(*taille + 1);
                serializeJson(d, *reponse, *taille + 1);
            }
            return ok;
        };
        ui_musique_brancher();
        aller("VMC");
        aller("Musique");
        toucher(40, 24); // Lecture
        lacher();
        avancer(1500); // 1er tic : artistes + lecteurs + état (piCore éteint -> PC-musique, dont la file n'est pas enregistrée)
        capture("92_ma_lecture_pc");
        toucher(310, 24); // Sortie
        lacher();
        avancer(300);
        capture("93_ma_sortie");
        toucher(150, 72 + 2 * 66 + 30); // Projecteur (rien ne joue : on change seulement de lecteur)
        lacher();
        avancer(1500);
        capture("94_ma_sortie_projecteur");
        toucher(40, 24); // Lecture
        lacher();
        avancer(1500);
        capture("95_ma_lecture_projecteur");
        toucher(341, 437); // ☰ : la file
        lacher();
        avancer(300);
        capture("96_ma_file");
        mu_commande(MU_LETTRE, 'A'); // 24/10 : la lettre en cours est gardée d'un chargement à l'autre (ici M, maquette précédente)
        toucher(130, 24); // Biblio : lettre A
        lacher();
        avancer(300);
        capture("97_ma_biblio_A");
        toucher(150, 88 + 23); // 1er artiste : ABBA
        lacher();
        avancer(6000); // pochettes : une toutes les 300 ms
        capture("98_ma_albums_abba");
        toucher(50, 70); // « < ABBA » : retour aux artistes
        lacher();
        avancer(300);
        // bande (24/10, à droite, x 360..400, y 88..466) : le doigt au bas la fait glisser vers la 2e moitié, puis remonte
        // au milieu et se lève sur la lettre qui s'y trouve (M visé)
        toucher(380, 88 + 378 - 4, 460);
        capture("100_bande_glisse");
        doigt_y = 88 + 189;
        avancer(60);
        lacher();
        avancer(300);
        printf("[simu] bande : lettre choisie %c (visée : M)\n", mu_etat.lettre);
        capture("99_ma_biblio_M");
        // 25/10 : radios FAVORITES de MA (vraie réponse) et leurs logos servis par MA (24 sur 33 ; sinon carré vide)
        toucher(220, 24); // Radios
        lacher();
        avancer(12000); // un logo toutes les 300 ms
        capture("124_radios_favorites");
        glisser(200, 420, 200, 100, 12);
        lacher();
        avancer(800);
        capture("125_radios_favorites_bas");
        // Maquette du 24/10 : onglet Sortie avec la chaîne piCore (état de HA) et l'ampli (marche / entrées)
        mu_etat.chaine = MU_CH_PRETE;
        mu_etat.ampli = true;
        mu_etat.entree = 1;
        toucher(310, 24); // Sortie
        lacher();
        avancer(300);
        capture("100_maquette_sortie_chaine_prete");
        toucher(200, 319); // chaîne : 1er appui = confirmation
        lacher();
        avancer(300);
        capture("101_maquette_sortie_confirmer_arret");
        toucher(130, 24); // un autre onglet annule la confirmation
        lacher();
        mu_etat.chaine = MU_CH_DEMARRAGE;
        mu_etat.ampli = false;
        toucher(310, 24);
        lacher();
        avancer(300);
        capture("102_maquette_sortie_demarrage");
        mu_etat.chaine = MU_CH_ETEINTE;
        page_musique_maj();
        avancer(300);
        capture("103_maquette_sortie_eteinte");
        // Lecture en attente (24/10) : Salon choisi chaîne éteinte, une radio -> HA allume la chaîne ; la radio part quand
        // piCore apparaît dans MA
        recevoir("dashboard_guition/musique/etat", "{\"picore\":\"eteint\",\"ampli\":0}");
        toucher(150, 72 + 30); // Salon
        lacher();
        avancer(1500);
        capture("104_ma_sortie_salon_eteint");
        toucher(220, 24); // Radios
        lacher();
        avancer(300);
        toucher(150, 88 + 23 + 6 * 50); // FIP
        lacher();
        avancer(1500);
        capture("105_ma_attente_picore");
        recevoir("dashboard_guition/musique/etat", "{\"picore\":\"demarrage\",\"ampli\":1}");
        avancer(2000);
        printf("[simu] piCore apparaît dans MA\n");
        g_simu_picore = true;
        avancer(6000);
        capture("106_ma_attente_partie");
    }
    // 24/10 : page PC sur de VRAIS messages de l'agent du PC (btop_agent.py en MQTT, donnees/pc_*_24-10.json), puis
    // « PC en veille » après 10 s sans nouvelles
    {
        auto lire = [](const char *chemin)
        {
            std::string s;
            FILE *f = fopen(chemin, "r");
            char b[4096];
            size_t n;
            while (f && (n = fread(b, 1, sizeof b, f)) > 0)
                s.append(b, n);
            if (f)
                fclose(f);
            return s;
        };
        aller("PC");
        recevoir("dashboard_guition/pc/processus", lire("donnees/pc_processus_24-10.json").c_str());
        // 31/10 : relevé du 31/10 (blocs de VRAM) ; 320 états à 1 s d'intervalle, la charge variée, pour remplir le graphe
        const std::string etat = lire("donnees/pc_etat_31-10.json");
        for (int i = 0; i < 320; i++)
        {
            const int v = i > 100 && i < 130 ? 60 + i % 25 : i > 250 && i < 262 ? 90 : 5 + i % 7;
            std::string e = etat;
            e.replace(e.find("\"cpu\":9,"), 8, "\"cpu\":" + std::to_string(v) + ",");
            recevoir("dashboard_guition/pc/etat", e.c_str());
            avancer(1000);
        }
        avancer(300);
        capture("107_mqtt_pc_systeme");
        toucher(200, 24); // GPU
        lacher();
        avancer(300);
        capture("107b_mqtt_pc_gpu");
        toucher(301, 24); // Processus
        lacher();
        avancer(300);
        capture("108_mqtt_pc_processus");
        // 11/11 : plus d'onglet « IA » ici (page séparée du rail, scénario 170+). Le sujet pc/ia arrive toujours —
        // avec UN SEUL compte désormais (CONTRAT du 11/11) — et nourrit la page IA. Capture : PC en veille, 3 onglets.
        {
            const uint32_t m = (uint32_t)time(nullptr);
            char ia[240];
            snprintf(ia, sizeof ia,
                     "{\"comptes\":[{\"nom\":\"compte-un@exemple.invalid\",\"h5\":92,\"h5_reset\":%u,\"semaine\":13,\"semaine_reset\":%u,\"maj\":%u}]}",
                     m + 2 * 3600 + 13 * 60, m + 6 * 86400 + 14 * 3600, m - 60);
            recevoir("dashboard_guition/pc/ia", ia);
        }
        avancer(11000);
        capture("109_mqtt_pc_veille");
    }
    // --- 11/11/2026 : page « IA » (maquette validée du 10/11) : nuit et jour x les trois poses de Clawd, pires cas,
    // puis la planche des trois icônes soumises au choix retenu ---
    {
        auto envoyer_activite = [&](const char *etat, const char *alerte, uint32_t tokens, int outils, int prompts,
                                    int sessions, const uint32_t *histo)
        {
            char json[900];
            int n = snprintf(json, sizeof json,
                             "{\"etat\":\"%s\",\"alerte\":\"%s\",\"tokens\":%u,\"outils\":%d,\"prompts\":%d,\"sessions\":%d,\"histo\":[",
                             etat, alerte, tokens, outils, prompts, sessions);
            for (int i = 0; i < IA_HISTO; i++)
                n += snprintf(json + n, sizeof json - n, "%u%s", histo[i], i == IA_HISTO - 1 ? "" : ",");
            snprintf(json + n, sizeof json - n, "],\"maj\":%u}", (uint32_t)time(nullptr));
            recevoir("dashboard_guition/pc/ia_activite", json);
        };
        // production d'une soirée sur le PC : tranches de 10 min en tokens (creux et pics, la dernière = en cours)
        const uint32_t H[IA_HISTO] = {0, 0, 2500, 6200, 11000, 4800, 0, 0, 0, 3400, 7600, 12800, 18400, 14100, 10200,
                                      6300, 8500, 15200, 22100, 26400, 19300, 11600, 5400, 9200, 16500, 24800, 30100,
                                      21700, 13400, 17800};
        const uint32_t H0[IA_HISTO] = {0};
        auto envoyer_compte = [&](const char *nom, int h5, int semaine)
        {
            const uint32_t m = (uint32_t)time(nullptr);
            char ia[240];
            snprintf(ia, sizeof ia,
                     "{\"comptes\":[{\"nom\":\"%s\",\"h5\":%d,\"h5_reset\":%u,\"semaine\":%d,\"semaine_reset\":%u,\"maj\":%u}]}",
                     nom, h5, m + 2 * 3600 + 13 * 60, semaine, m + 6 * 86400 + 14 * 3600, m - 60);
            recevoir("dashboard_guition/pc/ia", ia);
        };
        // « en attente de l'agent du PC » : on repart à zéro (le compte du scénario PC a été reçu avant)
        memset(&ia_compte, 0, sizeof(ia_compte));
        memset(&ia_activite, 0, sizeof(ia_activite));
        recevoir("dashboard_guition/maison/etat", "{\"phase\":\"nuit\",\"presence\":1}"); // le ciel suit la phase HA
        aller("IA");
        avancer(300);
        capture("170_ia_attente_donnees");
        envoyer_compte("compte-un@exemple.invalid", 92, 13); // relevé du 31/10, heures recalées sur maintenant
        envoyer_activite("reflechit", "", 612345, 411, 14, 2, H);
        avancer(300);
        capture("171_ia_nuit_reflechit");
        envoyer_activite("attente", "", 612345, 411, 14, 2, H);
        avancer(300);
        capture("172_ia_nuit_attente");
        envoyer_activite("repos", "", 612345, 411, 14, 2, H);
        avancer(300);
        capture("173_ia_nuit_repos");
        recevoir("dashboard_guition/maison/etat", "{\"phase\":\"jour\",\"presence\":1}");
        envoyer_activite("reflechit", "", 612345, 411, 14, 2, H);
        avancer(300);
        capture("174_ia_jour_reflechit");
        envoyer_activite("attente", "", 612345, 411, 14, 2, H);
        avancer(300);
        capture("175_ia_jour_attente");
        envoyer_activite("repos", "", 612345, 411, 14, 2, H);
        avancer(300);
        capture("176_ia_jour_repos");
        // les deux alertes (l'état passe en rouge, Clawd prend le « ! » quelle que soit l'alerte)
        envoyer_activite("reflechit", "autorisation", 612345, 411, 14, 2, H);
        avancer(300);
        capture("177_ia_alerte_autorisation");
        envoyer_activite("reflechit", "question", 612345, 411, 14, 2, H);
        avancer(300);
        capture("178_ia_alerte_question");
        envoyer_activite("reflechit", "", 612345, 411, 14, 2, H);
        // pires cas demandés : jauge à 100 %, très grands nombres, histogramme vide, adresse de compte à rallonge
        envoyer_compte("compte-un@exemple.invalid", 100, 100);
        avancer(1200);
        capture("179_ia_jauge_100");
        envoyer_compte("compte-un@exemple.invalid", 92, 13);
        envoyer_activite("reflechit", "", 12345000, 999, 987, 12, H);
        avancer(1200);
        capture("180_ia_grands_nombres");
        envoyer_activite("reflechit", "", 0, 0, 0, 0, H0);
        avancer(1200);
        capture("181_ia_histo_vide");
        envoyer_compte("prenom.nom-tres-long-avec-points@corp-international-exemple.fr", 7, 64);
        avancer(1200);
        capture("182_ia_adresse_longue");
        envoyer_compte("compte-un@exemple.invalid", 92, 13);
        envoyer_activite("reflechit", "", 612345, 411, 14, 2, H);
        // péremption : dernier message vieux de 5 min (agent arrêté) -> « au repos », derniers chiffres gardés
        {
            char json[900];
            int n = snprintf(json, sizeof json,
                             "{\"etat\":\"reflechit\",\"alerte\":\"\",\"tokens\":612345,\"outils\":411,\"prompts\":14,\"sessions\":2,\"histo\":[");
            for (int i = 0; i < IA_HISTO; i++)
                n += snprintf(json + n, sizeof json - n, "%u%s", H[i], i == IA_HISTO - 1 ? "" : ",");
            snprintf(json + n, sizeof json - n, "],\"maj\":%u}", (uint32_t)time(nullptr) - 300);
            recevoir("dashboard_guition/pc/ia_activite", json);
        }
        avancer(1200);
        capture("183_ia_hors_ligne");
        envoyer_activite("reflechit", "", 612345, 411, 14, 2, H); // on repart frais pour la suite du scénario
        // 12/11/2026 : ciel animé (maison/ciel) — heures du 12/11 relevées dans HA (aube 07:12, lever 07:46,
        // coucher 17:22, crépuscule 17:56 heure de Paris) ; l'heure de la scène est imposée (page_ia_heure_test)
        {
            const uint32_t A = 1794550018 - 86400, L = 1794552045 - 86400, C = 1794586954 - 86400, D = 1794588979 - 86400;
            auto ciel = [&](uint32_t t, float age, int pct, const char *meteo, int nuages, float pluie)
            {
                page_ia_heure_test = t;
                auto prochain = [&](uint32_t x) { return x > t ? x : x + 86400; };
                char json[300];
                snprintf(json, sizeof json,
                         "{\"aube\":%u,\"lever\":%u,\"coucher\":%u,\"crepuscule\":%u,\"lune_age\":%.1f,\"lune_pct\":%d,"
                         "\"meteo\":\"%s\",\"nuages\":%d,\"pluie\":%.1f,\"neige\":0.0}",
                         prochain(A), prochain(L), prochain(C), prochain(D), age, pct, meteo, nuages, pluie);
                recevoir("dashboard_guition/maison/ciel", json);
                avancer(300);
            };
            ciel(A + 15 * 60, 2.9f, 9, "partlycloudy", 30, 0);
            capture("190_ciel_aube");
            ciel(L + 20 * 60, 2.9f, 9, "partlycloudy", 30, 0);
            capture("191_ciel_lever");
            ciel(L + 150 * 60, 2.9f, 9, "sunny", 5, 0);
            capture("192_ciel_matin");
            ciel((L + C) / 2, 2.9f, 9, "partlycloudy", 55, 0);
            capture("193_ciel_midi_nuageux");
            ciel(C - 15 * 60, 2.9f, 9, "sunny", 5, 0);
            capture("194_ciel_soleil_couchant");
            ciel(C + 15 * 60, 2.9f, 9, "partlycloudy", 30, 0);
            capture("195_ciel_crepuscule");
            ciel(C + 100 * 60, 2.9f, 9, "clear-night", 0, 0);
            capture("196_ciel_nuit_croissant");
            ciel(C + 330 * 60, 14.8f, 100, "clear-night", 20, 0);
            capture("197_ciel_nuit_pleine_lune");
            ciel(C + 330 * 60, 22.1f, 50, "clear-night", 0, 0);
            ciel(L - 120 * 60 + 86400, 22.1f, 50, "clear-night", 0, 0);
            capture("198_ciel_nuit_dernier_quartier");
            ciel(C + 200 * 60, 2.9f, 9, "rainy", 95, 1.2f);
            capture("199_ciel_nuit_couverte_pluie");
            ciel(L + 300 * 60, 2.9f, 9, "pouring", 100, 4.2f);
            capture("200_ciel_jour_averse");
            for (int k = 0; k < 6; k++) // dérive : 24 s plus tard, les nuages ont avancé de 4 à 6 cases
                avancer(4000);
            capture("201_ciel_jour_averse_24s_plus_tard");
            ciel(L + 200 * 60, 2.9f, 9, "rainy", 80, 0.6f);
            capture("202_ciel_jour_pluie_faible");
            ciel(L + 200 * 60, 2.9f, 9, "snowy", 90, 0);
            avancer(2500);
            capture("203_ciel_jour_neige");
            ciel(C + 200 * 60, 2.9f, 9, "snowy", 70, 0);
            avancer(2500);
            capture("204_ciel_nuit_neige");
            ciel(L + 240 * 60, 2.9f, 9, "fog", 60, 0);
            capture("205_ciel_jour_brouillard");
            ciel(L + 260 * 60, 2.9f, 9, "lightning-rainy", 100, 3.0f);
            capture("206_ciel_orage");
            avancer(9000 + 250 + 60 - 300);
            // 13/11 (README, section « Page IA », GIF « orage ») : une capture par tic du minuteur pluie+éclair
            // (66 ms, src/ui_lvgl/page_ia.cpp:331), l'éclair dure 2 tics. Le calcul précédent (prochain éclair
            // déduit du cycle de 136 tics) le ratait : position RELEVÉE en capturant 160 tics d'affilée, l'éclair
            // tombe aux tics 76 et 77 après ce point. On saute 40 tics : 91 images, l'éclair aux images 36-37,
            // 6 s d'animation. 207 = l'image 36 (vérifiée : le ciel blanchit).
            {
                char nom[48];
                for (int i = 0; i < 40; i++) // par pas de 66 ms : un seul grand saut fait tourner le minuteur moins de fois
                    avancer(66);
                for (int i = 0; i < 91; i++)
                {
                    snprintf(nom, sizeof nom, "anim_orage_%03d", i);
                    capture(nom);
                    if (i == 36)
                        capture("207_ciel_orage_eclair");
                    avancer(66);
                }
            }
            // 12/11 : surface redessinée par seconde, 5 s par météo (le temps de dessin du PC ne dit rien de
            // l'écran ; la surface, si : sur l'écran ~0,66 µs par pixel, mesuré le 12/11 : 230 400 px en 151 ms)
            {
                struct { const char *nom, *meteo; int nuages; float pluie; uint32_t t; } M[] = {
                    {"nuit claire", "clear-night", 0, 0, C + 330 * 60}, {"jour nuageux", "partlycloudy", 70, 0, L + 200 * 60},
                    {"pluie faible", "rainy", 80, 0.6f, L + 200 * 60}, {"pluie moyenne", "rainy", 90, 2.0f, L + 200 * 60},
                    {"averse", "pouring", 100, 4.2f, L + 200 * 60}, {"orage", "lightning-rainy", 100, 3.0f, L + 200 * 60},
                    {"neige", "snowy", 90, 0, L + 200 * 60}};
                for (auto &m : M)
                {
                    ciel(m.t, 14.8f, 100, m.meteo, m.nuages, m.pluie);
                    avancer(1000);
                    g_flush_px = 0;
                    avancer(5000);
                    printf("[mesure ciel] %-14s : %7.0f px/s redessinés, soit ~%3.0f ms/s sur l'écran (%2.0f %% du temps)\n", m.nom,
                           g_flush_px / 5.0, g_flush_px / 5.0 * 0.66e-3, g_flush_px / 5.0 * 0.66e-3 / 10);
                }
            }
            page_ia_heure_test = 0;
        }
        // planche de choix : les 3 icônes « IA » côte à côte dans le rail (variante 1 à sa place, les 2 et 3 posées
        // provisoirement sur PC et Imprimante 3D, numéros collés en bas de chaque pastille)
        int i_ia = -1, i_pc = -1, i_imp = -1;
        for (int i = 0; i < (int)(sizeof(pages) / sizeof(pages[0])); i++)
        {
            if (!strcmp(pages[i].nom, "IA"))
                i_ia = i;
            if (!strcmp(pages[i].nom, "PC"))
                i_pc = i;
            if (!strcmp(pages[i].nom, "Imprimante 3D"))
                i_imp = i;
        }
        ui_shell_icone(i_pc, &icon_ia_etincelle, 0);
        ui_shell_icone(i_imp, &icon_ia_bulle, 0);
        // le rail ne défile pas tout seul (comportement actuel de ui_shell) : on l'amène sur les trois pastilles
        lv_obj_t *rail = nullptr; // la planche 185/186 du soir s'en sert aussi
        {
            struct rail_t
            {
                static lv_obj_t *cherche(lv_obj_t *o)
                {
                    // le rail porte toutes les pages SAUF l'épinglée (Batterie, dans son conteneur à part)
                    if (lv_obj_get_child_cnt(o) == (uint32_t)ui_shell_nb() - 1)
                        return o;
                    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
                    {
                        if (lv_obj_t *r = cherche(lv_obj_get_child(o, i)))
                            return r;
                    }
                    return nullptr;
                }
            };
            rail = rail_t::cherche(lv_scr_act());
            if (rail)
                lv_obj_scroll_to_y(rail, 6 + 12 * 68 - 10, LV_ANIM_OFF); // Imprimante (12), PC (13), IA (14) à l'écran (rail : pad 6, pas 68)
        }
        lv_obj_t *numeros[3] = {}; // numéros posés sur les pastilles de la planche 184, retirés juste après
        int nnum = 0;
        {
            struct trouve_t
            {
                static lv_obj_t *bouton_de(lv_obj_t *o, const void *src)
                {
                    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
                    {
                        lv_obj_t *c = lv_obj_get_child(o, i);
                        if (lv_obj_check_type(c, &UI_CLASSE_IMAGE) && lv_image_get_src(c) == src)
                            return o; // le bouton est le PARENT de l'image trouvée
                        if (lv_obj_t *r = bouton_de(c, src))
                            return r;
                    }
                    return nullptr;
                }
            };
            const struct
            {
                const void *src;
                const char *num;
            } pastilles[] = {{&icon_ia, "1"}, {&icon_ia_etincelle, "2"}, {&icon_ia_bulle, "3"}};
            for (auto &p : pastilles)
            {
                lv_obj_t *b = trouve_t::bouton_de(lv_scr_act(), p.src);
                if (!b)
                    continue;
                // blanc (pas orange) : la pastille active a un fond orange, le numéro doit rester lisible
                lv_obj_t *l = ui_label(b, UI_POLICE_PETITE, UI_TEXTE, p.num);
                lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -2);
                numeros[nnum++] = l;
            }
        }
        avancer(300);
        capture("184_ia_choix_icone");
        for (int i = 0; i < 3; i++)
            if (numeros[i])
                lv_obj_delete(numeros[i]); // les numéros sont pour la planche 184 seulement
        ui_shell_icone(i_pc, &icon_pc, 0); // remise en place des vraies icônes (le scénario continue)
        ui_shell_icone(i_imp, &icon_imprimante, 0);
        // 11/11 soir : contrôle de l'icône adoucie (smooth() d'icon2lvgl.py, réglage retenu du rail).
        // Zoom x2 plus proche voisin fait après coup par tools/zoom_png.py pour comparer les liserés.
        if (rail)
            lv_obj_scroll_to_y(rail, 99999, LV_ANIM_OFF); // en bas du rail (borné) : Imprimante, PC, IA entiers à l'écran
        ui_shell_aller(i_ia); // IA active : pastille orange, icône blanche
        avancer(300);
        capture("185_ia_icone_active");
        ui_shell_aller(i_pc); // IA inactive : icône grise sur fond carte
        avancer(300);
        capture("186_ia_icone_inactive");
    }
    // Maquette du 24/10 : Réglages (mélodies et éclairage terrasse de HA) et Infos (appareils de HA), sur les VRAIS
    // messages retenus (relevés le 24/10 dans donnees/)
    {
        auto lire = [](const char *chemin)
        {
            std::string s;
            FILE *f = fopen(chemin, "r");
            char b[4096];
            size_t n;
            while (f && (n = fread(b, 1, sizeof b, f)) > 0)
                s.append(b, n);
            if (f)
                fclose(f);
            return s;
        };
        JsonDocument j;
        deserializeJson(j, lire("donnees/maison_reglages_24-10.json"));
        rg_reglages_t &r = rg_reglages;
        snprintf(r.melodie, sizeof r.melodie, "%s", j["melodie"] | "");
        r.nb_melodies = 0;
        for (JsonVariantConst m : j["melodies"].as<JsonArrayConst>())
            snprintf(r.melodies[r.nb_melodies++], sizeof r.melodies[0], "%s", m | "");
        r.volume = j["volume"] | 0;
        r.terrasse_porte = j["terrasse_porte"] | 0;
        r.terrasse_mouvement = j["terrasse_mouvement"] | 0;
        aller("Réglages");
        avancer(300);
        glisser(200, 400, 200, 100, 10);
        lacher();
        avancer(600);
        capture("110_maquette_reglages_maison");
        deserializeJson(j, lire("donnees/maison_appareils_24-10.json"));
        if_nb_appareils = 0;
        for (JsonArrayConst a : j["liste"].as<JsonArrayConst>())
        {
            if (if_nb_appareils >= IF_NB_APPAREILS)
                break;
            if_appareil_t &x = if_appareils[if_nb_appareils++];
            snprintf(x.nom, sizeof x.nom, "%s", a[0] | "");
            snprintf(x.piece, sizeof x.piece, "%s", a[1] | "");
            x.ok = (a[2] | 0) != 0;
        }
        aller("Informations");
        avancer(300);
        capture("111_maquette_infos_haut");
        glisser(200, 400, 200, 100, 10);
        lacher();
        avancer(600);
        capture("112_maquette_infos_appareils");
        glisser(200, 400, 200, 100, 10);
        lacher();
        avancer(600);
        capture("113_maquette_infos_appareils_suite");
        // Branchement (24/10) : l'écran démarre (réglages publiés), reçoit les retenus de HA, puis HA change deux réglages
        // de CET écran par appareil/cuisine/reglages/set
        ui_appareil_demarrer("cuisine");
        recevoir("dashboard_guition/maison/reglages", lire("donnees/maison_reglages_24-10.json").c_str());
        recevoir("dashboard_guition/maison/appareils", lire("donnees/maison_appareils_24-10.json").c_str());
        avancer(1500);
        recevoir("dashboard_guition/appareil/chambre2/reglages/set", "{\"retour_s\":0}"); // pas pour cet écran : ignoré
        recevoir("dashboard_guition/appareil/cuisine/reglages/set", "{\"retour_s\":120,\"lum_nuit\":20}");
        aller("Réglages");
        avancer(300);
        capture("114_mqtt_reglages_ecran");
        // Rétroéclairage (24/10, repris de la production) : veille -> niveau de repos (le toucher clique) ; absent depuis 5 min
        // -> éteint (le toucher qui rallume ne clique pas) ; fondus d'1 s
        static std::vector<int> mli;
        ui_appareil_lumiere = [](int v) { mli.push_back(v); };
        recevoir("dashboard_guition/appareil/cuisine/reglages/set", "{\"veille_s\":30}");
        recevoir("dashboard_guition/maison/etat", "{\"phase\":\"nuit\",\"presence\":1}");
        avancer(33000);
        printf("[simu] veille, présent : MLI %d (repos de nuit %d %% -> %d), toucher avalé %d (attendu 0)\n", mli.empty() ? -1 : mli.back(),
               rg_reglages.lum_nuit_repos, rg_reglages.lum_nuit_repos * 255 / 100, ui_appareil_toucher(true));
        ui_appareil_toucher(false);
        recevoir("dashboard_guition/maison/etat", "{\"phase\":\"nuit\",\"presence\":0}");
        avancer(5 * 60 * 1000 + 3000);
        printf("[simu] veille, absent 5 min : MLI %d (attendu 0)\n", mli.empty() ? -1 : mli.back());
        mli.clear();
        const bool a1 = ui_appareil_toucher(true), a2 = ui_appareil_toucher(true), a3 = ui_appareil_toucher(false);
        avancer(1200);
        printf("[simu] réveil : avalé %d %d, relâché %d (attendu 1 1 0) ; fondu %zu valeurs, de %d à %d (nuit en service %d %% -> %d)\n",
               a1, a2, a3, mli.size(), mli.empty() ? -1 : mli.front(), mli.empty() ? -1 : mli.back(), rg_reglages.lum_nuit,
               rg_reglages.lum_nuit * 255 / 100);
        if (mli.size() > 10)
            printf("[simu] premiers pas du fondu : %d %d %d %d %d (gamma : progressifs en bas)\n", mli[0], mli[1], mli[2], mli[3], mli[4]);
        recevoir("dashboard_guition/maison/etat", "{\"phase\":\"jour\",\"presence\":1}");
        aller("Réglages");
        avancer(300);
        capture("115_maquette_luminosites");
        // Maquette du journal (24/10) : lignes produites par ui_journal (l'heure = celle du PC)
        if_journal_effacer();
        ui_journal(false, TXT("Démarrage (mise sous tension) · données en 4" SEP_DEC "2 s", "Startup (power-on) · data in 4" SEP_DEC "2 s"));
        ui_journal(false, "Wi-Fi Maison · -61 dBm");
        ui_journal(false, TXT("MQTT connecté en 0" SEP_DEC "8 s", "MQTT connected in 0" SEP_DEC "8 s"));
        ui_journal(true, TXT("Wi-Fi perdu", "Wi-Fi lost"));
        ui_journal(false, "Wi-Fi Maison · -64 dBm");
        ui_journal(false, TXT("MQTT connecté en 1" SEP_DEC "3 s", "MQTT connected in 1" SEP_DEC "3 s"));
        ui_journal(true, TXT("Home Assistant muet depuis 3 min", "Home Assistant silent for 3 min"));
        ui_journal(false, TXT("Home Assistant répond de nouveau", "Home Assistant responding again"));
        aller("Informations");
        avancer(300);
        for (int k = 0; k < 4; k++)
        {
            glisser(200, 420, 200, 80, 10);
            lacher();
            avancer(400);
        }
        capture("116_maquette_journal");
    }

    // 24/10 : stress du store (l'écran de la cuisine se fige en glissant le curseur, deux cœurs bloqués, sans trace) :
    // glissés dans tous les sens, hors du curseur et hors de l'écran, positions reçues de HA doigt posé (le store bouge)
    aller("Ouvrants");
    avancer(300);
    for (int tour = 0; tour < 40; tour++)
        for (int i = 0; i < OV_NB; i++)
        {
            const int x = i ? 300 : 100;
            char json[48];
            toucher(x, 92 + (tour * 37) % 150);
            for (int k = 0; k < 24; k++)
            {
                doigt_x = x + ((k * 53 + tour * 7) % 260) - 130;
                doigt_y = (k * 97 + tour * 31) % 520 - 20;
                avancer(20);
                if (k % 5 == 0)
                {
                    snprintf(json, sizeof json, "{\"position\":%d}", (k * 13 + tour) % 101);
                    recevoir(i ? "dashboard_guition/ouvrants/volet_cuisine/etat" : "dashboard_guition/ouvrants/store_terrasse/etat", json);
                    recevoir("dashboard_guition/ouvrants/toile/etat", k % 10 ? "{\"position\":40,\"mouvement\":\"ferme\"}" : "{\"position\":100}");
                }
            }
            lacher();
        }
    capture("117_stress_store");

    // 24/10 : le bandeau d'icônes jeté vers le bas (geste vif, inertie) : il doit rester plein, sans vide sous la dernière icône
    for (int k = 0; k < 3; k++)
    {
        glisser(440, 440, 440, 40, 4);
        lacher();
    }
    avancer(1500);
    capture("118_bandeau_en_bas");
    glisser(440, 40, 440, 440, 4);
    lacher();
    avancer(1500);

    // 24/10 : faire défiler Réglages en partant d'un curseur de luminosité (retour d'usage : « les sliders bougent quand je veux
    // faire glisser la page ») : la page défile, la valeur ne bouge pas ; un glissé horizontal, lui, règle
    aller("Réglages");
    avancer(600);
    rg_reglages.lum_jour = 75;
    page_reglages_maj();
    avancer(100);
    const int avant = rg_reglages.lum_jour;
    glisser(150, 150, 165, 20, 12); // vers le haut, un peu de travers comme un vrai doigt
    lacher();
    avancer(800);
    printf("[simu] glissé vertical sur le curseur : lum_jour %d -> %d (attendu : inchangé)\n", avant, rg_reglages.lum_jour);
    capture("119_reglages_defile_depuis_curseur");
    aller("Informations"); // la page est recréée en haut au retour
    avancer(300);
    aller("Réglages");
    avancer(600);
    glisser(280, 150, 120, 152, 12); // horizontal vers la gauche
    lacher();
    avancer(300);
    printf("[simu] glissé horizontal sur le curseur : lum_jour %d (attendu : plus bas que %d)\n", rg_reglages.lum_jour, avant);

    // 24/10 (retour d'usage : « j'ai du mal avec le volume, il visait le bouton source ») : qui reçoit le doigt, point par point
    {
        aller("Musique");
        avancer(600);
        struct { int x, y; const char *ou; } pts[] = {
            {300, 92, "anneau, en haut à droite (poignée vers 75 %)"}, {362, 66, "texte Salon"}, {368, 98, "légende sortie"},
            {80, 70, "coin haut-gauche du carré, hors anneau"}, {340, 330, "coin bas-droit du carré, hors anneau"},
            {202, 52, "anneau, en haut"}};
        for (auto &q : pts)
        {
            lv_obj_t *o = nullptr;
            lv_point_t p = {(lv_coord_t)q.x, (lv_coord_t)q.y};
            o = lv_indev_search_obj(lv_layer_top(), &p);
            if (!o)
                o = lv_indev_search_obj(lv_scr_act(), &p);
            const char *qui = !o ? "rien" : lv_obj_check_type(o, &lv_arc_class) ? "VOLUME" : lv_obj_check_type(o, &UI_CLASSE_BOUTON) ? "bouton" : "autre";
            printf("[simu] doigt en (%d, %d), %s : %s\n", q.x, q.y, q.ou, qui);
        }
    }

    // 24/10 (retour d'usage : « quand je vise le bas du disque RGB, un bout de disque rouge apparaît dessous ») : doigt au bas du
    // disque (x 172..392, y 52..272), sur le bord puis un peu dessous
    aller("Lumières");
    avancer(600);
    toucher(282, 266, 120);
    lacher();
    avancer(300);
    capture("120_lumieres_bas_disque");
    toucher(282, 280, 120);
    lacher();
    avancer(300);
    capture("121_lumieres_sous_disque");
    // 24/10 : liste descendue sur « Toutes », puis une pièce : la liste doit repartir du haut
    toucher(40, 24); // Toutes
    lacher();
    glisser(80, 420, 80, 120, 10);
    lacher();
    avancer(800);
    toucher(118, 24); // Séjour
    lacher();
    avancer(300);
    capture("122_lumieres_piece_en_haut");

    // 24/10 (retour d'usage : « programmer un café par incrément de 5 minutes ») : depuis 6:43, + -> 6:45, − -> 6:40, appui long +
    cf_programmes[CF_CAFE].minute = 6 * 60 + 43;
    cf_programmes[CF_CAFE].actif = false;
    aller("Café");
    toucher(140, 24); // onglet Café
    lacher();
    avancer(300);
    toucher(308, 149); // +
    lacher();
    printf("[simu] café : 6:43 puis + -> %d:%02d (attendu 6:45)\n", cf_programmes[CF_CAFE].minute / 60, cf_programmes[CF_CAFE].minute % 60);
    cf_programmes[CF_CAFE].minute = 6 * 60 + 43;
    toucher(95, 149); // −
    lacher();
    printf("[simu] café : 6:43 puis − -> %d:%02d (attendu 6:40)\n", cf_programmes[CF_CAFE].minute / 60, cf_programmes[CF_CAFE].minute % 60);
    toucher(308, 149, 1500); // appui long sur +
    lacher();
    printf("[simu] café : appui long + 1,5 s depuis 6:40 -> %d:%02d (pas 5, 10, 20 puis 30)\n", cf_programmes[CF_CAFE].minute / 60, cf_programmes[CF_CAFE].minute % 60);
    cf_programmes[CF_CAFE].minute = 6 * 60 + 40;
    toucher(308, 149, 3000); // appui long de 3 s : le pas plafonne à 30 min
    lacher();
    printf("[simu] café : appui long + 3 s depuis 6:40 -> %d:%02d\n", cf_programmes[CF_CAFE].minute / 60, cf_programmes[CF_CAFE].minute % 60);
    toucher(308, 149); // un appui simple après : de nouveau 5 min
    lacher();
    printf("[simu] café : puis un appui simple -> %d:%02d (+5)\n", cf_programmes[CF_CAFE].minute / 60, cf_programmes[CF_CAFE].minute % 60);

    // 24/10 : écran de démarrage (comme la production), pendant la synchronisation avec HA, puis effacé
    ui_demarrage_creer("Cuisine");
    ui_demarrage_etape(TXT("Synchronisation avec Home Assistant…", "Synchronizing with Home Assistant…"), 65);
    avancer(400);
    capture("123_demarrage");
    ui_demarrage_etape("Bibliothèque musicale…", 85);
    avancer(400);
    ui_demarrage_fin();
    avancer(600);

    // 25/10 : poêle en mode Manuel (tirage réglé à la main) : la jauge du tirage
    pl_etat.mode = 1;
    pl_etat.tirage_manuel = 55;
    aller("Poêle");
    avancer(300);
    toucher(100, 24); // onglet Feu
    lacher();
    avancer(300);
    capture("126_poele_manuel");

    // 25/10 : maquettes de la page Eau chaude avec suivi du compteur d'eau (vraies valeurs de HA ; rien de branché)
    aller("Eau chaude");
    avancer(300);
    for (int v = 1; v <= 3; v++)
    {
        lv_obj_t *m = maquette_eau(v);
        avancer(200);
        char nom[40];
        snprintf(nom, sizeof nom, "13%d_maquette_eau_v%d", v - 1, v);
        capture(nom);
        lv_obj_del(m);
    }
    const char *const onglets[] = {"133_eau_onglet_ballon", "134_eau_onglet_courbe", "135_eau_onglet_reglages", "136_eau_ballon_chauffe"};
    for (int o = 0; o < 4; o++)
    {
        lv_obj_t *m = maquette_eau_onglet(o);
        avancer(200);
        capture(onglets[o]);
        lv_obj_del(m);
    }
    // 26/10 : page « Batterie et capteur » (la vraie), sur batterie puis en charge ; pile vivante dans le rail
    batterie_fictive(false);
    aller("Batterie et capteur");
    avancer(300);
    capture("144_batterie_page");
    batterie_fictive(true);
    avancer(300);
    capture("145_batterie_page_charge");
    // la vue Radar (seuils de la production, quelqu'un à 2,40 m) : zone 6 choisie au doigt, +2, onglet Maintien, retour
    {
        static const uint8_t D[16] = {75, 65, 40, 30, 25, 25, 20, 20, 51, 50, 30, 28, 25, 25, 25, 25};
        static const uint8_t M[16] = {20, 20, 20, 20, 20, 20, 15, 14, 25, 25, 25, 25, 25, 22, 22, 22};
        static const uint8_t S[16] = {38, 30, 34, 62, 22, 16, 12, 10, 9, 8, 7, 6, 6, 5, 5, 5};
        rd_etat.present = rd_etat.presence = rd_etat.pc = true;
        rd_etat.distance = 240;
        memcpy(rd_etat.detection, D, 16);
        memcpy(rd_etat.maintien, M, 16);
        memcpy(rd_etat.energie, S, 16);
        rd_envoyer = []() { snprintf(rd_etat.message, sizeof rd_etat.message, TXT("seuils envoyés au radar", "thresholds sent")); printf("[radar] envoyer\n"); };
        toucher(200, 24); // 31/10 : onglet Radar
        lacher();
        avancer(300);
        capture("148_radar_page");
        toucher(34 + 6 * 22 + 11, 104 + 150); // zone 6
        lacher();
        toucher(UI_ZONE_L - UI_MARGE - 14 - 22, 336 + 36); // +
        lacher();
        toucher(UI_ZONE_L - UI_MARGE - 14 - 22, 336 + 36); // +
        lacher();
        toucher(343, 69); // Maintien
        lacher();
        avancer(200);
        capture("149_radar_maintien_zone6");
        toucher(200, 442); // Envoyer au radar
        lacher();
        avancer(200);
        capture("150_radar_envoye");
        printf("[radar] zone 6 : détection %d maintien %d\n", rd_etat.detection[6], rd_etat.maintien[6]);
        // 31/10 : onglet Réglages (gestion d'énergie) : coupure Wi-Fi activée, économie 20 s, marge +5
        rd_etat.marge = 5;
        toucher(301, 24);
        lacher();
        avancer(300);
        capture("151_batterie_reglages");
        toucher(200, 76 + 2 + 62 + 31); // coupure Wi-Fi
        lacher();
        toucher(200, 76 + 2 + 2 * 62 + 31); // économie : 10 -> 20 s
        lacher();
        avancer(300);
        capture("151b_batterie_reglages_coupure");
        // 26/10 : diag radar (1re ligne des Réglages) : lancé, puis 4 détections notées
        rd_diag = [](bool actif)
        {
            rd_etat.diag = actif;
            rd_etat.diag_fin = (uint32_t)time(nullptr) + 12 * 3600;
            rd_etat.diag_nb = 0;
        };
        toucher(200, 76 + 2 + 31);
        lacher();
        avancer(300);
        capture("152_batterie_diag_lance");
        rd_etat.diag_nb = 4;
        rd_etat.diag_fin = (uint32_t)time(nullptr) + 11 * 3600 + 40 * 60;
        page_batterie_maj();
        avancer(300);
        capture("153_batterie_diag_en_cours");
        toucher(99, 24); // retour à l'onglet Batterie : la carte du dernier réveil montre le diag
        lacher();
        avancer(300);
        capture("153b_batterie_onglet_diag");
        rd_etat.diag = false;
        page_batterie_maj();
    }
    bt_etat.charge = false;
    bt_etat.pourcent = 12;
    batterie_fictive(false);
    bt_etat.pourcent = 12;
    {
        int niveau;
        uint32_t couleur;
        bt_icone(niveau, couleur);
        static const lv_img_dsc_t *const PILES[] = {&icon_bat0, &icon_bat1, &icon_bat2, &icon_bat3};
        ui_shell_icone((int)(sizeof(pages) / sizeof(pages[0])) - 1, PILES[niveau], couleur);
        page_batterie_maj();
    }
    aller("Informations");
    avancer(300);
    avancer(300);
    capture("146_batterie_faible_rail"); // la pile (12 %, rouge) épinglée en bas du rail

    // 09/11 : GIF « rail animé » du README (docs/images/rail-anime.gif). Le carrousel tel qu'il bouge
    // sous le doigt : le VRAI geste du scénario ci-dessous (appui à (440, 380), 12 pas de 30 ms vers
    // (440, 40), cf. glisser()), image à chaque pas, puis images toutes les 50 ms pendant l'élan
    // (inertie LVGL 9.6 : LV_INDEV_DEF_SCROLL_THROW = 10, l'anim de lancer s'éteint en moins de 1,024 s,
    // lib/lvgl-9.6.0/src/indev/lv_indev.c:2029-2044), puis appui sur l'icône sous le pointeur du rail :
    // la page change d'un seul coup (le shell n'anime pas les pages, src/ui_lvgl/ui_shell.cpp:68-111).
    // L'état est reconstitué à la fin (rail en haut, page Informations) pour que la capture 147 reste
    // identique, octet à octet.
    {
        char nom[48];
        int t = 0;
        snprintf(nom, sizeof nom, "anim_rail_%03d", t);
        capture(nom);
        doigt_x = 440; doigt_y = 380; doigt_pose = true;
        avancer(80);
        t += 80;
        for (int i = 1; i <= 12; i++) // le geste de glisser(), pas à pas : 12 pas de 30 ms
        {
            doigt_y = 380 + (40 - 380) * i / 12;
            avancer(30);
            t += 30;
            snprintf(nom, sizeof nom, "anim_rail_%03d", t);
            capture(nom);
        }
        doigt_pose = false;
        for (int i = 1; i <= 18; i++) // élan : images toutes les 50 ms (900 ms de fenêtre)
        {
            avancer(50);
            t += 50;
            snprintf(nom, sizeof nom, "anim_rail_%03d", t);
            capture(nom);
        }
        // 13/11 (vitrine du README) : l'appui doit aboutir sur la page Imprimante 3D, plus sur IA. On vise
        // l'icône elle-même là où le carrousel s'est arrêté : enfant 12 du rail = pages[12] « Imprimante 3D »
        // (les pages épinglées ne sont pas dans le carrousel), coordonnées ÉCRAN (le scroll_y des enfants
        // ne bouge pas, pas leurs coords).
        lv_obj_t *rail = nullptr;
        for (uint32_t i = 0; !rail && i < lv_obj_get_child_cnt(lv_scr_act()); i++)
        {
            lv_obj_t *c = lv_obj_get_child(lv_scr_act(), i);
            if (lv_obj_get_x(c) == UI_ZONE_L && lv_obj_get_width(c) == UI_RAIL_L && lv_obj_get_y(c) == 0)
                rail = c;
        }
        lv_obj_t *cible = rail ? lv_obj_get_child(rail, 12) : nullptr;
        lv_area_t ac;
        if (cible)
            lv_obj_get_coords(cible, &ac);
        const int page_avant = ui_shell_page();
        if (cible)
        {
            printf("[anim-rail] icône Imprimante : x %d..%d y %d..%d\n", ac.x1, ac.x2, ac.y1, ac.y2);
            toucher((ac.x1 + ac.x2) / 2, (ac.y1 + ac.y2) / 2);
            lacher();
            t += 180;
            snprintf(nom, sizeof nom, "anim_rail_%03d", t);
            capture(nom);
            avancer(200);
            t += 200;
            snprintf(nom, sizeof nom, "anim_rail_%03d", t);
            capture(nom);
            printf("[anim-rail] clic icône : « %s » -> « %s »\n", ui_shell_nom(page_avant), ui_shell_nom(ui_shell_page()));
        }
        // 13/11 : petite animation caméra en bout de course : bouton « Caméra » de la page Imprimante
        // (vue caisson), puis les vraies images de la caméra du caisson (l'auteur, accord explicite)
        // pendant un park : 10 frames prises à 20 images/s, servies à leur cadence réelle — une capture
        // tous les 50 ms (le flux simulé est tiré ~2 fois par pas de 10 ms, la séquence avance d'une
        // frame tous les 10 tirages).
        if (cible && ui_shell_nom(ui_shell_page()) && !strcmp(ui_shell_nom(ui_shell_page()), "Imprimante 3D"))
        {
            toucher(359, 71); // bouton « Caméra »
            lacher();
            avancer(300);
            t += 300;
            snprintf(nom, sizeof nom, "anim_rail_%03d", t);
            capture(nom);
            snprintf(g_seq_cam, sizeof g_seq_cam, "park_caisson"); // donnees/cameras/park_caisson/NNN.jpg (vraie caméra, frames f116..f125)
            g_seq_i = 0;
            for (int i = 0; i < 10; i++)
            {
                avancer(50);
                t += 50;
                snprintf(nom, sizeof nom, "anim_rail_%03d", t);
                capture(nom);
            }
            g_seq_cam[0] = 0;
        }
        // état reconstitué : rail en haut (geste inverse, sans élastique il cale exactement en haut),
        // page Informations comme avant ce bloc
        glisser(440, 60, 440, 430);
        lacher();
        avancer(600);
        if (rail)
            printf("[anim-rail] remonté : scroll_y = %d\n", (int)lv_obj_get_scroll_y(rail));
        aller("Informations");
        avancer(300);
    }

    glisser(440, 380, 440, 40); // le carrousel défile au-dessus d'elle
    lacher();
    avancer(300);
    capture("147_batterie_rail_defile");
    // 26/10 : maquettes du mode batterie (écran du séjour)
    for (int c = 0; c < 2; c++)
    {
        lv_obj_t *m = maquette_page_batterie(c);
        avancer(200);
        capture(c ? "141_batterie_page_en_charge" : "140_batterie_page");
        lv_obj_del(m);
    }
    aller("Réglages");
    avancer(300);
    for (int o = 0; o < 2; o++)
    {
        lv_obj_t *m = maquette_radar(o, 3);
        avancer(200);
        capture(o ? "143_radar_maintien" : "142_radar_declenchement");
        lv_obj_del(m);
    }
    // 31/10 : maquettes de la page PC en 4 onglets (Système en 2 variantes, GPU = contenu de la VRAM)
    for (int v = 0; v < 2; v++)
    {
        lv_obj_t *m = maquette_pc_systeme(v);
        avancer(200);
        capture(v ? "161_maquette_pc_systeme_large" : "160_maquette_pc_systeme_btop");
        lv_obj_del(m);
    }
    {
        lv_obj_t *m = maquette_pc_gpu();
        avancer(200);
        capture("162_maquette_pc_gpu");
        lv_obj_del(m);
    }

    // 27/10 : l'objet d'une zone invalidée (le plus profond dont la surface couvre la zone) : type, texte d'une étiquette
    struct audit_t
    {
        static void chercher(lv_obj_t *o, const lv_area_t &z, lv_obj_t *&meilleur, long &taille)
        {
            lv_area_t c;
            lv_obj_get_coords(o, &c);
            const lv_coord_t m = 12; // marge : taille de dessin étendue (ombres, contours)
            if (c.x1 - m <= z.x1 && c.y1 - m <= z.y1 && c.x2 + m >= z.x2 && c.y2 + m >= z.y2 && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
            {
                const long t = lv_area_get_size(&c);
                if (t <= taille)
                {
                    meilleur = o;
                    taille = t;
                }
            }
            for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
                chercher(lv_obj_get_child(o, i), z, meilleur, taille);
        }
        static std::string decrire(const lv_area_t &z)
        {
            lv_obj_t *o = nullptr;
            long t = 1L << 40;
            chercher(lv_scr_act(), z, o, t);
            char b[160];
            if (!o)
                return "?";
            lv_area_t c;
            lv_obj_get_coords(o, &c);
            const char *genre = lv_obj_check_type(o, &lv_label_class) ? "étiquette" : lv_obj_check_type(o, &UI_CLASSE_IMAGE) ? "image"
                                : lv_obj_check_type(o, &lv_arc_class) ? "arc" : lv_obj_check_type(o, &lv_switch_class) ? "interrupteur"
                                : lv_obj_check_type(o, &UI_CLASSE_BOUTON) ? "bouton" : lv_obj_get_child_cnt(o) ? "conteneur" : "objet";
            snprintf(b, sizeof b, "%s %dx%d@%d,%d%s%s%s", genre, (int)lv_area_get_width(&c), (int)lv_area_get_height(&c), (int)c.x1,
                     (int)c.y1, lv_obj_check_type(o, &lv_label_class) ? " \"" : "",
                     lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "", lv_obj_check_type(o, &lv_label_class) ? "\"" : "");
            return b;
        }
    };
    // ---- 27/10 : AUDIT DES REDESSINS INUTILES (retour d'usage : « MQTT actualise même ce qui ne change pas d'état », puis
    // « vérifie toutes les pages ») : chaque page, CHAQUE ONGLET (barre d'onglets trouvée à l'écran, onglets cliqués un à
    // un), les 5 pièces du thermostat et l'aperçu d'un fichier de l'imprimante. Sur chaque vue, affichage stable, chaque
    // dernier message est envoyé DEUX FOIS de suite ; juste après le second (avant que LVGL dessine), la surface qu'il a
    // marquée à redessiner : tout pixel marqué est un redessin inutile (l'état affiché est déjà celui du message).
    {
        FILE *f = fopen("captures/audit_redessins.tsv", "w");
        fprintf(f, "vue\tsujet\tpixels\tzones\tobjets\torigine\n");
#if LVGL_VERSION_MAJOR >= 9
        // les aires marquées à redessiner (inv_areas / inv_area_joined / inv_p) sont passées dans l'en-tête
        // PRIVÉ du display sous 9.6 (src/display/lv_display_private.h:130-133) : le simulateur est un outil de
        // mesure, il a le droit de les lire ; c'est la même source que le px_num du moniteur 8.4.
        lv_display_t *disp = lv_display_get_default();
#else
        lv_disp_t *disp = lv_disp_get_default();
#endif
        int nb_vues = 0;
        auto audit_vue = [&](const std::string &vue)
        {
            nb_vues++;
            for (auto &d : derniers)
            {
                ui_mqtt_recevoir(d.first.c_str(), (const uint8_t *)d.second.c_str(), d.second.size());
                avancer(40);
                lv_refr_now(NULL);
#ifdef AUDIT_PILE
                g_piles.clear();
                g_audit_pile = true;
#endif
                ui_mqtt_recevoir(d.first.c_str(), (const uint8_t *)d.second.c_str(), d.second.size());
#ifdef AUDIT_PILE
                g_audit_pile = false;
                const std::string origine = origines();
#else
                const std::string origine;
#endif
                long px = 0;
                int zones = 0;
                std::string objets;
                for (int k = 0; k < disp->inv_p; k++)
                    if (!disp->inv_area_joined[k])
                    {
                        px += lv_area_get_size(&disp->inv_areas[k]);
                        zones++;
                        objets += (objets.empty() ? "" : " | ") + audit_t::decrire(disp->inv_areas[k]);
                    }
                if (px)
                    fprintf(f, "%s\t%s\t%ld\t%d\t%s\t%s\n", vue.c_str(), d.first.c_str(), px, zones, objets.c_str(), origine.c_str());
            }
            avancer(200);
        };
        // la barre d'onglets (ui_onglets) : à y = 4, 40 px de haut, des boutons pour enfants
        struct onglets_t
        {
            static lv_obj_t *trouver(lv_obj_t *o)
            {
                lv_area_t c;
                lv_obj_get_coords(o, &c);
                if (c.y1 == 4 && lv_area_get_height(&c) == 40 && lv_obj_get_child_cnt(o) >= 2 &&
                    lv_obj_check_type(lv_obj_get_child(o, 0), &UI_CLASSE_BOUTON))
                    return o;
                for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
                    if (lv_obj_t *t = trouver(lv_obj_get_child(o, i)))
                        return t;
                return nullptr;
            }
            // un bouton par sa position à l'écran
            static lv_obj_t *bouton_a(lv_obj_t *o, int x, int y)
            {
                lv_area_t c;
                lv_obj_get_coords(o, &c);
                if (lv_obj_check_type(o, &UI_CLASSE_BOUTON) && c.x1 <= x && x <= c.x2 && c.y1 <= y && y <= c.y2)
                    return o;
                for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
                    if (lv_obj_t *t = bouton_a(lv_obj_get_child(o, i), x, y))
                        return t;
                return nullptr;
            }
        };
        const int nb_pages = (int)(sizeof(pages) / sizeof(pages[0]));
        for (int i = 0; i < nb_pages; i++)
        {
            ui_shell_aller(i);
            avancer(1500);
            const std::string nom = pages[i].nom;
            lv_obj_t *barre = onglets_t::trouver(lv_scr_act());
            const int n = barre ? (int)lv_obj_get_child_cnt(barre) : 1;
            for (int v = 0; v < n; v++)
            {
                std::string vue = nom;
                if (barre)
                {
                    barre = onglets_t::trouver(lv_scr_act()); // la page a pu se reconstruire
                    lv_obj_t *b = barre ? lv_obj_get_child(barre, v) : nullptr;
                    if (!b)
                        break;
                    vue += std::string(" / ") + lv_label_get_text(lv_obj_get_child(b, 0));
                    envoyer_clic(b);
                    avancer(800);
                }
                audit_vue(vue);
                if (nom == "Imprimante 3D" && vue.find("Fichiers") != std::string::npos)
                {
                    if (lv_obj_t *fi = onglets_t::bouton_a(lv_scr_act(), 200, 82)) // 1re ligne : l'aperçu
                    {
                        envoyer_clic(fi);
                        avancer(800);
                        audit_vue(nom + " / Fichiers / aperçu");
                    }
                }
            }
            if (nom == "Chauffage")
                for (int k = 1; k < TH_NB_PIECES; k++)
                    if (lv_obj_t *suiv = onglets_t::bouton_a(lv_scr_act(), UI_ZONE_L - 30, 20))
                    {
                        envoyer_clic(suiv);
                        avancer(800);
                        audit_vue(nom + " / pièce " + th_pieces[th_piece_affichee].nom);
                    }
        }
        fclose(f);
        printf("[audit] %d sujets rejoués sur %d vues : captures/audit_redessins.tsv\n", (int)derniers.size(), nb_vues);
    }
#endif
    return 0;
}
