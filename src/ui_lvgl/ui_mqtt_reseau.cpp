// 22/10/2026 : transport MQTT du contrat dashboard_guition/ (PubSubClient). ESP32 seulement : le simulateur PC
// branche ui_mqtt_publier lui-même et rejoue un relevé du broker dans ui_mqtt_recevoir().
#ifdef ARDUINO
#include "ui_mqtt.h"
#include <Arduino.h>
#include "ui_diag.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Ping.h>
#include "../secrets_mqtt.h" // broker + identifiants (hors git)

static WiFiClient g_client_tcp;
static PubSubClient g_mqtt(g_client_tcp);
static char g_nom[24];
static char g_sujet_statut[64];

// Une tentative bloque la boucle (donc LVGL) tant que le broker ne répond pas : connexion TCP limitée à 1 s
// (sur le réseau local elle prend quelques ms ; défaut du framework 3 s), CONNACK à 2 s. Puis reconnexion espacée :
// 5 s doublée jusqu'à 1 min (le broker est l'add-on de HA : redémarré à chaque mise à jour, pas plus d'1 min d'attente).
// Jamais de redémarrage de l'écran : la page reste utilisable avec les dernières valeurs.
static uint32_t g_derniere_tentative = 0, g_attente = 0, g_echecs = 0;
static bool g_deja = false; // connecté au moins une fois depuis le démarrage
uint32_t ui_mqtt_stabilisation_ms = 3000;
#define ATTENTE_MIN 5000
#define ATTENTE_MAX 60000
#define ATTENTE_DEMARRAGE 2000

static bool publier(const char *sujet, const char *message, bool retenu)
{
    return g_mqtt.connected() && g_mqtt.publish(sujet, message, retenu);
}

static void recevoir(char *sujet, uint8_t *message, unsigned int longueur)
{
    ui_mqtt_recevoir(sujet, message, longueur);
}

void ui_mqtt_demarrer(const char *nom)
{
    snprintf(g_nom, sizeof g_nom, "%s", nom);
    snprintf(g_sujet_statut, sizeof g_sujet_statut, UI_MQTT_PREFIXE "appareil/%s/statut", nom);
    g_mqtt.setServer(MQTT_SERVEUR, 1883);
    g_mqtt.setBufferSize(4096); // défaut 256 : les messages plus grands seraient ignorés EN SILENCE
    // 24/10 : 5 s (2 avant) : une ancienne session restée ouverte (redémarrage brutal) prend ~2 s au broker à reprendre ;
    // à 2 s, l'écran abandonnait pile à ce moment (journal du broker : « session taken over » puis « closed by client »)
    g_mqtt.setSocketTimeout(5);
    g_client_tcp.setTimeout(1);
    g_mqtt.setCallback(recevoir);
    ui_mqtt_publier = publier;
    ui_mqtt_brancher();
}

static bool connecter()
{
    char id[40];
    snprintf(id, sizeof id, "guition-%s", g_nom);
    g_client_tcp.stop(); // ancienne socket fermée avant d'en ouvrir une autre (erreur -2 au démarrage, production)
    // 24/10 : au démarrage, 3 s pour la connexion TCP comme la production (défaut d'Arduino), dont la reconnexion
    // « fonctionnait coup sur coup » (retour d'usage) : la pile TCP renvoie une demande perdue au bout de ~3 s ; à 1 s, une
    // seule perte faisait échouer l'essai (vu au banc : 3 à 9 échecs -2 de suite). Ensuite 1 s : un broker perdu en route
    // ne fige pas l'interface plus longtemps
    g_client_tcp.setTimeout(g_deja ? 1 : 3);
    if (!g_mqtt.connect(id, MQTT_UTILISATEUR, MQTT_MOT_DE_PASSE, g_sujet_statut, 0, true, "hors_ligne"))
        return false;
    g_mqtt.publish(g_sujet_statut, "en_ligne", true);
    for (int i = 0; ui_mqtt_abonnements[i]; i++)
        g_mqtt.subscribe(ui_mqtt_abonnements[i]); // les retenus arrivent aussitôt : la page se remplit
    ui_trace("[mqtt] connecté (%s)\n", id);
    if (ui_mqtt_evenement)
        ui_mqtt_evenement(true, millis() - g_derniere_tentative);
    return true;
}

void ui_mqtt_quitter()
{
    if (!g_mqtt.connected())
        return;
    g_mqtt.publish(g_sujet_statut, "hors_ligne", true);
    g_mqtt.disconnect();
    g_client_tcp.stop();
}

void ui_mqtt_tache()
{
    static bool connecte = false;
    if (g_mqtt.connected())
    {
        connecte = g_deja = true;
        g_mqtt.loop();
        return;
    }
    if (connecte) // vient de perdre le broker
    {
        connecte = false;
        if (ui_mqtt_evenement)
            ui_mqtt_evenement(false, 0);
    }
    static uint32_t wifi_depuis = 0;
    if (!WiFi.isConnected())
    {
        wifi_depuis = 0;
        return;
    }
    if (!wifi_depuis)
        wifi_depuis = millis() | 1;
    // stabilisation réseau après le Wi-Fi, au démarrage seulement (secteur : 3 s ; batterie : 0, voir ui_mqtt.h)
    if (!g_deja && millis() - wifi_depuis < ui_mqtt_stabilisation_ms)
        return;
    if (g_derniere_tentative && millis() - g_derniere_tentative < g_attente)
        return;
    g_derniere_tentative = millis();
    if (connecter())
        g_attente = ATTENTE_MIN;
    else
    {
        g_echecs++;
        // 24/10 : au démarrage (jamais connecté), un essai toutes les 2 s : les premiers échouent parfois juste après le
        // Wi-Fi (vu au banc : 3 échecs, puis 5 + 10 + 20 s d'attente, écran de démarrage abandonné à 30 s ; la production
        // a une étape « stabilisation réseau ») ; l'attente doublée reste pour un broker perdu en route (HA qui redémarre)
        g_attente = !g_deja ? ATTENTE_DEMARRAGE : g_attente ? min<uint32_t>(g_attente * 2, ATTENTE_MAX) : ATTENTE_MIN;
        ui_trace("[mqtt] échec n°%u (état %d, %u ms), nouvel essai dans %u s\n", g_echecs, g_mqtt.state(),
                      millis() - g_derniere_tentative, g_attente / 1000);
        // 24/10 : au démarrage, la machine du broker répond-elle au ping ? (les essais n'arrivent pas au broker : son
        // journal ne les montre pas) -> chemin réseau ou TCP seul ; la production pingue le routeur avant MQTT
        if (!g_deja)
        {
            IPAddress broker;
            broker.fromString(MQTT_SERVEUR);
            const uint32_t t0 = millis();
            const bool ok = Ping.ping(broker, 1);
            ui_trace("[mqtt] ping du broker : %s (%lu ms)\n", ok ? "répond" : "rien", (unsigned long)(millis() - t0));
        }
    }
}

void ui_mqtt_relancer()
{
    g_derniere_tentative = 0;
    g_attente = 0;
}

bool ui_mqtt_connecte() { return g_mqtt.connected(); }
#endif
