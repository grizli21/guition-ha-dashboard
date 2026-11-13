// 10/11/2026 : MODE DÉMO (drapeau de compilation -D MODE_DEMO) : l'écran s'allume sans Wi-Fi, sans Home Assistant et
// sans broker MQTT. Les données arrivent par le VRAI chemin : la table { sujet, message } de ui_demo.cpp est rejouée
// message par message par ui_mqtt_recevoir(), exactement comme les messages retenus de Home Assistant à la connexion.
// Rien n'est rempli à la main dans les structures des pages. Ce fichier ne change rien au chemin normal : il n'est
// appelé que sous #ifdef MODE_DEMO.
//
// Musique : la bibliothèque, la file et les radios ne passent PAS par MQTT en production (HTTP Music Assistant,
// ui_ma_requete) : ui_demo_ma() répond à ces commandes avec des réponses embarquées, même interface, même parseurs.
// Pochettes, logos de radios et caméras : vides en démo (pas de serveur d'images ; ui_ma_serveur reste vide).
//
// Heure : sans HA ni NTP, l'horloge est fixée par le champ « maj » de maison/etat (epoch 1794925800 =
// 17/11/2026 15:30 CET, lu par ui_appareil_heure) ; les « fin » des historiques portent 930 = 15:30 en minutes du
// jour (recalées en journée le 13/11/2026 pour que le ciel de la page IA tourne en jour).
#pragma once

#ifdef MODE_DEMO
#include <stddef.h>
#include <stdint.h>

// Rejoue toute la table sur ui_mqtt_recevoir. À appeler après ui_mqtt_brancher() et les créations de pages.
void ui_demo_lancer();

// Interception des commandes d'une page (dans publier(), au point où elles partiraient vers le broker) : renvoie à
// ui_mqtt_recevoir l'état que Home Assistant aurait publié en retour. Vrai si la commande est traitée (elle n'est
// alors pas publiée). Répond à : chauffage, VMC, lumières, ouvrants, ballon, poêle, café/bouilloire.
bool ui_demo_echo(const char *sujet, const char *message);

// Faux Music Assistant pour ui_ma_requete : lecteurs, file, bibliothèque, radios ; les commandes de lecture
// (play/pause, volume…) modifient l'état renvoyé, comme un vrai lecteur.
bool ui_demo_ma(const char *corps, char **reponse, size_t *taille);
void ui_demo_brancher_ma(); // pose ui_ma_requete (et laisse ui_ma_serveur vide : pas d'images en démo)
#endif
