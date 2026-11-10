#include "page_imprimante.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "ui_theme.h"
#include "ui_courbe.h" // 05/11 (vague D) : wrapper uc_arc — anneau de progression sous 9.6
#include "ui_camera.h"
#include "icones_28.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

UI_PSRAM(pr_etat_imp_t, pr_etat_p, 1, nullptr); // 29/10 : en PSRAM (ui_psram.h)
void (*pr_commande)(pr_action_t action, int arg) = nullptr;

static const struct
{
    const char *nom;
    int buse, plateau, caisson;
} PRECHAUFFES[] = {{TXT("Arrêt", "Off"), 0, 0, 0}, {"PLA", 200, 60, 20}, {"PETG", 235, 70, 30}, {"ABS", 245, 100, 55}};

// 27/10 (retour d'usage) : plus de ligne d'actions en bas du Suivi : l'anneau et la caméra prennent la place, recentrés ;
// les commandes de la machine (allumer, éteindre, pause, arrêter) passent dans un 4e onglet « Marche » ; « Lancer »
// est dans l'aperçu d'un fichier (onglet Fichiers)
#define ANNEAU 270
#define ANNEAU_Y 60
#define CAM_L 375 // caméra 400 x 320 de motionEye : même rapport (27/10 : 350 -> 375, retour d'usage « agrandis un peu »)
#define CAM_H 300
#define TITRE_Y 356
#define TEMP_Y 410
#define VIG PR_VIGNETTE

static lv_obj_t *g_zone = nullptr;
static int g_vue = 0;     // 0 Suivi, 1 Réglages, 2 Fichiers, 3 Marche
static int g_apercu = -1; // Fichiers : fichier ouvert en aperçu (-1 : la liste)
// 08/11/2026 (retour d'usage) : la vue Suivi a TROIS états, dans l'ordre de l'appui : miniature du fichier (STL) -> caméra du
// caisson -> caméra du plateau -> miniature. 27/10 : ce n'était qu'un booléen (STL / caméra du caisson). Le nom
// g_camera est gardé : c'est lui qui commande la zone caméra de la page (zone_camera(), la cle de structure, le banc).
enum { VUE_STL = 0, VUE_CAISSON = 1, VUE_PLATEAU = 2 };
static int g_camera = VUE_STL;
static bool g_confirme = false;

static lv_obj_t *o_camera, *o_pct, *o_anneau;
// 24/10 (retour d'usage : « vérifie tes dessins de page ») : la page n'est plus reconstruite à chaque état reçu (la Klipper envoie
// ses températures plusieurs fois par seconde : page entière refaite ~1 fois/s pendant une chauffe ou une impression).
// Reconstruite seulement si sa STRUCTURE change (cle_structure) ; sinon ces valeurs sont mises à jour sur place.
static lv_obj_t *o_pre[4], *o_cons[3], *o_sw[4]; // 27/10 : Réglages mis à jour sur place (maj_reglages)
static lv_obj_t *o_cam_pct, *o_titre_f, *o_detail, *o_t_act[3], *o_t_cons[3], *o_r_act[3], *o_puissance, *o_pm;
static char g_cle[96];

static void construire();
static void maj_reglages();
static void envoyer(pr_action_t a, int arg = 0)
{
    if (pr_commande)
        pr_commande(a, arg);
}
static const pr_fichier_t *fichier_affiche()
{
    const pr_etat_imp_t &p = pr_etat;
    int i = (p.etat == PR_IMPRESSION || p.etat == PR_PAUSE) ? p.travail : p.choix;
    return (i >= 0 && i < p.nb_fichiers) ? &p.fichiers[i] : nullptr;
}

lv_obj_t *page_imprimante_zone_camera() { return g_zone && g_vue == 0 && g_camera ? o_camera : nullptr; }

// =====================================================================  SUIVI
// 27/10 (retour d'usage : « le dégradé sur le début et la fin de l'anneau, comme le thermostat ») : 135 segments de 2°, les
// 10 derniers degrés de chaque bout fondus vers le fond (ui_anneau_dessiner, ouverture vide)
static void anneau_cb(lv_event_t *e)
{
    uc_cible_t * ctx = uc_cible_du(e); // 9.6 : la layer de l'événement (avant : lv_event_get_draw_ctx)
    lv_area_t a;
    lv_obj_get_coords(uc_objet_du(e), &a);
    lv_point_t c = {(lv_coord_t)(a.x1 + lv_area_get_width(&a) / 2), (lv_coord_t)(a.y1 + lv_area_get_height(&a) / 2)};
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.width = 19; // 27/10 : 14 -> 19 (retour d'usage)
    d.rounded = 0;
    const pr_etat_imp_t &p = pr_etat;
    const bool en_cours = p.etat == PR_IMPRESSION || p.etat == PR_PAUSE;
    const int NB = 135, fait = en_cours ? p.progression * NB / 100 : 0;
    const lv_color_t plein = ui_c(p.etat == PR_PAUSE ? UI_TEXTE2 : UI_ACCENT), vide = ui_c(UI_CARTE2);
    for (int k = 0; k < NB; k++)
    {
        d.color = k < fait ? plein : vide;
        if (k < 5 || k >= NB - 5)
        {
            const int bord = k < 5 ? k : NB - 1 - k; // 0 au bout
            d.color = lv_color_mix(d.color, ui_c(UI_FOND), (uint8_t)(255 * (bord + 0.5f) / 5));
        }
        const uint16_t deb = 135 + k * 2, fin = deb + (k == NB - 1 ? 2 : 3); // 1° de recouvrement contre les fentes
        uc_arc(ctx, d, &c, ANNEAU / 2, deb % 360, fin % 360); // 9.6 : angles dans le descripteur (uc_arc)
    }
    // NOTE coût 9.6 : 135 tasks d'arc par redraw — la famille que la vague B estime à ~1000 tasks par courbe,
    // ajout de task en O(n) (src/draw/lv_draw.c:112-121). Cet anneau (journal, candidat n°2) est le plus gros
    // consommateur de tasks de la page.
}

static void camera_cb(lv_event_t *)
{
    if (pr_etat.etat == PR_ETEINTE)
        return; // rien si la machine est éteinte (inchangé)
    // 08/11/2026 (retour d'usage) : cycle à trois vues à chaque appui : STL -> caisson -> plateau -> STL.
    g_camera = (g_camera + 1) % 3;
    construire();
}

static void action_cb(lv_event_t *e)
{
    int b = (int)(intptr_t)lv_event_get_user_data(e); // 0 = bouton de gauche, 1 = de droite
    pr_etat_imp_t &p = pr_etat;
    if (g_confirme)
    {
        g_confirme = false;
        if (b == 1)
            envoyer(PR_ARRETER);
    }
    else
        switch (p.etat)
        {
        case PR_IMPRESSION:
        case PR_PAUSE:
            if (b == 0)
                envoyer(PR_PAUSE_REPRISE);
            else
                g_confirme = true; // arrêter : confirmation sur place
            break;
        case PR_PRETE: // 27/10 : un seul bouton, « Éteindre » ; « Lancer » est dans l'aperçu des Fichiers (lancer_cb) — l'ancienne
                       // correspondance (0 = Lancer) faisait LANCER une impression à l'appui sur « Éteindre » (retour d'usage)
            if (b == 0)
                envoyer(PR_ETEINDRE);
            break;
        case PR_ETEINTE:
            if (b == 0)
                envoyer(PR_ALLUMER);
            break;
        }
    construire();
}

static void hhmm(char *b, size_t n, int m) { snprintf(b, n, TXT("%dh%02d", "%02d:%02d"), m / 60 % 24, m % 60); }

static lv_obj_t *bouton(lv_obj_t *parent, lv_coord_t l, lv_coord_t h, const char *txt, int genre)
{
    // genre : 0 neutre, 1 principal (ambre), 2 danger (contour rouge), 3 danger plein
    lv_obj_t *b = ui_bouton(parent, l, h);
    if (genre == 1)
        ui_style_ajouter(b, &ui_st_bouton_principal, 0);
    if (genre >= 2)
    {
        ui_style_bg_color(b, ui_c(genre == 3 ? UI_ROUGE : 0x2A1717), 0);
        ui_style_border_color(b, ui_c(genre == 3 ? UI_ROUGE : 0x5A2A26), 0);
        ui_style_text_color(b, ui_c(genre == 3 ? UI_SUR_ACCENT : 0xFF8A7A), 0);
    }
    lv_obj_t *t = lv_label_create(b);
    ui_label_texte(t, txt);
    lv_obj_center(t);
    return b;
}

static void construire_suivi(lv_obj_t *z)
{
    const pr_etat_imp_t &p = pr_etat;
    const pr_fichier_t *f = fichier_affiche();
    
    if (g_camera && p.etat != PR_ETEINTE)
    {
        // caméra : les images de motionEye (ui_camera), tant que la vue est affichée. 08/11 : deux flux selon la vue —
        // le caisson ou le plateau. Un seul flux à la fois : ui_camera_afficher reconnaît l'URL (ui_camera.cpp:705) et
        // repart de zéro quand elle change, la boucle coupe le précédent (ui_camera_couper).
        o_camera = ui_conteneur(z);
        ui_style_ajouter(o_camera, &ui_st_carte, 0);
        lv_obj_set_style_pad_all(o_camera, 0, 0);
        lv_obj_set_style_clip_corner(o_camera, true, 0);
        lv_obj_set_size(o_camera, CAM_L, CAM_H);
        lv_obj_set_pos(o_camera, (UI_ZONE_L - CAM_L) / 2, 50);
        o_cam_pct = ui_label(o_camera, UI_POLICE_3X5_P, UI_TEXTE, "");
        lv_obj_align(o_cam_pct, LV_ALIGN_BOTTOM_LEFT, 12, -10);
        // l'image se pose sous le pourcentage
        ui_camera_afficher(o_camera, g_camera == VUE_PLATEAU ? UI_CAM_PLATEAU : UI_CAM_IMPRIMANTE);
    }
    else
    {
        o_anneau = ui_conteneur(z);
        lv_obj_set_size(o_anneau, ANNEAU, ANNEAU);
        lv_obj_set_pos(o_anneau, (UI_ZONE_L - ANNEAU) / 2, ANNEAU_Y);
        lv_obj_add_event_cb(o_anneau, anneau_cb, LV_EVENT_DRAW_MAIN, NULL);
        lv_obj_clear_flag(o_anneau, LV_OBJ_FLAG_CLICKABLE);
        if (f && f->vignette && p.etat != PR_ETEINTE)
        {
            // 27/10 (retour d'usage : « pas de remplissage ») : la miniature entière ; l'anneau seul donne l'avancement (le
            // remplissage de bas en haut laissait une bande sombre au-dessus de la couche en cours)
            lv_obj_t *v = lv_img_create(o_anneau);
            ui_img_src(v, f->vignette);
            lv_obj_align(v, LV_ALIGN_CENTER, 0, -8);
        }
        else
        {
            lv_obj_t *n = ui_label(o_anneau, UI_POLICE_PETITE, UI_TEXTE3, p.etat == PR_ETEINTE ? TXT("Imprimante éteinte", "Printer off") : TXT("pas de miniature", "no thumbnail"));
            lv_obj_center(n);
        }
        o_pct = ui_label(z, UI_POLICE_TEMP_PETITE, UI_TEXTE, ""); // 27/10 : 37 -> 49 px (retour d'usage « agrandis le pourcentage »)
    }

    // bouton de vue : dans le coin haut droit de la caméra, à 10 px de ses deux bords (même place sur l'anneau).
    // 08/11/2026 : le libellé dit ce que le PROCHAIN appui affiche — c'était déjà ce sens (« Pièce » quand la caméra
    // était affichée). Avec les trois vues : « Caisson » depuis la miniature, « Plateau » depuis le caisson,
    // « Pièce » depuis le plateau. 66 px de large : « Caisson » et « Plateau » mesurent ~48 px en 12 SemiBold.
    lv_obj_t *cb = bouton(z, 66, 34, g_camera == VUE_STL ? TXT("Caisson", "Enclosure") : g_camera == VUE_CAISSON ? TXT("Plateau", "Bed") : TXT("Pièce", "Part"), 0);
    ui_style_text_font(cb, UI_POLICE_LEGENDE, 0);
    lv_obj_set_pos(cb, (UI_ZONE_L - CAM_L) / 2 + CAM_L - 66 - 10, 50 + 10);
    ui_style_opa(cb, p.etat == PR_ETEINTE ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_add_event_cb(cb, camera_cb, LV_EVENT_CLICKED, NULL);

    // 27/10 (retour d'usage) : particules fines PM1 de l'AQM : icône + valeur dans la police pixel du pourcentage de la caméra,
    // sans fond (« ce n'est pas un bouton ») ; en haut à gauche sur l'anneau (miroir du bouton), en bas à droite sur la
    // caméra (miroir du pourcentage) ; sur la caméra, enfant de la zone : l'image ne passe pas dessous
    lv_obj_t *pm = ui_conteneur(g_camera && o_camera ? o_camera : z);
    lv_obj_set_size(pm, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(pm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(pm, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pm, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pm, 6, 0);
    ui_icone(pm, &icon_particules_28, UI_TEXTE2);
    o_pm = ui_label(pm, UI_POLICE_3X5_P, UI_TEXTE, "");
    if (g_camera && o_camera)
        lv_obj_align(pm, LV_ALIGN_BOTTOM_RIGHT, -12, -6);
    else
        lv_obj_set_pos(pm, (UI_ZONE_L - CAM_L) / 2 + 10, 50 + 10 + (34 - 28) / 2);

    // titre + détail
    o_titre_f = ui_label(z, UI_POLICE_TEXTE, UI_TEXTE, "");
    lv_label_set_long_mode(o_titre_f, LV_LABEL_LONG_DOT);
    lv_obj_set_size(o_titre_f, UI_ZONE_L - 2 * UI_MARGE, 20);
    lv_obj_set_style_text_align(o_titre_f, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(o_titre_f, UI_MARGE, TITRE_Y);
    o_detail = ui_label(z, UI_POLICE_PETITE, UI_TEXTE2, "");

    // températures
    lv_obj_t *r = ui_conteneur(z);
    lv_obj_set_size(r, UI_ZONE_L - 2 * UI_MARGE, 60);
    lv_obj_set_pos(r, UI_MARGE, TEMP_Y);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *noms[] = {TXT("Buse", "Nozzle"), TXT("Plateau", "Bed"), TXT("Caisson", "Enclosure")};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *c = ui_conteneur(r);
        ui_style_ajouter(c, &ui_st_carte, 0);
        lv_obj_set_size(c, 121, 60);
        lv_obj_set_style_pad_hor(c, 10, 0);
        lv_obj_set_style_pad_ver(c, 6, 0);
        lv_obj_set_pos(ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE2, noms[i]), 0, 0);
        o_t_act[i] = ui_label(c, UI_POLICE_3X5_P, UI_TEXTE, "");
        lv_obj_set_pos(o_t_act[i], 0, 18);
        if (p.etat == PR_ETEINTE)
            continue;
        o_t_cons[i] = ui_label(c, UI_POLICE_LEGENDE, UI_TEXTE3, "");
        lv_obj_align(o_t_cons[i], LV_ALIGN_BOTTOM_RIGHT, 0, -2);
    }

}

// la ligne d'état (Suivi et Marche) ; sa couleur
static uint32_t detail(char *t, size_t n)
{
    const pr_etat_imp_t &p = pr_etat;
    const pr_fichier_t *f = fichier_affiche();
    char h[12];
    if (p.etat == PR_IMPRESSION)
    {
        hhmm(h, sizeof h, p.fin_minute);
        snprintf(t, n, TXT("Couche %d / %d · reste %d h %02d · fin %s", "Layer %d / %d · %d h %02d left · ends %s"),
             p.couche, p.couches, p.reste_min / 60, p.reste_min % 60, h);
    }
    else if (p.etat == PR_PAUSE)
    {
        snprintf(t, n, TXT("En pause · couche %d / %d", "Paused · layer %d / %d"), p.couche, p.couches);
        return UI_ACCENT;
    }
    else if (p.etat == PR_PRETE && f)
        snprintf(t, n, TXT("Prête · %s · %d h %02d", "Ready · %s · %d h %02d"), f->matiere, f->minutes / 60, f->minutes % 60);
    else if (p.etat == PR_PRETE)
        snprintf(t, n, TXT("Prête", "Ready"));
    else
        snprintf(t, n, TXT("Klipper, caméra et Pi arrêtés", "Klipper, camera and Pi off"));
    return UI_TEXTE2;
}

// les valeurs du Suivi (sur place, gardées : rien n'est redessiné si rien n'a changé)
static void maj_suivi()
{
    const pr_etat_imp_t &p = pr_etat;
    const bool en_cours = p.etat == PR_IMPRESSION || p.etat == PR_PAUSE;
    const pr_fichier_t *f = fichier_affiche();
    char t[64];
    if (o_pm)
    {
        if (p.pm1 < 0)
            ui_label_texte(o_pm, "--"); // la police 3x5 n'a pas de « – »
        else if (p.pm1 < 10)
            ui_label_texte_fmt(o_pm, "%d" SEP_DEC "%d", (int)p.pm1, (int)(p.pm1 * 10) % 10); // virgule à la française
        else
            ui_label_texte_fmt(o_pm, "%d", (int)lroundf(p.pm1));
    }
    if (o_cam_pct)
    {
        if (en_cours)
            ui_label_texte_fmt(o_cam_pct, "%d%%", p.progression);
        else
            ui_label_texte(o_cam_pct, ""); // 26/10 : rien sur l'image quand rien n'imprime (« 0 % » vu sur l'écran)
    }
    if (o_pct)
    {
        if (en_cours)
            ui_label_texte_fmt(o_pct, "%d%%", p.progression);
        else
            ui_label_texte(o_pct, p.etat == PR_ETEINTE ? "OFF" : TXT("PRÊTE", "READY"));
        ui_style_text_color(o_pct, ui_c(p.etat == PR_PAUSE ? UI_TEXTE2 : en_cours ? UI_TEXTE : UI_TEXTE3), 0);
        ui_aligner(o_pct, LV_ALIGN_TOP_MID, 0, ANNEAU_Y + ANNEAU - 48);
    }
    if (o_anneau && en_cours)
    {
        static int prog = -1; // l'anneau (dessiné) : seulement quand la progression change
        if (prog != p.progression)
        {
            prog = p.progression;
            lv_obj_invalidate(o_anneau);
        }
    }
    ui_label_texte(o_titre_f, p.etat == PR_ETEINTE ? TXT("Éteinte", "Off") : f ? f->nom : TXT("Prête", "Ready"));
    const uint32_t cd = detail(t, sizeof t);
    ui_label_texte(o_detail, t);
    ui_style_text_color(o_detail, ui_c(cd), 0);
    ui_aligner(o_detail, LV_ALIGN_TOP_MID, 0, TITRE_Y + 22);
    const int act[] = {p.buse, p.plateau, p.caisson}, cons[] = {p.buse_c, p.plateau_c, p.caisson_c};
    for (int i = 0; i < 3; i++)
    {
        if (p.etat == PR_ETEINTE)
        {
            ui_label_texte(o_t_act[i], "--"); // la police 3x5 n'a pas de « – » (rectangle vide, vu au simulateur le 23/10)
            ui_style_text_color(o_t_act[i], ui_c(UI_TEXTE3), 0);
            continue;
        }
        ui_label_texte_fmt(o_t_act[i], "%d°", act[i]);
        ui_style_text_color(o_t_act[i], ui_c(cons[i] > 0 && act[i] < cons[i] - 2 ? UI_CHAUFFE : UI_TEXTE), 0); // en chauffe
        if (cons[i] > 0)
            ui_label_texte_fmt(o_t_cons[i], "→ %d°", cons[i]);
        else
            ui_label_texte(o_t_cons[i], TXT("arrêt", "off"));
        ui_aligner(o_t_cons[i], LV_ALIGN_BOTTOM_RIGHT, 0, -2);
    }
}

// =====================================================================  RÉGLAGES
static void prechauffe_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (pr_etat.etat == PR_ETEINTE)
        return;
    pr_etat.buse_c = PRECHAUFFES[i].buse;
    pr_etat.plateau_c = PRECHAUFFES[i].plateau;
    pr_etat.caisson_c = PRECHAUFFES[i].caisson;
    envoyer(PR_CONSIGNES);
    maj_reglages(); // 27/10 : sur place (reconstruire faisait clignoter toute la vue)
}

static void consigne_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e); // ligne * 2 + (1 = plus)
    if (pr_etat.etat == PR_ETEINTE)
        return;
    int *v[] = {&pr_etat.buse_c, &pr_etat.plateau_c, &pr_etat.caisson_c};
    const int maxi[] = {280, 110, 60};
    int i = code / 2;
    *v[i] = LV_CLAMP(0, *v[i] + (code % 2 ? 5 : -5), maxi[i]);
    envoyer(PR_CONSIGNES);
    maj_reglages();
}

static void inter_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (pr_etat.etat == PR_ETEINTE)
        return;
    bool *v[] = {&pr_etat.lumiere, &pr_etat.filtration, &pr_etat.chauffage, &pr_etat.carte_mere};
    *v[i] = !*v[i];
    envoyer(PR_INTER, i);
    maj_reglages(); // 27/10 : seul cet interrupteur change (retour d'usage : « les commutateurs clignotent tous »)
}

static void origine_cb(lv_event_t *)
{
    if (pr_etat.etat == PR_PRETE)
        envoyer(PR_ORIGINE);
}

static void construire_reglages(lv_obj_t *z)
{
    const pr_etat_imp_t &p = pr_etat;
    const bool off = p.etat == PR_ETEINTE;
    lv_obj_t *page = ui_conteneur(z);
    lv_obj_set_size(page, UI_ZONE_L, UI_ZONE_H - 50);
    lv_obj_set_pos(page, 0, 50);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    ui_defilement_rapide(page); // 05/11 : scroll par bande (garde = style lu a la creation)
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_hor(page, UI_MARGE, 0);
    lv_obj_set_style_pad_bottom(page, 16, 0);
    lv_obj_set_style_pad_row(page, 10, 0);
    lv_obj_set_style_pad_column(page, 6, 0);
    ui_style_opa(page, off ? LV_OPA_50 : LV_OPA_COVER, 0);

    ui_titre_section(page, TXT("PRÉCHAUFFE", "PREHEAT"));
    lv_obj_set_width(lv_obj_get_child(page, 0), lv_pct(100));
    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *b = ui_bouton(page, (UI_ZONE_L - 2 * UI_MARGE - 18) / 4, 52);
        ui_style_ajouter(b, &ui_st_pastille_active, LV_STATE_CHECKED); // la préchauffe en cours : maj_reglages
        o_pre[i] = b;
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label_texte(lv_label_create(b), PRECHAUFFES[i].nom);
        lv_obj_t *s = lv_label_create(b);
        ui_style_text_font(s, UI_POLICE_LEGENDE, 0);
        if (i)
            ui_label_texte_fmt(s, "%d / %d / %d", PRECHAUFFES[i].buse, PRECHAUFFES[i].plateau, PRECHAUFFES[i].caisson);
        else
            ui_label_texte(s, TXT("tout couper", "turn all off"));
        lv_obj_add_event_cb(b, prechauffe_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *c = ui_conteneur(page);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(c, 0, 0);
    lv_obj_set_style_pad_right(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    const char *noms[] = {TXT("Buse", "Nozzle"), TXT("Plateau", "Bed"), TXT("Caisson", "Enclosure")};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *r = ui_conteneur(c);
        lv_obj_set_size(r, lv_pct(100), 56);
        if (i < 2)
        {
            lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
            ui_style_border_width(r, 1, 0);
            ui_style_border_color(r, ui_c(UI_BORD), 0);
        }
        lv_obj_align(ui_label(r, UI_POLICE_TEXTE, UI_TEXTE, noms[i]), LV_ALIGN_TOP_LEFT, 0, 9);
        o_r_act[i] = ui_label(r, UI_POLICE_LEGENDE, UI_TEXTE2, ""); // maj_reglages
        lv_obj_align(o_r_act[i], LV_ALIGN_BOTTOM_LEFT, 0, -9);
        lv_obj_t *plus = ui_bouton(r, 44, 44);
        ui_style_bg_color(plus, ui_c(UI_CARTE2), 0);
        lv_obj_center(ui_label(plus, UI_POLICE_TITRE, UI_TEXTE, "+"));
        lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(plus, consigne_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 2 + 1));
        o_cons[i] = ui_label(r, UI_POLICE_3X5_P, UI_TEXTE2, ""); // maj_reglages
        lv_obj_t *moins = ui_bouton(r, 44, 44);
        ui_style_bg_color(moins, ui_c(UI_CARTE2), 0);
        lv_obj_center(ui_label(moins, UI_POLICE_TITRE, UI_TEXTE, "−"));
        lv_obj_align(moins, LV_ALIGN_RIGHT_MID, -128, 0);
        lv_obj_add_event_cb(moins, consigne_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i * 2));
    }

    ui_titre_section(page, TXT("CAISSON", "ENCLOSURE"));
    lv_obj_set_width(lv_obj_get_child(page, lv_obj_get_child_cnt(page) - 1), lv_pct(100));
    const char *inters[] = {TXT("Lumière", "Light"), TXT("Filtration", "Filtration"), TXT("Chauffage", "Heating"), TXT("Carte mère", "Mainboard")};
    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *b = ui_bouton(page, (UI_ZONE_L - 2 * UI_MARGE - 8) / 2, 56);
        lv_obj_set_style_pad_hor(b, 12, 0);
        lv_obj_align(ui_label(b, UI_POLICE_TEXTE, UI_TEXTE, inters[i]), LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *sw = lv_switch_create(b);
        lv_obj_set_size(sw, 44, 26);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
        ui_interrupteur_couleurs(sw);
        o_sw[i] = sw; // état : maj_reglages
        lv_obj_add_event_cb(b, inter_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    ui_titre_section(page, "AXES");
    lv_obj_set_width(lv_obj_get_child(page, lv_obj_get_child_cnt(page) - 1), lv_pct(100));
    const bool possible = p.etat == PR_PRETE;
    lv_obj_t *o = bouton(page, UI_ZONE_L - 2 * UI_MARGE, 52,
                         possible ? TXT("Mise à l'origine des axes", "Home all axes") : off ? TXT("Mise à l'origine (imprimante éteinte)", "Home axes (printer off)") : TXT("Mise à l'origine (impression en cours)", "Home axes (printing)"), 0);
    ui_style_text_color(o, ui_c(possible ? UI_TEXTE : UI_TEXTE3), 0);
    lv_obj_add_event_cb(o, origine_cb, LV_EVENT_CLICKED, NULL);
}

// 27/10 : tout ce que l'état change dans la vue, posé sur place et seulement s'il change (la vue n'est plus reconstruite
// à chaque interrupteur, préchauffe, consigne ou écho de HA : tout clignotait)
static void etat_coche(lv_obj_t *o, bool oui)
{
    if (o && lv_obj_has_state(o, LV_STATE_CHECKED) != oui)
        oui ? lv_obj_add_state(o, LV_STATE_CHECKED) : lv_obj_clear_state(o, LV_STATE_CHECKED);
}

static void maj_reglages()
{
    const pr_etat_imp_t &p = pr_etat;
    const bool off = p.etat == PR_ETEINTE;
    for (int i = 0; i < 4; i++)
        etat_coche(o_pre[i], p.buse_c == PRECHAUFFES[i].buse && p.plateau_c == PRECHAUFFES[i].plateau && p.caisson_c == PRECHAUFFES[i].caisson);
    const int cons[] = {p.buse_c, p.plateau_c, p.caisson_c};
    for (int i = 0; i < 3 && o_cons[i]; i++)
    {
        if (cons[i])
            ui_label_texte_fmt(o_cons[i], "%d°", cons[i]);
        else
            ui_label_texte(o_cons[i], "OFF");
        ui_style_text_color(o_cons[i], ui_c(cons[i] ? UI_ACCENT : UI_TEXTE2), 0);
        ui_aligner(o_cons[i], LV_ALIGN_RIGHT_MID, -60, 0);
    }
    const bool etats[] = {p.lumiere, p.filtration, p.chauffage, p.carte_mere};
    for (int i = 0; i < 4; i++)
        etat_coche(o_sw[i], etats[i] && !off);
    const int act[] = {p.buse, p.plateau, p.caisson};
    for (int i = 0; i < 3; i++)
    {
        if (p.etat == PR_ETEINTE)
            ui_label_texte(o_r_act[i], TXT("actuel –", "now –"));
        else
            ui_label_texte_fmt(o_r_act[i], TXT("actuel %d°", "now %d°"), act[i]);
        ui_aligner(o_r_act[i], LV_ALIGN_BOTTOM_LEFT, 0, -9);
    }
}

// =====================================================================  FICHIERS
// 27/10 : un appui ouvre l'aperçu du fichier (grande miniature, détails, « Lancer ») ; « ‹ Fichiers » revient à la liste
static void fichier_cb(lv_event_t *e)
{
    pr_etat.choix = g_apercu = (int)(intptr_t)lv_event_get_user_data(e);
    construire();
}
static void retour_cb(lv_event_t *)
{
    g_apercu = -1;
    construire();
}
static void lancer_cb(lv_event_t *)
{
    if (pr_etat.etat != PR_PRETE)
        return;
    envoyer(PR_LANCER, pr_etat.choix);
    g_vue = 0;
    g_apercu = -1;
    construire();
}

static void construire_fichiers(lv_obj_t *z)
{
    const pr_etat_imp_t &p = pr_etat;
    lv_obj_t *l = ui_conteneur(z);
    lv_obj_set_size(l, UI_ZONE_L, UI_ZONE_H - 50); // jusqu'en bas : plus de bouton « Lancer » sous la liste
    lv_obj_set_pos(l, 0, 50);
    lv_obj_add_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(l, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_OFF);
    ui_defilement_rapide(l); // 05/11 : scroll par bande (garde = style lu a la creation)
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(l, UI_MARGE, 0);
    lv_obj_set_style_pad_row(l, 6, 0);
    lv_obj_set_style_pad_bottom(l, 8, 0);
    for (int i = 0; i < p.nb_fichiers; i++)
    {
        const pr_fichier_t &f = p.fichiers[i];
        const bool choisi = i == p.travail && (p.etat == PR_IMPRESSION || p.etat == PR_PAUSE); // celui qui s'imprime
        lv_obj_t *b = ui_bouton(l, lv_pct(100), 64);
        lv_obj_set_style_pad_hor(b, 6, 0);
        lv_obj_set_style_pad_right(b, 12, 0);
        if (choisi)
        {
            ui_style_bg_color(b, ui_c(UI_ACCENT_FOND), 0);
            ui_style_border_color(b, ui_c(UI_ACCENT), 0);
        }
        if (f.mini)
        {
            lv_obj_t *v = lv_img_create(b);
            ui_img_src(v, f.mini);
            lv_obj_align(v, LV_ALIGN_LEFT_MID, 0, 0);
        }
        lv_obj_t *n = ui_label(b, UI_POLICE_TEXTE, UI_TEXTE, f.nom);
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_set_size(n, 230, 20);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 62, 12);
        lv_obj_t *d = ui_label(b, UI_POLICE_LEGENDE, UI_TEXTE2, "");
        ui_label_texte_fmt(d, "%s · %s", f.matiere, f.date);
        lv_obj_align(d, LV_ALIGN_BOTTOM_LEFT, 62, -12);
        lv_obj_t *du;
        lv_obj_align(ui_valeur_unite(b, UI_POLICE_3X5_P, choisi ? UI_ACCENT : UI_TEXTE2, "h", &du), LV_ALIGN_RIGHT_MID, 0, 0);
        ui_label_texte_fmt(du, "%d:%02d", f.minutes / 60, f.minutes % 60); // « 1:44 h » : comme les durées de la musique
        lv_obj_add_event_cb(b, fichier_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void construire_apercu(lv_obj_t *z)
{
    const pr_etat_imp_t &p = pr_etat;
    const pr_fichier_t &f = p.fichiers[g_apercu];
    lv_obj_t *r = bouton(z, 110, 36, TXT("← Fichiers", "← Files"), 0); // « ← » : dans la police 16 (pas les symboles de LVGL)
    lv_obj_set_pos(r, UI_MARGE, 54);
    lv_obj_add_event_cb(r, retour_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *n = ui_label(z, UI_POLICE_TITRE, UI_TEXTE, f.nom);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_obj_set_width(n, UI_ZONE_L - 2 * UI_MARGE - 116);
    lv_obj_set_pos(n, UI_MARGE + 116, 60);

    // la grande miniature (celle de l'anneau du Suivi), sur une carte
    lv_obj_t *c = ui_conteneur(z);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, VIG + 32, VIG + 32);
    lv_obj_set_pos(c, (UI_ZONE_L - VIG - 32) / 2, 102);
    if (f.vignette)
    {
        lv_obj_t *v = lv_img_create(c);
        ui_img_src(v, f.vignette);
        lv_obj_center(v);
    }
    else
        lv_obj_center(ui_label(c, UI_POLICE_PETITE, UI_TEXTE3, TXT("pas de miniature", "no thumbnail")));

    // détails : matière et poids, durée, date de tranchage
    lv_obj_t *d = ui_conteneur(z);
    lv_obj_set_size(d, UI_ZONE_L - 2 * UI_MARGE, 44);
    lv_obj_set_pos(d, UI_MARGE, 102 + VIG + 32 + 10);
    lv_obj_set_flex_flow(d, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(d, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    char duree[16];
    snprintf(duree, sizeof duree, "%d h %02d", f.minutes / 60, f.minutes % 60);
    const char *val[] = {f.matiere, duree, f.date}, *leg[] = {TXT("matière", "material"), TXT("durée", "duration"), TXT("tranché le", "sliced")};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *k = ui_conteneur(d);
        lv_obj_set_size(k, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(k, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(k, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label(k, UI_POLICE_TEXTE, UI_TEXTE, val[i]);
        ui_label(k, UI_POLICE_LEGENDE, UI_TEXTE3, leg[i]);
    }

    const bool lancable = p.etat == PR_PRETE;
    lv_obj_t *b = bouton(z, UI_ZONE_L - 2 * UI_MARGE, 56,
                         lancable ? TXT("Lancer l'impression", "Start print") : p.etat == PR_ETEINTE ? TXT("Allumer l'imprimante d'abord (Marche)", "Turn printer on first (Power)") : TXT("Une impression est en cours", "A print is running"),
                         lancable ? 1 : 0);
    if (!lancable)
        ui_style_text_color(b, ui_c(UI_TEXTE3), 0);
    lv_obj_set_pos(b, UI_MARGE, UI_ZONE_H - 56 - 14);
    lv_obj_add_event_cb(b, lancer_cb, LV_EVENT_CLICKED, NULL);
}

// =====================================================================  MARCHE
// 27/10 (retour d'usage : « allumer, éteindre en 4e onglet ») : les commandes de la machine, selon son état ; l'arrêt d'une
// impression se confirme sur place
static void construire_marche(lv_obj_t *z)
{
    const pr_etat_imp_t &p = pr_etat;
    lv_obj_t *c = ui_conteneur(z);
    ui_style_ajouter(c, &ui_st_carte, 0);
    lv_obj_set_size(c, UI_ZONE_L - 2 * UI_MARGE, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_bottom(c, 14, 0);
    lv_obj_set_pos(c, UI_MARGE, 56);
    const char *etat = p.etat == PR_ETEINTE ? TXT("Éteinte", "Off")
                       : p.etat == PR_PRETE  ? TXT("Allumée, prête", "On, ready")
                       : p.etat == PR_PAUSE  ? TXT("En pause", "Paused")
                                             : TXT("Impression en cours", "Printing");
    const uint32_t couleur = p.etat == PR_ETEINTE ? UI_TEXTE3 : p.etat == PR_PAUSE ? UI_ACCENT : UI_VERT;
    lv_obj_t *pt = ui_conteneur(c);
    lv_obj_set_size(pt, 12, 12);
    lv_obj_set_style_radius(pt, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(pt, LV_OPA_COVER, 0);
    ui_style_bg_color(pt, ui_c(couleur), 0);
    lv_obj_align(pt, LV_ALIGN_TOP_LEFT, 4, 12);
    lv_obj_align(ui_label(c, UI_POLICE_TITRE, UI_TEXTE, etat), LV_ALIGN_TOP_LEFT, 26, 4);
    const pr_fichier_t *f = fichier_affiche();
    char t[64];
    const uint32_t cd = detail(t, sizeof t);
    lv_obj_t *d = ui_label(c, UI_POLICE_PETITE, cd, t);
    lv_label_set_long_mode(d, LV_LABEL_LONG_DOT);
    lv_obj_set_width(d, UI_ZONE_L - 2 * UI_MARGE - 56);
    lv_obj_align(d, LV_ALIGN_TOP_LEFT, 26, 34);
    // 27/10 (retour d'usage) : la puissance du wattmètre de la prise, en haut à droite, sans légende (le « W » suffit ; la
    // légende touchait la ligne d'état pendant une impression) ; mise à jour sur place : maj_marche
    lv_obj_t *pu = ui_valeur_unite(c, UI_POLICE_3X5_M, UI_TEXTE, "W", &o_puissance);
    lv_obj_align(pu, LV_ALIGN_TOP_RIGHT, -4, 4);
    if ((p.etat == PR_IMPRESSION || p.etat == PR_PAUSE) && f) // le fichier imprimé, puis le détail en dessous
    {
        lv_obj_align(ui_label(c, UI_POLICE_PETITE, UI_TEXTE2, f->nom), LV_ALIGN_TOP_LEFT, 26, 34);
        lv_obj_align(d, LV_ALIGN_TOP_LEFT, 26, 56);
    }

    const char *a = "", *b = nullptr;
    int ga = 0, gb = 0;
    if (g_confirme)
        a = TXT("Annuler", "Cancel"), b = TXT("Confirmer l'arrêt", "Confirm stop"), gb = 3;
    else if (p.etat == PR_IMPRESSION)
        a = TXT("Pause", "Pause"), b = TXT("Arrêter l'impression", "Stop print"), gb = 2;
    else if (p.etat == PR_PAUSE)
        a = TXT("Reprendre", "Resume"), ga = 1, b = TXT("Arrêter l'impression", "Stop print"), gb = 2;
    else if (p.etat == PR_PRETE)
        a = TXT("Éteindre l'imprimante", "Turn printer off");
    else
        a = TXT("Allumer l'imprimante", "Turn printer on"), ga = 1;
    lv_obj_add_event_cb(bouton(z, UI_ZONE_L - 2 * UI_MARGE, 64, a, ga), action_cb, LV_EVENT_CLICKED, (void *)0);
    lv_obj_set_pos(lv_obj_get_child(z, lv_obj_get_child_cnt(z) - 1), UI_MARGE, 176);
    if (b)
    {
        lv_obj_add_event_cb(bouton(z, UI_ZONE_L - 2 * UI_MARGE, 64, b, gb), action_cb, LV_EVENT_CLICKED, (void *)1);
        lv_obj_set_pos(lv_obj_get_child(z, lv_obj_get_child_cnt(z) - 1), UI_MARGE, 252);
    }
}

// =====================================================================  PAGE
static void onglet_cb(lv_event_t *e)
{
    g_vue = (int)(intptr_t)lv_event_get_user_data(e);
    g_confirme = false;
    g_apercu = -1;
    construire();
}

// ce qui décide des objets de la vue affichée : s'il change, la vue est reconstruite ; sinon valeurs sur place
static void cle_structure(char *b, size_t n)
{
    const pr_etat_imp_t &p = pr_etat;
    const pr_fichier_t *f = fichier_affiche();
    int minis = 0;
    for (int i = 0; i < p.nb_fichiers; i++)
        minis += p.fichiers[i].mini != nullptr;
    if (g_vue == 0)
        snprintf(b, n, "0 %d %d %d %p %p", p.etat, g_camera, g_confirme, (const void *)f, f ? (const void *)f->vignette : nullptr);
    else if (g_vue == 1)
        snprintf(b, n, "1 %d", p.etat == PR_ETEINTE); // 27/10 : consignes, préchauffes, interrupteurs : sur place (maj_reglages)
    else if (g_vue == 2)
    {
        const void *vig = g_apercu >= 0 && g_apercu < p.nb_fichiers ? (const void *)p.fichiers[g_apercu].vignette : nullptr;
        snprintf(b, n, "2 %d %d %d %d %d %p", p.etat, p.nb_fichiers, p.travail, minis, g_apercu, vig);
    }
    else
        snprintf(b, n, "3 %d %d %p", p.etat, g_confirme, (const void *)f);
}

static void maj_marche()
{
    if (!o_puissance)
        return;
    if (pr_etat.puissance < 0)
        ui_label_texte(o_puissance, "--"); // la police 3x5 n'a pas de « – »
    else
        ui_label_texte_fmt(o_puissance, "%d", pr_etat.puissance);
}

static void maj_valeurs()
{
    if (g_vue == 0)
        maj_suivi();
    else if (g_vue == 1)
        maj_reglages();
    else if (g_vue == 3)
        maj_marche();
}

static void construire()
{
    lv_obj_clean(g_zone);
    o_camera = o_pct = o_anneau = o_cam_pct = o_puissance = o_pm = nullptr;
    memset(o_t_cons, 0, sizeof o_t_cons);
    memset(o_pre, 0, sizeof o_pre);
    memset(o_cons, 0, sizeof o_cons);
    memset(o_sw, 0, sizeof o_sw);
    static const char *const ONGLETS[] = {TXT("Suivi", "Status"), TXT("Réglages", "Settings"), TXT("Fichiers", "Files"), TXT("Marche", "Power")};
    ui_onglets(g_zone, ONGLETS, 4, g_vue, onglet_cb, nullptr, nullptr);
    if (g_apercu >= pr_etat.nb_fichiers) // la liste a changé sous l'aperçu
        g_apercu = -1;
    if (g_vue == 0)
        construire_suivi(g_zone);
    else if (g_vue == 1)
        construire_reglages(g_zone);
    else if (g_vue == 2 && g_apercu >= 0)
        construire_apercu(g_zone);
    else if (g_vue == 2)
        construire_fichiers(g_zone);
    else
        construire_marche(g_zone);
    cle_structure(g_cle, sizeof g_cle);
    maj_valeurs();
}

void page_imprimante_maj()
{
    if (!g_zone)
        return;
    char cle[sizeof g_cle];
    cle_structure(cle, sizeof cle);
    if (strcmp(cle, g_cle))
        construire();
    else
        maj_valeurs();
}

void page_imprimante_creer(lv_obj_t *zone)
{
    g_zone = zone;
    g_vue = 0;
    g_apercu = -1;
    g_camera = VUE_STL; // 08/11 : en arrivant sur la page, c'est la miniature du fichier (comme avant)
    g_confirme = false;
    construire();
}

void page_imprimante_quitter()
{
    g_zone = nullptr;
}
