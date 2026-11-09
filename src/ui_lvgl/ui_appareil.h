// 24/10/2026 : ce qui est propre à CET écran (contrat §5, dashboard_guition/appareil/<nom>/…) : ses réglages, gardés en
// mémoire flash (comme la production : save_config / load_config dans LittleFS), appliqués (luminosité jour / nuit, veille, retour à
// l'accueil, pièce) et publiés pour HA, qui peut les changer (reglages/set) ; sa santé, publiée toutes les 60 s et montrée
// par la page Infos avec ses liaisons ; les actions de la page Infos. Les réglages de la MAISON (carillon, terrasse) partent
// vers HA (maison/reglages/set).
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

struct ui_sante_t
{
    uint32_t depuis_s;
    char redemarrage[40]; // cause du dernier redémarrage (« chien de garde des interruptions »)
    uint32_t ram_ko, psram_ko;
    int cpu_temp;
    char version[24];
    char wifi[33], ip[16];
    int rssi;
    int cpu_mhz;  // 31/10 : fréquence du processeur (80 en économie, 240 sinon)
    int tx_dbm4;  // 31/10 : puissance d'émission Wi-Fi en quarts de dBm (0 : Wi-Fi arrêté)
};

// Fournis par la plateforme (ESP32 : ui_appareil_esp.cpp et le programme ; simulateur : rien, la mémoire ne survit pas)
extern bool (*ui_appareil_charger)(void *donnees, size_t taille); // mémoire flash -> réglages ; false si rien de valable
extern void (*ui_appareil_sauver)(const void *donnees, size_t taille);
extern void (*ui_appareil_sante)(ui_sante_t &s);
extern void (*ui_appareil_redemarrer)();
extern void (*ui_appareil_effacer_memoire)(); // remise à zéro : réglages effacés, puis redémarrage
extern void (*ui_appareil_lumiere)(int valeur); // rétroéclairage 0..255 (MLI), appelé pendant les fondus
extern void (*ui_appareil_regler_heure)(uint32_t unix); // règle l'horloge (fuseau de la production : CET/CEST)
extern bool (*ui_appareil_wifi)();                     // Wi-Fi connecté ?
// 26/10 : écran sur batterie (comme la production : powerReduction_timer) : la luminosité de repos arrive au bout de ce
// délai (10 s) et non du délai de veille, qui devient celui de la veille profonde (0 = comportement sur secteur)
extern uint32_t ui_appareil_repos_ms;
// 26/10 : si non nul, rétroéclairage ÉTEINT (pas seulement au repos) après ce temps sans toucher, même avec quelqu'un à la
// maison : le diag radar d'un écran sur batterie, qui ne dort plus (comme sa veille profonde, l'écran noir)
extern uint32_t ui_appareil_extinction_ms;
// 27/10 : détail du coucher précédent (broche du radar, durée de la veille), ajouté à la ligne « Réveil »
extern char ui_appareil_reveil_info[192];
int ui_appareil_veille_s(); // délai de veille réglé (s) ; 0 = jamais
void ui_appareil_rallumer(); // 26/10 : réveil court de l'écran sur batterie : fondu depuis le noir, une fois l'écran prêt

void ui_appareil_demarrer(const char *nom); // après ui_mqtt_demarrer : réglages relus, appliqués, publiés
const char *ui_appareil_nom();
// À appeler par la lecture du tactile : true = cacher ce toucher à LVGL (il rallume l'écran endormi, il ne clique pas)
bool ui_appareil_toucher(bool appuye);
// 24/10 (retour d'usage : « la transition splash -> page principale n'est pas fluide ») : passage d'un écran à l'autre par le
// rétroéclairage (baisse 150 ms, milieu(), un tour pour dessiner, remontée 300 ms) : un fondu dessiné d'un écran entier
// semi-transparent coûte trop à l'ESP32 ; celui-ci ne coûte rien
void ui_appareil_transition(void (*milieu)());
void ui_appareil_reglages_set(JsonDocument &j); // appareil/<nom>/reglages/set (HA -> cet écran)
// 24/10 : l'heure vient de HA, comme la production (horodatage « maj » de maison/etat, pas de NTP) : l'horloge est réglée
// au premier message, puis seulement si elle RETARDE de plus d'une minute (un message retenu peut dater de 5 min)
void ui_appareil_heure(uint32_t unix);
// Journal (24/10) : une ligne horodatée, gardée pour la page Infos (12 dernières) et envoyée à HA (appareil/<nom>/journal,
// inscrite dans son journal de bord) ; les lignes d'avant la connexion partent dès qu'elle est là
void ui_journal(bool alerte, const char *fmt, ...);
// 31/10 : écran sur batterie, Wi-Fi coupé après la synchro : true quand le dashboard a toutes ses valeurs (ui_mqtt_complet),
// la ligne du réveil partie et l'état (batterie comprise) publié ; appelée en boucle jusqu'à true
bool ui_appareil_synchroniser();
// 31/10 : Wi-Fi coupé exprès (puis rallumé au toucher) : ni « Wi-Fi perdu », ni « MQTT perdu », ni « HA muet » au journal
void ui_appareil_reseau_volontaire(bool coupe);
