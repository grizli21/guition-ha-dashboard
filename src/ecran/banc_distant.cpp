#include "banc_distant.h"
#include "../ui_lvgl/ui_sonnette.h"
#include <Arduino.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include "../ui_lvgl/ui_diag.h"
#include "../ui_lvgl/ui_theme.h"
#include "../ui_lvgl/ui_radar.h"
#include "ui_lvgl/ui_shell.h"
#include "../ui_lvgl/page_graphe.h" // 06/11 : route /scrubgraph (mesure du glissé horizontal dans le graphe)
#include "../ui_lvgl/page_ballon.h" // 06/11 (chantier 2) : route /scrubballon
#include "../ui_lvgl/page_poele.h"  // 06/11 (chantier 2) : route /scrubpoele
#include "../ui_lvgl/page_radar.h"  // 06/11 (chantier 2) : route /scrubradar
#include "secrets_wifi.h"           // le banc ne répond qu'au PC du réseau local (RESEAU_PC, hors git)

#define L 480
#define H 480
static const IPAddress PC(RESEAU_OCTET_1, RESEAU_OCTET_2, RESEAU_OCTET_3, RESEAU_PC); // seul client accepté

static void (*g_lire)(int y, int n, uint16_t *px) = nullptr;
static WebServer g_serveur(8080);
volatile uint32_t banc_dodo_s = 0;

// appui simulé : du point 0 au point 1 pendant « duree », puis un relâcher
static struct
{
    bool actif, relacher;
    uint32_t debut, duree;
    int x0, y0, x1, y1;
} g_doigt = {};

bool banc_toucher(bool *appuye, uint16_t *x, uint16_t *y)
{
    if (g_doigt.actif)
    {
        const uint32_t t = millis() - g_doigt.debut;
        if (t < g_doigt.duree)
        {
            *appuye = true;
            *x = g_doigt.x0 + (g_doigt.x1 - g_doigt.x0) * (int32_t)t / (int32_t)g_doigt.duree;
            *y = g_doigt.y0 + (g_doigt.y1 - g_doigt.y0) * (int32_t)t / (int32_t)g_doigt.duree;
            return true;
        }
        g_doigt.actif = false;
        g_doigt.relacher = true;
    }
    if (g_doigt.relacher) // une lecture « relâché » au point final, pour que LVGL voie le clic
    {
        g_doigt.relacher = false;
        *appuye = false;
        *x = g_doigt.x1;
        *y = g_doigt.y1;
        return true;
    }
    return false;
}

// 24/10 : reproduire sans toucher le figeage du store (déclenché par le style d'un bouton large retiré / remis à chaque
// image) : un bouton de même taille, posé par-dessus tout, dont le style bascule à chaque image pendant s secondes
static lv_obj_t *g_stress = nullptr;
static uint32_t g_stress_fin = 0;
static void stress_cb(lv_timer_t *t)
{
    static bool principal = false;
    if ((int32_t)(millis() - g_stress_fin) >= 0)
    {
        lv_obj_del(g_stress);
        g_stress = nullptr;
        lv_timer_del(t);
        ui_trace("[banc] stress terminé\n");
        return;
    }
    principal = !principal;
    if (principal)
        lv_obj_add_style(g_stress, &ui_st_bouton_principal, 0);
    else
        lv_obj_remove_style(g_stress, &ui_st_bouton_principal, 0);
}

static bool autorise()
{
    if (g_serveur.client().remoteIP() == PC)
        return true;
    g_serveur.send(403, "text/plain", "reserve au PC\n");
    return false;
}

static int arg(const char *nom, int defaut)
{
    return g_serveur.hasArg(nom) ? g_serveur.arg(nom).toInt() : defaut;
}

static void lancer(int x0, int y0, int x1, int y1, int ms)
{
    g_doigt = {true, false, millis(), (uint32_t)LV_CLAMP(30, ms, 5000), LV_CLAMP(0, x0, L - 1), LV_CLAMP(0, y0, H - 1),
               LV_CLAMP(0, x1, L - 1), LV_CLAMP(0, y1, H - 1)};
    g_serveur.send(200, "text/plain", "ok\n");
}

// Descente d'arbre manuelle sous un point : lv_obj_hit_test ne rend qu'un booléen et le verrou doit rester
// strict (analyse de portage 8.4 -> 9.6, 05/11).
#if LVGL_VERSION_MAJOR >= 9
static bool est_cliquable(lv_obj_t *o) { return lv_obj_is_clickable(o); }
// 06/11 (etape 3- cran bis) : idem pour SCROLLABLE — « lv_obj_has_flag » est deprecie en 9.6 et le
// projet enregistre lv_log_register_print_cb (ui_diag_esp.cpp:181), donc chaque appel couterait une
// trace « [lvgl] [Warn] … » : le successeur public est « lv_obj_is_scrollable » (include/lvgl/core/lv_obj.h:585).
static bool est_defilable(lv_obj_t *o) { return lv_obj_is_scrollable(o); }
#define CLASSE_BOUTON lv_button_class
#define CLASSE_PAVILLON lv_buttonmatrix_class
#define CLASSE_IMAGE lv_image_class
#else
static bool est_cliquable(lv_obj_t *o) { return lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE); }
static bool est_defilable(lv_obj_t *o) { return lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE); }
#define CLASSE_BOUTON lv_btn_class
#define CLASSE_PAVILLON lv_btnmatrix_class
#define CLASSE_IMAGE lv_img_class
#endif

static const char *nom_classe(lv_obj_t *c)
{
    if (lv_obj_check_type(c, &CLASSE_BOUTON)) return "bouton";
    if (lv_obj_check_type(c, &lv_slider_class)) return "curseur";
    if (lv_obj_check_type(c, &lv_switch_class)) return "interrupteur";
    if (lv_obj_check_type(c, &CLASSE_PAVILLON)) return "pavillon";
    if (lv_obj_check_type(c, &lv_checkbox_class)) return "case";
    if (lv_obj_check_type(c, &lv_label_class)) return "texte";
    if (lv_obj_check_type(c, &CLASSE_IMAGE)) return "image";
    if (lv_obj_check_type(c, &lv_obj_class)) return "conteneur";
    return "autre";
}

// 05/11 soir (INCIDENT : une lumiere changee par un glisse) : ce qui commande n'est pas qu'un curseur. Une carte
// d'application peut s'activer sur la RELEASE, meme apres 280 px de deplacement : un glisse qui DEMARRE sur un
// objet cliquable peut actionner. Le banc refuse ce demarrage (et tout appui), sauf forcer=1 en connaissance de cause.
static lv_obj_t *objet_sous(lv_obj_t *o, const lv_point_t *p); // plus bas

static bool appui_dangereux(int x, int y, char *quoi, size_t n)
{
    // le point sonde est celui qui sera reellement envoye : « lancer » clampe deja x/y dans l'ecran, donc sans ce
    // clamp un (x,y) hors ecran franchissait le verrou (rien « sous le doigt ») puis retombait sur un objet du coin.
    lv_point_t p = {LV_CLAMP(0, x, L - 1), LV_CLAMP(0, y, H - 1)};
    lv_obj_t *o = objet_sous(lv_scr_act(), &p);
    if (!o)
        return false;
    for (lv_obj_t *c = o; c; c = lv_obj_get_parent(c))
        if (est_cliquable(c))
        {
            if (quoi)
                snprintf(quoi, n, "%s", nom_classe(c));
            return true;
        }
    return false;
}

static lv_obj_t *objet_sous(lv_obj_t *o, const lv_point_t *p)
{
    if (!o || !lv_obj_hit_test(o, p))
        return nullptr;
    for (int i = lv_obj_get_child_cnt(o) - 1; i >= 0; i--)
    {
        lv_obj_t *c = objet_sous(lv_obj_get_child(o, i), p);
        if (c)
            return c;
    }
    return o;
}

// ── /défiler : le DÉFILEMENT sans DOIGT (route de banc) ────────────────────────
static lv_obj_t *defilable_dabord(lv_obj_t *o)
{
    if (!o)
        return nullptr;
    if (est_defilable(o) && lv_obj_get_scroll_top(o) + lv_obj_get_scroll_bottom(o) > 0)
        return o;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
    {
        lv_obj_t *c = defilable_dabord(lv_obj_get_child(o, i));
        if (c)
            return c;
    }
    return nullptr;
}

// Le deplacement brut, sans animation et sans indev : le MEME appel dans les deux versions, sous des
// noms differents (8.4 : « _lv_obj_scroll_by_raw », lv_obj_scroll.h:260 ; 9.6 : « lv_obj_scroll_by_raw »,
// src/core/lv_obj_scroll_private.h:40). C est le point d entree du chemin rapide « scroll par bande ».
#if LVGL_VERSION_MAJOR >= 9
// 9.6 : ce n'est PLUS un en-tete public — « lv_obj_scroll_by_raw » est passe dans src/core/
// lv_obj_scroll_private.h, que l'application ne voit pas (l'en-tete de compat
// include/lvgl/api_map/lv_api_map_v9_1.h:73 ne fait que renommer « _lv_obj_scroll_by_raw » vers lui).
// On en donne donc le prototype exact ici, a la main : c'est bien le symbole que la librairie
// s'appelle a elle-meme pour un glisse (src/core/lv_obj_scroll.c:1017, elan) et le lien le prouve —
// sans lui, la compilation passerait mais le lien échouerait immediatement (reference non definie).
// On n'utilise PAS le successeur public « lv_obj_scroll_by(..., LV_ANIM_OFF) » : il encadre l'appel
// brut de LV_EVENT_SCROLL_BEGIN/END (src/core/lv_obj_scroll.c:391-400) et ces evenements-la n'ont
// jamais ete emis par un glisse de doigt — la comparaison avec le geste d'avant le verrou exigerait
// le meme chemin, pas un chemin voisin.
extern "C" lv_result_t lv_obj_scroll_by_raw(lv_obj_t * obj, int32_t x, int32_t y);
#endif
static void defile_brut(lv_obj_t *o, int dy)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_scroll_by_raw(o, 0, dy);
#else
    _lv_obj_scroll_by_raw(o, 0, (lv_coord_t)dy);
#endif
}

// 06/11 (chantier 2, audit e58325f) : balayage pas à pas d'un hook de lecture au doigt — même
// pompe que /scrubgraph (4f41058) : une image LVGL par pas (lv_timer_handler entre les pas),
// aucun geste injecté (ni /glisser, ni /toucher, ni forcer) ; c'est le hook qui emprunte le
// chemin réel du doigt. tenir=1 : le dernier pas reste posé (doigt tenu), pour la capture.
static void balayer_lecture(void (*paso)(int), int pas, int ms, int x0, int x1, int tenir)
{
    paso(-1); // depart : doigt leve, comme avant l'appui
    lv_timer_handler();
    for (int i = 1; i <= pas; i++)
    {
        paso(x0 + (int)((int64_t)(x1 - x0) * i / pas));
        const uint32_t echeance = millis() + (uint32_t)(ms / pas);
        do
        {
            lv_timer_handler();
            delay(1);
        } while ((int32_t)(millis() - echeance) < 0);
        esp_task_wdt_reset();
    }
    if (!tenir)
        paso(-1); // relacher, comme un doigt qui se leve
    lv_timer_handler();
}

void banc_demarrer(void (*lire)(int y, int n, uint16_t *px))
{
    g_lire = lire;
    g_serveur.on("/ecran", []()
                 {
        if (!autorise())
            return;
        ui_diag_etape("banc:ecran");
        // relue d'un coup dans un tampon temporaire (PSRAM, libéré après l'envoi), puis envoyée par 4 Ko comme avant
        // (24/10 : relire 4 lignes entre deux envois figeait la boucle plus de 10 s)
        uint16_t *image = (uint16_t *)ps_malloc(L * H * 2);
        if (!image)
        {
            g_serveur.send(500, "text/plain", "pas de PSRAM\n");
            return;
        }
        const uint32_t t0 = millis();
        g_lire(0, H, image);
        const uint32_t t1 = millis();
        g_serveur.client().setNoDelay(true); // sans : chaque fin de bloc attend l'accusé de réception (~165 ms, 18,7 s en tout)
        g_serveur.setContentLength(L * H * 2);
        g_serveur.send(200, "application/octet-stream", "");
        // la boucle LVGL est arrêtée pendant l'envoi : image cohérente. 25/10 : sur un Wi-Fi lent (chambre 1), l'envoi
        // passait 10 s et le chien de garde redémarrait l'écran : nourri à chaque bloc, abandon au-delà de 25 s
        for (size_t o = 0; o < L * H * 2 && millis() - t1 < 25000; o += 4096)
        {
            g_serveur.sendContent((const char *)image + o, LV_MIN(4096, L * H * 2 - o));
            esp_task_wdt_reset();
        }
        free(image);
        ui_trace("[banc] capture : relue en %u ms, envoyée en %u ms\n", (unsigned)(t1 - t0), (unsigned)(millis() - t1)); });
    g_serveur.on("/toucher", []()
                 {
        if (!autorise())
            return;
        const int x = arg("x", 0), y = arg("y", 0);
        char quoi[24] = "";
        if (!arg("forcer", 0) && appui_dangereux(x, y, quoi, sizeof quoi))
        {
            ui_trace("[banc] appui REFUSE : (%d,%d) sur %s\n", x, y, quoi);
            g_serveur.send(403, "text/plain", "appui sur un objet cliquable : appui refuse\n");
            return;
        }
        lancer(x, y, x, y, arg("ms", 150)); });
    // 01/11 : banc du radar muet après un réveil : veille à distance (personne pour réveiller l'écran) et battement du
    // capteur lu sans passer par la page web du radar (qui compte comme une activité : pas de veille)
    g_serveur.on("/dodo", []()
                 {
        if (!autorise())
            return;
        banc_dodo_s = LV_CLAMP(5, arg("s", 20), 3600);
        g_serveur.send(200, "text/plain", "ok"); });
    g_serveur.on("/battement", []()
                 {
        if (!autorise())
            return;
        uint32_t trames, octets, muets, relances;
        const char *par;
        ui_radar_compteurs(&trames, &octets, &muets, &relances, &par);
        char j[160];
        snprintf(j, sizeof j, "{\"trames\":%lu,\"octets\":%lu,\"broche\":%d,\"mhz\":%u,\"muets\":%lu,\"relances\":%lu,\"par\":\"%s\",\"ms\":%lu}",
                 (unsigned long)trames, (unsigned long)octets, digitalRead(2), (unsigned)getCpuFrequencyMhz(), (unsigned long)muets,
                 (unsigned long)relances, par, (unsigned long)millis());
        g_serveur.send(200, "application/json", j); });
    // 05/11 (retour d'usage : « attention a ne pas appuyer sur un bouton en swipant ») : naviguer SANS appui et
    // verifier ce qui est sous le doigt AVANT tout geste synthetique. Le banc ne touche plus le rail a l'oeil.
    g_serveur.on("/pages", []()
                 {
        if (!autorise())
            return;
        char j[640]; int n = 0;
        n += snprintf(j + n, sizeof j - n, "{\"actuelle\":%d,\"pages\":[", ui_shell_page());
        for (int i = 0; i < ui_shell_nb() && n < (int)sizeof j - 48; i++)
            n += snprintf(j + n, sizeof j - n, "%s{\"i\":%d,\"nom\":\"%s\"}", i ? "," : "", i, ui_shell_nom(i) ? ui_shell_nom(i) : "?");
        snprintf(j + n, sizeof j - n, "]}");
        g_serveur.send(200, "application/json", j); });
    g_serveur.on("/page", []()
                 {
        if (!autorise())
            return;
        const int i = arg("i", -1);
        if (i < 0 || i >= ui_shell_nb())
        {
            g_serveur.send(404, "text/plain", "hors plage\n");
            return;
        }
        ui_shell_aller(i);
        ui_trace("[banc] page %d : %s\n", i, ui_shell_nom(i));
        g_serveur.send(200, "text/plain", "ok"); });
    g_serveur.on("/sous_le_doigt", []()
                 {
        if (!autorise())
            return;
        const int x = arg("x", 0), y = arg("y", 0);
        lv_point_t p = {x, y};
        lv_obj_t *o = objet_sous(lv_scr_act(), &p);
        if (!o)
        {
            g_serveur.send(404, "text/plain", "rien\n");
            return;
        }
        const char *classe = "rien de commande";
        bool commande = false;
        for (lv_obj_t *c = o; c; c = lv_obj_get_parent(c))
        {
            if (est_cliquable(c))
            {
                commande = true;
                if (lv_obj_check_type(c, &lv_slider_class)) classe = "curseur";
                else if (lv_obj_check_type(c, &lv_switch_class)) classe = "interrupteur";
                else if (lv_obj_check_type(c, &CLASSE_PAVILLON)) classe = "pavillon de boutons";
                else if (lv_obj_check_type(c, &lv_checkbox_class)) classe = "case a cocher";
                else if (lv_obj_check_type(c, &CLASSE_BOUTON)) classe = "bouton";
                else classe = "objet cliquable";
                break;
            }
        }
        char chaine[120]; int nc = 0;
        for (lv_obj_t *c = o; c && nc < (int)sizeof chaine - 16; c = lv_obj_get_parent(c))
            nc += snprintf(chaine + nc, sizeof chaine - nc, "%s%s", nc ? "<" : "", nom_classe(c));
        char j[320];
        snprintf(j, sizeof j, "{\"x\":%d,\"y\":%d,\"commande\":%d,\"classe\":\"%s\",\"chaine\":\"%s\"}",
                 x, y, (int)commande, classe, chaine);
        g_serveur.send(200, "application/json", j); });
    g_serveur.on("/glisser", []()
                 {
        if (!autorise())
            return;
        char quoi[24] = "";
        if (!arg("forcer", 0) && appui_dangereux(arg("x0", 0), arg("y0", 0), quoi, sizeof quoi))
        {
            ui_trace("[banc] glisse REFUSE : appui en (%d,%d) sur %s\n", arg("x0", 0), arg("y0", 0), quoi);
            g_serveur.send(403, "text/plain", "appui sur un objet cliquable : geste refuse\n");
            return;
        }
        lancer(arg("x0", 0), arg("y0", 0), arg("x1", 0), arg("y1", 0), arg("ms", 400)); });
    // 06/11 (etape 3- cran bis) : GET /defiler?dy=-280&ms=700 — deplacer le contenu de l ecran courant
    // SANS geste (voir le commentaire de « defilable_dabord »). dy = deplacement vertical total en px
    // (signe : negatif = on fait monter le contenu), ms = duree, bornee a 300…1500 pour que la commande
    // reste plus courte que le chien de garde et que deux passes se ressemblent.
    g_serveur.on("/defiler", []()
                 {
        if (!autorise())
            return;
        const int dy = arg("dy", -280);
        const int ms = LV_CLAMP(300, arg("ms", 700), 1500);
        lv_obj_t *o = defilable_dabord(lv_scr_act());
        if (!o)
        {
            ui_trace("[banc] defiler REFUSE : rien de defilable a l'ecran\n");
            g_serveur.send(404, "text/plain", "rien de defilable\n");
            return;
        }
        // Chaque passe repart du HAUT : sans ce rappel, six passes de 280 px descendraient jusqu'en bas
        // du contenu et les trois dernieres ne deplaceraient plus rien — les passes ne seraient plus
        // identiques. Le saut en haut repeint l'aire entiere : c'est une image, elle reste AVANT le
        // marqueur DEBUT (on laisse a LVGL le temps de la finir), donc elle ne pollue pas la mesure.
        lv_obj_scroll_to_y(o, 0, LV_ANIM_OFF);
        for (int i = 0; i < 4; i++)
        {
            lv_timer_handler();
            delay(5);
        }
        const int PAS = 30;                 // une trentaine de pas, comme au simulateur
        const int repos = ms / PAS;         // 700/30 = 23 ms ; la periode de rafraichissement est de 20 ms
                                            // (LV_DISP_DEF_REFR_PERIOD / LV_DEF_REFR_PERIOD) : ~ une image par pas
        const int debut_y = lv_obj_get_scroll_y(o);
        const int marge_bas = lv_obj_get_scroll_bottom(o);
        ui_trace("[banc] defiler DEBUT dy=%d ms=%d pas=%d cible=%s scroll_y=%d marge_bas=%d\n", dy, ms, PAS,
                 nom_classe(o), debut_y, marge_bas);
        int fait = 0;
        for (int i = 1; i <= PAS; i++)
        {
            const int saut = (int)((int32_t)dy * i / PAS) - fait;
            fait += saut;
            if (saut)
                defile_brut(o, saut);
            // delay() seul ne ferait tourner RIEN du tout : « lv_timer_handler() » n est appele que dans
            // loop() (src/ecran/ecran.cpp:1945) et le banc est servi DANS cette meme
            // tache — les 30 pas seraient fusionnes en une seule image. On pompe donc LVGL soi-meme
            // pendant le repos du pas : c est ce qui produit une image par pas, comme un vrai glisse.
            const uint32_t echeance = millis() + (uint32_t)repos;
            do
            {
                lv_timer_handler();
                delay(1);
            } while ((int32_t)(millis() - echeance) < 0);
            esp_task_wdt_reset(); // pendant la commande, la boucle ne nourrit plus le chien de garde
        }
        lv_timer_handler(); // la derniere poussee doit etre rendue avant de rendre la main
        ui_trace("[banc] defiler FIN demande=%d applique=%d marge_bas=%d\n", dy, lv_obj_get_scroll_y(o) - debut_y,
                 lv_obj_get_scroll_bottom(o));
        g_serveur.send(200, "text/plain", "ok\n"); });
    g_serveur.on("/scrubgraph", []()
                 {
        if (!autorise())
            return;
        const int pas = arg("pas", 30), ms = arg("ms", 700);
        const int x0 = arg("x0", 44), x1 = arg("x1", 396), tenir = arg("tenir", 0);
        ui_trace("[banc] scrubgraph DEBUT pas=%d ms=%d x=%d..%d tenir=%d\n", pas, ms, x0, x1, tenir);
        page_graphe_banc_scrub(-1); // depart : doigt leve, comme avant l'appui
        lv_timer_handler();
        for (int i = 1; i <= pas; i++)
        {
            page_graphe_banc_scrub(x0 + (int)((int64_t)(x1 - x0) * i / pas));
            const uint32_t echeance = millis() + (uint32_t)(ms / pas);
            do
            {
                lv_timer_handler();
                delay(1);
            } while ((int32_t)(millis() - echeance) < 0);
            esp_task_wdt_reset();
        }
        if (!tenir)
            page_graphe_banc_scrub(-1); // relacher, comme un doigt qui se leve
        lv_timer_handler();
        ui_trace("[banc] scrubgraph FIN pas=%d ms=%d\n", pas, ms);
        g_serveur.send(200, "text/plain", "ok\n"); });
    // Route de banc sans geste : le verrou refuserait les boutons de pièce du graphe (page Graphes ne commande
    // rien de réel) ; on appelle le code même de piece_cb.
    g_serveur.on("/piecegraphe", []()
                 {
        if (!autorise())
            return;
        const int sens = arg("sens", 1) < 0 ? -1 : 1;
        ui_trace("[banc] piecegraphe sens=%d (code de piece_cb, aucun geste)\n", sens);
        page_graphe_banc_piece(sens);
        lv_timer_handler();
        g_serveur.send(200, "text/plain", "ok\n"); });
    g_serveur.on("/scrubballon", []()
                 {
        if (!autorise())
            return;
        const int pas = arg("pas", 30), ms = arg("ms", 700);
        const int x0 = arg("x0", 40), x1 = arg("x1", 395), tenir = arg("tenir", 0);
        ui_trace("[banc] scrubballon DEBUT pas=%d ms=%d x=%d..%d tenir=%d\n", pas, ms, x0, x1, tenir);
        page_ballon_banc_courbe();
        lv_timer_handler();
        balayer_lecture(page_ballon_banc_scrub, pas, ms, x0, x1, tenir);
        ui_trace("[banc] scrubballon FIN pas=%d ms=%d\n", pas, ms);
        g_serveur.send(200, "text/plain", "ok\n"); });
    // Poêle (page « Poêle », vue Courbe) : courbe x 44..396 (page_poele.cpp:320-321, mêmes cotes
    // que page_graphe). La page est visible sur `.193` : mesure possible.
    g_serveur.on("/scrubpoele", []()
                 {
        if (!autorise())
            return;
        const int pas = arg("pas", 30), ms = arg("ms", 700);
        const int x0 = arg("x0", 44), x1 = arg("x1", 396), tenir = arg("tenir", 0);
        ui_trace("[banc] scrubpoele DEBUT pas=%d ms=%d x=%d..%d tenir=%d\n", pas, ms, x0, x1, tenir);
        page_poele_banc_courbe();
        lv_timer_handler();
        balayer_lecture(page_poele_banc_scrub, pas, ms, x0, x1, tenir);
        ui_trace("[banc] scrubpoele FIN pas=%d ms=%d\n", pas, ms);
        g_serveur.send(200, "text/plain", "ok\n"); });
    // Radar (onglet Radar de la page « Batterie et capteur ») : 16 zones, ~21 px de course par
    // franchissement (page_radar.cpp:156-157). ATTENTION : cette page n'existe que sur les écrans
    // SUR BATTERIE (ecran.cpp:1662) — pas sur `.193` (/page?i=15 répond « hors plage »).
    // Outil commité pour la suite, ni mesuré ni corrigeable sur `.193` ce soir.
    g_serveur.on("/scrubradar", []()
                 {
        if (!autorise())
            return;
        const int pas = arg("pas", 30), ms = arg("ms", 700);
        const int x0 = arg("x0", 36), x1 = arg("x1", 396), tenir = arg("tenir", 0);
        ui_trace("[banc] scrubradar DEBUT pas=%d ms=%d x=%d..%d tenir=%d\n", pas, ms, x0, x1, tenir);
        balayer_lecture(page_radar_banc_scrub, pas, ms, x0, x1, tenir);
        ui_trace("[banc] scrubradar FIN pas=%d ms=%d\n", pas, ms);
        g_serveur.send(200, "text/plain", "ok\n"); });
    // 27/10 : essai de la vue caméra de la sonnette sans sonner (le vrai bouton fait sonner le carillon)
    g_serveur.on("/sonnette", []()
                 {
        if (!autorise())
            return;
        ui_sonnette_recevoir(arg("etat", 1) != 0);
        g_serveur.send(200, "text/plain", "ok"); });
    // 27/10 : essai A/B de la recopie aussitôt entre les deux mémoires d'image (ecran.cpp)
    g_serveur.on("/sync", []()
                 {
        if (!autorise())
            return;
        extern bool g_sync_aussitot;
        g_sync_aussitot = arg("v", 1) != 0;
        ui_trace("[banc] recopie aussitôt : %s\n", g_sync_aussitot ? "oui" : "non");
        g_serveur.send(200, "text/plain", "ok"); });
    g_serveur.on("/stress", []()
                 {
        if (!autorise())
            return;
        if (g_stress)
        {
            g_serveur.send(409, "text/plain", "deja en cours\n");
            return;
        }
        const int s = LV_CLAMP(1, arg("s", 60), 600);
        g_stress = ui_bouton(lv_layer_top(), 368, 40);
        lv_obj_align(g_stress, LV_ALIGN_BOTTOM_LEFT, 12, -12);
        lv_obj_center(ui_label(g_stress, UI_POLICE_TEXTE, UI_TEXTE, "Stress du rendu (banc)"));
        g_stress_fin = millis() + s * 1000;
        lv_timer_create(stress_cb, 20, nullptr);
        ui_trace("[banc] stress du rendu : %d s\n", s);
        g_serveur.send(200, "text/plain", "ok\n"); });
    g_serveur.begin();
}

void banc_tache() { g_serveur.handleClient(); }
