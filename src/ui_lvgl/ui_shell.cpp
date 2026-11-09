#include "ui_shell.h"
#include "ui_theme.h"
#include "ui_camera.h"
#include "ui_diag.h"

#define ITEM_H 64
#define RAIL_REPRISE_MS 300 // la caméra repart quand le rail est immobile depuis ce délai

static const ui_page_t *g_pages = nullptr;
static int g_nb = 0;
static int g_actif = -1;
static int g_accueil = 0;
static uint32_t g_retour_accueil_ms = 60000;

// 06/11 diagnostic page Réglages (9.6) : le plantage tombe pendant la PREMIÈRE image dessinée après une
// navigation, jamais pendant la construction. Ce compte à rebours arme les sondes de tas posées sur le
// chemin de rendu (src/ecran/ecran.cpp) pendant les trois images qui suivent.
static int g_sonder_images = 0;

static lv_obj_t *g_rail;
static lv_obj_t *g_zone;
static lv_obj_t *g_boutons[24];
static uint32_t g_couleur[24]; // couleur au repos propre à une icône (0 = UI_TEXTE2)

static void marquer(int i, bool actif)
{
    if (i < 0)
        return;
    lv_obj_t *b = g_boutons[i];
    if (actif)
        lv_obj_add_state(b, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(b, LV_STATE_CHECKED);
    // l'icône ne partage pas l'état de son bouton : recoloration à la main
    lv_obj_set_style_img_recolor(lv_obj_get_child(b, 0), ui_c(actif ? UI_ACCENT : g_couleur[i] ? g_couleur[i] : UI_TEXTE2), 0);
}

void ui_shell_icone(int index, const lv_img_dsc_t *icone, uint32_t couleur)
{
    if (index < 0 || index >= g_nb)
        return;
    ui_img_src(lv_obj_get_child(g_boutons[index], 0), icone);
    if (g_couleur[index] != couleur)
    {
        g_couleur[index] = couleur;
        marquer(index, index == g_actif);
    }
}

int ui_shell_nb() { return g_nb; }
const char *ui_shell_nom(int index) { return (index >= 0 && index < g_nb) ? g_pages[index].nom : nullptr; }

#ifdef ARDUINO
// 06/11 diagnostic des redémarrages 9.6 (pages Lumières et Réglages) : entoure la construction de
// page — durée, objets créés, tas interne avant/après et intégrité du tas. Commit séparé, retirable :
// aucune incidence sur le rendu (une trace et quatre repères par navigation).
#include <Arduino.h> // millis()
#include <esp_heap_caps.h>
static uint32_t zone_objets(lv_obj_t *o)
{
    uint32_t n = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++)
        n += zone_objets(lv_obj_get_child(o, i));
    return n;
}
#endif

void ui_shell_aller(int index)
{
    if (index < 0 || index >= g_nb || index == g_actif)
        return;
#ifdef ARDUINO
    const uint32_t t0 = millis();
    const uint32_t lib0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t bloc0 = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const int integ0 = heap_caps_check_integrity_all(MALLOC_CAP_INTERNAL) ? 1 : 0;
    ui_diag_point("quitter");
#endif
    if (g_actif >= 0 && g_pages[g_actif].quitter)
        g_pages[g_actif].quitter();
#ifdef ARDUINO
    ui_diag_point("clean");
#endif
    marquer(g_actif, false);
    lv_obj_clean(g_zone);
#ifdef ARDUINO
    // 06/11 v5 : l'arbre de l'ancienne page vient d'être détruit — le tas est-il encore intègre ?
    ui_trace("[page-diag] %d : integrite apres clean %d\n", index, heap_caps_check_integrity_all(MALLOC_CAP_INTERNAL) ? 1 : 0);
#endif
#ifdef ARDUINO
    ui_diag_point("creer");
#endif
    g_actif = index;
    marquer(g_actif, true);
    g_pages[g_actif].creer(g_zone);
#ifdef ARDUINO
    ui_diag_point("fin");
    // 06/11 v4 : en v3, AUCUNE trace n'est passée (r5) — le crash est dans le premier getter évalué
    // (GCC évalue les arguments de droite à gauche). Chaque mesure a sa trace : la dernière ligne
    // émise dit précisément quel appel du tas a tué la boucle.
    ui_trace("[page-diag] %d : %u ms ; interne avant %u o ; plus gros avant %u o ; integrite avant %d\n",
             index, (unsigned)(millis() - t0), (unsigned)lib0, (unsigned)bloc0, integ0);
    ui_trace("[page-diag] %d : interne apres %u o\n", index, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ui_trace("[page-diag] %d : plus gros apres %u o\n", index, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ui_trace("[page-diag] %d : integrite apres %d\n", index, heap_caps_check_integrity_all(MALLOC_CAP_INTERNAL) ? 1 : 0);
    ui_trace("[page-diag] %d : integrite PSRAM apres %d ; pile %u mots\n", index,
             heap_caps_check_integrity_all(MALLOC_CAP_SPIRAM) ? 1 : 0, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    ui_trace("[page-diag] %d : objets %u\n", index, (unsigned)zone_objets(g_zone));
#endif
    g_sonder_images = 3; // 06/11 diagnostic : les trois prochaines images dessinées sont sondées
}

int ui_shell_page() { return g_actif; }

bool ui_shell_sonder_dessin() { return g_sonder_images > 0; }
void ui_shell_image_sondee() { if (g_sonder_images > 0) g_sonder_images--; }

void ui_shell_retour_accueil(uint32_t ms) { g_retour_accueil_ms = ms; }

static void item_cb(lv_event_t *e)
{
    // CLICKED n'arrive pas si le doigt a fait défiler la colonne
    ui_shell_aller((int)(intptr_t)lv_event_get_user_data(e));
}

void ui_shell_creer(const ui_page_t *pages, int nb, int page_accueil)
{
    g_pages = pages;
    g_nb = nb > 24 ? 24 : nb;
    g_accueil = page_accueil;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, ui_c(UI_FOND), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    g_zone = ui_conteneur(scr);
    lv_obj_set_size(g_zone, UI_ZONE_L, UI_ZONE_H);
    lv_obj_set_pos(g_zone, 0, 0);

    // 26/10 (retour d'usage : « épinglée en bas du rail ») : une page épinglée a sa place fixe sous le carrousel, qui raccourcit d'autant
    int nb_epinglees = 0;
    for (int i = 0; i < g_nb; i++)
        nb_epinglees += pages[i].epinglee;
    const lv_coord_t h_epingle = nb_epinglees ? nb_epinglees * (ITEM_H + 4) + 6 : 0;
    lv_obj_t *epingle = nullptr;
    if (nb_epinglees)
    {
        epingle = ui_conteneur(scr);
        lv_obj_set_size(epingle, UI_RAIL_L, h_epingle);
        lv_obj_set_pos(epingle, UI_ZONE_L, UI_ECRAN - h_epingle);
        lv_obj_set_style_pad_bottom(epingle, 6, 0);
        lv_obj_set_style_pad_row(epingle, 4, 0);
        lv_obj_set_flex_flow(epingle, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(epingle, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }

    g_rail = lv_obj_create(scr);
    lv_obj_remove_style_all(g_rail);
    lv_obj_set_size(g_rail, UI_RAIL_L, UI_ECRAN - h_epingle);
    lv_obj_set_pos(g_rail, UI_ZONE_L, 0);
    // même fond que la page, pas de trait de séparation (18/10)
    lv_obj_set_style_pad_ver(g_rail, 6, 0);
    lv_obj_set_style_pad_row(g_rail, 4, 0);
    lv_obj_set_flex_flow(g_rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_rail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(g_rail, LV_DIR_VER);
    // 24/10 (retour d'usage : « le bandeau va trop loin, il est censé être toujours complet ») : sans aimantation (LV_SCROLL_SNAP_START
    // amenait la dernière icône en haut : du vide dessous) ni élastique : il s'arrête sur la première / la dernière icône
    lv_obj_clear_flag(g_rail, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(g_rail, LV_SCROLLBAR_MODE_OFF);
    // 27/10 : la caméra s'arrête dès que le doigt se pose sur le rail, et ne repart que quand il est levé ET que le rail est
    // IMMOBILE depuis RAIL_REPRISE_MS (300 ms ; essayé 1000 : le tremblement venait d'ailleurs, cf. g_sync_aussitot ; retour d'usage : « mets le stream en pause jusqu'à la fin du mouvement du rail ») : l'élan avance
    // par petits pas qui ont chacun leur début / fin de défilement ; relancée entre deux pas, la caméra espaçait les pas
    // (47 -> 135-300 ms, mesuré) et l'élan durait 1,4 à 3 s au lieu de 0,4 à 0,8 ; les appuis des icônes remontent
    // jusqu'au rail (LV_OBJ_FLAG_EVENT_BUBBLE)
    lv_obj_add_event_cb(g_rail, [](lv_event_t *e)
                        {
                            static bool appuye = false;
                            static lv_timer_t *reprise = nullptr;
                            const lv_event_code_t code = lv_event_get_code(e);
                            if (code == LV_EVENT_PRESSED || code == LV_EVENT_SCROLL_BEGIN)
                            {
                                if (code == LV_EVENT_PRESSED)
                                    appuye = true;
                                if (reprise)
                                    lv_timer_pause(reprise);
                                ui_camera_suspendre(true);
                                return;
                            }
                            if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
                                appuye = false;
                            else if (code != LV_EVENT_SCROLL_END)
                                return;
                            if (appuye)
                                return;
                            if (!reprise)
                                reprise = lv_timer_create([](lv_timer_t *t)
                                                          {
                                                              lv_timer_pause(t);
                                                              ui_camera_suspendre(false); }, RAIL_REPRISE_MS, NULL);
                            lv_timer_reset(reprise);
                            lv_timer_resume(reprise); },
                        LV_EVENT_ALL, NULL);

    for (int i = 0; i < g_nb; i++)
    {
        lv_obj_t *b = lv_obj_create(pages[i].epinglee ? epingle : g_rail);
        lv_obj_remove_style_all(b);
        g_boutons[i] = b;
        lv_obj_set_size(b, 68, ITEM_H);
        // 05/11/2026 (étape 2-vague E) : en 9.6 « lv_obj_add_flag / lv_obj_clear_flag » prennent un
        // « lv_obj_flag_t » typé (include/lvgl/core/lv_obj.h) : « A | B » y est un int, refusé en C++. Le cast
        // rétablit le type ; la sémantique est identique, ces deux fonctions traitent bien chaque bit du masque
        // (src/core/lv_obj.c:406-423 pour l'ajout, :454-470 pour le retrait). En 8.4 le cast est neutre.
        lv_obj_clear_flag(b, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS)); // sinon l'icône touchée remonte en tête
        lv_obj_add_flag(b, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE));
        lv_obj_set_style_radius(b, UI_RAYON, 0);
        lv_obj_set_style_bg_color(b, ui_c(UI_CARTE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, ui_c(UI_ACCENT_FOND), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if (!pages[i].epinglee)
            lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE); // 27/10 : appui / relâché jusqu'au rail (pause de la caméra)

        lv_obj_t *img = ui_icone(b, pages[i].icone, UI_TEXTE2);
        lv_obj_center(img);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    }

    ui_shell_aller(g_accueil);
}

void ui_shell_tache()
{
    // Après inactivité : le carrousel remonte, puis on revient à la page d'accueil
    static bool rail_remonte = true, accueil_fait = true;
    uint32_t inactif = lv_disp_get_inactive_time(NULL);
    if (inactif < 1000)
    {
        rail_remonte = false;
        accueil_fait = false;
        return;
    }
    // 07/11 (retour d'usage) : le rail ne remonte plus seul à 30 s : il remonte avec le retour à l'accueil, et pas du tout si
    // ce retour est réglé sur « jamais » (g_retour_accueil_ms == 0)
    if (!rail_remonte && g_retour_accueil_ms && inactif > g_retour_accueil_ms)
    {
        lv_obj_scroll_to_y(g_rail, 0, LV_ANIM_ON);
        rail_remonte = true;
    }
    if (!accueil_fait && g_retour_accueil_ms && inactif > g_retour_accueil_ms)
    {
        ui_shell_aller(g_accueil);
        accueil_fait = true;
    }
}
