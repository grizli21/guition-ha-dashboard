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
#include <sys/time.h>
#include <time.h>
#include "version_git.h" // 02/11 : écrit par version_git.py à la compilation

static const char *ESPACE = "guition"; // espace de la mémoire flash (NVS)

static bool charger(void *donnees, size_t taille)
{
    Preferences p;
    if (!p.begin(ESPACE, true))
        return false;
    const bool ok = p.getBytesLength("reglages") == taille && p.getBytes("reglages", donnees, taille) == taille;
    p.end();
    return ok;
}

static void sauver(const void *donnees, size_t taille)
{
    Preferences p;
    if (!p.begin(ESPACE, false))
        return;
    p.putBytes("reglages", donnees, taille);
    p.end();
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
}

static void effacer_memoire()
{
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
