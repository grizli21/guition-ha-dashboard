// 26/10/2026 : radar LD2410S sur l'ESP32 (voir ui_radar.h). Fonctions et page web reprises de la production
// (radar.h : radar_read_configuration, radar_apply_full_configuration, radar_start_auto_threshold,
// radar_finish_auto_threshold ; radar_webserver.h : mêmes adresses, même page). Onglet « Journal » de la page web :
// « complet » = les traces de la session (la production : /log_complet.txt), « résumé » = les réveils des 24 h.
#ifdef ARDUINO
#include "ui_radar.h"
#include "ui_langue.h"
#include "page_radar.h"
#include "page_batterie.h"
#include "radar_page_web.h"
#include "ui_diag.h"
#include "ui_appareil.h"
#include <Arduino.h>
#include <esp_rtc_time.h>
#include <HLK-LD2410S.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include "memoire_brute.h" // 13/11 : réglages en flash sans NVS
#include <lvgl.h>
#include <time.h>
#include <math.h>

#define RX_PIN 41
#define TX_PIN 40

static LD2410S ld2410s(Serial1, RX_PIN, TX_PIN);
static WebServer g_web(80);
static bool g_actif = false, g_web_pret = false;
static volatile uint32_t g_web_activite = 0;
static const uint32_t WEB_ACTIVITE_MS = 60000;
static bool g_auto_a_finir = false;
static uint8_t g_auto_interrompu = 0; // 30/10 : progression où le réglage auto s'est figé (0 = fini normalement)
static bool g_config_lue = false;
static uint16_t g_fw[3] = {0, 0, 0};
static bool g_fw_lu = false;
static uint32_t g_diag_debut_ms = 0;
// 28/10 : passage en trames PC au lancement du diag : 1er essai tout de suite, puis filet de 4 essais toutes les 2 s
static uint8_t g_diag_essais = 0;
static uint32_t g_diag_essai_ms = 0;
static String octets_recus();
static bool passer_pc();
static void echange_brut(const char *quoi, const uint8_t *octets, size_t n)
{
    char h[96 * 2 + 1];
    size_t k = 0;
    for (size_t i = 0; i < n && k + 2 < sizeof h; i++)
        k += snprintf(h + k, sizeof h - k, "%02X", octets[i]);
    h[k] = 0;
    ui_trace("[echange] %s (%u o) %s\n", quoi, (unsigned)n, h);
}

static const uint32_t DIAG_MS = 12UL * 3600 * 1000; // arrêt automatique (une nuit, sans vider la batterie si on l'oublie)

// paramètres communs (relus du capteur)
static uint32_t g_far = 8, g_near = 0, g_delai = 10, g_freq_etat = 5, g_freq_dist = 5, g_vitesse = 10;

// 31/10 (retour d'usage : « pas de lecture du radar si déjà en mémoire ») : la configuration du capteur, une fois relue (ou
// écrite), est gardée en mémoire RTC : elle survit à la veille profonde ; l'onglet Radar ne la relit plus à chaque ouverture
#define CONFIG_RTC_MAGIC 0x52414443u // « RADC »
struct config_rtc_t
{
    uint32_t magic;
    uint8_t detection[RD_ZONES], maintien[RD_ZONES];
    uint32_t far, near, delai, freq_etat, freq_dist, vitesse;
};
RTC_NOINIT_ATTR static config_rtc_t g_config_rtc;

static void memoriser()
{
    g_config_rtc = {CONFIG_RTC_MAGIC, {}, {}, g_far, g_near, g_delai, g_freq_etat, g_freq_dist, g_vitesse};
    memcpy(g_config_rtc.detection, rd_etat.detection, RD_ZONES);
    memcpy(g_config_rtc.maintien, rd_etat.maintien, RD_ZONES);
    g_config_lue = rd_etat.lu = true;
}

static bool retrouver()
{
    if (g_config_rtc.magic != CONFIG_RTC_MAGIC)
        return false;
    memcpy(rd_etat.detection, g_config_rtc.detection, RD_ZONES);
    memcpy(rd_etat.maintien, g_config_rtc.maintien, RD_ZONES);
    g_far = g_config_rtc.far, g_near = g_config_rtc.near, g_delai = g_config_rtc.delai;
    g_freq_etat = g_config_rtc.freq_etat, g_freq_dist = g_config_rtc.freq_dist, g_vitesse = g_config_rtc.vitesse;
    g_config_lue = rd_etat.lu = true;
    return true;
}

// affichées tant que le capteur n'a pas été relu : ce que la production (radar.h) mettait VRAIMENT dans le capteur 1.1.1
// (0x72 = 16 déclenchements : ses « maintiens 0-7 » 20…14 y tombaient en portes 8-15 ; maintien 0x77 jamais écrit : inconnu)
static const uint8_t DETECTION[RD_ZONES] = {75, 65, 40, 30, 25, 25, 20, 20, 20, 20, 20, 20, 20, 20, 15, 14};
static const uint8_t MAINTIEN[RD_ZONES] = {0};

static void mode_comm()
{
    // trames « PC » : énergie par zone (comme la production : RADAR_COMM_MODE_PC)
    ld2410s.setCommMode(LD2410SCommMode::PC);
}

static bool lire_config()
{
    // 30/10 : pas pendant un réglage auto : entrer en configuration risque de l'interrompre (resté à 51 % sur le séjour,
    // juste après une relecture : lien à confirmer)
    if (ld2410s.isAutoThresholdRunning())
    {
        ui_trace("[radar] relecture refusée : réglage auto en cours\n");
        return false;
    }
    if (!ld2410s.enterConfigMode())
    {
        ui_trace("[radar] pas de réponse à la demande de configuration\n");
        return false;
    }
    delay(20);
    if (!g_fw_lu) // radar_get_revision de la production (sur le port série)
    {
        g_fw_lu = ld2410s.readFirmwareVersion(g_fw[0], g_fw[1], g_fw[2]);
        ui_trace("[radar] firmware %u.%u.%u%s\n", g_fw[0], g_fw[1], g_fw[2], g_fw_lu ? "" : " (illisible)");
        delay(20);
    }
    ld2410s.readCommonParametersCommand(g_far, g_near, g_delai, g_freq_etat, g_freq_dist, g_vitesse);
    // 30/10 : firmware 1.1.1 : 16 seuils de déclenchement (0x73) et 16 de maintien (0x77), voir HLK-LD2410S.h
    const bool a = ld2410s.readTriggers16(rd_etat.detection);
    const bool b = ld2410s.readHolds16(rd_etat.maintien);
    delay(20);
    mode_comm();
    delay(20);
    ld2410s.exitConfigMode();
    g_config_lue = a && b;
    if (g_config_lue)
        memoriser();
    ui_trace("[radar] configuration %s (portée %u-%u, délai %u s)\n", g_config_lue ? "relue" : "illisible", (unsigned)g_near,
             (unsigned)g_far, (unsigned)g_delai);
    return g_config_lue;
}

static bool appliquer(uint16_t far, uint16_t near, uint16_t delai, float freq_etat, float freq_dist, uint8_t vitesse)
{
    if (!ld2410s.enterConfigMode())
        return false;
    bool ok = ld2410s.setCustomParameters(far, near, delai, freq_etat, freq_dist, vitesse);
    delay(20);
    ok = ld2410s.writeTriggers16(rd_etat.detection) && ok;
    delay(20);
    ok = ld2410s.writeHolds16(rd_etat.maintien) && ok;
    delay(20);
    ld2410s.readCommonParametersCommand(g_far, g_near, g_delai, g_freq_etat, g_freq_dist, g_vitesse);
    delay(20);
    mode_comm();
    delay(20);
    ld2410s.exitConfigMode();
    if (ok)
        memoriser();
    return ok;
}

static bool demarrer_auto(uint16_t secondes)
{
    if (!ld2410s.enterConfigMode())
        return false;
    const bool ok = ld2410s.startAutoThreshold(2, 1, secondes); // marges de la documentation Hi-Link (comme la production)
    delay(20);
    ld2410s.exitConfigMode(); // indispensable pour que la progression arrive
    if (ok)
        g_auto_a_finir = true;
    return ok;
}

// 30/10 : marge en dB ajoutée aux seuils de DÉCLENCHEMENT calculés par le réglage auto (le capteur, facteur 2, les met à
// 2-3 sigma du bruit : faux positifs) ; les seuils de maintien ne bougent pas ; gardée en mémoire flash (réglée sur la page web)
#define MARGE_DEFAUT 5 // 30/10 (retour d'usage) : +5 dB par défaut (bruit max relevé à 1-3 dB des seuils du réglage auto)
static uint8_t g_marge = MARGE_DEFAUT;

static void marge_charger()
{
    if (!mb_lire(MB_RADAR, &g_marge, 1)) // 13/11 : mémoire brute ; NVS seulement pour la migration
    {
        Preferences p;
        if (p.begin("radar", true)) // n'aboutit pas tant que rien n'a été enregistré : la valeur par défaut reste
        {
            g_marge = p.getUChar("marge", MARGE_DEFAUT);
            p.end();
            mb_noter(MB_RADAR, &g_marge, 1);
        }
    }
    rd_etat.marge = g_marge;
}

static void marge_sauver(uint8_t db)
{
    g_marge = db;
    rd_etat.marge = db;
    mb_ecrire(MB_RADAR, &db, 1); // 13/11 : plus de NVS
}

static bool finir_auto(bool avec_marge)
{
    if (!ld2410s.enterConfigMode())
        return false;
    delay(20);
    bool lu = ld2410s.readTriggers16(rd_etat.detection);
    delay(20);
    lu = ld2410s.readHolds16(rd_etat.maintien) && lu;
    delay(20);
    if (!lu) // 30/10 : une lecture ratée = un essai raté (avant : « seuils relus » annoncé, la marge sautée sans le dire)
    {
        ld2410s.exitConfigMode();
        return false;
    }
    if (avec_marge && g_marge) // 16 portes, 10-95 dB (firmware 1.1.1)
    {
        // 30/10 : la PREUVE dans le journal : calculé par le capteur, relu après écriture, et l'écart porte par porte
        uint8_t calcule[RD_ZONES], voulu[RD_ZONES];
        memcpy(calcule, rd_etat.detection, RD_ZONES);
        for (int g = 0; g < RD_ZONES; g++)
            voulu[g] = LV_MIN(95, calcule[g] + g_marge);
        const bool ok = ld2410s.writeTriggers16(voulu);
        delay(20);
        const bool relu = ld2410s.readTriggers16(rd_etat.detection); // ce que le capteur a vraiment gardé
        delay(20);
        auto liste = [](const uint8_t *v, char *t, size_t n)
        {
            size_t k = 0;
            for (int g = 0; g < RD_ZONES && k < n; g++)
                k += snprintf(t + k, n - k, "%s%u", g ? " " : "", v[g]);
        };
        char a[64], b[64];
        int justes = 0;
        for (int g = 0; g < RD_ZONES; g++)
            justes += relu && rd_etat.detection[g] == voulu[g];
        liste(calcule, a, sizeof a);
        liste(rd_etat.detection, b, sizeof b);
        ui_trace("[radar] déclenchement calculé par le capteur : %s\n", a);
        ui_trace("[radar] déclenchement relu après la marge   : %s\n", b);
        ui_journal(justes != RD_ZONES, TXT("Réglage auto : marge +%u dB %s, relue juste sur %d portes sur 16 (capteur : %s ; relu : %s)",
                              "Auto-tune: margin +%u dB %s, exact on %d of 16 gates (sensor: %s; reread: %s)"),
                   (unsigned)g_marge, ok ? TXT("écrite", "written") : TXT("NON écrite", "NOT written"), justes, a, b);
    }
    mode_comm();
    delay(20);
    ld2410s.exitConfigMode();
    memoriser();
    return true;
}

// ---------------------------------------------------------------------------------------------------- page web
// la dernière trame décodée (n = numéro de trame : la page n'en rate ni n'en double aucune)
static String etat_json()
{
    const bool pc = ld2410s.hasEngineeringData(); // 26/10 : voir ui_radar_tache
    String j = "{\"n\":" + String(ld2410s.frameCount()) + ",\"presence\":" + String(ld2410s.isMotionDetected() ? "true" : "false") +
               ",\"distance\":" + String(ld2410s.getDistance()) + ",\"pcMode\":" + String(pc ? "true" : "false") + ",\"gateEnergy\":[";
    for (uint8_t g = 0; g < LD2410S::GATE_COUNT; g++)
        j += String(pc ? ld2410s.gateEnergy(g) : 0) + (g < LD2410S::GATE_COUNT - 1 ? "," : "");
    return j + "]}";
}

// 30/10 (retour d'usage : « une trame, un rafraîchissement ») : chaque trame est POUSSÉE à la page web dès sa réception, par un
// flux d'événements (text/event-stream) sur le port 81 ; la page ne demande plus rien (elle interrogeait /radar/status
// toutes les 700 ms : une trame sur trois)
#define FLUX_PORT 81
#define FLUX_CLIENTS 3
static WiFiServer g_flux_srv(FLUX_PORT);
static WiFiClient g_flux[FLUX_CLIENTS];

static void flux_accueillir()
{
    WiFiClient c = g_flux_srv.accept();
    if (!c)
        return;
    while (c.available()) // la requête du navigateur : rien à en tirer
        c.read();
    c.setNoDelay(true);
    c.print("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\nConnection: keep-alive\r\n\r\n");
    for (auto &f : g_flux)
        if (!f.connected())
        {
            f = c;
            ui_trace("[radar] page web branchée sur le flux des trames\n");
            return;
        }
    c.stop(); // déjà FLUX_CLIENTS pages ouvertes
}

static void flux_pousser()
{
    String m;
    for (auto &f : g_flux)
    {
        if (!f.connected())
            continue;
        if (!m.length())
            m = "data: " + etat_json() + "\n\n";
        f.print(m);
        g_web_activite = millis(); // une page ouverte garde l'écran éveillé, comme ses anciennes demandes
    }
}

static String tableau(const uint8_t *v)
{
    String s;
    for (int g = 0; g < RD_ZONES; g++)
        s += String(v[g]) + (g < RD_ZONES - 1 ? "," : "");
    return s;
}

static String seuils_json()
{
    return ",\"trig\":[" + tableau(rd_etat.detection) + "],\"hold\":[" + tableau(rd_etat.maintien) + "]"; // 16 portes chacun
}

static String json_echappe(const char *t)
{
    String o;
    for (; *t; t++)
        if (*t == '"')
            o += "\\\"";
        else if (*t == '\\')
            o += "\\\\";
        else if (*t == '\n')
            o += "\\n";
        else if ((uint8_t)*t >= 0x20)
            o += *t;
    return o;
}

static void web_brancher()
{
    auto racine = []()
    {
        g_web_activite = millis();
        g_web.send_P(200, "text/html", RADAR_HTML);
    };
    g_web.on("/", HTTP_GET, racine);
    g_web.on("/radar", HTTP_GET, racine);
    g_web.on("/radar/status", HTTP_GET, []()
             {
        g_web_activite = millis();
        g_web.send(200, "application/json", etat_json()); });
    g_web.on("/radar/config", HTTP_GET, []()
             {
        g_web_activite = millis();
        const bool ok = lire_config(); // 26/10 : « Lecture échouée » sur la page plutôt que les anciens seuils
        String j = "{\"ok\":" + String(ok ? "true" : "false") + ",\"maxGate\":" + String(g_far) + ",\"minGate\":" + String(g_near) + ",\"delay\":" + String(g_delai) +
                   ",\"statusFreq\":" + String(g_freq_etat / 10.0, 1) + ",\"distFreq\":" + String(g_freq_dist / 10.0, 1) +
                   ",\"speed\":" + String(g_vitesse) + seuils_json() + "}";
        g_web.send(200, "application/json", j); });
    g_web.on("/radar/apply", HTTP_POST, []()
             {
        g_web_activite = millis();
        for (uint8_t g = 0; g < RD_ZONES; g++)
        {
            rd_etat.detection[g] = g_web.arg("t" + String(g)).toInt();
            rd_etat.maintien[g] = g_web.arg("h" + String(g)).toInt();
        }
        const bool ok = appliquer(g_web.arg("mg").toInt(), g_web.arg("ng").toInt(), g_web.arg("delay").toInt(), g_web.arg("sf").toFloat(),
                                  g_web.arg("df").toFloat(), g_web.arg("speed").toInt());
        page_radar_maj();
        g_web.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}"); });
    g_web.on("/radar/autocal/start", HTTP_POST, []()
             {
        g_web_activite = millis();
        const uint16_t s = LV_CLAMP(10, (int)g_web.arg("scan").toInt(), 600);
        if (g_web.hasArg("marge"))
            marge_sauver(LV_CLAMP(0, (int)g_web.arg("marge").toInt(), 30));
        const bool ok = demarrer_auto(s);
        ui_trace("[radar] réglage auto (page web, %u s) %s\n", (unsigned)s, ok ? "lancé" : "refusé");
        g_web.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}"); });
    g_web.on("/radar/autocal/status", HTTP_GET, []()
             {
        g_web_activite = millis();
        const bool en_cours = ui_radar_auto_en_cours(); // jusqu'aux seuils relus (ui_radar_tache)
        g_web.send(200, "application/json", "{\"running\":" + String(en_cours ? "true" : "false") + ",\"progress\":" +
                                                String(ld2410s.autoThresholdProgress(), 1) + ",\"marge\":" + String(g_marge) + seuils_json() + "}"); });
    // 30/10 : la marge seule, sans lancer de réglage : /radar/marge?db=N (0-30)
    g_web.on("/radar/marge", HTTP_GET, []()
             {
        g_web_activite = millis();
        if (g_web.hasArg("db"))
            marge_sauver(LV_CLAMP(0, (int)g_web.arg("db").toInt(), 30));
        page_radar_maj();
        g_web.send(200, "application/json", "{\"marge\":" + String(g_marge) + "}"); });
    // 30/10 : seuils d'usine (relus sur ce capteur avant toute modification, radar.h de la production ; identiques à la
    // remise d'usine du composant ESPHome ld2410s de Mike Lawrence) ; paramètres communs gardés (portée, délai, fréquences
    // basse consommation, trames PC)
    g_web.on("/radar/usine", HTTP_POST, []()
             {
        g_web_activite = millis();
        // 30/10 : relus sur ce capteur avant toute modification (radar.h de la production, « issues de tes logs ») avec
        // l'ancien schéma = en réalité les 16 DÉCLENCHEMENTS (0x73) : 8 valeurs, puis 31 sur les portes 8-15. Le maintien
        // d'usine (0x77) n'a jamais été relu : il n'est pas touché.
        static const uint8_t USINE_D[RD_ZONES] = {48, 42, 36, 34, 32, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31};
        bool ok = !ld2410s.isAutoThresholdRunning() && ld2410s.enterConfigMode();
        if (ok)
        {
            ok = ld2410s.writeTriggers16(USINE_D);
            delay(20);
            ld2410s.exitConfigMode();
        }
        const bool relu = lire_config(); // ce que le capteur a vraiment gardé
        const bool juste = relu && !memcmp(rd_etat.detection, USINE_D, RD_ZONES);
        snprintf(rd_etat.message, sizeof rd_etat.message, juste ? TXT("déclenchement d'usine écrit", "factory trigger values written")
                                                       : TXT("usine : relecture différente", "factory: readback differs"));
        ui_trace("[radar] valeurs d'usine %s, relecture %s\n", ok ? "écrites" : "NON écrites", juste ? "identique" : "DIFFÉRENTE");
        page_radar_maj();
        g_web.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + ",\"identique\":" + (juste ? "true" : "false") + "}"); });
    g_web.on("/radar/journal", HTTP_GET, []()
             {
        g_web_activite = millis();
        static char traces[3200];
        ui_diag_traces(traces, sizeof traces);
        String resume; // les réveils des 24 h
        static const char *const CAUSES[] = {"radar", "toucher", "minuterie", "démarrage"};
        for (int i = 0; i < bt_etat.nb_eveils; i++)
        {
            const bt_eveil_t &e = bt_etat.eveils[i];
            const time_t t = e.debut;
            struct tm lt;
            localtime_r(&t, &lt);
            char l[64];
            snprintf(l, sizeof l, "%02d/%02d %02d:%02d  %-9s  allumé %u s\n", lt.tm_mday, lt.tm_mon + 1, lt.tm_hour, lt.tm_min,
                     CAUSES[e.cause % 4], (unsigned)e.duree);
            resume += l;
        }
        g_web.send(200, "application/json", "{\"complet\":\"" + json_echappe(traces) + "\",\"resume\":\"" + json_echappe(resume.c_str()) + "\"}"); });
    g_web.on("/radar/journal/clear", HTTP_POST, []()
             {
        g_web_activite = millis();
        const bool ok = g_web.arg("which") == "complet";
        if (ok)
            ui_diag_traces_effacer();
        g_web.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}"); });
    g_web.begin();
}

// ---------------------------------------------------------------------------------------------------- écran
static void cmd_lire()
{
    snprintf(rd_etat.message, sizeof rd_etat.message, lire_config() ? TXT("seuils relus du radar", "thresholds re-read") : TXT("le radar ne répond pas", "radar not responding"));
    page_radar_maj();
}

static void cmd_envoyer()
{
    // les paramètres communs relus du capteur (sinon ceux de la production : 8 zones, 0, 10 s, 0,5 Hz, 0,5 Hz, rapide)
    const bool ok = appliquer(g_far, g_near, g_delai, g_freq_etat / 10.0f, g_freq_dist / 10.0f, (uint8_t)g_vitesse);
    snprintf(rd_etat.message, sizeof rd_etat.message, ok ? TXT("seuils envoyés au radar", "thresholds sent") : TXT("échec de l'envoi", "send failed"));
    ui_trace("[radar] seuils %s\n", ok ? "envoyés" : "NON envoyés");
    page_radar_maj();
}

// 28/10 : lancé de l'écran, le réglage partait au toucher, avec la personne encore devant : 30 s pour sortir d'abord
#define SORTIE_S 30
#define FIN_AUTO_ATTENTE_MS 5000 // 30/10 : silence du capteur après un réglage auto (voir ui_radar_tache)
static uint32_t g_depart_ms = 0; // 0 = aucun départ en attente
static uint16_t g_depart_scan = 0;

static void cmd_auto(uint16_t secondes)
{
    if (!secondes)
    {
        g_depart_ms = 0;
        rd_etat.depart_s = 0;
        snprintf(rd_etat.message, sizeof rd_etat.message, TXT("réglage auto annulé", "auto-tune cancelled"));
    }
    else
    {
        g_depart_ms = (millis() + SORTIE_S * 1000) | 1;
        g_depart_scan = secondes;
        rd_etat.depart_s = SORTIE_S;
        snprintf(rd_etat.message, sizeof rd_etat.message, TXT("sortez : départ dans %d s", "leave: starting in %d s"), SORTIE_S);
    }
    page_radar_maj();
}

static void depart_tache()
{
    if (!g_depart_ms)
        return;
    const int32_t reste = (int32_t)(g_depart_ms - millis());
    if (reste <= 0)
    {
        g_depart_ms = 0;
        rd_etat.depart_s = 0;
        const bool ok = demarrer_auto(g_depart_scan);
        snprintf(rd_etat.message, sizeof rd_etat.message, ok ? TXT("réglage auto lancé (%u s)", "auto-tune started (%u s)") : TXT("réglage auto refusé", "auto-tune refused"), (unsigned)g_depart_scan);
        ui_trace("[radar] réglage auto %s\n", ok ? "lancé" : "refusé");
        page_radar_maj();
        return;
    }
    const uint8_t s = (reste + 999) / 1000;
    if (s != rd_etat.depart_s)
    {
        rd_etat.depart_s = s;
        snprintf(rd_etat.message, sizeof rd_etat.message, TXT("sortez : départ dans %u s", "leave: starting in %u s"), (unsigned)s);
        page_radar_maj();
    }
}

// 26/10 : diag radar (interrupteur de la page « Batterie et capteur ») : l'écran reste éveillé (rétroéclairage éteint
// au délai de veille) et chaque détection part dans le journal de bord de HA (heure, zones au-dessus du seuil, pic en dB,
// distance, durée) : de quoi trouver ce qui réveille l'écran sur batterie, où l'ESP dort quand le capteur décide
// 28/10 : ce qui est arrivé pendant la dernière entrée en configuration (voir LD2410S::dbgEntree)
static String octets_recus()
{
    char t[3 * 24 + 1] = "";
    for (uint8_t i = 0; i < ld2410s.dbgEntree.nb && i < 24; i++)
        snprintf(t + 3 * i, 4, "%02X ", ld2410s.dbgEntree.recu[i]);
    return String(ld2410s.dbgEntree.avant) + " octets en attente, " + ld2410s.dbgEntree.nb + " reçus : " + t;
}

static bool passer_pc()
{
    if (!ld2410s.enterConfigMode())
    {
        ui_trace("[radar] entrée en configuration ratée : %s\n", octets_recus().c_str());
        return false;
    }
    delay(20);
    const bool pc = ld2410s.setCommMode(LD2410SCommMode::PC);
    delay(20);
    ld2410s.exitConfigMode();
    return pc;
}

static void cmd_diag(bool actif)
{
    if (actif == rd_etat.diag)
        return;
    rd_etat.diag = actif;
    if (actif)
    {
        g_diag_debut_ms = millis();
        rd_etat.diag_nb = 0;
        const uint32_t t = (uint32_t)time(nullptr);
        rd_etat.diag_fin = t > 1700000000u ? t + DIAG_MS / 1000 : 0;
        if (!g_config_lue)
            lire_config();
        const bool deja = ld2410s.hasEngineeringData();
        const bool pc = deja || passer_pc();
        g_diag_essais = pc ? 0 : 1;
        g_diag_essai_ms = millis();
        if (pc)
            ui_journal(false, TXT("Diag radar lancé (12 h au plus) · énergies %s", "Radar diag started (12 h max) · energies %s"),
                       deja ? TXT("reçues", "received") : TXT("demandées au capteur (essai 1)", "requested from sensor (try 1)"));
        else
            ui_journal(false, TXT("Diag radar lancé (12 h au plus) · capteur muet à l'essai 1 (%s) : nouvel essai dans 2 s",
                       "Radar diag started (12 h max) · sensor silent on try 1 (%s): retry in 2 s"),
                       octets_recus().c_str());
    }
    else
        ui_journal(false, TXT("Diag radar arrêté : %u détection%s", "Radar diag stopped: %u detection%s"), rd_etat.diag_nb, rd_etat.diag_nb > 1 ? "s" : "");
    page_batterie_maj();
}

// une détection (présence du capteur, de son début à sa fin, délai de maintien compris) : une ligne au plus toutes les 10 s
static void diag_tache()
{
    static bool avant = false;
    static uint32_t debut = 0, derniere_ligne = 0;
    static uint16_t distance = 0, en_attente = 0;
    static uint8_t pic[RD_ZONES];
    if (!rd_etat.diag)
    {
        avant = false;
        return;
    }
    if (millis() - g_diag_debut_ms >= DIAG_MS)
    {
        cmd_diag(false);
        return;
    }
    if (g_diag_essais && millis() - g_diag_essai_ms >= 2000) // le filet du passage en trames PC
    {
        g_diag_essai_ms = millis();
        g_diag_essais++;
        if (ld2410s.hasEngineeringData() || passer_pc())
        {
            ui_journal(false, TXT("Diag radar : énergies demandées au capteur (essai %u)", "Radar diag: energies requested from sensor (try %u)"), g_diag_essais);
            g_diag_essais = 0;
        }
        else if (g_diag_essais >= 5)
        {
            ui_journal(true, TXT("Diag radar : capteur muet après 5 essais, pas d'énergies (%s)", "Radar diag: sensor silent after 5 tries, no energies (%s)"), octets_recus().c_str());
            g_diag_essais = 0;
        }
    }
    const bool presence = ld2410s.isMotionDetected();
    if (presence && !avant) // début : l'écran se rallume, comme un réveil sur batterie
    {
        debut = millis();
        distance = 0;
        memset(pic, 0, sizeof pic);
        lv_disp_trig_activity(NULL);
    }
    if (presence)
    {
        if (!distance)
            distance = ld2410s.getDistance();
        if (ld2410s.hasEngineeringData())
            for (uint8_t g = 0; g < RD_ZONES; g++)
                pic[g] = LV_MAX(pic[g], ld2410s.gateEnergy(g));
    }
    if (!presence && avant) // fin : la ligne
    {
        rd_etat.diag_nb++;
        page_batterie_maj();
        if (derniere_ligne && millis() - derniere_ligne < 10000)
            en_attente++;
        else
        {
            derniere_ligne = millis();
            // zones au-dessus de leur seuil de détection ; la plus forte (marge) donne le pic affiché
            char zones[40] = "";
            int forte = -1, marge = -999;
            for (int g = 0; g < RD_ZONES; g++)
            {
                const int m = (int)pic[g] - rd_etat.detection[g];
                if (pic[g] && m > marge)
                    marge = m, forte = g;
                if (pic[g] && m > 0)
                    snprintf(zones + strlen(zones), sizeof zones - strlen(zones), "%s%d", zones[0] ? "," : "", g);
            }
            const unsigned s = (millis() - debut) / 1000;
            char suite[16] = "";
            if (en_attente)
                snprintf(suite, sizeof suite, " · +%u", en_attente);
            en_attente = 0;
            if (forte < 0)
                ui_journal(false, TXT("Détection, zone inconnue%s · %u" SEP_DEC "%02u m · %u s%s", "Detection, unknown zone%s · %u" SEP_DEC "%02u m · %u s%s"), ld2410s.hasEngineeringData() ? "" : TXT(" (aucune énergie reçue)", " (no energy received)"),
                           distance / 100, distance % 100, s, suite);
            else
                ui_journal(false, TXT("Détection zone%s %s : %u dB (seuil %u) · %u" SEP_DEC "%02u m · %u s%s", "Detection zone%s %s: %u dB (threshold %u) · %u" SEP_DEC "%02u m · %u s%s"), strchr(zones, ',') ? "s" : "",
                           zones[0] ? zones : "-", pic[forte], rd_etat.detection[forte], distance / 100, distance % 100, s, suite);
        }
    }
    avant = presence;
}

static void commande_notee(uint16_t cmd, bool ok); // plus bas, avec le battement

void ui_radar_demarrer(bool froid)
{
    g_actif = true;
    memcpy(rd_etat.detection, DETECTION, RD_ZONES);
    memcpy(rd_etat.maintien, MAINTIEN, RD_ZONES);
    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); // comme la production (radar.begin)
    rd_lire = cmd_lire;
    rd_envoyer = cmd_envoyer;
    rd_auto = cmd_auto;
    rd_diag = cmd_diag;
    rd_marge = marge_sauver; // 31/10 : onglet Réglages de la page Batterie
    marge_charger();
    ld2410s.surEchange = echange_brut;
    ld2410s.surCommande = commande_notee;
    if (froid) // la production relit la configuration au démarrage à froid seulement (pas au réveil : 1,39 s)
        lire_config();
    else if (retrouver()) // 31/10 : réveil : celle gardée en mémoire RTC
        ui_trace("[radar] configuration retrouvée en mémoire RTC (portée %u-%u, délai %u s)\n", (unsigned)g_near, (unsigned)g_far,
                 (unsigned)g_delai);
}

void ui_radar_web_demarrer()
{
    if (!g_actif || g_web_pret)
        return;
    web_brancher();
    g_flux_srv.begin();
    g_flux_srv.setNoDelay(true);
    g_web_pret = true;
}

// Battement du capteur, filet de sécurité (après un réveil le radar se taisait) : une ligne au journal quand
// il se tait (6 s sans trame) et quand il reparle ; relance en trois temps (sortie de configuration, puis
// mode PC, puis relecture complète).
#define CMD_RTC_MAGIC 0x434D4431u // « CMD1 »
struct cmd_rtc_t
{
    uint32_t magic;
    uint16_t cmd;
    bool ok, ouverte; // ouverte : entrée tentée, pas de sortie acquittée depuis
    uint64_t t_us;
};
RTC_NOINIT_ATTR static cmd_rtc_t g_cmd_rtc;
static void commande_notee(uint16_t cmd, bool ok)
{
    if (g_cmd_rtc.magic != CMD_RTC_MAGIC)
        g_cmd_rtc = {CMD_RTC_MAGIC, 0, false, false, 0};
    g_cmd_rtc.cmd = cmd;
    g_cmd_rtc.ok = ok;
    g_cmd_rtc.t_us = esp_rtc_get_time_us();
    if (cmd == 0x00FF)
        g_cmd_rtc.ouverte = true;
    else if (cmd == 0x00FE && ok)
        g_cmd_rtc.ouverte = false;
    if (!ok)
        ui_trace("[radar] commande %04X sans réponse\n", cmd);
}

#define MUET_MS 6000
#define RELANCE_MS 3000
static uint32_t g_premiere_ms = 0; // première trame depuis le démarrage (0 = aucune)
static uint32_t g_muets = 0, g_relances = 0;
static const char *g_relance_par = "-";
static void battement()
{
    static uint32_t vue = 0, t_trame = 0, octets = 0, t_etape = 0;
    static uint8_t etape = 0; // 0 : parle ; 1 : muet, rien tenté ; 2 : sortie tentée ; 3 : mode PC ; 4 : relecture ; 5 : échec
    static bool ack_sortie = false;
    const uint32_t t = millis();
    if (ld2410s.frameCount() != vue || ld2410s.isAutoThresholdRunning() || g_auto_a_finir)
    {
        vue = ld2410s.frameCount();
        if (!g_premiere_ms && vue)
        {
            g_premiere_ms = t | 1;
            ui_trace("[radar] première trame à %lu ms\n", (unsigned long)t);
        }
        if (etape)
        {
            g_relances += etape >= 2 && etape <= 4;
            g_relance_par = etape == 2 ? "sortie" : etape == 3 ? "mode" : etape == 4 ? "relecture" : "seul";
            ui_journal(false, TXT("Radar : reparle après %lu s de silence, %s", "Radar: talking again after %lu s of silence, %s"),
                       (unsigned long)((t - t_trame) / 1000),
                       etape == 2   ? (ack_sortie ? TXT("relancé par la SORTIE de configuration (acquittée)", "woken by leaving CONFIG (acknowledged)")
                                                  : TXT("relancé par la sortie de configuration (non acquittée)", "woken by leaving config (not acknowledged)"))
                       : etape == 3 ? TXT("relancé par le MODE PC remis (la sortie seule n'a pas suffi)", "woken by re-entering PC MODE (leaving config was not enough)")
                       : etape == 4 ? TXT("relancé par la RELECTURE (ni la sortie ni le mode PC n'ont suffi)", "woken by the REREAD (neither config exit nor PC mode sufficed)")
                                    : TXT("tout seul", "on its own"));
        }
        etape = 0;
        t_trame = t;
        octets = ld2410s.byteCount();
        return;
    }
    if (!etape)
    {
        if (t - t_trame < MUET_MS)
            return;
        etape = 1;
        g_muets++;
        char premiere[40];
        if (g_premiere_ms)
            snprintf(premiere, sizeof premiere, TXT("1re trame à %lu ms", "1st frame at %lu ms"), (unsigned long)g_premiere_ms);
        else
            snprintf(premiere, sizeof premiere, TXT("aucune trame", "no frame"));
        char cmd[96] = TXT("aucune commande notée", "no command noted");
        if (g_cmd_rtc.magic == CMD_RTC_MAGIC)
            snprintf(cmd, sizeof cmd, TXT("dernière cmd %04X %s il y a %lu s, config %s", "last cmd %04X %s %lu s ago, config %s"),
                     g_cmd_rtc.cmd, g_cmd_rtc.ok ? "ok" : TXT("SANS RÉPONSE", "NO REPLY"),
                     (unsigned long)((esp_rtc_get_time_us() - g_cmd_rtc.t_us) / 1000000),
                     g_cmd_rtc.ouverte ? TXT("LAISSÉE OUVERTE", "LEFT OPEN") : TXT("refermée", "closed"));
        ui_journal(true, TXT("Radar muet %lu s : démarré il y a %lu s (%s), écran actif il y a %lu s, %lu o sans trame, %u MHz ; %s",
                      "Radar silent %lu s: up %lu s (%s), screen active %lu s, %lu bytes with no frame, %u MHz; %s"),
                   (unsigned long)((t - t_trame) / 1000), (unsigned long)(t / 1000), premiere,
                   (unsigned long)(lv_disp_get_inactive_time(NULL) / 1000), (unsigned long)(ld2410s.byteCount() - octets),
                   (unsigned)getCpuFrequencyMhz(), cmd);
    }
    if (etape == 1)
    {
        ack_sortie = ld2410s.exitConfigMode();
        ui_trace("[radar] muet : sortie de configuration %s\n", ack_sortie ? "acquittée" : "sans réponse");
        etape = 2;
        t_etape = millis();
    }
    else if (etape == 2 && millis() - t_etape >= RELANCE_MS)
    {
        const bool ok = passer_pc();
        ui_trace("[radar] muet : mode PC %s\n", ok ? "remis" : "NON remis");
        etape = 3;
        t_etape = millis();
    }
    else if (etape == 3 && millis() - t_etape >= RELANCE_MS)
    {
        const bool ok = lire_config();
        ui_trace("[radar] muet : relecture %s\n", ok ? "faite" : "ratée");
        etape = 4;
        t_etape = millis();
    }
    else if (etape == 4 && millis() - t_etape >= RELANCE_MS)
    {
        ui_journal(true, TXT("Radar toujours muet après sortie (%s), mode PC et relecture : nouvel essai dans 60 s",
                      "Radar still silent after exit (%s), PC mode and reread: retry in 60 s"),
               ack_sortie ? TXT("acquittée", "acknowledged") : TXT("sans réponse", "no reply"));
        g_relance_par = "echec";
        etape = 5;
        t_etape = millis();
    }
    else if (etape == 5 && millis() - t_etape >= 60000)
        etape = 1;
}

void ui_radar_compteurs(uint32_t *trames, uint32_t *octets, uint32_t *muets, uint32_t *relances, const char **par)
{
    *trames = ld2410s.frameCount();
    *octets = ld2410s.byteCount();
    *muets = g_muets;
    *relances = g_relances;
    *par = g_relance_par;
}

void ui_radar_tache()
{
    if (!g_actif)
        return;
    ld2410s.loop();
    battement();
    if (g_web_pret)
        g_web.handleClient();
    rd_etat.present = true;
    const bool mouvement = ld2410s.isMotionDetected();
    const uint16_t distance = ld2410s.getDistance();
    // 26/10 : les trames « PC » reçues, pas le mode noté par la bibliothèque (réglé seulement en relisant la configuration,
    // donc faux après un démarrage court : le capteur, lui, garde son mode)
    rd_etat.pc = ld2410s.hasEngineeringData();
    // 13/11 (retour d'usage, « chien de garde ») : le séjour ne dormait plus depuis 4h49 (présence permanente à 7,1 m, broche
    // haute) ; une relecture de la configuration (/radar/config) l'a débloqué. Présence ininterrompue depuis 1 h ET aucun
    // toucher depuis 1 h -> relecture (bloque la boucle ~1,6 s ; personne ne touche l'écran), au plus une par heure ;
    // une ligne au journal de HA avant, une 15 s après (présence encore là = vraie présence, partie = blocage).
    {
        const uint32_t CHIEN_MS = 3600000UL;
        static uint32_t presence_depuis = 0, derniere_relance = 0, verif_ms = 0;
        const uint32_t t = millis();
        if (!mouvement)
            presence_depuis = 0;
        else if (!presence_depuis)
            presence_depuis = t | 1;
        if (verif_ms && t - verif_ms >= 15000)
        {
            verif_ms = 0;
            if (mouvement)
                ui_journal(false, TXT("Radar relu : présence toujours là (%u cm)", "Radar re-read: presence still there (%u cm)"), (unsigned)distance);
            else
                ui_journal(true, TXT("Radar relu : plus de présence (il était bloqué)", "Radar re-read: no presence anymore (it was stuck)"));
        }
        if (presence_depuis && t - presence_depuis >= CHIEN_MS && lv_disp_get_inactive_time(NULL) >= CHIEN_MS &&
            (!derniere_relance || t - derniere_relance >= CHIEN_MS) && !ld2410s.isAutoThresholdRunning() && !g_auto_a_finir &&
            !rd_etat.diag)
        {
            derniere_relance = t | 1;
            ui_journal(true, TXT("Radar : présence ininterrompue depuis %lu min sans toucher (%u cm) : relecture",
                          "Radar: uninterrupted presence for %lu min without touch (%u cm): re-read"),
                       (unsigned long)((t - presence_depuis) / 60000), (unsigned)distance);
            lire_config();
            verif_ms = millis() | 1;
        }
    }
    rd_etat.auto_en_cours = ld2410s.isAutoThresholdRunning() || g_auto_a_finir; // pas le compte à rebours : la page montre la présence
    rd_etat.progression = ld2410s.autoThresholdProgress();
    depart_tache();
    // 30/10 : progression figée (51 % pendant des minutes, le capteur renvoyait déjà ses trames normales, seuils inchangés) :
    // l'écran ne dormait plus ; au bout de 30 s sans progrès, le réglage est tenu pour interrompu et les seuils relus
    static float progres_vu = -1;
    static uint32_t progres_ms = 0;
    if (!ld2410s.isAutoThresholdRunning())
        progres_vu = -1;
    else if (rd_etat.progression != progres_vu)
    {
        progres_vu = rd_etat.progression;
        progres_ms = millis();
    }
    else if (millis() - progres_ms >= 30000)
    {
        ui_trace("[radar] réglage auto figé à %d %% depuis 30 s : interrompu\n", (int)progres_vu);
        ld2410s.abandonAutoThreshold();
        g_auto_interrompu = (uint8_t)progres_vu;
    }
    // 26/10 : fin du réglage auto (lancé de l'écran ou de la page web) : juste après, le capteur peut ne pas répondre
    // (vu sur le séjour) ; sans nouvel essai, il restait sans trames d'énergie et la page montrait les anciens seuils
    // 30/10 : MESURÉ 2 fois sur 2 : muet à +0,3 s et +2,3 s, répond à +4,3 s -> on attend 5 s sans rien lui envoyer
    // (retour d'usage : « pourquoi faire un essai si tu sais que ça va échouer »), les essais suivants restent en secours
    if (g_auto_a_finir && !ld2410s.isAutoThresholdRunning())
    {
        static uint32_t essai = 0, fin = 0;
        static uint8_t essais = 0;
        if (!fin)
            fin = millis();
        if (millis() - fin >= FIN_AUTO_ATTENTE_MS && (!essais || millis() - essai >= 2000))
        {
            essai = millis();
            const bool ok = finir_auto(!g_auto_interrompu); // interrompu : les seuils sont les anciens, pas de marge en plus
            if (ok || ++essais >= 5)
            {
                ui_trace("[radar] réglage auto fini : %s (essai %u, %lu ms après la fin)\n", ok ? "seuils relus" : "le radar ne répond pas",
                         (unsigned)(essais + ok), (unsigned long)(essai - fin));
                if (!ok)
                    ui_journal(true, TXT("Réglage auto : seuils non relus après 5 essais, marge NON posée", "Auto-tune: thresholds not reread after 5 tries, margin NOT set"));
                if (ok && g_auto_interrompu)
                    snprintf(rd_etat.message, sizeof rd_etat.message, TXT("réglage auto interrompu à %u %%", "auto-tune interrupted at %u %%"), (unsigned)g_auto_interrompu);
                else
                    snprintf(rd_etat.message, sizeof rd_etat.message, ok ? TXT("réglage auto terminé", "auto-tune done") : TXT("réglage auto : le radar ne répond pas", "auto-tune: radar not responding"));
                g_auto_a_finir = false;
                g_auto_interrompu = 0;
                essais = 0;
                fin = 0;
            }
            else
                ui_trace("[radar] réglage auto fini, radar muet : essai %u/5\n", (unsigned)essais);
        }
    }
    diag_tache();
    if (g_web_pret)
        flux_accueillir();
    // 30/10 (retour d'usage : « une trame, un rafraîchissement ») : l'écran et la page web à CHAQUE trame reçue, ni plus ni moins
    // (l'écran était limité à une mise à jour toutes les 300 ms : avec des trames toutes les 260 ms, il en sautait)
    static uint32_t vue = 0;
    if (ld2410s.frameCount() == vue)
        return;
    vue = ld2410s.frameCount();
    rd_etat.presence = mouvement;
    rd_etat.distance = distance;
    for (uint8_t g = 0; g < RD_ZONES; g++)
        rd_etat.energie[g] = rd_etat.pc ? ld2410s.gateEnergy(g) : 0;
    page_radar_maj();
    flux_pousser();
}

bool ui_radar_mouvement() { return g_actif && ld2410s.isMotionDetected(); }
// 31/10 : la page branchée sur le flux des trames ne demande plus rien : elle compte comme ouverte tant qu'elle est branchée
bool ui_radar_web_actif()
{
    for (auto &f : g_flux)
        if (f.connected())
            return true;
    return g_web_activite && millis() - g_web_activite < WEB_ACTIVITE_MS;
}
bool ui_radar_auto_en_cours() { return g_actif && (ld2410s.isAutoThresholdRunning() || g_auto_a_finir || g_depart_ms); }
bool ui_radar_diag_actif() { return g_actif && rd_etat.diag; }
#endif
