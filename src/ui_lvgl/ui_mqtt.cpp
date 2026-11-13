#include "ui_diag.h"
#include "ui_mqtt.h"
#include "ui_demo.h" // MODE_DEMO : écho des commandes (le fichier est vide sans le drapeau)
#include "page_thermostat.h"
#include "page_ballon.h"
#include "page_vmc.h"
#include "page_air.h"
#include "page_ouvrants.h"
#include "page_croquettes.h"
#include "page_cafe.h"
#include "page_poele.h"
#include "page_lumieres.h"
#include "page_panneau_led.h" // 02/11/2026 : panneau de luminothérapie de l'atelier
#include "page_imprimante.h"
#include "ui_vignettes.h"
#include "page_musique.h"
#include "page_pc.h"
#include "page_ia.h" // 11/11/2026 : page « IA » séparée (pc/ia, pc/ia_activite)
#include "page_reglages.h"
#include "page_infos.h"
#include "ui_appareil.h"
#include "page_graphe.h"
#include "ui_sonnette.h"
#include "ui_theme.h"
#include <math.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

const char *const ui_mqtt_abonnements[] = {
    UI_MQTT_PREFIXE "maison/etat",
    UI_MQTT_PREFIXE "maison/ciel", // 12/11/2026 : scène de la page IA (soleil, lune, météo)
    UI_MQTT_PREFIXE "thermostats/+/etat",
    UI_MQTT_PREFIXE "thermostats/+/historique",
    UI_MQTT_PREFIXE "maison/historique",
    UI_MQTT_PREFIXE "ballon/etat",
    UI_MQTT_PREFIXE "ballon/historique",
    UI_MQTT_PREFIXE "vmc/+/etat",
    UI_MQTT_PREFIXE "air/+/etat",
    UI_MQTT_PREFIXE "air/+/historique",
    UI_MQTT_PREFIXE "ouvrants/+/etat",
    UI_MQTT_PREFIXE "croquettes/etat",
    UI_MQTT_PREFIXE "cafe/etat",
    UI_MQTT_PREFIXE "poele/etat",
    UI_MQTT_PREFIXE "poele/historique",
    UI_MQTT_PREFIXE "lumieres/+/etat",
    UI_MQTT_PREFIXE "panneau_led/etat", // 02/11/2026 : panneau de luminothérapie (HA ESPHome panneau_led_rouge)
    UI_MQTT_PREFIXE "imprimante/etat",
    UI_MQTT_PREFIXE "imprimante/fichiers",
    UI_MQTT_PREFIXE "imprimante/fichiers/+",
    UI_MQTT_PREFIXE "musique/etat",
    UI_MQTT_PREFIXE "pc/etat",
    UI_MQTT_PREFIXE "pc/processus",
    UI_MQTT_PREFIXE "pc/ia", // 31/10 : comptes Claude (retenu) ; 11/11 : un seul compte, la page « IA » du rail
    UI_MQTT_PREFIXE "pc/ia_activite", // 11/11 : activité de l'agent Claude (retenu) — CONTRAT_MQTT.md
    UI_MQTT_PREFIXE "maison/reglages",
    UI_MQTT_PREFIXE "sonnette/etat", // 27/10 : vue caméra de l'entrée (HA 1791000000040 / 041)
    UI_MQTT_PREFIXE "maison/appareils",
    UI_MQTT_PREFIXE "appareil/+/reglages/set", // le nom est vérifié à la réception
    nullptr,
};

ui_maison_t ui_maison = {false, true, 0};
uint32_t ui_mqtt_dernier_ha = 0;
void (*ui_mqtt_evenement)(bool connecte, uint32_t duree_ms) = nullptr;
bool (*ui_mqtt_publier)(const char *sujet, const char *message, bool retenu) = nullptr;

// Même ordre que th_pieces[]
static const char *const PIECES[TH_NB_PIECES] = {"cuisine", "sejour", "chambre1", "chambre2", "sdb"};
static const char *const MODES[] = {"arret", "horsgel", "eco", "confort", "manuel"}; // ordre de th_mode_t
static const char *const BC_MODES[] = {"arret", "auto", "force"};                     // ordre de bc_mode_t
static const char *const VMC_ZONES[VMC_NB] = {"maison", "sous_sol"};                  // même ordre que vmc_zones[]
static const char *const AIR[AQ_NB] = {"atelier", "sejour"};                           // même ordre que aq_purificateurs[]
static const char *const OUVRANTS[OV_NB + 1] = {"store_terrasse", "volet_cuisine", "toile"}; // ov_ouvrants[] puis la toile

// Après une commande, HA passe par des états intermédiaires (chauffage rallumé PUIS préréglage) et les publie :
// pendant GARDE_MS on garde ce que l'utilisateur a choisi, et on applique ensuite le DERNIER état reçu.
#define GARDE_MS 3000
static uint32_t g_garde[TH_NB_PIECES];
static bool g_en_attente[TH_NB_PIECES];
static float g_consigne_recue[TH_NB_PIECES];
static th_mode_t g_mode_recu[TH_NB_PIECES];
// Eau chaude : la page envoie tout son état, on n'envoie que ce qui a changé depuis le dernier état connu
static bc_etat_t g_bc_connu, g_bc_recu;
static uint32_t g_bc_garde;
static bool g_bc_attente;
static uint32_t g_vmc_garde[VMC_NB];
static bool g_vmc_attente[VMC_NB];
static int g_vmc_recue[VMC_NB];
static char g_vmc_lieu[VMC_NB][24]; // vmc_zones[].lieu pointe ici après le premier message
static uint32_t g_aq_garde[AQ_NB];
static bool g_aq_attente[AQ_NB];
static int g_aq_recu[AQ_NB];
static char g_aq_modes[AQ_NB][5][12]; // libellés reçus (aq_purificateurs[].modes pointe ici)
static uint32_t g_ov_garde[OV_NB + 1];
static bool g_ov_attente[OV_NB + 1];
static int g_ov_pos_recue[OV_NB + 1];
static bool g_ov_tv_recue;
// Croquettes : un repas réglé = 4 entités changées dans HA (heure, minute, quantité, actif) = 4 états intermédiaires
static uint32_t g_cq_garde;
static bool g_cq_attente;
static cq_repas_t g_cq_repas_recus[4];
// Café : un réglage change plusieurs aides dans HA (heure, température, heures récentes, programmé) -> états intermédiaires
struct cf_recu_t
{
    cf_programme_t programmes[2];
    int recentes[2];
    int temp;
};
static uint32_t g_cf_garde;
static bool g_cf_attente;
static cf_recu_t g_cf_recu;
// Poêle : ce que l'écran commande (mode, tirage manuel, brassage, réglages)
struct pl_commandable_t
{
    int mode, tirage_manuel, brassage;
    pl_reglages_t r;
};
static uint32_t g_pl_garde;
static bool g_pl_attente;
static pl_commandable_t g_pl_recu;
static const char *const PL_MODES[] = {"auto", "manuel", "levier"}; // ordre de pl_etat.mode
// Lumières : même ordre que lu_lampes[] ; garde par lampe sur ce que l'utilisateur règle
static const char *const LAMPES[LU_NB] = {"lampadaire_1", "lampadaire_2", "lampadaire_3", "lampe_fenetre", "globe_lunaire",
                                          "bandeau_wled", "bandeau_lidl", "panneau_led", "bandeau_bureau", "lampe_bureau", "chevet"};
static uint32_t g_lu_garde[LU_NB];
static bool g_lu_attente[LU_NB];
static lu_lampe_t g_lu_recue[LU_NB];
static char g_lu_noms[LU_NB][28], g_lu_pieces[LU_NB][16]; // lu_lampes[].nom / .piece pointent ici après le 1er message
// Panneau LED (02/11/2026) : ce que l'écran commande (séance, longueurs d'onde, mode, durée, les 8 intensités) ;
// le switch, le select et les numbers passent par des états intermédiaires côté ESPHome -> garde comme le Poêle
struct pnl_commandable_t
{
    bool seance;
    int longueurs, mode, duree;
    int intensites[PNL_NB_INT];
};
static uint32_t g_pnl_garde;
static bool g_pnl_attente;
static pnl_commandable_t g_pnl_recu;
static const char *const PNL_LONGUEURS[3] = {"all", "rouge_jaune", "nir"};        // ordre de pnl_longueurs_t
static const char *const PNL_MODES_PANNEAU[2] = {"minuterie", "illimite"};        // ordre de pnl_mode_t
// Imprimante : consignes et interrupteurs réglés à l'écran
struct pr_commandable_t
{
    int buse_c, plateau_c, caisson_c;
    bool lumiere, filtration, chauffage, carte_mere;
};
static uint32_t g_pr_garde;
static bool g_pr_attente;
static pr_commandable_t g_pr_recu;
static char g_pr_base[72];      // adresse du dossier des miniatures (donnée par HA)
static char g_pr_en_cours[96];  // fichier imprimé, d'après l'état (sans .gcode)

static void publier(const char *sujet, const char *message)
{
#ifdef MODE_DEMO
    // MODE DÉMO (10/11/2026) : pas de broker — la commande est interceptée ici, au point exact où elle partirait,
    // et renvoyée à ui_mqtt_recevoir telle que Home Assistant l'aurait republiée (ui_demo_echo). Sans le drapeau,
    // ces deux lignes n'existent pas : le chemin normal est strictement inchangé.
    if (ui_demo_echo(sujet, message))
        return;
#endif
    bool ok = ui_mqtt_publier && ui_mqtt_publier(sujet, message, false);
    if (!ok)
        ui_trace("[mqtt] non envoyé : %s %s\n", sujet, message);
}

// --- Écran -> HA ---
static void commande_thermostat(int piece, th_mode_t mode, float consigne)
{
    char sujet[64], message[64];
    snprintf(sujet, sizeof sujet, UI_MQTT_PREFIXE "thermostats/%s/set", PIECES[piece]);
    if (mode == TH_MANUEL)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"consigne\":%.1f}", consigne);
    else
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"mode\":\"%s\"}", MODES[mode]);
    g_garde[piece] = lv_tick_get();
    g_en_attente[piece] = false;
    publier(sujet, message);
}

static void commande_ballon(const bc_etat_t &e)
{
    const char *sujet = UI_MQTT_PREFIXE "ballon/set";
    char message[64];
    if (e.mode != g_bc_connu.mode)
    {
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"mode\":\"%s\"}", BC_MODES[e.mode]);
        publier(sujet, message);
    }
    if (e.eco != g_bc_connu.eco)
    {
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"eco\":%d}", (int)e.eco);
        publier(sujet, message);
    }
    if (e.consigne != g_bc_connu.consigne)
    {
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"consigne\":%.0f}", e.consigne);
        publier(sujet, message);
    }
    g_bc_connu = e;
    g_bc_garde = lv_tick_get();
    g_bc_attente = false;
}

static void commande_vmc(int zone, int vitesse)
{
    char sujet[64], message[48];
    snprintf(sujet, sizeof sujet, UI_MQTT_PREFIXE "vmc/%s/set", VMC_ZONES[zone]);
    snprintf(message, sizeof message, "{\"action\":\"vitesse\",\"vitesse\":%d}", vitesse);
    g_vmc_garde[zone] = lv_tick_get();
    g_vmc_attente[zone] = false;
    publier(sujet, message);
}

static void commande_air(int purificateur, int mode)
{
    char sujet[64], message[48];
    snprintf(sujet, sizeof sujet, UI_MQTT_PREFIXE "air/%s/set", AIR[purificateur]);
    snprintf(message, sizeof message, "{\"action\":\"mode\",\"mode\":%d}", mode);
    g_aq_garde[purificateur] = lv_tick_get();
    g_aq_attente[purificateur] = false;
    publier(sujet, message);
}

static void commande_ouvrant(int idx, ov_action_t a, int pos)
{
    static const char *const ACTIONS[] = {"ouvrir", "stop", "fermer"}; // ordre de ov_action_t
    char sujet[64], message[48];
    snprintf(sujet, sizeof sujet, UI_MQTT_PREFIXE "ouvrants/%s/set", OUVRANTS[idx]);
    if (a == OV_POSITION)
        snprintf(message, sizeof message, "{\"action\":\"position\",\"position\":%d}", pos);
    else if (a == OV_SEANCE)
        snprintf(message, sizeof message, "{\"action\":\"seance\",\"on\":%d}", pos ? 1 : 0);
    else
        snprintf(message, sizeof message, "{\"action\":\"%s\"}", ACTIONS[a]);
    g_ov_garde[idx] = lv_tick_get();
    g_ov_attente[idx] = false;
    publier(sujet, message);
}

static void commande_croquettes(cq_action_t a, int idx)
{
    const char *sujet = UI_MQTT_PREFIXE "croquettes/set";
    char message[96];
    if (a == CQ_REPAS)
    {
        const cq_repas_t &r = cq_etat.repas[idx];
        snprintf(message, sizeof message, "{\"action\":\"repas\",\"numero\":%d,\"h\":%d,\"q\":%d,\"a\":%d}", idx + 1,
                 r.minute, r.g, (int)r.actif);
        g_cq_garde = lv_tick_get();
        g_cq_attente = false;
    }
    else
        snprintf(message, sizeof message, "{\"action\":\"%s\"}", a == CQ_SERVIR ? "servir" : "tare");
    publier(sujet, message);
}

static void commande_cafe(int app, cf_action_t a)
{
    const char *sujet = UI_MQTT_PREFIXE "cafe/set";
    const char *nom = app == CF_CAFE ? "cafe" : "bouilloire";
    const int temp = CF_TEMPERATURES[LV_CLAMP(0, cf_temp, 4)];
    char message[112];
    if (a == CF_PROGRAMMER)
    {
        const cf_programme_t &p = cf_programmes[app];
        if (app == CF_BOUILLOIRE)
            snprintf(message, sizeof message, "{\"action\":\"programmer\",\"appareil\":\"%s\",\"h\":%d,\"on\":%d,\"temp\":%d}", nom,
                     p.minute, (int)p.actif, temp);
        else
            snprintf(message, sizeof message, "{\"action\":\"programmer\",\"appareil\":\"%s\",\"h\":%d,\"on\":%d}", nom, p.minute,
                     (int)p.actif);
        g_cf_garde = lv_tick_get();
        g_cf_attente = false;
    }
    else if (a == CF_DIRECT && app == CF_BOUILLOIRE)
        snprintf(message, sizeof message, "{\"action\":\"maintenant\",\"appareil\":\"%s\",\"temp\":%d}", nom, temp);
    else
        snprintf(message, sizeof message, "{\"action\":\"%s\",\"appareil\":\"%s\"}", a == CF_DIRECT ? "maintenant" : "arreter", nom);
    publier(sujet, message);
}

static void commande_poele(const char *quoi)
{
    const char *sujet = UI_MQTT_PREFIXE "poele/set";
    const pl_reglages_t &r = pl_etat.r;
    char message[80];
    if (strcmp(quoi, "brassage") == 0)
        snprintf(message, sizeof message, "{\"action\":\"brassage\",\"niveau\":%d}", pl_etat.brassage);
    else if (strcmp(quoi, "mode") == 0)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"mode\":\"%s\"}", PL_MODES[LV_CLAMP(0, pl_etat.mode, 2)]);
    else if (strcmp(quoi, "tirage") == 0)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"tirage_manuel\":%d}", pl_etat.tirage_manuel);
    else if (strcmp(quoi, "kp") == 0 || strcmp(quoi, "ki") == 0 || strcmp(quoi, "kd") == 0)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"%s\":%g}", quoi,
                 quoi[1] == 'p' ? r.kp : (quoi[1] == 'i' ? r.ki : r.kd));
    else
    {
        int v = strcmp(quoi, "consigne") == 0     ? r.consigne
                : strcmp(quoi, "tirage_min") == 0 ? r.tirage_min
                : strcmp(quoi, "recharge") == 0   ? r.recharge
                : strcmp(quoi, "limite") == 0     ? r.limite
                                                  : -1;
        if (v < 0)
            return;
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"%s\":%d}", quoi, v);
    }
    g_pl_garde = lv_tick_get();
    g_pl_attente = false;
    publier(sujet, message);
}

static void commande_lampe(int i)
{
    const lu_lampe_t &l = lu_lampes[i];
    char sujet[64], message[96];
    snprintf(sujet, sizeof sujet, UI_MQTT_PREFIXE "lumieres/%s/set", LAMPES[i]);
    if (!l.on)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"on\":0}");
    else if (l.couleur && l.mode_hs)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"on\":1,\"lum\":%d,\"teinte\":%d,\"sat\":%d}", l.lum, l.teinte, l.sat);
    else
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"on\":1,\"lum\":%d,\"kelvin\":%d}", l.lum, l.kelvin);
    g_lu_garde[i] = lv_tick_get();
    g_lu_attente[i] = false;
    publier(sujet, message);
}

// ce que l'utilisateur règle (le reste de la lampe : nom, pièce, couleur possible, hors ligne, vient d'ailleurs)
static void appliquer_lampe(lu_lampe_t &l, const lu_lampe_t &r)
{
    l.on = r.on;
    l.lum = r.lum;
    l.mode_hs = r.mode_hs;
    l.kelvin = r.kelvin;
    l.teinte = r.teinte;
    l.sat = r.sat;
}

// Panneau LED : quoi = "seance", "longueurs", "mode", "duree" ou une clé d'intensité (PNL_CLES_INT)
static void commande_panneau_led(const char *quoi)
{
    const char *sujet = UI_MQTT_PREFIXE "panneau_led/set";
    char message[64];
    if (strcmp(quoi, "seance") == 0)
        snprintf(message, sizeof message, "{\"action\":\"seance\",\"on\":%d}", (int)pnl_etat.seance);
    else if (strcmp(quoi, "longueurs") == 0)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"longueurs\":\"%s\"}",
                 PNL_LONGUEURS[LV_CLAMP(0, pnl_etat.longueurs, 2)]);
    else if (strcmp(quoi, "mode") == 0)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"mode\":\"%s\"}", PNL_MODES_PANNEAU[LV_CLAMP(0, pnl_etat.mode, 1)]);
    else if (strcmp(quoi, "duree") == 0)
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"duree\":%d}", LV_CLAMP(PNL_DUREE_MIN, pnl_etat.duree, PNL_DUREE_MAX));
    else
    {
        int k = 0;
        while (k < PNL_NB_INT && strcmp(quoi, PNL_CLES_INT[k]) != 0)
            k++;
        if (k >= PNL_NB_INT)
            return;
        snprintf(message, sizeof message, "{\"action\":\"regler\",\"%s\":%d}", quoi, LV_CLAMP(0, pnl_etat.intensites[k], 100));
    }
    g_pnl_garde = lv_tick_get();
    g_pnl_attente = false;
    publier(sujet, message);
}

static void commande_imprimante(pr_action_t a, int arg)
{
    static const char *const INTERS[] = {"lumiere", "filtration", "chauffage", "carte_mere"}; // ordre de l'arg de PR_INTER
    const bool *etats[] = {&pr_etat.lumiere, &pr_etat.filtration, &pr_etat.chauffage, &pr_etat.carte_mere};
    const char *sujet = UI_MQTT_PREFIXE "imprimante/set";
    char message[160];
    switch (a)
    {
    case PR_LANCER:
        if (arg < 0 || arg >= pr_etat.nb_fichiers)
            return;
        snprintf(message, sizeof message, "{\"action\":\"lancer\",\"fichier\":\"%s\"}", pr_etat.fichiers[arg].fichier);
        break;
    case PR_CONSIGNES:
        snprintf(message, sizeof message, "{\"action\":\"consignes\",\"buse\":%d,\"plateau\":%d,\"caisson\":%d}", pr_etat.buse_c,
                 pr_etat.plateau_c, pr_etat.caisson_c);
        break;
    case PR_INTER:
        if (arg < 0 || arg > 3)
            return;
        snprintf(message, sizeof message, "{\"action\":\"inter\",\"nom\":\"%s\",\"on\":%d}", INTERS[arg], (int)*etats[arg]);
        break;
    default:
    {
        static const char *const ACTIONS[] = {"allumer", "eteindre", "pause_reprise", "arreter"}; // PR_ALLUMER..PR_ARRETER
        snprintf(message, sizeof message, "{\"action\":\"%s\"}", a == PR_ORIGINE ? "origine" : ACTIONS[a]);
    }
    }
    if (a == PR_CONSIGNES || a == PR_INTER)
    {
        g_pr_garde = lv_tick_get();
        g_pr_attente = false;
    }
    publier(sujet, message);
}

static void appliquer_imprimante(const pr_commandable_t &c)
{
    pr_etat.buse_c = c.buse_c;
    pr_etat.plateau_c = c.plateau_c;
    pr_etat.caisson_c = c.caisson_c;
    pr_etat.lumiere = c.lumiere;
    pr_etat.filtration = c.filtration;
    pr_etat.chauffage = c.chauffage;
    pr_etat.carte_mere = c.carte_mere;
}

static void appliquer_poele(const pl_commandable_t &c)
{
    pl_etat.mode = c.mode;
    pl_etat.tirage_manuel = c.tirage_manuel;
    pl_etat.brassage = c.brassage;
    pl_etat.r = c.r;
}

static void appliquer_panneau_led(const pnl_commandable_t &c)
{
    pnl_etat.seance = c.seance;
    pnl_etat.longueurs = c.longueurs;
    pnl_etat.mode = c.mode;
    pnl_etat.duree = c.duree;
    memcpy(pnl_etat.intensites, c.intensites, sizeof c.intensites);
}

static void appliquer_cafe(const cf_recu_t &r)
{
    memcpy(cf_programmes, r.programmes, sizeof r.programmes);
    memcpy(cf_recentes, r.recentes, sizeof r.recentes);
    cf_temp = r.temp;
}

static bool en_garde(uint32_t debut) { return debut && lv_tick_elaps(debut) < GARDE_MS; }

// ——— Anti-écho (mesuré lot 6 : le même geste « attente && garde expirée -> appliquer, reposer, rafraîchir »
// était écrit onze fois). Un domaine = une ligne de la table ; « garde » et « attente » pointent les états
// existants (une ligne par entrée pour les domaines à tableaux : thermostat, vmc, air, ouvrants, lumières).
// L'ordre des lignes est l'ordre exact des anciens blocs de garde_cb — rien ne change au tick près.
struct echo_t
{
    const char *nom;
    uint32_t *garde;
    bool *attente;
    int nb;
    void (*appliquer)(int i); // applique le dernier état reçu de la ligne i
    void (*maj)(void);        // rafraîchit la page, une fois si au moins une ligne est appliquée
};

static void appliquer_th(int i) { th_pieces[i].mode = g_mode_recu[i]; th_pieces[i].consigne = g_consigne_recue[i]; }
static void appliquer_bc(int)
{
    bc_etat.mode = g_bc_connu.mode = g_bc_recu.mode;
    bc_etat.eco = g_bc_connu.eco = g_bc_recu.eco;
    bc_etat.consigne = g_bc_connu.consigne = g_bc_recu.consigne;
}
static void appliquer_vmc(int z) { vmc_zones[z].vitesse = g_vmc_recue[z]; }
static void appliquer_aq(int i) { aq_purificateurs[i].mode = g_aq_recu[i]; }
static void appliquer_ov(int i)
{
    if (i < OV_NB)
        ov_ouvrants[i].pos = g_ov_pos_recue[i];
    else
    {
        ov_toile = g_ov_pos_recue[i]; // la toile porte l'état firetv avec elle
        ov_firetv = g_ov_tv_recue;
    }
}
static void appliquer_lu(int i) { appliquer_lampe(lu_lampes[i], g_lu_recue[i]); }
static void appliquer_pr(int) { appliquer_imprimante(g_pr_recu); }
static void appliquer_pl(int) { appliquer_poele(g_pl_recu); }
static void appliquer_pnl(int) { appliquer_panneau_led(g_pnl_recu); }
static void appliquer_cf(int) { appliquer_cafe(g_cf_recu); }
static void appliquer_cq(int) { memcpy(cq_etat.repas, g_cq_repas_recus, sizeof cq_etat.repas); }

static const echo_t ECHOS[] = {
    { "thermostat", g_garde, g_en_attente, TH_NB_PIECES, appliquer_th, page_thermostat_maj },
    { "ballon", &g_bc_garde, &g_bc_attente, 1, appliquer_bc, page_ballon_maj },
    { "vmc", g_vmc_garde, g_vmc_attente, VMC_NB, appliquer_vmc, page_vmc_maj },
    { "air", g_aq_garde, g_aq_attente, AQ_NB, appliquer_aq, page_air_maj },
    { "ouvrants", g_ov_garde, g_ov_attente, OV_NB + 1, appliquer_ov, page_ouvrants_maj },
    { "lumieres", g_lu_garde, g_lu_attente, LU_NB, appliquer_lu, page_lumieres_maj },
    { "imprimante", &g_pr_garde, &g_pr_attente, 1, appliquer_pr, page_imprimante_maj },
    { "poele", &g_pl_garde, &g_pl_attente, 1, appliquer_pl, page_poele_maj },
    { "panneau_led", &g_pnl_garde, &g_pnl_attente, 1, appliquer_pnl, page_panneau_led_maj },
    { "cafe", &g_cf_garde, &g_cf_attente, 1, appliquer_cf, page_cafe_maj },
    { "croquettes", &g_cq_garde, &g_cq_attente, 1, appliquer_cq, page_croquettes_maj },
};

static void garde_cb(lv_timer_t *)
{
    for (const echo_t &E : ECHOS)
    {
        bool change = false;
        for (int i = 0; i < E.nb; i++)
            if (E.attente[i] && !en_garde(E.garde[i]))
            {
                E.appliquer(i);
                E.attente[i] = false;
                change = true;
            }
        if (change)
            E.maj();
    }
}


// PC : réveil / veille par HA (switch.eveille_ordi_cachy) ; l'état, lui, vient de l'agent du PC
static void commande_pc(pc_action_t a)
{
    publier(UI_MQTT_PREFIXE "pc/set", a == PC_REVEILLER ? "{\"action\":\"reveiller\"}" : "{\"action\":\"veille\"}");
}

// « PC en veille » : plus rien de l'agent depuis 10 s (il publie chaque seconde, non retenu)
#define PC_SILENCE 10000
static uint32_t g_pc_t = 0; // dernier message de l'agent
static void pc_silence_cb(lv_timer_t *)
{
    if (!pc_etat.en_veille && lv_tick_elaps(g_pc_t) > PC_SILENCE)
    {
        pc_etat.en_veille = true;
        page_pc_maj();
    }
}

void ui_mqtt_brancher()
{
    page_graphe_preparer(); // les historiques arrivent dès la connexion, page affichée ou non
    th_commande = commande_thermostat;
    bc_commande = commande_ballon;
    vmc_commande = commande_vmc;
    ov_commande = commande_ouvrant;
    cq_commande = commande_croquettes;
    cf_commande = commande_cafe;
    pl_commande = commande_poele;
    lu_commande = commande_lampe;
    pnl_commande = commande_panneau_led;
    pr_commande = commande_imprimante;
    for (int k = 0; k < PL_HIST; k++) // avant le premier historique de HA : courbes vides, pas une ligne à 0
    {
        pl_etat.h_fumees[k] = pl_etat.h_corps[k] = PL_VIDE;
        pl_etat.h_tirage[k] = PL_VIDE_TIRAGE;
    }
    aq_commande = commande_air;
    pc_commande = commande_pc;
    g_pc_t = lv_tick_get(); // au démarrage : 10 s pour entendre l'agent, sinon « en veille »
    lv_timer_create(pc_silence_cb, 1000, nullptr);
    for (int i = 0; i < AQ_NB; i++)
        memset(aq_purificateurs[i].hist, AQ_VIDE, AQ_HIST); // avant le premier historique de HA : courbe vide, pas une ligne à 0
    g_bc_connu = bc_etat;
    lv_timer_create(garde_cb, 500, nullptr);
}

// --- HA -> écran ---
// Une clé absente ou d'un autre type laisse la valeur en place
template <typename T>
static void lire(JsonVariantConst v, T &dst)
{
    if (v.is<T>())
        dst = v.as<T>();
}
static void lire(JsonVariantConst v, bool &dst)
{
    if (v.is<int>())
        dst = v.as<int>() != 0;
}
// nombre JSON (entier ou décimal) arrondi vers un entier de la page (ex. corps 24.1 -> 24)
static void lire_arrondi(JsonVariantConst v, int &dst)
{
    if (v.is<float>())
        dst = (int)lroundf(v.as<float>());
}

ui_ciel_t ui_ciel = {};

static void recevoir_ciel(JsonDocument &j)
{
    lire(j["aube"], ui_ciel.aube);
    lire(j["lever"], ui_ciel.lever);
    lire(j["coucher"], ui_ciel.coucher);
    lire(j["crepuscule"], ui_ciel.crepuscule);
    lire(j["lune_age"], ui_ciel.lune_age);
    lire(j["lune_pct"], ui_ciel.lune_pct);
    if (j["meteo"].is<const char *>())
        snprintf(ui_ciel.meteo, sizeof ui_ciel.meteo, "%s", j["meteo"].as<const char *>());
    lire(j["nuages"], ui_ciel.nuages);
    lire(j["pluie"], ui_ciel.pluie);
    lire(j["neige"], ui_ciel.neige);
    ui_ciel.recu = ui_ciel.lever && ui_ciel.coucher;
    page_ia_maj();
}

static void recevoir_maison(JsonDocument &j)
{
    lire(j["ext_temp"], th_ext_temp);
    lire(j["ext_hum"], th_ext_hum);
    if (j["phase"].is<const char *>())
        ui_maison.nuit = strcmp(j["phase"], "nuit") == 0;
    lire(j["presence"], ui_maison.presence);
    lire(j["maj"], ui_maison.maj);
    ui_appareil_heure(ui_maison.maj); // l'heure de l'écran vient de HA (comme la production)
    page_thermostat_maj();
}

static void recevoir_thermostat(int i, JsonDocument &j)
{
    th_piece_t &p = th_pieces[i];
    lire(j["temp"], p.temp);
    lire(j["hum"], p.hum);
    lire(j["chauffe"], p.chauffe);
    lire(j["batterie"], p.batterie);
    lire(j["signal"], p.signal);

    th_mode_t mode = p.mode;
    if (j["mode"].is<const char *>())
        for (int m = TH_ARRET; m <= TH_MANUEL; m++)
            if (strcmp(j["mode"], MODES[m]) == 0)
                mode = (th_mode_t)m;
    float consigne = p.consigne;
    lire(j["consigne"], consigne);
    if (en_garde(g_garde[i]))
    {
        g_mode_recu[i] = mode;
        g_consigne_recue[i] = consigne;
        g_en_attente[i] = true;
    }
    else
    {
        p.mode = mode;
        p.consigne = consigne;
    }
    page_thermostat_maj();
}

static void recevoir_ballon(JsonDocument &j)
{
    lire(j["temp"], bc_etat.temp);
    lire(j["chauffe"], bc_etat.chauffe);
    lire_arrondi(j["chaude_jour"], bc_etat.chaude_jour); // 25/10 : la journée et le direct
    lire_arrondi(j["froide_jour"], bc_etat.froide_jour);
    lire(j["kwh_jour"], bc_etat.kwh_jour);
    lire_arrondi(j["debit"], bc_etat.debit);
    lire_arrondi(j["puissance"], bc_etat.puissance);
    lire_arrondi(j["combles"], bc_etat.combles);
    bc_etat_t r = g_bc_attente ? g_bc_recu : g_bc_connu;
    if (j["mode"].is<const char *>())
        for (int m = BC_ARRET; m <= BC_FORCE; m++)
            if (strcmp(j["mode"], BC_MODES[m]) == 0)
                r.mode = (bc_mode_t)m;
    lire(j["eco"], r.eco);
    lire(j["consigne"], r.consigne);
    if (en_garde(g_bc_garde))
    {
        g_bc_recu = r;
        g_bc_attente = true;
    }
    else
    {
        bc_etat.mode = g_bc_connu.mode = r.mode;
        bc_etat.eco = g_bc_connu.eco = r.eco;
        bc_etat.consigne = g_bc_connu.consigne = r.consigne;
    }
    page_ballon_maj();
}

// 25/10 : historique 24 h du ballon (288 points, le plus récent en dernier), calé à droite tant que HA en a moins ;
// consigne et litres = [rang, valeur] (rang dans la liste de HA), chauffe / heures creuses = 1 bit par point en hexa
static void recevoir_ballon_historique(JsonDocument &j)
{
    JsonArrayConst t = j["t"];
    const int dec = BC_POINTS - (int)t.size();
    for (int i = 0; i < BC_POINTS; i++)
    {
        const int v = i >= dec ? (t[i - dec] | -999) : -999;
        bc_hist.temp[i] = v == -999 ? BC_VIDE : (int16_t)v;
        bc_hist.consigne[i] = BC_VIDE;
        bc_hist.litres[i] = 0;
    }
    for (JsonArrayConst r : j["c"].as<JsonArrayConst>())
        for (int i = LV_MAX(0, (r[0] | 0) + dec); i < BC_POINTS; i++)
            bc_hist.consigne[i] = (int16_t)((r[1] | 0) * 10);
    for (JsonArrayConst r : j["l"].as<JsonArrayConst>())
    {
        const int i = (r[0] | 0) + dec;
        if (i >= 0 && i < BC_POINTS)
            bc_hist.litres[i] = (uint8_t)LV_CLAMP(0, r[1] | 0, 255);
    }
    const char *ch = j["ch"] | "", *hc = j["hc"] | ""; // HA cale déjà sur 288
    const size_t nch = strlen(ch), nhc = strlen(hc);
    for (int k = 0; k < BC_POINTS / 8; k++)
    {
        unsigned a = 0, b = 0;
        if (nch >= (size_t)(2 * k + 2))
            sscanf(ch + 2 * k, "%2x", &a);
        if (nhc >= (size_t)(2 * k + 2))
            sscanf(hc + 2 * k, "%2x", &b);
        bc_hist.chauffe[k] = (uint8_t)a;
        bc_hist.creuses[k] = (uint8_t)b;
    }
    bc_hist.fin = j["fin"] | bc_hist.fin;
    page_ballon_historique_maj();
}

static void recevoir_vmc(int z, JsonDocument &j)
{
    vmc_zone_t &v = vmc_zones[z];
    lire(j["auto"], v.auto_en_cours);
    lire(j["hum"], v.hum);
    lire(j["seuil"], v.seuil);
    if (j["hum_lieu"].is<const char *>())
    {
        snprintf(g_vmc_lieu[z], sizeof g_vmc_lieu[z], "%s", j["hum_lieu"].as<const char *>());
        v.lieu = g_vmc_lieu[z];
    }
    int vitesse = v.vitesse;
    lire(j["vitesse"], vitesse);
    if (vitesse < 0 || vitesse > 2)
        vitesse = v.vitesse;
    if (en_garde(g_vmc_garde[z]))
    {
        g_vmc_recue[z] = vitesse;
        g_vmc_attente[z] = true;
    }
    else
        v.vitesse = vitesse;
    page_vmc_maj();
}

static void recevoir_air(int i, JsonDocument &j)
{
    aq_purificateur_t &p = aq_purificateurs[i];
    bool en_ligne = true;
    lire(j["en_ligne"], en_ligne);
    p.en_ligne = en_ligne;
    if (!en_ligne)
        snprintf(p.info, sizeof p.info, TXT("Hors ligne", "Offline"));
    else
    {
        if (j["info"].is<const char *>())
            snprintf(p.info, sizeof p.info, "%s", j["info"].as<const char *>());
        lire(j["pm25"], p.pm25);
        if (j["rpm"].is<int>() || j["rpm"].is<float>())
        {
            p.rpm = j["rpm"].as<int>();
            p.rpm_connu = true;
        }
    }
    // Libellés des modes : seulement si leur nombre est celui des boutons déjà construits par la page
    JsonArrayConst modes = j["modes"];
    if ((int)modes.size() == p.nb_modes)
        for (int m = 0; m < p.nb_modes; m++)
            if (modes[m].is<const char *>())
            {
                snprintf(g_aq_modes[i][m], sizeof g_aq_modes[i][m], "%s", modes[m].as<const char *>());
                p.modes[m] = g_aq_modes[i][m];
            }
    int mode = p.mode;
    lire(j["mode"], mode);
    if (mode < -1 || mode >= p.nb_modes)
        mode = p.mode;
    if (en_garde(g_aq_garde[i]))
    {
        g_aq_recu[i] = mode;
        g_aq_attente[i] = true;
    }
    else
        p.mode = mode;
    page_air_maj();
}

// Courbe des 24 h tenue par HA (moyenne de chaque demi-heure, AQ_VIDE = pas de mesure), le plus récent en dernier
static void recevoir_air_historique(int i, JsonDocument &j)
{
    JsonArrayConst pts = j["points"];
    if (pts.isNull())
        return;
    aq_purificateur_t &p = aq_purificateurs[i];
    int n = LV_MIN((int)pts.size(), AQ_HIST), debut = (int)pts.size() - n;
    memset(p.hist, AQ_VIDE, AQ_HIST - n); // moins de 24 h connues : courbe calée à droite
    for (int k = 0; k < n; k++)
    {
        int v = pts[debut + k] | (int)AQ_VIDE;
        p.hist[AQ_HIST - n + k] = (uint8_t)LV_CLAMP(0, v, (int)AQ_VIDE);
    }
    page_air_maj();
}

static void recevoir_ouvrant(int i, JsonDocument &j)
{
    int &pos = i < OV_NB ? ov_ouvrants[i].pos : ov_toile;
    int p = pos;
    bool tv = ov_firetv;
    lire(j["position"], p);
    p = LV_CLAMP(0, p, 100);
    if (i == OV_NB)
    {
        lire(j["firetv"], tv);
        if (j["mouvement"].is<const char *>()) // pas gardé : c'est HA qui sait si la toile bouge
            ov_toile_mouvement = strcmp(j["mouvement"], "ouvre") == 0 ? 1 : (strcmp(j["mouvement"], "ferme") == 0 ? -1 : 0);
    }
    if (en_garde(g_ov_garde[i]))
    {
        g_ov_pos_recue[i] = p;
        if (i == OV_NB)
            g_ov_tv_recue = tv;
        g_ov_attente[i] = true;
    }
    else
    {
        pos = p;
        if (i == OV_NB)
            ov_firetv = tv;
    }
    page_ouvrants_maj();
}

static void recevoir_croquettes(JsonDocument &j)
{
    lire(j["bol"], cq_etat.bol);
    lire(j["stock"], cq_etat.stock);
    lire(j["autonomie"], cq_etat.autonomie);
    lire(j["jour_semaine"], cq_etat.jour_semaine);
    JsonArrayConst jours = j["jours"];
    for (int k = 0; k < 7 && k < (int)jours.size(); k++)
    {
        lire(jours[k]["g"], cq_etat.jours[k].g);
        lire(jours[k]["n"], cq_etat.jours[k].repas);
    }
    cq_repas_t repas[4];
    memcpy(repas, g_cq_attente ? g_cq_repas_recus : cq_etat.repas, sizeof repas);
    JsonArrayConst rs = j["repas"];
    for (int k = 0; k < 4 && k < (int)rs.size(); k++)
    {
        lire(rs[k]["h"], repas[k].minute);
        lire(rs[k]["q"], repas[k].g);
        lire(rs[k]["a"], repas[k].actif);
    }
    if (en_garde(g_cq_garde))
    {
        memcpy(g_cq_repas_recus, repas, sizeof repas);
        g_cq_attente = true;
    }
    else
        memcpy(cq_etat.repas, repas, sizeof repas);
    page_croquettes_maj();
}

static void recevoir_cafe(JsonDocument &j)
{
    lire(j["bouilloire"]["chauffe"], cf_bouilloire_chauffe);
    cf_recu_t r;
    if (g_cf_attente)
        r = g_cf_recu;
    else
    {
        memcpy(r.programmes, cf_programmes, sizeof r.programmes);
        memcpy(r.recentes, cf_recentes, sizeof r.recentes);
        r.temp = cf_temp;
    }
    const char *const APP[2] = {"cafe", "bouilloire"};
    for (int a = 0; a < 2; a++)
    {
        JsonVariantConst o = j[APP[a]];
        lire(o["prog"], r.programmes[a].actif);
        lire(o["h"], r.programmes[a].minute);
    }
    int temp = -1;
    lire(j["bouilloire"]["temp"], temp);
    for (int k = 0; k < 5; k++)
        if (CF_TEMPERATURES[k] == temp)
            r.temp = k;
    JsonArrayConst rec = j["recentes"];
    if (!rec.isNull())
        for (int k = 0; k < 2; k++)
            r.recentes[k] = k < (int)rec.size() ? (rec[k] | -1) : -1;
    if (en_garde(g_cf_garde))
    {
        g_cf_recu = r;
        g_cf_attente = true;
    }
    else
        appliquer_cafe(r);
    page_cafe_maj();
}

static void recevoir_poele(JsonDocument &j)
{
    lire_arrondi(j["fumees"], pl_etat.fumees);
    lire_arrondi(j["corps"], pl_etat.corps);
    lire_arrondi(j["tirage"], pl_etat.tirage);
    if (j["feu"].is<const char *>())
    {
        static const char *const FEUX[] = {"eteint", "allumage", "regulation", "recharger"}; // ordre de pl_feu_t
        for (int k = 0; k < 4; k++)
            if (strcmp(j["feu"], FEUX[k]) == 0)
                pl_etat.feu = (pl_feu_t)k;
    }
    pl_commandable_t c = g_pl_attente ? g_pl_recu : pl_commandable_t{pl_etat.mode, pl_etat.tirage_manuel, pl_etat.brassage, pl_etat.r};
    if (j["mode"].is<const char *>())
        for (int k = 0; k < 3; k++)
            if (strcmp(j["mode"], PL_MODES[k]) == 0)
                c.mode = k;
    lire_arrondi(j["tirage_manuel"], c.tirage_manuel);
    lire_arrondi(j["brassage"], c.brassage);
    lire_arrondi(j["consigne"], c.r.consigne);
    lire_arrondi(j["tirage_min"], c.r.tirage_min);
    lire_arrondi(j["recharge"], c.r.recharge);
    lire_arrondi(j["limite"], c.r.limite);
    lire(j["kp"], c.r.kp);
    lire(j["ki"], c.r.ki);
    lire(j["kd"], c.r.kd);
    if (en_garde(g_pl_garde))
    {
        g_pl_recu = c;
        g_pl_attente = true;
    }
    else
        appliquer_poele(c);
    page_poele_maj();
}

// Courbes des 12 h tenues par HA : -1 = pas de mesure, le plus récent en dernier, calées à droite
static void recevoir_poele_historique(JsonDocument &j)
{
    JsonArrayConst f = j["fumees"], c = j["corps"], t = j["tirage"];
    for (int k = 0; k < PL_HIST; k++)
    {
        auto point = [&](JsonArrayConst a) -> int
        {
            int i = (int)a.size() - PL_HIST + k; // aligné sur le dernier point
            return (i >= 0 && a[i].is<int>()) ? a[i].as<int>() : PL_VIDE;
        };
        pl_etat.h_fumees[k] = (int16_t)point(f);
        pl_etat.h_corps[k] = (int16_t)point(c);
        int v = point(t);
        pl_etat.h_tirage[k] = v < 0 ? PL_VIDE_TIRAGE : (uint8_t)LV_MIN(v, 100);
    }
    pl_etat.h_fin = j["fin"] | -1;
    page_poele_maj();
}

// Panneau LED (02/11/2026) : l'état commandable est gardé comme celui du Poêle ; le reste (LED réellement
// allumées, minutes restantes, radiateur, ventilateurs) est seulement montré, appliqué aussitôt
static void recevoir_panneau_led(JsonDocument &j)
{
    pnl_commandable_t c;
    if (g_pnl_attente)
        c = g_pnl_recu;
    else
    {
        c.seance = pnl_etat.seance;
        c.longueurs = pnl_etat.longueurs;
        c.mode = pnl_etat.mode;
        c.duree = pnl_etat.duree;
        memcpy(c.intensites, pnl_etat.intensites, sizeof c.intensites);
    }
    lire(j["seance"], c.seance);
    if (j["longueurs"].is<const char *>())
        for (int k = 0; k < 3; k++)
            if (strcmp(j["longueurs"], PNL_LONGUEURS[k]) == 0)
                c.longueurs = k;
    if (j["mode"].is<const char *>())
        for (int k = 0; k < 2; k++)
            if (strcmp(j["mode"], PNL_MODES_PANNEAU[k]) == 0)
                c.mode = k;
    lire_arrondi(j["duree"], c.duree);
    c.duree = LV_CLAMP(PNL_DUREE_MIN, c.duree, PNL_DUREE_MAX);
    for (int k = 0; k < PNL_NB_INT; k++)
    {
        lire_arrondi(j[PNL_CLES_INT[k]], c.intensites[k]);
        c.intensites[k] = LV_CLAMP(0, c.intensites[k], 100);
    }
    if (en_garde(g_pnl_garde))
    {
        g_pnl_recu = c;
        g_pnl_attente = true;
    }
    else
        appliquer_panneau_led(c);
    lire(j["allumees"], pnl_etat.allumees);
    if (j["restant"].is<int>())
        pnl_etat.restant = LV_MAX(PNL_RESTANT_INCONNU, j["restant"].as<int>()); // -1 = inconnu (mode Illimité)
    lire_arrondi(j["radiateur"], pnl_etat.radiateur);
    lire_arrondi(j["ventilos"], pnl_etat.ventilos);
    page_panneau_led_maj();
}

static void recevoir_lampe(int i, JsonDocument &j)
{
    lu_lampe_t &l = lu_lampes[i];
    bool en_ligne = true;
    lire(j["en_ligne"], en_ligne);
    l.hors_ligne = !en_ligne;
    lire(j["couleur"], l.couleur);
    if (j["nom"].is<const char *>())
    {
        snprintf(g_lu_noms[i], sizeof g_lu_noms[i], "%s", j["nom"].as<const char *>());
        l.nom = g_lu_noms[i];
    }
    if (j["piece"].is<const char *>())
    {
        snprintf(g_lu_pieces[i], sizeof g_lu_pieces[i], "%s", j["piece"].as<const char *>());
        l.piece = g_lu_pieces[i];
    }
    lu_lampe_t r = g_lu_attente[i] ? g_lu_recue[i] : l;
    lire(j["on"], r.on);
    lire_arrondi(j["lum"], r.lum); // absents quand la lampe est éteinte : l'écran garde le dernier réglage
    lire_arrondi(j["teinte"], r.teinte);
    lire_arrondi(j["sat"], r.sat);
    lire_arrondi(j["kelvin"], r.kelvin);
    if (j["mode"].is<const char *>())
        r.mode_hs = strcmp(j["mode"], "couleur") == 0;
    if (en_garde(g_lu_garde[i]))
    {
        g_lu_recue[i] = r;
        g_lu_attente[i] = true;
    }
    else
        appliquer_lampe(l, r);
    page_lumieres_maj();
}

// le fichier imprimé dans la liste (sa miniature remplit l'anneau) ; -1 s'il n'y est pas
static void trouver_travail()
{
    pr_etat.travail = -1;
    if (!*g_pr_en_cours)
        return;
    size_t n = strlen(g_pr_en_cours);
    for (int k = 0; k < pr_etat.nb_fichiers; k++)
        if (strncmp(pr_etat.fichiers[k].fichier, g_pr_en_cours, n) == 0 &&
            (pr_etat.fichiers[k].fichier[n] == 0 || strcmp(pr_etat.fichiers[k].fichier + n, ".gcode") == 0))
            pr_etat.travail = k;
}

static void recevoir_imprimante(JsonDocument &j)
{
    if (j["etat"].is<const char *>())
    {
        static const char *const ETATS[] = {"eteinte", "prete", "impression", "pause"}; // ordre de pr_etat_t
        for (int k = 0; k < 4; k++)
            if (strcmp(j["etat"], ETATS[k]) == 0)
                pr_etat.etat = (pr_etat_t)k;
    }
    lire_arrondi(j["progression"], pr_etat.progression);
    lire_arrondi(j["couche"], pr_etat.couche);
    lire_arrondi(j["couches"], pr_etat.couches);
    lire_arrondi(j["reste_min"], pr_etat.reste_min);
    lire_arrondi(j["fin"], pr_etat.fin_minute);
    lire_arrondi(j["buse"], pr_etat.buse);
    lire_arrondi(j["plateau"], pr_etat.plateau);
    lire_arrondi(j["caisson"], pr_etat.caisson);
    lire_arrondi(j["puissance"], pr_etat.puissance); // 27/10 : wattmètre de la prise
    lire(j["pm1"], pr_etat.pm1);                     // 27/10 : AQM (sensor.aqm_pm1)
    pr_commandable_t c = g_pr_attente ? g_pr_recu
                                      : pr_commandable_t{pr_etat.buse_c, pr_etat.plateau_c, pr_etat.caisson_c, pr_etat.lumiere,
                                                         pr_etat.filtration, pr_etat.chauffage, pr_etat.carte_mere};
    lire_arrondi(j["buse_c"], c.buse_c);
    lire_arrondi(j["plateau_c"], c.plateau_c);
    lire_arrondi(j["caisson_c"], c.caisson_c);
    lire(j["lumiere"], c.lumiere);
    lire(j["filtration"], c.filtration);
    lire(j["chauffage"], c.chauffage);
    lire(j["carte_mere"], c.carte_mere);
    if (en_garde(g_pr_garde))
    {
        g_pr_recu = c;
        g_pr_attente = true;
    }
    else
        appliquer_imprimante(c);
    if (j["fichier"].is<const char *>())
        snprintf(g_pr_en_cours, sizeof g_pr_en_cours, "%s", j["fichier"].as<const char *>());
    trouver_travail();
    page_imprimante_maj();
}

// Liste des fichiers (un sous-sujet par fichier, rang 0 = le plus récent) et son résumé {nb, base}
static void recevoir_fichier(int k, JsonDocument &j)
{
    if (k < 0 || k >= PR_FICHIERS_MAX)
        return;
    pr_fichier_t &f = pr_etat.fichiers[k];
    if (j["nom"].is<const char *>())
        snprintf(f.nom, sizeof f.nom, "%s", j["nom"].as<const char *>());
    if (j["fichier"].is<const char *>())
        snprintf(f.fichier, sizeof f.fichier, "%s", j["fichier"].as<const char *>());
    if (j["matiere"].is<const char *>())
        snprintf(f.matiere, sizeof f.matiere, "%s", j["matiere"].as<const char *>());
    if (j["date"].is<const char *>())
        snprintf(f.date, sizeof f.date, "%s", j["date"].as<const char *>());
    lire_arrondi(j["minutes"], f.minutes);
    ui_vignettes_demander(k, g_pr_base, f.fichier);
    trouver_travail();
    page_imprimante_maj();
}

static void recevoir_fichiers(JsonDocument &j)
{
    int nb = pr_etat.nb_fichiers;
    lire_arrondi(j["nb"], nb);
    pr_etat.nb_fichiers = LV_CLAMP(0, nb, PR_FICHIERS_MAX);
    if (pr_etat.choix >= pr_etat.nb_fichiers)
        pr_etat.choix = -1;
    if (j["base"].is<const char *>())
        snprintf(g_pr_base, sizeof g_pr_base, "%s", j["base"].as<const char *>());
    for (int k = 0; k < pr_etat.nb_fichiers; k++) // la base a pu arriver après les fichiers
        ui_vignettes_demander(k, g_pr_base, pr_etat.fichiers[k].fichier);
    trouver_travail();
    page_imprimante_maj();
}

// Musique : seulement la chaîne piCore et l'ampli (le reste parle directement à Music Assistant : ui_musique)
static void recevoir_chaine(JsonDocument &j)
{
    static const char *const ETATS[] = {"eteint", "arret", "demarrage", "pret"}; // ordre de mu_chaine_t
    for (int k = 0; k < 4; k++)
        if (strcmp(j["picore"] | "", ETATS[k]) == 0)
            mu_etat.chaine = k;
    if (j["ampli"].is<int>())
        mu_etat.ampli = j["ampli"].as<int>() != 0;
    page_musique_maj();
}

// PC (agent btop_agent.py, 1 fois par seconde) ; débits réseau / disque en historique, à l'échelle du plus grand
static void recevoir_pc(JsonDocument &j)
{
    pc_etat_t &p = pc_etat;
    lire_arrondi(j["cpu"], p.cpu);
    lire_arrondi(j["freq"], p.freq_mhz);
    lire_arrondi(j["cpu_temp"], p.cpu_temp);
    lire_arrondi(j["cpu_w"], p.cpu_watts);
    int k = 0;
    for (JsonVariantConst c : j["coeurs"].as<JsonArrayConst>())
        if (k < PC_COEURS)
            lire_arrondi(c, p.coeurs[k++]);
    lire(j["ram"], p.ram);
    lire(j["cache"], p.cache);
    lire(j["ram_total"], p.ram_total);
    lire_arrondi(j["swap"], p.swap);
    k = 0;
    for (JsonVariantConst g : j["gpus"].as<JsonArrayConst>())
    {
        if (k >= PC_GPUS)
            break;
        pc_gpu_t &x = p.gpus[k++];
        snprintf(x.nom, sizeof x.nom, "%s", g["nom"] | "");
        lire_arrondi(g["charge"], x.charge);
        lire_arrondi(g["temp"], x.temp);
        lire_arrondi(g["w"], x.puissance);
        lire(g["vram"], x.vram);
        lire(g["vram_total"], x.vram_total);
        // 08/11 (maquette GPU) : « ps » (P0..P12), « f » et « fm » (MHz courants), « f_max » et « fm_max »
        // (maxima de la carte, null si inconnu). pc_etat garde ses valeurs d'une trame à l'autre : on remet donc à 0
        // avant de lire, pour qu'un champ absent ou null revienne à « - » à l'écran au lieu de rester affiché.
        x.ps[0] = 0;
        snprintf(x.ps, sizeof x.ps, "%s", g["ps"] | "");
        x.f = x.fm = x.f_max = x.fm_max = 0;
        lire_arrondi(g["f"], x.f);
        lire_arrondi(g["fm"], x.fm);
        lire_arrondi(g["f_max"], x.f_max);
        lire_arrondi(g["fm_max"], x.fm_max);
        int b = 0;
        for (JsonVariantConst v : g["blocs"].as<JsonArrayConst>())
        {
            if (b >= PC_BLOCS)
                break;
            pc_bloc_t &y = x.blocs[b++];
            snprintf(y.nom, sizeof y.nom, "%s", v["n"] | "");
            y.mo = v["mo"] | 0;
            y.couleur = strtoul(v["c"] | "6b7280", nullptr, 16);
        }
        x.nb_blocs = b;
    }
    lire(j["net_bas"], p.net_bas);
    lire(j["net_haut"], p.net_haut);
    lire(j["disque_l"], p.disque_l);
    lire(j["disque_e"], p.disque_e);
    if (j["depuis"].is<const char *>())
        snprintf(p.allume_depuis, sizeof p.allume_depuis, "%s", j["depuis"].as<const char *>());
    if (j["heure"].is<const char *>())
        snprintf(p.derniere_nouvelle, sizeof p.derniere_nouvelle, "%s", j["heure"].as<const char *>());

    static float h_net[PC_HIST], h_dsk[PC_HIST]; // Mo/s, le plus récent à droite
    memmove(h_net, h_net + 1, sizeof(float) * (PC_HIST - 1));
    memmove(h_dsk, h_dsk + 1, sizeof(float) * (PC_HIST - 1));
    h_net[PC_HIST - 1] = p.net_bas + p.net_haut;
    h_dsk[PC_HIST - 1] = p.disque_l + p.disque_e;
    float mn = 1, md = 1; // 1 Mo/s au moins : le bruit au repos reste à plat
    for (int i = 0; i < PC_HIST; i++)
        mn = LV_MAX(mn, h_net[i]), md = LV_MAX(md, h_dsk[i]);
    for (int i = 0; i < PC_HIST; i++)
    {
        p.h_net[i] = (uint8_t)(100 * h_net[i] / mn);
        p.h_disque[i] = (uint8_t)(100 * h_dsk[i] / md);
    }
    // 31/10 : graphe CPU sur 5 min : un point toutes les 2 s, moyenne des états reçus entre-temps
    static uint32_t cpu_somme, cpu_n, cpu_t;
    cpu_somme += LV_CLAMP(0, p.cpu, 100);
    cpu_n++;
    if (lv_tick_elaps(cpu_t) >= 2000)
    {
        memmove(p.h_cpu, p.h_cpu + 1, PC_HIST_CPU - 1);
        p.h_cpu[PC_HIST_CPU - 1] = (uint8_t)(cpu_somme / cpu_n);
        p.n_cpu++;
        cpu_somme = cpu_n = 0;
        cpu_t = lv_tick_get();
    }
    p.en_veille = false;
    g_pc_t = lv_tick_get();
    page_pc_maj();
}

// Liste des processus : arrive juste après l'état, affichée à la reconstruction suivante (une par seconde suffit)
static void recevoir_processus(JsonDocument &j)
{
    int n = 0;
    for (JsonVariantConst v : j.as<JsonArrayConst>())
    {
        if (n >= PC_PROCS)
            break;
        pc_proc_t &x = pc_etat.procs[n++];
        snprintf(x.nom, sizeof x.nom, "%s", v["nom"] | "");
        snprintf(x.role, sizeof x.role, "%s", v["role"] | "");
        x.cpu = v["cpu"] | 0.0f;
        x.mem = v["mem"] | 0.0f;
        x.vram = v["vram"] | 0.0f;
    }
    pc_etat.nb_procs = n;
}

// 31/10 : usage des comptes Claude (agent du PC, retenu). 11/11 : le format garde le tableau « comptes » mais
// l'agent n'en publie qu'UN, le compte en cours ; l'écran lit le premier et l'affiche sur la page « IA » du rail.
static void recevoir_ia(JsonDocument &j)
{
    for (JsonVariantConst v : j["comptes"].as<JsonArrayConst>())
    {
        ia_compte_t &x = ia_compte;
        snprintf(x.nom, sizeof x.nom, "%s", v["nom"] | "");
        x.h5 = v["h5"] | 0;
        x.semaine = v["semaine"] | 0;
        x.h5_reset = v["h5_reset"] | 0u;
        x.semaine_reset = v["semaine_reset"] | 0u;
        x.maj = v["maj"] | 0u;
        x.recu = true;
        break; // un seul compte : le premier (CONTRAT_MQTT.md, 11/11)
    }
    page_ia_maj();
}

// 11/11 : activité de l'agent Claude sur le PC (dashboard_guition/pc/ia_activite, retenu) : état, alerte,
// production (tokens, outils, prompts, sessions) et histogramme 30 tranches de 10 min, la plus ancienne d'abord.
static void recevoir_ia_activite(JsonDocument &j)
{
    ia_activite_t &a = ia_activite;
    snprintf(a.etat, sizeof a.etat, "%s", j["etat"] | "");
    snprintf(a.alerte, sizeof a.alerte, "%s", j["alerte"] | "");
    a.tokens = j["tokens"] | 0u;
    a.outils = j["outils"] | 0;
    a.prompts = j["prompts"] | 0;
    a.sessions = j["sessions"] | 0;
    int n = 0;
    for (JsonVariantConst v : j["histo"].as<JsonArrayConst>()) // s'il en manque, les plus anciennes restent à 0
    {
        if (n >= IA_HISTO)
            break;
        a.histo[n++] = v | 0u;
    }
    while (n < IA_HISTO)
        a.histo[n++] = 0;
    a.maj = j["maj"] | 0u;
    if (!a.maj) // horloge du PC pas réglée : la date de l'écran fait foi pour la péremption (2 min)
        a.maj = (uint32_t)time(nullptr);
    a.recu = true;
    page_ia_maj();
}

// Réglages de la maison (carillon, éclairage terrasse), les mêmes sur tous les écrans
static void recevoir_reglages_maison(JsonDocument &j)
{
    rg_reglages_t &r = rg_reglages;
    if (j["melodie"].is<const char *>())
        snprintf(r.melodie, sizeof r.melodie, "%s", j["melodie"].as<const char *>());
    if (j["melodies"].is<JsonArrayConst>())
    {
        r.nb_melodies = 0;
        for (JsonVariantConst m : j["melodies"].as<JsonArrayConst>())
            if (r.nb_melodies < (int)(sizeof r.melodies / sizeof r.melodies[0]))
                snprintf(r.melodies[r.nb_melodies++], sizeof r.melodies[0], "%s", m | "");
    }
    lire_arrondi(j["volume"], r.volume);
    lire(j["terrasse_porte"], r.terrasse_porte);
    lire(j["terrasse_mouvement"], r.terrasse_mouvement);
    page_reglages_maj();
}

// Appareils de la maison vus par HA : [nom, pièce, en ligne] ; la page n'est refaite que si la liste a changé
static void recevoir_appareils(JsonDocument &j)
{
    static uint32_t empreinte = 0;
    uint32_t e = 2166136261u;
    int n = 0;
    for (JsonArrayConst a : j["liste"].as<JsonArrayConst>())
    {
        if (n >= IF_NB_APPAREILS)
            break;
        if_appareil_t &x = if_appareils[n++];
        snprintf(x.nom, sizeof x.nom, "%s", a[0] | "");
        snprintf(x.piece, sizeof x.piece, "%s", a[1] | "");
        x.ok = (a[2] | 0) != 0;
        for (const char *c = x.nom; *c; c++)
            e = (e ^ (uint8_t)*c) * 16777619u;
        e = (e ^ (x.ok ? 1 : 2)) * 16777619u;
    }
    if_nb_appareils = n;
    if (e != empreinte)
    {
        empreinte = e;
        page_infos_maj();
    }
}

// Historiques 24 h tenus par HA (288 points, le plus récent en dernier) : calés à droite tant que HA en a moins
static void recevoir_historique_piece(int p, JsonDocument &j)
{
    if (!gr_pieces)
        return; // allocation échouée (page_graphe_preparer) : on n'écrit dans rien — 06/11 audit 9.6
    gr_historique_t &h = gr_pieces[p];
    JsonArrayConst t = j["t"], hu = j["h"];
    const int n = t.size(), dec = GR_POINTS - n; // rang du 1er point reçu dans nos 288
    for (int i = 0; i < GR_POINTS; i++)
    {
        const int k = i - dec;
        const int v = k >= 0 ? (t[k] | -999) : -999;
        h.temp[i] = v == -999 ? GR_VIDE : (int16_t)v;
        const int hv = k >= 0 && k < (int)hu.size() ? (hu[k] | 0) : 0;
        h.hum[i] = (uint8_t)LV_CLAMP(0, hv, 100);
        h.cons[i] = GR_VIDE;
    }
    for (JsonArrayConst r : j["c"].as<JsonArrayConst>()) // consigne en escalier : [rang, valeur] à chaque changement
    {
        const int debut = (r[0] | 0) + dec, v = r[1] | -999;
        for (int i = LV_MAX(0, debut); i < GR_POINTS; i++)
            h.cons[i] = v == -999 ? GR_VIDE : (int16_t)v;
    }
    const char *hx = j["ch"] | ""; // 1 bit par point, 36 octets en hexa (HA cale déjà sur 288)
    for (int k = 0; k < GR_POINTS / 8; k++)
    {
        unsigned o = 0;
        if (hx[2 * k] && hx[2 * k + 1])
            sscanf(hx + 2 * k, "%2x", &o);
        h.chauffe[k] = (uint8_t)o;
    }
    gr_minute_maintenant = j["fin"] | gr_minute_maintenant;
    page_graphe_maj();
}

static void recevoir_historique_ext(JsonDocument &j)
{
    if (!gr_ext)
        return; // allocation échouée (page_graphe_preparer) : on n'écrit dans rien — 06/11 audit 9.6
    JsonArrayConst e = j["ext"];
    const int dec = GR_POINTS - (int)e.size();
    for (int i = 0; i < GR_POINTS; i++)
    {
        const int v = i >= dec ? (e[i - dec] | -999) : -999;
        gr_ext[i] = v == -999 ? GR_VIDE : (int16_t)v;
    }
    page_graphe_maj();
}

// 31/10 (retour d'usage : la synchro, c'est « quand le dashboard a toutes ses valeurs renseignées ») : chaque valeur retenue par HA
// que les pages montrent, plus les fiches de l'imprimante annoncées par imprimante/fichiers ; l'agent du PC et la sonnette
// (évènement) ne sont pas retenus sur le broker (inventaire du 31/10) : pas attendus
#define ATTENDUS_MAX 64
static char g_attendus[ATTENDUS_MAX][32];
static int g_nb_attendus = 0;
static uint64_t g_recus = 0;
static uint32_t g_fiches_recues = 0;
static bool g_fiches_annoncees = false;

static void attendus_construire()
{
    auto ajouter = [](const char *a, const char *b, const char *c)
    {
        if (g_nb_attendus < ATTENDUS_MAX)
            snprintf(g_attendus[g_nb_attendus++], sizeof g_attendus[0], "%s%s%s", a, b, c);
    };
    for (const char *t : {"maison/etat", "maison/historique", "maison/reglages", "maison/appareils", "ballon/etat", "ballon/historique",
                          "croquettes/etat", "cafe/etat", "musique/etat", "poele/etat", "poele/historique", "panneau_led/etat",
                          "imprimante/etat", "imprimante/fichiers", "pc/ia", "pc/ia_activite"}) // 11/11 : les deux sujets IA sont retenus
        ajouter(t, "", "");
    for (const char *p : PIECES)
    {
        ajouter("thermostats/", p, "/etat");
        ajouter("thermostats/", p, "/historique");
    }
    for (const char *z : VMC_ZONES)
        ajouter("vmc/", z, "/etat");
    for (const char *a : AIR)
    {
        ajouter("air/", a, "/etat");
        ajouter("air/", a, "/historique");
    }
    for (const char *o : OUVRANTS)
        ajouter("ouvrants/", o, "/etat");
    for (const char *l : LAMPES)
        ajouter("lumieres/", l, "/etat");
}

static void attendu_noter(const char *s)
{
    if (!g_nb_attendus)
        attendus_construire();
    if (strncmp(s, "imprimante/fichiers/", 20) == 0)
    {
        const int k = atoi(s + 20);
        if (k >= 0 && k < 32)
            g_fiches_recues |= 1u << k;
        return;
    }
    if (strcmp(s, "imprimante/fichiers") == 0)
        g_fiches_annoncees = true;
    for (int i = 0; i < g_nb_attendus; i++)
        if (strcmp(s, g_attendus[i]) == 0)
            g_recus |= 1ull << i;
}

bool ui_mqtt_complet()
{
    if (!g_nb_attendus)
        attendus_construire();
    const uint32_t fiches = pr_etat.nb_fichiers >= 32 ? 0xFFFFFFFFu : (1u << pr_etat.nb_fichiers) - 1;
    return g_recus == (g_nb_attendus >= 64 ? ~0ull : (1ull << g_nb_attendus) - 1) && g_fiches_annoncees &&
           (g_fiches_recues & fiches) == fiches;
}

int ui_mqtt_manquants(char *b, size_t n)
{
    int nb = 0;
    size_t k = 0;
    b[0] = 0;
    for (int i = 0; i < g_nb_attendus; i++)
        if (!(g_recus >> i & 1))
        {
            nb++;
            if (k < n)
                k += snprintf(b + k, n - k, "%s%s", k ? ", " : "", g_attendus[i]);
        }
    for (int f = 0; f < pr_etat.nb_fichiers; f++)
        if (!(g_fiches_recues >> f & 1))
        {
            nb++;
            if (k < n)
                k += snprintf(b + k, n - k, "%simprimante/fichiers/%d", k ? ", " : "", f);
        }
    return nb;
}

// Routage « préfixe + tableau de noms + suffixe » : index de l'entrée visée, -1 si le sujet ne désigne
// rien (quatre blocs identiques mesurés lot 3 : air, ouvrants, lumieres, vmc).
static int index_nom(const char *t, const char *const noms[], int nb, const char *suffixe)
{
    for (int i = 0; i < nb; i++)
    {
        size_t n = strlen(noms[i]);
        if (strncmp(t, noms[i], n) == 0 && strcmp(t + n, suffixe) == 0)
            return i;
    }
    return -1;
}

void ui_mqtt_recevoir(const char *sujet, const uint8_t *message, unsigned longueur)
{
    const size_t lp = strlen(UI_MQTT_PREFIXE);
    if (strncmp(sujet, UI_MQTT_PREFIXE, lp) != 0)
        return;
    const char *s = sujet + lp;
    char etape[48];
    snprintf(etape, sizeof etape, "mqtt:%s", s);
    ui_diag_etape(etape); // ce message et la mise à jour de page qu'il déclenche

    struct psram_t : ArduinoJson::Allocator
    {
        void *allocate(size_t n) override { return lv_malloc(n); }
        void deallocate(void *p) override { lv_free(p); }
        void *reallocate(void *p, size_t n) override { return lv_realloc(p, n); }
    };
    static psram_t psram;
    JsonDocument j(&psram);
    DeserializationError err = deserializeJson(j, message, longueur);
    if (err)
    {
        ui_trace("[mqtt] JSON illisible sur %s : %s\n", sujet, err.c_str());
        return;
    }

    if (strncmp(s, "pc/", 3) != 0) // tout vient de HA, sauf l'agent du PC
        ui_mqtt_dernier_ha = lv_tick_get() | 1;
    attendu_noter(s);

    if (strcmp(s, "maison/etat") == 0)
        recevoir_maison(j);
    else if (strcmp(s, "maison/ciel") == 0)
        recevoir_ciel(j);
    else if (strcmp(s, "ballon/etat") == 0)
        recevoir_ballon(j);
    else if (strcmp(s, "ballon/historique") == 0)
        recevoir_ballon_historique(j);
    else if (strcmp(s, "croquettes/etat") == 0)
        recevoir_croquettes(j);
    else if (strcmp(s, "cafe/etat") == 0)
        recevoir_cafe(j);
    else if (strcmp(s, "musique/etat") == 0)
        recevoir_chaine(j);
    else if (strcmp(s, "maison/historique") == 0)
        recevoir_historique_ext(j);
    else if (strcmp(s, "maison/reglages") == 0)
        recevoir_reglages_maison(j);
    else if (strcmp(s, "sonnette/etat") == 0)
        ui_sonnette_recevoir((j["sonne"] | 0) != 0);
    else if (strcmp(s, "maison/appareils") == 0)
        recevoir_appareils(j);
    else if (strncmp(s, "appareil/", 9) == 0) // appareil/<nom>/reglages/set : seulement le nôtre
    {
        char attendu[64];
        snprintf(attendu, sizeof attendu, "appareil/%s/reglages/set", ui_appareil_nom());
        if (strcmp(s, attendu) == 0)
            ui_appareil_reglages_set(j);
    }
    else if (strcmp(s, "pc/etat") == 0)
        recevoir_pc(j);
    else if (strcmp(s, "pc/processus") == 0)
        recevoir_processus(j);
    else if (strcmp(s, "pc/ia") == 0)
        recevoir_ia(j);
    else if (strcmp(s, "pc/ia_activite") == 0)
        recevoir_ia_activite(j);
    else if (strcmp(s, "imprimante/etat") == 0)
        recevoir_imprimante(j);
    else if (strcmp(s, "imprimante/fichiers") == 0)
        recevoir_fichiers(j);
    else if (strncmp(s, "imprimante/fichiers/", 20) == 0)
        recevoir_fichier(atoi(s + 20), j);
    else if (strcmp(s, "poele/etat") == 0)
        recevoir_poele(j);
    else if (strcmp(s, "poele/historique") == 0)
        recevoir_poele_historique(j);
    else if (strcmp(s, "panneau_led/etat") == 0)
        recevoir_panneau_led(j);
    else if (strncmp(s, "air/", 4) == 0)
    {
        int i = index_nom(s + 4, AIR, AQ_NB, "/etat");
        if (i >= 0)
            recevoir_air(i, j);
        else if ((i = index_nom(s + 4, AIR, AQ_NB, "/historique")) >= 0)
            recevoir_air_historique(i, j);
    }
    else if (strncmp(s, "ouvrants/", 9) == 0)
    {
        const int i = index_nom(s + 9, OUVRANTS, OV_NB + 1, "/etat");
        if (i >= 0)
            recevoir_ouvrant(i, j);
    }
    else if (strncmp(s, "lumieres/", 9) == 0)
    {
        const int i = index_nom(s + 9, LAMPES, LU_NB, "/etat");
        if (i >= 0)
            recevoir_lampe(i, j);
    }
    else if (strncmp(s, "vmc/", 4) == 0)
    {
        const int i = index_nom(s + 4, VMC_ZONES, VMC_NB, "/etat");
        if (i >= 0)
            recevoir_vmc(i, j);
    }
    else if (strncmp(s, "thermostats/", 12) == 0)
    {
        const char *piece = s + 12, *fin = strchr(piece, '/');
        if (!fin || (strcmp(fin, "/etat") != 0 && strcmp(fin, "/historique") != 0))
            return;
        for (int i = 0; i < TH_NB_PIECES; i++)
            if (strlen(PIECES[i]) == (size_t)(fin - piece) && strncmp(piece, PIECES[i], fin - piece) == 0)
            {
                if (fin[1] == 'e')
                    recevoir_thermostat(i, j);
                else
                    recevoir_historique_piece(i, j);
            }
    }
}
