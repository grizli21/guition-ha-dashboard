// 11/11/2026 : page « IA » (maquette validée le 10/11, voir page_ia.h). Une seule carte de
// 380 x 456 : scène pixel (images générées par tools/gen_scene_ia.py) collée en BAS de la carte — sa
// première ligne de ciel est exactement UI_CARTE en RVB565, la soudure en haut est invisible —, jauges
// posées sur le ciel, bande d'activité sur la terre, Clawd et ses marques en petits objets séparés.
// Comme le 27/10 pour la page PC : la structure n'est construite qu'une fois, les valeurs sont posées
// SUR PLACE et seulement si elles changent (jamais le fond à chaque message).
#include "page_ia.h"
#include "ui_psram.h"
#include "ui_theme.h"
#include "ui_mqtt.h" // ui_maison.nuit : la phase publiée par HA (maison/etat) choisit le ciel
#include "icones_ia.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

UI_PSRAM(ia_compte_t, ia_compte_p, 1, nullptr);    // en PSRAM, comme pc_etat (ui_psram.h)
UI_PSRAM(ia_activite_t, ia_activite_p, 1, nullptr);

static lv_obj_t *g_zone;
static lv_timer_t *g_tic; // 1 s : les compte à rebours (« dans 2 h 13 », « lu il y a… », péremption 2 min)

// --- géométrie de la carte (pixels dans la carte ; scène 380 x 412 posée en bas, à y = 44) ---
#define CARTE_L 380
#define CARTE_H 456
#define SCENE_Y 44                       // haut de la scène dans la carte (456 - 412 : le ciel gagné en haut)
#define SCENE_H 412                      // hauteur de la scène = hauteur de la colonne répétée (95 x 103 cases x4)
#define JAUGE_Y(k) (50 + (k) * 72)       // « 5 heures » puis « Semaine » (mêmes hauteurs que l'onglet IA du 31/10)
#define JAUGE_L 356                      // largeur des barres (comme l'onglet IA de la page PC)
#define Y0 344                           // haut de la bande d'activité (44 + 75 cases x 4 : sous l'herbe)
#define HISTO_BASE (Y0 + 94)             // ligne de base de l'histogramme (438) ; les barres s'arrêtent juste au-dessus
#define HISTO_H 32                       // hauteur max d'une barre
#define HISTO_X0 14                      // « −5 h » / « maintenant » sous cette ligne, dans les marges de la carte
// positions dans la carte (repères de tools/gen_scene_ia.py : cases de scène x4, scène à y = 44)
#define CLAWD_X (26 * 4)            // Clawd : case (26, 57) de la scène
#define CLAWD_Y (SCENE_Y + 57 * 4)
#define PENSEE_X (144)              // « + » et carrés de la pensée : coin (36, 48)
#define PENSEE_Y (SCENE_Y + 192)
#define EXCL_X (148)                // le « ! » (attente) : coin (37, 47)
#define EXCL_Y (SCENE_Y + 188)
#define Z_X (156)                   // les deux « z » (repos) : coin (39, 47)
#define Z_Y (SCENE_Y + 188)

// objets dont les valeurs changent (structure construite une fois)
static lv_obj_t *o_ciel, *o_etoiles, *o_lune, *o_soleil, *o_nuage1, *o_nuage2, *o_touffes, *o_buisson;
static lv_obj_t *o_nom, *o_maj, *o_val[2], *o_barre[2], *o_reset[2];
static lv_obj_t *o_etat, *o_tokens, *o_sessions, *o_outils;
static lv_obj_t *o_clawd, *o_pensee, *o_excl, *o_z, *o_histo[IA_HISTO];

static uint32_t charge_couleur(int pct) { return pct >= 90 ? UI_ROUGE : pct >= 75 ? UI_ACCENT : UI_TEXTE; }

// 27/10 (audit des redessins, page PC) : lv_obj_set_size redessine même à l'identique — on ne taille que si ça change
static void taille(lv_obj_t *o, lv_coord_t w, lv_coord_t h)
{
    if (lv_obj_get_style_width(o, LV_PART_MAIN) != w || lv_obj_get_style_height(o, LV_PART_MAIN) != h)
        lv_obj_set_size(o, w, h);
}

// barre horizontale fine (fond + partie pleine) : même dessin que la page PC, valeurs : barre_maj
static lv_obj_t *barre(lv_obj_t *p, lv_coord_t l, lv_coord_t h)
{
    lv_obj_t *b = ui_conteneur(p);
    lv_obj_set_size(b, l, h);
    lv_obj_set_style_radius(b, h / 2, 0);
    ui_style_bg_color(b, ui_c(UI_CARTE2), 0);
    ui_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(b, true, 0);
    lv_obj_t *f = ui_conteneur(b);
    lv_obj_set_size(f, 0, h);
    ui_style_bg_opa(f, LV_OPA_COVER, 0);
    return b;
}
static void barre_maj(lv_obj_t *b, int pct, uint32_t c)
{
    const lv_coord_t l = lv_obj_get_style_width(b, LV_PART_MAIN), h = lv_obj_get_style_height(b, LV_PART_MAIN);
    taille(lv_obj_get_child(b, 0), l * LV_CLAMP(0, pct, 100) / 100, h);
    ui_style_bg_color(lv_obj_get_child(b, 0), ui_c(c), 0);
}

// « dans 2 h 13 », « dans 35 min », « dans 1 j 4 h » (repris de l'onglet IA du 31/10, page PC)
static void dans(char *b, size_t n, int32_t s)
{
    if (s <= 0)
        snprintf(b, n, TXT("maintenant", "now"));
    else if (s >= 86400)
        snprintf(b, n, TXT("dans %d j %d h", "in %d d %d h"), (int)(s / 86400), (int)(s % 86400 / 3600));
    else if (s >= 3600)
        snprintf(b, n, TXT("dans %d h %02d", "in %d h %02d"), (int)(s / 3600), (int)(s % 3600 / 60));
    else
        snprintf(b, n, TXT("dans %d min", "in %d min"), (int)(s / 60 + 1));
}

// nombre à la française, séparateur de milliers par une espace : 12345 -> « 12 345 »
static void milliers(char *b, size_t n, uint32_t v)
{
    char s[12];
    snprintf(s, sizeof s, "%u", v);
    const int l = (int)strlen(s);
    int k = 0;
    for (int i = 0; i < l && k < (int)n - 1; i++)
    {
        if (i && (l - i) % 3 == 0)
            b[k++] = ' ';
        b[k++] = s[i];
    }
    b[k] = 0;
}

// =====================================================================  CONSTRUCTION
static void construire()
{
    lv_obj_t *c = ui_conteneur(g_zone);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, CARTE_L, CARTE_H);
    lv_obj_set_pos(c, UI_MARGE, UI_MARGE);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_clip_corner(c, true, 0); // la scène est rectangulaire : elle s'arrête aux coins ronds

    // fond (soir du 11/11, -488 Ko de flash) : plus de deux images 380 x 412 mais une COLONNE 1 x 412
    // (bandes horizontales ciel/herbe/terre) répétée sur toute la largeur par LVGL — LV_IMAGE_ALIGN_TILE,
    // supporté en 9.6 (lv_image.c:908 : l'objet est rempli de carreaux ; lv_draw_image.c:276 : le décodeur
    // s'ouvre UNE fois puis boucle sur les carreaux, chaque carreau 1 x 412 est un blend direct).
    // Par-dessus, les petits décors posés aux positions affichées par tools/gen_scene_ia.py (mêmes pixels
    // que l'ancien fond : l'outil le vérifie à la génération). Clawd et marques restent au-dessus.
    o_ciel = lv_img_create(c);
    lv_obj_set_pos(o_ciel, 0, SCENE_Y);
    lv_obj_set_size(o_ciel, CARTE_L, SCENE_H); // le tuilage remplit ce rectangle (l'image elle-même fait 1 px de large)
    lv_image_set_inner_align(o_ciel, LV_IMAGE_ALIGN_TILE);
    o_etoiles = lv_img_create(c); // nuit : 4 luminosités + 3 croix, calque unique (boîte x 2..94, y 48..66)
    lv_img_set_src(o_etoiles, &etoiles_ia);
    lv_obj_set_pos(o_etoiles, 8, SCENE_Y + 192);
    o_lune = lv_img_create(c);
    lv_img_set_src(o_lune, &lune_ia);
    lv_obj_set_pos(o_lune, 32, SCENE_Y + 204);
    o_soleil = lv_img_create(c); // jour : disque + rais (boîte x 6..20, y 49..63)
    lv_img_set_src(o_soleil, &soleil_ia);
    lv_obj_set_pos(o_soleil, 24, SCENE_Y + 196);
    o_nuage1 = lv_img_create(c); // nuages (déplacés le 11/11 hors de la zone des marques)
    lv_img_set_src(o_nuage1, &nuage1_ia);
    lv_obj_set_pos(o_nuage1, 208, SCENE_Y + 180);
    o_nuage2 = lv_img_create(c);
    lv_img_set_src(o_nuage2, &nuage2_ia);
    lv_obj_set_pos(o_nuage2, 264, SCENE_Y + 224);
    o_touffes = lv_img_create(c); // touffes d'herbe : couleur selon la phase (deux morceaux)
    lv_obj_set_pos(o_touffes, 24, SCENE_Y + 284);
    o_buisson = lv_img_create(c); // buisson : mêmes verts jour et nuit, posé après les nuages (il les masque)
    lv_img_set_src(o_buisson, &buisson_ia);
    lv_obj_set_pos(o_buisson, 272, SCENE_Y + 228);

    // haut de carte : adresse du compte, âge de la donnée, jauges 5 h et semaine (décalques de l'onglet IA du 31/10)
    o_nom = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "");
    // mode DOT avec hauteur AUTO = piège (vu capture 182 du 11/11) : l'étiquette grandit et passe sur
    // les jauges. Une hauteur fixe d'une ligne (sb16) force l'ellipsize sur un seul rang.
    lv_label_set_long_mode(o_nom, LV_LABEL_LONG_DOT);
    lv_obj_set_size(o_nom, 230, 22);
    lv_obj_set_pos(o_nom, 14, 14);
    o_maj = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE3, "");
    lv_obj_align(o_maj, LV_ALIGN_TOP_RIGHT, -14, 17);
    static const char *const FENETRES[] = {TXT("5 heures", "5 hours"), TXT("Semaine", "Week")};
    for (int k = 0; k < 2; k++)
    {
        lv_obj_set_pos(ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, FENETRES[k]), 14, JAUGE_Y(k));
        o_val[k] = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "");
        lv_obj_align(o_val[k], LV_ALIGN_TOP_RIGHT, -14, JAUGE_Y(k) - 4);
        o_barre[k] = barre(c, JAUGE_L, 10);
        lv_obj_set_pos(o_barre[k], 14, JAUGE_Y(k) + 24);
        o_reset[k] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
        lv_obj_set_pos(o_reset[k], 14, JAUGE_Y(k) + 42);
    }

    // Clawd et ses marques : petits objets séparés (échange par ui_img_src / ui_cacher, jamais le fond)
    o_clawd = lv_img_create(c);
    lv_obj_set_pos(o_clawd, CLAWD_X, CLAWD_Y);
    o_pensee = lv_img_create(c);
    lv_img_set_src(o_pensee, &marque_ia_reflechit);
    lv_obj_set_pos(o_pensee, PENSEE_X, PENSEE_Y);
    o_excl = lv_img_create(c);
    lv_img_set_src(o_excl, &marque_ia_attente);
    lv_obj_set_pos(o_excl, EXCL_X, EXCL_Y);
    o_z = lv_img_create(c);
    lv_img_set_src(o_z, &marque_ia_repos);
    lv_obj_set_pos(o_z, Z_X, Z_Y);

    // bande de terre = panneau d'activité (maquette du 10/11) : état et tokens à gauche,
    // sessions/prompts et outils à droite, histogramme 30 tranches et ses légendes dessous.
    // Largeurs mesurées dans les TTF du maquette : « autorisation demandée » 193 px en sb16,
    // « 12 sessions · 987 prompts » ~150 px en sb12 — et LVGL coupe plus tôt que PIL (vu sur les captures
    // 177/180 du 11/11 : retour à la ligne et chevauchement). Les deux colonnes sont donc larges et CLIP
    // (jamais de retour à la ligne) : les zones se recouvrent, pas les textes (193+14 < 216 ≤ 366-150).
    o_etat = ui_label(c, UI_POLICE_TEXTE, UI_IA_ORANGE, "");
    lv_obj_set_width(o_etat, 234);
    lv_label_set_long_mode(o_etat, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(o_etat, 14, Y0 + 6);
    o_tokens = ui_label(c, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_obj_set_pos(o_tokens, 14, Y0 + 28);
    o_sessions = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_width(o_sessions, 165);
    lv_label_set_long_mode(o_sessions, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(o_sessions, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(o_sessions, CARTE_L - 14 - 165, Y0 + 8);
    o_outils = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_width(o_outils, 165);
    lv_label_set_long_mode(o_outils, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(o_outils, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(o_outils, CARTE_L - 14 - 165, Y0 + 30);
    for (int i = 0; i < IA_HISTO; i++)
    {
        o_histo[i] = ui_conteneur(c);
        lv_obj_set_size(o_histo[i], 9, 0);
        ui_style_bg_opa(o_histo[i], LV_OPA_COVER, 0);
        // la dernière tranche (la tranche en cours) est la seule orange, les autres « terre claire »
        ui_style_bg_color(o_histo[i], ui_c(i == IA_HISTO - 1 ? UI_IA_ORANGE : UI_HISTO), 0);
        lv_obj_set_pos(o_histo[i], HISTO_X0 + (i * 354 + 15) / 30, HISTO_BASE - 1); // pas = (368-14)/30 = 11,8
    }
    lv_obj_t *ligne = ui_conteneur(c); // le trait de base de l'histogramme
    lv_obj_set_size(ligne, 354, 1);
    ui_style_bg_color(ligne, ui_c(UI_HISTO_LIGNE), 0);
    ui_style_bg_opa(ligne, LV_OPA_COVER, 0);
    lv_obj_set_pos(ligne, HISTO_X0, HISTO_BASE);
    lv_obj_set_pos(ui_label(c, UI_POLICE_LEGENDE, UI_IA_LEGENDE, TXT("−5 h", "−5 h")), HISTO_X0, HISTO_BASE + 2);
    lv_obj_t *lg2 = ui_label(c, UI_POLICE_LEGENDE, UI_IA_LEGENDE, TXT("maintenant", "now"));
    lv_obj_set_width(lg2, 90);
    lv_obj_set_style_text_align(lg2, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(lg2, CARTE_L - 14 - 90, HISTO_BASE + 2);
}

// =====================================================================  MISES À JOUR (sur place)
static void maj_jauges()
{
    static const char *const JOURS[] = {TXT("dim.", "Sun"), TXT("lun.", "Mon"), TXT("mar.", "Tue"), TXT("mer.", "Wed"), TXT("jeu.", "Thu"), TXT("ven.", "Fri"), TXT("sam.", "Sat")};
    const uint32_t maintenant = (uint32_t)time(nullptr);
    if (!ia_compte.recu) // pire cas demandé : rien de l'agent du PC (capture 176)
    {
        ui_label_texte(o_nom, TXT("en attente de l'agent du PC", "waiting for the PC agent"));
        ui_label_texte(o_maj, "");
        for (int k = 0; k < 2; k++)
        {
            ui_label_texte(o_val[k], "");
            ui_label_texte(o_reset[k], "");
            barre_maj(o_barre[k], 0, UI_TEXTE);
        }
        return;
    }
    const ia_compte_t &a = ia_compte;
    ui_label_texte(o_nom, a.nom);
    const int32_t age = a.maj && maintenant > 1700000000u ? (int32_t)(maintenant - a.maj) : 0;
    char t[48], d[24];
    if (age < 600) // lue il y a moins de 10 min : rien à dire (règle de l'onglet IA du 31/10)
        ui_label_texte(o_maj, "");
    else
    {
        dans(d, sizeof d, age);
        ui_label_texte_fmt(o_maj, TXT("lu il y a %s", "read %s ago"), d + 5); // « 3 h 05 » (sans « dans »)
    }
    const int pct[] = {a.h5, a.semaine};
    const uint32_t reset[] = {a.h5_reset, a.semaine_reset};
    for (int k = 0; k < 2; k++)
    {
        ui_label_texte_fmt(o_val[k], "%d%%", pct[k]);
        ui_style_text_color(o_val[k], ui_c(charge_couleur(pct[k])), 0);
        barre_maj(o_barre[k], pct[k], pct[k] >= 90 ? UI_ROUGE : k ? UI_FROID : UI_ACCENT);
        if (!reset[k] || reset[k] <= maintenant)
        {
            ui_label_texte(o_reset[k], k ? TXT("réinitialisation inconnue", "unknown reset") : TXT("pas de fenêtre en cours", "no current session"));
            continue;
        }
        struct tm lt;
        const time_t tt = reset[k];
        localtime_r(&tt, &lt);
        if (k == 0 || reset[k] - maintenant < 86400)
            snprintf(t, sizeof t, TXT("%02dh%02d", "%02d:%02d"), lt.tm_hour, lt.tm_min);
        else
            snprintf(t, sizeof t, TXT("%s %02d/%02d %02dh%02d", "%s %02d/%02d %02d:%02d"), JOURS[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1, lt.tm_hour, lt.tm_min);
        dans(d, sizeof d, (int32_t)(reset[k] - maintenant));
        ui_label_texte_fmt(o_reset[k], TXT("réinitialisation %s · %s", "reset %s · %s"), t, d);
    }
}

// l'état de l'agent, sa pose et ses chiffres ; « au repos » si le message est absent ou vieux de plus de 2 min
static void maj_activite()
{
    const ia_activite_t &a = ia_activite;
    const uint32_t maintenant = (uint32_t)time(nullptr);
    const bool frais = a.recu && (maintenant < a.maj + 2 || (int32_t)(maintenant - a.maj) <= 120);
    const char *txt = TXT("au repos", "idle");
    uint32_t col = UI_TEXTE2;
    int pose = 0; // 0 repos (yeux fermés + « z »), 1 réfléchit (pensée), 2 attente (« ! »)
    if (frais)
    {
        if (!strcmp(a.alerte, "autorisation"))
            txt = TXT("autorisation demandée", "authorization requested"), col = UI_ROUGE, pose = 2;
        else if (!strcmp(a.alerte, "question"))
            txt = TXT("question pour toi", "question for you"), col = UI_ROUGE, pose = 2;
        else if (!strcmp(a.etat, "reflechit"))
            txt = TXT("réfléchit…", "thinking…"), col = UI_IA_ORANGE, pose = 1;
        else if (!strcmp(a.etat, "attente"))
            txt = TXT("en attente…", "waiting…"), col = UI_IA_ORANGE, pose = 2;
    }
    ui_label_texte(o_etat, txt);
    ui_style_text_color(o_etat, ui_c(col), 0);
    ui_img_src(o_clawd, pose == 0 ? &clawd_ia_repos : &clawd_ia);
    ui_cacher(o_pensee, pose != 1);
    ui_cacher(o_excl, pose != 2);
    ui_cacher(o_z, pose != 0);

    char b[16];
    milliers(b, sizeof b, (a.tokens + 500) / 1000);
    ui_label_texte_fmt(o_tokens, TXT("%s k tokens produits", "%s k tokens produced"), b);
    ui_label_texte_fmt(o_sessions, TXT("%d sessions · %d prompts", "%d sessions · %d prompts"), a.sessions, a.prompts);
    ui_label_texte_fmt(o_outils, TXT("%d outils", "%d tools"), a.outils);

    uint32_t max = 1;
    for (int i = 0; i < IA_HISTO; i++)
        if (a.histo[i] > max)
            max = a.histo[i];
    for (int i = 0; i < IA_HISTO; i++)
    {
        const lv_coord_t hh = (lv_coord_t)((uint64_t)a.histo[i] * HISTO_H / max);
        taille(o_histo[i], 9, hh);
        if (lv_obj_get_style_y(o_histo[i], LV_PART_MAIN) != HISTO_BASE - hh)
            lv_obj_set_y(o_histo[i], HISTO_BASE - hh);
    }
}

static void maj_valeurs()
{
    // ciel du jour ou de la nuit (phase publiée par HA, maison/etat) : un échange de colonne +
    // affichage/masquage des décors de la phase, tout gardé (ui_img_src / ui_cacher ne touchent rien si identique)
    const bool nuit = ui_maison.nuit;
    ui_img_src(o_ciel, nuit ? &colonne_ia_nuit : &colonne_ia_jour);
    ui_cacher(o_etoiles, !nuit);
    ui_cacher(o_lune, !nuit);
    ui_cacher(o_soleil, nuit);
    ui_cacher(o_nuage1, nuit);
    ui_cacher(o_nuage2, nuit);
    ui_img_src(o_touffes, nuit ? &touffes_ia_nuit : &touffes_ia_jour);
    maj_jauges();
    maj_activite();
}

static void tic_cb(lv_timer_t *)
{
    if (g_zone)
        maj_valeurs(); // compte à rebours et péremption : sur place, seulement ce qui change
}

void page_ia_maj()
{
    if (g_zone)
        maj_valeurs();
}

void page_ia_creer(lv_obj_t *zone)
{
    g_zone = zone;
    construire();
    maj_valeurs();
    g_tic = lv_timer_create(tic_cb, 1000, nullptr);
}

void page_ia_quitter()
{
    if (g_tic)
        lv_timer_del(g_tic);
    g_tic = nullptr;
    g_zone = nullptr;
}
