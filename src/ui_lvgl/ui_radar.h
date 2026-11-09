// 26/10/2026 : radar de présence LD2410S de l'écran sur batterie (ESP32 : ui_radar_esp.cpp), repris de la production
// (main.cpp, radar.h, radar_webserver.h) : port série 1 (RX 41, TX 40) à 115 200 bauds, trames « PC » (énergie par zone),
// configuration relue au démarrage à froid, page web de réglage sur le port 80 (/ et /radar), gardée telle quelle.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void ui_radar_demarrer(bool froid); // froid : mise sous tension / bouton (la production relit alors la configuration)
void ui_radar_web_demarrer();       // une fois le Wi-Fi connecté
void ui_radar_tache();              // dans loop()
bool ui_radar_mouvement();          // quelqu'un est détecté
bool ui_radar_web_actif();          // la page web est ouverte (pas de veille, comme la production)
bool ui_radar_auto_en_cours();      // réglage automatique en cours (pas de veille non plus)
bool ui_radar_diag_actif();         // 26/10 : diag radar : pas de veille, chaque détection notée dans le journal de HA
// 01/11 : battement du capteur (banc distant) : trames, octets, silences, relances et la dernière (sortie / relecture / seul / echec)
void ui_radar_compteurs(uint32_t *trames, uint32_t *octets, uint32_t *muets, uint32_t *relances, const char **par);
