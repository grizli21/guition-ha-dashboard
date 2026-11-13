// 13/11/2026 : réglages de l'écran en mémoire flash SANS NVS.
// Une écriture NVS (~15 opérations flash, 3,5 à 6 ms) faisait glitcher l'image à 16-17 MHz ; une écriture brute de
// 32 octets (0,2 ms) et l'effacement d'un secteur (34 ms) ne glitchent pas (essais à l'œil du 13/11, à 17 MHz).
//
// Un enregistrement de 32 octets contient TOUS nos réglages (écran, énergie, radar, batterie). Chaque sauvegarde écrit
// l'enregistrement complet à l'emplacement suivant, dans les 16 derniers secteurs (64 Ko) de la partition « spiffs »
// (inutilisée depuis LVGL : LittleFS n'est plus monté). Un secteur plein -> le suivant, effacé juste avant ; le secteur
// qui garde le dernier enregistrement valide n'est jamais effacé : une coupure de courant ne perd rien.
// Au démarrage : l'enregistrement valide (signature + somme de contrôle) au plus grand numéro. Une partie jamais
// enregistrée ici est lue dans l'ancien NVS par son module (migration), puis notée (mb_noter) pour la prochaine écriture.
#pragma once
#include <stddef.h>

enum mb_partie_t
{
    MB_ECRAN,    // 9 octets : memoire_t de ui_appareil.cpp
    MB_ENERGIE,  // 3 octets : coupure du Wi-Fi, économie (s), émission réduite
    MB_RADAR,    // 1 octet : marge (dB)
    MB_BATTERIE, // 9 octets : débranchement (u32), tension max (float), en charge (u8)
};

bool mb_lire(mb_partie_t partie, void *dst, size_t n);         // vrai si la partie a été enregistrée ici
void mb_noter(mb_partie_t partie, const void *src, size_t n);  // en mémoire seulement (migration depuis NVS)
void mb_ecrire(mb_partie_t partie, const void *src, size_t n); // met à jour la partie et enregistre tout
void mb_oublier(mb_partie_t partie);                           // remise à zéro : la partie revient aux valeurs par défaut
