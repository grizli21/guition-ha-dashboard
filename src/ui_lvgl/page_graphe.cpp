#include "page_graphe.h"
#include "ui_theme.h"
#include "ui_diag.h"
#include "ui_courbe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
using std::max;
using std::min;

gr_historique_t *gr_pieces = nullptr;
int16_t *gr_ext = nullptr;

// 9.6 : lv_mem_alloc a disparu, l'équivalent public est lv_malloc (stdlib/lv_mem.h) — même alloc, toujours ps_malloc
// côté ESP (le gestionnaire de tas 9.6 est réglé par la conf).
#if LVGL_VERSION_MAJOR >= 9
#define GR_ALLOC(sz) lv_malloc(sz)
#else
#define GR_ALLOC(sz) lv_mem_alloc(sz)
#endif

static bool g_gr_echec_signale = false; // 06/11 (audit 9.6) : l'échec d'allocation ne se dit qu'une fois

void page_graphe_preparer()
{
    if (gr_pieces && gr_ext)
        return;
    if (!gr_pieces)
        gr_pieces = (gr_historique_t *)GR_ALLOC(sizeof(gr_historique_t) * TH_NB_PIECES); // lv_mem_alloc = ps_malloc
    if (!gr_ext)
        gr_ext = (int16_t *)GR_ALLOC(sizeof(int16_t) * GR_POINTS);
    if (!gr_pieces || !gr_ext)
    {
        // 06/11 (audit 9.6, chantier 3) : ces deux allocations n'étaient pas vérifiées et le remplissage
        // qui suit écrivait immédiatement à travers le nul — la famille exacte du dépassement de tampon
        // déjà réparé (72210b8 : 4 o/px écrits dans un tampon de 3 o/px). En cas d'échec : RIEN n'est
        // écrit (les dessins, rafraichir() et les récepteurs MQTT testent les pointeurs et s'abstiennent),
        // on le dit UNE fois dans les traces, et l'appel suivant retentera (la mémoire peut se libérer).
        if (!g_gr_echec_signale)
        {
            g_gr_echec_signale = true;
            ui_trace("[graphe] allocation historique ÉCHOUÉE (%d o + %d o) : courbes et étiquettes non dessinées\n",
                     (int)(sizeof(gr_historique_t) * TH_NB_PIECES), (int)(sizeof(int16_t) * GR_POINTS));
        }
        return;
    }
    for (int k = 0; k < TH_NB_PIECES; k++) // rien de mesuré tant que HA n'a rien envoyé
    {
        gr_historique_t &h = gr_pieces[k];
        for (int i = 0; i < GR_POINTS; i++)
            h.temp[i] = h.cons[i] = GR_VIDE, h.hum[i] = 0;
        memset(h.chauffe, 0, sizeof h.chauffe);
    }
    for (int i = 0; i < GR_POINTS; i++)
        gr_ext[i] = GR_VIDE;
}
uint16_t gr_minute_maintenant = 0;

// --- Géométrie (coordonnées dans la zone de page 404×480) ; 24/10 : SANS carte (retour d'usage : « plus propre avec le fondu
// du graph, et le graph occupe plus d'espace ») ; la courbe du poêle est posée de la même façon ---
#define G_Y 122         // haut de l'objet graphe
#define G_H 220         // hauteur de l'objet graphe
#define T_X0 44         // début de la courbe (laisse la place aux graduations « 20° »)
#define T_L 352         // largeur de la courbe (fin à 396, comme le poêle)
#define T_Y0 10         // marge haute dans l'objet graphe
#define T_H 186         // hauteur de la courbe
#define H_Y 348         // bande humidité
#define H_H 52
#define C_EXT UC_EXTERIEUR
#define C_CONS UI_TEXTE // consigne
// 25/10 (retour d'usage : la courbe de la pièce et la chauffe trop proches, ambre / orange) : chauffe en rouge, sur ce graphe seulement
#define C_CHAUFFE 0xE5484D

enum
{
    COUCHE_CONS,
    COUCHE_EXT,
    COUCHE_CHAUFFE,
    COUCHE_HUM,
    NB_COUCHES
};
static bool g_couches[NB_COUCHES] = {true, true, true, true};
static int g_piece = 0;
static int g_scrub = -1; // point lu au doigt (-1 = maintenant)

// --- Calque statique du contenu figé (07/11, retour d'usage : « ok pour le patch statique ») -------------
// Grille, heures, bande de chauffe, dégradé, extérieur, consigne, courbe et fond de page sont figés
// UNE fois dans une image PSRAM par lv_snapshot_take (faculté de la librairie, déjà employée par
// ui_camera.cpp:34-46), recopiée telle quelle à chaque repeinte ; le pas de doigt ne redessine que le
// curseur par-dessus. Patron de l'anneau (page_thermostat.cpp:265-314) : connaître ce qui est déjà
// affiché, ne refaire que sur changement ou état inconnu. Correctif annulable : supprimer ce bloc et
// les quelques appels marqués « calque » revient au chemin complet d'hier.
// Format : prise de vue RGB565 (lv_snapshot.c:96-106 n'accepte pas RGB565_SWAPPED), puis UNE passe
// lv_draw_rgb565_swap et header.cf posé à RGB565_SWAPPED — la recopie vers la dalle (déjà
// RGB565_SWAPPED, b44b4c3) devient un memcpy pur par ligne (lv_draw_sw_blend_to_rgb565_swapped.c,
// rgb565_swapped_image_blend) au lieu d'un échange par pixel à chaque pas. Chiffres au journal.
static lv_draw_buf_t *g_calque = nullptr;   // image figée de o_graph (404 × 220)
static lv_draw_buf_t *g_calque_h = nullptr; // image figée de o_hum (404 × 52)
static bool g_calque_sale = true;           // patron anneau : état inconnu ou changé -> à refaire
static bool g_prise_vue = false;            // prise de vue en cours : dessin SANS curseur (voir les deux callbacks)
static bool g_calque_echec_signale = false;

static lv_obj_t *o_points[TH_NB_PIECES], *o_nom, *o_val, *o_min, *o_max, *o_graph, *o_hum, *o_hum_txt, *o_bulle, *o_bulle_txt;
static lv_obj_t *o_couches[NB_COUCHES];

static void fmt1(char *buf, size_t n, int16_t v10)
{
    snprintf(buf, n, "%d,%d", v10 / 10, abs(v10 % 10));
    if (v10 < 0 && v10 > -10)
        snprintf(buf, n, "-0,%d", -v10);
}

// 07/11 (retour d'usage : « la ligne Consigne affiche une valeur absurde ») : la bulle imprimait la SENTINELLE telle
// quelle. « Consigne -3276,8° » lu sur la capture `.banc_v9_esp/calque/gf_tenu_10.png` = GR_VIDE (INT16_MIN)
// passé dans fmt1, qui fait v10/10 puis abs(v10%10). La donnée, elle, dit « pas de consigne » : HA publie -999
// quand le climate est en arrêt ou sans consigne — mesuré dans le stockage de reprise de HA
// (le « restore_state » interne de HA, .storage/core.restore_state) : 287 des 288 points de consigne de la Chambre 1 valent -999,
// un seul à 80 = 8,0° — et ui_mqtt.cpp:1210 en fait GR_VIDE. Les courbes ignorent déjà la sentinelle
// (uc_serie(..., GR_VIDE, ...)) ; la bulle, non. Pareil pour la température de pièce et l'extérieur. Sans valeur
// affichable -> un tiret, SANS degré (le tiret haut « — » n'est pas dans la police d'écran : ceux du dépôt ne
// sont que dans les traces console). Le texte était identique en 8.4
// (git show 35b09f7:src/ui_lvgl/page_graphe.cpp:210-214) : défaut antérieur au portage 9.6.
static void fmt1_aff(char *buf, size_t n, int16_t v10, int16_t bas, int16_t haut)
{
    char v[12];
    if (v10 == GR_VIDE || v10 < bas || v10 > haut) // bornes en dixièmes de °C : 0..40° ici, -20..40° dehors
        snprintf(buf, n, "-");
    else
    {
        fmt1(v, sizeof v, v10);
        snprintf(buf, n, "%s°", v);
    }
}

static int point_courant() { return g_scrub >= 0 ? g_scrub : GR_POINTS - 1; }
static void texte_heure(char *buf, size_t n, int i) { uc_texte_heure(buf, n, i, GR_POINTS - 1, gr_minute_maintenant, 5); }

// --- Échelle °C commune aux couches visibles ---
static void echelle(const gr_historique_t &h, int &lo, int &hi)
{
    int mn = INT16_MAX, mx = INT16_MIN;
    for (int i = 0; i < GR_POINTS; i++)
    {
        int16_t v[3] = {h.temp[i], g_couches[COUCHE_CONS] ? h.cons[i] : GR_VIDE, g_couches[COUCHE_EXT] ? gr_ext[i] : GR_VIDE};
        for (int k = 0; k < 3; k++)
            if (v[k] != GR_VIDE)
            {
                mn = min(mn, (int)v[k]);
                mx = max(mx, (int)v[k]);
            }
    }
    if (mn > mx)
        mn = 150, mx = 200;
    lo = (int)floorf(mn / 10.0f - 0.5f);
    hi = (int)ceilf(mx / 10.0f + 0.5f);
    if (hi <= lo)
        hi = lo + 1;
}

// --- Dessin du graphe °C (module commun ui_courbe) ---
static uc_cadre_t cadre(uc_cible_t *ctx, lv_obj_t *o, lv_coord_t y0, lv_coord_t h)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    uc_cadre_t c = {ctx, (lv_coord_t)(a.x1 + T_X0), (lv_coord_t)(a.x1 + T_X0 + T_L), (lv_coord_t)(a.y1 + y0), (lv_coord_t)(a.y1 + y0 + h),
                    0, GR_POINTS, 0, 1};
    return c;
}

static bool chauffe(int i) { return gr_chauffe(gr_pieces[g_piece], i); }

// Libère l'image figée ; l'état redevient « inconnu » (patron de l'anneau : g_affiche_connu faux).
static void calque_defaire()
{
    if (g_calque)
        lv_draw_buf_destroy(g_calque); // le tampon vient de lv_malloc -> notre tas PSRAM (ui_mem_v9.cpp)
    if (g_calque_h)
        lv_draw_buf_destroy(g_calque_h);
    g_calque = g_calque_h = nullptr;
    g_calque_sale = true;
}

// Photo d'une partie figée, déjà dans l'ordre d'octets de la dalle (voir le bloc plus haut).
// Sans le curseur : g_prise_vue est posé par le preneur de vue, les deux callbacks de dessin le testent.
static lv_draw_buf_t *calque_capturer(lv_obj_t *o)
{
    if (!o || lv_obj_is_hidden(o))
        return nullptr; // couche masquée : rien à figer (et lv_snapshot ignore un objet caché)
    if (lv_obj_get_width(o) <= 0 || lv_obj_get_height(o) <= 0)
        return nullptr; // layout pas encore posé (sortie de page à l'instant) : on retentera au prochain rafraichir
    lv_draw_buf_t *b = lv_snapshot_take(o, LV_COLOR_FORMAT_RGB565);
    if (!b)
        return nullptr; // mémoire manquante : chemin complet en attendant
    if (b->header.w != (uint32_t)lv_obj_get_width(o) || b->header.h != (uint32_t)lv_obj_get_height(o))
    {
        // coordonnées pas encore posées (page à peine créée) ou objet redimensionné entre-temps :
        // la photo ne correspondrait pas à l'objet, on la jette et on retentera au prochain rafraichir.
        lv_draw_buf_destroy(b);
        return nullptr;
    }
    lv_draw_rgb565_swap(b->data, b->data_size / 2); // une seule fois, à la prise de vue
    b->header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;  // déclaration du format échangé : recopie = memcpy
    return b;
}

// Refonte le calque des deux objets. Appelée HORS chemin de dessin (rafraichir) : lv_snapshot_take
// redessine l'objet lui-même, le rappeler depuis le callback de dessin serait récursif.
static void calque_refaire()
{
    calque_defaire();
    g_prise_vue = true; // le curseur est dynamique : il ne doit pas rester cuit dans l'image
    g_calque = calque_capturer(o_graph);
    g_calque_h = calque_capturer(o_hum);
    g_prise_vue = false;
    if (g_calque)
        g_calque_sale = false; // image en place (o_hum peut manquer : sa couche est peut-être masquée)
    else if (!g_calque_echec_signale && lv_obj_get_width(o_graph) > 0)
    {
        g_calque_echec_signale = true;
        ui_trace("[graphe] capture du calque ÉCHOUÉE : chemin complet conservé, reprise à la prochaine donnée\n");
    }
    if (g_calque)
        g_calque_echec_signale = false;
}

static void graphe_dessin_cb(lv_event_t *e)
{
    if (!gr_pieces || !gr_ext)
        return; // allocation échouée (voir page_graphe_preparer) : ne rien dessiner
    const gr_historique_t &h = gr_pieces[g_piece];
    int lo, hi;
    echelle(h, lo, hi);
    uc_cible_t *ctx = uc_cible_du(e);
    lv_obj_t *o = uc_objet_du(e);
    // Chemin léger : recopie de l'image figée (memcpy pur) + curseur seul. Si la taille de l'objet ne
    // correspond plus à l'image (garde défensive contre un redimensionnement), chemin complet et
    // image marquée périmée pour la reprise au prochain rafraichir.
    if (g_calque && !g_prise_vue)
    {
        if (g_calque->header.w == (uint32_t)lv_obj_get_width(o) && g_calque->header.h == (uint32_t)lv_obj_get_height(o))
        {
            lv_draw_image_dsc_t d;
            lv_draw_image_dsc_init(&d);
            d.src = g_calque; // lv_draw_buf_t est interchangeable avec lv_image_dsc_t (lv_draw_buf.h:351-352)
            lv_area_t z;
            lv_obj_get_coords(o, &z);
            lv_draw_image(ctx, &d, &z);
            if (g_scrub >= 0)
            {
                uc_cadre_t cc = cadre(ctx, o, T_Y0, T_H);
                cc.lo = lo;
                cc.hi = hi;
                uc_curseur(cc, g_scrub, uc_serie(h.temp, GR_VIDE, 10), UI_ACCENT);
            }
            return;
        }
        g_calque_sale = true; // taille différente de l'image : image périmée
    }
    uc_cadre_t c = cadre(ctx, o, T_Y0, T_H);
    c.lo = lo;
    c.hi = hi;
    // 25/10 (retour d'usage : « page graphe lente, quel est le consommateur ? ») : durée de chaque étape, tracée au-delà de 20 ms
    // (appelé une fois par bande de 80 lignes redessinée)
    uint32_t t0 = lv_tick_get(), t[6];
    uc_grille(c, hi - lo > 10 ? 4 : 2, "°");
    uc_heures(c, gr_minute_maintenant, 5, 360); // repères 00h, 06h, 12h, 18h (ils glissent avec le temps)
    t[0] = lv_tick_get();
    if (g_couches[COUCHE_CHAUFFE])
        uc_bandes(c, chauffe, C_CHAUFFE);
    t[1] = lv_tick_get();
    const uc_serie_t temp = uc_serie(h.temp, GR_VIDE, 10);
    uc_degrade(c, temp, UI_ACCENT);
    t[2] = lv_tick_get();
    if (g_couches[COUCHE_EXT])
        uc_trait(c, uc_serie(gr_ext, GR_VIDE, 10), C_EXT, 2);
    t[3] = lv_tick_get();
    if (g_couches[COUCHE_CONS])
        uc_escalier(c, uc_serie(h.cons, GR_VIDE, 10), C_CONS);
    t[4] = lv_tick_get();
    uc_principale(c, temp, UI_ACCENT);
    if (g_scrub >= 0 && !g_prise_vue) // prise de vue du calque : le curseur reste hors de l'image
        uc_curseur(c, g_scrub, temp, UI_ACCENT);
    t[5] = lv_tick_get();
    if (t[5] - t0 > 50) // 25/10 : 50 ms (après l'optimisation, ~25 ms la bande la plus chargée)
        ui_trace("[graphe] bande y %d-%d : %u ms = grille %u, chauffe %u, dégradé %u, extérieur %u, consigne %u, courbe %u\n",
                 uc_clip(c.ctx)->y1, uc_clip(c.ctx)->y2, (unsigned)(t[5] - t0), (unsigned)(t[0] - t0), (unsigned)(t[1] - t[0]),
                 (unsigned)(t[2] - t[1]), (unsigned)(t[3] - t[2]), (unsigned)(t[4] - t[3]), (unsigned)(t[5] - t[4]));
}

// --- Bande humidité (sa propre échelle) ---
static void hum_dessin_cb(lv_event_t *e)
{
    if (!gr_pieces)
        return; // allocation échouée : ne rien dessiner
    const gr_historique_t &h = gr_pieces[g_piece];
    int mn = 100, mx = 0;
    for (int i = 0; i < GR_POINTS; i++)
        if (h.hum[i])
            mn = min(mn, (int)h.hum[i]), mx = max(mx, (int)h.hum[i]);
    uc_cible_t *ctx = uc_cible_du(e);
    lv_obj_t *o = uc_objet_du(e);
    // Chemin léger : recopie de l'image figée de la bande + curseur seul (garde de taille comme plus haut).
    if (g_calque_h && !g_prise_vue && g_calque_h->header.w == lv_obj_get_width(o) && g_calque_h->header.h == lv_obj_get_height(o))
    {
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = g_calque_h;
        lv_area_t z;
        lv_obj_get_coords(o, &z);
        lv_draw_image(ctx, &d, &z);
        if (g_scrub >= 0 && mn <= mx) // pas de curseur sans données : comme le chemin complet (retour plus bas)
        {
            uc_cadre_t cc = cadre(ctx, o, 10, H_H - 12);
            cc.lo = mn - 2;
            cc.hi = mx + 2;
            uc_curseur(cc, g_scrub, uc_serie(h.hum, 0), UI_HUMIDITE);
        }
        return;
    }
    if (mn > mx)
        return;
    uc_cadre_t c = cadre(ctx, o, 10, H_H - 12);
    c.lo = mn - 2;
    c.hi = mx + 2;
    const uint32_t t0 = lv_tick_get();
    uc_degrade(c, uc_serie(h.hum, 0), UI_HUMIDITE); // dégradé comme la courbe principale (24/10)
    const uint32_t t1 = lv_tick_get();
    uc_trait(c, uc_serie(h.hum, 0), UI_HUMIDITE, 2);
    if (lv_tick_elaps(t0) > 50)
        ui_trace("[graphe] humidité : %u ms = dégradé %u, trait %u\n", (unsigned)lv_tick_elaps(t0), (unsigned)(t1 - t0), (unsigned)lv_tick_elaps(t1));
    if (g_scrub >= 0 && !g_prise_vue) // prise de vue du calque : le curseur reste hors de l'image
        uc_curseur(c, g_scrub, uc_serie(h.hum, 0), UI_HUMIDITE);
}

static void rafraichir()
{
    if (!o_nom)
        return;
    if (!gr_pieces || !gr_ext)
        return; // allocation échouée : pas d'étiquette non plus (le message est au preparer)
    const gr_historique_t &h = gr_pieces[g_piece];
    char v[12], t[32], b[160];
    int i = point_courant();
    while (i > 0 && h.temp[i] == GR_VIDE)
        i--;

    ui_label_texte(o_nom, th_pieces[g_piece].nom);
    for (int k = 0; k < TH_NB_PIECES; k++) // les 5 points de la page Chauffage : la pièce montrée
        ui_style_bg_color(o_points[k], ui_c(k == g_piece ? UI_TEXTE : UI_ETEINT), 0);
    fmt1_aff(v, sizeof v, h.temp[i], 0, 400); // valeur de tête : tiret si la sentinelle ou une invraisemblance
    ui_label_texte(o_val, v);
    ui_style_text_color(o_val, ui_c(g_scrub >= 0 ? UI_ACCENT : UI_TEXTE), 0);
    // 25/10 (retour d'usage : « doublon de l'heure ») : plus d'heure à côté de la valeur pendant la lecture au doigt, la bulle la donne

    int imin = -1, imax = -1;
    for (int k = 0; k < GR_POINTS; k++)
        if (h.temp[k] != GR_VIDE)
        {
            if (imin < 0 || h.temp[k] < h.temp[imin])
                imin = k;
            if (imax < 0 || h.temp[k] > h.temp[imax])
                imax = k;
        }
    if (imin >= 0)
    {
        fmt1(v, sizeof v, h.temp[imin]);
        texte_heure(t, sizeof t, imin);
        ui_label_texte_fmt(o_min, "min %s° à %s", v, t);
        fmt1(v, sizeof v, h.temp[imax]);
        texte_heure(t, sizeof t, imax);
        ui_label_texte_fmt(o_max, "max %s° à %s", v, t);
    }

    ui_label_texte_fmt(o_hum_txt, "Humidité %d %%", h.hum[i]);

    // Bulle pendant la lecture au doigt
    if (g_scrub >= 0)
    {
        char vt[12], vc[12], ve[12];
        fmt1_aff(vt, sizeof vt, h.temp[i], 0, 400);
        fmt1_aff(vc, sizeof vc, h.cons[i], 0, 400);
        fmt1_aff(ve, sizeof ve, gr_ext[i], -200, 400);
        texte_heure(t, sizeof t, i);
        // le degré est porté par la valeur elle-même (fmt1_aff) : une valeur vide s'affiche « - », sans degré
        snprintf(b, sizeof b, "%s\n#FFB547 Pièce %s%s#\nConsigne %s\n#7CC4F2 Extérieur %s#", t, vt,
                 gr_chauffe(h, i) ? " (chauffe)" : "", vc, ve);
        ui_label_texte(o_bulle_txt, b);
        lv_area_t z;
        lv_obj_get_coords(lv_obj_get_parent(o_bulle), &z);
        uc_bulle_placer(o_bulle, cadre(nullptr, o_graph, T_Y0, T_H).x(i) - z.x1, G_Y + 4, UI_ZONE_L);
    }
    else
        ui_cacher(o_bulle, true);

    for (int c = 0; c < NB_COUCHES; c++)
        uc_pastille_etat(o_couches[c], g_couches[c]);
    ui_cacher(o_hum, !g_couches[COUCHE_HUM]);
    ui_cacher(o_hum_txt, !g_couches[COUCHE_HUM]);

    // Calque (patron de l'anneau) : on ne refait l'image figée que si la partie figée a changé
    // (drapeau « sale » posé par la donnée MQTT, le changement de pièce, de couche, l'entrée de page)
    // ou si aucune image n'est en place. La capture se passe ICI, jamais dans le callback de dessin.
    if (g_calque_sale || !g_calque)
        calque_refaire();

    lv_obj_invalidate(o_graph);
    lv_obj_invalidate(o_hum);
}

void page_graphe_maj()
{
    g_calque_sale = true; // donnée MQTT : courbe/chauffe/heures nouvelles -> l'image figée est périmée
    rafraichir();
}

// 06/11 correctif C (a) — glissé horizontal de lecture (retour d'usage : « glissé horizontal dans le graph »).
// Mesure baseline du même jour (traces psram/traces_C_baseline_0611.txt) : rafraichir() invalide
// o_graph + o_hum EN ENTIER, donc chaque pas de doigt repeint 119 086 px en 103 ms (~9,2 images/s)
// alors que le geste ne déplace que : le curseur (trait de 1 px + pastille de 13 px, ui_courbe.cpp
// uc_curseur — dessiné dans le graphe ET dans la bande humidité, lignes 143-144 et 171-172 ci-dessus),
// la bulle (déplacée), la valeur o_val et le texte d'humidité. Ce chemin léger ne repeint que les
// deux colonnes du curseur (avant/après) sur chaque objet, plus l'ancienne place de la bulle ;
// lv_obj_set_pos n'invalidant que la NOUVELLE position en 9.6 (lv_obj_pos.c:68-90), c'est ici qu'on
// efface l'ancienne. Les autres éléments (nom, pastilles de couches, min/max, points de pièce) ne
// bougent pas pendant le glissé : leur mise à jour reste dans rafraichir() (appui, relâcher, données).
static void curseur_repeindre(int i_avant, int i_apres)
{
    auto bande = [](lv_obj_t *o, const uc_cadre_t &c, int i)
    {
        if (i < 0)
            return;
        const lv_coord_t px = c.x(i);
        lv_area_t z = {(lv_coord_t)(px - 8), c.haut, (lv_coord_t)(px + 8), c.bas}; // ±8 : trait 1 px + pastille rayon 6
        lv_obj_invalidate_area(o, &z);
    };
    const uc_cadre_t cg = cadre(nullptr, o_graph, T_Y0, T_H);
    bande(o_graph, cg, i_avant);
    bande(o_graph, cg, i_apres);
    const uc_cadre_t ch = cadre(nullptr, o_hum, 10, H_H - 12); // même cadre que hum_dessin_cb (ligne ci-dessus)
    bande(o_hum, ch, i_avant);
    bande(o_hum, ch, i_apres);
}

static void rafraichir_scrub(int i_avant)
{
    if (!o_nom)
        return;
    if (!gr_pieces || !gr_ext)
        return; // allocation échouée : ne rien dessiner
    const gr_historique_t &h = gr_pieces[g_piece];
    int i = point_courant();
    while (i > 0 && h.temp[i] == GR_VIDE)
        i--;
    char v[12], t[32], b[160];
    fmt1_aff(v, sizeof v, h.temp[i], 0, 400); // valeur de tête : tiret si la sentinelle ou une invraisemblance
    ui_label_texte(o_val, v);
    ui_style_text_color(o_val, ui_c(g_scrub >= 0 ? UI_ACCENT : UI_TEXTE), 0);
    ui_label_texte_fmt(o_hum_txt, "Humidité %d %%", h.hum[i]);
    if (g_scrub >= 0)
    {
        char vt[12], vc[12], ve[12];
        fmt1_aff(vt, sizeof vt, h.temp[i], 0, 400);
        fmt1_aff(vc, sizeof vc, h.cons[i], 0, 400);
        fmt1_aff(ve, sizeof ve, gr_ext[i], -200, 400);
        texte_heure(t, sizeof t, i);
        // le degré est porté par la valeur elle-même (fmt1_aff) : une valeur vide s'affiche « - », sans degré
        snprintf(b, sizeof b, "%s\n#FFB547 Pièce %s%s#\nConsigne %s\n#7CC4F2 Extérieur %s#", t, vt,
                 gr_chauffe(h, i) ? " (chauffe)" : "", vc, ve);
        lv_area_t avant_bulle;
        lv_obj_get_coords(o_bulle, &avant_bulle);
        const bool etait_visible = !lv_obj_has_flag(o_bulle, LV_OBJ_FLAG_HIDDEN);
        ui_label_texte(o_bulle_txt, b);
        lv_area_t z;
        lv_obj_get_coords(lv_obj_get_parent(o_bulle), &z);
        uc_bulle_placer(o_bulle, cadre(nullptr, o_graph, T_Y0, T_H).x(i) - z.x1, G_Y + 4, UI_ZONE_L);
        if (etait_visible)
            lv_obj_invalidate_area(o_graph, &avant_bulle); // repeindre le graphe sous l'ancienne place de la bulle
    }
    else
        ui_cacher(o_bulle, true);
    curseur_repeindre(i_avant, g_scrub);
}

// 06/11 (mesure du glissé horizontal, retour d'usage : « glissé horizontal dans le graph ») : un pas du doigt
// sur la courbe, sans indev ni commande vers HA. Emprunte le chemin du doigt (graphe_cb, PRESSED /
// PRESSING : point du doigt -> g_scrub -> rafraichir) avec la MÊME correspondance x -> point que
// uc_point_du_doigt (ui_courbe.cpp:399-405), x fourni par le banc — seul écart forcé par le verrou
// de geste : uc_point_du_doigt lit lv_indev_get_act() et ne marche que dans un événement de doigt.
// rafraichir() fait tout ce que fait le doigt : valeur courante, min/max, humidité, bulle repositionnée,
// points de pièce, pastilles, puis lv_obj_invalidate(o_graph) + lv_obj_invalidate(o_hum) EN ENTIER.
void page_graphe_banc_scrub(int x_doigt)
{
    if (!o_graph)
        return;
    if (x_doigt < 0) // relâcher, comme LV_EVENT_RELEASED
    {
        if (g_scrub != -1)
        {
            g_scrub = -1;
            rafraichir();
        }
        return;
    }
    const uc_cadre_t c = cadre(nullptr, o_graph, T_Y0, T_H);
    const int i = c.premier + (int)lroundf((float)(x_doigt - c.x0) * (c.n - 1) / (c.x1 - c.x0));
    const int borne = LV_CLAMP(c.premier, i, c.premier + c.n - 1);
    if (borne != g_scrub)
    {
        const int avant = g_scrub;
        g_scrub = borne;
        // comme le doigt : premier pas = PRESSED (chemin complet), ensuite PRESSING (chemin léger si le
        // correctif C (a) est actif) — la mesure du banc suit donc exactement le chemin du geste réel.
        if (avant >= 0)
            rafraichir_scrub(avant);
        else
            rafraichir();
    }
}

// --- Événements ---
static void piece_cb(lv_event_t *e)
{
    g_piece = (g_piece + TH_NB_PIECES + (int)(intptr_t)lv_event_get_user_data(e)) % TH_NB_PIECES;
    g_scrub = -1;
    g_calque_sale = true; // la courbe figée change de pièce : image périmée
    rafraichir();
}

static void couche_cb(lv_event_t *e)
{
    int c = (int)(intptr_t)lv_event_get_user_data(e);
    g_couches[c] = !g_couches[c];
    g_calque_sale = true; // consigne/extérieur/chauffe/humidité affichées ou retirées : image périmée
    rafraichir();
}

static void graphe_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING)
    {
        const int i = uc_point_du_doigt(cadre(nullptr, o_graph, T_Y0, T_H));
        if (i != g_scrub)
        {
            const int avant = g_scrub;
            g_scrub = i;
            // 06/11 correctif C (a) : pendant le glissé (PRESSING, curseur déjà posé), chemin léger —
            // colonnes du curseur + bulle + valeur, pas l'objet entier (mesure : 103 ms / 119 086 px par pas).
            if (code == LV_EVENT_PRESSING && avant >= 0)
                rafraichir_scrub(avant);
            else
                rafraichir();
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        g_scrub = -1;
        rafraichir();
    }
}

// --- Construction ---
static lv_obj_t *fleche(lv_obj_t *zone, const char *sym, lv_coord_t x, int sens)
{
    lv_obj_t *b = ui_bouton(zone, 56, 44);
    ui_style_bg_opa(b, LV_OPA_TRANSP, 0);
    ui_style_border_width(b, 0, 0);
    lv_obj_set_pos(b, x, 0);
    lv_obj_center(ui_label(b, UI_POLICE_CHEVRONS, UI_TEXTE2, sym)); // 31/10 : comme le thermostat (36, 27/10)
    lv_obj_add_event_cb(b, piece_cb, LV_EVENT_CLICKED, (void *)(intptr_t)sens);
    return b;
}

void page_graphe_creer(lv_obj_t *zone)
{
    page_graphe_preparer();
    calque_defaire(); // entrée de page : objets neufs, toute image de la visite précédente est périmée
    g_piece = th_piece_affichee;
    g_scrub = -1;

    fleche(zone, LV_SYMBOL_LEFT, 8, -1);
    fleche(zone, LV_SYMBOL_RIGHT, UI_ZONE_L - 8 - 56, 1);
    o_nom = ui_label(zone, UI_POLICE_NOM_PIECE, UI_TEXTE, ""); // 31/10 (retour d'usage) : en gras, comme le thermostat
    lv_obj_align(o_nom, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t *points = ui_conteneur(zone); // 24/10 : comme la page Chauffage (vu au banc : il manquait)
    lv_obj_set_size(points, LV_SIZE_CONTENT, 9);
    lv_obj_set_flex_flow(points, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(points, 9, 0);
    lv_obj_align(points, LV_ALIGN_TOP_MID, 0, 40);
    for (int k = 0; k < TH_NB_PIECES; k++)
    {
        o_points[k] = ui_conteneur(points);
        lv_obj_set_size(o_points[k], 9, 9);
        lv_obj_set_style_radius(o_points[k], LV_RADIUS_CIRCLE, 0);
        ui_style_bg_opa(o_points[k], LV_OPA_COVER, 0);
    }

    o_val = ui_label(zone, UI_POLICE_TEMP_PETITE, UI_TEXTE, "");
    lv_obj_set_pos(o_val, 16, 58);
    o_min = ui_label(zone, UI_POLICE_TEXTE, UI_FROID, "");
    lv_obj_align(o_min, LV_ALIGN_TOP_RIGHT, -14, 60);
    o_max = ui_label(zone, UI_POLICE_TEXTE, UI_CHAUFFE, "");
    lv_obj_align(o_max, LV_ALIGN_TOP_RIGHT, -14, 84);

    o_graph = ui_conteneur(zone);
    lv_obj_set_size(o_graph, UI_ZONE_L, G_H);
    lv_obj_set_pos(o_graph, 0, G_Y);
    // L'image figée est opaque (RGB565) : on donne à l'objet le fond de page, plein et sans dégradé
    // (ui_shell.cpp:134-135, UI_FOND ui_theme.h:7) — la photo le porte et le rendu à l'écran ne change pas.
    ui_style_bg_color(o_graph, ui_c(UI_FOND), 0);
    ui_style_bg_opa(o_graph, LV_OPA_COVER, 0);
    lv_obj_add_flag(o_graph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_graph, graphe_dessin_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(o_graph, graphe_cb, LV_EVENT_ALL, NULL);

    o_hum = ui_conteneur(zone);
    lv_obj_set_size(o_hum, UI_ZONE_L, H_H);
    lv_obj_set_pos(o_hum, 0, H_Y);
    ui_style_bg_color(o_hum, ui_c(UI_FOND), 0); // même raison que pour o_graph
    ui_style_bg_opa(o_hum, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o_hum, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o_hum, hum_dessin_cb, LV_EVENT_DRAW_MAIN, NULL);
    o_hum_txt = ui_label(zone, UI_POLICE_LEGENDE, UI_HUMIDITE, "");
    lv_obj_set_pos(o_hum_txt, 4, H_Y);

    // Bulle (au-dessus du graphe, cachée hors lecture)
    o_bulle = uc_bulle(zone, &o_bulle_txt);

    // Pastilles de couches
    static const char *noms[NB_COUCHES] = {"Consigne", "Extérieur", "Chauffe", "Humidité"};
    static const uint32_t coul[NB_COUCHES] = {C_CONS, C_EXT, C_CHAUFFE, UI_HUMIDITE};
    lv_obj_t *rang = ui_conteneur(zone);
    lv_obj_set_size(rang, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(rang, UI_MARGE, 412);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int c = 0; c < NB_COUCHES; c++)
        o_couches[c] = uc_pastille(rang, noms[c], coul[c], 89, couche_cb, c);

    rafraichir();
}

void page_graphe_quitter()
{
    o_nom = nullptr;
    g_scrub = -1;
}

// 07/11 (mesure du calque, garde-fou « sortie/entrée de page et changement de pièce ») : le sélecteur
// de pièce est un bouton — le verrou du banc refuse tout appui synthétique (/toucher, /glisser, forcer,
// et c'est voulu). Ce hook appelle le CODE MÊME de piece_cb (même corps, même ordre), pas un geste
// injecté. Précédent : page_ballon_banc_courbe() qui appelait le code de onglet_cb (51083e7).
void page_graphe_banc_piece(int sens)
{
    if (!o_graph)
        return; // page fermée : rien à changer
    g_piece = (g_piece + TH_NB_PIECES + sens) % TH_NB_PIECES;
    g_scrub = -1;
    rafraichir();
}
