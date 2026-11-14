// 24/10/2026 : plateforme de ui_appareil sur l'ESP32 : mémoire flash (Preferences), santé, redémarrage. Le simulateur PC
// n'a rien de tout ça (ses réglages ne survivent pas). Le rétroéclairage (ui_appareil_lumiere) est branché par le programme.
#ifdef ARDUINO
#include "ui_appareil.h"
#include "ui_mqtt.h"
#include <Arduino.h>
#include "ui_diag.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_ota_ops.h>   // 14/11 : partition d'application en cours d'exécution
#include <esp_image_format.h> // 14/11 : en-têtes de l'image (taille sans tout relire)
#include <sys/time.h>
#include <time.h>
#include "version_git.h" // 02/11 : écrit par version_git.py à la compilation
#include "memoire_brute.h" // 13/11 : réglages en flash sans NVS (glitch de l'image pendant une écriture NVS)

static const char *ESPACE = "guition"; // espace de la mémoire flash (NVS)

static bool charger(void *donnees, size_t taille)
{
    if (mb_lire(MB_ECRAN, donnees, taille)) // 13/11 : mémoire brute d'abord ; NVS seulement pour la migration
        return true;
    Preferences p;
    if (!p.begin(ESPACE, true))
        return false;
    const bool ok = p.getBytesLength("reglages") == taille && p.getBytes("reglages", donnees, taille) == taille;
    p.end();
    if (ok)
        mb_noter(MB_ECRAN, donnees, taille);
    return ok;
}

static void sauver(const void *donnees, size_t taille) { mb_ecrire(MB_ECRAN, donnees, taille); } // 13/11 : plus de NVS

// 14/11 : taille de l'image d'application d'après ses seuls en-têtes, comme esp_image_format.c (process_checksum) :
// en-tête, segments (en-tête 8 o + données), bourrage jusqu'au bloc de 16 o qui finit par la somme, puis le hachage
// SHA-256 (32 o) s'il est ajouté. 0 si l'en-tête n'est pas valide.
static uint32_t taille_image(const esp_partition_t *p)
{
    esp_image_header_t h;
    if (esp_partition_read(p, 0, &h, sizeof h) != ESP_OK || h.magic != ESP_IMAGE_HEADER_MAGIC)
        return 0;
    uint32_t fin = sizeof h;
    for (int i = 0; i < h.segment_count; i++)
    {
        esp_image_segment_header_t sh;
        if (esp_partition_read(p, fin, &sh, sizeof sh) != ESP_OK)
            return 0;
        fin += sizeof sh + sh.data_len;
    }
    fin = (fin + 1 + 15) & ~15u; // la somme (1 o), puis bloc de 16 complet
    if (h.hash_appended)
        fin += 32;
    return fin;
}

static void sante(ui_sante_t &s)
{
    s.depuis_s = millis() / 1000;
    snprintf(s.redemarrage, sizeof s.redemarrage, "%s", ui_diag_cause()); // raison analysée par ui_diag
    s.ram_ko = ESP.getFreeHeap() / 1024;
    s.psram_ko = ESP.getFreePsram() / 1024;
    s.cpu_temp = (int)temperatureRead();
    s.cpu_mhz = getCpuFrequencyMhz();
    int8_t tx = 0;
    s.tx_dbm4 = esp_wifi_get_max_tx_power(&tx) == ESP_OK ? tx : 0; // en quarts de dBm ; erreur = Wi-Fi arrêté
    snprintf(s.version, sizeof s.version, "%s", UI_VERSION); // 02/11 : commit git (version_git.py), au lieu de la date de compilation de ce seul fichier
    if (WiFi.isConnected())
    {
        snprintf(s.wifi, sizeof s.wifi, "%s", WiFi.SSID().c_str());
        snprintf(s.ip, sizeof s.ip, "%s", WiFi.localIP().toString().c_str());
        s.rssi = WiFi.RSSI();
    }
    // 14/11 : mode d'économie Wi-Fi réellement appliqué (le programme le coupe à la connexion :
    // la page doit dire la vérité, pas ce que la config Arduino laisse espérer). Erreur = Wi-Fi arrêté.
    wifi_ps_type_t ps = WIFI_PS_NONE;
    s.wifi_ps = esp_wifi_get_ps(&ps) == ESP_OK ? (int)ps : -1;
    // Place dans la partition d'application active : taille réelle de l'image en cours (checksum et bourrage
    // compris, API Arduino) contre taille de la partition OTA en cours d'exécution.
    // 14/11 : PAS ESP.getSketchSize() : il relit et vérifie toute l'image (~3,5 Mo) ; appelé à chaque rafraîchissement
    // (10 s), il faisait glitcher l'écran toutes les 10 s. La taille se lit dans les en-têtes (~100 octets), une fois.
    static uint32_t app = 0, taille = 0;
    if (!taille)
    {
        const esp_partition_t *part = esp_ota_get_running_partition();
        if (part)
            app = taille_image(part), taille = part->size;
    }
    if (taille)
        s.app_octets = app, s.part_octets = taille;
}

static void effacer_memoire()
{
    mb_oublier(MB_ECRAN); // 13/11 : la mémoire brute, puis l'ancien NVS (sinon il serait relu au démarrage)
    Preferences p;
    if (p.begin(ESPACE, false))
    {
        p.clear();
        p.end();
    }
    ui_mqtt_quitter(); // 24/10 : déconnexion propre (le broker libère la session tout de suite)
    ESP.restart();
}

// l'heure de HA (comme la production : time_fonctions.h sync_time, fuseau de mobile_device.h)
static void regler_heure(uint32_t unix)
{
    struct timeval tv = {(time_t)unix, 0};
    settimeofday(&tv, nullptr);
    ui_trace("[heure] réglée par HA : %u\n", (unsigned)unix);
}

// branché avant le premier appel (constructeur statique : rien à appeler depuis le setup)
static struct branchement_appareil_t // 26/10 : nom propre au fichier (deux « branchement_t » = un seul pour l'éditeur de liens : la pause de la caméra devenait ESP.restart)
{
    branchement_appareil_t()
    {
        ui_appareil_charger = charger;
        ui_appareil_sauver = sauver;
        ui_appareil_mhz = []() { return (int)getCpuFrequencyMhz(); };
        ui_appareil_sante = sante;
        ui_appareil_redemarrer = []()
        {
            ui_mqtt_quitter();
            ESP.restart();
        };
        ui_appareil_effacer_memoire = effacer_memoire;
        ui_appareil_regler_heure = regler_heure;
        ui_appareil_wifi = []() { return WiFi.isConnected(); };
        setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
        tzset();
    }
} g_branchement;
#endif
