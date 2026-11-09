#include "page_croquettes.h"
#include "ui_theme.h"
#include <stdio.h>

cq_etat_t cq_etat = {0, 0, 0, {{390, 15, true}, {750, 15, true}, {1170, 15, true}, {1320, 10, false}}, {}, 0, 0};
void (*cq_commande)(cq_action_t action, int idx) = nullptr;

static const char *JOURS_COURTS[] = {"Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim"};
static const char *JOURS_LONGS[] = {"Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche"};

#define BARRES_H 150 // hauteur utile des barres de la semaine (60 g en haut)
#define BARRES_MAX 60

static lv_obj_t *g_zone = nullptr;
static bool g_semaine = false;
static int g_edition = -1;   // repas dont on règle l'HEURE (les − / + changent alors l'heure, par 15 min)
static int g_jour_choisi = 0; // semaine : jour détaillé (0 = aujourd'hui)

// vue principale
static lv_obj_t *o_bol, *o_stock, *o_stock_auto, *o_stock_barre;
static lv_obj_t *o_inter[4], *o_heure[4], *o_heure_txt[4], *o_qte[4], *o_reglage[4];
// vue semaine
static lv_obj_t *o_moy, *o_ecart, *o_obj_txt, *o_barres[7], *o_pts[7], *o_points[7][4], *o_noms[7], *o_obj_ligne;
static lv_obj_t *o_det_titre, *o_det_repas, *o_det_ecart, *o_det_g;

static void construire();

static void envoyer(cq_action_t a, int idx)
{
    if (cq_commande)
        cq_commande(a, idx);
}

static int objectif()
{
    int s = 0;
    for (auto &r : cq_etat.repas)
        if (r.actif)
            s += r.g;
    return s;
}
static int repas_prevus()
{
    int n = 0;
    for (auto &r : cq_etat.repas)
        n += r.actif;
    return n;
}

static void fmt_g(char *b, size_t n, float g)
{
    snprintf(b, n, "%.1f", g);
    for (char *p = b; *p; p++)
        if (*p == '.')
            *p = ',';
}

static void rafraichir()
{
    if (!g_zone)
        return;
    char t[48];
    if (!g_semaine)
    {
        fmt_g(t, sizeof t, cq_etat.bol);
        ui_label_texte_fmt(o_bol, "%s", t);
        ui_label_texte_fmt(o_stock, "%d%%", cq_etat.stock);
        ui_label_texte_fmt(o_stock_auto, "≈ %d jours", cq_etat.autonomie);
        lv_obj_set_width(o_stock_barre, lv_pct(LV_CLAMP(0, cq_etat.stock, 100)));
        ui_style_bg_color(o_stock_barre, ui_c(cq_etat.stock < 15 ? UI_ROUGE : UI_ACCENT), 0);
        for (int i = 0; i < 4; i++)
        {
            const cq_repas_t &r = cq_etat.repas[i];
            if (r.actif)
                lv_obj_add_state(o_inter[i], LV_STATE_CHECKED);
            else
                lv_obj_clear_state(o_inter[i], LV_STATE_CHECKED);
            ui_style_opa(o_reglage[i], r.actif ? LV_OPA_COVER : LV_OPA_40, 0);
            ui_label_texte_fmt(o_heure_txt[i], "%02d:%02d", r.minute / 60, r.minute % 60);
            ui_label_texte_fmt(o_qte[i], "%d", r.g);
            // l'heure en réglage : bouton ambre ; la quantité s'efface un peu (les − / + agissent sur l'heure)
            bool ed = g_edition == i;
            ui_style_border_color(o_heure[i], ui_c(ed ? UI_ACCENT : UI_BORD), 0);
            ui_style_text_color(o_heure_txt[i], ui_c(ed ? UI_ACCENT : UI_TEXTE), 0);
            ui_style_text_color(o_qte[i], ui_c(ed ? UI_TEXTE3 : UI_TEXTE), 0);
        }
        return;
    }

    // --- semaine ---
    const int obj = objectif(), prevus = repas_prevus();
    int somme = 0;
    for (int k = 1; k < 7; k++) // les 6 jours complets (aujourd'hui n'est pas fini)
        somme += cq_etat.jours[k].g;
    const int moy = (somme + 3) / 6, ecart = moy - obj;
    ui_label_texte_fmt(o_moy, "%d", moy);
    ui_label_texte_fmt(o_obj_txt, "Objectif %d g / jour", obj);
    ui_label_texte_fmt(o_ecart, "%+d", ecart);
    ui_style_text_color(o_ecart, ui_c(LV_ABS(ecart) <= 3 ? UI_VERT : UI_CHAUFFE), 0);
    lv_obj_set_y(o_obj_ligne, BARRES_H - obj * BARRES_H / BARRES_MAX);

    for (int col = 0; col < 7; col++)
    {
        const int k = 6 - col; // colonne de gauche = il y a 6 jours, à droite = aujourd'hui
        const cq_jour_t &j = cq_etat.jours[k];
        const lv_coord_t h = LV_MAX(6, LV_MIN(j.g, BARRES_MAX) * BARRES_H / BARRES_MAX);
        lv_obj_set_height(o_barres[col], h);
        lv_obj_align(o_pts[col], LV_ALIGN_BOTTOM_MID, 0, -34 - h - 6); // points juste au-dessus de la barre
        ui_style_bg_color(o_barres[col], ui_c(k == 0 ? UI_ACCENT : UI_ETEINT), 0);
        lv_obj_set_style_outline_width(o_barres[col], k == g_jour_choisi ? 2 : 0, 0);
        ui_label_texte(o_noms[col], k == 0 ? "Auj." : JOURS_COURTS[(cq_etat.jour_semaine - k + 14) % 7]);
        ui_style_text_color(o_noms[col], ui_c(k == g_jour_choisi ? UI_TEXTE : UI_TEXTE2), 0);
        for (int p = 0; p < 4; p++)
        {
            if (p >= prevus)
            {
                ui_cacher(o_points[col][p], true);
                continue;
            }
            ui_cacher(o_points[col][p], false);
            // servi : vert ; manqué : rouge ; aujourd'hui pas encore servi : gris
            uint32_t c = p < j.repas ? UI_VERT : (k == 0 ? UI_ETEINT : UI_ROUGE);
            ui_style_bg_color(o_points[col][p], ui_c(c), 0);
        }
    }

    const cq_jour_t &d = cq_etat.jours[g_jour_choisi];
    const bool auj = g_jour_choisi == 0;
    if (auj)
        ui_label_texte(o_det_titre, "Aujourd'hui (en cours)");
    else
        ui_label_texte(o_det_titre, JOURS_LONGS[(cq_etat.jour_semaine - g_jour_choisi + 14) % 7]);
    // prochain repas actif après maintenant
    int prochain = -1;
    for (auto &r : cq_etat.repas)
        if (r.actif && r.minute > cq_etat.maintenant && (prochain < 0 || r.minute < prochain))
            prochain = r.minute;
    if (auj && prochain >= 0)
        snprintf(t, sizeof t, "%d repas servi%s sur %d · prochain à %dh%02d", d.repas, d.repas > 1 ? "s" : "", prevus,
                 prochain / 60, prochain % 60);
    else
        snprintf(t, sizeof t, "%d repas servi%s sur %d", d.repas, d.repas > 1 ? "s" : "", prevus);
    ui_label_texte(o_det_repas, t);
    const int ej = d.g - obj;
    if (auj)
        ui_label_texte_fmt(o_det_ecart, "Objectif du jour : %d g", obj);
    else if (ej == 0)
        ui_label_texte(o_det_ecart, "Pile l'objectif");
    else
        ui_label_texte_fmt(o_det_ecart, "%+d g par rapport à l'objectif", ej);
    ui_style_text_color(o_det_ecart, ui_c(auj || LV_ABS(ej) <= 3 ? UI_TEXTE2 : UI_CHAUFFE), 0);
    ui_label_texte_fmt(o_det_g, "%d", d.g);
}

void page_croquettes_maj() { rafraichir(); }

// --- Événements ---
static void inter_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    cq_etat.repas[i].actif = !cq_etat.repas[i].actif;
    g_edition = -1;
    rafraichir();
    envoyer(CQ_REPAS, i);
}

static void heure_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    g_edition = g_edition == i ? -1 : i; // un tap sur l'heure : les − / + règlent l'heure ; un 2e tap : retour
    rafraichir();
}

static void pas_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e); // repas * 2 + (0 = moins, 1 = plus)
    int i = code / 2, sens = code % 2 ? 1 : -1;
    cq_repas_t &r = cq_etat.repas[i];
    if (!r.actif)
        return;
    if (g_edition == i)
        r.minute = (r.minute + sens * 15 + 1440) % 1440;
    else
        r.g = LV_CLAMP(0, r.g + sens, 99);
    rafraichir();
    envoyer(CQ_REPAS, i);
}

static void action_cb(lv_event_t *e)
{
    int a = (int)(intptr_t)lv_event_get_user_data(e);
    if (a == 2) // Semaine
    {
        g_semaine = true;
        g_jour_choisi = 0;
        construire();
        return;
    }
    envoyer(a == 0 ? CQ_SERVIR : CQ_TARE, 0);
}

static void retour_cb(lv_event_t *)
{
    g_semaine = false;
    construire();
}

static void jour_cb(lv_event_t *e)
{
    g_jour_choisi = (int)(intptr_t)lv_event_get_user_data(e);
    rafraichir();
}

// --- Construction ---
static lv_obj_t *tuile(lv_obj_t *zone, lv_coord_t x, lv_coord_t y, lv_coord_t l, lv_coord_t h)
{
    lv_obj_t *c = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, l, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 8, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(c, 2, 0);
    return c;
}

static lv_obj_t *petit_bouton(lv_obj_t *parent, lv_coord_t l, const char *txt, const lv_font_t *police)
{
    lv_obj_t *b = ui_bouton(parent, l, 44);
    lv_obj_set_style_radius(b, 12, 0);
    ui_style_bg_color(b, ui_c(UI_CARTE2), 0);
    lv_obj_t *t = ui_label(b, police, UI_TEXTE, txt);
    lv_obj_center(t);
    return b;
}

static void construire_principale(lv_obj_t *zone)
{
    lv_obj_t *titre = ui_label(zone, UI_POLICE_VALEUR, UI_TEXTE, "Croquettes");
    lv_obj_align(titre, LV_ALIGN_TOP_MID, 0, 2);

    const lv_coord_t demi = (UI_ZONE_L - 2 * UI_MARGE - 12) / 2;
    lv_obj_t *c = tuile(zone, UI_MARGE, 50, demi, 70);
    ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "Bol");
    ui_valeur_unite(c, UI_POLICE_3X5_M, UI_TEXTE, "g", &o_bol);

    c = tuile(zone, UI_MARGE + demi + 12, 50, demi, 70);
    lv_obj_t *r = ui_conteneur(c);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    ui_label(r, UI_POLICE_LEGENDE, UI_TEXTE2, "Stock");
    o_stock_auto = ui_label(r, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_align(o_stock_auto, LV_ALIGN_TOP_RIGHT, 0, 0);
    o_stock = ui_label(c, UI_POLICE_3X5_M, UI_TEXTE, "");
    lv_obj_t *fond = ui_conteneur(c);
    lv_obj_set_size(fond, lv_pct(100), 4);
    lv_obj_set_style_radius(fond, 2, 0);
    ui_style_bg_color(fond, ui_c(UI_BORD), 0);
    ui_style_bg_opa(fond, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(fond, true, 0);
    o_stock_barre = ui_conteneur(fond);
    lv_obj_set_height(o_stock_barre, 4);
    ui_style_bg_opa(o_stock_barre, LV_OPA_COVER, 0);

    // 4 repas : interrupteur | heure | − quantité +
    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *l = ui_conteneur(zone);
        ui_style_ajouter(l, &ui_st_carte, 0);
        lv_obj_set_size(l, UI_ZONE_L - 2 * UI_MARGE, 62);
        lv_obj_set_pos(l, UI_MARGE, 132 + i * 68);
        lv_obj_set_style_pad_hor(l, 10, 0);
        lv_obj_set_style_pad_ver(l, 0, 0);
        lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(l, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(l, 8, 0);

        o_inter[i] = lv_switch_create(l);
        lv_obj_set_size(o_inter[i], 52, 30);
        ui_interrupteur_couleurs(o_inter[i]);
        lv_obj_add_event_cb(o_inter[i], inter_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        o_reglage[i] = ui_conteneur(l);
        lv_obj_set_size(o_reglage[i], 290, 44);
        lv_obj_set_flex_flow(o_reglage[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(o_reglage[i], LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        o_heure[i] = petit_bouton(o_reglage[i], 104, "", UI_POLICE_3X5_P);
        o_heure_txt[i] = lv_obj_get_child(o_heure[i], 0);
        lv_obj_add_event_cb(o_heure[i], heure_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *m = petit_bouton(o_reglage[i], 44, "−", UI_POLICE_TITRE);
        lv_obj_add_event_cb(m, pas_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 2));
        lv_obj_t *q = ui_valeur_unite(o_reglage[i], UI_POLICE_3X5_P, UI_TEXTE, "g", &o_qte[i]);
        lv_obj_set_width(q, 66);
        lv_obj_set_flex_align(q, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_t *p = petit_bouton(o_reglage[i], 44, "+", UI_POLICE_TITRE);
        lv_obj_add_event_cb(p, pas_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 2 + 1));
    }

    // Servir | Tare | Semaine
    lv_obj_t *rang = ui_conteneur(zone);
    lv_obj_set_size(rang, UI_ZONE_L - 2 * UI_MARGE, 52);
    lv_obj_set_pos(rang, UI_MARGE, 412);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *noms[] = {"Servir", "Tare", "Semaine"};
    for (int a = 0; a < 3; a++)
    {
        lv_obj_t *b = ui_bouton(rang, 121, 52);
        if (a == 0)
            ui_style_ajouter(b, &ui_st_bouton_principal, 0);
        lv_obj_t *t = lv_label_create(b);
        ui_label_texte(t, noms[a]);
        lv_obj_center(t);
        lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)a);
    }
}

static void construire_semaine(lv_obj_t *zone)
{
    lv_obj_t *ret = ui_bouton(zone, 56, 44);
    ui_style_bg_opa(ret, LV_OPA_TRANSP, 0);
    ui_style_border_width(ret, 0, 0);
    lv_obj_set_pos(ret, 8, 0);
    lv_obj_center(ui_label(ret, UI_POLICE_VALEUR, UI_TEXTE2, LV_SYMBOL_LEFT));
    lv_obj_add_event_cb(ret, retour_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *titre = ui_label(zone, UI_POLICE_VALEUR, UI_TEXTE, "Semaine");
    lv_obj_align(titre, LV_ALIGN_TOP_MID, 0, 2);

    const lv_coord_t demi = (UI_ZONE_L - 2 * UI_MARGE - 12) / 2;
    lv_obj_t *c = tuile(zone, UI_MARGE, 50, demi, 64);
    ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "Moyenne par jour");
    ui_valeur_unite(c, UI_POLICE_3X5_M, UI_TEXTE, "g", &o_moy);
    c = tuile(zone, UI_MARGE + demi + 12, 50, demi, 64);
    o_obj_txt = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    ui_valeur_unite(c, UI_POLICE_3X5_M, UI_TEXTE, "g", &o_ecart);

    // barres : zone de 380 × 220 (points, barre, nom du jour)
    lv_obj_t *g = ui_conteneur(zone);
    lv_obj_set_size(g, UI_ZONE_L - 2 * UI_MARGE, 220);
    lv_obj_set_pos(g, UI_MARGE, 126);
    lv_obj_t *haut = ui_conteneur(g); // repère des barres : bas des barres à BARRES_H
    lv_obj_set_size(haut, lv_pct(100), BARRES_H);
    lv_obj_set_pos(haut, 0, 220 - 34 - BARRES_H);
    o_obj_ligne = ui_conteneur(haut); // ligne d'objectif (pointillés : petits traits)
    lv_obj_set_size(o_obj_ligne, lv_pct(100), 2);
    lv_obj_set_flex_flow(o_obj_ligne, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(o_obj_ligne, 5, 0);
    for (int k = 0; k < 38; k++)
    {
        lv_obj_t *trait = ui_conteneur(o_obj_ligne);
        lv_obj_set_size(trait, 5, 2);
        ui_style_bg_color(trait, ui_c(UI_TEXTE3), 0);
        ui_style_bg_opa(trait, LV_OPA_COVER, 0);
    }
    const lv_coord_t col_l = (UI_ZONE_L - 2 * UI_MARGE - 6 * 10) / 7;
    for (int col = 0; col < 7; col++)
    {
        lv_obj_t *b = lv_btn_create(g); // toute la colonne choisit le jour
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, col_l, 220);
        lv_obj_set_pos(b, col * (col_l + 10), 0);
        lv_obj_add_event_cb(b, jour_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(6 - col));

        o_noms[col] = ui_label(b, UI_POLICE_PETITE, UI_TEXTE2, "");
        lv_obj_align(o_noms[col], LV_ALIGN_BOTTOM_MID, 0, -6);
        o_barres[col] = ui_conteneur(b);
        lv_obj_clear_flag(o_barres[col], LV_OBJ_FLAG_CLICKABLE); // sinon la barre garde le toucher pour elle
        lv_obj_set_width(o_barres[col], col_l);
        lv_obj_set_style_radius(o_barres[col], 6, 0);
        ui_style_bg_opa(o_barres[col], LV_OPA_COVER, 0);
        lv_obj_set_style_outline_color(o_barres[col], ui_c(UI_TEXTE), 0);
        lv_obj_set_style_outline_pad(o_barres[col], 2, 0);
        lv_obj_align(o_barres[col], LV_ALIGN_BOTTOM_MID, 0, -34);
        o_pts[col] = ui_conteneur(b);
        lv_obj_clear_flag(o_pts[col], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(o_pts[col], LV_SIZE_CONTENT, 7);
        lv_obj_set_flex_flow(o_pts[col], LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(o_pts[col], 3, 0);
        for (int p = 0; p < 4; p++)
        {
            o_points[col][p] = ui_conteneur(o_pts[col]);
            lv_obj_set_size(o_points[col][p], 7, 7);
            lv_obj_set_style_radius(o_points[col][p], LV_RADIUS_CIRCLE, 0);
            ui_style_bg_opa(o_points[col][p], LV_OPA_COVER, 0);
        }
    }

    // détail du jour choisi
    c = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 102);
    lv_obj_set_pos(c, UI_MARGE, 362);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_t *col = ui_conteneur(c);
    lv_obj_set_size(col, 260, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    o_det_titre = ui_label(col, UI_POLICE_TEXTE, UI_TEXTE, "");
    o_det_repas = ui_label(col, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    o_det_ecart = ui_label(col, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_align(ui_valeur_unite(c, UI_POLICE_TEMP_PETITE, UI_TEXTE, "g", &o_det_g), LV_ALIGN_RIGHT_MID, 0, 0);
}

// la page a deux vues : on vide la zone et on reconstruit celle demandée
static void construire()
{
    lv_obj_clean(g_zone);
    g_edition = -1;
    if (g_semaine)
        construire_semaine(g_zone);
    else
        construire_principale(g_zone);
    rafraichir();
}

void page_croquettes_creer(lv_obj_t *zone)
{
    g_zone = zone;
    g_semaine = false;
    construire();
}

void page_croquettes_quitter()
{
    g_zone = nullptr; // rafraichir() ne touche plus à rien
}
