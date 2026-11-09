// 24/10/2026 (retour d'usage : « une synchro au démarrage dans le splash screen que tu as complètement boycotté ») : l'écran de
// démarrage de la production (lvgl_page_boot.h : DASHBOARD, la pièce, barre de progression, étape en cours, icônes
// Espressif / Arduino), aux couleurs du thème. Posé sur la couche du dessus : l'interface se construit dessous, le doigt
// n'y arrive qu'une fois l'écran effacé (dans le noir : ui_appareil_transition). Les étapes sont menées par le firmware (ecran.cpp :
// démarrage, Wi-Fi, MQTT, synchronisation HA, bibliothèque musicale).
#pragma once
#include <lvgl.h>

void ui_demarrage_creer(const char *piece); // piece : le nom affiché sous DASHBOARD (« Cuisine »)
void ui_demarrage_etape(const char *texte, int pct); // pct < 0 : la barre ne bouge pas
void ui_demarrage_fin();                              // supprimé dans le noir (transition par le rétroéclairage)
bool ui_demarrage_affiche();
