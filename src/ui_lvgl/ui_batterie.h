// 26/10/2026 : batterie de l'écran du séjour (ESP32 : ui_batterie_esp.cpp), reprise de la production (mobile_device.h,
// main.cpp) : tension par le pont diviseur (100k + 47k) sur GPIO 1, 64 lectures toutes les 10 ms sans les 4 plus hautes ni
// les 4 plus basses, pourcentage par paliers, charge devinée par la tension (à chaud : +0,05 V en 2 s ; au redémarrage :
// comparée à la dernière tension gardée). Les réveils et la courbe vivent en mémoire RTC (survivent à la veille profonde).
#pragma once
#include "page_batterie.h"

void ui_batterie_demarrer(bt_cause_t cause);                       // au démarrage de l'écran sur batterie
void ui_batterie_tache();                                          // dans loop() : mesure toutes les 10 ms
void ui_batterie_etape(bt_etape_t e);                              // 31/10 : étape du réveil atteinte (millis), une fois
void ui_batterie_dodo();                                           // juste avant la veille profonde : durée du réveil
extern void (*ui_batterie_icone)(int niveau, uint32_t couleur);    // la pile du rail a changé
