#include "ui_appareil.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "page_batterie.h"
#include "ui_mqtt.h"
#include "ui_diag.h"
#include "ui_shell.h"
#include "page_reglages.h"
#include "page_infos.h"
#include "page_thermostat.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>

bool (*ui_appareil_charger)(void *donnees, size_t taille) = nullptr;
void (*ui_appareil_sauver)(const void *donnees, size_t taille) = nullptr;
void (*ui_appareil_sante)(ui_sante_t &s) = nullptr;
void (*ui_appareil_redemarrer)() = nullptr;
void (*ui_appareil_effacer_memoire)() = nullptr;
void (*ui_appareil_lumiere)(int valeur) = nullptr;
uint32_t ui_appareil_repos_ms = 0;
uint32_t ui_appareil_extinction_ms = 0;
char ui_appareil_reveil_info[192] = "";
void (*ui_appareil_regler_heure)(uint32_t unix) = nullptr;
bool (*ui_appareil_wifi)() = nullptr;

static char g_nom[24];
static const char *const PIECES[TH_NB_PIECES] = {"cuisine", "sejour", "chambre1", "chambre2", "sdb"}; // ordre de th_pieces
static const int VEILLES_S[] = {30, 60, 300, 0};  // page_reglages : 30 s / 1 min / 5 min / Jamais
static const int RETOURS_S[] = {30, 60, 120, 0};  // page_reglages : après 30 s / 60 s / 2 min / jamais
#define SANTE_MS 60000
#define HA_SILENCE_MS 180000 // HA republie la liste des appareils chaque minute : 3 min sans rien = HA muet

// Ce qui est gardé en mémoire flash (seulement les réglages de l'écran ; ceux de la maison sont tenus par HA)
struct memoire_t
{
    uint8_t version; // MEMOIRE_VERSION : une autre forme (ancien firmware) est ignorée
    int8_t lum_jour, lum_nuit, veille, piece, retour;
    bool camera;
    int8_t lum_jour_repos, lum_nuit_repos;
};
#define MEMOIRE_VERSION 2 // 2 (24/10) : niveaux de repos

const char *ui_appareil_nom() { return g_nom; }

static bool publier(const char *sujet_court, const char *message, bool retenu)
{
    char sujet[96];
    snprintf(sujet, sizeof sujet, UI_MQTT_PREFIXE "appareil/%s/%s", g_nom, sujet_court);
    return ui_mqtt_publier && ui_mqtt_publier(sujet, message, retenu);
}
static bool g_reglages_a_publier = false; // pas encore parti (broker pas encore là) : retenté chaque seconde

// ---------------------------------------------------------------------------------------------------- réglages de l'écran
static void appliquer()
{
    const rg_reglages_t &r = rg_reglages;
    ui_shell_retour_accueil(RETOURS_S[r.retour] * 1000u);
    th_piece_affichee = r.piece;
}

static void publier_reglages()
{
    g_reglages_a_publier = true;
    const rg_reglages_t &r = rg_reglages;
    char m[160];
    snprintf(m, sizeof m,
             "{\"lum_jour\":%d,\"lum_jour_repos\":%d,\"lum_nuit\":%d,\"lum_nuit_repos\":%d,\"veille_s\":%d,\"piece\":\"%s\",\"retour_s\":%d,\"camera\":%d}",
             r.lum_jour, r.lum_jour_repos, r.lum_nuit, r.lum_nuit_repos, VEILLES_S[r.veille], PIECES[r.piece], RETOURS_S[r.retour], r.camera);
    if (publier("reglages", m, true))
        g_reglages_a_publier = false;
}

static void sauver()
{
    const rg_reglages_t &r = rg_reglages;
    memoire_t m = {MEMOIRE_VERSION, (int8_t)r.lum_jour, (int8_t)r.lum_nuit, (int8_t)r.veille, (int8_t)r.piece, (int8_t)r.retour, r.camera,
                   (int8_t)r.lum_jour_repos, (int8_t)r.lum_nuit_repos};
    if (ui_appareil_sauver)
        ui_appareil_sauver(&m, sizeof m);
}

static int index_de(const int *t, int n, int v)
{
    for (int i = 0; i < n; i++)
        if (t[i] == v)
            return i;
    return -1;
}

void ui_appareil_reglages_set(JsonDocument &j)
{
    rg_reglages_t &r = rg_reglages;
    if (j["lum_jour"].is<int>())
        r.lum_jour = LV_CLAMP(5, j["lum_jour"].as<int>(), 100);
    if (j["lum_nuit"].is<int>())
        r.lum_nuit = LV_CLAMP(5, j["lum_nuit"].as<int>(), 100);
    if (j["lum_jour_repos"].is<int>())
        r.lum_jour_repos = LV_CLAMP(0, j["lum_jour_repos"].as<int>(), r.lum_jour);
    if (j["lum_nuit_repos"].is<int>())
        r.lum_nuit_repos = LV_CLAMP(0, j["lum_nuit_repos"].as<int>(), r.lum_nuit);
    int k;
    if (j["veille_s"].is<int>() && (k = index_de(VEILLES_S, 4, j["veille_s"].as<int>())) >= 0)
        r.veille = k;
    if (j["retour_s"].is<int>() && (k = index_de(RETOURS_S, 4, j["retour_s"].as<int>())) >= 0)
        r.retour = k;
    for (int i = 0; i < TH_NB_PIECES; i++)
        if (strcmp(j["piece"] | "", PIECES[i]) == 0)
            r.piece = i;
    if (j["camera"].is<int>())
        r.camera = j["camera"].as<int>() != 0;
    appliquer();
    sauver();
    publier_reglages();
    page_reglages_maj();
}

// ---------------------------------------------------------------------------------------------------- réglages de la maison
static void maison(const char *action, const char *cle, int valeur, const char *texte)
{
    JsonDocument j; // nom de mélodie : sérialisé (accents, guillemets)
    j["action"] = action;
    if (texte)
        j[cle] = texte;
    else
        j[cle] = valeur;
    char m[96];
    serializeJson(j, m, sizeof m);
    if (ui_mqtt_publier)
        ui_mqtt_publier(UI_MQTT_PREFIXE "maison/reglages/set", m, false);
}

static void commande(const char *quoi)
{
    const rg_reglages_t &r = rg_reglages;
    if (!strcmp(quoi, "melodie"))
        maison("melodie", "nom", 0, r.melodie);
    else if (!strcmp(quoi, "volume"))
        maison("volume", "volume", r.volume, nullptr);
    else if (!strcmp(quoi, "terrasse_porte") || !strcmp(quoi, "terrasse_mouvement"))
        maison(quoi, "on", strcmp(quoi, "terrasse_porte") ? r.terrasse_mouvement : r.terrasse_porte, nullptr);
    else // réglage de cet écran
    {
        appliquer();
        sauver();
        publier_reglages();
    }
}

// ---------------------------------------------------------------------------------------------------- heure, journal
void ui_appareil_heure(uint32_t unix)
{
    const time_t maintenant = time(nullptr);
    const bool reglee = maintenant > 1600000000; // sinon : 1970, jamais réglée
    if (!ui_appareil_regler_heure || unix < 1600000000 || (reglee && (time_t)unix < maintenant + 60))
        return;
    ui_appareil_regler_heure(unix);
}

#define JOURNAL_ATTENTE 8
typedef char attente_t[320];
static UI_PSRAM(attente_t, g_journal_attente, JOURNAL_ATTENTE, nullptr); // 29/10 : en PSRAM (ui_psram.h) // JSON prêt à partir (broker pas encore là) ; 27/10 : 128 -> 320 (lignes de mesure coupées)
static int g_nb_attente = 0;

static void journal_envoyer()
{
    while (g_nb_attente && publier("journal", g_journal_attente[0], false))
    {
        memmove(g_journal_attente[0], g_journal_attente[1], sizeof g_journal_attente[0] * (JOURNAL_ATTENTE - 1));
        g_nb_attente--;
    }
}

void ui_journal(bool alerte, const char *fmt, ...)
{
    char texte[256], heure[8] = "--:--"; // 27/10 : 96 coupait la ligne « Réveil » (mesure du coucher)
    va_list va;
    va_start(va, fmt);
    vsnprintf(texte, sizeof texte, fmt, va);
    va_end(va);
    const time_t maintenant = time(nullptr);
    if (maintenant > 1600000000)
    {
        struct tm t;
        localtime_r(&maintenant, &t);
        snprintf(heure, sizeof heure, "%02d:%02d", t.tm_hour, t.tm_min);
    }
    if_journal_ajouter(heure, texte, alerte);
    page_infos_maj();
    JsonDocument j; // HA horodate lui-même (journal de bord) : le texte et l'alerte suffisent
    j["texte"] = texte;
    j["alerte"] = alerte ? 1 : 0;
    if (g_nb_attente == JOURNAL_ATTENTE) // file pleine : la plus ancienne ligne en attente saute
    {
        memmove(g_journal_attente[0], g_journal_attente[1], sizeof g_journal_attente[0] * (JOURNAL_ATTENTE - 1));
        g_nb_attente--;
    }
    serializeJson(j, g_journal_attente[g_nb_attente++], sizeof g_journal_attente[0]);
    journal_envoyer();
}

// 26/10 : sortie de veille profonde (écran sur batterie) : une seule ligne par réveil dans le journal de HA
static bool reveil()
{
    ui_sante_t s = {};
    if (ui_appareil_sante)
        ui_appareil_sante(s);
    return strcmp(s.redemarrage, "réveil") == 0;
}

static bool g_reseau_volontaire = false; // 31/10 : Wi-Fi coupé exprès (ui_appareil_reseau_volontaire)
static bool g_wifi_journal = false;     // état du Wi-Fi vu par le journal
static bool g_demarre = false;          // la ligne du démarrage (ou du réveil) est écrite

static void mqtt_evenement(bool connecte, uint32_t duree_ms)
{
    static bool premiere = true;
    if (g_reseau_volontaire)
        return;
    const bool taire = premiere && connecte && reveil(); // la connexion du réveil : dans la ligne « Réveil »
    premiere = false;
    if (taire)
        return;
    if (connecte)
        ui_journal(false, "MQTT connecté en %u,%u s", (unsigned)(duree_ms / 1000), (unsigned)(duree_ms % 1000 / 100));
    else
        ui_journal(true, "MQTT perdu");
}

// Évènements vus chaque seconde : démarrage (1res données de HA), Wi-Fi, HA muet
static void journal_tic()
{
    static bool ha_muet = false;
    bool &demarre = g_demarre, &wifi = g_wifi_journal;
    if (!demarre && ui_mqtt_dernier_ha)
    {
        demarre = true;
        ui_sante_t s = {};
        if (ui_appareil_sante)
            ui_appareil_sante(s);
        if (ui_diag_etape_precedente()[0]) // plantage ou figeage : où en était la boucle (le détail : appareil/<nom>/dump)
            ui_journal(true, "Démarrage (%s, pendant %s) · données en %u,%u s · version %s", s.redemarrage, ui_diag_etape_precedente(),
                       (unsigned)(ui_mqtt_dernier_ha / 1000), (unsigned)(ui_mqtt_dernier_ha % 1000 / 100), s.version);
        else if (strcmp(s.redemarrage, "réveil") == 0)
            ui_journal(false, "Réveil · données en %u,%u s%s", (unsigned)(ui_mqtt_dernier_ha / 1000),
                       (unsigned)(ui_mqtt_dernier_ha % 1000 / 100), ui_appareil_reveil_info);
        else
            ui_journal(false, "Démarrage (%s) · données en %u,%u s · version %s%s", s.redemarrage[0] ? s.redemarrage : "?",
                       (unsigned)(ui_mqtt_dernier_ha / 1000), (unsigned)(ui_mqtt_dernier_ha % 1000 / 100), s.version,
                       ui_appareil_reveil_info);
    }
    const bool w = ui_appareil_wifi ? ui_appareil_wifi() : true;
    static bool wifi_premier = true;
    if (w != wifi && wifi_premier && w && reveil()) // la connexion du réveil : dans la ligne « Réveil »
        wifi = w;
    if (w)
        wifi_premier = false;
    if (g_reseau_volontaire)
        wifi = w;
    if (w != wifi)
    {
        wifi = w;
        ui_sante_t s = {};
        if (w && ui_appareil_sante)
            ui_appareil_sante(s);
        if (w && s.wifi[0])
            ui_journal(false, "Wi-Fi %s · %d dBm", s.wifi, s.rssi);
        else if (w)
            ui_journal(false, "Wi-Fi connecté");
        else
            ui_journal(true, "Wi-Fi perdu");
    }
    const bool muet = !g_reseau_volontaire && ui_mqtt_dernier_ha && lv_tick_elaps(ui_mqtt_dernier_ha) >= HA_SILENCE_MS;
    if (muet != ha_muet)
    {
        ha_muet = muet;
        if (muet)
            ui_journal(true, "Home Assistant muet depuis 3 min");
        else
            ui_journal(false, "Home Assistant répond de nouveau");
    }
    journal_envoyer();
    ui_diag_publier(ui_mqtt_publier, g_nom); // dump du plantage précédent, retenu (une fois, dès que le broker est là)
}

// ---------------------------------------------------------------------------------------------------- santé, liaisons, lumière
static bool sante()
{
    if (!ui_appareil_sante)
        return false;
    ui_sante_t s = {};
    ui_appareil_sante(s);
    const uint32_t j = s.depuis_s / 86400, h = s.depuis_s % 86400 / 3600, mn = s.depuis_s % 3600 / 60;
    if (j)
        snprintf(if_systeme[0].valeur, sizeof if_systeme[0].valeur, "%u j %02u h", (unsigned)j, (unsigned)h);
    else
        snprintf(if_systeme[0].valeur, sizeof if_systeme[0].valeur, "%u h %02u min", (unsigned)h, (unsigned)mn);
    snprintf(if_systeme[1].valeur, sizeof if_systeme[1].valeur, "%s", s.redemarrage);
    // 25/10 (retour d'usage : « débordement PSRAM ») : RAM et PSRAM chacune dans sa case, la PSRAM en Mo
    snprintf(if_systeme[2].valeur, sizeof if_systeme[2].valeur, "%u Ko", (unsigned)s.ram_ko);
    snprintf(if_systeme[3].valeur, sizeof if_systeme[3].valeur, "%u,%u Mo", (unsigned)(s.psram_ko / 1024), (unsigned)(s.psram_ko % 1024 * 10 / 1024));
    snprintf(if_systeme[4].valeur, sizeof if_systeme[4].valeur, "%d °C", s.cpu_temp);
    snprintf(if_systeme[5].valeur, sizeof if_systeme[5].valeur, "%d MHz", s.cpu_mhz); // 31/10 : comme l'ancien dashboard
    snprintf(if_systeme[6].valeur, sizeof if_systeme[6].valeur, "%u ms", (unsigned)ui_diag_boucle_max()); // jamais remplie avant
    if (s.tx_dbm4) // 31/10 : puissance d'émission Wi-Fi (7 dBm en réglage réduit)
        snprintf(if_systeme[7].valeur, sizeof if_systeme[7].valeur, "%d,%d dBm", s.tx_dbm4 / 4, s.tx_dbm4 % 4 * 25 / 10);
    else
        snprintf(if_systeme[7].valeur, sizeof if_systeme[7].valeur, "%s", "arrêté");
    snprintf(if_systeme[8].valeur, sizeof if_systeme[8].valeur, "%s", s.version);
    snprintf(if_systeme[9].valeur, sizeof if_systeme[9].valeur, "%s", s.ip);

    if_liaisons[0].ok = s.wifi[0] != 0;
    if (s.wifi[0])
        snprintf(if_liaisons[0].detail, sizeof if_liaisons[0].detail, "%s · %d dBm", s.wifi, s.rssi);
    else
        snprintf(if_liaisons[0].detail, sizeof if_liaisons[0].detail, "%s", "déconnecté");
    snprintf(rg_reglages.ssid, sizeof rg_reglages.ssid, "%s", s.wifi);
    snprintf(rg_reglages.ip, sizeof rg_reglages.ip, "%s", s.ip);
    rg_reglages.rssi = s.rssi;

    char m[420];
    int n = snprintf(m, sizeof m,
                     "{\"depuis\":\"%s\",\"redemarrage\":\"%s\",\"ram\":%u,\"psram\":%u,\"cpu_temp\":%d,\"version\":\"%s\",\"wifi\":\"%s\",\"ip\":\"%s\",\"rssi\":%d",
                     if_systeme[0].valeur, s.redemarrage, (unsigned)s.ram_ko, (unsigned)s.psram_ko, s.cpu_temp, s.version, s.wifi, s.ip, s.rssi);
    if (bt_etat.present && bt_etat.tension > 1) // 26/10 : écran sur batterie, surveillé par HA comme les autres appareils sur batterie
        n += snprintf(m + n, sizeof m - n, ",\"batterie\":%d,\"tension\":%.2f,\"en_charge\":%d,\"debranche\":%u", bt_etat.pourcent,
                      bt_etat.tension, bt_etat.charge ? 1 : 0, (unsigned)bt_etat.depuis);
    snprintf(m + n, sizeof m - n, "}");
    return publier("infos", m, true);
}

bool ui_appareil_synchroniser()
{
    static bool fait = false;
    if (fait)
        return true;
    if (!ui_mqtt_complet()) // 31/10 (retour d'usage) : toutes les valeurs du dashboard renseignées
        return false;
    journal_tic(); // la ligne du réveil sans attendre le tic de la seconde
    if (!g_demarre || g_nb_attente || !bt_etat.present || bt_etat.tension <= 1)
        return false;
    fait = sante();
    return fait;
}

void ui_appareil_reseau_volontaire(bool coupe)
{
    g_reseau_volontaire = coupe;
    if (!coupe) // le journal repart de l'état présent : pas de « Wi-Fi connecté » pour le rallumage
        g_wifi_journal = ui_appareil_wifi ? ui_appareil_wifi() : true;
}

static void liaison_ha()
{
    const bool ok = ui_mqtt_dernier_ha && lv_tick_elaps(ui_mqtt_dernier_ha) < HA_SILENCE_MS;
    if_liaisons[1].ok = ok;
    if (!ui_mqtt_dernier_ha)
        snprintf(if_liaisons[1].detail, sizeof if_liaisons[1].detail, "%s", "rien reçu");
    else
        snprintf(if_liaisons[1].detail, sizeof if_liaisons[1].detail, "il y a %u s", (unsigned)(lv_tick_elaps(ui_mqtt_dernier_ha) / 1000));
}

// Rétroéclairage, repris de la production (setBacklight / reduced_power_mode / full_power_mode) :
// - en service : lum_jour / lum_nuit (jour / nuit de maison/etat) ; après la veille sans toucher : niveau de repos ;
//   éteint seulement si personne n'est là (presence de maison/etat à 0 depuis 5 min, comme HOME_GRACE_PERIOD) ;
// - chaque changement en fondu d'1 s (FADE_DURATION) ; amélioré : interpolé en luminosité PERÇUE (gamma 2,2) et non en
//   MLI brute, sinon les faibles niveaux de nuit sautent par paliers ; les niveaux d'arrivée restent ceux réglés.
#define FONDU_MS 1000
#define ABSENCE_MS (5 * 60 * 1000)
#define APERCU_MONTRE_MS 2000 // au relâcher d'un curseur : le vrai niveau montré 2 s
static bool g_eteint = false;  // écran éteint (absent) : le toucher qui le rallume ne clique pas
static int g_mli = 0, g_depart = 0, g_cible = -1; // 0..255
static uint32_t g_debut_fondu = 0, g_absent_depuis = 0, g_apercu_fin = 0;
static int g_apercu = -1; // % imposé par la page Réglages (-1 : aucun)
static lv_timer_t *g_fondu = nullptr;
static lv_timer_t *g_trans = nullptr; // transition par le rétroéclairage en cours (ui_appareil_transition)

static int percue(int mli) { return (int)(255.0f * powf(mli / 255.0f, 1 / 2.2f)); }
// arrondi vers la cible : en montée le 1er pas n'est pas 0 (la MLI 8 bits écrase les tout petits niveaux)
static int mli_de(float p, bool monte) { const float v = 255.0f * powf(p / 255.0f, 2.2f); return (int)(monte ? ceilf(v) : floorf(v)); }

static void fondu_cb(lv_timer_t *)
{
    if (g_trans) // une transition tient le rétroéclairage
        return;
    const uint32_t t = lv_tick_elaps(g_debut_fondu);
    if (t >= FONDU_MS)
        g_mli = g_cible;
    else
    {
        const float a = percue(g_depart), b = percue(g_cible);
        g_mli = mli_de(a + (b - a) * t / FONDU_MS, b > a);
    }
    if (ui_appareil_lumiere)
        ui_appareil_lumiere(g_mli);
    if (t >= FONDU_MS)
        lv_timer_pause(g_fondu);
}

// --- transition par le rétroéclairage (voir ui_appareil.h) ---
#define TRANS_BAISSE_MS 150
#define TRANS_MONTEE_MS 300
static void (*g_trans_milieu)() = nullptr;
static uint32_t g_trans_t = 0;
static int g_trans_phase = 0; // 0 baisse, 1 un tour pour dessiner, 2 montée

static void transition_cb(lv_timer_t *)
{
    const uint32_t e = lv_tick_elaps(g_trans_t);
    const float haut = percue(g_mli);
    if (g_trans_phase == 0)
    {
        if (ui_appareil_lumiere)
            ui_appareil_lumiere(e >= TRANS_BAISSE_MS ? 0 : mli_de(haut * (TRANS_BAISSE_MS - e) / TRANS_BAISSE_MS, false));
        if (e < TRANS_BAISSE_MS)
            return;
        if (g_trans_milieu)
            g_trans_milieu();
        g_trans_phase = 1; // LVGL dessine le nouvel écran pendant ce tour, dans le noir
        return;
    }
    if (g_trans_phase == 1)
    {
        g_trans_phase = 2;
        g_trans_t = lv_tick_get();
        return;
    }
    if (e < TRANS_MONTEE_MS)
    {
        if (ui_appareil_lumiere)
            ui_appareil_lumiere(mli_de(haut * e / TRANS_MONTEE_MS, true));
        return;
    }
    if (ui_appareil_lumiere)
        ui_appareil_lumiere(g_mli);
    lv_timer_del(g_trans);
    g_trans = nullptr;
    if (g_fondu && g_mli != g_cible) // un fondu normal en cours : il reprend
        lv_timer_resume(g_fondu);
}

void ui_appareil_transition(void (*milieu)())
{
    if (g_trans) // déjà en cours : le milieu tout de suite
    {
        if (milieu)
            milieu();
        return;
    }
    g_trans_milieu = milieu;
    g_trans_phase = 0;
    g_trans_t = lv_tick_get();
    if (g_fondu)
        lv_timer_pause(g_fondu);
    g_trans = lv_timer_create(transition_cb, 15, nullptr);
}

static void aller_vers(int mli, bool fondu)
{
    if (mli == g_cible)
        return;
    g_cible = mli;
    if (!fondu) // aperçu : tout de suite
    {
        g_mli = mli;
        if (g_fondu)
            lv_timer_pause(g_fondu);
        if (ui_appareil_lumiere)
            ui_appareil_lumiere(mli);
        return;
    }
    g_depart = g_mli; // depuis où on en est (un fondu en cours repart de là)
    g_debut_fondu = lv_tick_get();
    if (!g_fondu)
        g_fondu = lv_timer_create(fondu_cb, 20, nullptr);
    lv_timer_resume(g_fondu);
}

static void lumiere()
{
    const rg_reglages_t &r = rg_reglages;
    if (ui_maison.presence)
        g_absent_depuis = 0;
    else if (!g_absent_depuis)
        g_absent_depuis = lv_tick_get() | 1;
    const bool absent = g_absent_depuis && lv_tick_elaps(g_absent_depuis) >= ABSENCE_MS;
    if (g_apercu >= 0) // la page Réglages montre un niveau
    {
        if (!g_apercu_fin || lv_tick_elaps(g_apercu_fin) < APERCU_MONTRE_MS)
        {
            aller_vers(g_apercu * 255 / 100, false);
            return;
        }
        g_apercu = -1;
    }
    const int veille_ms = ui_appareil_repos_ms ? (int)ui_appareil_repos_ms : VEILLES_S[r.veille] * 1000;
    const bool repos = veille_ms && lv_disp_get_inactive_time(NULL) > (uint32_t)veille_ms;
    g_eteint = (repos && absent) ||
               (ui_appareil_extinction_ms && lv_disp_get_inactive_time(NULL) > ui_appareil_extinction_ms); // diag radar
    const int pct = g_eteint ? 0 : repos ? (ui_maison.nuit ? r.lum_nuit_repos : r.lum_jour_repos) : (ui_maison.nuit ? r.lum_nuit : r.lum_jour);
    aller_vers(pct * 255 / 100, true);
}

int ui_appareil_veille_s() { return VEILLES_S[rg_reglages.veille]; }

void ui_appareil_rallumer()
{
    g_mli = 0;
    g_cible = -1;
    if (ui_appareil_lumiere)
        ui_appareil_lumiere(0);
    lumiere();
}

static void apercu(int pct, bool fin)
{
    g_apercu = pct;
    g_apercu_fin = fin ? lv_tick_get() | 1 : 0;
    lumiere();
}

bool ui_appareil_toucher(bool appuye)
{
    static bool avale = false; // le doigt qui a réveillé l'écran : caché jusqu'à ce qu'il se lève
    if (appuye && g_eteint) // au repos (pas éteint) l'écran se lit : le toucher clique, comme la production
    {
        avale = true;
        lv_disp_trig_activity(NULL); // le toucher caché compte quand même comme une activité
        lumiere();                  // rallumé tout de suite, pas au tic suivant
    }
    else if (!appuye)
        avale = false;
    return avale;
}

static void tic(lv_timer_t *)
{
    static uint32_t derniere_sante = 0;
    lumiere();
    liaison_ha();
    journal_tic();
    if (g_reglages_a_publier)
        publier_reglages();
    if (!derniere_sante || lv_tick_elaps(derniere_sante) >= SANTE_MS)
    {
        derniere_sante = lv_tick_get() | 1;
        sante();
        page_infos_maj();
        page_reglages_maj();
    }
}

static void action(if_action_t a)
{
    if (a == IF_REDEMARRER && ui_appareil_redemarrer)
        ui_appareil_redemarrer();
    else if (a == IF_RAZ_MEMOIRE && ui_appareil_effacer_memoire)
        ui_appareil_effacer_memoire();
}

void ui_appareil_demarrer(const char *nom)
{
    snprintf(g_nom, sizeof g_nom, "%s", nom);
    rg_reglages_t &r = rg_reglages;
    memoire_t m;
    if (ui_appareil_charger && ui_appareil_charger(&m, sizeof m) && m.version == MEMOIRE_VERSION)
    {
        r.lum_jour = LV_CLAMP(5, m.lum_jour, 100);
        r.lum_nuit = LV_CLAMP(5, m.lum_nuit, 100);
        r.veille = LV_CLAMP(0, m.veille, 3);
        r.piece = LV_CLAMP(0, m.piece, TH_NB_PIECES - 1);
        r.retour = LV_CLAMP(0, m.retour, 3);
        r.camera = m.camera;
        r.lum_jour_repos = LV_CLAMP(0, m.lum_jour_repos, r.lum_jour);
        r.lum_nuit_repos = LV_CLAMP(0, m.lum_nuit_repos, r.lum_nuit);
    }
    else // jamais réglé : la pièce de l'écran d'après son nom (« cuisine », « chambre2 »…)
        for (int i = 0; i < TH_NB_PIECES; i++)
            if (!strcmp(nom, PIECES[i]))
                r.piece = i;
    rg_commande = commande;
    rg_apercu = apercu;
    ui_mqtt_evenement = mqtt_evenement;
    if_action = action;
    appliquer();
    publier_reglages(); // retenu : HA sait comment est réglé l'écran
    lumiere();          // allumage en fondu dès le démarrage
    lv_timer_create(tic, 1000, nullptr);
}
