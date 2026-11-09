// 26/10/2026 : page « Batterie et capteur » de l'écran sur batterie (séjour ; maquette validée,
// simu/maquettes_batterie.cpp) : la batterie, les réveils des 24 dernières heures avec la courbe de la batterie, le dernier
// réveil (Wi-Fi, MQTT, écran prêt), l'accès aux réglages du radar. La page Info n'est pas touchée. Mesures et réveils :
// ui_batterie_esp.cpp (ESP32) ; le simulateur remplit bt_etat lui-même.
#pragma once
#include <lvgl.h>
#include <stdint.h>

#define BT_EVEILS 48  // réveils gardés (les 24 dernières heures en montrent rarement plus)
#define BT_COURBE 96  // batterie toutes les 15 min sur 24 h
#define BT_COURBE_VIDE 255

enum bt_cause_t
{
    BT_RADAR,
    BT_TOUCHER,
    BT_MINUTERIE, // nouvel essai, Wi-Fi absent au réveil précédent
    BT_DEMARRAGE  // mise sous tension, bouton, redémarrage logiciel
};

struct bt_eveil_t
{
    uint32_t debut;  // horodatage Unix
    uint16_t duree;  // s allumé
    uint8_t cause;   // bt_cause_t
};

// 31/10 : chronologie du dernier réveil (carte « Dernier réveil »)
enum bt_etape_t
{
    BT_E_REVEIL,   // démarrage du programme (millis 0)
    BT_E_WIFI,     // Wi-Fi connecté
    BT_E_MQTT,     // MQTT connecté
    BT_E_SYNCHRO,  // état de HA reçu, journal et batterie envoyés
    BT_E_COUPURE,  // Wi-Fi coupé (coupure après la synchro)
    BT_ETAPES
};

struct bt_etat_t
{
    bool present;   // cet écran a une batterie
    float tension;  // V (moyenne glissante)
    int pourcent;
    bool charge;    // sur l'USB
    uint32_t depuis; // horodatage du dernier branchement / débranchement (0 = inconnu)
    // réveils (le plus récent en dernier) et batterie en %, un point par quart d'heure, le plus récent en dernier
    bt_eveil_t eveils[BT_EVEILS];
    int nb_eveils;
    uint8_t courbe[BT_COURBE];
    uint32_t courbe_fin; // horodatage du dernier point
    // dernier réveil
    uint8_t cause;
    uint32_t reveil; // horodatage
    uint32_t etapes[BT_ETAPES]; // millis de chaque étape (0 = pas atteinte, sauf le réveil)
};
extern bt_etat_t *bt_etat_p; // 29/10 : en PSRAM (ui_psram.h)
#define bt_etat (*bt_etat_p)

// 31/10 : onglet Réglages (gestion d'énergie), gardés et appliqués par la plateforme (bt_regler)
struct bt_reglages_t
{
    bool coupure_wifi;  // réveil par le radar sans toucher : Wi-Fi coupé après la synchro, rallumé au toucher
    uint8_t economie_s; // passage en économie (luminosité de repos, 80 MHz, Wi-Fi en veille) après ce temps sans toucher
    bool wifi_reduit;   // émission Wi-Fi à 7 dBm (la production)
};
extern bt_reglages_t bt_reglages;
extern void (*bt_regler)(const char *quoi); // « coupure_wifi », « economie », « wifi_reduit »

// pile du rail : 0 (vide) à 3 (pleine) et sa couleur (0 = normale)
void bt_icone(int &niveau, uint32_t &couleur);

void page_batterie_creer(lv_obj_t *zone);
void page_batterie_quitter();
void page_batterie_maj(); // nouvelles mesures (sans effet si la page n'est pas affichée)
