// 24/10/2026 : page Musique branchée DIRECTEMENT sur Music Assistant, sans automatisation HA (retour d'usage : la production
// parlait directement à MPD). API HTTP de MA : POST <serveur>/api {"command", "args"} avec le jeton « dashboard ».
// Bibliothèque, radios, file, lecture, volume, lecteurs ; la chaîne piCore et l'ampli restent à HA (MQTT).
#pragma once
#include <stddef.h>
#include <stdint.h>

// Fourni par la plateforme (ESP32 : ui_musique_reseau.cpp ; simulateur : réponses enregistrées). corps = la commande
// en JSON ; alloue *reponse (libéré par free), false si échec.
extern bool (*ui_ma_requete)(const char *corps, char **reponse, size_t *taille);
// 24/10 : lance travail en tâche de fond (ESP32 : sur l'autre cœur, sans bloquer LVGL ; travail ne touche pas à LVGL) ;
// nullptr (simulateur) : la bibliothèque est lue tout de suite, à l'entrée de la page
extern bool (*ui_ma_en_fond)(void (*travail)(void *));
extern const char *ui_ma_serveur; // adresse du Music Assistant (secrets_ma.h) : sert aux pochettes (imageproxy, sans jeton)

// 25/10 : instant (lv_tick) du dernier appui sur un bouton de lecture : mesure de la latence des commandes
extern volatile uint32_t mu_appui_ms;
void ui_musique_brancher(); // branche mu_commande et lance la synchro (une fois, après lv_init)
// 24/10 : précache de la bibliothèque (artistes) au démarrage, en fond ; prête = lue (ou échouée : on n'attend plus)
void ui_musique_precharger();
// 26/10 (retour d'usage : « en mode batterie la biblio sera lue que sur lecture de la liste ») : pas de lecture au démarrage ni
// toutes les heures ; ui_musique_demander_biblio() à l'ouverture des onglets Biblio / Radios (relue si plus d'1 h)
extern bool ui_musique_a_la_demande;
void ui_musique_demander_biblio();
bool ui_musique_biblio_prete();
