// 10/11/2026 : MODE DÉMO — voir ui_demo.h. Tout le fichier est sous #ifdef MODE_DEMO : sans le drapeau, il ne
// contribue RIEN (ni code, ni chaînes) et le firmware reste strictement identique.
//
// Les messages ci-dessous sont le relevé d'un vrai broker (simu/donnees/releve_broker_22-10.tsv, contrats de
// CONTRAT_MQTT.md) réécrit avec des DONNÉES NEUTRES : pièces génériques, aucun nom de personne, IP d'exemple.
// Les tableaux des historiques (288 points de 5 min, calés à droite, « fin » = 930 = 15:30 — l'horloge de la
// démo a été calée en journée le 13/11/2026 pour que le ciel animé de la page IA tourne en jour, cf. maison/ciel ;
// les courbes étant calées à droite, ce recalage ne change que leurs étiquettes) ont été générés hors
// ligne par courbes sinusoïdales déterministes, puis embarqués en littéraux — ils cheminent ensuite par le VRAI
// parseur ui_mqtt_recevoir, comme les messages retenus de Home Assistant.

#ifdef MODE_DEMO

#include "ui_demo.h"
#include "ui_mqtt.h"
#include "ui_diag.h"
#include "ui_musique.h"
#include "page_thermostat.h"
#include "page_ballon.h"
#include "page_poele.h"
#include "page_cafe.h"
#include "page_vmc.h"
#include "page_lumieres.h"
#include "page_ouvrants.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Historiques générés hors ligne (courbes déterministes calées sur les états affichés) : t/h en °C×10 et %, c en escaliers [rang, °C×10],
// ch/hc en bits hexa, fin en minute du jour. Pièces dans l'ordre de PIECES[] : cuisine, sejour, chambre1,
// chambre2, sdb. Air : 48 points de 30 min (AQ_HIST). Poêle : 144 points de 5 min sur 12 h (PL_HIST).
#define HIST_PIECE0 \
    "{\"t\":[189,189,189,188,188,187,186,185,183,182,181,180,178,177,176,175,174,173,172,171,171,170,170,170,169,169," \
    "168,167,166,165,165,164,163,163,162,162,161,161,161,160,160,160,159,159,158,159,158,158,157,157,157,156,156,15" \
    "6,156,156,156,156,155,155,155,155,154,154,153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,152,152," \
    "152,152,151,151,151,151,151,151,153,153,153,153,153,153,152,152,152,152,151,151,152,153,154,155,156,157,158,15" \
    "8,159,160,161,162,162,163,163,163,164,164,165,165,166,167,167,168,168,169,169,170,170,170,170,170,171,172,172," \
    "172,173,173,173,174,174,175,175,175,176,176,176,176,176,176,176,176,176,177,178,178,179,179,179,180,180,180,18" \
    "0,180,180,180,180,180,180,180,180,181,181,181,181,182,183,183,183,183,183,183,183,183,183,183,183,183,183,184," \
    "184,184,184,184,185,185,184,184,185,185,185,185,185,185,185,185,186,186,186,186,186,187,187,186,186,186,186,18" \
    "6,186,186,187,187,187,187,188,188,188,188,188,188,188,188,188,187,187,187,187,187,187,187,187,188,189,189,189," \
    "189,189,189,189,188,188,188,188,188,188,188,188,188,188,189,189,189,189,189,189,189,189,189,189,188,188,188,18" \
    "9,189,189,189,189,189,189,189,189,189,189,188,188,188,189],\"h\":[45,45,45,45,46,46,46,46,46,46,46,46,46,47,47,4" \
    "7,46,46,46,46,46,46,46,46,46,45,45,45,45,45,44,44,44,44,44,44,44,44,43,43,43,43,44,44,44,44,44,44,44,45,45,45," \
    "45,46,46,46,47,47,47,48,48,48,49,49,49,50,50,50,50,50,51,51,51,51,51,51,51,51,51,51,51,51,51,51,51,51,51,51,51" \
    ",51,51,51,51,51,51,51,51,51,51,51,51,51,52,52,52,52,52,53,53,53,54,54,54,55,55,55,56,56,56,57,57,57,58,58,58,5" \
    "8,58,59,59,59,59,59,59,59,59,59,59,59,59,59,58,58,58,58,58,58,57,57,57,57,57,57,57,57,57,56,56,56,56,57,57,57," \
    "57,57,57,57,57,58,58,58,58,58,58,59,59,59,59,59,59,59,60,60,60,60,60,60,59,59,59,59,59,59,58,58,58,58,57,57,57" \
    ",56,56,56,55,55,55,54,54,54,54,53,53,53,53,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,5" \
    "3,53,53,52,52,52,52,52,52,52,52,51,51,51,51,50,50,50,49,49,49,48,48,48,47,47,47,46,46,46,45,45,45,45,45,44,44," \
    "44,44,44,44,44,44,44,44,44,44,45,45,45,45,45,45],\"c\":[[0,190],[5,160],[101,190]],\"ch\":\"0000000000e0fffffffffff" \
    "fffffffffffffffffffffffffffffffffffffffffffffffff\",\"fin\":930}"

#define HIST_PIECE1 \
    "{\"t\":[200,199,199,198,198,196,194,193,191,189,188,186,185,184,183,181,181,180,179,178,177,176,175,174,173,171," \
    "170,170,170,169,168,168,167,167,166,166,165,165,164,163,163,162,161,161,160,159,159,159,158,158,158,158,158,15" \
    "8,158,158,157,157,156,156,155,155,155,154,154,154,154,154,154,154,154,154,154,153,153,153,152,152,152,152,152," \
    "152,152,152,152,152,153,153,153,152,152,152,152,151,151,151,151,151,151,151,151,152,154,155,156,158,159,160,16" \
    "1,161,162,163,163,164,165,166,166,167,168,169,170,170,171,172,172,172,173,173,173,174,174,176,176,177,177,178," \
    "178,179,179,180,180,180,180,181,181,181,181,181,181,182,182,183,183,184,184,184,184,186,186,186,186,186,186,18" \
    "6,186,186,186,187,187,187,188,188,188,189,189,189,189,189,189,188,188,188,189,190,190,190,191,191,191,191,192," \
    "192,192,192,191,191,191,191,191,191,191,192,192,192,192,193,193,193,193,194,194,194,193,193,193,193,193,193,19" \
    "4,194,194,194,194,195,195,195,195,194,194,194,194,194,194,194,194,195,195,195,196,196,196,196,196,196,196,195," \
    "195,195,195,195,195,195,195,195,195,195,196,196,196,196,195,196,196,196,196,195,195,195,195,196,196,196,196,19" \
    "6,196,196,196,196,196,195,195,195,195,195,195,195,195,196],\"h\":[50,50,50,50,50,50,50,50,50,49,49,49,49,49,49,4" \
    "8,48,48,48,48,47,47,47,47,47,46,46,46,46,46,46,46,46,46,46,46,46,46,46,47,47,47,47,48,48,48,48,49,49,49,50,50," \
    "50,51,51,51,51,52,52,52,52,52,53,53,53,53,53,53,53,53,53,53,53,53,53,53,53,53,53,52,52,52,52,52,52,52,52,52,52" \
    ",53,53,53,53,53,53,54,54,54,54,55,55,55,56,56,56,57,57,58,58,58,59,59,59,60,60,60,60,60,61,61,61,61,61,61,61,6" \
    "1,61,61,61,61,61,61,61,60,60,60,60,60,60,60,60,59,59,59,59,59,59,59,59,59,59,59,60,60,60,60,60,60,61,61,61,61," \
    "61,62,62,62,62,62,63,63,63,63,63,63,63,63,63,63,63,63,63,63,62,62,62,62,61,61,61,61,60,60,60,59,59,59,58,58,58" \
    ",58,57,57,57,57,57,57,56,56,56,56,56,56,56,56,56,56,56,56,56,57,57,57,57,57,57,57,57,57,57,57,57,57,57,56,56,5" \
    "6,56,56,56,55,55,55,54,54,54,53,53,53,52,52,52,51,51,50,50,50,50,49,49,49,49,49,48,48,48,48,48,48,48,48,48,48," \
    "48,48,48,49,49,49,49,49,49,49,49,50,50,50,50,50],\"c\":[[0,200],[5,160],[101,200]],\"ch\":\"000000000000f8fffffffff" \
    "fffffffffffffffffffffffffffffffffffffffffffffffff\",\"fin\":930}"

#define HIST_PIECE2 \
    "{\"t\":[160,159,159,158,157,157,157,156,156,156,156,156,156,156,156,156,156,156,155,155,154,154,154,153,154,154," \
    "154,154,154,155,155,156,157,157,157,158,159,159,159,160,160,160,161,161,162,162,163,163,165,165,165,165,165,16" \
    "6,166,166,166,166,166,166,167,168,168,168,169,169,169,169,170,170,169,169,170,170,170,170,170,171,171,171,172," \
    "172,172,172,173,173,173,173,173,173,173,173,173,173,173,173,175,175,175,175,175,175,175,175,175,175,175,175,17" \
    "6,176,176,176,176,177,177,177,177,177,178,177,178,178,178,178,178,177,176,176,175,175,175,174,175,175,174,174," \
    "173,172,172,171,171,170,170,169,170,170,170,170,170,169,169,169,169,169,168,168,168,168,168,167,167,167,167,16" \
    "7,167,167,167,167,168,168,168,168,168,167,167,167,167,166,166,166,168,168,168,168,168,168,168,168,168,168,167," \
    "167,168,168,168,168,168,168,168,168,169,169,169,169,170,169,169,169,169,169,169,169,169,169,169,169,170,170,17" \
    "0,171,170,170,170,170,170,170,169,169,170,170,170,171,171,171,171,171,171,171,171,171,172,172,171,171,171,171," \
    "171,171,171,171,172,172,173,173,173,173,173,172,172,172,172,172,171,171,173,173,173,173,173,173,173,173,173,17" \
    "3,173,172,173,173,173,173,173,173,173,173,173,173,173,174],\"h\":[53,53,52,52,52,52,52,51,51,51,51,50,50,50,50,5" \
    "0,49,49,49,49,49,49,49,49,49,49,49,49,49,49,49,49,49,50,50,50,50,51,51,51,52,52,52,52,53,53,53,53,54,54,54,54," \
    "54,55,55,55,55,55,55,55,55,55,55,55,55,55,54,54,54,54,54,54,54,54,54,54,54,54,54,54,54,54,54,55,55,55,55,56,56" \
    ",56,56,57,57,57,58,58,59,59,59,60,60,60,61,61,61,62,62,62,62,62,63,63,63,63,63,63,63,63,63,63,63,63,63,63,62,6" \
    "2,62,62,62,62,62,62,62,62,62,62,62,62,62,62,62,62,62,62,62,63,63,63,63,63,64,64,64,64,65,65,65,65,65,66,66,66," \
    "66,66,66,66,66,66,66,66,66,66,66,66,66,66,65,65,65,65,64,64,64,63,63,63,63,62,62,62,62,61,61,61,61,61,61,60,60" \
    ",60,60,60,60,60,60,60,60,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,61,60,60,60,60,59,59,59,58,5" \
    "8,58,57,57,57,56,56,55,55,55,54,54,54,54,53,53,53,53,53,52,52,52,52,52,52,52,52,52,52,52,52,53,53,53,53,53,53," \
    "53,53,53,53,53,54,54,54,53,53,53,53,53,53,53,53],\"c\":[[0,160],[29,185],[125,160]],\"ch\":\"e0ffffffffffffffffffff" \
    "ffffffff1f000000ffffffffffffffffffffffffffffffffff\",\"fin\":930}"

#define HIST_PIECE3 \
    "{\"t\":[159,159,158,158,158,158,157,157,157,157,157,157,156,156,156,156,156,155,155,154,154,154,154,154,154,154," \
    "154,154,155,155,155,156,156,156,156,156,156,157,157,158,158,159,160,160,161,161,161,161,161,161,161,161,161,16" \
    "1,162,163,163,163,164,164,164,165,165,165,165,165,165,165,164,165,165,166,166,166,166,166,167,167,167,167,167," \
    "167,168,168,168,168,168,168,168,168,168,168,169,169,169,169,170,170,170,170,170,170,170,170,170,170,170,170,17" \
    "0,170,172,172,172,172,172,172,172,172,172,171,171,171,171,173,173,172,172,172,172,171,171,170,170,169,169,168," \
    "167,168,168,167,167,167,167,167,167,167,166,166,166,166,165,166,166,165,165,165,165,165,165,165,165,165,165,16" \
    "5,166,166,165,165,165,165,164,164,164,164,164,164,164,164,166,166,166,166,166,165,165,165,165,165,165,164,164," \
    "165,166,166,166,166,166,166,166,166,166,166,166,166,165,166,166,166,166,167,167,167,167,167,167,167,167,167,16" \
    "7,168,168,167,167,167,167,167,167,168,168,168,168,168,168,169,169,169,169,168,168,168,168,168,168,168,168,169," \
    "170,170,170,170,170,170,170,169,169,169,169,169,169,169,170,170,170,170,170,170,170,170,170,170,170,169,169,16" \
    "9,170,170,170,170,170,170,171,171,171,171,171,170,170,171],\"h\":[50,50,50,49,49,49,49,48,48,48,48,48,48,48,48,4" \
    "8,48,48,48,48,48,48,48,48,48,49,49,49,49,50,50,50,50,51,51,51,51,52,52,52,52,52,52,53,53,53,53,53,53,53,53,53," \
    "53,53,53,53,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,52,53,53,53,53,54,54,54,55,55,55,56,56,57" \
    ",57,57,58,58,58,59,59,59,60,60,60,60,60,60,61,61,61,61,61,61,61,61,61,61,61,60,60,60,60,60,60,60,60,60,60,60,6" \
    "0,60,60,60,60,60,60,60,60,61,61,61,61,61,62,62,62,62,63,63,63,64,64,64,64,65,65,65,65,65,65,65,65,65,65,65,65," \
    "65,65,65,65,65,65,64,64,64,64,63,63,63,63,62,62,62,62,61,61,61,61,61,61,60,60,60,60,60,60,60,60,60,60,60,61,61" \
    ",61,61,61,61,61,61,61,61,61,61,62,62,61,61,61,61,61,61,61,61,60,60,60,60,59,59,59,58,58,58,57,57,57,56,56,55,5" \
    "5,55,54,54,54,54,53,53,53,53,53,53,52,52,52,52,52,52,52,52,53,53,53,53,53,53,53,53,53,53,53,53,53,53,53,53,53," \
    "53,53,53,53,53,53,52,52,52,52,51,51,51,51,50,50],\"c\":[[0,160],[29,180],[125,160]],\"ch\":\"00fcffffffffffffffffff" \
    "ffffffff1f00e0ffffffffffffffffffffffffffffffffffff\",\"fin\":930}"

#define HIST_PIECE4 \
    "{\"t\":[219,219,219,218,218,217,217,216,216,215,214,215,214,214,213,213,212,212,212,212,211,212,212,212,211,211," \
    "211,210,210,208,206,206,205,203,202,201,200,199,198,197,196,196,195,194,193,192,191,190,189,189,188,187,188,18" \
    "7,187,186,186,185,185,184,184,183,183,183,182,181,181,181,180,180,180,180,181,180,180,180,179,179,179,178,178," \
    "177,178,178,178,177,177,177,177,177,177,177,177,178,178,177,177,177,176,176,176,176,176,177,177,177,177,177,17" \
    "7,177,177,177,177,177,177,177,177,177,177,177,177,177,177,178,179,179,179,179,178,178,178,178,178,178,179,179," \
    "179,179,179,179,180,180,180,180,181,181,180,180,180,180,180,180,180,180,181,182,182,182,182,182,182,182,182,18" \
    "2,183,183,183,183,183,183,183,183,183,184,184,185,185,185,185,185,185,185,184,184,184,185,185,186,186,186,186," \
    "186,187,187,187,188,188,187,187,187,187,187,187,187,187,188,189,189,189,189,189,189,189,189,189,189,190,189,18" \
    "9,189,189,190,190,190,190,190,191,192,191,191,191,191,191,191,191,190,191,192,192,192,192,192,192,192,192,192," \
    "193,193,193,193,193,192,192,192,192,192,193,195,196,197,199,200,201,201,202,203,204,205,206,207,207,208,209,21" \
    "0,211,211,212,214,214,215,215,215,216,216,216,216,217,218],\"h\":[53,53,53,53,53,53,53,53,53,53,53,53,53,53,53,5" \
    "3,54,54,54,54,54,55,55,55,55,56,56,56,56,56,56,57,57,57,57,57,57,57,57,57,57,57,57,57,57,57,57,56,56,56,56,56," \
    "56,56,56,56,56,56,55,55,56,56,56,56,56,56,56,56,57,57,57,57,58,58,58,59,59,59,60,60,61,61,61,62,62,62,63,63,63" \
    ",63,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,6" \
    "5,65,65,65,66,66,66,66,67,67,67,68,68,68,69,69,69,69,70,70,70,70,70,70,70,70,70,70,70,70,70,70,70,70,69,69,69," \
    "69,69,68,68,68,68,67,67,67,67,67,67,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,67,67,67,67,67,67,67,67,67,68" \
    ",68,68,68,68,68,68,67,67,67,67,67,67,66,66,66,66,65,65,65,64,64,64,63,63,63,62,62,61,61,61,61,60,60,60,60,59,5" \
    "9,59,59,59,59,59,59,59,59,59,59,59,59,59,59,59,59,59,59,59,59,59,60,60,59,59,59,59,59,59,59,59,59,58,58,58,58," \
    "57,57,57,57,56,56,56,55,55,55,54,54,54,54,54,53],\"c\":[[0,220],[29,180],[257,220]],\"ch\":\"c0ffff1f00000080ffffff" \
    "ffffffffffffffffffffffffffffffffffffffffffffffffff\",\"fin\":930}"

#define HIST_EXT \
    "{\"ext\":[61,61,62,62,61,61,60,60,58,57,56,55,54,52,52,51,51,50,50,51,51,51,51,52,52,51,51,50,49,48,47,46,45,44," \
    "43,43,42,42,42,43,43,44,44,45,45,45,45,44,44,43,42,41,40,39,39,38,38,38,38,39,40,40,41,42,42,42,43,42,42,42,41" \
    ",40,39,39,38,38,38,38,39,40,41,41,42,43,44,45,45,45,45,45,45,44,44,43,43,43,43,43,44,45,46,47,48,49,50,51,52,5" \
    "2,52,52,52,52,52,51,51,51,51,52,53,54,55,56,57,59,60,61,62,62,62,63,62,62,62,62,62,62,62,63,63,64,65,67,68,69," \
    "70,71,72,73,73,73,73,73,73,73,73,73,73,73,74,74,75,77,78,79,80,81,82,83,83,83,83,83,83,82,82,82,82,82,82,83,83" \
    ",84,85,87,88,88,89,90,90,90,90,89,89,88,88,87,87,87,87,87,88,88,89,90,91,92,92,92,92,92,92,91,91,90,89,88,88,8" \
    "7,87,87,87,88,88,89,89,90,90,90,90,90,89,89,88,87,86,85,84,83,83,82,82,83,83,83,84,84,84,84,84,84,83,82,81,80," \
    "78,77,76,75,75,74,74,74,74,74,74,75,75,75,74,74,73,72,71,70,69,67,66,65,64,64,63,63,63,63,64,64,64,64]}"

#define HIST_BALLON \
    "{\"t\":[600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600," \
    "600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,60" \
    "0,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600," \
    "600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,60" \
    "0,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600," \
    "600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,60" \
    "0,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600," \
    "600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,60" \
    "0,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600," \
    "600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,600,583,583,583,583,583,583,583,583,583,583,58" \
    "3,583,583,583,583,583,583,583,583,582,582,582,582,582,582],\"c\":[[0,55]],\"l\":[[113,40],[179,15],[263,45]],\"ch\":" \
    "\"00000000fcffffffff0f0000000000000000000000000000000000000000000000000000\",\"hc\":\"00000000fcffffffffffff3f00000" \
    "0000000000000000000f0ffffffff00000000000000\",\"fin\":930}"

#define HIST_AIR0 \
    "{\"points\":[5,6,6,6,6,6,5,4,4,4,4,4,5,5,6,6,6,6,6,5,4,4,4,4,4,5,6,6,6,6,6,5,5,4,4,4,4,4,22,34,39,35,23,6,5,4,4," \
    "4]}"

#define HIST_AIR1 \
    "{\"points\":[6,7,7,7,7,7,6,5,5,5,5,5,6,6,7,7,7,7,7,6,5,5,5,5,5,6,7,7,7,7,7,6,6,5,5,5,5,5,6,7,7,7,7,7,6,5,5,5]}"

#define HIST_POELE \
    "{\"fumees\":[22,23,23,24,24,24,23,22,22,21,20,20,20,20,20,21,22,22,23,23,23,23,22,21,21,20,19,19,19,19,20,20,21," \
    "22,22,23,22,22,21,21,20,19,18,18,18,19,19,20,21,21,22,22,22,21,20,20,19,18,18,18,18,18,19,19,20,21,21,21,21,20" \
    ",20,19,18,17,17,17,17,18,18,19,20,20,21,20,20,19,19,18,17,17,16,16,17,17,18,19,19,20,20,20,19,19,18,17,16,16,1" \
    "6,16,16,17,17,18,19,19,19,19,18,18,17,16,15,15,15,15,16,16,17,18,18,19,19,18,18,17,16,15,15,14,14,15,15,16,17," \
    "17],\"corps\":[26,26,26,26,26,26,26,26,26,26,26,26,26,26,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,2" \
    "5,25,25,25,26,26,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25," \
    "25,25,25,25,25,25,25,25,25,24,24,24,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,24,24" \
    ",24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,25,25,25,25,25,25,24,24,24,24,24,24,24,24,24,24,24,24,2" \
    "4,24],\"tirage\":[45,46,47,48,49,49,48,47,46,45,44,42,41,40,40,40,41,41,42,44,45,46,47,47,47,47,46,45,44,42,41,4" \
    "0,39,39,39,39,40,41,42,43,45,45,46,46,46,45,44,42,41,40,39,38,37,37,38,39,40,41,42,43,44,45,45,44,44,43,41,40," \
    "39,37,37,36,36,36,37,38,39,41,42,43,43,43,43,42,41,40,39,37,36,35,35,35,35,36,37,38,39,40,41,42,42,42,41,40,39" \
    ",37,36,35,34,33,33,34,34,36,37,38,39,40,41,41,40,40,39,37,36,35,33,33,32,32,32,33,34,35,37,38,39,39,39,39,38,3" \
    "7,36,35],\"fin\":930}"

// --- La table rejouée par ui_mqtt_recevoir, dans l'ordre de la rafale de Home Assistant à la connexion ---
struct message_demo_t
{
    const char *sujet, *message;
};
static const message_demo_t MESSAGES[] = {
    // La maison : l'heure de la démo est là (« maj » = 1794925800 = 17/11/2026 15:30 CET, lu par ui_appareil_heure)
    {UI_MQTT_PREFIXE "maison/etat",
     "{\"ext_temp\":6.2,\"ext_hum\":72,\"maj\":1794925800,\"phase\":\"jour\",\"presence\":1}"},
    // Chauffage : 5 pièces, modes variés, deux en chauffe
    {UI_MQTT_PREFIXE "thermostats/cuisine/etat",
     "{\"batterie\":74,\"chauffe\":0,\"consigne\":19.0,\"hum\":52.4,\"mode\":\"confort\",\"signal\":172,\"temp\":18.9}"},
    {UI_MQTT_PREFIXE "thermostats/sejour/etat",
     "{\"batterie\":88,\"chauffe\":1,\"consigne\":20.0,\"hum\":55.1,\"mode\":\"confort\",\"signal\":198,\"temp\":19.6}"},
    {UI_MQTT_PREFIXE "thermostats/chambre1/etat",
     "{\"batterie\":61,\"chauffe\":0,\"consigne\":16.0,\"hum\":58.2,\"mode\":\"eco\",\"signal\":156,\"temp\":17.4}"},
    {UI_MQTT_PREFIXE "thermostats/chambre2/etat",
     "{\"batterie\":95,\"chauffe\":0,\"consigne\":16.0,\"hum\":57.0,\"mode\":\"eco\",\"signal\":210,\"temp\":17.1}"},
    {UI_MQTT_PREFIXE "thermostats/sdb/etat",
     "{\"batterie\":56,\"chauffe\":1,\"consigne\":22.0,\"hum\":66.5,\"mode\":\"manuel\",\"signal\":164,\"temp\":21.8}"},
    // Eau chaude : relais de la nuit en heures creuses, douches du matin et du soir dans la courbe
    {UI_MQTT_PREFIXE "ballon/etat",
     "{\"chauffe\":0,\"consigne\":55,\"eco\":0,\"mode\":\"auto\",\"temp\":58.2,\"chaude_jour\":96,\"froide_jour\":142,"
     "\"kwh_jour\":2.6,\"debit\":0,\"puissance\":0,\"combles\":23.4}"},
    {UI_MQTT_PREFIXE "ballon/historique", HIST_BALLON},
    // VMC : maison en petite vitesse (automatisme humidité), sous-sol arrêté
    {UI_MQTT_PREFIXE "vmc/maison/etat",
     "{\"auto\":1,\"hum\":54,\"hum_lieu\":\"salle de bain\",\"seuil\":65,\"vitesse\":1}"},
    {UI_MQTT_PREFIXE "vmc/sous_sol/etat", "{\"auto\":0,\"hum\":58,\"hum_lieu\":\"sous-sol\",\"seuil\":0,\"vitesse\":0}"},
    // Air : deux purificateurs ; pic de poussière à l'atelier en fin d'après-midi (impression 3D)
    {UI_MQTT_PREFIXE "air/atelier/etat",
     "{\"en_ligne\":1,\"info\":\"Filtre 72 %\",\"mode\":0,\"modes\":[\"Arrêt\",\"Fan\",\"1\",\"2\",\"3\"],\"pm25\":12}"},
    {UI_MQTT_PREFIXE "air/sejour/etat",
     "{\"en_ligne\":1,\"info\":\"Filtre 58 %\",\"mode\":1,\"modes\":[\"Arrêt\",\"Auto\",\"Silence\",\"Fan\"],\"pm25\":6}"},
    {UI_MQTT_PREFIXE "air/atelier/historique", HIST_AIR0},
    {UI_MQTT_PREFIXE "air/sejour/historique", HIST_AIR1},
    // Ouvrants : store banne rentré, volet à mi-hauteur, toile de vidéoprojecteur remontée
    {UI_MQTT_PREFIXE "ouvrants/store_terrasse/etat", "{\"en_ligne\":1,\"mouvement\":\"arret\",\"position\":0}"},
    {UI_MQTT_PREFIXE "ouvrants/volet_cuisine/etat", "{\"en_ligne\":1,\"mouvement\":\"arret\",\"position\":35}"},
    {UI_MQTT_PREFIXE "ouvrants/toile/etat", "{\"en_ligne\":1,\"firetv\":0,\"mouvement\":\"arret\",\"position\":100}"},
    // Croquettes : distributeur en ligne, stock plein, 3 repas programmés sur 4
    {UI_MQTT_PREFIXE "croquettes/etat",
     "{\"autonomie\":8,\"bol\":90,\"jour_semaine\":1,\"jours\":[{\"g\":430,\"n\":5},{\"g\":595,\"n\":7},{\"g\":520,\"n\":6},"
     "{\"g\":610,\"n\":7},{\"g\":585,\"n\":7},{\"g\":640,\"n\":8},{\"g\":600,\"n\":7}],\"repas\":[{\"a\":1,\"h\":390,\"q\":15},"
     "{\"a\":1,\"h\":750,\"q\":15},{\"a\":1,\"h\":1170,\"q\":15},{\"a\":0,\"h\":1290,\"q\":10}],\"stock\":1650,\"vide\":0}"},
    // Café et bouilloire : deux programmes le matin, dernière tasse à 20 h
    {UI_MQTT_PREFIXE "cafe/etat",
     "{\"bouilloire\":{\"chauffe\":0,\"en_ligne\":1,\"h\":480,\"prog\":1,\"temp\":90},\"cafe\":{\"allumee\":1,\"en_ligne\":1,"
     "\"h\":420,\"marche\":0,\"prog\":1},\"recentes\":[505,1175]}"},
    // Poêle : éteint, 12 h d'historique (refroidissement de la journée)
    {UI_MQTT_PREFIXE "poele/etat",
     "{\"brassage\":0,\"consigne\":240,\"corps\":24.0,\"en_ligne\":1,\"feu\":\"eteint\",\"fumees\":18,\"kd\":0.0,\"ki\":0.01,"
     "\"kp\":3.0,\"limite\":30,\"mode\":\"auto\",\"recharge\":180,\"tirage\":40,\"tirage_manuel\":100,\"tirage_min\":30}"},
    {UI_MQTT_PREFIXE "poele/historique", HIST_POELE},
    // Lumières : ids = LAMPES[] de ui_mqtt.cpp (noms affichés génériques) ; trois allumées, une hors ligne
    {UI_MQTT_PREFIXE "lumieres/lampadaire_1/etat",
     "{\"couleur\":1,\"en_ligne\":1,\"lum\":80,\"kelvin\":2700,\"nom\":\"Lampadaire 1\",\"on\":1,\"piece\":\"S\\u00e9jour\"}"},
    {UI_MQTT_PREFIXE "lumieres/lampadaire_2/etat",
     "{\"couleur\":1,\"en_ligne\":1,\"nom\":\"Lampadaire 2\",\"on\":0,\"piece\":\"S\\u00e9jour\"}"},
    {UI_MQTT_PREFIXE "lumieres/lampadaire_3/etat",
     "{\"couleur\":1,\"en_ligne\":1,\"nom\":\"Lampadaire 3\",\"on\":0,\"piece\":\"S\\u00e9jour\"}"},
    {UI_MQTT_PREFIXE "lumieres/lampe_fenetre/etat",
     "{\"couleur\":0,\"en_ligne\":1,\"lum\":60,\"kelvin\":3000,\"nom\":\"Lampe fen\\u00eatre\",\"on\":1,\"piece\":\"S\\u00e9jour\"}"},
    {UI_MQTT_PREFIXE "lumieres/globe_lunaire/etat",
     "{\"couleur\":1,\"en_ligne\":1,\"lum\":40,\"teinte\":210,\"sat\":60,\"nom\":\"Globe lunaire\",\"on\":1,\"piece\":\"S\\u00e9jour\"}"},
    {UI_MQTT_PREFIXE "lumieres/bandeau_wled/etat",
     "{\"couleur\":1,\"en_ligne\":0,\"nom\":\"Bandeau WLED\",\"on\":0,\"piece\":\"Atelier\"}"},
    {UI_MQTT_PREFIXE "lumieres/bandeau_lidl/etat",
     "{\"couleur\":1,\"en_ligne\":1,\"nom\":\"Bandeau Lidl\",\"on\":0,\"piece\":\"Atelier\"}"},
    {UI_MQTT_PREFIXE "lumieres/panneau_led/etat",
     "{\"couleur\":1,\"en_ligne\":1,\"nom\":\"Panneau LED\",\"on\":0,\"piece\":\"Atelier\"}"},
    {UI_MQTT_PREFIXE "lumieres/bandeau_bureau/etat",
     "{\"couleur\":0,\"en_ligne\":1,\"lum\":100,\"kelvin\":4000,\"nom\":\"Bandeau bureau\",\"on\":1,\"piece\":\"Bureau\"}"},
    {UI_MQTT_PREFIXE "lumieres/lampe_bureau/etat",
     "{\"couleur\":0,\"en_ligne\":1,\"nom\":\"Lampe de bureau\",\"on\":0,\"piece\":\"Bureau\"}"},
    {UI_MQTT_PREFIXE "lumieres/chevet/etat",
     "{\"couleur\":1,\"en_ligne\":1,\"nom\":\"Chevet\",\"on\":0,\"piece\":\"Chambre\"}"},
    // Panneau LED de luminothérapie : séance arrêtée, minuterie 20 min en proche infrarouge
    {UI_MQTT_PREFIXE "panneau_led/etat",
     "{\"allumees\":0,\"duree\":20,\"i590\":0,\"i630\":0,\"i660\":0,\"i810\":0,\"i830\":0,\"i850\":0,\"i940\":0,"
     "\"longueurs\":\"nir\",\"mode\":\"minuterie\",\"radiateur\":0,\"restant\":-1,\"seance\":0,\"toutes\":0,\"ventilos\":0}"},
    // Imprimante 3D : une impression en cours ; miniatures hors ligne en démo (base vide)
    {UI_MQTT_PREFIXE "imprimante/etat",
     "{\"buse\":214,\"buse_c\":215,\"caisson\":24,\"caisson_c\":0,\"carte_mere\":1,\"chauffage\":1,\"couche\":33,\"couches\":72,"
     "\"etat\":\"impression\",\"fichier\":\"support_camera_v3_0.2mm_PLA_Generic Klipper Printer_1h35m.gcode\",\"filtration\":1,\"fin\":1385,\"lumiere\":1,\"plateau\":60,"
     "\"plateau_c\":60,\"progression\":46,\"puissance\":118,\"pm1\":8,\"reste_min\":95}"},
    {UI_MQTT_PREFIXE "imprimante/fichiers", "{\"base\":\"\",\"nb\":6}"},
    {UI_MQTT_PREFIXE "imprimante/fichiers/0",
     "{\"date\":\"10/11\",\"fichier\":\"support_camera_v3_0.2mm_PLA_Generic Klipper Printer_1h35m.gcode\",\"matiere\":\"PLA "
     "\\u00b7 38 g\",\"minutes\":95,\"nom\":\"support_camera_v3\"}"},
    {UI_MQTT_PREFIXE "imprimante/fichiers/1",
     "{\"date\":\"08/11\",\"fichier\":\"boitier_capteur_0.2mm_PLA_Generic Klipper Printer_2h10m.gcode\",\"matiere\":\"PLA "
     "\\u00b7 44 g\",\"minutes\":130,\"nom\":\"boitier_capteur\"}"},
    {UI_MQTT_PREFIXE "imprimante/fichiers/2",
     "{\"date\":\"05/11\",\"fichier\":\"boite_rangement_0.2mm_PETG_Generic Klipper Printer_3h05m.gcode\",\"matiere\":\"PETG "
     "\\u00b7 88 g\",\"minutes\":185,\"nom\":\"boite_rangement\"}"},
    {UI_MQTT_PREFIXE "imprimante/fichiers/3",
     "{\"date\":\"02/11\",\"fichier\":\"crochet_mural_0.2mm_PLA_Generic Klipper Printer_0h35m.gcode\",\"matiere\":\"PLA "
     "\\u00b7 12 g\",\"minutes\":35,\"nom\":\"crochet_mural\"}"},
    {UI_MQTT_PREFIXE "imprimante/fichiers/4",
     "{\"date\":\"29/10\",\"fichier\":\"guide_perceuse_0.2mm_PLA_Generic Klipper Printer_1h15m.gcode\",\"matiere\":\"PLA "
     "\\u00b7 26 g\",\"minutes\":75,\"nom\":\"guide_perceuse\"}"},
    {UI_MQTT_PREFIXE "imprimante/fichiers/5",
     "{\"date\":\"26/10\",\"fichier\":\"protege_lampe_0.2mm_PLA_Generic Klipper Printer_0h50m.gcode\",\"matiere\":\"PLA "
     "\\u00b7 18 g\",\"minutes\":50,\"nom\":\"protege_lampe\"}"},
    // Musique (le reste de la page vient de Music Assistant : voir ui_demo_ma)
    {UI_MQTT_PREFIXE "musique/etat", "{\"picore\":\"pret\",\"ampli\":1}"},
    // PC (agent btop) : noms de processus génériques, modèles de IA publics
    {UI_MQTT_PREFIXE "pc/etat",
     "{\"cpu\":14,\"freq\":3600,\"cpu_temp\":54,\"cpu_w\":48,\"coeurs\":[24,18,15,12,10,8,6,5,4,3,2,2,1,1,0,0],\"ram\":24.5,"
     "\"cache\":18.2,\"ram_total\":32.0,\"swap\":0,\"gpus\":[{\"nom\":\"3060\",\"charge\":38,\"temp\":57,\"w\":110,\"vram\":8.2,"
     "\"vram_total\":12.0,\"ps\":\"P0\",\"f\":1800,\"fm\":1900,\"blocs\":[{\"n\":\"llama-3.1-8B-Q4_K_M\",\"mo\":5100,"
     "\"c\":\"22d3ee\"},{\"n\":\"whisper-large-v3\",\"mo\":3100,\"c\":\"35c46a\"}]}],\"net_bas\":1.2,\"net_haut\":0.4,"
     "\"disque_l\":2.0,\"disque_e\":0.0,\"depuis\":\"1 j 04 h\",\"heure\":\"15:30\"}"},
    {UI_MQTT_PREFIXE "pc/processus",
     "[{\"nom\":\"firefox\",\"role\":\"\",\"cpu\":8.4,\"mem\":1.6,\"vram\":0.0},{\"nom\":\"code\",\"role\":\"\",\"cpu\":4.1,"
     "\"mem\":0.9,\"vram\":0.0},{\"nom\":\"llama-server\",\"role\":\"llama-local\",\"cpu\":0.0,\"mem\":1.8,\"vram\":8.0},"
     "{\"nom\":\"mpd\",\"role\":\"\",\"cpu\":0.1,\"mem\":0.1,\"vram\":0.0},{\"nom\":\"kwin_wayland\",\"role\":\"plasma\",\"cpu\":"
     "1.4,\"mem\":0.3,\"vram\":0.0},{\"nom\":\"python\",\"role\":\"assistant\",\"cpu\":0.3,\"mem\":0.2,\"vram\":0.0}]"},
    // Réglages de la maison (carillon, terrasse) et appareils vus par HA (noms génériques, un hors ligne)
    {UI_MQTT_PREFIXE "maison/reglages",
     "{\"melodie\":\"Simpsons\",\"melodies\":[\"Mario\",\"Simpsons\",\"Halloween\",\"No\\u00ebl\"],\"volume\":40,"
     "\"terrasse_porte\":0,\"terrasse_mouvement\":1}"},
    {UI_MQTT_PREFIXE "maison/appareils",
     "{\"liste\":[[\"Sonde cuisine\",\"Cuisine\",1],[\"Relais chauffage cuisine\",\"Cuisine\",1],[\"hotte cuisine\","
     "\"Cuisine\",1],[\"Cafetière\",\"Cuisine\",1],[\"Bouilloire\",\"Cuisine\",1],[\"Station météo\",\"Cuisine\",1],"
     "[\"Sonde séjour\",\"Séjour\",1],[\"Détecteur de fumée\",\"Séjour\",1],[\"Détecteur de monoxyde\",\"Séjour\",1],"
     "[\"Distributeur croquettes\",\"Séjour\",1],[\"Toile VP et ampli\",\"Séjour\",1],[\"Sonde chambre\",\"Chambre\",1],"
     "[\"Relais chauffage chambre\",\"Chambre\",1],[\"Sonde salle de bain\",\"Salle de bain\",1],[\"Déshumidificateur\","
     "\"Salle de bain\",1],[\"Ballon eau chaude\",\"Combles\",1],[\"sonoff 4 ch vmc\",\"Combles\",1],[\"Panneau LED\","
     "\"Atelier\",1],[\"Prise imprimante 3D\",\"Atelier\",1],[\"Relais chauffage atelier\",\"Atelier\",1],[\"Store "
     "terrasse\",\"Terrasse\",1],[\"Passerelle Zigbee\",\"\",1],[\"Espace de stockage NAS\",\"Sous-sol\",0]]]}"},
    // Graphes 24 h (après les états : les courbes se calent sur les valeurs ci-dessus)
    {UI_MQTT_PREFIXE "thermostats/cuisine/historique", HIST_PIECE0},
    {UI_MQTT_PREFIXE "thermostats/sejour/historique", HIST_PIECE1},
    {UI_MQTT_PREFIXE "thermostats/chambre1/historique", HIST_PIECE2},
    {UI_MQTT_PREFIXE "thermostats/chambre2/historique", HIST_PIECE3},
    {UI_MQTT_PREFIXE "thermostats/sdb/historique", HIST_PIECE4},
    {UI_MQTT_PREFIXE "maison/historique", HIST_EXT},
};

void ui_demo_lancer()
{
    for (const message_demo_t &m : MESSAGES)
        ui_mqtt_recevoir(m.sujet, (const uint8_t *)m.message, (unsigned)strlen(m.message));

    // 13/11 : page IA et ciel animé. Ces trois-là sont calculés à l'allumage, en RELATIF par rapport à
    // l'horloge de l'écran — réglée juste avant par le « maj » de maison/etat ci-dessus (au simulateur :
    // l'horloge du PC). Le firmware déduit la phase du ciel de l'ORDRE des prochains événements (coucher
    // avant lever = jour, comme les capteurs soleil de HA) et exige des horodatages de moins d'un jour :
    // en figeant ces horodatages, la page retomberait sur le ciel de repli et les compte à rebours
    // (« dans 2 h 13 ») seraient faux. Calculé : coucher 1 h 50 après l'allumage, lever le lendemain
    // (soleil bas à l'ouest, 82 % de l'arc), réinitialisations 2 h 13 et 6 j 14 h plus tard, messages IA
    // vieux de 5 min et 1 min (sous les péremptions : 10 min et 2 min).
    // Lune : premier quartier cohérent (âge 7,9 j -> 56 % avec la formule de
    // ha_propositions/ciel_dashboard_guition.yaml) ; le firmware ne la montre qu'hors du plein jour :
    // en démo (jour) elle est cachée, mais les valeurs publiées restent cohérentes entre elles.
    const uint32_t t = (uint32_t)time(nullptr);
    char b[512];
    snprintf(b, sizeof b, "{\"aube\":%u,\"lever\":%u,\"coucher\":%u,\"crepuscule\":%u,"
             "\"lune_age\":7.9,\"lune_pct\":56,\"meteo\":\"partlycloudy\",\"nuages\":45,\"pluie\":0,\"neige\":0}",
             t + 56700u, t + 58740u, t + 6600u, t + 8640u);
    ui_mqtt_recevoir(UI_MQTT_PREFIXE "maison/ciel", (const uint8_t *)b, (unsigned)strlen(b));
    snprintf(b, sizeof b, "{\"comptes\":[{\"nom\":\"compte-un@exemple.invalid\",\"h5\":42,\"semaine\":66,"
             "\"h5_reset\":%u,\"semaine_reset\":%u,\"maj\":%u}]}", t + 7980u, t + 568800u, t - 300u);
    ui_mqtt_recevoir(UI_MQTT_PREFIXE "pc/ia", (const uint8_t *)b, (unsigned)strlen(b));
    snprintf(b, sizeof b, "{\"etat\":\"reflechit\",\"alerte\":\"\",\"tokens\":51700,\"outils\":96,"
             "\"prompts\":7,\"sessions\":1,\"histo\":[0,0,0,0,0,0,0,0,0,0,0,0,1200,3400,5600,4200,3100,0,0,0,"
             "2400,4800,6100,5200,4400,3800,2900,2100,1600,900],\"maj\":%u}", t - 60u);
    ui_mqtt_recevoir(UI_MQTT_PREFIXE "pc/ia_activite", (const uint8_t *)b, (unsigned)strlen(b));
}

// --- Écho des commandes : ce que Home Assistant publierait en retour ---
// ids à garder IDENTIQUES aux tables statiques de ui_mqtt.cpp (PIECES[], MODES[], LAMPES[], VMC_ZONES[], OUVRANTS[])
static const char *const PIECES_DEMO[] = {"cuisine", "sejour", "chambre1", "chambre2", "sdb"};
static const char *const MODES_DEMO[] = {"arret", "horsgel", "eco", "confort", "manuel"};
static const char *const BC_MODES_DEMO[] = {"arret", "auto", "force"};
static const char *const VMC_DEMO[] = {"maison", "sous_sol"};
static const char *const OUVRANTS_DEMO[] = {"store_terrasse", "volet_cuisine", "toile"};
static const char *const LAMPES_DEMO[] = {"lampadaire_1", "lampadaire_2", "lampadaire_3", "lampe_fenetre", "globe_lunaire",
                                          "bandeau_wled", "bandeau_lidl", "panneau_led", "bandeau_bureau", "lampe_bureau",
                                          "chevet"};
static const char *const PL_MODES_DEMO[] = {"auto", "manuel", "levier"};
static const char *const PL_FEUX_DEMO[] = {"eteint", "allumage", "regulation", "recharger"};

// renvoie à ui_mqtt_recevoir l'état que Home Assistant publierait après la commande
static void renvoyer(const char *sujet, const char *b)
{
    ui_trace("[demo][echo] %s : %s\n", sujet, b);
    ui_mqtt_recevoir(sujet, (const uint8_t *)b, (unsigned)strlen(b));
}

bool ui_demo_echo(const char *sujet, const char *message)
{
    const size_t lp = strlen(UI_MQTT_PREFIXE);
    if (strncmp(sujet, UI_MQTT_PREFIXE, lp) != 0)
        return false;
    const char *s = sujet + lp;
    const size_t n = strlen(s);
    if (n < 4 || strcmp(s + n - 4, "/set") != 0)
        return false; // seule la commande a un écho : le reste partait au broker de toute façon

    char t[128]; // le sujet « etat » correspondant : « .../set » -> « .../etat »
    snprintf(t, sizeof t, "%s", sujet);
    t[strlen(t) - 4] = 0;
    strcat(t, "/etat");

    JsonDocument d;
    if (deserializeJson(d, message))
        return false;
    char b[512];

    if (strncmp(s, "thermostats/", 12) == 0)
    {
        for (int k = 0; k < TH_NB_PIECES; k++)
        {
            size_t m = strlen(PIECES_DEMO[k]);
            if (strncmp(s + 12, PIECES_DEMO[k], m) != 0 || strcmp(s + 12 + m, "/set") != 0)
                continue;
            const th_piece_t &p = th_pieces[k]; // la page a déjà appliqué le choix : HA confirme l'état courant
            snprintf(b, sizeof b, "{\"temp\":%.1f,\"hum\":%.1f,\"chauffe\":%d,\"batterie\":%d,\"signal\":%d,\"mode\":\"%s\",\"consigne\":%.1f}",
                     (double)p.temp, (double)p.hum, (int)(p.mode != TH_ARRET && p.temp < p.consigne - 0.3f), p.batterie,
                     p.signal, MODES_DEMO[p.mode], (double)p.consigne);
            renvoyer(t, b);
            return true;
        }
    }
    else if (strncmp(s, "vmc/", 4) == 0)
    {
        for (int z = 0; z < VMC_NB; z++)
        {
            size_t m = strlen(VMC_DEMO[z]);
            if (strncmp(s + 4, VMC_DEMO[z], m) != 0 || strcmp(s + 4 + m, "/set") != 0)
                continue;
            snprintf(b, sizeof b, "{\"auto\":%d,\"hum\":%d,\"hum_lieu\":\"%s\",\"seuil\":%d,\"vitesse\":%d}",
                     (int)vmc_zones[z].auto_en_cours, vmc_zones[z].hum, vmc_zones[z].lieu, vmc_zones[z].seuil,
                     vmc_zones[z].vitesse);
            renvoyer(t, b);
            return true;
        }
    }
    else if (strncmp(s, "lumieres/", 9) == 0)
    {
        for (int i = 0; i < LU_NB; i++)
        {
            size_t m = strlen(LAMPES_DEMO[i]);
            if (strncmp(s + 9, LAMPES_DEMO[i], m) != 0 || strcmp(s + 9 + m, "/set") != 0)
                continue;
            const lu_lampe_t &l = lu_lampes[i];
            snprintf(b, sizeof b, "{\"en_ligne\":%d,\"nom\":\"%s\",\"piece\":\"%s\",\"couleur\":%d,\"on\":%d,\"lum\":%d,\"kelvin\":%d,\"teinte\":%d,\"sat\":%d}",
                     (int)!l.hors_ligne, l.nom, l.piece, (int)l.couleur, (int)l.on, l.lum, l.kelvin, l.teinte, l.sat);
            renvoyer(t, b);
            return true;
        }
    }
    else if (strncmp(s, "ouvrants/", 9) == 0)
    {
        for (int i = 0; i <= OV_NB; i++) // OV_NB = la toile
        {
            size_t m = strlen(OUVRANTS_DEMO[i]);
            if (strncmp(s + 9, OUVRANTS_DEMO[i], m) != 0 || strcmp(s + 9 + m, "/set") != 0)
                continue;
            int pos = i < OV_NB ? ov_ouvrants[i].pos : ov_toile;
            const char *a = d["action"] | "";
            if (!strcmp(a, "ouvrir"))
                pos = 100; // position 100 = ouvert (volet relevé, store sorti, toile remontée)
            else if (!strcmp(a, "fermer"))
                pos = 0;
            else if (!strcmp(a, "position"))
                pos = d["position"] | pos;
            if (i == OV_NB)
                snprintf(b, sizeof b, "{\"en_ligne\":1,\"firetv\":%d,\"mouvement\":\"arret\",\"position\":%d}", (int)ov_firetv, pos);
            else
                snprintf(b, sizeof b, "{\"en_ligne\":1,\"mouvement\":\"arret\",\"position\":%d}", pos);
            renvoyer(t, b);
            return true;
        }
    }
    else if (strcmp(s, "ballon/set") == 0)
    {
        const bool chauffe = bc_etat.mode == BC_FORCE || (bc_etat.mode == BC_AUTO && bc_etat.temp < bc_etat.consigne - 2.0f);
        snprintf(b, sizeof b, "{\"chauffe\":%d,\"consigne\":%.0f,\"eco\":%d,\"mode\":\"%s\",\"temp\":%.1f,\"debit\":0,\"puissance\":%d}",
                 (int)chauffe, (double)bc_etat.consigne, (int)bc_etat.eco, BC_MODES_DEMO[bc_etat.mode],
                 (double)bc_etat.temp, chauffe ? 1800 : 0);
        renvoyer(t, b);
        return true;
    }
    else if (strcmp(s, "poele/set") == 0)
    {
        snprintf(b, sizeof b,
                 "{\"brassage\":%d,\"consigne\":%d,\"corps\":%d,\"en_ligne\":1,\"feu\":\"%s\",\"fumees\":%d,\"kd\":%g,\"ki\":%g,"
                 "\"kp\":%g,\"limite\":%d,\"mode\":\"%s\",\"recharge\":%d,\"tirage\":%d,\"tirage_manuel\":%d,\"tirage_min\":%d}",
                 pl_etat.brassage, pl_etat.r.consigne, pl_etat.corps, PL_FEUX_DEMO[pl_etat.feu], pl_etat.fumees,
                 (double)pl_etat.r.kd, (double)pl_etat.r.ki, (double)pl_etat.r.kp, pl_etat.r.limite,
                 PL_MODES_DEMO[pl_etat.mode], pl_etat.r.recharge, pl_etat.tirage, pl_etat.tirage_manuel, pl_etat.r.tirage_min);
        renvoyer(t, b);
        return true;
    }
    else if (strcmp(s, "cafe/set") == 0)
    {
        // le seul effet que la page n'applique pas elle-même : la bouilloire se met à chauffer (ou s'arrête)
        int chauffe = cf_bouilloire_chauffe ? 1 : 0;
        const char *a = d["action"] | "", *app = d["appareil"] | "";
        if (!strcmp(a, "maintenant") && !strcmp(app, "bouilloire"))
            chauffe = 1;
        else if (!strcmp(a, "arreter") && !strcmp(app, "bouilloire"))
            chauffe = 0;
        snprintf(b, sizeof b,
                 "{\"bouilloire\":{\"chauffe\":%d,\"en_ligne\":1,\"h\":%d,\"prog\":%d,\"temp\":%d},\"cafe\":{\"allumee\":1,"
                 "\"en_ligne\":1,\"h\":%d,\"marche\":0,\"prog\":%d},\"recentes\":[%d,%d]}",
                 chauffe, cf_programmes[1].minute, (int)cf_programmes[1].actif, CF_TEMPERATURES[cf_temp],
                 cf_programmes[0].minute, (int)cf_programmes[0].actif, cf_recentes[0], cf_recentes[1]);
        renvoyer(t, b);
        return true;
    }
    return false; // croquettes, imprimante, panneau, air, PC, musique : pas d'écho (la commande n'aboutit pas en démo)
}

// --- Le faux Music Assistant (la bibliothèque, la file et les radios ne passent pas par MQTT en production) ---
// Mêmes commandes, mêmes parseurs côté page ; seuls les réponses changent. LECTEURS[] de ui_musique.cpp fixe les ids
// de lecteurs : le PC de musique (le deuxième) tient la file de la démo. Pochettes et logos : pas de proxy_id, et
// ui_ma_serveur reste vide -> rien à charger, la page affiche ses carrés vides.
static bool g_ma_lecture = true;
static int g_ma_volume = 35;

// la file : l'album « The Dark Side of the Moon » de Pink Floyd (titre connu, pas de donnée personnelle)
static const char *const MA_QUEUE_ITEMS =
    "[{\"name\":\"Speak to Me\",\"duration\":68,\"media_item\":{\"name\":\"Speak to Me\",\"artists\":[{\"name\":\"Pink Floyd\"}]}},"
    "{\"name\":\"Breathe\",\"duration\":163,\"media_item\":{\"name\":\"Breathe\",\"artists\":[{\"name\":\"Pink Floyd\"}]}},"
    "{\"name\":\"Time\",\"duration\":413,\"media_item\":{\"name\":\"Time\",\"artists\":[{\"name\":\"Pink Floyd\"}]}},"
    "{\"name\":\"Breathe (Reprise)\",\"duration\":60,\"media_item\":{\"name\":\"Breathe (Reprise)\",\"artists\":[{\"name\":\"Pink Floyd\"}]}},"
    "{\"name\":\"The Great Gig in the Sky\",\"duration\":276,\"media_item\":{\"name\":\"The Great Gig in the Sky\",\"artists\":[{\"name\":\"Pink Floyd\"}]}},"
    "{\"name\":\"Money\",\"duration\":381,\"media_item\":{\"name\":\"Money\",\"artists\":[{\"name\":\"Pink Floyd\"}]}}]";

static const char *const MA_ARTISTES =
    "[{\"item_id\":\"11\",\"name\":\"ABBA\",\"sort_name\":\"ABBA\"},"
    "{\"item_id\":\"12\",\"name\":\"Daft Punk\",\"sort_name\":\"Daft Punk\"},"
    "{\"item_id\":\"13\",\"name\":\"Miles Davis\",\"sort_name\":\"Miles Davis\"},"
    "{\"item_id\":\"14\",\"name\":\"Norah Jones\",\"sort_name\":\"Norah Jones\"},"
    "{\"item_id\":\"15\",\"name\":\"Pink Floyd\",\"sort_name\":\"Pink Floyd\"},"
    "{\"item_id\":\"16\",\"name\":\"The Beatles\",\"sort_name\":\"Beatles\"},"
    "{\"item_id\":\"17\",\"name\":\"Tryo\",\"sort_name\":\"Tryo\"},"
    "{\"item_id\":\"18\",\"name\":\"Vivaldi\",\"sort_name\":\"Vivaldi\"}]";

// albums par artiste (item_id de MA) : titres d'albums connus ; pas de proxy_id -> pochettes vides en démo
static const char *const MA_ALBUMS_11 =
    "[{\"name\":\"Arrival\",\"uri\":\"library://album/101\",\"year\":1976},{\"name\":\"The Visitors\",\"uri\":\"library://album/102\",\"year\":1981},"
    "{\"name\":\"Voyage\",\"uri\":\"library://album/103\",\"year\":2021}]";
static const char *const MA_ALBUMS_12 =
    "[{\"name\":\"Homework\",\"uri\":\"library://album/111\",\"year\":1997},{\"name\":\"Discovery\",\"uri\":\"library://album/112\",\"year\":2001},"
    "{\"name\":\"Random Access Memories\",\"uri\":\"library://album/113\",\"year\":2013}]";
static const char *const MA_ALBUMS_13 =
    "[{\"name\":\"Kind of Blue\",\"uri\":\"library://album/121\",\"year\":1959},{\"name\":\"Bitches Brew\",\"uri\":\"library://album/122\",\"year\":1970}]";
static const char *const MA_ALBUMS_14 =
    "[{\"name\":\"Come Away with Me\",\"uri\":\"library://album/131\",\"year\":2002},{\"name\":\"Feels like Home\",\"uri\":\"library://album/132\",\"year\":2004}]";
static const char *const MA_ALBUMS_15 =
    "[{\"name\":\"The Dark Side of the Moon\",\"uri\":\"library://album/141\",\"year\":1973},{\"name\":\"Wish You Were Here\",\"uri\":\"library://album/142\",\"year\":1975},"
    "{\"name\":\"The Division Bell\",\"uri\":\"library://album/143\",\"year\":1994}]";
static const char *const MA_ALBUMS_16 =
    "[{\"name\":\"Abbey Road\",\"uri\":\"library://album/151\",\"year\":1969},{\"name\":\"Let It Be\",\"uri\":\"library://album/152\",\"year\":1970}]";
static const char *const MA_ALBUMS_17 =
    "[{\"name\":\"Mamagototo\",\"uri\":\"library://album/161\",\"year\":2004},{\"name\":\"Au bar du boulevard\",\"uri\":\"library://album/162\",\"year\":2008}]";
static const char *const MA_ALBUMS_18 =
    "[{\"name\":\"Les Quatre Saisons\",\"uri\":\"library://album/171\",\"year\":1974},{\"name\":\"Magnificat\",\"uri\":\"library://album/172\",\"year\":1989}]";

static const char *const MA_RADIOS =
    "[{\"name\":\"FIP\",\"uri\":\"radio://0/201\"},{\"name\":\"France Inter\",\"uri\":\"radio://0/202\"},"
    "{\"name\":\"France Musique\",\"uri\":\"radio://0/203\"},{\"name\":\"Radio Classique\",\"uri\":\"radio://0/204\"},"
    "{\"name\":\"Radio Nova\",\"uri\":\"radio://0/205\"},{\"name\":\"TSF Jazz\",\"uri\":\"radio://0/206\"}]";

bool ui_demo_ma(const char *corps, char **reponse, size_t *taille)
{
    *reponse = nullptr;
    *taille = 0;
    auto repondre = [&](const char *json)
    {
        *reponse = strdup(json);
        *taille = strlen(json);
        return true;
    };
    if (strstr(corps, "players/all"))
    {
        char b[512];
        snprintf(b, sizeof b,
                 "[{\"player_id\":\"aa:bb:cc:00:00:21\",\"name\":\"Salon\",\"available\":false,\"playback_state\":\"idle\","
                 "\"volume_level\":0},{\"player_id\":\"aa:bb:cc:00:00:22\",\"name\":\"PC-musique\",\"available\":true,"
                 "\"playback_state\":\"%s\",\"volume_level\":%d},{\"player_id\":\"upbddd0d2cbad1\",\"name\":\"Projecteur\","
                 "\"available\":true,\"playback_state\":\"idle\",\"volume_level\":10}]",
                 g_ma_lecture ? "playing" : "paused", g_ma_volume);
        return repondre(b);
    }
    if (strstr(corps, "player_queues/get"))
    {
        char b[512];
        snprintf(b, sizeof b,
                 "{\"state\":\"%s\",\"shuffle_enabled\":false,\"repeat_mode\":\"off\",\"current_index\":2,\"elapsed_time\":127,"
                 "\"items\":6,\"current_item\":{\"name\":\"Time\",\"duration\":413,\"media_item\":{\"name\":\"Time\","
                 "\"media_type\":\"track\",\"artists\":[{\"name\":\"Pink Floyd\"}],\"album\":{\"name\":\"The Dark Side of "
                 "the Moon\"}}}}",
                 g_ma_lecture ? "playing" : "paused");
        return repondre(b);
    }
    if (strstr(corps, "player_queues/items"))
        return repondre(MA_QUEUE_ITEMS);
    if (strstr(corps, "music/artists/library_items"))
        return repondre(MA_ARTISTES);
    if (strstr(corps, "music/artists/artist_albums"))
    {
        const char *p = strstr(corps, "\"item_id\":\"");
        const int id = p ? atoi(p + 11) : 0;
        switch (id)
        {
        case 11: return repondre(MA_ALBUMS_11);
        case 12: return repondre(MA_ALBUMS_12);
        case 13: return repondre(MA_ALBUMS_13);
        case 14: return repondre(MA_ALBUMS_14);
        case 15: return repondre(MA_ALBUMS_15);
        case 16: return repondre(MA_ALBUMS_16);
        case 17: return repondre(MA_ALBUMS_17);
        case 18: return repondre(MA_ALBUMS_18);
        default: return repondre("[]");
        }
    }
    if (strstr(corps, "music/radios/library_items"))
        return repondre(MA_RADIOS);
    if (strstr(corps, "\"command\":")) // commandes de lecture : l'état renvoyé bouge, comme un vrai lecteur
    {
        if (strstr(corps, "play_pause"))
            g_ma_lecture = !g_ma_lecture;
        else if (strstr(corps, "play_media") || strstr(corps, "play_index") || strstr(corps, "player_queues/play\""))
            g_ma_lecture = true;
        const char *v = strstr(corps, "\"volume_level\":");
        if (v)
        {
            int vol = atoi(v + 15);
            g_ma_volume = vol < 0 ? 0 : (vol > 100 ? 100 : vol);
        }
        return repondre("null");
    }
    return false;
}

void ui_demo_brancher_ma()
{
    ui_ma_serveur = ""; // pas de serveur d'images en démo : pochettes et logos restent vides
    ui_ma_requete = &ui_demo_ma;
    // ui_ma_en_fond reste vide : la page musique lit alors artistes, radios, lecteurs et file immédiatement
    // (chemin « simulateur » de tic()) — nos réponses sont instantanées, il n'y a pas de réseau à attendre.
}

#endif // MODE_DEMO
