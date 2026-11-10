#include "page_ouvrants.h"
#include "ui_theme.h"
#include "ui_courbe.h" // 05/11 (vague D) : wrappers uc_* — dessin des stores sous 9.6
#include "ui_diag.h"

ov_ouvrant_t ov_ouvrants[OV_NB] = {{TXT("Store terrasse", "Patio blind"), 0, true}, {TXT("Volet cuisine", "Shutter"), 100, false}};
int ov_toile = 100;
bool ov_firetv = false;
int ov_toile_mouvement = 0;
void (*ov_commande)(int idx, ov_action_t action, int pos) = nullptr;

#define CARTE_H 272
#define STORE_L 92
#define STORE_H 150
#define TOILE_Y 332

// Pointeurs valides seulement quand la page est affichée (remis à nullptr par page_ouvrants_quitter)
static lv_obj_t *o_titre, *o_pct[OV_NB], *o_store[OV_NB];
static lv_obj_t *o_toile, *o_toile_etat, *o_seance, *o_seance_txt;
static int g_glisse = -1; // ouvrant en cours de réglage au doigt

static void envoyer(int idx, ov_action_t a, int pos)
{
    if (ov_commande)
        ov_commande(idx, a, pos);
}

// part déroulée depuis le haut, en % : volet = partie fermée, store banne = partie sortie
static int deroule(int i) { return ov_ouvrants[i].banne ? ov_ouvrants[i].pos : 100 - ov_ouvrants[i].pos; }

// --- curseur vertical façon store : volet = lames grises depuis le haut ; store banne = caisson + toile rayée qui
// se déroule ; poignée ambre au bas de la partie déroulée ---
static void store_dessin_cb(lv_event_t *e)
{
    ui_diag_point("dessin store");
    uc_cible_t * ctx = uc_cible_du(e); // 9.6 : la layer de l'événement (avant : lv_event_get_draw_ctx)
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const int ferme = STORE_H * deroule(i) / 100;

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = ui_c(UI_ETEINT);
    if (ov_ouvrants[i].banne)
    {
        for (int x = 0; x < STORE_L; x += 12) // bandes verticales de toile, 6 px claires / 6 px sombres
        {
            d.bg_color = ui_c((x / 12) % 2 ? 0x2C333C : UI_ETEINT);
            lv_area_t l = {(lv_coord_t)(a.x1 + x), a.y1, (lv_coord_t)(a.x1 + LV_MIN(x + 11, STORE_L - 1)), (lv_coord_t)(a.y1 + ferme - 1)};
            if (ferme > 0)
                uc_carre(ctx, d, &l); // NOTE coût 9.6 : une task par bande (8 bandes + poignée), ajout en O(n)
        }
        d.bg_color = ui_c(0x4A5360);
        lv_area_t caisson = {a.x1, a.y1, a.x2, (lv_coord_t)(a.y1 + 5)};
        uc_carre(ctx, d, &caisson);
    }
    else
        for (int y = 0; y < ferme; y += 12) // lames de 10 px, jour de 2 px
        {
            lv_area_t l = {a.x1, (lv_coord_t)(a.y1 + y), a.x2, (lv_coord_t)(a.y1 + LV_MIN(y + 9, ferme - 1))};
            uc_carre(ctx, d, &l);
        }
    d.bg_color = ui_c(g_glisse == i ? UI_TEXTE : UI_ACCENT);
    d.radius = 3;
    lv_coord_t py = (lv_coord_t)LV_CLAMP(4, ferme - 3, STORE_H - 10);
    lv_area_t p = {(lv_coord_t)(a.x1 + 26), (lv_coord_t)(a.y1 + py), (lv_coord_t)(a.x2 - 26), (lv_coord_t)(a.y1 + py + 5)};
    uc_carre(ctx, d, &p);
    ui_diag_point("rendu");
}

static void toile_dessin_cb(lv_event_t *e)
{
    uc_cible_t * ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = ui_c(UI_ETEINT); // caisson
    d.radius = 3;
    lv_area_t caisson = {a.x1, a.y1, a.x2, (lv_coord_t)(a.y1 + 5)};
    uc_carre(ctx, d, &caisson);
    const int h = (lv_area_get_height(&a) - 6) * (100 - ov_toile) / 100; // partie déroulée
    if (h > 0)
    {
        d.bg_color = ui_c(UI_TEXTE);
        d.radius = 1;
        lv_area_t t = {(lv_coord_t)(a.x1 + 4), (lv_coord_t)(a.y1 + 6), (lv_coord_t)(a.x2 - 4), (lv_coord_t)(a.y1 + 5 + h)};
        uc_carre(ctx, d, &t);
    }
    d.bg_color = ui_c(UI_TEXTE3); // barre de lestage
    d.radius = 1;
    lv_area_t b = {(lv_coord_t)(a.x1 + 2), (lv_coord_t)(a.y1 + 6 + h), (lv_coord_t)(a.x2 - 2), (lv_coord_t)(a.y1 + 8 + h)};
    uc_carre(ctx, d, &b);
}

static void rafraichir()
{
    if (!o_titre)
        return;
    // 27/10 (audit des redessins) : un dessin n'est refait que si ce qu'il montre change (tous les dessins l'étaient à
    // chaque message d'un seul ouvrant) ; à la construction de la page, LVGL les dessine de toute façon
    static int dessine[OV_NB], toile_dessinee = -1;
    static bool glisse_dessine[OV_NB];
    for (int i = 0; i < OV_NB; i++)
    {
        ui_label_texte_fmt(o_pct[i], "%d%%", ov_ouvrants[i].pos);
        ui_style_text_color(o_pct[i], ui_c(g_glisse == i ? UI_ACCENT : UI_TEXTE), 0);
        if (deroule(i) != dessine[i] || (g_glisse == i) != glisse_dessine[i])
        {
            dessine[i] = deroule(i);
            glisse_dessine[i] = g_glisse == i;
            lv_obj_invalidate(o_store[i]);
        }
    }
    if (ov_toile != toile_dessinee)
    {
        toile_dessinee = ov_toile;
        lv_obj_invalidate(o_toile);
    }
    // mots courts : « Baissée à 40 % » débordait sous les boutons (vu au simulateur, 22/10) ; la toile n'est entre
    // deux positions que pendant son mouvement, le dessin à gauche montre où elle en est
    if (ov_toile_mouvement)
        ui_label_texte(o_toile_etat, ov_toile_mouvement > 0 ? TXT("Remonte", "Raising") : TXT("Descend", "Lowering"));
    else if (ov_toile >= 100)
        ui_label_texte(o_toile_etat, TXT("Levée", "Up"));
    else if (ov_toile <= 0)
        ui_label_texte(o_toile_etat, TXT("Baissée", "Down"));
    else
        ui_label_texte(o_toile_etat, TXT("Arrêtée", "Stopped"));
    ui_style_text_color(o_toile_etat, ui_c(ov_toile >= 100 ? UI_TEXTE2 : UI_TEXTE), 0);

    // séance : bouton principal (ambre) pour lancer, secondaire pour finir
    ui_label_texte(o_seance_txt, ov_firetv ? LV_SYMBOL_STOP "  " TXT("Fin de séance : éteindre la Fire TV", "End Movie: turn off Fire TV")
                                              : LV_SYMBOL_PLAY "  " TXT("Séance : allumer la Fire TV", "Movie: turn on Fire TV"));
    ui_style_text_color(o_seance_txt, ui_c(ov_firetv ? UI_TEXTE : UI_SUR_ACCENT), 0);
    // 24/10 : gardé (ui_style_*) ; retiré / remis à chaque appel, donc à chaque mouvement du doigt sur un store, il
    // faisait redessiner tout le bouton à chaque image : c'est ce qui déclenchait le figeage de l'écran de la cuisine
    if (ov_firetv)
        ui_style_retirer(o_seance, &ui_st_bouton_principal, 0);
    else
        ui_style_ajouter(o_seance, &ui_st_bouton_principal, 0);
}

void page_ouvrants_maj() { rafraichir(); }

static void store_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING)
    {
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        lv_area_t a;
        lv_obj_get_coords(o_store[i], &a);
        int f = LV_CLAMP(0, (p.y - a.y1) * 100 / STORE_H, 100);
        const int pos = ((ov_ouvrants[i].banne ? f : 100 - f) + 2) / 5 * 5; // pas de 5 %
        // 24/10 : pendant le glissé, seulement CE store et son pourcentage, et rien si la valeur n'a pas changé
        // (rafraichir() refaisait toute la page à chaque mouvement du doigt)
        if (pos != ov_ouvrants[i].pos || g_glisse != i)
        {
            ov_ouvrants[i].pos = pos;
            g_glisse = i;
            ui_label_texte_fmt(o_pct[i], "%d%%", pos);
            ui_style_text_color(o_pct[i], ui_c(UI_ACCENT), 0);
            lv_obj_invalidate(o_store[i]);
        }
        return;
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        g_glisse = -1;
        envoyer(i, OV_POSITION, ov_ouvrants[i].pos);
    }
    else
        return;
    rafraichir();
}

static void bouton_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e); // idx * 3 + action
    int idx = code / 3;
    ov_action_t a = (ov_action_t)(code % 3);
    if (idx < OV_NB && a != OV_STOP)
        ov_ouvrants[idx].pos = a == OV_OUVRIR ? 100 : 0; // affichage immédiat ; HA confirmera la position
    if (idx == OV_NB && a != OV_STOP)
        ov_toile = a == OV_OUVRIR ? 100 : 0;
    rafraichir();
    envoyer(idx, a, 0);
}

static void seance_cb(lv_event_t *)
{
    ov_firetv = !ov_firetv; // la toile, elle, sera bougée par l'automatisation de HA
    rafraichir();
    envoyer(OV_NB, OV_SEANCE, ov_firetv ? 1 : 0);
}

static void trois_boutons(lv_obj_t *parent, int idx, lv_coord_t l, lv_coord_t h)
{
    static const char *SYMB[] = {LV_SYMBOL_UP, LV_SYMBOL_STOP, LV_SYMBOL_DOWN};
    for (int a = 0; a < 3; a++)
    {
        lv_obj_t *b = ui_bouton(parent, l, h);
        ui_style_bg_color(b, ui_c(UI_CARTE2), 0);
        lv_obj_t *t = ui_label(b, UI_POLICE_TEXTE, UI_TEXTE, SYMB[a]);
        lv_obj_center(t);
        // store banne : la flèche du bas le sort (la toile descend à l'écran), celle du haut le rentre
        int action = idx < OV_NB && ov_ouvrants[idx].banne ? 2 - a : a;
        lv_obj_add_event_cb(b, bouton_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(idx * 3 + action));
    }
}

void page_ouvrants_creer(lv_obj_t *zone)
{
    o_titre = ui_label(zone, UI_POLICE_VALEUR, UI_TEXTE, TXT("Ouvrants", "Covers"));
    lv_obj_align(o_titre, LV_ALIGN_TOP_MID, 0, 2);

    const lv_coord_t carte_l = (UI_ZONE_L - 2 * UI_MARGE - 12) / 2;
    for (int i = 0; i < OV_NB; i++)
    {
        lv_obj_t *c = ui_conteneur(zone);
        ui_style_ajouter(c, &ui_st_carte, 0);
        lv_obj_set_size(c, carte_l, CARTE_H);
        lv_obj_set_pos(c, UI_MARGE + i * (carte_l + 12), 50);
        lv_obj_set_style_pad_all(c, 10, 0);

        ui_label(c, UI_POLICE_PETITE, UI_TEXTE, ov_ouvrants[i].nom); // 14 px : le nom et le % tiennent sur 164 px
        // pourcentage en PETITE police pixel : la moyenne (37 px) chevauchait le nom, vu au banc le 24/10
        o_pct[i] = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "");
        lv_obj_align(o_pct[i], LV_ALIGN_TOP_RIGHT, 0, 0);

        o_store[i] = ui_conteneur(c);
        lv_obj_set_size(o_store[i], STORE_L, STORE_H);
        lv_obj_align(o_store[i], LV_ALIGN_TOP_MID, 0, 32);
        ui_style_bg_color(o_store[i], ui_c(UI_FOND), 0);
        ui_style_bg_opa(o_store[i], LV_OPA_COVER, 0);
        ui_style_border_color(o_store[i], ui_c(UI_BORD), 0);
        ui_style_border_width(o_store[i], 1, 0);
        lv_obj_set_style_radius(o_store[i], 12, 0);
        lv_obj_set_style_clip_corner(o_store[i], true, 0);
        lv_obj_set_style_border_post(o_store[i], true, 0); // bordure dessinée APRÈS l'enfant (comme le ballon, 1ce763d)
        lv_obj_add_flag(o_store[i], LV_OBJ_FLAG_CLICKABLE);
        // 06/11 (audit 9.6, chantier 3) : le dessin passe dans un ENFANT du cadre arrondi, patron éprouvé
        // de la cuve du ballon (1ce763d). En 8.4, clip_corner posait un masque rayonné au DRAW_MAIN par
        // défaut (8.4 core/lv_obj.c:554-566), retiré au DRAW_POST (:588-594), et les callbacks normaux
        // tournent après le défaut (8.4 core/lv_event.c:452-470) : store_dessin_cb était rogné. En 9.6,
        // clip_corner ne rogne plus que les ENFANTS (9.6 core/lv_refr.c:171-263) : le caisson et les
        // lames peignaient les angles en carré (mesuré capture avant, .banc_v9_esp/grandes_repeintes/
        // cc_avant_ouvrants.raw : à (58,93) et (149,93) obtenu (74,80,98) = caisson, attendu (24,32,32)
        // = fond de carte ; à (254,242) et (345,242) obtenu (57,64,74) = lames). L'enfant est rogné dans
        // les DEUX lignes. Il n'est pas cliquable : le contact — et store_cb, qui commande le store
        // réel — restent sur le cadre, inchangés.
        lv_obj_t *store_grad = ui_conteneur(o_store[i]);
        lv_obj_set_size(store_grad, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(store_grad, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(store_grad, store_dessin_cb, LV_EVENT_DRAW_MAIN_END, (void *)(intptr_t)i);
        lv_obj_add_event_cb(o_store[i], store_cb, LV_EVENT_ALL, (void *)(intptr_t)i);

        lv_obj_t *rang = ui_conteneur(c);
        lv_obj_set_size(rang, lv_pct(100), 52);
        lv_obj_align(rang, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        trois_boutons(rang, i, (carte_l - 20 - 12) / 3, 52);
    }

    // toile du vidéoprojecteur
    lv_obj_t *c = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 136);
    lv_obj_set_pos(c, UI_MARGE, TOILE_Y);
    lv_obj_set_style_pad_all(c, 10, 0);

    o_toile = ui_conteneur(c);
    lv_obj_set_size(o_toile, 104, 62);
    lv_obj_set_pos(o_toile, 2, 2);
    lv_obj_add_event_cb(o_toile, toile_dessin_cb, LV_EVENT_DRAW_MAIN, NULL);

    lv_obj_t *txt = ui_conteneur(c);
    lv_obj_set_size(txt, 110, LV_SIZE_CONTENT);
    lv_obj_set_pos(txt, 118, 2);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    ui_label(txt, UI_POLICE_TEXTE, UI_TEXTE, TXT("Toile", "Screen"));
    o_toile_etat = ui_label(txt, UI_POLICE_TITRE, UI_TEXTE, "");
    ui_label(txt, UI_POLICE_LEGENDE, UI_TEXTE3, TXT("suit la Fire TV", "follows Fire TV"));

    lv_obj_t *rang = ui_conteneur(c);
    lv_obj_set_size(rang, 3 * 44 + 2 * 6, 44);
    lv_obj_align(rang, LV_ALIGN_TOP_RIGHT, 0, 10);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    trois_boutons(rang, OV_NB, 44, 44);

    o_seance = ui_bouton(c, lv_pct(100), 40);
    lv_obj_align(o_seance, LV_ALIGN_BOTTOM_MID, 0, 0);
    o_seance_txt = ui_label(o_seance, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_obj_center(o_seance_txt);
    lv_obj_add_event_cb(o_seance, seance_cb, LV_EVENT_CLICKED, NULL);

    rafraichir();
}

void page_ouvrants_quitter()
{
    o_titre = nullptr; // rafraichir() ne touche plus à rien
    g_glisse = -1;
}
