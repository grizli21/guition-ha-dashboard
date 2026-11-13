// Firmware de l'écran : interface toute LVGL (src/ui_lvgl/). Envs : « ecran » (production, OTA),
// « ecran-usb » (câble), « banc-chambre1-v9 » (mesure). Wi-Fi et OTA partout : reflash sans câble.

#include <Arduino.h>
#include <algorithm>
#include "../ui_lvgl/ui_diag.h"
#include "../ui_lvgl/ui_psram.h"
#include "../ui_lvgl/ui_langue.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <esp_rtc_time.h>
#include <driver/gpio.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <LGFX_ESP32S3_RGB_TFT_SPI_ST7701_GT911.h>
#include <lvgl.h>
#include "icones.h"
#include "icones_ventilo.h"
#include "icones_pc.h"
#include "icones_ia.h" // 11/11/2026 : icône de la page « IA » dans le rail (variante 1 de la planche du 11/11)
#include "../ui_lvgl/ui_theme.h"
#include "../ui_lvgl/ui_anneau.h"
#include "../ui_lvgl/ui_courbe.h" // 05/11 (vague D) : wrappers uc_* pour le banc de l'anneau sous 9.6
#include "../ui_lvgl/ui_ecran.h" // 05/11 : adaptateur du « scroll par bande » (patch lv_obj_scroll.c)
#include <src/draw/sw/lv_draw_sw.h>
#include "../ui_lvgl/ui_shell.h"
#include "../ui_lvgl/ui_demo.h" // MODE_DEMO : mode démo embarqué (fichier vide sans le drapeau)
#include "../ui_lvgl/page_thermostat.h"
#include "../ui_lvgl/page_graphe.h"
#include "../ui_lvgl/page_air.h"
#include "../ui_lvgl/page_ouvrants.h"
#include "../ui_lvgl/page_croquettes.h"
#include "../ui_lvgl/page_cafe.h"
#include "../ui_lvgl/page_ballon.h"
#include "../ui_lvgl/page_batterie.h"
#include "../ui_lvgl/ui_batterie.h"
#include "../ui_lvgl/ui_radar.h"
#include "../ui_lvgl/ui_camera.h"
#include "../ui_lvgl/page_vmc.h"
#include "../ui_lvgl/page_poele.h"
#include "../ui_lvgl/page_musique.h"
#include "../ui_lvgl/page_lumieres.h"
#include "../ui_lvgl/page_panneau_led.h" // 02/11/2026 : panneau de luminothérapie de l'atelier
#include "../ui_lvgl/page_imprimante.h"
#include "../ui_lvgl/page_pc.h"
#include "../ui_lvgl/page_ia.h" // 11/11/2026 : page « IA » séparée, après PC dans le rail
#include "../ui_lvgl/page_reglages.h"
#include "../ui_lvgl/page_infos.h"
#include "../ui_lvgl/ui_mqtt.h"
#include "../ui_lvgl/ui_musique.h"
#include "../ui_lvgl/ui_demarrage.h"

// 26/10 (retour d'usage : « les IP fixes non plus en fonction de la pièce mais de la MAC de l'ESP ») : UN SEUL firmware pour tous
// les écrans ; l'écran se reconnaît à sa MAC (Wi-Fi) et en tire son IP fixe, sa pièce et son sens de montage. Écran
// inconnu : DHCP, pièce « inconnu », sa MAC dans les traces ([ecran]) pour l'ajouter ici.
struct ecran_t
{
    uint8_t mac[6];
    uint8_t ip;        // dernier octet de l'IP statique (préfixe RESEAU_*, voir secrets_wifi.h)
    const char *piece; // nom MQTT de la pièce (contrat dashboard_guition)
    uint8_t rotation;  // 1 = tourné de 90° (setRotation de Lovyan)
    bool batterie;     // 26/10 : écran sur batterie (radar, veille profonde, page « Batterie et capteur »)
};
// EXEMPLE à adapter : relever la MAC (Wi-Fi) de chaque écran au premier démarrage — la trace [ecran] l'affiche —
// puis remplir la table. Un écran absent de la table démarre en DHCP, pièce « inconnu ».
static const ecran_t ECRANS[] = {
    {{0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x01}, 10, "cuisine", 0, false},
    {{0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x02}, 11, "sejour", 0, true}, // modèle batterie (radar LD2410S)
    {{0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x03}, 12, "chambre1", 0, false},
    {{0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x04}, 13, "chambre2", 0, false},
    // Toute la production en rotation 0. La rotation de cette table est le SEUL choix : le forçage de compilation
    // -DECRAN_ROT0 n'est plus nécessaire et reste seulement dans l'env de mesure (banc-chambre1-v9).
    // Conséquence à l'exécution : deux mémoires d'image en PSRAM et mode direct, plus le chemin rapide
    // du défilement par bande.
};
static const ecran_t *g_ecran = nullptr;

static void ecran_reconnaitre()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    for (const ecran_t &e : ECRANS)
        if (!memcmp(e.mac, mac, 6))
            g_ecran = &e;
    ui_trace("[ecran] MAC %02x:%02x:%02x:%02x:%02x:%02x : %s\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             g_ecran ? g_ecran->piece : "INCONNU (DHCP, à ajouter dans ECRANS[])");
}

static const char *piece() { return g_ecran ? g_ecran->piece : "inconnu"; }
#include "../ui_lvgl/ui_appareil.h"
#include "banc_distant.h"


#include "secrets_wifi.h" // hors git : Wi-Fi maison

#define BACKLIGHT_PIN 38

LGFX tft;
#if LVGL_VERSION_MAJOR >= 9
static lv_display_t *g_disp = nullptr; // l'affichage unique (480 x 480)
static lv_draw_buf_t g_fb_buf[2];      // les deux mémoires d'image vues par LVGL (même indice que la mémoire Lovyan)
static uint32_t g_rendu_px = 0;        // pixels dessinés dans l'image en cours (le « px » du monitor_cb 8.4)
static uint32_t g_rendu_debut = 0;     // horodatage de LV_EVENT_REFR_START (le « ms » du monitor_cb 8.4)
#else
static lv_disp_draw_buf_t draw_buf;
#endif

// --- Pilotes écran / tactile (même principe que la production) ---
// 25/10 : temps passé dans la copie vers la mémoire d'image pendant l'image en cours (le reste = rendu de LVGL)
static uint32_t g_copie_us = 0;
// Rotation choisie au démarrage (table ECRANS[]) : 0 -> DEUX mémoires d'image, LVGL dessine dans celle qui
// n'est pas montrée et la dalle bascule à la fin de la trame (VSYNC), sans copie ; 90 -> mode normal,
// Lovyan tourne pendant la copie (mesures du 29/10).
#include <esp_cache.h>
static bool g_double = false;                         // 27/10 : deux mémoires d'image
static uint8_t **g_lignes_fb[2] = {nullptr, nullptr}; // leurs lignes (la caméra écrit dans celle qui est montrée)
static uint32_t g_bascule_us = 0;                     // 27/10 : synchronisation du cache + attente du VSYNC (trace [rendu])
// 27/10 : la caméra écrit aussi dans la mémoire CACHÉE et demande la bascule à la fin de chaque image (elle écrivait dans
// la mémoire montrée : déchirures constatées) ; pendant ce temps, LVGL ne bascule plus seul : ses changements
// partent avec l'image de caméra suivante (au plus 150 ms d'attente si la caméra cale)
static volatile int8_t g_cam_cible = -1, g_cam_prete = -1; // mémoire où la caméra écrit / vient de finir une image
static volatile int8_t g_lvgl_prete = -1;                  // mémoire où LVGL a fini de dessiner, en attente de la caméra
static uint32_t g_lvgl_prete_ms = 0;
#define LVGL_ATTENTE_CAMERA_MS 150
// 27/10 : LVGL ne recopie les zones d'une image dans l'autre mémoire qu'au DÉBUT du dessin suivant (refr_sync_areas), et
// son minuteur de rafraîchissement s'endort quand rien ne change : rail arrêté, la mémoire cachée gardait le rail un pas
// en arrière ; la caméra, qui alterne les deux mémoires, le faisait trembler dès sa reprise (retour d'usage). On relance donc le
// minuteur après chaque image : un passage sans dessin qui ne fait que cette recopie. Débrayable au banc (/sync?v=0)
bool g_sync_aussitot = true;
// 27/10 : tenu par la boucle pendant LVGL (dessin) et les bascules ; la caméra le prend SANS attendre à la fin de son image
// pour basculer tout de suite (par la boucle seule, la bascule attendait la fin d'un dessin de LVGL : 54 -> 70 ms par image)
static SemaphoreHandle_t g_verrou_ecran = nullptr;
// bascule sur la mémoire h où la caméra vient de finir son image ; LVGL dessinera dans l'autre (verrou tenu)
// 27/10 : quelle image de caméra chaque mémoire contient (numéro croissant) : pendant une pause de la caméra (rail en
// défilement), la mémoire où LVGL va dessiner reçoit la zone de la caméra de la mémoire montrée si elle a une image plus
// ancienne (sinon, à chaque bascule de LVGL, l'image alternait entre les deux dernières : elle tremblait)
static uint32_t g_cam_num = 0, g_cam_num_fb[2] = {0, 0};
static void montrer_image_camera(int h)
{
    g_cam_num_fb[h] = ++g_cam_num;
    tft._bus_instance.showFrameBuffer(h);
#if LVGL_VERSION_MAJOR >= 9
    // 9.6 : aucun setter public du tampon actif (lv_display_get_buf_active est une simple lecture,
    // display/lv_display.h:742). lv_display_set_draw_buffers(d, b1, b2) pose buf_1 = b1, buf_2 = b2 et
    // buf_act = buf_1 : passer la mémoire cachée en premier fait donc exactement le coup de
    // « buf_act = getFrameBuffer(1 - h) » de la 8.4.
    lv_display_set_draw_buffers(g_disp, &g_fb_buf[1 - h], &g_fb_buf[h]);
#else
    lv_disp_get_default()->driver->draw_buf->buf_act = tft._bus_instance.getFrameBuffer(1 - h);
#endif
    g_lvgl_prete = -1;
    g_cam_prete = -1;
}
struct acces_fb : lgfx::Panel_FrameBufferBase
{
    static uint8_t **lignes(lgfx::Panel_FrameBufferBase *p) { return p->*(&acces_fb::_lines_buffer); }
    static int rotation(lgfx::Panel_FrameBufferBase *p) { return p->*(&acces_fb::_internal_rotation); }
};
static lv_area_t g_rendu = {480, 480, -1, -1}; // 26/10 : rectangle englobant de l'image (trace [rendu] : qui redessine)
static lv_area_t g_rendu_prec = {480, 480, -1, -1}; // 04/11 : englobant de l'image precedente (zones recopiees par refr_sync_areas)
static uint32_t g_msync_us = 0; // 04/11 : temps du seul vidage du cache dans l'image en cours (banc BILAN_RENDU_S)
static uint32_t g_banc_px = 0;            // 04/11 : pixels de la derniere image (banc du glisse)
static uint32_t g_interface_prete_ms = 0; // 04/11 : fin de l'ecran de demarrage (le banc du glisse attend 5 s de plus)
// 25/10 : RAM interne libre, son plus bas niveau depuis le démarrage et le plus gros bloc libre (Arduino 3 : la pile
// réseau de l'IDF 5 en prend plus)
static size_t g_ram_plus_bas_signale = SIZE_MAX;
static void trace_ram()
{
    g_ram_plus_bas_signale = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    ui_trace("[ram] %lu ms : libre %u Ko, plus bas %u Ko, plus gros bloc %u Ko\n", (unsigned long)millis(), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(g_ram_plus_bas_signale / 1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
}

static uint32_t g_economie_ms = 10000; // powerReduction_timer de la production ; 31/10 : réglable (onglet Réglages de la page Batterie)
#define RESET_ECRAN_PIN 42     // reset commun GT911 / ST7701 (modèle sur batterie) : tenu bas pendant la veille
#define RADAR_REVEIL_PIN GPIO_NUM_2
static bool g_economie = false;
// 31/10 : coupure du Wi-Fi après la synchro (réglable, onglet Réglages de la page Batterie) : réveil par le radar (ou la
// minuterie) sans toucher, une fois HA reçu, la ligne du réveil et l'état envoyés : MQTT quitté proprement, puis la radio
// arrêtée (pilote gardé : sa RAM n'est pas rendue puis reprise) ; rallumée au premier toucher, en économie (PS_MIN) jusqu'à
// la veille. Gardée tant que le diag radar, la page web ou le réglage auto du radar tournent.
static bool g_touche = false;       // touché depuis le réveil
static bool g_wifi_coupe = false;   // radio arrêtée exprès
static bool g_rallumer = false;     // demandé par un toucher, fait dans la boucle
static bool g_wifi_rallume = false; // rallumée par un toucher : PS_MIN jusqu'à la veille
static uint32_t g_rallume_ms = 0;

// ---- 26/10 : démarrage court (production : setup_fast_v3), au réveil par le radar et après un redémarrage logiciel :
// pas d'écran de démarrage, la page principale directement, éclairée quand HA a répondu ; Wi-Fi sur la borne et le canal
// du dernier démarrage (gardés en mémoire RTC ; la production scannait le seul canal de la maison, en parallèle) ; MQTT
// sans attente ; la bibliothèque musicale à la demande. Wi-Fi absent : veilles de plus en plus longues (wifiBackoffSleepUs).
struct wifi_rtc_t
{
    uint32_t magic;
    uint8_t bssid[6];
    uint8_t canal;
    uint8_t echecs; // wifi_fail_streak
    // 27/10 : diagnostic du dernier échec (le séjour a bouclé sans Wi-Fi, cause invisible : traces perdues à chaque veille)
    uint8_t raison;     // dernière raison de déconnexion du Wi-Fi (wifi_err_reason_t), 0 = aucune vue
    int8_t rssi_vise;   // borne choisie au scan (dBm), 0 = borne gardée sans scan
    uint8_t court;      // l'essai raté était un réveil court
};
RTC_NOINIT_ATTR static wifi_rtc_t g_wifi_rtc;
#define WIFI_RTC_MAGIC 0x57494649u // « WIFI »
static int32_t g_rssi_choisi = -100; // borne choisie au scan (wifi_connecter)
static void wifi_connecter(); // plus bas : la recherche de la meilleure borne
static bool wifi_connecter_vite();
static bool g_court = false;      // ce démarrage est un réveil court
static bool g_vite = false;       // 27/10 : connexion directe à la borne gardée (sans scan)
static bool g_pret = false;       // écran prêt (HA a répondu) : éclairé, temps notés
static uint32_t g_t_wifi = 0, g_t_mqtt = 0;

static uint64_t attente_wifi_us(uint8_t echecs)
{
    static const uint32_t s[] = {15, 30, 60, 120, 300, 600, 1200}; // 15 s -> 20 min (production)
    return (uint64_t)s[LV_MIN(echecs, 6)] * 1000000ULL;
}

// commande 9 bits de la ST7701 (D/C en tête) envoyée à la main sur les broches SPI de la dalle (cs 39, sclk 48, mosi 47)
static void st7701_9bits(uint32_t donnee, bool est_donnee)
{
    const uint32_t trame = (donnee & 0xFF) | (est_donnee ? 0x100 : 0);
    for (int i = 8; i >= 0; i--)
    {
        digitalWrite(48, LOW);
        digitalWrite(47, (trame >> i) & 1);
        digitalWrite(48, HIGH);
    }
}

// la dalle en veille profonde (DSTB), séquence de la production (deepSleepST7701S)
static void dalle_dodo()
{
    pinMode(39, OUTPUT);
    pinMode(48, OUTPUT);
    pinMode(47, OUTPUT);
    digitalWrite(39, LOW);
    st7701_9bits(0x28, false); // Display OFF
    st7701_9bits(0x10, false); // Sleep IN
    delay(10);
    const uint8_t b1[] = {0x77, 0x01, 0x00, 0x00, 0x80}, b2[] = {0x77, 0x01, 0x00, 0x80, 0x00};
    st7701_9bits(0xFF, false);
    for (uint8_t v : b1)
        st7701_9bits(v, true);
    st7701_9bits(0xFF, false);
    for (uint8_t v : b2)
        st7701_9bits(v, true);
    digitalWrite(39, HIGH);
    delay(120); // attente demandée par Sitronix
}

// 27/10 : diagnostic des réveils en double, relue au réveil suivant (ligne « Réveil ») : heure RTC (continue pendant la
// veille), broche du radar à la toute fin du coucher ; 31/10 : + attente de la broche basse avant de se coucher
struct coucher_rtc_t
{
    uint32_t magic;
    uint8_t broche_fin;
    uint32_t attente_ms;
    uint64_t t_us;
};
RTC_NOINIT_ATTR static coucher_rtc_t g_coucher;
#define COUCHER_MAGIC 0x434F5532u // « COU2 » (31/10 : structure changée)

// 31/10 : la broche de réveil du radar, éveillé : entrée numérique ordinaire (la garde du coucher la lit), libérée du
// verrou du coucher précédent ; elle ne passe en RTC qu'au coucher (le réveil ext0 l'exige). Lue en RTC dès le
// démarrage (rtc_gpio_get_level), elle répondait « haute » en permanence : le séjour ne dormait plus (31/10 15:52-22:40)
static void broche_radar_preparer()
{
    rtc_gpio_hold_dis(RADAR_REVEIL_PIN);
    rtc_gpio_deinit(RADAR_REVEIL_PIN);
    pinMode(RADAR_REVEIL_PIN, INPUT_PULLDOWN);
}

// 01/11 : liaison du radar au réveil, AVANT son port série : TX en sortie haute (repos de la ligne), puis le verrou de
// la veille relâché. Ouvert sous le verrou (Serial1.begin puis gpio_deep_sleep_hold_dis, l'ordre de la production), le
// capteur se taisait (0 octet) jusqu'à sa première commande, à chaque réveil par le radar quand il ne voyait personne au
// coucher : 5 sur 5 (01/11, matin et contre-épreuve), 0 sur 5 dans cet ordre-ci (pourquoi seulement ce cas : inexpliqué)
#define RADAR_TX_PIN 40
static void radar_liaison_preparer()
{
    pinMode(RADAR_TX_PIN, OUTPUT);
    digitalWrite(RADAR_TX_PIN, HIGH);
    gpio_deep_sleep_hold_dis();
}

static void dodo(uint64_t minuterie_us)
{
    ui_trace("[energie] veille profonde%s\n", minuterie_us ? " (avec minuterie)" : "");
    // 30/10 : 32 plantages sur 42 (chien de garde des interruptions) 63-66 s après le réveil, donc au coucher :
    // l'étape exacte reste en RTC et part dans la ligne « Démarrage » du journal de HA
    ui_diag_etape("dodo");
    ui_diag_point("debut");
    esp_sleep_enable_ext0_wakeup(RADAR_REVEIL_PIN, 1);
    if (minuterie_us)
        esp_sleep_enable_timer_wakeup(minuterie_us);
    rtc_gpio_init(RADAR_REVEIL_PIN);
    rtc_gpio_set_direction(RADAR_REVEIL_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_dis(RADAR_REVEIL_PIN);
    rtc_gpio_pulldown_en(RADAR_REVEIL_PIN);
    ui_diag_point("lumiere");
    tft.setBrightness(0);
    lv_timer_enable(false);
    ui_diag_point("dalle");
    dalle_dodo();
    ui_diag_point("tactile");
    if (tft.touch())
        tft.touch()->sleep();
    ui_diag_point("mqtt");
    ui_mqtt_quitter();
    delay(100);
    ui_diag_point("wifi");
    WiFi.disconnect(false);
    delay(100);
    esp_task_wdt_reset();
    ui_diag_point("batterie");
    ui_batterie_dodo(); // durée du réveil, dernier point de la courbe, tension gardée
    delay(100);
    ui_diag_point("broches");
    digitalWrite(RESET_ECRAN_PIN, LOW);
    delay(2);
    gpio_hold_en((gpio_num_t)RESET_ECRAN_PIN);
    esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
    gpio_deep_sleep_hold_en();
    delay(50);
    g_coucher.broche_fin = rtc_gpio_get_level(RADAR_REVEIL_PIN);
    rtc_gpio_hold_en(RADAR_REVEIL_PIN);
    g_coucher.t_us = esp_rtc_get_time_us();
    g_coucher.magic = COUCHER_MAGIC;
    ui_diag_point("sommeil");
    esp_deep_sleep_start();
}

// 26/10 : réveil court : temps du Wi-Fi, de MQTT et de l'écran prêt (comme la page Info de la production : depuis le
// début du setup) ; l'écran s'éclaire quand HA a répondu (2 s au plus après MQTT, pour ne pas rester noir)
static uint32_t g_t0 = 0;
static void reveil_tache()
{
    if (!g_court || g_pret)
        return;
    const uint32_t t = millis();
    if (!g_t_wifi && WiFi.isConnected())
        g_t_wifi = t;
    if (!g_t_mqtt && ui_mqtt_connecte())
        g_t_mqtt = t;
    const bool trop_long = t - g_t0 > 3000; // HA ne répond pas : on montre quand même la page (données d'avant la veille)
    if (!trop_long && (!g_t_mqtt || (!ui_mqtt_dernier_ha && t - g_t_mqtt < 2000)))
        return;
    g_pret = true;
    ui_appareil_rallumer();
    const uint16_t wifi = g_t_wifi ? g_t_wifi - g_t0 : 0, mqtt = g_t_mqtt ? g_t_mqtt - g_t_wifi : 0;
    ui_trace("[reveil] Wi-Fi %u ms, MQTT %u ms, écran prêt en %u ms%s\n", wifi, mqtt, (unsigned)(t - g_t0), trop_long ? " (sans HA)" : "");
}

static void energie_tache()
{
    if (!g_ecran || !g_ecran->batterie)
        return;
    // Wi-Fi toujours absent 20 s après le démarrage (production : 200 x 100 ms) : veille, nouvel essai de plus en plus tard
    static bool wifi_vu = false, repli = false;
    wifi_vu = wifi_vu || WiFi.isConnected();
    // 27/10 : la borne gardée ne répond pas en 6 s (d'habitude ~1 s) : on cherche la meilleure, une fois, dans ce réveil
    if (g_vite && !wifi_vu && !repli && millis() > 6000)
    {
        repli = true;
        ui_trace("[wifi] borne gardée muette : nouveau scan\n");
        wifi_connecter();
    }
    if (!wifi_vu && millis() > 20000)
    {
        if (g_wifi_rtc.magic != WIFI_RTC_MAGIC)
            memset(&g_wifi_rtc, 0, sizeof g_wifi_rtc), g_wifi_rtc.magic = WIFI_RTC_MAGIC;
        const uint64_t us = attente_wifi_us(g_wifi_rtc.echecs);
        g_wifi_rtc.echecs = (uint8_t)LV_MIN(250, g_wifi_rtc.echecs + 1);
        g_wifi_rtc.court = g_court;
        g_wifi_rtc.rssi_vise = g_court ? 0 : (int8_t)g_rssi_choisi;
        ui_trace("[energie] Wi-Fi absent (échec n°%u) : nouvel essai dans %u s\n", g_wifi_rtc.echecs, (unsigned)(us / 1000000));
        dodo(us);
    }
    if (ui_demarrage_affiche() || (g_court && !g_pret))
        return;
    const uint32_t inactif = lv_disp_get_inactive_time(NULL);
    // 27/10 : PAS d'économie (80 MHz, Wi-Fi en économie) tant que le Wi-Fi n'est pas connecté : au réveil par le radar,
    // personne ne touche l'écran et elle tombait 10 s après le démarrage, en pleine connexion
    const bool wifi_ok = WiFi.isConnected() || g_wifi_coupe; // 31/10 : coupé exprès : l'économie reste
    if (inactif >= g_economie_ms && !g_economie && wifi_ok)
    {
        g_economie = true;
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        setCpuFrequencyMhz(80); // la production : ~70 mA à 80 MHz, l'optimum
    }
    else if ((inactif < g_economie_ms || !wifi_ok) && g_economie)
    {
        g_economie = false;
        setCpuFrequencyMhz(240);
        esp_wifi_set_ps(g_wifi_rallume ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    }
    const int veille_s = ui_appareil_veille_s();
    ui_appareil_extinction_ms = ui_radar_diag_actif() ? (uint32_t)veille_s * 1000 : 0; // diag radar : écran noir, ESP éveillé
    const bool pret = veille_s && inactif >= (uint32_t)veille_s * 1000 && !ui_radar_mouvement() && !ui_radar_web_actif() &&
                      !ui_radar_auto_en_cours() && !ui_radar_diag_actif();
    // 31/10 : ni coucher tant que la broche du radar est haute : c'est elle qui réveille (ext0, niveau haut) ; la garde
    // ne regardait que les trames (« personne ») : 7 couchers sur 16 (27-31/10) broche haute = réveil 0,4 s après
    static uint32_t attente_t = 0; // début de l'attente de la broche basse (0 : pas d'attente)
    // 01/11 : ce qui retient le coucher, 10 s après le délai de veille : une ligne au journal à chaque changement de raison
    static uint8_t retenu = 0;
    if (veille_s && inactif >= (uint32_t)veille_s * 1000 + 10000)
    {
        const uint8_t r = ui_radar_mouvement() | ui_radar_web_actif() << 1 | ui_radar_auto_en_cours() << 2 | ui_radar_diag_actif() << 3 |
                          (digitalRead(RADAR_REVEIL_PIN) ? 1 : 0) << 4;
        if (r && r != retenu)
            ui_journal(true, TXT("Pas de veille depuis %lu s :%s%s%s%s%s", "No sleep for %lu s:%s%s%s%s%s"),
                       (unsigned long)(inactif / 1000 - veille_s), r & 1 ? TXT(" présence", " presence") : "",
                       r & 2 ? TXT(" page web", " web page") : "", r & 4 ? TXT(" réglage auto", " auto-tune") : "",
                       r & 8 ? " diag" : "", r & 16 ? TXT(" broche haute", " pin high") : "");
        retenu = r;
    }
    else
        retenu = 0;
    if (banc_dodo_s) // 01/11 : banc distant (/dodo) : veille profonde, réveil par la minuterie ou le radar
    {
        const uint32_t s = banc_dodo_s;
        banc_dodo_s = 0;
        ui_trace("[banc] veille demandée : %lu s\n", (unsigned long)s);
        g_coucher.attente_ms = 0;
        dodo((uint64_t)s * 1000000ULL);
    }
    if (!pret)
        attente_t = 0;
    else if (digitalRead(RADAR_REVEIL_PIN))
        attente_t = attente_t ? attente_t : millis() | 1;
    else
    {
        g_coucher.attente_ms = attente_t ? millis() - attente_t : 0;
        dodo(0);
    }
}

// 05/11/2026 : adaptateur du « scroll par bande » écrit dans la librairie (lv_obj_scroll.c, étage 1).
// Cuisine seulement (rotation 0, deux mémoires d'image, direct_mode) : renvoie le tampon où LVGL
// dessinera l'image suivante — buf_act, celui qui n'est pas montré, la dalle basculera dessus au VSYNC.
// Chambres (rotation 90, mode normal) : NULL, le chemin rapide de la librairie reste éteint.
uint8_t *ui_ecran_memoire_active(void)
{
#ifdef SONDE_INVALIDATION
    // Diagnostic (extinct par defaut) : 0 appel = le crochet de lv_obj_pos.c ne part pas ; appele mais NULL = pas en deux memoires.
    static uint32_t s_appels = 0;
    if (s_appels++ < 3 || (s_appels % 100) == 0)
        ui_trace("[defilement rapide] adaptateur demande (appel %lu, g_double=%d)\n", (unsigned long)s_appels, (int)g_double);
#endif
    if (!g_double)
        return nullptr;
#if LVGL_VERSION_MAJOR >= 9
    // 9.6 : lv_display_get_buf_active renvoie le lv_draw_buf_t dans lequel LVGL dessine (display/lv_display.h:742),
    // son champ public « data » est l'adresse de la mémoire d'image (draw/lv_draw_buf.h:96).
    lv_draw_buf_t *b = lv_display_get_buf_active(lv_display_get_default());
    return b ? b->data : nullptr;
#else
    lv_disp_t *d = lv_disp_get_default();
    if (!d || !d->driver || !d->driver->draw_buf)
        return nullptr;
    return (uint8_t *)d->driver->draw_buf->buf_act;
#endif
}

// 05/11 : le décalage de lignes (patch) écrit TOUTE l'aire du conteneur par le CPU : ces lignes salies
// dans le cache ne sont pas dans g_rendu (LVGL ne repeint que les bandes entrée/sortie) ni dans
// g_rendu_prec. Sans vidage, la dalle lirait du PSRAM périmé — même mécanisme que refr_sync_areas.
static lv_area_t g_rendu_dec = {480, 480, -1, -1};
void ui_ecran_zone_decalee(const lv_area_t *zone)
{
#ifdef SONDE_INVALIDATION
    // Diagnostic (extinct par defaut) : appelee UNIQUEMENT par un decalage reellement execute.
    static uint32_t s_dec = 0;
    if (s_dec++ < 3)
        ui_trace("[defilement rapide] decalage execute (%u lignes, appel %lu)\n",
                 (unsigned)(zone->y2 - zone->y1 + 1), (unsigned long)s_dec);
#endif
    g_rendu_dec.x1 = LV_MIN(g_rendu_dec.x1, zone->x1);
    g_rendu_dec.y1 = LV_MIN(g_rendu_dec.y1, zone->y1);
    g_rendu_dec.x2 = LV_MAX(g_rendu_dec.x2, zone->x2);
    g_rendu_dec.y2 = LV_MAX(g_rendu_dec.y2, zone->y2);
}


// Vidage du cache sur les seules lignes touchées : les lignes d'une zone sont contiguës (960 o par ligne,
// multiple de la ligne de cache de 32 o) -> un seul appel par emprise. Le drapeau UNALIGNED reste :
// l'adresse de la mémoire d'image rendue par LovyanGFX n'est pas garantie alignée.
static void cache_ecrire(uint8_t *fb, const lv_area_t *z)
{
    const int y1 = LV_MAX(0, (int)z->y1), y2 = LV_MIN(479, (int)z->y2);
    if (y2 < y1)
        return;
    esp_cache_msync(fb + (size_t)y1 * 480 * 2, (size_t)(y2 - y1 + 1) * 480 * 2,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

// 9.6 : signature publique du flush (include/lvgl/display/lv_display.h:81) — « px_map » est l'adresse du
// tampon en cours de vidage (lv_refr.c:1430 passe layer->draw_buf->data, soit buf_act->data : lv_refr.c:899).
#if LVGL_VERSION_MAJOR >= 9
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
#else
static void flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
#endif
{
    ui_diag_point("flush");
    const uint32_t t0 = micros();
#if LVGL_VERSION_MAJOR >= 9
    // 9.6 : plus de monitor_cb (aucun équivalent public : la 9.6 ne publie que des événements
    // LV_EVENT_REFR_START/REFR_READY, sans nombre de pixels). On compte donc ici les pixels des zones
    // vidées — les mêmes aires que le « px_num » de lv_refr.c:631 en 8.4 — pour le [rendu]/[bilan].
    g_rendu_px += lv_area_get_size(area);
#endif
    // 04/11 : l'englobant est agrandi AVANT le vidage du cache : à la dernière zone, cache_ecrire(g_rendu) doit couvrir
    // aussi la zone en cours (une petite mise à jour = une seule zone : sinon rien de ce qui vient d'être dessiné
    // ne partait en PSRAM)
    g_rendu.x1 = LV_MIN(g_rendu.x1, area->x1);
    g_rendu.y1 = LV_MIN(g_rendu.y1, area->y1);
    g_rendu.x2 = LV_MAX(g_rendu.x2, area->x2);
    g_rendu.y2 = LV_MAX(g_rendu.y2, area->y2);
    if (g_double) // 27/10 : à la dernière zone, toute la mémoire dessinée part en PSRAM puis la dalle bascule dessus
    {
#if LVGL_VERSION_MAJOR >= 9
        if (lv_display_flush_is_last(disp))
#else
        if (lv_disp_flush_is_last(disp))
#endif
        {
            const uint32_t t1 = micros();
#if LVGL_VERSION_MAJOR >= 9
            uint8_t *fb = (uint8_t *)color_p; // le tampon en cours de vidage = buf_act (voir l'en-tête de la fonction)
#else
            uint8_t *fb = (uint8_t *)disp->draw_buf->buf_act;
#endif
            // Deux emprises : ce que LVGL vient de dessiner (g_rendu) PLUS ce que refr_sync_areas a recopié des
            // zones invalides de l'image précédente (g_rendu_prec). La recopie de la caméra a son propre appel.
#ifdef MESURE_MSYNC_COMPLET
            esp_cache_msync(fb, 480 * 480 * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
#else
            cache_ecrire(fb, &g_rendu);
            cache_ecrire(fb, &g_rendu_prec);
            cache_ecrire(fb, &g_rendu_dec); // 05/11 : lignes recouvertes par le décalage du patch « scroll par bande »
#endif
            g_msync_us += micros() - t1;
            const int i = fb == tft._bus_instance.getFrameBuffer(1) ? 1 : 0;
            // bascule à la fin de la trame en cours ; sans l'attendre ici : la boucle ne rappelle LVGL qu'une fois faite
            // (il dessinera alors dans l'autre) ; caméra en route : c'est elle qui basculera, à la fin de son image
            lv_area_t z;
            if (ui_camera_zone_directe(&z))
            {
                g_lvgl_prete_ms = millis();
                g_lvgl_prete = i;
            }
            else
                tft._bus_instance.showFrameBuffer(i);
            if (g_sync_aussitot) // la recopie vers l'autre mémoire au prochain passage, même si rien d'autre ne change
            {
#if LVGL_VERSION_MAJOR >= 9
                lv_timer_resume(lv_display_get_refr_timer(disp)); // display/lv_display.h:670 (le « refr_timer » privé de la 8.4)
#else
                lv_timer_resume(lv_disp_get_default()->refr_timer);
#endif
            }
            g_bascule_us += micros() - t1;
        }
    }
    else
#if LVGL_VERSION_MAJOR >= 9
        // 9.6 : lv_color_t n'a plus de champ « full » ; px_map est déjà l'adresse brute des pixels RGB565.
        tft.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1,
                         (lgfx::swap565_t *)color_p);
#else
        tft.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1,
                         (lgfx::swap565_t *)&color_p->full);
#endif
    g_copie_us += micros() - t0;
    ui_diag_point("rendu");
#if LVGL_VERSION_MAJOR >= 9
    lv_display_flush_ready(disp); // display/lv_display.h:404
#else
    lv_disp_flush_ready(disp);
#endif
}

// ── 05/11 ÉTAPE 2-vague A : le glue écran sous 9.6 ──────────────────────────────────────────────
// Le corps de la mesure de rendu est commun aux deux versions (bilan_rendu) : seule la prise d'appel
// change — monitor_cb en 8.4, événements d'affichage en 9.6.

// 25/10 : durée de rendu de chaque image (LVGL) : quelle page, quel geste coûte ; 02/11 : tracée au-delà de 100 ms
// seulement (une image ordinaire en prend 50 à 60 : la trace d'avant, au-delà de 20 ms, remplissait le journal)
// 04/11 : le seuil se règle à la compilation — l'environnement de mesure de l'époque le descendait à 20 ms
// pour comparer avant/après un correctif du chemin de rafraîchissement (colonnes copie / bascule).
#ifndef TRACE_RENDU_MS
#define TRACE_RENDU_MS 100
#endif

// Corps du monitor_cb 8.4 : ms = durée de l'image, px = pixels peints. Branché en 8.4 sur
// disp_drv.monitor_cb, en 9.6 sur surveille_rendu (événements LV_EVENT_REFR_START / REFR_READY).
// 12/11 : compteurs de la commande telnet « rendu » (ui_diag.h) : toutes les images, sans seuil
static uint32_t g_rd_n, g_rd_ms, g_rd_max, g_rd_debut, g_rd_bascule_us, g_rd_copie_us;
static uint64_t g_rd_px;
static void rendu_bilan(char *b, unsigned n)
{
    const uint32_t s = millis() - g_rd_debut;
    // la durée d'une image compte aussi la copie vers l'autre mémoire et l'attente de la bascule (VSYNC) : séparées ici
    const float n_ = g_rd_n ? (float)g_rd_n : 1.0f;
    snprintf(b, n, "rendu : %lu images en %lu ms (%.1f/s), %.1f ms par image dont copie %.1f et bascule %.1f, max %lu ms, %lu px par image, %.0f %% du temps (hors bascule %.0f %%)\n",
             (unsigned long)g_rd_n, (unsigned long)s, s ? g_rd_n * 1000.0f / s : 0.0f, g_rd_ms / n_, g_rd_copie_us / 1000.0f / n_,
             g_rd_bascule_us / 1000.0f / n_, (unsigned long)g_rd_max, (unsigned long)(g_rd_n ? g_rd_px / g_rd_n : 0),
             s ? g_rd_ms * 100.0f / s : 0.0f, s ? (g_rd_ms - g_rd_bascule_us / 1000.0f) * 100.0f / s : 0.0f);
    g_rd_n = g_rd_ms = g_rd_max = g_rd_bascule_us = g_rd_copie_us = 0;
    g_rd_px = 0;
    g_rd_debut = millis();
}

static void bilan_rendu(uint32_t ms, uint32_t px)
{
    if (!ui_diag_rendu)
    {
        ui_diag_rendu = rendu_bilan;
        g_rd_debut = millis();
    }
    g_rd_n++;
    g_rd_ms += ms;
    g_rd_bascule_us += g_bascule_us;
    g_rd_copie_us += g_copie_us;
    g_rd_px += px;
    if (ms > g_rd_max)
        g_rd_max = ms;
#ifdef BILAN_RENDU_S
    // 04/11 : banc de mesure — TOUTES les images comptées sur l'écran (pas de seuil : un seuil fait disparaître du
    // journal les images que le correctif accélère), classées par lignes touchées, bilan toutes les BILAN_RENDU_S s.
    // Une ligne par classe : [bilan] <mode> <classe> n=… rendu_ms=<moy, dixièmes> msync_us=<moy> msync_max_us=…
    // copie_us=<moy> ; classes : petite <= 48 lignes, moyenne 49-479, pleine = 480.
    {
        struct classe_t { uint32_t n, rendu_ms, msync_us, msync_max, copie_us; };
        static classe_t cl[3];
        static uint32_t debut = millis();
        const int lignes = g_rendu.y2 >= g_rendu.y1 ? g_rendu.y2 - g_rendu.y1 + 1 : 0;
        classe_t &c = cl[lignes <= 48 ? 0 : (lignes < 480 ? 1 : 2)];
        c.n++;
        c.rendu_ms += ms;
        c.msync_us += g_msync_us;
        c.msync_max = LV_MAX(c.msync_max, g_msync_us);
        c.copie_us += g_copie_us;
        if (millis() - debut >= BILAN_RENDU_S * 1000UL)
        {
#ifdef MESURE_MSYNC_COMPLET
            static const char *mode = "complet";
#else
            static const char *mode = "lignes";
#endif
            static const char *noms[3] = {"petite", "moyenne", "pleine"};
            for (int k = 0; k < 3; k++)
                if (cl[k].n)
                    ui_trace("[bilan] %s %s n=%u rendu_ms=%u.%u msync_us=%u msync_max_us=%u copie_us=%u\n", mode, noms[k],
                             (unsigned)cl[k].n, (unsigned)(cl[k].rendu_ms * 10 / cl[k].n / 10),
                             (unsigned)(cl[k].rendu_ms * 10 / cl[k].n % 10), (unsigned)(cl[k].msync_us / cl[k].n),
                             (unsigned)cl[k].msync_max, (unsigned)(cl[k].copie_us / cl[k].n));
            memset(cl, 0, sizeof(cl));
            debut = millis();
        }
    }
#endif
    g_msync_us = 0;
#ifdef SONDE_INVALIDATION
    extern uint32_t lv_sonde_inv_px, lv_sonde_inv_n; // lv_refr.c : ce que LVGL recoit comme sale (a reporter en 9.6)
#endif
    if (ms > TRACE_RENDU_MS) {
#ifdef SONDE_INVALIDATION
        ui_trace("[rendu] %u ms (copie %u ms, bascule %u ms), %u px dans (%d,%d)-(%d,%d), inval %lu px / %lu zones\n", (unsigned)ms, (unsigned)(g_copie_us / 1000),
                 (unsigned)(g_bascule_us / 1000), (unsigned)px, g_rendu.x1, g_rendu.y1, g_rendu.x2, g_rendu.y2,
                 (unsigned long)lv_sonde_inv_px, (unsigned long)lv_sonde_inv_n);
        lv_sonde_inv_px = 0;
        lv_sonde_inv_n = 0;
#else
        ui_trace("[rendu] %u ms (copie %u ms, bascule %u ms), %u px dans (%d,%d)-(%d,%d)\n", (unsigned)ms, (unsigned)(g_copie_us / 1000),
                 (unsigned)(g_bascule_us / 1000), (unsigned)px, g_rendu.x1, g_rendu.y1, g_rendu.x2, g_rendu.y2);
#endif
    }
    g_copie_us = 0;
    g_bascule_us = 0;
    g_rendu_prec = g_rendu; // 04/11 : ces zones seront recopiees dans l'autre memoire a la prochaine image
    g_banc_px = px;
    g_rendu = {480, 480, -1, -1};
    g_rendu_dec = {480, 480, -1, -1}; // 05/11 : le décalage de la prochaine image sera signalé à nouveau
}

#if LVGL_VERSION_MAJOR >= 9
// Équivalent 9.6 du render_start_cb 8.4 (appelé lv_refr.c:615 en 8.4, une fois par image). En 9.6 le
// crochet public est lv_display_set_sync_cb (display/lv_display.h:340) : il est appelé zone par zone AU
// DÉBUT de l'image et REMPLACE la recopie interne des zones sales (lv_refr.c:771-784 : si un sync_cb est
// posé, lv_draw_buf_copy n'est plus fait). Il faut donc refaire soi-même ce que la librairie faisait,
// puis la recopie caméra. lv_display_sync_ready (display/lv_display.h:419) est obligatoire : sinon
// lv_refr.c:1547 boucle (« while(disp->syncing); »).
static void sync_rendu(lv_display_t *disp, const lv_area_t *zone)
{
    lv_draw_buf_t *act = lv_display_get_buf_active(disp);         // la mémoire cachée : LVGL va dessiner dedans
    const int i = act == &g_fb_buf[1] ? 1 : 0;                    // son indice = celui de la mémoire Lovyan
    const uint8_t *src = tft._bus_instance.getFrameBuffer(1 - i); // la mémoire montrée par la dalle
    uint8_t *dst = act ? act->data : nullptr;
    if (dst)
    {
        // 1) ce que la 9.6 ne fait plus elle-même : recopier dans la mémoire cachée les zones salies par l'image précédente
        const int x1 = LV_MAX(0, (int)zone->x1), x2 = LV_MIN(479, (int)zone->x2);
        if (x2 >= x1)
            for (int y = LV_MAX(0, (int)zone->y1); y <= LV_MIN(479, (int)zone->y2); y++)
                memcpy(dst + (y * 480 + x1) * 2, src + (y * 480 + x1) * 2, (x2 - x1 + 1) * 2);
        // 2) la recopie caméra du render_start_cb 8.4 : pendant une pause de la caméra, la mémoire cachée reçoit
        // l'image de la mémoire montrée. Le garde sur g_cam_num_fb ne la laisse se faire qu'une fois par image,
        // même si la 9.6 rappelle ce crochet pour chaque zone à synchroniser.
        lv_area_t z;
        if (ui_camera_zone_en_pause(&z) && g_cam_num_fb[i] != g_cam_num_fb[1 - i])
        {
            for (int y = LV_MAX(0, z.y1); y <= LV_MIN(479, z.y2); y++)
                memcpy(dst + (y * 480 + z.x1) * 2, src + (y * 480 + z.x1) * 2, (z.x2 - z.x1 + 1) * 2);
            cache_ecrire(dst, &z); // 04/11 : cette recopie aussi est passee par le cache, il faut l'ecrire avant la bascule
            g_cam_num_fb[i] = g_cam_num_fb[1 - i];
        }
    }
    lv_display_sync_ready(disp);
}

// Équivalent 9.6 du monitor_cb 8.4. Mesuré : la 9.6 ne publie AUCUN callback de performance (aucun
// « monitor_cb » ni « perf_cb » dans include/lvgl ; seul le module interne lv_sysmon.c se branche sur les
// événements d'affichage, et LV_USE_SYSMON est à 0 dans notre conf). Les événements REFR_START/REFR_READY
// donnent la durée de l'image ; le nombre de pixels n'a pas d'équivalent public (le « px_num » de
// lv_refr.c:631 en 8.4) — il est donc compté zone par zone dans flush_cb (g_rendu_px).
static void surveille_rendu(lv_event_t *e)
{
    switch (lv_event_get_code(e))
    {
        case LV_EVENT_REFR_START:
            g_rendu_debut = lv_tick_get();
            g_rendu_px = 0;
            ui_diag_point("refr>"); // 06/11 diagnostic : image commencée (le dump dit « refr> » si l'image ne finit pas)
            break;
        case LV_EVENT_REFR_READY:
            if (!g_rendu_px)
                return; // rien de peint : en 8.4 monitor_cb n'était pas appelé (lv_refr.c:340)
            ui_diag_point("<refr"); // 06/11 diagnostic : image terminée
            bilan_rendu(lv_tick_elaps(g_rendu_debut), g_rendu_px);
            break;
        default:
            break;
    }
}
#endif

// 31/10 : réglages d'énergie de l'écran sur batterie (onglet Réglages de la page Batterie), en mémoire flash
static void emission_appliquer() { WiFi.setTxPower(bt_reglages.wifi_reduit ? WIFI_POWER_7dBm : WIFI_POWER_19_5dBm); }

static void energie_charger()
{
    Preferences p;
    if (p.begin("energie", true)) // n'aboutit pas tant que rien n'a été enregistré : les valeurs par défaut restent
    {
        bt_reglages.coupure_wifi = p.getBool("coupure", false);
        bt_reglages.economie_s = p.getUChar("economie", 10);
        bt_reglages.wifi_reduit = p.getBool("tx7", false);
        p.end();
    }
    g_economie_ms = bt_reglages.economie_s * 1000u;
    ui_appareil_repos_ms = g_economie_ms;
    bt_regler = [](const char *quoi)
    {
        Preferences p;
        if (p.begin("energie", false))
        {
            p.putBool("coupure", bt_reglages.coupure_wifi);
            p.putUChar("economie", bt_reglages.economie_s);
            p.putBool("tx7", bt_reglages.wifi_reduit);
            p.end();
        }
        g_economie_ms = bt_reglages.economie_s * 1000u;
        ui_appareil_repos_ms = g_economie_ms;
        if (!strcmp(quoi, "wifi_reduit") && !g_wifi_coupe)
            emission_appliquer();
        ui_trace("[energie] réglage %s : coupure %d, économie %u s, émission %s\n", quoi, bt_reglages.coupure_wifi,
                 (unsigned)bt_reglages.economie_s, bt_reglages.wifi_reduit ? "7 dBm" : "max");
    };
}

// 31/10 : chronologie du réveil (page Batterie), synchro, coupure du Wi-Fi et rallumage au toucher
static void synchro_tache()
{
    if (!g_ecran || !g_ecran->batterie)
        return;
    if (WiFi.isConnected())
        ui_batterie_etape(BT_E_WIFI);
    if (ui_mqtt_connecte())
        ui_batterie_etape(BT_E_MQTT);
    static bool synchro = false;
    if (!synchro && ui_mqtt_connecte() && ui_appareil_synchroniser())
    {
        synchro = true;
        ui_batterie_etape(BT_E_SYNCHRO);
        ui_trace("[energie] synchronisé à %lu ms\n", (unsigned long)millis());
    }
    static bool dit = false; // pas de synchro (donc pas de coupure) au bout de 10 s : ce qui manque, une fois
    if (!synchro && !dit && millis() > 10000 && ui_mqtt_connecte())
    {
        dit = true;
        char m[200];
        const int n = ui_mqtt_manquants(m, sizeof m);
        ui_trace("[energie] pas de synchro à 10 s : %d valeur%s manquante%s (%s)\n", n, n > 1 ? "s" : "", n > 1 ? "s" : "", m);
    }
    if (g_rallumer)
    {
        g_rallumer = false;
        g_wifi_coupe = false;
        g_wifi_rallume = true;
        g_rallume_ms = millis();
        esp_wifi_start();
        if (!wifi_connecter_vite())
            wifi_connecter();
        ui_mqtt_relancer();
        ui_trace("[energie] Wi-Fi rallumé au toucher\n");
    }
    // le journal reprend quand HA parle de nouveau (sans « Wi-Fi connecté » ni « MQTT connecté » pour ce rallumage)
    if (g_wifi_rallume && g_rallume_ms && ui_mqtt_connecte() && ui_mqtt_dernier_ha && lv_tick_elaps(ui_mqtt_dernier_ha) < 2000)
    {
        g_rallume_ms = 0;
        ui_appareil_reseau_volontaire(false);
    }
    if (!synchro || g_wifi_coupe || g_wifi_rallume || g_touche || !bt_reglages.coupure_wifi ||
        (bt_etat.cause != BT_RADAR && bt_etat.cause != BT_MINUTERIE))
        return;
    if (ui_radar_diag_actif() || ui_radar_web_actif() || ui_radar_auto_en_cours())
        return;
    ui_appareil_reseau_volontaire(true);
    ui_mqtt_quitter(); // statut « hors_ligne », comme avant la veille
    delay(100);        // le statut et la fermeture partent avant la radio
    esp_wifi_disconnect();
    esp_wifi_stop();
    g_wifi_coupe = true;
    ui_batterie_etape(BT_E_COUPURE);
    if (!g_economie)
    {
        g_economie = true;
        setCpuFrequencyMhz(80);
    }
    ui_trace("[energie] Wi-Fi coupé après la synchro (%lu ms)\n", (unsigned long)millis());
}

// 9.6 : le read_cb reçoit l'indev (plus le driver) — lv_indev_read_cb_t, include/lvgl/indev/lv_indev.h:78.
// Le corps est inchangé : LV_INDEV_STATE_PR / _REL sont redéfinis par la 9.6
// (include/lvgl/api_map/lv_api_map_v8.h:118-119, en-tête inclus sans condition depuis lvgl.h:214).
#if LVGL_VERSION_MAJOR >= 9
extern "C" lv_timer_t *lv_indev_get_read_timer(lv_indev_t *indev); // lv_indev.c:579 (non déclarée en public dans LVGL 9.6)
static void touch_cb(lv_indev_t *drv, lv_indev_data_t *data)
#else
static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
#endif
{
    uint16_t x, y;
    bool appuye;
    const bool simule = banc_toucher(&appuye, &x, &y); // 24/10 : appui simulé par le PC (banc_distant)
    ui_diag_point("toucher");
    if (!simule)
        appuye = tft.getTouch(&x, &y);
    ui_diag_point("rendu");
    if (appuye) // 31/10 : le Wi-Fi coupé après la synchro se rallume au premier toucher
    {
        g_touche = true;
        g_rallumer = g_rallumer || g_wifi_coupe;
    }
    if (ui_appareil_toucher(appuye)) // écran endormi : ce toucher le rallume, sans cliquer
        data->state = LV_INDEV_STATE_REL;
    else if (appuye)
    {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
        if (simule) // le relâcher simulé se fait au point final (clic ou fin de glissé)
        {
            data->point.x = x;
            data->point.y = y;
        }
    }
}

// 24/10 : les pages du carrousel, comme le simulateur (le banc teste tout : les données arrivent par MQTT) ;
// 02/11/2026 : Panneau LED (17 pages) ; 11/11/2026 : page « IA » juste après PC (18 pages)
static const ui_page_t pages[] = {
    {&icon_room, "Chauffage", page_thermostat_creer, page_thermostat_quitter},
    {&icon_graphic, "Graphes", page_graphe_creer, page_graphe_quitter},
    {&icon_pm25, "Qualité de l'air", page_air_creer, page_air_quitter},
    {&icon_store, "Ouvrants", page_ouvrants_creer, page_ouvrants_quitter},
    {&icon_cat, "Croquettes", page_croquettes_creer, page_croquettes_quitter},
    {&icon_cafe2, "Café", page_cafe_creer, page_cafe_quitter},
    {&icon_robinet, "Eau chaude", page_ballon_creer, page_ballon_quitter},
    {&icon_ventilo, "VMC", page_vmc_creer, page_vmc_quitter},
    {&icon_flamme, "Poêle", page_poele_creer, page_poele_quitter},
    {&icon_music, "Musique", page_musique_creer, page_musique_quitter},
    {&icon_ampoule, "Lumières", page_lumieres_creer, page_lumieres_quitter},
    {&icon_panneau_led, "Panneau LED", page_panneau_led_creer, page_panneau_led_quitter}, // 02/11/2026
    {&icon_imprimante, "Imprimante 3D", page_imprimante_creer, page_imprimante_quitter},
    {&icon_pc, "PC", page_pc_creer, page_pc_quitter},
    {&icon_ia_etincelle, "IA", page_ia_creer, page_ia_quitter}, // 11/11/2026 : juste après PC ; icône 2 (Clawd + étincelle), la retenue
    {&icon_config, "Réglages", page_reglages_creer, page_reglages_quitter},
    {&icon_info, "Informations", page_infos_creer, page_infos_quitter},
    // 26/10 : écran sur batterie seulement (dernière, épinglée en bas du rail ; l'icône est la pile vivante)
    {&icon_bat3, "Batterie et capteur", page_batterie_creer, page_batterie_quitter, true},
};

// --- Données fictives (remplacées plus tard par MQTT) ---
static void donnees_fictives()
{
    th_ext_temp = 7;
    th_ext_hum = 81;
    const float temps[] = {18.3f, 19.6f, 17.2f, 16.8f, 19.5f};
    for (int i = 0; i < TH_NB_PIECES; i++)
        th_pieces[i].temp = temps[i];
    th_pieces[1].mode = TH_CONFORT;
    th_pieces[1].consigne = 20;
    th_pieces[1].chauffe = true;
    th_pieces[2].mode = TH_ECO;
    th_pieces[2].consigne = 16;
}

// Historique fictif des graphes : 24 h glissantes jusqu'à 21h30, programme éco 16° / confort 20°
// (6h30-8h30 et 17h-22h), régulation simple, extérieur sinusoïdal (même simulation que la maquette)
static void historique_fictif()
{
    if (!gr_pieces || !gr_ext)
        return; // allocation échouée (page_graphe_preparer) : on n'écrit dans rien — 06/11 audit 9.6
    gr_minute_maintenant = 21 * 60 + 30;
    for (int k = 0; k < TH_NB_PIECES; k++)
    {
        gr_historique_t &g = gr_pieces[k];
        memset(g.chauffe, 0, sizeof g.chauffe);
        float t = 17.5f + k * 0.4f;
        bool chauf = false;
        for (int i = 0; i < GR_POINTS; i++)
        {
            int m = (gr_minute_maintenant - (GR_POINTS - 1 - i) * 5 + 1440) % 1440;
            float h = m / 60.0f;
            float c = ((h >= 6.5f && h < 8.5f) || (h >= 17 && h < 22)) ? 20 : 16;
            float e = 7.5f + 3.5f * sinf((h - 9) / 24 * 2 * PI) + sinf(i * 0.7f + k) * 0.25f;
            if (t < c - 0.3f)
                chauf = true;
            if (t > c + 0.3f)
                chauf = false;
            t += chauf ? 0.045f : -0.006f * (t - e) / 4;
            t += sinf(i * 1.3f + k) * 0.02f;
            g.temp[i] = (int16_t)lroundf(t * 10);
            g.cons[i] = (int16_t)lroundf(c * 10);
            g.hum[i] = (uint8_t)lroundf(58 + 6 * sinf((h - 3) / 24 * 2 * PI + k) + sinf(i * 0.9f) * 1.2f);
            if (chauf)
                g.chauffe[i >> 3] |= 1 << (i & 7);
            if (k == 0)
                gr_ext[i] = (int16_t)lroundf(e * 10);
        }
        th_pieces[k].temp = t; // le thermostat affiche la même valeur que le bout de la courbe
    }
}

static void commande_fictive(int piece, th_mode_t mode, float consigne)
{
    th_piece_t &p = th_pieces[piece];
    p.chauffe = mode != TH_ARRET && p.temp < consigne - 0.3f; // simule la régulation de HA
    ui_trace("[thermostat] %s : mode %d consigne %.1f\n", p.nom, (int)mode, consigne);
    page_thermostat_maj();
}

// Wi-Fi à IP fixe : la box et le répéteur diffusent le même SSID ; un simple WiFi.begin peut s'accrocher au
// répéteur le plus loin (vu au banc le 24/10 : -77 dBm, démarrage en 56 s) -> on retient la borne la plus forte
// et on impose son BSSID et son canal. Les paramètres réseau (SSID, mot de passe, canal, préfixe d'adresses)
// sont dans secrets_wifi.h (hors git, voir secrets_wifi.h.exemple).

// 26/10 : réveil court : directement sur la borne et le canal gardés, sans scan
static bool wifi_connecter_vite()
{
    // 27/10 : pas après un échec : la borne gardée ne change qu'à une connexion réussie ; le séjour bouclait sans fin
    // (radar haut au coucher -> réveil court aussitôt -> même borne muette -> 20 s -> veille -> …)
    if (!g_ecran || g_wifi_rtc.magic != WIFI_RTC_MAGIC || !g_wifi_rtc.canal || g_wifi_rtc.echecs)
        return false;
    WiFi.config(IPAddress(RESEAU_OCTET_1, RESEAU_OCTET_2, RESEAU_OCTET_3, g_ecran->ip),
                IPAddress(RESEAU_OCTET_1, RESEAU_OCTET_2, RESEAU_OCTET_3, RESEAU_PASSERELLE),
                IPAddress(255, 255, 255, 0), IPAddress(8, 8, 8, 8));
    WiFi.begin(WIFI_MAISON_SSID, WIFI_MAISON_MDP, g_wifi_rtc.canal, g_wifi_rtc.bssid, true);
    ui_trace("[wifi] réveil : borne gardée, canal %u\n", g_wifi_rtc.canal);
    return true;
}

// 02/11 : les scans sont ASYNCHRONES (la boucle, donc LVGL, ne s'arrête plus : un scan complet la figeait 2 à 3 s à
// chaque reconnexion, celui de la surveillance ~0,3 s) ; wifi_scan_tache lit le résultat aux tours suivants
enum scan_t : uint8_t
{
    SCAN_AUCUN,
    SCAN_COMPLET,  // wifi_connecter : tous les canaux (la borne maison la plus forte)
    SCAN_CANAL,    // le scan complet n'a rien vu de la maison : son canal seul (comme la production)
    SCAN_SURVEILLE // wifi_surveiller : une borne bien meilleure sur le canal courant ?
};
static scan_t g_scan = SCAN_AUCUN;
static int g_scan_complet = 0;    // réseaux vus au scan complet (trace)
static int32_t g_scan_rssi = 0;   // signal de la borne courante au lancement de SCAN_SURVEILLE
static bool g_connecter_apres = false; // connexion demandée pendant SCAN_SURVEILLE : faite à sa fin
static uint32_t g_scan_debut = 0;
#define SCAN_MAX_MS 15000 // un scan qui ne finit pas compte comme vide (la connexion ne doit jamais rester en plan)

static void wifi_choisir(int n); // juste en dessous : la connexion une fois le scan fini

static void wifi_connecter()
{
    if (g_scan == SCAN_COMPLET || g_scan == SCAN_CANAL)
        return; // la connexion est déjà en route (elle suit le scan en cours)
    if (g_scan == SCAN_SURVEILLE)
    {
        g_connecter_apres = true; // à la fin du scan de surveillance (un seul scan à la fois)
        return;
    }
    WiFi.disconnect();
    WiFi.scanDelete();
    g_scan_debut = millis();
    if (WiFi.scanNetworks(true, true) != WIFI_SCAN_FAILED)
        g_scan = SCAN_COMPLET;
    else // scan impossible : la connexion sans choix de borne, comme quand le scan ne voit rien
    {
        g_scan = SCAN_CANAL;
        g_scan_complet = 0;
        wifi_choisir(0);
    }
}

// la connexion, une fois le scan (complet, puis canal de la maison s'il le faut) terminé
static void wifi_choisir(int n)
{
    int meilleur = -1;
    for (int i = 0; i < n; i++)
        if (WiFi.SSID(i) == WIFI_MAISON_SSID && (meilleur < 0 || WiFi.RSSI(i) > WiFi.RSSI(meilleur)))
            meilleur = i;
    if (g_scan == SCAN_COMPLET && meilleur < 0) // 24/10 : le scan complet est revenu vide au banc ; la production ne scanne que le canal de la maison
    {
        g_scan_complet = n;
        WiFi.scanDelete();
        g_scan = SCAN_CANAL;
        g_scan_debut = millis();
        if (WiFi.scanNetworks(true, true, false, 300, WIFI_MAISON_CANAL) != WIFI_SCAN_FAILED)
            return;
        n = 0;
    }
    if (g_scan == SCAN_CANAL)
        ui_trace("[wifi] scan complet : %d réseaux ; canal %d : %d\n", g_scan_complet, WIFI_MAISON_CANAL, n);
    g_scan = SCAN_AUCUN;
    if (g_ecran)
        WiFi.config(IPAddress(RESEAU_OCTET_1, RESEAU_OCTET_2, RESEAU_OCTET_3, g_ecran->ip),
                    IPAddress(RESEAU_OCTET_1, RESEAU_OCTET_2, RESEAU_OCTET_3, RESEAU_PASSERELLE),
                    IPAddress(255, 255, 255, 0), IPAddress(8, 8, 8, 8));
    else
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); // écran inconnu : DHCP
    if (meilleur >= 0)
    {
        uint8_t bssid[6];
        memcpy(bssid, WiFi.BSSID(meilleur), 6);
        const int canal = WiFi.channel(meilleur);
        g_rssi_choisi = WiFi.RSSI(meilleur);
        ui_trace("[wifi] maison (IP fixe) : borne %s, canal %d, %d dBm (%d bornes vues)\n", WiFi.BSSIDstr(meilleur).c_str(), canal,
                 (int)g_rssi_choisi, n);
        WiFi.scanDelete();
        WiFi.begin(WIFI_MAISON_SSID, WIFI_MAISON_MDP, canal, bssid, true);
    }
    else
    {
        WiFi.scanDelete();
        WiFi.begin(WIFI_MAISON_SSID, WIFI_MAISON_MDP);
        ui_trace("[wifi] maison (IP fixe) : aucune borne vue au scan\n");
    }
}

// dans la boucle : le scan en cours est-il fini ?
static void wifi_scan_tache()
{
    if (g_scan == SCAN_AUCUN)
        return;
    if (g_wifi_coupe) // radio arrêtée exprès (écran sur batterie) : le scan ne finira pas, et on ne se reconnecte pas
    {
        g_scan = SCAN_AUCUN;
        g_connecter_apres = false;
        return;
    }
    int16_t r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING && millis() - g_scan_debut < SCAN_MAX_MS)
        return;
    if (r == WIFI_SCAN_RUNNING)
    {
        ui_trace("[wifi] scan sans fin après %u s : abandonné\n", SCAN_MAX_MS / 1000);
        WiFi.scanDelete();
        r = 0;
    }
    const int n = r > 0 ? r : 0; // échec : comme un scan vide
    if (g_scan != SCAN_SURVEILLE)
    {
        wifi_choisir(n);
        return;
    }
    g_scan = SCAN_AUCUN;
    if (g_connecter_apres)
    {
        g_connecter_apres = false;
        wifi_connecter();
        return;
    }
    int32_t mieux = -100;
    for (int i = 0; i < n; i++)
        if (WiFi.SSID(i) == WIFI_MAISON_SSID && WiFi.BSSIDstr(i) != WiFi.BSSIDstr())
            mieux = LV_MAX(mieux, WiFi.RSSI(i));
    WiFi.scanDelete();
    ui_trace("[wifi] signal faible (%d dBm) ; meilleure autre borne : %d dBm\n", (int)g_scan_rssi, (int)mieux);
    if (mieux >= g_scan_rssi + 10)
        wifi_connecter();
}

// Comme la production (loop.h, toutes les 30 s) : signal sous -75 dBm -> on regarde si une borne bien meilleure est là
// (10 dB de mieux au moins, pour ne pas se déconnecter pour rien)
static void wifi_surveiller()
{
    // Comme la production (mobile_device.h, après la connexion) : pas d'économie d'énergie du Wi-Fi. 24/10 : elle était
    // active malgré WiFi.setSleep(false) avant la connexion (ping 2 à 150 ms, capture à 28 Ko/s au lieu de 0,9 s)
    static bool connecte = false;
    if (WiFi.isConnected() != connecte)
    {
        connecte = !connecte;
        if (connecte)
        {
            wifi_ps_type_t ps = WIFI_PS_NONE;
            esp_wifi_get_ps(&ps);
            esp_wifi_set_ps(g_wifi_rallume ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE); // 31/10 : rallumé au toucher : PS_MIN
            ui_trace("[wifi] connecté ; économie d'énergie trouvée : %d (0 = aucune), %s\n", (int)ps, g_wifi_rallume ? "PS_MIN" : "coupée");
            if (g_ecran && g_ecran->batterie)
                emission_appliquer(); // 31/10 : 7 dBm si réglé
            // 26/10 : la borne du réveil suivant (écran sur batterie)
            g_wifi_rtc.magic = WIFI_RTC_MAGIC;
            memcpy(g_wifi_rtc.bssid, WiFi.BSSID(), 6);
            g_wifi_rtc.canal = WiFi.channel();
            g_wifi_rtc.echecs = 0;
        }
    }
    static uint32_t dernier = 0;
    if (millis() - dernier < 30000 || !WiFi.isConnected())
        return;
    dernier = millis();
    if (g_scan != SCAN_AUCUN)
        return;
    g_scan_rssi = WiFi.RSSI();
    if (g_scan_rssi >= -75)
        return;
    WiFi.scanDelete();
    g_scan_debut = millis();
    if (WiFi.scanNetworks(true, false, false, 300, WiFi.channel()) != WIFI_SCAN_FAILED)
        g_scan = SCAN_SURVEILLE; // résultat : wifi_scan_tache
}

void setup()
{
    // 29/10 : AVANT TOUT : les tableaux de texte inscrits avant setup() reçoivent leur PSRAM (ui_psram.h)
    int psram_nb = 0;
    const size_t psram_octets = ui_psram_allouer(&psram_nb);
    // 25/10 : allocations de 512 octets et plus en PSRAM d'abord (framework : 4 Ko) : la RAM interne reste au Wi-Fi, aux
    // tampons de dessin et aux piles des tâches
    g_t0 = millis(); // 26/10 : référence des temps du réveil (comme setup_start_time de la production)
    heap_caps_malloc_extmem_enable(512);
    Serial.begin(115200);
    ui_diag_demarrer(); // 24/10 : raison du redémarrage, dump du plantage précédent, Telnet :23, chien de garde
    ui_diag_ram_etape("diag"); // 28/10 : AUDIT RAM
    ui_trace("[ram] %d tableaux de texte : %u octets en PSRAM\n", psram_nb, (unsigned)psram_octets);
    ecran_reconnaitre(); // 26/10 : IP, pièce et sens de montage d'après la MAC
    if (g_ecran && g_ecran->batterie) // 26/10 : la cause du réveil (radar sur GPIO 2, minuterie si le Wi-Fi manquait)
    {
        const esp_sleep_wakeup_cause_t r = esp_sleep_get_wakeup_cause();
        ui_batterie_demarrer(r == ESP_SLEEP_WAKEUP_EXT0 ? BT_RADAR : r == ESP_SLEEP_WAKEUP_TIMER ? BT_MINUTERIE : BT_DEMARRAGE);
        if (r == ESP_SLEEP_WAKEUP_EXT0 && g_coucher.magic == COUCHER_MAGIC) // 27/10 : dans la ligne « Réveil »
        {
            const uint32_t ms = (uint32_t)((esp_rtc_get_time_us() - g_coucher.t_us) / 1000);
            char duree[16];
            if (ms < 60000)
                snprintf(duree, sizeof duree, TXT("%u" SEP_DEC "%u s", "%u" SEP_DEC "%u s"), (unsigned)(ms / 1000), (unsigned)(ms % 1000 / 100));
            else
                snprintf(duree, sizeof duree, "%u min", (unsigned)(ms / 60000));
            char attente[32] = "";
            if (g_coucher.attente_ms)
                snprintf(attente, sizeof attente, TXT(" après %u" SEP_DEC "%u s d'attente", " after %u" SEP_DEC "%u s waiting"),
                         (unsigned)(g_coucher.attente_ms / 1000),
                         (unsigned)(g_coucher.attente_ms % 1000 / 100));
            snprintf(ui_appareil_reveil_info, sizeof ui_appareil_reveil_info,
                     TXT(" · endormi %s ; au coucher précédent : broche %s%s", " · asleep for %s ; at previous sleep: pin %s%s"),
                     duree, g_coucher.broche_fin ? TXT("haute", "high") : TXT("basse", "low"), attente);
        }
        g_coucher.magic = 0;
        g_coucher.attente_ms = 0; // un coucher sans garde (Wi-Fi absent) n'attend pas
        broche_radar_preparer();
        radar_liaison_preparer(); // 01/11 : avant le port série du radar (le capteur se taisait)
        // radar : la configuration n'est relue qu'au démarrage à froid (mise sous tension, bouton), comme la production
        const esp_reset_reason_t rr = esp_reset_reason();
        ui_radar_demarrer(r == ESP_SLEEP_WAKEUP_UNDEFINED && (rr == ESP_RST_POWERON || rr == ESP_RST_EXT));
        // sort l'écran de son reset (production : setup.h) ; le verrou de la veille est relâché plus haut
        gpio_hold_dis((gpio_num_t)RESET_ECRAN_PIN);
        pinMode(RESET_ECRAN_PIN, OUTPUT);
        digitalWrite(RESET_ECRAN_PIN, HIGH);
        energie_charger(); // 31/10 : luminosité de repos au bout de 10 s (réglable) ; le délai de veille = veille profonde
        // les 2 démarrages de la production : court au réveil par le radar et après un redémarrage logiciel, complet sinon
        g_court = r == ESP_SLEEP_WAKEUP_EXT0 || (r == ESP_SLEEP_WAKEUP_UNDEFINED && rr == ESP_RST_SW);
        ui_mqtt_stabilisation_ms = 0;   // pas d'attente avant MQTT (la production s'y connectait tout de suite)
        ui_musique_a_la_demande = true; // bibliothèque lue à l'ouverture de la liste
        ui_trace("[ecran] démarrage %s\n", g_court ? "court (réveil)" : "complet");
    }

    // 26/10 : comme la production (setup.h) : sans ça, begin() rallume le rétroéclairage à sa valeur par défaut (127) sur
    // une image noire, jusqu'au setBrightness(0) plus bas : un éclair gris à chaque démarrage et à chaque réveil
    tft.setBrightness(0);
    tft.begin();
    ui_diag_ram_etape("tft.begin"); // 28/10 : AUDIT RAM
    tft.setRotation(g_ecran ? g_ecran->rotation : 0);
#ifdef ECRAN_ROT0 // 05/11 : forçage de compilation de la rotation 0 (deux mémoires d'image, mode direct, chemin rapide
                  // du scroll), à l'époque porté par l'env de banc de la chambre 1 — aujourd'hui « banc-chambre1-v9 ».
                  // DEPUIS LE 08/11 il ne sert plus à rien : la table ECRANS[] met toute la production en rotation 0.
                  // Il est conservé pour les A-B de rotation. Tout le reste suit (g_double, caméra, flush).
    tft.setRotation(0);
#endif
    // 26/10 : caméras : écriture directe dans la mémoire d'image (ui_camera.h), rotation interne réelle de Lovyan
    {
        auto *fb = static_cast<lgfx::Panel_FrameBufferBase *>(tft.getPanel());
        ui_camera_ecran_lignes = acces_fb::lignes(fb);
        ui_camera_ecran_rotation = acces_fb::rotation(fb);
        ui_camera_ecran_synchro = [](void *debut, size_t octets)
        {
            const uintptr_t a = (uintptr_t)debut & ~127u, b = ((uintptr_t)debut + octets + 127u) & ~127u; // comme Lovyan
            esp_cache_msync((void *)a, b - a, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
        };
    }
    tft.setColorDepth(16);

    lv_init();
#if LVGL_VERSION_MAJOR >= 9
    // Sous 9.6, lv_tick_get() doit être branché sur millis() : sans horloge aucun timer LVGL ne mûrit — écran
    // figé à 85 %, mesuré sur un écran le 06/11 (analyse complète).
    lv_tick_set_cb(millis); // include/lvgl/tick/lv_tick.h:84 — lv_tick_get_cb_t = uint32_t (*)(void)

    // 9.6 : l'affichage doit exister avant les tampons — lv_display_set_buffers s'appuie sur la résolution
    // (display/lv_display.h:269 ; lv_display.c:550 refuse une résolution nulle). Format RGB565 posé à la
    // main : tout le glue ci-dessous raisonne en 2 octets par pixel (mémoire d'image 480 * 480 * 2).
    g_disp = lv_display_create(480, 480); // display/lv_display.h:96
    // Format RGB565_SWAPPED : la 9.6 n'a plus LV_COLOR_16_SWAP ; sans cela la dalle reçoit les deux octets
    // de chaque pixel inversés (rouge et bleu permutés). La caméra écrit déjà en ordre inverse : ne pas la changer.
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565_SWAPPED); // display/lv_display.h:362
#endif
    ui_diag_ram_etape("lv_init"); // 28/10 : AUDIT RAM
    // Tampon de dessin : UN seul tampon de 80 lignes en RAM INTERNE (la copie est synchrone, jamais d'écriture
    // pendant l'envoi) ; repli 40 lignes RAM puis 80 PSRAM. Rotation 0 : libéré juste après (deux mémoires d'image).
    size_t lignes = 80;
    lv_color_t *b1 = (lv_color_t *)heap_caps_malloc(480 * lignes * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!b1)
    {
        lignes = 40;
        b1 = (lv_color_t *)heap_caps_malloc(480 * lignes * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    const bool interne = b1 != nullptr;
    if (!b1)
    {
        lignes = 80;
        b1 = (lv_color_t *)heap_caps_malloc(480 * lignes * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    lv_color_t *b2 = nullptr;
    ui_trace("[ecran] tampon de dessin : 1 x %u lignes en %s\n", (unsigned)lignes, interne ? "RAM interne" : "PSRAM");
#if LVGL_VERSION_MAJOR >= 9
    // 9.6 : lv_display_set_buffers (display/lv_display.h:269). ATTENTION à l'unité : la 8.4 comptait la
    // taille du tampon en PIXELS (lv_disp_draw_buf_init : « size_in_px_cnt »), la 9.6 en OCTETS.
    lv_display_set_buffers(g_disp, b1, b2, 480 * lignes * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
    lv_disp_draw_buf_init(&draw_buf, b1, b2, 480 * lignes);
#endif
    ui_diag_ram_etape("tampons de dessin"); // 28/10 : AUDIT RAM
    if (!tft.getRotation() && tft._bus_instance.initDoubleBuffer())
    {
        for (int i = 0; i < 2; i++)
        {
            g_lignes_fb[i] = (uint8_t **)heap_caps_malloc(480 * sizeof(uint8_t *), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            for (int y = 0; y < 480; y++)
                g_lignes_fb[i][y] = tft._bus_instance.getFrameBuffer(i) + y * 480 * 2;
        }
        free(b1);
        free(b2);
        // la 0 est montrée : LVGL dessine d'abord dans la 1
#if LVGL_VERSION_MAJOR >= 9
        // 9.6 : les deux mémoires d'image sont décrites par nos propres lv_draw_buf_t publics puis posées
        // avec lv_display_set_draw_buffers (display/lv_display.h:295) — c'est lui qui pose le tampon actif
        // sur le premier argument (lv_display.c:513), donc la mémoire CACHÉE (la 1) passe en premier :
        // c'est l'équivalent exact du « direct_mode = 1 avec buf1 = mémoire cachée » de la 8.4.
        // Le mode de rendu DIRECT remplace direct_mode (display/lv_display.h:53).
        lv_draw_buf_init(&g_fb_buf[1], 480, 480, LV_COLOR_FORMAT_RGB565_SWAPPED, 480 * 2, tft._bus_instance.getFrameBuffer(1), 480 * 480 * 2);
        lv_draw_buf_init(&g_fb_buf[0], 480, 480, LV_COLOR_FORMAT_RGB565_SWAPPED, 480 * 2, tft._bus_instance.getFrameBuffer(0), 480 * 480 * 2);
        lv_display_set_draw_buffers(g_disp, &g_fb_buf[1], &g_fb_buf[0]);
        lv_display_set_render_mode(g_disp, LV_DISPLAY_RENDER_MODE_DIRECT);
#else
        lv_disp_draw_buf_init(&draw_buf, tft._bus_instance.getFrameBuffer(1), tft._bus_instance.getFrameBuffer(0), 480 * 480);
#endif
        ui_camera_ecran_lignes = g_lignes_fb[0];
        ui_camera_ecran_debut = []() -> uint8_t **
        {
            while (g_cam_prete >= 0) // l'image précédente n'est pas encore partie (la boucle la basculera)
                vTaskDelay(1);
            auto &bus = tft._bus_instance;
            int montree;
            bool attente;
            do // état cohérent : pas de fin de trame entre les deux lectures
            {
                attente = bus.swapPending();
                montree = bus.shownFrameBuffer();
            } while (attente != bus.swapPending());
            if (!attente)
                g_cam_cible = 1 - montree; // la mémoire cachée
            else
            {
                // 27/10 : COURSE AU BALAYAGE : la bascule précédente attend la fin de la trame (jusqu'à 24 ms à 42 Hz) ; la
                // mémoire encore montrée deviendra cachée à ce moment-là : on y écrit dès que le balayage est passé sous le
                // haut de la zone (il va 4,6 fois plus vite que la caméra : elle ne le rattrape pas avant la bascule)
                lv_area_t z;
                const int haut = ui_camera_zone_directe(&z) ? z.y1 : 0;
                const auto &c = bus.config();
                const float ligne_us = (480 + c.hsync_front_porch + c.hsync_pulse_width + c.hsync_back_porch) * 1e6f / c.freq_write;
                const int64_t depart = bus.lastVsyncUs() + (int64_t)((c.vsync_pulse_width + c.vsync_back_porch + haut + 16) * ligne_us);
                while (bus.swapPending() && esp_timer_get_time() < depart)
                    vTaskDelay(1);
                g_cam_cible = montree;
            }
            return g_lignes_fb[g_cam_cible];
        };
        g_verrou_ecran = xSemaphoreCreateMutex();
        ui_camera_ecran_fin = []()
        {
            if (xSemaphoreTake(g_verrou_ecran, 0) == pdTRUE) // LVGL ne dessine pas : bascule tout de suite
            {
                if (!tft._bus_instance.swapPending() && tft._bus_instance.shownFrameBuffer() != g_cam_cible)
                    montrer_image_camera(g_cam_cible);
                else
                    g_cam_prete = g_cam_cible;
                xSemaphoreGive(g_verrou_ecran);
            }
            else
                g_cam_prete = g_cam_cible; // la boucle basculera après le dessin en cours
        };
        g_double = true;
        ui_trace("[ecran] deux mémoires d'image : LVGL dessine dans celle qui n'est pas montrée, bascule au VSYNC\n");
    }
    else // 29/10 : rotation 90 (ou deuxième mémoire impossible) : mode normal, tampons de dessin
        ui_trace("[ecran] rotation %d : mode normal (Lovyan tourne pendant la copie)\n", (int)tft.getRotation());

#if LVGL_VERSION_MAJOR >= 9
    // 9.6 : le pilote n'est plus une structure qu'on remplit — mise en route par appels publics.
    // « direct_mode = g_double » devient le mode de rendu : DIRECT quand les deux mémoires d'image existent
    // (posé avec lv_display_set_buffers plus haut), PARTIAL sinon, l'équivalent de direct_mode = false.
    lv_display_set_flush_cb(g_disp, flush_cb); // display/lv_display.h:316
    if (g_double)
        lv_display_set_sync_cb(g_disp, sync_rendu); // display/lv_display.h:340 — équivalent du render_start_cb 8.4
    // Équivalent du monitor_cb 8.4 : la 9.6 n'en publie pas, on se branche sur ses événements d'affichage.
    lv_display_add_event_cb(g_disp, surveille_rendu, LV_EVENT_ALL, NULL); // display/lv_display.h:560
#else
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 480;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = flush_cb;
    disp_drv.direct_mode = g_double;
    disp_drv.render_start_cb = [](lv_disp_drv_t *d)
    {
        lv_area_t z;
        if (!g_double || !ui_camera_zone_en_pause(&z))
            return;
        uint8_t *dst = (uint8_t *)d->draw_buf->buf_act;
        const int i = dst == tft._bus_instance.getFrameBuffer(1) ? 1 : 0;
        if (g_cam_num_fb[i] == g_cam_num_fb[1 - i])
            return; // déjà la même image : rien à recopier (une ou deux fois par défilement seulement)
        const uint8_t *src = tft._bus_instance.getFrameBuffer(1 - i);
        for (int y = LV_MAX(0, z.y1); y <= LV_MIN(479, z.y2); y++)
            memcpy(dst + (y * 480 + z.x1) * 2, src + (y * 480 + z.x1) * 2, (z.x2 - z.x1 + 1) * 2);
        cache_ecrire(dst, &z); // 04/11 : cette recopie aussi est passee par le cache, il faut l'ecrire avant la bascule
        g_cam_num_fb[i] = g_cam_num_fb[1 - i];
    };

    // 25/10 : durée de rendu de chaque image (LVGL) : quelle page, quel geste coûte. Le corps est passé dans
    // bilan_rendu, commun avec la 9.6 (qui n'a pas de monitor_cb) et appelée là par ses événements d'affichage.
    disp_drv.monitor_cb = [](lv_disp_drv_t *, uint32_t ms, uint32_t px) { bilan_rendu(ms, px); };
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
#endif // LVGL_VERSION_MAJOR >= 9
#ifdef BANC_DECALAGE
    {
        const int LARG = 404, HAUT = 434, PAS = 22;                 // aire visible de la liste, pas du doigt
        const size_t TAMPON = 480 * 480 * 2;                        // une mémoire d'image, comme sur l'écran
        uint8_t *a = (uint8_t *)ps_malloc(TAMPON), *b = (uint8_t *)ps_malloc(TAMPON);
        if (a && b)
        {
            for (size_t i = 0; i < TAMPON; i += 4) // les pages doivent être résidentes avant de chronométrer
                *(volatile uint32_t *)(a + i) = 0x12345678, *(volatile uint32_t *)(b + i) = i;
            const char *NOMS[3] = {"decalage dans le meme tampon (memmove)", "d'un tampon a l'autre (memcpy)", "remplissage seul (memset)"};
            for (int c = 0; c < 3; c++)
            {
                uint32_t t[15];
                for (int k = 0; k < 15; k++)
                {
                    const uint32_t t0 = micros();
                    for (int y = 0; y < HAUT; y++)
                    {
                        uint8_t *dst = a + (size_t)y * 480 * 2;
                        if (c == 0)
                            memmove(dst, a + (size_t)(y + PAS) * 480 * 2, LARG * 2);
                        else if (c == 1)
                            memcpy(dst, b + (size_t)y * 480 * 2, LARG * 2);
                        else
                            memset(dst, k, LARG * 2);
                    }
                    t[k] = micros() - t0;
                }
                std::sort(t, t + 15);
                const uint32_t px = (uint32_t)LARG * HAUT;
                ui_trace("[banc decalage] %-36s min %u us, mediane %u us, max %u us (%u ns/px)\n", NOMS[c],
                         (unsigned)t[0], (unsigned)t[7], (unsigned)t[14], (unsigned)((uint64_t)t[7] * 1000 / px));
            }
        }
        else
            ui_trace("[banc decalage] pas de PSRAM pour deux memoires d'image\n");
        if (a) free(a);
        if (b) free(b);
    }
#endif
#ifdef BILAN_RENDU_S
    // 04/11 : banc du vidage du cache, une fois au démarrage (une cuisine vide ne redessine rien : pas de bilan sans
    // activité). Chronomètre esp_cache_msync seul, en µs, sur la mémoire d'image 0 : toute la mémoire (460 800 o) ou
    // 48 lignes (46 080 o), cache PROPRE ou après avoir SALI 48 lignes / toute la mémoire (pixels relus et réécrits à
    // l'identique : l'écran ne change pas). 15 essais, min / médiane / max.
    if (g_double)
    {
        uint8_t *fb = (uint8_t *)tft._bus_instance.getFrameBuffer(0);
        const int FLAGS = ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED;
        const size_t TOUT = 480 * 480 * 2, L48 = 48 * 480 * 2, DEBUT48 = 200 * 480 * 2;
        auto salir = [&](size_t debut, size_t n)
        {
            volatile uint32_t *p = (volatile uint32_t *)(fb + debut);
            for (size_t i = 0; i < n / 4; i++)
                p[i] = p[i];
        };
        struct cas_t { const char *nom; size_t sale_debut, sale_n, sync_debut, sync_n; };
        const cas_t CAS[] = {
            {"propre, tout", 0, 0, 0, TOUT},
            {"propre, 48 lignes", 0, 0, DEBUT48, L48},
            {"48 lignes salies, tout", DEBUT48, L48, 0, TOUT},
            {"48 lignes salies, 48 lignes", DEBUT48, L48, DEBUT48, L48},
            {"tout sali, tout", 0, TOUT, 0, TOUT},
        };
        esp_cache_msync(fb, TOUT, FLAGS);
        for (const cas_t &c : CAS)
        {
            uint32_t t[15];
            for (int k = 0; k < 15; k++)
            {
                if (c.sale_n)
                    salir(c.sale_debut, c.sale_n);
                const uint32_t t0 = micros();
                esp_cache_msync(fb + c.sync_debut, c.sync_n, FLAGS);
                t[k] = micros() - t0;
                if (c.sync_n != TOUT)
                    esp_cache_msync(fb, TOUT, FLAGS); // repartir propre pour l'essai suivant
            }
            std::sort(t, t + 15);
            ui_trace("[banc msync] %-28s min %u us, mediane %u us, max %u us\n", c.nom, (unsigned)t[0], (unsigned)t[7], (unsigned)t[14]);
        }
    }
    {
        const int C = 320;
        lv_color_t *cible = (lv_color_t *)heap_caps_malloc(C * C * 2, MALLOC_CAP_SPIRAM);
        lv_color_t *image = (lv_color_t *)heap_caps_malloc(C * C * 2, MALLOC_CAP_SPIRAM);
#if LVGL_VERSION_MAJOR >= 9
        lv_obj_t * canevas = (cible && image) ? lv_canvas_create(lv_scr_act()) : NULL;
        lv_layer_t layer;
        if (canevas)
        {
            lv_canvas_set_buffer(canevas, cible, C, C, LV_COLOR_FORMAT_RGB565);
            lv_obj_add_flag(canevas, LV_OBJ_FLAG_HIDDEN);
            lv_canvas_init_layer(canevas, &layer);
        }
        uc_cible_t * dessin = canevas ? &layer : NULL;
        lv_area_t clip = {0, 0, C - 1, C - 1};
#else
        lv_draw_ctx_t *ctx = (lv_draw_ctx_t *)lv_mem_alloc(sizeof(lv_draw_sw_ctx_t));
        uc_cible_t * dessin = ctx;
        lv_area_t clip = {0, 0, C - 1, C - 1};
#endif
        if (cible && image && dessin)
        {
#if LVGL_VERSION_MAJOR < 9
            static lv_disp_t faux;
            static lv_disp_drv_t drv;
            memset(&faux, 0, sizeof faux);
            faux.driver = &drv;
            lv_disp_drv_init(&drv);
            drv.hor_res = C;
            drv.ver_res = C;
            lv_draw_sw_init_ctx(&drv, ctx);
            drv.draw_ctx = ctx;
            ctx->clip_area = &clip;
            ctx->buf_area = &clip;
            ctx->buf = cible;
            lv_disp_t *ori = _lv_refr_get_disp_refreshing();
            _lv_refr_set_disp_refreshing(&faux);
#endif
            const lv_point_t centre = {C / 2, C / 2};
            lv_draw_rect_dsc_t fond_dsc;
            lv_draw_rect_dsc_init(&fond_dsc);
            fond_dsc.bg_color = ui_c(UI_FOND);
            fond_dsc.bg_opa = LV_OPA_COVER;
#if LVGL_VERSION_MAJOR >= 9
            // 9.6 : lv_img_dsc_t -> lv_image_dsc_t (draw/lv_image_dsc.h:141-149) ; le header perd always_zero et
            // gagne magic + stride (draw/lv_image_dsc.h:29-50) ; LV_IMG_CF_TRUE_COLOR -> LV_COLOR_FORMAT_RGB565.
            lv_image_dsc_t img;
            memset(&img, 0, sizeof img);
            img.header.magic = LV_IMAGE_HEADER_MAGIC; // draw/lv_image_dsc.h:25
            img.header.cf = LV_COLOR_FORMAT_RGB565;
            img.header.w = C;
            img.header.h = C;
            img.header.stride = C * 2;
            img.data_size = C * C * 2;
            img.data = (const uint8_t *)image;
#else
            static lv_img_dsc_t img;
            img.header.always_zero = 0;
            img.header.cf = LV_IMG_CF_TRUE_COLOR;
            img.header.w = C;
            img.header.h = C;
            img.data_size = C * C * 2;
            img.data = (const uint8_t *)image;
#endif
            lv_draw_arc_dsc_t arc_dsc;
            lv_draw_arc_dsc_init(&arc_dsc);
            arc_dsc.width = 46;
            arc_dsc.color = ui_c(0xFF5B4B);
            auto somme = [&]()
            {
                uint32_t s = 0;
                const uint16_t *p = (const uint16_t *)cible;
                for (int i = 0; i < C * C; i++)
                    s = s * 31 + p[i];
                return s;
            };
            uint32_t somme_arcs = 0, somme_image = 0;
            for (int cas = 0; cas < 4; cas++)
            {
                uint32_t t[11];
                for (int k = 0; k < 11; k++)
                {
                    esp_task_wdt_reset();
                    const uint32_t t0 = micros();
                    uc_carre(dessin, fond_dsc, &clip);
                    if (cas == 1)
                        ui_anneau_dessiner(dessin, &centre, C, 46, 0.6f, false);
                    if (cas >= 2)
                        uc_image(dessin, &img, &clip);
                    if (cas == 3)
                        for (int a = 0; a < 10; a++)
                            uc_arc(dessin, arc_dsc, &centre, C / 2, 300 + a * 2, 303 + a * 2);
#if LVGL_VERSION_MAJOR >= 9
                    // 9.6 : les appels ci-dessus ne CREENT que des tasks sur la layer ; le rendu effectif par
                    // l'unité logicielle se déclenche ici. Le chrono englobe donc création + rendu, comme en 8.4
                    // où tous les appels étaient synchrones. Coût propre à 9.6 : ~1 task par segment dessiné,
                    // ajout de task en O(n) (src/draw/lv_draw.c:112-121, vu à la vague B sur les courbes).
                    lv_canvas_finish_layer(canevas, &layer);
#endif
                    t[k] = micros() - t0;
                    if (cas == 1 && k == 0)
                    {
                        memcpy(image, cible, C * C * 2); // l'image pré-rendue = l'anneau dessiné
                        somme_arcs = somme();
                    }
                    if (cas == 2 && k == 0)
                        somme_image = somme();
                }
                std::sort(t, t + 11);
                static const char *noms[4] = {"fond seul", "fond + anneau en arcs", "fond + anneau en image",
                                              "fond + image + 10 arcs"};
                ui_trace("[banc anneau] %-24s min %u us, mediane %u us, max %u us\n", noms[cas], (unsigned)t[0],
                         (unsigned)t[5], (unsigned)t[10]);
            }
            ui_trace("[banc anneau] controle : image %s de l'anneau dessine\n",
                     somme_arcs == somme_image ? "IDENTIQUE" : "DIFFERENTE");
#if LVGL_VERSION_MAJOR >= 9
            lv_obj_delete(canevas);
#else
            _lv_refr_set_disp_refreshing(ori);
            lv_draw_sw_deinit_ctx(&drv, ctx);
#endif
        }
        else
            ui_trace("[banc anneau] memoire insuffisante\n");
#if LVGL_VERSION_MAJOR < 9
        if (ctx)
            lv_mem_free(ctx);
#endif
        heap_caps_free(cible);
        heap_caps_free(image);
    }
#endif

#if LVGL_VERSION_MAJOR >= 9
    // 9.6 : le tactile n'est plus une structure mais un objet créé (indev/lv_indev.h:91) ; le read_cb
    // reçoit l'indev (indev/lv_indev.h:78) et l'indev doit être rattaché à l'affichage (:167) pour que
    // les coordonnées soient celles de l'écran.
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); // indev/lv_indev.h:139
    lv_indev_set_read_cb(indev, touch_cb);           // indev/lv_indev.h:146
    // 11/11 : la lecture du tactile suit LV_DEF_REFR_PERIOD (lv_indev.c:105) ; LVGL étant calé sur la bascule (2 ms),
    // le GT911 serait lu à chaque passage : lecture ramenée à 10 ms (rythme du GT911)
    lv_timer_set_period(lv_indev_get_read_timer(indev), 10);
    lv_indev_set_display(indev, g_disp);             // indev/lv_indev.h:167
#else
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_cb;
    lv_indev_drv_register(&indev_drv);
#endif
    ui_diag_ram_etape("pilotes LVGL + affichage"); // 28/10 : AUDIT RAM

    ui_theme_init();
#ifdef MODE_DEMO
    // MODE DÉMO (10/11/2026) : aucune donnée fictive, pas de Wi-Fi, pas de MQTT. Les pages sont remplies par la
    // table embarquée de ui_demo.cpp, rejouée par le VRAI chemin ui_mqtt_recevoir ; les commandes des pages sont
    // interceptées dans publier() et renvoyées telles que Home Assistant les republierait (ui_demo_echo).
    page_graphe_preparer();
    ui_musique_brancher(); // 24/10 : la musique parle directement à Music Assistant
    ui_demo_brancher_ma(); // en démo : Music Assistant simulé, réponses embarquées (voir ui_demo.cpp)
    ui_mqtt_brancher();    // commandes des pages branchées + garde
    ui_demo_lancer();
    ui_diag_ram_etape("mode démo");
#else
    donnees_fictives();
    page_graphe_preparer();
    historique_fictif();
    ui_diag_ram_etape("thème + données"); // 28/10 : AUDIT RAM
    th_commande = commande_fictive;
    // 22/10/2026 : thermostats et extérieur RÉELS par le contrat dashboard_guition/ (les retenus remplacent les
    // données fictives dès la connexion) ; les boutons de mode et l'anneau commandent VRAIMENT le chauffage.
    ui_mqtt_demarrer(piece());
    ui_diag_ram_etape("ui_mqtt_demarrer"); // 28/10 : AUDIT RAM
    ui_musique_brancher(); // 24/10 : la musique parle directement à Music Assistant
#endif
    ui_diag_ram_etape("ui_musique_brancher"); // 28/10 : AUDIT RAM
    // 24/10 : réglages de l'écran (mémoire flash, publiés pour HA), santé, rétroéclairage jour / nuit / veille
    // Arduino 3 : la broche reste à LovyanGFX (Light_PWM, 160 Hz, réglé dans LGFX_…ST7701_GT911.h) : un 2e ledcAttach
    // échoue (« already attached ») ; luminosité 0-255 comme avant
    tft.setBrightness(0); // éteint : ui_appareil monte en fondu (comme la production) ; AVANT ui_appareil_demarrer, qui lance ce fondu
    // 26/10 : réveil court : rien d'éclairé avant que l'écran soit prêt (ui_appareil_rallumer fait alors le fondu)
    ui_appareil_lumiere = [](int valeur) { if (!g_court || g_pret) tft.setBrightness(valeur); };
    ui_appareil_demarrer(piece());
    ui_diag_ram_etape("ui_appareil_demarrer"); // 28/10 : AUDIT RAM
    const int nb_pages = sizeof(pages) / sizeof(pages[0]) - (g_ecran && g_ecran->batterie ? 0 : 1);
    ui_shell_creer(pages, nb_pages, 0);
    ui_diag_ram_etape("ui_shell_creer (pages)"); // 28/10 : AUDIT RAM
    if (g_ecran && g_ecran->batterie)
        ui_batterie_icone = [](int niveau, uint32_t couleur)
        {
            static const lv_img_dsc_t *const PILES[] = {&icon_bat0, &icon_bat1, &icon_bat2, &icon_bat3};
            ui_shell_icone(sizeof(pages) / sizeof(pages[0]) - 1, PILES[niveau], couleur);
        };
    if (!g_court) // 26/10 : pas d'écran de démarrage au réveil (retour d'usage) : la page principale directement
#ifdef MODE_DEMO
        ui_demarrage_creer(TXT("Mode démo", "Demo mode")); // 10/11 : le démarrage le dit ; demarrage_tache l'efface en ~1,5 s
#else
        ui_demarrage_creer(th_pieces[LV_CLAMP(0, rg_reglages.piece, TH_NB_PIECES - 1)].nom); // 24/10 : comme la production
#endif


#ifdef MODE_DEMO
    // MODE DÉMO : pas de Wi-Fi, pas d'OTA, pas de banc réseau — rien ne cherche le réseau (voir aussi loop()).
#else
    // Wi-Fi + OTA, même IP fixe que l'écran pour reflasher la production par le même chemin
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // sinon l'invitation OTA se perd (18/10)
    ui_diag_ram_etape("WiFi.mode STA"); // 28/10 : AUDIT RAM
    // 27/10 : la raison de la dernière déconnexion (gardée en RTC pour la ligne du démarrage suivant)
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info)
                 { if (g_wifi_rtc.magic == WIFI_RTC_MAGIC) g_wifi_rtc.raison = info.wifi_sta_disconnected.reason; },
                 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    if (g_wifi_rtc.magic == WIFI_RTC_MAGIC && g_wifi_rtc.echecs) // échecs avant ce démarrage : dans la ligne du journal
    {
        char vise[16] = TXT("borne gardée", "held pin");
        if (g_wifi_rtc.rssi_vise)
            snprintf(vise, sizeof vise, "%d dBm", (int)g_wifi_rtc.rssi_vise);
        snprintf(ui_appareil_reveil_info + strlen(ui_appareil_reveil_info), sizeof ui_appareil_reveil_info - strlen(ui_appareil_reveil_info),
                 TXT(" · ⚠️ Wi-Fi : %u échec%s avant (dernier : raison %u, %s, réveil %s)", " · ⚠️ Wi-Fi: %u failure%s before (last: reason %u, %s, wake-up %s)"),
                 g_wifi_rtc.echecs, g_wifi_rtc.echecs > 1 ? "s" : "",
                 g_wifi_rtc.raison, vise, g_wifi_rtc.court ? TXT("court", "short") : TXT("complet", "full"));
    }
    if (g_wifi_rtc.magic == WIFI_RTC_MAGIC)
        g_wifi_rtc.raison = 0;
    g_vite = g_court && wifi_connecter_vite();
    if (!g_vite)
        wifi_connecter();
    ui_diag_ram_etape("wifi_connecter"); // 28/10 : AUDIT RAM
    // 02/11 : sans mDNS : personne ne s'en sert (flash par l'IP, HA par MQTT) ; les 4 écrans s'y annonçaient sous le même nom
    // tous sous le même nom d'annonce, et il coûtait ~6 Ko de RAM interne (tâche mdns : pile de 4 352 octets ; ArduinoOTA.begin)
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.setTimeout(15000); // défaut 1 s sans données = abandon (21/10 : trop court à travers la 4G)
    ArduinoOTA.onStart([]()
                       { tft.setBrightness(0); lv_timer_enable(false); ui_mqtt_quitter(); }); // 24/10 : session MQTT libérée
    // 25/10 : comme la production (ota_fonctions.h) : chien de garde nourri pendant l'OTA. Elle se déroule tout entière dans
    // ArduinoOTA.handle() ; vers Chambre 1 (Wi-Fi lent) elle dure 22 à 29 s, plus que les 10 s du chien de garde de la boucle
    ArduinoOTA.onProgress([](unsigned int, unsigned int) { esp_task_wdt_reset(); });
    ArduinoOTA.begin();
    ui_diag_ram_etape("ArduinoOTA.begin"); // 28/10 : AUDIT RAM
    // 24/10 : capture d'écran (relue dans la dalle à la demande) et appuis simulés depuis le PC (port 8080)
    banc_demarrer([](int y, int n, uint16_t *px)
                  {
                      if (g_double) // 27/10 : la mémoire montrée (Lovyan ne lit que la première)
                      {
                          const int i = tft._bus_instance.shownFrameBuffer();
                          for (int r = 0; r < n; r++)
                              memcpy(px + r * 480, g_lignes_fb[i][y + r], 480 * 2);
                      }
                      else
                          tft.readRect(0, y, 480, n, (lgfx::swap565_t *)px);
                  });
    ui_diag_ram_etape("banc (fin du setup)"); // 28/10 : AUDIT RAM
#endif // MODE_DEMO (Wi-Fi + OTA + banc)
}

// 24/10 (retour d'usage : « une synchro au démarrage dans le splash screen ») : l'écran de démarrage suit les étapes, comme la
// production (Wi-Fi, MQTT, synchronisation), plus la bibliothèque musicale lue en fond ; il s'efface quand tout est là.
// Pas de redémarrage sur échec (règle de ce projet) : passé le délai, l'interface s'ouvre et l'étape manquante est dite.
static void demarrage_tache()
{
    if (!ui_demarrage_affiche())
        return;
#ifdef MODE_DEMO
    // MODE DÉMO : pas de Wi-Fi ni de MQTT à attendre — l'écran de démarrage dit « Mode démo » et s'efface vite.
    if (millis() < 1500)
    {
        ui_demarrage_etape(TXT("Mode démo : données embarquées, pas de réseau", "Demo mode: embedded data, no network"), 60);
        return;
    }
    ui_demarrage_etape(TXT("Mode démo", "Demo mode"), 100);
    ui_trace("[demarrage] mode démo : interface en %lu ms\n", (unsigned long)millis());
    trace_ram();
    ui_demarrage_fin();
    g_interface_prete_ms = millis();
    ui_diag_ram_etape("interface prête (démo)"); // 28/10 : AUDIT RAM
    return;
#endif
    static uint32_t t_mqtt = 0, t_biblio = 0;
    const uint32_t t = millis();
    char b[96];
    const char *manque = nullptr;
    if (!WiFi.isConnected())
    {
        ui_demarrage_etape(TXT("Wi-Fi : connexion à la borne…", "Wi-Fi: connecting…"), 20);
        if (t < 30000)
            return;
        manque = TXT("sans Wi-Fi", "no Wi-Fi");
    }
    else if (!ui_mqtt_connecte())
    {
        snprintf(b, sizeof b, TXT("Wi-Fi %s · %d dBm\nMQTT : connexion…", "Wi-Fi %s · %d dBm\nMQTT: connecting…"), WiFi.SSID().c_str(), (int)WiFi.RSSI());
        ui_demarrage_etape(b, 45);
        if (t < 30000)
            return;
        manque = TXT("sans MQTT", "no MQTT");
    }
    else
    {
        if (!t_mqtt)
            t_mqtt = t;
        // les messages retenus arrivent en rafale juste après l'abonnement : on laisse passer la rafale
        if (!ui_mqtt_dernier_ha || t - t_mqtt < 1500)
        {
            ui_demarrage_etape(TXT("Synchronisation avec Home Assistant…", "Synchronizing with Home Assistant…"), 65);
            if (t - t_mqtt < 15000)
                return;
            manque = TXT("Home Assistant ne répond pas", "Home Assistant not responding");
        }
        else
        {
            if (!t_biblio)
            {
                t_biblio = t;
                if (!ui_musique_a_la_demande) // 26/10 : sur batterie, lue à l'ouverture de la liste seulement
                    ui_musique_precharger();
            }
            if (!ui_musique_biblio_prete() && t - t_biblio < 10000)
            {
                ui_demarrage_etape(TXT("Bibliothèque musicale…", "Music library…"), 85);
                return;
            }
        }
    }
    ui_demarrage_etape(manque ? manque : TXT("Prêt", "Ready"), 100);
    ui_trace("[demarrage] interface en %lu ms%s%s\n", (unsigned long)t, manque ? " : " : "", manque ? manque : "");
    trace_ram();
    ui_demarrage_fin();
    g_interface_prete_ms = millis();
    ui_diag_ram_etape("interface prête"); // 28/10 : AUDIT RAM
}

void loop()
{
#ifdef BILAN_RENDU_S
    // 04/11 : banc du GLISSE sur l'anneau du thermostat, une fois, 5 s apres la fin de l'ecran de demarrage (avant :
    // l'ecran de demarrage couvre la page, LVGL ne la dessinerait pas). Chaque pas = ce que fait arc_cb quand le doigt
    // bouge (bouton deplace + consigne + rafraichir), SANS commande vers HA ; l'image est forcee (lv_refr_now) et
    // chronometree. La piece affichee est remise a son etat a la fin (les donnees MQTT suivantes l'ecrasent de toute facon).
    static bool banc_fait = false;
    if (!banc_fait && g_interface_prete_ms && millis() - g_interface_prete_ms > 5000)
    {
        banc_fait = true;
        if (g_verrou_ecran) // comme la boucle autour de lv_timer_handler
            xSemaphoreTake(g_verrou_ecran, portMAX_DELAY);
        tft.startWrite();
        ui_shell_aller(0);
        th_piece_t &p = th_pieces[th_piece_affichee];
        const th_piece_t garde = p;
        auto image = [&]()
        {
            while (g_double && (tft._bus_instance.swapPending() || g_lvgl_prete >= 0))
            {
                if (g_lvgl_prete >= 0 && !tft._bus_instance.swapPending())
                {
                    tft._bus_instance.showFrameBuffer(g_lvgl_prete);
                    g_lvgl_prete = -1;
                }
                delay(1);
            }
            g_banc_px = 0;
            const uint32_t t0 = micros();
            lv_refr_now(NULL);
            return micros() - t0;
        };
        p.mode = TH_ECO, p.consigne = 16, p.temp = 16.4f, p.chauffe = false;
        page_thermostat_maj();
        image();
        lv_obj_invalidate(lv_scr_act());
        uint32_t t = image();
        ui_trace("[banc glisse] page entiere : %u us, %u px\n", (unsigned)t, (unsigned)g_banc_px);
        uint32_t pas[10], somme = 0;
        for (int i = 0; i < 10; i++)
        {
            esp_task_wdt_reset();
            page_thermostat_banc_glisse(16.5f + 0.5f * i, false);
            pas[i] = image();
            somme += pas[i];
            ui_trace("[banc glisse] pas %.1f : %u us, %u px\n", 16.5f + 0.5f * i, (unsigned)pas[i], (unsigned)g_banc_px);
        }
        page_thermostat_banc_glisse(0, true);
        t = image();
        ui_trace("[banc glisse] lacher : %u us, %u px\n", (unsigned)t, (unsigned)g_banc_px);
        std::sort(pas, pas + 10);
        ui_trace("[banc glisse] BILAN pas de 0,5 : moyenne %u us, mediane %u us, max %u us\n", (unsigned)(somme / 10),
                 (unsigned)pas[5], (unsigned)pas[9]);
        p = garde;
        page_thermostat_maj();
        tft.endWrite();
        if (g_verrou_ecran)
            xSemaphoreGive(g_verrou_ecran);
    }
#endif
    ui_batterie_tache(); // 26/10 : sans effet sur secteur
    ui_radar_tache();
    ui_camera_tache(); // 26/10 : caméras (imprimante, sonnette)
    reveil_tache();
    energie_tache();
    synchro_tache();
    if (WiFi.isConnected())
        ui_radar_web_demarrer(); // page web du radar (port 80), une fois
    // 25/10 : RAM interne : une trace chaque fois que le plus bas niveau depuis le démarrage descend de 2 Ko ou plus
    static uint32_t ram_verif = 0;
    if (millis() - ram_verif >= (millis() < 60000 ? 250u : 10000u)) // la 1re minute : serré (démarrage, bibliothèque)
    {
        ram_verif = millis();
        if (heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) + 2048 <= g_ram_plus_bas_signale)
            trace_ram();
    }

    { // 28/10 : AUDIT RAM : étapes de la boucle
        static bool w = false, m = false, m1 = false, m10 = false, h1 = false;
        if (!w && WiFi.isConnected()) { w = true; ui_diag_ram_etape("Wi-Fi connecté"); }
        if (!m && ui_mqtt_connecte()) { m = true; ui_diag_ram_etape("MQTT connecté"); }
        if (!m1 && millis() > 60000) { m1 = true; ui_diag_ram_etape("1 min"); }
        if (!m10 && millis() > 600000) { m10 = true; ui_diag_ram_etape("10 min"); }
        if (!h1 && millis() > 3600000) { h1 = true; ui_diag_ram_etape("1 h"); }
    }
    { // 28/10 : remède à l'écran « sourd » (gardé : relance du Wi-Fi)
        // 28/10 : chambre 1 « sourde » (MQTT fermé par l'écran faute de réponse, Wi-Fi affiché connecté, plus de ping) :
        // la RAM au moment de la perte, puis au bout de 2 min le Wi-Fi est relancé (nouvelle association, nouvelles clés) :
        // si MQTT revient, l'association était en cause ; la RAM notée dit si elle était épuisée
        static uint32_t sourd = 0;
        static bool m = false; // MQTT connecté au moins une fois
        m = m || ui_mqtt_connecte();
        if (m && WiFi.isConnected() && !ui_mqtt_connecte() && !g_rallume_ms) // 31/10 : pas pendant un rallumage voulu
        {
            if (!sourd)
            {
                sourd = millis();
                ui_trace("[sourd] MQTT perdu, Wi-Fi connecté (%d dBm) ; RAM interne libre %u, plus gros bloc %u\n", (int)WiFi.RSSI(),
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
                ui_diag_ram_etape("MQTT perdu (Wi-Fi connecté)");
            }
            else if (millis() - sourd > 120000)
            {
                ui_trace("[sourd] 2 min sans MQTT, Wi-Fi affiché connecté : relance du Wi-Fi\n");
                ui_diag_ram_etape("relance du Wi-Fi");
                wifi_connecter();
                sourd = millis();
            }
        }
        else if (sourd && ui_mqtt_connecte())
        {
            ui_trace("[sourd] MQTT revenu après %lu s\n", (unsigned long)((millis() - sourd) / 1000));
            ui_diag_ram_etape("MQTT revenu");
            sourd = 0;
        }
    }
    ui_diag_tache();
    ui_diag_etape("ota");
#ifndef MODE_DEMO
    ArduinoOTA.handle();
#endif

    // 19/10 : essai d'un redémarrage logiciel en mode téléchargement (pour flasher par le port série du routeur)
    // ABANDONNÉ : la ROM de cette carte plante en « waiting for download » (LoadProhibited) même après reset système,
    // drapeaux USB à 0 et GPIO19/20 remis à zéro. Le reset par RTS à distance fonctionne, pas l'entrée en téléchargement.

#ifndef MODE_DEMO
    // Wi-Fi perdu depuis 30 s : on recommence (scan : la borne la plus forte a pu changer)
    static uint32_t hors_ligne_depuis = 0;
    if (WiFi.isConnected() || g_wifi_coupe) // 31/10 : coupé exprès : pas de nouvel essai
        hors_ligne_depuis = 0;
    else if (!hors_ligne_depuis)
        hors_ligne_depuis = millis();
    else if (millis() - hors_ligne_depuis > 30000)
    {
        wifi_connecter();
        hors_ligne_depuis = millis();
    }
    ui_diag_etape("wifi");
    wifi_scan_tache(); // 02/11 : scans asynchrones
    wifi_surveiller();
#endif
    ui_diag_etape("shell");
    ui_shell_tache();
    ui_diag_etape("mqtt");
    ui_mqtt_tache();
    ui_diag_etape("banc");
#ifndef MODE_DEMO
    banc_tache();
#endif
    ui_diag_etape("demarrage");
    demarrage_tache();

    static uint32_t dernier = 0;
    // 27/10 : deux mémoires d'image : les bascules se font ici, entre deux passages de LVGL (jamais au milieu d'un dessin)
    if (g_verrou_ecran)
        xSemaphoreTake(g_verrou_ecran, portMAX_DELAY);
    lv_area_t z_cam;
    if (g_double && !tft._bus_instance.swapPending())
    {
        if (g_cam_prete >= 0) // une image de caméra complète dans la mémoire cachée (LVGL y a peut-être dessiné aussi)
            montrer_image_camera(g_cam_prete);
        else if (g_lvgl_prete >= 0 && (millis() - g_lvgl_prete_ms >= LVGL_ATTENTE_CAMERA_MS || !ui_camera_zone_directe(&z_cam)))
        // la caméra cale, ou elle n'écrit plus (pause : rail touché, 27/10 : LVGL attendait sinon 60-70 ms, le rail sautait)
        {
            tft._bus_instance.showFrameBuffer(g_lvgl_prete);
            g_lvgl_prete = -1;
        }
    }
    if (millis() - dernier >= 5 && !(g_double && (tft._bus_instance.swapPending() || g_lvgl_prete >= 0))) // 27/10 : bascule en attente
    {
        dernier = millis();
        ui_diag_etape("lvgl");
        ui_diag_point("th>"); // 06/11 diagnostic : si le dump dit « th> », la mort est dans lv_timer_handler avant la fin du premier aire
        tft.startWrite();
        lv_timer_handler();
        tft.endWrite();
        ui_diag_point("th<");
    }
    if (g_verrou_ecran)
        xSemaphoreGive(g_verrou_ecran);
    // 26/10 : la boucle laisse la main 1 ms par tour : sans ça, elle tournait sans arrêt et partageait le cœur 1 avec les
    // tâches de même priorité (décodage de la caméra : 81 -> 76 ms mesuré en A/B, 37 Ko par image) ; LVGL reste appelé
    // toutes les 5 ms, le reste chaque milliseconde
    delay(1);
}
