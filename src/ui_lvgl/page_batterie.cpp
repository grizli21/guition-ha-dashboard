#include "page_batterie.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "page_radar.h"
#include "ui_theme.h"
#include "ui_courbe.h" // 05/11 (vague D) : wrappers uc_* — dessin commun sous 9.6 (layer, pas lv_draw_ctx_t)
#include <stdio.h>
#include <string.h>
#include <time.h>

UI_PSRAM(bt_etat_t, bt_etat_p, 1, nullptr); // 29/10 : en PSRAM (ui_psram.h)
bt_reglages_t bt_reglages = {false, 10, false};
void (*bt_regler)(const char *quoi) = nullptr;

// 31/10 : trois onglets (Batterie, Radar, Réglages) ; g_zone n'existe que pendant l'onglet Batterie
static int g_vue = 0;
static lv_obj_t *o_onglets[3];
static lv_obj_t *g_zone, *o_pile, *o_pct, *o_tension, *o_etat, *o_depuis, *o_resume, *o_graphe, *o_cause, *o_chrono;
static uint32_t g_chrono_empreinte;
// onglet Réglages
static lv_obj_t *o_reglages, *o_diag, *o_diag_txt, *o_coupure, *o_economie, *o_puissance, *o_marge;
static int g_pile_pct = -1; // ce que montre la pile (redessinée seulement si ça change)
static bool g_pile_charge;
static uint32_t g_graphe_empreinte;
static const uint32_t COUL_CAUSE[] = {UI_VERT, UI_ACCENT, UI_TEXTE3, UI_FROID};

static uint32_t couleur_batterie() { return bt_etat.charge || bt_etat.pourcent > 30 ? UI_VERT : bt_etat.pourcent > 15 ? UI_ACCENT : UI_ROUGE; }

void bt_icone(int &niveau, uint32_t &couleur)
{
    const int p = bt_etat.pourcent;
    niveau = p < 15 ? 0 : p < 40 ? 1 : p < 70 ? 2 : 3;
    couleur = bt_etat.charge ? UI_VERT : p < 15 ? UI_ROUGE : 0;
}

// « 1 j 19 h », « 3 h 05 », « 35 min »
static void duree_texte(char *b, size_t n, uint32_t s)
{
    if (s >= 86400)
        snprintf(b, n, TXT("%u j %u h", "%u d %u h"), (unsigned)(s / 86400), (unsigned)(s % 86400 / 3600));
    else if (s >= 3600)
        snprintf(b, n, "%u h %02u", (unsigned)(s / 3600), (unsigned)(s % 3600 / 60));
    else
        snprintf(b, n, "%u min", (unsigned)(s / 60));
}

// ---------------------------------------------------------------------------------------------------- pile
static void pile_cb(lv_event_t *e)
{
    uc_cible_t * ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 10;
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = ui_c(UI_TEXTE2);
    d.border_width = 3;
    const lv_area_t corps = {a.x1, a.y1, (lv_coord_t)(a.x2 - 9), a.y2};
    uc_carre(ctx, d, &corps);
    lv_draw_rect_dsc_t t;
    lv_draw_rect_dsc_init(&t);
    t.radius = 3;
    t.bg_color = ui_c(UI_TEXTE2);
    const lv_area_t teton = {(lv_coord_t)(a.x2 - 7), (lv_coord_t)(a.y1 + 15), a.x2, (lv_coord_t)(a.y2 - 15)};
    uc_carre(ctx, t, &teton);
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = 6;
    r.bg_color = ui_c(couleur_batterie());
    const lv_coord_t l = lv_area_get_width(&corps) - 14;
    const lv_area_t plein = {(lv_coord_t)(corps.x1 + 7), (lv_coord_t)(corps.y1 + 7),
                             (lv_coord_t)(corps.x1 + 7 + l * LV_CLAMP(0, bt_etat.pourcent, 100) / 100), (lv_coord_t)(corps.y2 - 7)};
    if (bt_etat.pourcent > 0)
        uc_carre(ctx, r, &plein);
    if (bt_etat.charge)
    {
        lv_draw_label_dsc_t ld;
        lv_draw_label_dsc_init(&ld);
        ld.font = UI_POLICE_TITRE;
        ld.color = ui_c(UI_FOND);
        ld.align = LV_TEXT_ALIGN_CENTER;
        const lv_area_t z = {corps.x1, (lv_coord_t)(corps.y1 + 13), corps.x2, corps.y2};
        uc_texte(ctx, ld, &z, LV_SYMBOL_CHARGE, false); // littéral : pas besoin de copier le texte en 9.6
    }
}

// ---------------------------------------------------------------------------------------------------- réveils 24 h
static void eveils_cb(lv_event_t *e)
{
    uc_cible_t * ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const lv_coord_t x0 = a.x1 + 40, x1 = a.x2 - 4, haut = a.y1 + 6, bas = a.y1 + 70, bande0 = bas + 6, bande1 = bas + 30;
    const uint32_t fin = (uint32_t)time(nullptr), debut = fin - 86400;
    if (fin < 1700000000u) // heure pas encore reçue de HA
        return;
    auto x = [&](uint32_t t) { return (lv_coord_t)(x0 + (int64_t)((int64_t)t - debut) * (x1 - x0) / 86400); };
    lv_draw_line_dsc_t gl;
    lv_draw_line_dsc_init(&gl);
    gl.color = ui_c(0x1F252D);
    gl.width = 1;
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = UI_POLICE_LEGENDE;
    ld.color = ui_c(UI_TEXTE3);
    for (int v = 0; v <= 100; v += 50) // échelle de la batterie (%), de 0 à 100
    {
        const lv_coord_t y = bas - (lv_coord_t)(v / 100.0f * (bas - haut));
        uc_segment(ctx, gl, x0, y, x1, y); // 9.6 : p1/p2 passent dans le descripteur (uc_segment)
        char s[8];
        snprintf(s, sizeof s, "%d%%", v);
        const lv_area_t z = {a.x1, (lv_coord_t)(y - 7), (lv_coord_t)(x0 - 4), (lv_coord_t)(y + 7)};
        ld.align = LV_TEXT_ALIGN_RIGHT;
        uc_texte(ctx, ld, &z, s, true); // texte sur la pile : 9.6 doit le copier (text_local)
    }
    ld.align = LV_TEXT_ALIGN_CENTER; // repères toutes les 6 h (heures du jour)
    struct tm lt;
    for (uint32_t t = debut - debut % 3600 + 3600; t < fin; t += 3600)
    {
        const time_t tt = t;
        localtime_r(&tt, &lt);
        if (lt.tm_hour % 6)
            continue;
        uc_segment(ctx, gl, x(t), haut, x(t), bande1);
        char s[8];
        snprintf(s, sizeof s, TXT("%02dh", "%02d:00"), lt.tm_hour);
        const lv_area_t z = {(lv_coord_t)(x(t) - 18), (lv_coord_t)(bande1 + 4), (lv_coord_t)(x(t) + 18), (lv_coord_t)(bande1 + 18)};
        uc_texte(ctx, ld, &z, s, true);
    }
    lv_draw_rect_dsc_t d; // bande des réveils : un bloc par période allumée, couleur = cause
    lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    d.bg_color = ui_c(0x161A20);
    const lv_area_t fb = {x0, bande0, x1, bande1};
    uc_carre(ctx, d, &fb);
    d.radius = 2;
    for (int i = 0; i < bt_etat.nb_eveils; i++)
    {
        const bt_eveil_t &ev = bt_etat.eveils[i];
        if (ev.debut + ev.duree < debut)
            continue;
        d.bg_color = ui_c(COUL_CAUSE[ev.cause % 4]);
        const lv_coord_t xa = LV_MAX(x0, x(ev.debut));
        const lv_area_t z = {xa, (lv_coord_t)(bande0 + 3), (lv_coord_t)LV_MAX(xa + 2, x(ev.debut + ev.duree)), (lv_coord_t)(bande1 - 3)};
        uc_carre(ctx, d, &z);
    }
    lv_draw_line_dsc_t bl; // courbe de la batterie
    lv_draw_line_dsc_init(&bl);
    bl.color = ui_c(UI_VERT);
    bl.width = 2;
    bl.round_start = bl.round_end = 1;
    bool avant = false;
    lv_point_t prec = {0, 0};
    for (int i = 0; i < BT_COURBE; i++)
    {
        const uint8_t v = bt_etat.courbe[i];
        const uint32_t t = bt_etat.courbe_fin - (uint32_t)(BT_COURBE - 1 - i) * 900;
        if (v == BT_COURBE_VIDE || t < debut) // pas de point pendant la veille : la courbe relie les réveils
            continue;
        const lv_coord_t yp = (lv_coord_t)(bas - LV_CLAMP(0, v, 100) * (bas - haut) / 100);
        if (avant)
            uc_segment(ctx, bl, prec.x, prec.y, x(t), yp);
        prec.x = x(t); prec.y = yp;
        avant = true;
    }
}

// ---------------------------------------------------------------------------------------------------- dernier réveil
// 31/10 : chronologie (millis) réveil -> Wi-Fi -> MQTT -> synchronisé -> coupure Wi-Fi : une ligne continue, un point par
// étape (plein : atteinte), l'heure au-dessus, l'écart avec l'étape d'avant sur le trait
static void chrono_cb(lv_event_t *e)
{
    static const char *const NOMS[BT_ETAPES] = {TXT("réveil", "wake"), "Wi-Fi", "MQTT", TXT("synchro", "sync"), TXT("coupure", "Wi-Fi off")};
    // 31/10 (retour d'usage) : trait bleu, points et chiffres blancs, la coupure (le but du réveil) en ambre
    auto couleur = [](int i) { return i == BT_E_COUPURE ? (uint32_t)UI_ACCENT : (uint32_t)UI_TEXTE; };
    uc_cible_t * ctx = uc_cible_du(e);
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    const lv_coord_t xg = a.x1 + 36, xd = a.x2 - 36, yl = a.y1 + 62;
    auto x = [&](int i) { return (lv_coord_t)(xg + i * (xd - xg) / (BT_ETAPES - 1)); };
    auto atteinte = [](int i) { return i == BT_E_REVEIL || bt_etat.etapes[i]; };
    int fin = 0; // dernière étape atteinte
    for (int i = 1; i < BT_ETAPES; i++)
        if (atteinte(i))
            fin = i;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.width = 3;
    l.round_start = l.round_end = 1;
    l.color = ui_c(0x2A313A);
    uc_segment(ctx, l, x(0), yl, x(BT_ETAPES - 1), yl);
    for (int i = 1; i <= fin; i++) // chaque bout de trait dans la couleur de l'étape où il arrive
    {
        l.color = ui_c(UI_FROID);
        uc_segment(ctx, l, x(i - 1), yl, x(i), yl);
    }
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.align = LV_TEXT_ALIGN_CENTER;
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = LV_RADIUS_CIRCLE;
    char t[16];
    uint32_t avant = 0;
    for (int i = 0; i < BT_ETAPES; i++)
    {
        const bool ok = atteinte(i);
        const lv_coord_t xi = x(i);
        r.bg_color = ui_c(ok ? couleur(i) : UI_CARTE);
        r.border_color = ui_c(ok ? couleur(i) : UI_ETEINT);
        r.border_width = 3;
        const lv_area_t pt = {(lv_coord_t)(xi - 6), (lv_coord_t)(yl - 6), (lv_coord_t)(xi + 6), (lv_coord_t)(yl + 6)};
        uc_carre(ctx, r, &pt);
        ld.font = UI_POLICE_3X5_P;
        ld.color = ui_c(ok ? couleur(i) : UI_TEXTE3);
        snprintf(t, sizeof t, ok ? "%u" : "-", (unsigned)bt_etat.etapes[i]);
        const lv_area_t zn = {(lv_coord_t)(xi - 38), (lv_coord_t)(yl - 52), (lv_coord_t)(xi + 38), (lv_coord_t)(yl - 24)};
        uc_texte(ctx, ld, &zn, t, true);
        ld.font = UI_POLICE_LEGENDE;
        ld.color = ui_c(UI_TEXTE2);
        const lv_area_t zl = {(lv_coord_t)(xi - 38), (lv_coord_t)(yl + 14), (lv_coord_t)(xi + 38), (lv_coord_t)(yl + 30)};
        uc_texte(ctx, ld, &zl, NOMS[i], false); // NOMS est statique : pas de copie nécessaire en 9.6
        if (i && ok) // l'écart avec l'étape atteinte d'avant, sur le trait, au milieu
        {
            ld.color = ui_c(UI_TEXTE3);
            snprintf(t, sizeof t, "+%u", (unsigned)(bt_etat.etapes[i] - avant));
            const lv_coord_t xm = (x(i - 1) + xi) / 2;
            const lv_area_t ze = {(lv_coord_t)(xm - 30), (lv_coord_t)(yl - 22), (lv_coord_t)(xm + 30), (lv_coord_t)(yl - 6)};
            uc_texte(ctx, ld, &ze, t, true);
        }
        if (ok)
            avant = bt_etat.etapes[i];
    }
}

// ---------------------------------------------------------------------------------------------------- mise à jour
static void rafraichir_batterie()
{
    if (!g_zone)
        return;
    const uint32_t maintenant = (uint32_t)time(nullptr);
    if (bt_etat.pourcent != g_pile_pct || bt_etat.charge != g_pile_charge)
    {
        g_pile_pct = bt_etat.pourcent;
        g_pile_charge = bt_etat.charge;
        lv_obj_invalidate(o_pile);
    }
    ui_label_texte_fmt(o_pct, "%d", bt_etat.pourcent);
    ui_style_text_color(o_pct, ui_c(couleur_batterie()), 0);
    ui_label_texte_fmt(o_tension, "%d" SEP_DEC "%02d V", (int)bt_etat.tension, (int)(bt_etat.tension * 100 + 0.5f) % 100);
    ui_label_texte(o_etat, bt_etat.charge ? LV_SYMBOL_USB "  " TXT("En charge", "Charging") : TXT("Sur batterie", "On battery"));
    ui_style_text_color(o_etat, ui_c(bt_etat.charge ? UI_VERT : UI_TEXTE), 0);
    char d[24] = "";
    if (bt_etat.depuis && maintenant > bt_etat.depuis && maintenant > 1700000000u)
        duree_texte(d, sizeof d, maintenant - bt_etat.depuis);
    if (d[0])
        ui_label_texte_fmt(o_depuis, bt_etat.charge ? TXT("branché depuis %s", "plugged in for %s") : TXT("débranchée depuis %s", "unplugged for %s"), d);
    else // 31/10 (retour d'usage) : plutôt qu'un vide (date effacée : celle du « 56 ans » était fausse)
        ui_label_texte(o_depuis, bt_etat.charge ? "" : TXT("débranchée · heure inconnue", "unplugged · time unknown"));

    // résumé des 24 h : nombre de réveils, temps allumé
    int n = 0;
    uint32_t allume = 0;
    for (int i = 0; i < bt_etat.nb_eveils; i++)
        if (bt_etat.eveils[i].debut + 86400 >= maintenant)
        {
            n++;
            allume += bt_etat.eveils[i].duree;
        }
    duree_texte(d, sizeof d, allume);
    ui_label_texte_fmt(o_resume, TXT("%d réveil%s · allumé %s en tout", "%d wake%s · on %s total"), n, n > 1 ? "s" : "", d);
    // le graphe n'est redessiné que si ses données ont changé (ou toutes les 15 min, pour glisser)
    uint32_t e = 2166136261u;
    const uint32_t parts[] = {(uint32_t)bt_etat.nb_eveils, bt_etat.nb_eveils ? bt_etat.eveils[bt_etat.nb_eveils - 1].duree : 0u,
                              bt_etat.courbe_fin, maintenant / 900};
    for (uint32_t p : parts)
        e = (e ^ p) * 16777619u;
    if (e != g_graphe_empreinte)
    {
        g_graphe_empreinte = e;
        lv_obj_invalidate(o_graphe);
    }

    static const char *const CAUSES[] = {TXT("radar", "radar"), TXT("toucher", "touch"), TXT("minuterie", "timer"), TXT("démarrage", "startup")};
    if (bt_etat.reveil > 1700000000u)
    {
        struct tm lt;
        const time_t tt = bt_etat.reveil;
        localtime_r(&tt, &lt);
        ui_label_texte_fmt(o_cause, TXT("%s · %02dh%02d", "%s · %02d:%02d"), CAUSES[bt_etat.cause % 4], lt.tm_hour, lt.tm_min);
    }
    else
        ui_label_texte(o_cause, CAUSES[bt_etat.cause % 4]);
    uint32_t c = 2166136261u;
    for (uint32_t v : bt_etat.etapes)
        c = (c ^ v) * 16777619u;
    if (c != g_chrono_empreinte)
    {
        g_chrono_empreinte = c;
        lv_obj_invalidate(o_chrono);
    }
}

static void inter(lv_obj_t *sw, bool actif)
{
    if (actif)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

static void rafraichir_reglages()
{
    if (!o_reglages)
        return;
    inter(o_diag, rd_etat.diag);
    if (rd_etat.diag)
    {
        char d[24];
        const uint32_t maintenant = (uint32_t)time(nullptr);
        duree_texte(d, sizeof d, rd_etat.diag_fin > maintenant ? rd_etat.diag_fin - maintenant : 0);
        ui_label_texte_fmt(o_diag_txt, TXT("en cours : %u détection%s · arrêt dans %s", "running: %u detection%s · stops in %s"), rd_etat.diag_nb, rd_etat.diag_nb > 1 ? "s" : "", d);
    }
    else
        ui_label_texte(o_diag_txt, TXT("écran noir, chaque détection dans le journal", "black screen, every detection logged"));
    inter(o_coupure, bt_reglages.coupure_wifi);
    ui_label_texte_fmt(o_economie, "%u s " LV_SYMBOL_RIGHT, (unsigned)bt_reglages.economie_s);
    ui_label_texte(o_puissance, bt_reglages.wifi_reduit ? "7 dBm " LV_SYMBOL_RIGHT : "max " LV_SYMBOL_RIGHT);
    ui_label_texte_fmt(o_marge, "+%u", (unsigned)rd_etat.marge);
}

static void rafraichir()
{
    rafraichir_batterie();
    rafraichir_reglages();
}

void page_batterie_maj() { rafraichir(); }

static lv_obj_t *g_zone_page; // la zone de la page (chaque onglet s'y construit)
static void construire();

static void onglet_cb(lv_event_t *e)
{
    g_vue = (int)(intptr_t)lv_event_get_user_data(e);
    construire();
}

// ---------------------------------------------------------------------------------------------------- onglet Réglages
static void diag_cb(lv_event_t *)
{
    if (rd_diag)
        rd_diag(!rd_etat.diag);
    rafraichir();
}

static void regler(const char *quoi)
{
    rafraichir();
    if (bt_regler)
        bt_regler(quoi);
}

static void coupure_cb(lv_event_t *)
{
    bt_reglages.coupure_wifi = !bt_reglages.coupure_wifi;
    regler("coupure_wifi");
}

static void economie_cb(lv_event_t *)
{
    static const uint8_t PAS[] = {5, 10, 20, 30};
    int i = 0;
    while (i < 4 && PAS[i] != bt_reglages.economie_s)
        i++;
    bt_reglages.economie_s = PAS[(i + 1) % 4];
    regler("economie");
}

static void puissance_cb(lv_event_t *)
{
    bt_reglages.wifi_reduit = !bt_reglages.wifi_reduit;
    regler("wifi_reduit");
}

static void marge_cb(lv_event_t *e)
{
    const uint8_t m = (uint8_t)LV_CLAMP(0, rd_etat.marge + (int)(intptr_t)lv_event_get_user_data(e), 20);
    rd_etat.marge = m;
    if (rd_marge)
        rd_marge(m);
    rafraichir();
}

// ligne « titre + explication ... (à droite) » comme la page Réglages ; cb non nul : toute la ligne est le bouton
static lv_obj_t *ligne(lv_obj_t *c, const char *txt, const char *detail, lv_obj_t **o_detail, lv_event_cb_t cb, bool trait)
{
    lv_obj_t *b = lv_btn_create(c);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, lv_pct(100), 62);
    if (trait)
    {
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        ui_style_border_width(b, 1, 0);
        ui_style_border_color(b, ui_c(UI_BORD), 0);
    }
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    else
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *col = ui_conteneur(b);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    ui_label(col, UI_POLICE_TEXTE, UI_TEXTE, txt);
    lv_obj_t *d = ui_label(col, UI_POLICE_LEGENDE, UI_TEXTE2, detail);
    if (o_detail)
        *o_detail = d;
    return b;
}

static lv_obj_t *interrupteur(lv_obj_t *b)
{
    lv_obj_t *sw = lv_switch_create(b);
    lv_obj_set_size(sw, 52, 30);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    ui_interrupteur_couleurs(sw);
    return sw;
}

static void construire_reglages(lv_obj_t *zone)
{
    lv_obj_set_pos(ui_titre_section(zone, TXT("GESTION D'ÉNERGIE", "POWER MANAGEMENT")), UI_MARGE, 52);
    lv_obj_t *c = o_reglages = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, LV_SIZE_CONTENT);
    lv_obj_set_pos(c, UI_MARGE, 76);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_set_style_pad_ver(c, 2, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 0, 0);

    o_diag = interrupteur(ligne(c, TXT("Diag radar", "Radar diagnostics"), "", &o_diag_txt, diag_cb, true));
    o_coupure = interrupteur(ligne(c, TXT("Coupure Wi-Fi après la synchro", "Wi-Fi off after sync"), TXT("réveil radar sans toucher · OTA : toucher d'abord", "radar wake without touch · OTA: touch first"), nullptr, coupure_cb, true));
    o_economie = ui_label(ligne(c, TXT("Économie après", "Power save after"), TXT("luminosité de repos, 80 MHz, Wi-Fi en veille", "dim brightness, 80 MHz, Wi-Fi sleep"), nullptr, economie_cb, true),
                          UI_POLICE_TEXTE, UI_TEXTE2, "");
    o_puissance = ui_label(ligne(c, TXT("Puissance Wi-Fi", "Wi-Fi power"), TXT("7 dBm : moins de courant et de portée", "7 dBm: less current, shorter range"), nullptr, puissance_cb, true),
                           UI_POLICE_TEXTE, UI_TEXTE2, "");
    lv_obj_t *b = ligne(c, TXT("Marge du réglage auto", "Auto-tune margin"), TXT("dB ajoutés aux seuils mesurés", "dB added to measured thresholds"), nullptr, nullptr, false);
    lv_obj_t *r = ui_conteneur(b);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 10, 0);
    for (int k = 0; k < 2; k++)
    {
        lv_obj_t *p = ui_bouton(r, 40, 40);
        lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
        lv_obj_center(ui_label(p, UI_POLICE_TITRE, UI_TEXTE, k ? "+" : "−"));
        lv_obj_add_event_cb(p, marge_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(k ? 1 : -1));
        if (!k)
        {
            o_marge = ui_label(r, UI_POLICE_TEXTE, UI_ACCENT, "");
            lv_obj_set_width(o_marge, 34);
            lv_obj_set_style_text_align(o_marge, LV_TEXT_ALIGN_CENTER, 0);
        }
    }
}

void page_batterie_creer(lv_obj_t *zone)
{
    g_zone_page = zone;
    construire();
}

static void construire_batterie(lv_obj_t *zone)
{
    g_zone = zone;

    // batterie
    lv_obj_t *c = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 104);
    lv_obj_set_pos(c, UI_MARGE, 52);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_set_style_pad_ver(c, 12, 0);
    o_pile = ui_conteneur(c);
    lv_obj_set_size(o_pile, 112, 52);
    lv_obj_add_event_cb(o_pile, pile_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_t *r = ui_valeur_unite(c, UI_POLICE_3X5_M, UI_VERT, "%", &o_pct);
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, 0, -2);
    o_tension = ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, "");
    lv_obj_align(o_tension, LV_ALIGN_TOP_RIGHT, 0, 40);
    o_etat = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_obj_set_pos(o_etat, 0, 62);
    o_depuis = ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, "");
    lv_obj_align(o_depuis, LV_ALIGN_TOP_RIGHT, 0, 64);

    // réveils sur 24 h
    lv_obj_set_pos(ui_titre_section(zone, TXT("RÉVEILS · 24 H", "WAKE-UPS · 24 H")), UI_MARGE, 160);
    o_resume = ui_label(zone, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_align(o_resume, LV_ALIGN_TOP_RIGHT, -UI_MARGE - 4, 164);
    c = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 150);
    lv_obj_set_pos(c, UI_MARGE, 182);
    lv_obj_set_style_pad_all(c, 8, 0);
    o_graphe = ui_conteneur(c);
    lv_obj_set_size(o_graphe, lv_pct(100), 120);
    lv_obj_add_event_cb(o_graphe, eveils_cb, LV_EVENT_DRAW_MAIN, NULL);
    const char *noms[] = {TXT("radar", "radar"), TXT("toucher", "touch"), TXT("minuterie", "timer"), TXT("batterie", "battery")};
    const uint32_t coul[] = {UI_VERT, UI_ACCENT, UI_TEXTE3, UI_VERT};
    lv_coord_t x = 40;
    for (int k = 0; k < 4; k++)
    {
        lv_obj_t *p = ui_conteneur(c);
        lv_obj_set_size(p, k == 3 ? 14 : 10, k == 3 ? 2 : 10);
        lv_obj_set_style_radius(p, 2, 0);
        ui_style_bg_color(p, ui_c(coul[k]), 0);
        ui_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_pos(p, x, k == 3 ? 128 : 124);
        lv_obj_set_pos(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, noms[k]), x + (k == 3 ? 18 : 14), 121);
        x += (k == 3 ? 18 : 14) + uc_largeur_texte(noms[k], UI_POLICE_LEGENDE) + 14; // 9.6 : lv_txt_get_width mort, voir ui_courbe.h
    }

    // dernier réveil : la chronologie en millis
    lv_obj_set_pos(ui_titre_section(zone, TXT("DERNIER RÉVEIL · MS", "LAST WAKE · MS")), UI_MARGE, 338);
    o_cause = ui_label(zone, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_align(o_cause, LV_ALIGN_TOP_RIGHT, -UI_MARGE - 4, 342);
    c = ui_conteneur(zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, 110);
    lv_obj_set_pos(c, UI_MARGE, 360);
    lv_obj_set_style_pad_all(c, 0, 0);
    o_chrono = ui_conteneur(c);
    lv_obj_set_size(o_chrono, lv_pct(100), lv_pct(100));
    lv_obj_add_event_cb(o_chrono, chrono_cb, LV_EVENT_DRAW_MAIN, NULL);
}

static void construire()
{
    lv_obj_t *zone = g_zone_page;
    page_radar_quitter();
    g_zone = nullptr;
    o_reglages = nullptr;
    lv_obj_clean(zone);
    static const char *const ONGLETS[] = {TXT("Batterie", "Battery"), TXT("Radar", "Radar"), TXT("Réglages", "Settings")};
    ui_onglets(zone, ONGLETS, 3, g_vue, onglet_cb, nullptr, o_onglets);
    if (g_vue == 0)
        construire_batterie(zone);
    else if (g_vue == 1)
        page_radar_creer(zone);
    else
        construire_reglages(zone);
    g_pile_pct = -1;
    g_graphe_empreinte = g_chrono_empreinte = 0;
    rafraichir();
}

void page_batterie_quitter()
{
    page_radar_quitter();
    g_zone = nullptr;
    o_reglages = nullptr;
    g_zone_page = nullptr;
}
