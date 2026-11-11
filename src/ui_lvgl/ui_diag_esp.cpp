// 24/10/2026 : diagnostic des blocages sur l'ESP32 (voir ui_diag.h). Le simulateur a sa version dans ui_diag.cpp.
#ifdef ARDUINO
#include "ui_diag.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include <Arduino.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_rom_sys.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#define ANNEAU 3072
#define MAGIC 0x44494149u // « DIAG » : le contenu de la mémoire RTC vient bien de nous
#define LENT_MS 500       // une étape plus longue laisse une trace
#define CHIEN_S 10        // chien de garde de la boucle (celui commenté dans le setup.h de la production)

// Mémoire RTC non initialisée : survit à un redémarrage par plantage ou chien de garde (pas à une coupure de courant)
struct rtc_t
{
    uint32_t magic, pos, boucle_max;
    uint32_t chien; // 1 : redémarrage décidé par nous, boucle figée (chien de garde des tâches)
    uint32_t etape_debut; // millis() au début de l'étape en cours
    char etape[48];
    char point[24]; // repère fin dans l'étape (ui_diag_point)
    char anneau[ANNEAU]; // dernières traces, en anneau
};
RTC_NOINIT_ATTR static rtc_t g_rtc;

#define DUMP (ANNEAU + 200)
static UI_PSRAM(char, g_dump, DUMP, nullptr); // le plantage précédent, mis en forme au démarrage (29/10 : PSRAM)
static char g_etape_precedente[48];
static bool g_dump_present = false, g_dump_publie = false;
static char g_cause[40];
static WiFiServer g_telnet(23);
static WiFiClient g_client;

static void anneau_ecrire(const char *s)
{
    for (; *s; s++)
    {
        g_rtc.anneau[g_rtc.pos] = *s;
        g_rtc.pos = (g_rtc.pos + 1) % ANNEAU;
    }
}

// Sorties « ROM » (esp_rom_printf : chien de garde, piles d'appels) : dans l'anneau, le port série de la production
// n'étant branché nulle part (24/10 : « la prod n'a pas de série »)
static void rom_putc(char c)
{
    if (c == '\r')
        return;
    g_rtc.anneau[g_rtc.pos] = c;
    g_rtc.pos = (g_rtc.pos + 1) % ANNEAU;
}

// Chien de garde des tâches sans plantage immédiat : au 1er déclenchement, l'IDF imprime (après ce crochet) les piles
// d'appels des deux cœurs, qui partent dans l'anneau ; au 2e, la boucle est toujours figée : on redémarre.
static volatile uint8_t g_chien = 0;
extern "C" void esp_task_wdt_isr_user_handler(void)
{
    g_chien = g_chien + 1;
    if (g_chien < 2)
        return;
    g_rtc.chien = 1;
    abort();
}

// 24/10 : une trace venue d'une autre tâche (lecture de la bibliothèque musicale en fond) attend ici que la boucle la
// reprenne (ui_diag_tache) : l'anneau, le port série et le Telnet ne sont touchés que par la boucle
static TaskHandle_t g_principale = nullptr;
static portMUX_TYPE g_verrou = portMUX_INITIALIZER_UNLOCKED;
static char g_attente[512];
static size_t g_nb_attente = 0;

static void trace_sortir(const char *b);

void ui_trace(const char *fmt, ...)
{
    char b[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    if (g_principale && xTaskGetCurrentTaskHandle() != g_principale)
    {
        portENTER_CRITICAL(&g_verrou);
        const size_t n = strnlen(b, sizeof g_attente - 1 - g_nb_attente);
        memcpy(g_attente + g_nb_attente, b, n);
        g_nb_attente += n;
        g_attente[g_nb_attente] = 0;
        portEXIT_CRITICAL(&g_verrou);
        return;
    }
    trace_sortir(b);
}

static void trace_sortir(const char *b)
{
    Serial.print(b);
    anneau_ecrire(b);
    if (g_client && g_client.connected())
        g_client.print(b);
}

void ui_diag_etape(const char *etape)
{
    const uint32_t maintenant = millis(), duree = maintenant - g_rtc.etape_debut;
    if (duree > LENT_MS && g_rtc.etape[0])
        ui_trace("[lent] %s : %u ms\n", g_rtc.etape, (unsigned)duree);
    snprintf(g_rtc.etape, sizeof g_rtc.etape, "%s", etape);
    g_rtc.point[0] = 0;
    g_rtc.etape_debut = maintenant;
}

void ui_diag_point(const char *point) { strlcpy(g_rtc.point, point, sizeof g_rtc.point); }

const char *ui_diag_cause() { return g_cause; }
uint32_t ui_diag_boucle_max() { return g_rtc.boucle_max; }
const char *ui_diag_etape_precedente() { return g_etape_precedente; }

static const char *cause(esp_reset_reason_t r, bool *anormal)
{
    *anormal = true;
    switch (r)
    {
    case ESP_RST_PANIC: return "plantage";
    case ESP_RST_INT_WDT: return "chien de garde des interruptions";
    case ESP_RST_TASK_WDT: return "chien de garde des tâches";
    case ESP_RST_WDT: return "chien de garde";
    case ESP_RST_BROWNOUT: return "baisse de tension";
    default: break;
    }
    *anormal = false;
    switch (r)
    {
    case ESP_RST_POWERON: return "mise sous tension";
    case ESP_RST_SW: return "demandé";
    case ESP_RST_EXT: return "bouton reset";
    case ESP_RST_DEEPSLEEP: return "réveil";
    default: return "autre";
    }
}

void ui_diag_demarrer()
{
    bool anormal;
    snprintf(g_cause, sizeof g_cause, "%s", cause(esp_reset_reason(), &anormal));
    if (anormal && g_rtc.magic == MAGIC && g_rtc.chien)
        snprintf(g_cause, sizeof g_cause, "boucle figée (chien de garde)");
    if (anormal && g_rtc.magic == MAGIC && g_rtc.pos < ANNEAU) // ce qui précédait la chute : à garder avant d'effacer
    {
        snprintf(g_etape_precedente, sizeof g_etape_precedente, "%.23s/%.23s", g_rtc.etape, g_rtc.point);
        int n = snprintf(g_dump, DUMP, "Redémarrage : %s\nÉtape en cours : %s (repère : %s)\nBoucle la plus lente : %u ms\n--- dernières traces ---\n",
                         g_cause, g_rtc.etape[0] ? g_rtc.etape : "?", g_rtc.point[0] ? g_rtc.point : "-", (unsigned)g_rtc.boucle_max);
        for (uint32_t k = 0; k < ANNEAU && n < (int)DUMP - 1; k++) // de la plus ancienne à la plus récente
        {
            const char c = g_rtc.anneau[(g_rtc.pos + k) % ANNEAU];
            if (c)
                g_dump[n++] = c;
        }
        g_dump[n] = 0;
        g_dump_present = true;
        Serial.printf("\n===== DUMP DU PLANTAGE PRÉCÉDENT =====\n%s\n======================================\n", g_dump);
    }
    memset(&g_rtc, 0, sizeof g_rtc);
    g_rtc.magic = MAGIC;
    g_principale = xTaskGetCurrentTaskHandle(); // setup() et loop() : la même tâche (loopTask)
    esp_rom_install_channel_putc(1, rom_putc);
    // avertissements, erreurs, assertions de LVGL ; sauf « Style might be already inited » : 21 fois au démarrage, le thème
    // par défaut réinitialisé par ui_theme_init (LVGL l'a déjà fait à l'enregistrement de l'écran), sans suite
    lv_log_register_print_cb([](lv_log_level_t niveau, const char *texte)
                             {
                                 (void)niveau;
                                 if (!strstr(texte, "already inited")) ui_trace("[lvgl] %s", texte);
                             });
    // reconfigure celui du framework (5 s, plantage) : 10 s, sans plantage (voir le crochet plus haut), tâche inactive du
    // cœur 0 toujours surveillée (comme le framework) ; IDF 5 : reconfigure, init seulement s'il n'existe pas encore
    esp_task_wdt_config_t chien = {};
    chien.timeout_ms = CHIEN_S * 1000;
    chien.idle_core_mask = 1 << 0;
    chien.trigger_panic = false;
    if (esp_task_wdt_reconfigure(&chien) != ESP_OK)
        esp_task_wdt_init(&chien);
    esp_task_wdt_add(NULL);            // la tâche de loop() : figée trop longtemps = piles d'appels gardées puis redémarrage
    ui_trace("[diag] démarrage : %s%s\n", g_cause, g_dump_present ? " (dump du plantage gardé)" : "");
}

size_t ui_diag_traces(char *dst, size_t n)
{
    size_t k = 0;
    for (uint32_t i = 0; i < ANNEAU && k + 1 < n; i++)
    {
        const char c = g_rtc.anneau[(g_rtc.pos + i) % ANNEAU];
        if (c)
            dst[k++] = c;
    }
    if (n)
        dst[k] = 0;
    return k;
}

void ui_diag_traces_effacer()
{
    memset(g_rtc.anneau, 0, ANNEAU);
    g_rtc.pos = 0;
}

void ui_diag_tache()
{
    static uint32_t precedent = 0;
    const uint32_t maintenant = millis();
    if (precedent)
    {
        const uint32_t boucle = maintenant - precedent;
        if (boucle > g_rtc.boucle_max)
            g_rtc.boucle_max = boucle;
        if (boucle > 1000)
            ui_trace("[boucle] %u ms (étape %s)\n", (unsigned)boucle, g_rtc.etape);
    }
    precedent = maintenant;
    if (g_nb_attente) // traces des autres tâches
    {
        char b[sizeof g_attente];
        portENTER_CRITICAL(&g_verrou);
        memcpy(b, g_attente, g_nb_attente + 1);
        g_nb_attente = 0;
        portEXIT_CRITICAL(&g_verrou);
        trace_sortir(b);
    }
    esp_task_wdt_reset();
    g_chien = 0;

    static bool telnet_ouvert = false;
    if (!telnet_ouvert) // seulement Wi-Fi connecté : avant, la pile réseau n'existe pas (assert « Invalid mbox », vu le 24/10)
    {
        if (!WiFi.isConnected())
            return;
        g_telnet.begin();
        g_telnet.setNoDelay(true);
        telnet_ouvert = true;
    }
    if (g_telnet.hasClient()) // un seul client, comme la production
    {
        if (g_client)
            g_client.stop();
        g_client = g_telnet.accept(); // Arduino 3 : available() renommé
        g_client.printf("\n--- journal de l'écran (%s) ; « dump » = le plantage précédent, « traces » = la session en cours, « figer » = essai du chien de garde ---\n", g_cause);
        if (g_dump_present)
            g_client.print("(un dump du plantage précédent est gardé)\n");
    }
    static char cmd[16];
    static int nc = 0;
    void ram_afficher();
    while (g_client && g_client.connected() && g_client.available())
    {
        const char c = g_client.read();
        if (c != '\n' && c != '\r')
        {
            if (nc < (int)sizeof cmd - 1)
                cmd[nc++] = c;
            continue;
        }
        cmd[nc] = 0;
        nc = 0;
        if (!strcmp(cmd, "figer")) // essai de l'instrument : boucle figée exprès -> piles d'appels, redémarrage, dump
        {
            ui_trace("[diag] figeage volontaire (commande figer)\n");
            for (;;)
                ;
        }
        else if (!strcmp(cmd, "dump"))
            g_client.print(g_dump_present ? g_dump : "pas de plantage depuis la mise sous tension\n");
        else if (!strcmp(cmd, "traces")) // les dernières traces de la session en cours (l'anneau RTC)
        {
            g_client.printf("étape : %s (repère : %s) ; boucle la plus lente : %u ms\n", g_rtc.etape, g_rtc.point, (unsigned)g_rtc.boucle_max);
            static char *const texte = (char *)ui_psram_calloc(1, ANNEAU + 1); // 29/10 : PSRAM ; d'un bloc : octet par octet, l'envoi était tronqué (vu le 24/10)
            g_client.write(texte, ui_diag_traces(texte, ANNEAU + 1));
        }
        else if (!strcmp(cmd, "ram"))
            ram_afficher();
    }
}

// 28/10 : AUDIT RAM (ui_diag.h). Les textes sont en PSRAM : l'instrument ne prend pas de RAM interne.
static char *g_ram_etapes = nullptr;
#define RAM_ETAPES 2048
void ui_diag_ram_etape(const char *etape)
{
    if (!g_ram_etapes && (g_ram_etapes = (char *)heap_caps_calloc(1, RAM_ETAPES, MALLOC_CAP_SPIRAM)) == nullptr)
        return;
    const size_t l = strlen(g_ram_etapes);
    snprintf(g_ram_etapes + l, RAM_ETAPES - l, "%7lu ms  %-26s libre %6u  plus bas %6u  plus gros bloc %6u\n", (unsigned long)millis(), etape,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// blocs de la RAM interne, relevés d'abord (rien d'écrit ni d'alloué pendant le parcours : le tas est verrouillé)
struct ram_bloc_t
{
    void *p;
    uint32_t taille;
};
#define RAM_BLOCS 128
static ram_bloc_t *g_blocs;
static int g_nb_blocs;
static uint32_t g_petits_n, g_petits_o, g_libres_n, g_libres_o, g_debordes;
static bool ram_marcher(walker_heap_into_t, walker_block_info_t b, void *)
{
    if (!b.used)
    {
        g_libres_n++;
        g_libres_o += b.size;
    }
    else if (b.size < 1024)
    {
        g_petits_n++;
        g_petits_o += b.size;
    }
    else if (g_nb_blocs < RAM_BLOCS)
        g_blocs[g_nb_blocs++] = {b.ptr, (uint32_t)b.size};
    else
        g_debordes += b.size;
    return true;
}

size_t ui_camera_etat_connexions(char *s, size_t n); // ui_camera_esp.cpp

// Marche du tas PSRAM (blocs >= 64 Ko, nombre de trous, plus grand bloc) : « 1,48 Mo libres » ne dit pas
// qu'il ne reste aucun bloc contigu de 460 800 o. Seuil haut volontaire : la PSRAM contient des milliers
// de petits blocs LVGL.
static ram_bloc_t *g_blocs_p;
static int g_nb_blocs_p;
static uint32_t g_p_gas_n, g_p_gas_o, g_p_trou_max, g_p_debordes, g_p_utilise;
#define RAM_BLOCS_P 48
static bool ram_marcher_psram(walker_heap_into_t, walker_block_info_t b, void *)
{
    if (!b.used)
    {
        g_p_gas_n++;
        g_p_gas_o += b.size;
        if (b.size > g_p_trou_max)
            g_p_trou_max = b.size;
    }
    else
    {
        g_p_utilise += b.size;
        if (b.size >= 65536)
        {
            if (g_nb_blocs_p < RAM_BLOCS_P)
                g_blocs_p[g_nb_blocs_p++] = {b.ptr, (uint32_t)b.size};
            else
                g_p_debordes += b.size;
        }
    }
    return true;
}

void ram_afficher()
{
    const size_t TAILLE = 20480; // 07/11 : 12 288 ne suffisait plus depuis l'ajout de la marche PSRAM (rapport tronqué)
    char *s = (char *)heap_caps_malloc(TAILLE, MALLOC_CAP_SPIRAM);
    static TaskStatus_t *t = nullptr;
    if (!t)
        t = (TaskStatus_t *)heap_caps_malloc(40 * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
    if (!g_blocs)
        g_blocs = (ram_bloc_t *)heap_caps_malloc(RAM_BLOCS * sizeof(ram_bloc_t), MALLOC_CAP_SPIRAM);
    if (!g_blocs_p)
        g_blocs_p = (ram_bloc_t *)heap_caps_malloc(RAM_BLOCS_P * sizeof(ram_bloc_t), MALLOC_CAP_SPIRAM);
    if (!s || !t || !g_blocs || !g_blocs_p)
    {
        free(s);
        g_client.print("ram : pas de PSRAM pour le rapport\n");
        return;
    }
    size_t l = 0;
#define AJOUTE(...) l += snprintf(s + l, l < TAILLE ? TAILLE - l : 0, __VA_ARGS__)
    AJOUTE("=== RAM interne : libre %u, plus bas %u, plus gros bloc %u, total %u ; PSRAM libre %u, plus gros bloc %u, total %u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    AJOUTE("=== étapes (octets) :\n%s", g_ram_etapes ? g_ram_etapes : "");
    l += ui_camera_etat_connexions(s + l, l < TAILLE ? TAILLE - l : 0); // 29/10
    const UBaseType_t n = uxTaskGetSystemState(t, 40, nullptr);
    g_nb_blocs = 0;
    g_petits_n = g_petits_o = g_libres_n = g_libres_o = g_debordes = 0;
    heap_caps_walk(MALLOC_CAP_INTERNAL, ram_marcher, nullptr);
    AJOUTE("=== tâches : nom, cœur, priorité, pile (octets), pile jamais utilisée, où\n");
    uint32_t piles_internes = 0;
    for (UBaseType_t i = 0; i < n; i++)
    {
        uint32_t taille = 0;
        for (int b = 0; b < g_nb_blocs; b++)
            if ((uint8_t *)t[i].pxStackBase >= (uint8_t *)g_blocs[b].p && (uint8_t *)t[i].pxStackBase < (uint8_t *)g_blocs[b].p + g_blocs[b].taille)
                taille = g_blocs[b].taille;
        const bool interne = esp_ptr_internal(t[i].pxStackBase);
        if (interne)
            piles_internes += taille;
        AJOUTE("%-16s %2d %2u %6u %6u  %s\n", t[i].pcTaskName, (int)t[i].xCoreID, (unsigned)t[i].uxCurrentPriority, (unsigned)taille,
               (unsigned)t[i].usStackHighWaterMark, interne ? (taille ? "interne (tas)" : "interne (hors tas)") : "PSRAM");
    }
    AJOUTE("piles en RAM interne (tas) : %u octets\n", (unsigned)piles_internes);
    AJOUTE("=== blocs alloués >= 1 Ko en RAM interne : adresse, taille, propriétaire connu\n");
    uint32_t gros = 0;
    for (int b = 0; b < g_nb_blocs; b++)
    {
        const char *qui = "";
        for (UBaseType_t i = 0; i < n; i++)
            if ((uint8_t *)t[i].pxStackBase >= (uint8_t *)g_blocs[b].p && (uint8_t *)t[i].pxStackBase < (uint8_t *)g_blocs[b].p + g_blocs[b].taille)
                qui = t[i].pcTaskName;
        gros += g_blocs[b].taille;
        AJOUTE("%p %6u %s%s\n", g_blocs[b].p, (unsigned)g_blocs[b].taille, *qui ? "pile " : "", qui);
    }
    AJOUTE("=== total : %d blocs >= 1 Ko = %u ; %u petits blocs = %u ; non listés %u ; libre %u en %u morceaux\n", g_nb_blocs, (unsigned)gros,
           (unsigned)g_petits_n, (unsigned)g_petits_o, (unsigned)g_debordes, (unsigned)g_libres_o, (unsigned)g_libres_n);
    // 07/11 : la même marche côté PSRAM (voir ram_marcher_psram plus haut).
    g_nb_blocs_p = 0;
    g_p_gas_n = g_p_gas_o = g_p_trou_max = g_p_debordes = g_p_utilise = 0;
    heap_caps_walk(MALLOC_CAP_SPIRAM, ram_marcher_psram, nullptr);
    AJOUTE("=== PSRAM : %u o en blocs utilisés ; %u trous libres = %u o ; plus grand trou %u o%s\n", (unsigned)g_p_utilise,
           (unsigned)g_p_gas_n, (unsigned)g_p_gas_o, (unsigned)g_p_trou_max, g_p_trou_max < 460800 ? "  < tampon du banc (460 800 o) : capture impossible" : "");
    for (int b = 0; b < g_nb_blocs_p; b++)
    {
        // 07/11 : amorce du contenu, pour identifier le propriétaire d'un bloc sans backtrace. Si l'on voit
        // « \x89PNG » ou des octets de pixels structurés, c'est le chemin image ; si c'est du texte JSON, MQTT.
        const unsigned char *q = (const unsigned char *)g_blocs_p[b].p;
        char amorce[24];
        int k = 0;
        for (; k < 16; k++)
            amorce[k] = (q[k] >= 32 && q[k] < 127) ? (char)q[k] : '.';
        amorce[k] = 0;
        AJOUTE("%p %7u  « %s »  %02x %02x %02x %02x\n", g_blocs_p[b].p, (unsigned)g_blocs_p[b].taille, amorce, q[0], q[1], q[2], q[3]);
    }
    if (g_p_debordes)
        AJOUTE("(+%u o de blocs >= 64 Ko non listés)\n", (unsigned)g_p_debordes);
#undef AJOUTE
    g_client.write(s, l < TAILLE ? l : TAILLE - 1);
    free(s);
}

void ui_diag_publier(bool (*publier)(const char *sujet, const char *message, bool retenu), const char *nom)
{
    if (!g_dump_present || g_dump_publie || !publier)
        return;
    char sujet[80];
    snprintf(sujet, sizeof sujet, "dashboard_guition/appareil/%s/dump", nom);
    g_dump_publie = publier(sujet, g_dump, true);
}
#endif
