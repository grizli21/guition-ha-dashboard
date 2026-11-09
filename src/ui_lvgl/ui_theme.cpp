#include "ui_theme.h"
#include "ui_diag.h" // 05/11 : ui_trace — motifs de refus du scroll par bande
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if LVGL_VERSION_MAJOR >= 9
// 05/11/2026 (étape 2-vague E) : « lv_mem_alloc / lv_mem_free » ont disparu de l'API 9.6 ; les allocateurs
// publics sont « lv_malloc » (include/lvgl/stdlib/lv_mem.h:67) et « lv_free » (:95).
#define UI_ALLOC(n) lv_malloc(n)
#define UI_LIBERE(p) lv_free(p)
#else
#define UI_ALLOC(n) lv_mem_alloc(n) // 8.4 : lib/lvgl-8.4.0/src/misc/lv_mem.h
#define UI_LIBERE(p) lv_mem_free(p)
#endif

lv_style_t ui_st_nu;
lv_style_t ui_st_carte;
lv_style_t ui_st_bouton;
lv_style_t ui_st_bouton_presse;
lv_style_t ui_st_bouton_principal;
lv_style_t ui_st_pastille_active;

void ui_theme_init()
{
    // Le thème LVGL par défaut est clair (LV_THEME_DEFAULT_DARK 0) : on repart de lui en sombre,
    // puis nos styles passent par-dessus objet par objet.
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, ui_c(UI_ACCENT), ui_c(UI_FROID), true, UI_POLICE_TEXTE);
    lv_disp_set_theme(disp, th);

    lv_style_init(&ui_st_nu);
    lv_style_set_bg_opa(&ui_st_nu, LV_OPA_TRANSP);
    lv_style_set_border_width(&ui_st_nu, 0);
    lv_style_set_pad_all(&ui_st_nu, 0);
    lv_style_set_radius(&ui_st_nu, 0);
    lv_style_set_shadow_width(&ui_st_nu, 0);

    lv_style_init(&ui_st_carte);
    lv_style_set_bg_color(&ui_st_carte, ui_c(UI_CARTE));
    lv_style_set_bg_opa(&ui_st_carte, LV_OPA_COVER);
    lv_style_set_border_color(&ui_st_carte, ui_c(UI_BORD));
    lv_style_set_border_width(&ui_st_carte, 1);
    lv_style_set_radius(&ui_st_carte, 16);
    lv_style_set_pad_all(&ui_st_carte, UI_MARGE);
    lv_style_set_shadow_width(&ui_st_carte, 0);
    lv_style_set_text_color(&ui_st_carte, ui_c(UI_TEXTE));

    lv_style_init(&ui_st_bouton);
    lv_style_set_bg_color(&ui_st_bouton, ui_c(UI_CARTE));
    lv_style_set_bg_opa(&ui_st_bouton, LV_OPA_COVER);
    lv_style_set_border_color(&ui_st_bouton, ui_c(UI_BORD));
    lv_style_set_border_width(&ui_st_bouton, 1);
    lv_style_set_radius(&ui_st_bouton, UI_RAYON);
    lv_style_set_shadow_width(&ui_st_bouton, 0);
    lv_style_set_pad_all(&ui_st_bouton, 0);
    lv_style_set_text_color(&ui_st_bouton, ui_c(UI_TEXTE));
    lv_style_set_text_font(&ui_st_bouton, UI_POLICE_TEXTE);

    lv_style_init(&ui_st_bouton_presse);
    lv_style_set_bg_color(&ui_st_bouton_presse, ui_c(UI_CARTE2));

    lv_style_init(&ui_st_bouton_principal);
    lv_style_set_bg_color(&ui_st_bouton_principal, ui_c(UI_ACCENT));
    lv_style_set_border_color(&ui_st_bouton_principal, ui_c(UI_ACCENT));
    lv_style_set_text_color(&ui_st_bouton_principal, ui_c(UI_SUR_ACCENT));

    lv_style_init(&ui_st_pastille_active);
    lv_style_set_bg_color(&ui_st_pastille_active, ui_c(UI_ACCENT));
    lv_style_set_border_color(&ui_st_pastille_active, ui_c(UI_ACCENT));
    lv_style_set_text_color(&ui_st_pastille_active, ui_c(UI_SUR_ACCENT));
}

// --- Mises à jour gardées (voir ui_theme.h) ---
#if LVGL_VERSION_MAJOR >= 9
// 05/11/2026 (étape 2-vague E) : en 9.6 « lv_color_t » est un RGB888 de 3 octets sans champ « full »
// (include/lvgl/draw/lv_color.h:109-113, « blue/green/red » — et il n'y a plus de LV_COLOR_DEPTH dans lv_conf.h).
// Le 8.4 « .full » comparait l'entier encodé (uint16_t RGB565 : lib/lvgl-8.4.0/src/misc/lv_color.h:221).
// L'égalité publique 9.6 est « lv_color_eq » (include/lvgl/draw/lv_color.h:268) : elle compare les trois
// composantes, donc même sens et même résultat pour deux couleurs déjà passées par la même rampe.
#define UI_COULEUR_IDENTIQUE(a, b) lv_color_eq((a), (b))
#else
#define UI_COULEUR_IDENTIQUE(a, b) ((a).full == (b).full) // 8.4 : lv_color16_t.union « full » (RGB565)
#endif
static void si_change(lv_obj_t *o, lv_style_prop_t p, lv_style_value_t v, lv_style_selector_t s, bool couleur)
{
    // 05/11/2026 (étape 2-vague E) : CORRECTION lue dans les deux en-têtes. « lv_obj_get_local_style_prop » rend un
    // « lv_style_res_t » — pas un « lv_res_t » (9.6 : include/lvgl/core/lv_obj_style.h:256, enum
    // include/lvgl/core/lv_style.h:347-348 ; 8.4 : src/core/lv_obj_style.h:146, enum src/misc/lv_style.h:288-289).
    // Or « LV_STYLE_RES_NOT_FOUND = 0 » et « LV_RES_OK = 0 » : l'ancien « == LV_RES_OK » voulait donc dire
    // « introuvable » — on comparait une valeur non initialisée dans ce cas, et on ne comparait RIEN quand la
    // valeur était trouvée (le garde-fou ne servait jamais à rien, il re-écrivait la propriété à chaque appel).
    // Le compilateur 9.6 le signalait : « warning: comparison between 'enum lv_style_res_t' and 'enum lv_result_t' »
    // (mesuré dans la construction de l'objet, journal .banc_v9_esp/logs/objets_E/ui_theme.cpp.o.log).
    lv_style_value_t a = {};
    if (lv_obj_get_local_style_prop(o, p, &a, s) == LV_STYLE_RES_FOUND && (couleur ? UI_COULEUR_IDENTIQUE(a.color, v.color) : a.num == v.num))
        return;
    lv_obj_set_local_style_prop(o, p, v, s);
}
static lv_style_value_t couleur(lv_color_t c)
{
    lv_style_value_t v = {};
    v.color = c;
    return v;
}
static lv_style_value_t nombre(int32_t n)
{
    lv_style_value_t v = {};
    v.num = n;
    return v;
}
void ui_style_text_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s) { si_change(o, LV_STYLE_TEXT_COLOR, couleur(c), s, true); }
void ui_style_bg_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s) { si_change(o, LV_STYLE_BG_COLOR, couleur(c), s, true); }
void ui_style_border_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s) { si_change(o, LV_STYLE_BORDER_COLOR, couleur(c), s, true); }
void ui_style_img_recolor(lv_obj_t *o, lv_color_t c, lv_style_selector_t s) { si_change(o, LV_STYLE_IMG_RECOLOR, couleur(c), s, true); }
void ui_style_arc_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s) { si_change(o, LV_STYLE_ARC_COLOR, couleur(c), s, true); }
void ui_style_bg_opa(lv_obj_t *o, lv_opa_t v, lv_style_selector_t s) { si_change(o, LV_STYLE_BG_OPA, nombre(v), s, false); }
void ui_style_opa(lv_obj_t *o, lv_opa_t v, lv_style_selector_t s) { si_change(o, LV_STYLE_OPA, nombre(v), s, false); }
void ui_style_border_width(lv_obj_t *o, lv_coord_t v, lv_style_selector_t s) { si_change(o, LV_STYLE_BORDER_WIDTH, nombre(v), s, false); }
void ui_style_bg_grad_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s) { si_change(o, LV_STYLE_BG_GRAD_COLOR, couleur(c), s, true); }
void ui_style_bg_grad_dir(lv_obj_t *o, lv_grad_dir_t v, lv_style_selector_t s) { si_change(o, LV_STYLE_BG_GRAD_DIR, nombre(v), s, false); }
void ui_style_shadow_width(lv_obj_t *o, lv_coord_t v, lv_style_selector_t s) { si_change(o, LV_STYLE_SHADOW_WIDTH, nombre(v), s, false); }
void ui_style_shadow_color(lv_obj_t *o, lv_color_t c, lv_style_selector_t s) { si_change(o, LV_STYLE_SHADOW_COLOR, couleur(c), s, true); }

void ui_img_src(lv_obj_t *img, const void *src)
{
    if (lv_img_get_src(img) != src)
        lv_img_set_src(img, src);
}

void ui_cacher(lv_obj_t *o, bool cache)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == cache)
        return;
    if (cache)
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// --- texte affichable (voir ui_theme.h) ---
static bool dans_polices(uint32_t c)
{
    return (c >= 32 && c <= 126) || (c >= 160 && c <= 255) || c == 8211 || c == 8212 || c == 8217 || c == 8230 ||
           c == 8722 || c == 8776 || (c >= 8592 && c <= 8595) || c == 8804 || c == 8805 || (c >= 0xF000 && c <= 0xF8FF);
}

static char equivalent(uint32_t c) // 0 : retiré
{
    // Latin étendu A (U+0100..U+017F) : la lettre de base (table générée par la décomposition Unicode)
    static const char LATIN_A[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
    if (c >= 0x100 && c <= 0x17F)
        return LATIN_A[c - 0x100];
    if (c >= 0x80 && c <= 0x9F) // Windows-1252 lu comme Latin-1
    {
        static const char CP1252[] = "E\0,f\".\0\0\0%S\0O\0Z\0\0''\"\"---~\0s\0o\0zY";
        return CP1252[c - 0x80];
    }
    static const struct
    {
        uint16_t c;
        char l;
    } CYR[] = {{0x410, 'A'}, {0x412, 'B'}, {0x415, 'E'}, {0x41A, 'K'}, {0x41C, 'M'}, {0x41D, 'H'}, {0x41E, 'O'}, {0x420, 'P'},
               {0x421, 'C'}, {0x422, 'T'}, {0x425, 'X'}, {0x430, 'a'}, {0x435, 'e'}, {0x43E, 'o'}, {0x440, 'p'}, {0x441, 'c'},
               {0x443, 'y'}, {0x445, 'x'}, {0x455, 's'}, {0x456, 'i'}, {0x458, 'j'}};
    for (auto &x : CYR)
        if (x.c == c)
            return x.l;
    if (c < 32 || c == 127)
        return 0;
    return '?';
}

void ui_texte_propre(char *s)
{
    if (!s)
        return;
    const uint8_t *r = (const uint8_t *)s;
    char *w = s;
    while (*r)
    {
        uint32_t c = *r;
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n > 0;
        for (int k = 1; ok && k < n; k++)
            ok = (r[k] & 0xC0) == 0x80; // octet de suite manquant (texte coupé) : séquence rejetée
        if (!ok)
        {
            r++;
            continue;
        }
        if (n > 1)
            c &= 0xFF >> (n + 1);
        for (int k = 1; k < n; k++)
            c = (c << 6) | (r[k] & 0x3F);
        if (dans_polices(c))
        {
            memmove(w, r, n);
            w += n;
        }
        else if (const char e = equivalent(c))
            *w++ = e;
        r += n;
    }
    *w = 0;
}

void ui_style_text_font(lv_obj_t *o, const lv_font_t *f, lv_style_selector_t s)
{
    lv_style_value_t a = {}; // même correction qu'à « si_change » : on compare à LV_STYLE_RES_FOUND, pas à LV_RES_OK
    if (lv_obj_get_local_style_prop(o, LV_STYLE_TEXT_FONT, &a, s) == LV_STYLE_RES_FOUND && a.ptr == f)
        return;
    lv_style_value_t v = {};
    v.ptr = f;
    lv_obj_set_local_style_prop(o, LV_STYLE_TEXT_FONT, v, s);
}

uint32_t ui_empreinte(const void *donnees, size_t n, uint32_t h)
{
    const uint8_t *p = (const uint8_t *)donnees;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

void ui_aligner(lv_obj_t *o, lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    if (lv_obj_get_style_align(o, LV_PART_MAIN) == align && lv_obj_get_style_x(o, LV_PART_MAIN) == x &&
        lv_obj_get_style_y(o, LV_PART_MAIN) == y)
        return;
    lv_obj_align(o, align, x, y);
}

void ui_label_texte(lv_obj_t *label, const char *texte)
{
    // 27/10 (audit des redessins) : une étiquette tronquée en « … » (LV_LABEL_LONG_DOT) porte les points de suspension
    // DANS son texte : la comparaison échouait toujours, elle était redessinée à chaque mise à jour ; on compare alors
    // l'empreinte du dernier texte demandé, gardée dans ses données utilisateur (inutilisées ailleurs)
    if (texte && lv_label_get_long_mode(label) == LV_LABEL_LONG_DOT)
    {
        const uintptr_t e = ui_empreinte(texte, strlen(texte)) | 1; // jamais nulle (nulle : pas encore posé)
        if ((uintptr_t)lv_obj_get_user_data(label) == e)
            return;
        lv_obj_set_user_data(label, (void *)e);
        lv_label_set_text(label, texte);
        return;
    }
    const char *actuel = lv_label_get_text(label);
    if (actuel && texte && strcmp(actuel, texte) == 0)
        return;
    lv_label_set_text(label, texte);
}

void ui_label_texte_fmt(lv_obj_t *label, const char *fmt, ...)
{
    char b[256];
    va_list va;
    va_start(va, fmt);
    const int n = vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    if (n < (int)sizeof b)
    {
        ui_label_texte(label, b);
        return;
    }
    char *grand = (char *)UI_ALLOC(n + 1); // texte long (rare) : formaté à sa taille
    va_start(va, fmt);
    vsnprintf(grand, n + 1, fmt, va);
    va_end(va);
    ui_label_texte(label, grand);
    UI_LIBERE(grand);
}

#if LVGL_VERSION_MAJOR >= 9
// 05/11/2026 (étape 2-vague E) : « lv_obj_t » est devenu un type opaque en 9.6 (include/lvgl/core/lv_obj.h
// déclare seulement « struct _lv_obj_t » ; les champs style_cnt/styles sont dans src/core/lv_obj_private.h,
// non public). Le garde-fou « ne pas ajouter deux fois la même feuille de style » n'a plus lieu d'être écrit
// ici : « lv_obj_add_style » retire d'abord la feuille pour être sûr qu'elle ne soit pas ajoutée deux fois
// (src/core/lv_obj_style.c:130-131, commentaire « Try removing the style first to be sure it won't be added
// twice ») et « lv_obj_remove_style » ne dit rien quand la feuille est absente (remove_style_core se tait si
// rien n'a été supprimé, src/core/lv_obj_style.c:1667-1669). Les deux appels sont donc déjà idempotents.
// Piège relevé : « lv_obj_get_style_enabled » (l'autre sonde publique) aurait rendu le même service mais
// appelle find_style, qui émet un LV_LOG_WARN quand la feuille est absente (src/core/lv_obj_style.c:1695) —
// avec LV_LOG_LEVEL = LV_LOG_LEVEL_WARN (conf 9.6 du banc), chaque premier ajout polluerait la console.
void ui_style_ajouter(lv_obj_t *o, lv_style_t *style, lv_style_selector_t s)
{
    lv_obj_add_style(o, style, s);
}
void ui_style_retirer(lv_obj_t *o, lv_style_t *style, lv_style_selector_t s)
{
    lv_obj_remove_style(o, style, s);
}
#else
static bool present(lv_obj_t *o, lv_style_t *style, lv_style_selector_t s)
{
    for (uint32_t i = 0; i < o->style_cnt; i++)
        if (o->styles[i].style == style && o->styles[i].selector == s)
            return true;
    return false;
}
void ui_style_ajouter(lv_obj_t *o, lv_style_t *style, lv_style_selector_t s)
{
    if (!present(o, style, s))
        lv_obj_add_style(o, style, s);
}
void ui_style_retirer(lv_obj_t *o, lv_style_t *style, lv_style_selector_t s)
{
    if (present(o, style, s))
        lv_obj_remove_style(o, style, s);
}
#endif

// --- Curseur dans une page qui défile (voir ui_theme.h) ---
#define CURSEUR_SEUIL 10 // px avant de décider : réglage (horizontal) ou défilement de la page (vertical)
static struct
{
    lv_obj_t *curseur;
    int depart;
    lv_point_t p0;
    int8_t sens; // 0 pas encore décidé, 1 horizontal (le curseur suit le doigt), -1 la page défile
} g_curseur;

static void curseur_cb(lv_event_t *e)
{
    lv_obj_t *s = (lv_obj_t *)lv_event_get_target(e); // 9.6 : renvoie « void* » (8.4 : « lv_obj_t* ») — le cast est neutre en 8.4
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (code == LV_EVENT_PRESSED)
    {
        g_curseur.curseur = s;
        g_curseur.depart = lv_slider_get_value(s);
        g_curseur.sens = 0;
        if (indev)
            lv_indev_get_point(indev, &g_curseur.p0);
        return;
    }
    if (g_curseur.curseur != s)
        return;
    if (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING)
    {
        if (g_curseur.sens == 0 && indev)
        {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            const int dx = LV_ABS(p.x - g_curseur.p0.x), dy = LV_ABS(p.y - g_curseur.p0.y);
            if (lv_indev_get_scroll_obj(indev) || (dy >= CURSEUR_SEUIL && dy > dx))
                g_curseur.sens = -1;
            else if (dx >= CURSEUR_SEUIL)
                g_curseur.sens = 1;
        }
        if (g_curseur.sens != 1) // pas (encore) un réglage : le curseur reste où il était, la page n'en sait rien
        {
            if (lv_slider_get_value(s) != g_curseur.depart)
                lv_slider_set_value(s, g_curseur.depart, LV_ANIM_OFF);
            lv_event_stop_processing(e);
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        if (g_curseur.sens != 1)
            lv_event_stop_processing(e);
        g_curseur.curseur = nullptr;
    }
}

void ui_curseur_dans_page(lv_obj_t *curseur)
{
    lv_obj_add_flag(curseur, LV_OBJ_FLAG_SCROLL_CHAIN_VER); // LVGL l'enlève aux curseurs : le glissé vertical va à la page
    lv_obj_add_event_cb(curseur, curseur_cb, LV_EVENT_ALL, nullptr);
}

lv_obj_t *ui_conteneur(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    // 31/10 (retour d'usage : « ça descend plus bas que nécessaire et ça rebondit ») : pas d'élastique, comme le rail (24/10) :
    // une liste rendue défilante s'arrête net sur son premier / dernier élément
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_ELASTIC);
    return o;
}

// 05/11/2026 : scroll par bande — statue à la création du conteneur (jamais en cours de geste : mesuré,
// une exclusion déclenchée pendant le geste ne défait pas les images déjà sautées — 84 images / 3,7 Mpx).
// Le style du conteneur lui-même doit être neutre : un fond, une bordure, un rayon, une ombre, un contour
// ou une barre de défilement dessinés dans l'aire restent en place pendant que le contenu défile, et le
// décalage les étirerait (cas « fond » et « barre » du prototype : 84 images / 3 712 609 px chacun).
bool ui_defilement_rapide(lv_obj_t *o)
{
    if (!o)
        return false;
    // 05/11 (, chambre 1 en rotation 0) : le décalage pousse des LIGNES de la mémoire d'image.
    // Garde explicites, tracées une fois (le helper statue à la création) : axe de défilement non vertical,
    // ou conteneur plus large que haut — le chemin rapide se refuse, le chemin d'origine tient.
    if (lv_obj_get_scroll_dir(o) != LV_DIR_VER) {
        ui_trace("[defilement rapide] refuse : axe de defilement non vertical (scroll_dir) — chemin d'origine\n");
        return false;
    }
    if (lv_obj_get_width(o) > lv_obj_get_height(o)) {
        ui_trace("[defilement rapide] refuse : conteneur plus large que haut — chemin d'origine\n");
        return false;
    }
    struct { const char *motif; bool refuse; } regles[] = {
        {"fond non transparent (bg_opa)", lv_obj_get_style_bg_opa(o, LV_PART_MAIN) > LV_OPA_TRANSP},
        {"bordure (border_width)", lv_obj_get_style_border_width(o, LV_PART_MAIN) > 0},
        {"rayon (radius)", lv_obj_get_style_radius(o, LV_PART_MAIN) > 0},
        {"ombre (shadow_width)", lv_obj_get_style_shadow_width(o, LV_PART_MAIN) > 0},
        {"contour (outline_width)", lv_obj_get_style_outline_width(o, LV_PART_MAIN) > 0},
        {"barre de défilement (scrollbar_mode)", lv_obj_get_scrollbar_mode(o) != LV_SCROLLBAR_MODE_OFF}, // v8 : pas un style, un réglage d'objet
    };
    for (const auto &r : regles)
        if (r.refuse) {
            ui_trace("[defilement rapide] refuse : %s — chemin d'origine\n", r.motif);
            return false;
        }
    lv_obj_add_flag(o, LV_OBJ_FLAG_USER_1); // la librairie (lv_obj_pos.c) ne regarde que ce drapeau
    return true;
}

void ui_onglets(lv_obj_t *parent, const char *const *noms, int n, int choisi, lv_event_cb_t cb, const int *donnees, lv_obj_t **boutons)
{
    const lv_coord_t l = n <= 2 ? 127 : n == 3 ? 101 : 88, larg = n * l + 8;
    lv_obj_t *sel = ui_conteneur(parent);
    lv_obj_add_style(sel, &ui_st_carte, 0);
    lv_obj_set_size(sel, larg, 40);
    lv_obj_set_pos(sel, (UI_ZONE_L - larg) / 2, 4);
    lv_obj_set_style_radius(sel, 12, 0);
    lv_obj_set_style_pad_all(sel, 3, 0);
    lv_obj_set_style_pad_column(sel, 0, 0);
    lv_obj_set_flex_flow(sel, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < n; i++)
    {
        lv_obj_t *b = ui_bouton(sel, l, 32);
        lv_obj_set_style_radius(b, 9, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(b, ui_c(UI_TEXTE2), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, ui_c(UI_CARTE2), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(b, ui_c(UI_TEXTE), LV_STATE_CHECKED);
        if (i == choisi)
            lv_obj_add_state(b, LV_STATE_CHECKED);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, noms[i]);
        lv_obj_center(t);
        if (boutons)
            boutons[i] = b;
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)(donnees ? donnees[i] : i));
    }
}

lv_obj_t *ui_valeur_unite(lv_obj_t *parent, const lv_font_t *police, uint32_t couleur, const char *unite, lv_obj_t **valeur)
{
    lv_obj_t *r = ui_conteneur(parent);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(r, 4, 0);
    *valeur = ui_label(r, police, couleur, "");
    const bool grand = lv_font_get_line_height(police) > 30;
    lv_obj_t *u = ui_label(r, grand ? UI_POLICE_TEXTE : UI_POLICE_PETITE, UI_TEXTE2, unite);
    lv_obj_set_style_pad_bottom(u, grand ? 3 : 1, 0); // sur la ligne de base des chiffres
    return r;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *police, uint32_t couleur, const char *texte)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, police, 0);
    lv_obj_set_style_text_color(l, ui_c(couleur), 0);
    lv_label_set_text(l, texte);
    return l;
}

lv_obj_t *ui_bouton(lv_obj_t *parent, lv_coord_t l, lv_coord_t h)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &ui_st_bouton, 0);
    lv_obj_add_style(b, &ui_st_bouton_presse, LV_STATE_PRESSED);
    lv_obj_add_style(b, &ui_st_pastille_active, LV_STATE_CHECKED);
    lv_obj_set_size(b, l, h);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

lv_obj_t *ui_icone(lv_obj_t *parent, const lv_img_dsc_t *src, uint32_t couleur)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(img, ui_c(couleur), 0);
    return img;
}

lv_obj_t *ui_titre_section(lv_obj_t *page, const char *txt)
{
    lv_obj_t *l = ui_label(page, UI_POLICE_LEGENDE, UI_TEXTE2, txt);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_set_style_pad_left(l, 4, 0);
    lv_obj_set_style_pad_top(l, 4, 0);
    return l;
}

static void helice_angle_cb(void *img, int32_t v) { lv_img_set_angle((lv_obj_t *)img, (int16_t)v); }

void ui_helice_arreter(lv_obj_t *img) { lv_anim_del(img, helice_angle_cb); }

void ui_helice_tourner(lv_obj_t *img, uint32_t tour_ms)
{
    lv_anim_del(img, helice_angle_cb);
    if (!tour_ms)
    {
        lv_img_set_angle(img, 0);
        return;
    }
    const int depart = lv_img_get_angle(img);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, helice_angle_cb);
    lv_anim_set_values(&a, depart, depart + 3600); // dixièmes de degré
    lv_anim_set_time(&a, tour_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

void ui_interrupteur_couleurs(lv_obj_t *sw)
{
    ui_style_bg_color(sw, ui_c(UI_ETEINT), LV_PART_MAIN);
    ui_style_bg_color(sw, ui_c(UI_VERT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    ui_style_bg_color(sw, ui_c(UI_POIGNEE), LV_PART_KNOB);
}
