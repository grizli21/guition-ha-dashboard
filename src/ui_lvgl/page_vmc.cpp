#include "page_vmc.h"
#include "ui_theme.h"
#include "icones_ventilo_rgb.h" // en COULEUR : LVGL 8.4 ne fait tourner que les images couleur (pas les alpha 4/8 bits : rien ne s'affiche, vu au simulateur)

vmc_zone_t vmc_zones[VMC_NB] = {
    {TXT("Maison", "House"), 1, 0, TXT("salle de bain", "bathroom"), 80, false},
    {TXT("Sous-sol", "Basement"), 0, 0, TXT("sous-sol", "basement"), 0, false}, // fan.vmc_sous_sol ; pas d'automatisme humidité (22/10)
};
void (*vmc_commande)(int zone, int vitesse) = nullptr;

static const char *NOMS_VITESSE[] = {TXT("Arrêt", "Off"), TXT("Petite", "Low"), TXT("Grande", "High")};
static const uint32_t TOUR_MS[] = {0, 2400, 900}; // durée d'un tour de l'hélice selon la vitesse

#define CARTE_H 196

struct carte_t
{
    lv_obj_t *ventilo, *etat, *hum, *lieu, *auto_txt, *vitesses[3];
    int vitesse_animee; // vitesse pour laquelle l'animation tourne (-1 = aucune)
};
// Pointeurs valides seulement quand la page est affichée (remis à nullptr par page_vmc_quitter)
static carte_t g_cartes[VMC_NB];
static bool g_affichee = false;

// relance la rotation seulement quand la vitesse change (sinon l'hélice repartirait de zéro à chaque mise à jour)
static void animer(carte_t &c, int vitesse)
{
    if (c.vitesse_animee == vitesse)
        return;
    c.vitesse_animee = vitesse;
    ui_helice_tourner(c.ventilo, vitesse ? TOUR_MS[vitesse] : 0);
}

static void rafraichir()
{
    if (!g_affichee)
        return;
    for (int z = 0; z < VMC_NB; z++)
    {
        const vmc_zone_t &v = vmc_zones[z];
        carte_t &c = g_cartes[z];
        ui_style_img_recolor(c.ventilo, ui_c(v.vitesse ? UI_TEXTE : UI_TEXTE3), 0);
        animer(c, v.vitesse);

        ui_label_texte(c.etat, NOMS_VITESSE[v.vitesse]);
        ui_style_text_color(c.etat, ui_c(v.vitesse ? UI_TEXTE : UI_TEXTE3), 0);

        bool forte = v.seuil && v.hum >= v.seuil;
        ui_label_texte_fmt(c.hum, "%d%%", v.hum);
        ui_label_texte(c.lieu, v.lieu); // la pièce la plus humide change (HA l'envoie avec l'humidité)
        ui_style_text_color(c.hum, ui_c(forte ? UI_FROID : UI_TEXTE), 0);

        if (!v.seuil)
            ui_label_texte(c.auto_txt, TXT("Pas de grande vitesse automatique", "No automatic high speed"));
        else if (v.auto_en_cours) // humidité au-dessus du seuil OU boost douche / toilettes (input_boolean.vmc_maison_forcee)
            ui_label_texte(c.auto_txt, TXT("Grande vitesse automatique en cours", "Automatic high speed active"));
        else
            ui_label_texte_fmt(c.auto_txt, TXT("Grande vitesse automatique au-dessus de %d %% d'humidité",
                                "Automatic high speed above %d %% humidity"), v.seuil);
        ui_style_text_color(c.auto_txt, ui_c(v.auto_en_cours ? UI_FROID : UI_TEXTE2), 0);

        for (int s = 0; s < 3; s++)
            if (v.vitesse == s)
                lv_obj_add_state(c.vitesses[s], LV_STATE_CHECKED);
            else
                lv_obj_clear_state(c.vitesses[s], LV_STATE_CHECKED);
    }
}

void page_vmc_maj() { rafraichir(); }

static void vitesse_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e); // zone * 3 + vitesse
    int z = code / 3, s = code % 3;
    vmc_zones[z].vitesse = s;
    rafraichir();
    if (vmc_commande)
        vmc_commande(z, s);
}

static void creer_carte(lv_obj_t *zone, int z, lv_coord_t y)
{
    carte_t &c = g_cartes[z];
    c.vitesse_animee = -1;

    lv_obj_t *carte = ui_conteneur(zone);
    ui_style_ajouter(carte, &ui_st_carte, 0);
    lv_obj_set_size(carte, UI_ZONE_L - 2 * UI_MARGE, CARTE_H);
    lv_obj_set_pos(carte, UI_MARGE, y);

    // ventilateur + nom/vitesse à gauche, humidité à droite
    c.ventilo = ui_icone(carte, &icon_ventilo_rgb, UI_TEXTE);
    lv_obj_set_pos(c.ventilo, 0, 2);
    lv_img_set_pivot(c.ventilo, 28, 28);

    lv_obj_t *nom = ui_label(carte, UI_POLICE_TEXTE, UI_TEXTE, vmc_zones[z].nom);
    lv_obj_set_pos(nom, 70, 2);
    c.etat = ui_label(carte, UI_POLICE_TEXTE_22, UI_TEXTE, "");
    lv_obj_set_pos(c.etat, 70, 26);

    c.hum = ui_label(carte, UI_POLICE_TEMP_PETITE, UI_TEXTE, "");
    lv_obj_align(c.hum, LV_ALIGN_TOP_RIGHT, 0, 0);
    c.lieu = ui_label(carte, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_align(c.lieu, LV_ALIGN_TOP_RIGHT, 0, 48);

    c.auto_txt = ui_label(carte, UI_POLICE_LEGENDE, UI_TEXTE2, "");
    lv_obj_set_width(c.auto_txt, UI_ZONE_L - 4 * UI_MARGE);
    lv_obj_set_pos(c.auto_txt, 0, 74);

    lv_obj_t *rang = ui_conteneur(carte);
    lv_obj_set_size(rang, UI_ZONE_L - 4 * UI_MARGE - 2, 52);
    lv_obj_align(rang, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(rang, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rang, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int s = 0; s < 3; s++)
    {
        lv_obj_t *b = ui_bouton(rang, 112, 52);
        ui_style_bg_color(b, ui_c(UI_CARTE2), 0); // bouton posé dans une carte
        c.vitesses[s] = b;
        lv_obj_t *l = lv_label_create(b);
        ui_label_texte(l, NOMS_VITESSE[s]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, vitesse_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(z * 3 + s));
    }
}

void page_vmc_creer(lv_obj_t *zone)
{
    lv_obj_t *titre = ui_label(zone, UI_POLICE_VALEUR, UI_TEXTE, TXT("VMC", "Ventilation"));
    lv_obj_align(titre, LV_ALIGN_TOP_MID, 0, 2);
    creer_carte(zone, 0, 50);
    creer_carte(zone, 1, 50 + CARTE_H + 10);
    g_affichee = true;
    rafraichir();
}

void page_vmc_quitter()
{
    g_affichee = false; // rafraichir() ne touche plus à rien
    for (int z = 0; z < VMC_NB; z++)
        ui_helice_arreter(g_cartes[z].ventilo); // les icônes vont être détruites avec la zone
}
