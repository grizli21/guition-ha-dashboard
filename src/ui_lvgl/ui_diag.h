// 24/10/2026 (retour d'usage : « il te faut un débug des freeze comme la prod », après un figeage pendant le mouvement des
// stores) : diagnostic des blocages et plantages. Repris de la production : raison du redémarrage analysée au démarrage,
// journal lisible à distance en Telnet (port 23, comme debug.h / loop.h). Ajouté : les dernières traces et l'étape de la
// boucle en cours sont gardées en mémoire RTC (elle survit à un redémarrage par plantage ou chien de garde) et vidées au
// démarrage suivant (Telnet, et HA : appareil/<nom>/dump, retenu) ; chien de garde de la boucle à 10 s (celui commenté dans
// le setup.h de la production) : un écran figé redémarre et dit où il était, au lieu de rester mort.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Une trace : port série + Telnet + mémoire RTC (simulateur : printf)
void ui_trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// L'étape en cours (« mqtt:poele/etat », « images », « ma:players/all »…) : c'est elle qu'on lit après un figeage.
// Le texte est copié ; une étape qui dure plus de 500 ms laisse une trace « lent ».
void ui_diag_etape(const char *etape);
// Repère fin à l'intérieur d'une étape (« flush », « toucher »…), rien qu'une copie en mémoire RTC : après un chien de
// garde des interruptions (reset matériel, aucune trace de pile, vu le 24/10 sur le store), dit où le cœur s'est bloqué.
void ui_diag_point(const char *point);

// ESP32 (ui_diag_esp.cpp) : au démarrage (après Serial.begin), puis dans loop()
void ui_diag_demarrer();
void ui_diag_tache();
// Après la connexion MQTT : publie le dump du plantage précédent (s'il y en a un), une fois
void ui_diag_publier(bool (*publier)(const char *sujet, const char *message, bool retenu), const char *nom);
// 12/11 : commande telnet « rendu » : images rendues depuis la dernière demande (nombre, ms moyen et max, pixels)
extern void (*ui_diag_rendu)(char *b, unsigned n);
uint32_t ui_diag_boucle_max();          // 25/10 : la boucle la plus lente depuis le démarrage (ms)
const char *ui_diag_cause();             // raison du dernier redémarrage, en clair (« chien de garde des tâches »…)
const char *ui_diag_etape_precedente(); // étape en cours au moment du plantage précédent ("" : pas de plantage)
// 26/10 : les traces de la session (l'anneau RTC), dans l'ordre ; renvoie la longueur (texte terminé par 0)
size_t ui_diag_traces(char *dst, size_t n);
void ui_diag_traces_effacer();
// 28/10 : AUDIT RAM : RAM interne libre notée à chaque étape (démarrage, Wi-Fi, caméra…), relue par la commande telnet
// « ram » (avec les piles des tâches et les blocs alloués de plus de 1 Ko en RAM interne)
void ui_diag_ram_etape(const char *etape);
