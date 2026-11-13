// 22/10/2026 : contrat MQTT dashboard_guition/ (CONTRAT_MQTT.md) côté écran.
// ui_mqtt.cpp = JSON reçu -> données des pages + commandes des pages -> JSON « set ». Sans réseau : le même code tourne
// au simulateur PC. Le transport (PubSubClient) est dans ui_mqtt_reseau.cpp, compilé seulement sur l'ESP32.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define UI_MQTT_PREFIXE "dashboard_guition/"

// Sujets auxquels l'écran s'abonne (terminé par nullptr)
extern const char *const ui_mqtt_abonnements[];

// Données communes à plusieurs pages (dashboard_guition/maison/etat)
struct ui_maison_t
{
    bool nuit;
    bool presence;
    uint32_t maj; // horodatage Unix de la dernière publication de HA (0 = jamais reçu)
};
extern ui_maison_t ui_maison;

// 12/11/2026 : le ciel de la page IA (dashboard_guition/maison/ciel, retenu, CONTRAT_MQTT.md) : horodatages Unix des
// PROCHAINS aube / lever / coucher / crépuscule, lune (âge en jours, % éclairé) et météo OpenWeatherMap
struct ui_ciel_t
{
    uint32_t aube, lever, coucher, crepuscule;
    float lune_age;
    int lune_pct;
    char meteo[20]; // condition HA : sunny, clear-night, partlycloudy, cloudy, fog, rainy, pouring, snowy, lightning…
    int nuages;     // %
    float pluie, neige; // mm/h
    bool recu;
};
extern ui_ciel_t ui_ciel;

// Instant (lv_tick) du dernier message reçu de HA (0 = jamais) : liaison « Home Assistant » de la page Infos
extern uint32_t ui_mqtt_dernier_ha;

// Appelé par le transport à chaque connexion au broker (connecte, durée de la tentative en ms) et à chaque perte (false, 0)
extern void (*ui_mqtt_evenement)(bool connecte, uint32_t duree_ms);

// Fourni par le transport (ou le simulateur) : envoie un message, false si pas connecté
extern bool (*ui_mqtt_publier)(const char *sujet, const char *message, bool retenu);

void ui_mqtt_brancher(); // branche les commandes des pages sur ui_mqtt_publier (une fois, après lv_init)
void ui_mqtt_recevoir(const char *sujet, const uint8_t *message, unsigned longueur);

// Transport (ui_mqtt_reseau.cpp, ESP32 seulement) : nom = écran (« cuisine », « chambre2 »…) pour appareil/<nom>/statut
void ui_mqtt_demarrer(const char *nom);
void ui_mqtt_tache(); // dans loop(), sur le même fil que lv_timer_handler()
bool ui_mqtt_connecte();
// 31/10 : toutes les valeurs retenues que les pages montrent sont arrivées (synchro de l'écran sur batterie)
bool ui_mqtt_complet();
int ui_mqtt_manquants(char *b, size_t n); // nombre de valeurs manquantes, leurs sujets dans b
// 24/10 : avant un redémarrage voulu (logiciel, OTA) : statut « hors_ligne » puis déconnexion propre. Sans elle, le broker
// garde l'ancienne session jusqu'à 90 s et met ~2 s à la reprendre à la reconnexion (journal du broker, 21:30). ESP32.
void ui_mqtt_quitter();
void ui_mqtt_relancer(); // 31/10 : Wi-Fi rallumé exprès : nouvel essai tout de suite (pas d'attente doublée)
// 24/10 (retour d'usage) : « stabilisation réseau » avant le 1er essai MQTT, comme la production : gardée sur secteur (3 s),
// PAS en mode batterie (la production s'y connectait tout de suite au réveil, « coup sur coup ») : mettre 0. ESP32.
extern uint32_t ui_mqtt_stabilisation_ms;
