// 26/10/2026 : batterie de l'écran du séjour sur l'ESP32 (voir ui_batterie.h). Algorithme et seuils de la production
// (mobile_device.h : updateAndReadBatteryMovingAverage, getBatteryPercentage, processChargeStatusPostBoot,
// manageBatteryAndCharge) ; le dernier débranchement est gardé en NVS (la production : LittleFS, /disconnect…).
#ifdef ARDUINO
#include "ui_batterie.h"
#include "ui_diag.h"
#include <Arduino.h>
#include <Preferences.h>
#include "memoire_brute.h" // 13/11 : réglages en flash sans NVS
#include <esp_attr.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BAT_ADC_PIN 1
static const float DIVIDER_RATIO = 3.1277f; // (100k + 47k) / 47k
static const float ADC_CORRECTION = 1.082f;
static const float ADC_MAX = 4095.0f;
static const float ADC_VREF = 3.3f;
#define SAMPLES_COUNT 64
#define INTERVAL_COUNT 10 // ms entre deux lectures
#define EXTREMES_TO_DROP 4

void (*ui_batterie_icone)(int niveau, uint32_t couleur) = nullptr;

static int g_adc[SAMPLES_COUNT];
static int g_index = 0;
static uint32_t g_total = 0;
static bool g_plein = false;
static float g_tension = 0;
static bool g_present = false;
static uint32_t g_debut_ms = 0;

// dernier branchement / débranchement (NVS « batterie »), comme la production
static uint32_t g_debranche = 0;  // usb_disconnect_time
// 31/10 : débranchement constaté avant que HA ait donné l'heure (3 s après une mise sous tension) : time() valait 3 s,
// HA affichait « débranché depuis 56 ans » ; on retient l'instant (millis) et on le date dès que l'heure est connue
static uint32_t g_debranche_ms = 0; // 0 = rien en attente de date

static float g_tension_max = 0;   // max_charge_voltage
static bool g_charge_nvs = false; // was_charging_in_fs

static void sauver(uint32_t debranche, float tension, bool charge);
static void noter_debranchement()
{
    const uint32_t t = (uint32_t)time(nullptr);
    if (t > 1700000000u)
        g_debranche = t;
    else
    {
        g_debranche = 0; // inconnue tant que l'heure manque
        g_debranche_ms = millis() | 1;
    }
}

static void dater_debranchement()
{
    const uint32_t t = (uint32_t)time(nullptr);
    if (!g_debranche_ms || t <= 1700000000u)
        return;
    g_debranche = t - (millis() - g_debranche_ms) / 1000;
    g_debranche_ms = 0;
    sauver(g_debranche, g_tension_max, false);
}
static bool g_charge = false;

// mémoire RTC : survit à la veille profonde et au redémarrage logiciel (pas à une coupure)
#define RTC_MAGIC 0x42415431u // « BAT1 »
struct bt_rtc_t
{
    uint32_t magic;
    bt_eveil_t eveils[BT_EVEILS];
    uint8_t nb;
    uint8_t pct[BT_COURBE];   // une case par quart d'heure (n° de quart % 96)
    uint32_t quart[BT_COURBE]; // le n° de quart d'heure (heure Unix / 900) écrit dans la case
};
RTC_NOINIT_ATTR static bt_rtc_t g_rtc;
static int g_eveil = -1; // case du réveil en cours

static int pourcentage(float v)
{
    if (v >= 4.20f)
        return 100;
    if (v <= 3.20f)
        return 0;
    if (v > 4.05f)
        return 85 + (int)((v - 4.05f) * (15.0f / 0.15f));
    if (v > 3.75f)
        return 35 + (int)((v - 3.75f) * (50.0f / 0.30f));
    if (v > 3.60f)
        return 10 + (int)((v - 3.60f) * (25.0f / 0.15f));
    return 0 + (int)((v - 3.20f) * (10.0f / 0.40f));
}

static int comparer(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static float mesurer()
{
    g_total -= g_adc[g_index];
    g_adc[g_index] = analogRead(BAT_ADC_PIN);
    g_total += g_adc[g_index];
    if (++g_index >= SAMPLES_COUNT)
    {
        g_index = 0;
        g_plein = true;
    }
    const int n = g_plein ? SAMPLES_COUNT : g_index;
    if (!n)
        return 0;
    float brut;
    if (g_plein)
    {
        int tri[SAMPLES_COUNT];
        memcpy(tri, g_adc, sizeof tri);
        qsort(tri, SAMPLES_COUNT, sizeof(int), comparer);
        long t = 0;
        for (int i = EXTREMES_TO_DROP; i < SAMPLES_COUNT - EXTREMES_TO_DROP; i++)
            t += tri[i];
        brut = (float)t / (SAMPLES_COUNT - 2 * EXTREMES_TO_DROP);
    }
    else
        brut = (float)g_total / n;
    return brut / ADC_MAX * ADC_VREF * DIVIDER_RATIO * ADC_CORRECTION;
}

// 13/11 : mémoire brute (memoire_brute.h) : débranchement (u32), tension max (float), en charge (u8) ; NVS seulement pour
// la migration
static void sauver(uint32_t debranche, float tension, bool charge)
{
    uint8_t m[9];
    memcpy(m, &debranche, 4);
    memcpy(m + 4, &tension, 4);
    m[8] = charge;
    mb_ecrire(MB_BATTERIE, m, sizeof m);
}

static void charger()
{
    uint8_t m[9];
    if (mb_lire(MB_BATTERIE, m, sizeof m))
    {
        memcpy(&g_debranche, m, 4);
        memcpy(&g_tension_max, m + 4, 4);
        g_charge_nvs = m[8];
    }
    else
    {
        Preferences p;
        if (!p.begin("batterie", true))
            return;
        g_debranche = p.getUInt("debranche", 0);
        g_tension_max = p.getFloat("tension", 0);
        g_charge_nvs = p.getBool("charge", false);
        p.end();
        memcpy(m, &g_debranche, 4);
        memcpy(m + 4, &g_tension_max, 4);
        m[8] = g_charge_nvs;
        mb_noter(MB_BATTERIE, m, sizeof m);
    }
    if (g_debranche < 1700000000u) // 31/10 : notée sans heure (1970) par une version précédente : inconnue
        g_debranche = 0;
}

// processChargeStatusPostBoot (la production : 3 s après le démarrage, le temps de remplir sa moyenne ; 31/10 : remplie
// d'un coup au démarrage, ui_batterie_demarrer)
static void verifier_demarrage(float v)
{
    charger();
    if (g_tension_max == 0)
    {
        g_charge = v > 4.15f;
        return;
    }
    if (!g_charge_nvs) // arrêté sur batterie : un redémarrage ne veut pas dire qu'on a branché
    {
        g_charge = false;
        return;
    }
    if (g_tension_max - v < 0.05f) // toujours sur l'USB
        g_charge = true;
    else // la tension a chuté : on a débranché pendant l'arrêt
    {
        g_charge = false;
        noter_debranchement();
        g_tension_max = v;
        sauver(g_debranche, g_tension_max, false);
        ui_trace("[batterie] débranchée pendant l'arrêt (%.2f V)\n", v);
    }
}

// manageBatteryAndCharge, rythme de croisière
static void suivre_charge(float v)
{
    if (g_charge)
    {
        static uint32_t derniere_ecriture = 0;
        static float derniere_sauvee = 0;
        if (derniere_sauvee == 0)
            derniere_sauvee = g_tension_max;
        if (v > g_tension_max)
            g_tension_max = v;
        if (millis() - derniere_ecriture >= 60000)
        {
            derniere_ecriture = millis();
            if (v - derniere_sauvee >= 0.02f) // hausse d'au moins 0,02 V depuis la dernière sauvegarde
            {
                sauver(0, g_tension_max, true);
                derniere_sauvee = v;
                g_tension_max = v;
            }
        }
    }
    else
    {
        static uint32_t ref_ms = 0;
        static float ref = 0;
        if (ref == 0 || millis() - ref_ms >= 2000)
        {
            ref = v;
            ref_ms = millis();
        }
        if (v - ref >= 0.05f) // rebranchement à chaud
        {
            g_charge = true;
            ref = 0;
            ui_trace("[batterie] USB rebranché (%.2f V)\n", v);
        }
    }
}

// ---------------------------------------------------------------------------------------------------- réveils, courbe
static void noter_point(int pct)
{
    const uint32_t t = (uint32_t)time(nullptr);
    if (t < 1700000000u)
        return;
    const uint32_t q = t / 900;
    g_rtc.pct[q % BT_COURBE] = (uint8_t)LV_CLAMP(0, pct, 100);
    g_rtc.quart[q % BT_COURBE] = q;
}

static void publier_etat()
{
    bt_etat.present = true;
    bt_etat.tension = g_tension;
    bt_etat.pourcent = LV_CLAMP(0, pourcentage(g_tension), 100);
    bt_etat.charge = g_charge;
    bt_etat.depuis = g_charge ? 0 : g_debranche;
    // le réveil en cours s'allonge ; l'heure de son début peut arriver après lui (HA règle l'heure)
    const uint32_t t = (uint32_t)time(nullptr);
    if (g_eveil >= 0 && t > 1700000000u)
    {
        if (g_rtc.eveils[g_eveil].debut < 1700000000u)
            g_rtc.eveils[g_eveil].debut = t - (millis() - g_debut_ms) / 1000;
        g_rtc.eveils[g_eveil].duree = (uint16_t)LV_MIN(65535u, (millis() - g_debut_ms) / 1000);
    }
    bt_etat.nb_eveils = 0;
    for (int i = 0; i < g_rtc.nb; i++)
        if (g_rtc.eveils[i].debut > 1700000000u)
            bt_etat.eveils[bt_etat.nb_eveils++] = g_rtc.eveils[i];
    noter_point(bt_etat.pourcent);
    const uint32_t qfin = t / 900;
    for (int i = 0; i < BT_COURBE; i++)
    {
        const uint32_t q = qfin - (BT_COURBE - 1) + i;
        bt_etat.courbe[i] = g_rtc.quart[q % BT_COURBE] == q ? g_rtc.pct[q % BT_COURBE] : BT_COURBE_VIDE;
    }
    bt_etat.courbe_fin = qfin * 900;
    int niveau;
    uint32_t couleur;
    bt_icone(niveau, couleur);
    if (ui_batterie_icone)
        ui_batterie_icone(niveau, couleur);
    page_batterie_maj();
}

void ui_batterie_demarrer(bt_cause_t cause)
{
    g_present = true;
    g_debut_ms = millis();
    analogReadResolution(12);
    analogRead(BAT_ADC_PIN); // Arduino 3 : la broche doit être déjà en mode analogique pour régler son atténuation
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
    if (g_rtc.magic != RTC_MAGIC || g_rtc.nb > BT_EVEILS)
    {
        memset(&g_rtc, 0, sizeof g_rtc);
        g_rtc.magic = RTC_MAGIC;
    }
    if (g_rtc.nb == BT_EVEILS) // le plus ancien sort
    {
        memmove(&g_rtc.eveils[0], &g_rtc.eveils[1], sizeof(bt_eveil_t) * (BT_EVEILS - 1));
        g_rtc.nb--;
    }
    g_eveil = g_rtc.nb++;
    const uint32_t t = (uint32_t)time(nullptr); // l'heure survit à la veille profonde (pas à une coupure)
    g_rtc.eveils[g_eveil] = {t, 0, (uint8_t)cause};
    bt_etat.cause = cause;
    bt_etat.reveil = t;
    // 31/10 (retour d'usage) : pas 3 s d'attente pour la 1re mesure (la synchro de l'écran l'attendait) : les 64 lectures d'un
    // coup (quelques ms), la charge vérifiée et l'état prêt dès le démarrage
    for (int i = 0; i < SAMPLES_COUNT; i++)
        g_tension = mesurer();
    verifier_demarrage(g_tension);
    publier_etat();
}

void ui_batterie_etape(bt_etape_t e)
{
    if (e == BT_E_REVEIL || bt_etat.etapes[e])
        return;
    bt_etat.etapes[e] = millis();
    if (bt_etat.reveil < 1700000000u)
        bt_etat.reveil = (uint32_t)time(nullptr) - (millis() - g_debut_ms) / 1000;
    page_batterie_maj();
}

void ui_batterie_dodo()
{
    if (!g_present)
        return;
    publier_etat(); // durée du réveil, dernier point de la courbe
    if (!g_charge)
        sauver(g_debranche, g_tension, false);
}

void ui_batterie_tache()
{
    if (!g_present)
        return;
    static uint32_t derniere = 0, derniere_publication = 0;
    if (millis() - derniere < INTERVAL_COUNT)
        return;
    derniere = millis();
    g_tension = mesurer();
    const bool avant = g_charge;
    suivre_charge(g_tension);
    if (avant && !g_charge)
        noter_debranchement();
    dater_debranchement();
    if (avant != g_charge || millis() - derniere_publication >= 5000)
    {
        derniere_publication = millis();
        publier_etat();
    }
}
#endif
