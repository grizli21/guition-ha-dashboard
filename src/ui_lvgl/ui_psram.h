// 29/10/2026 (audit RAM, retour d'usage : « passe en psram ») : les tableaux de TEXTE (listes de la musique, adresses des
// images, journaux, états des pages) quittent la RAM interne (disputée par le Wi-Fi, les piles et la caméra) pour la
// PSRAM (8 Mo).
// ⚠️ MESURÉ (29/10, chambre 1) : avant setup(), la PSRAM n'est pas encore dans le tas : alloués par les constructeurs
// statiques, les 65 Ko retombaient en RAM interne. D'où : chaque tableau S'INSCRIT avant setup() (rien d'alloué, son
// pointeur reste nul), et ui_psram_allouer(), en TOUTE PREMIÈRE ligne de setup(), lui donne sa PSRAM (repli : RAM
// interne). Simulateur : calloc tout de suite.
#pragma once
#include <stdlib.h>
#include <string.h>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#endif

static inline void *ui_psram_calloc(size_t n, size_t taille)
{
#ifdef ARDUINO
    if (void *p = heap_caps_calloc(n, taille, MALLOC_CAP_SPIRAM))
        return p;
#endif
    return calloc(n, taille);
}

struct ui_psram_inscrit_t
{
    void **ptr;
    size_t taille;
    const void *depart; // valeur de départ (en flash), nullptr : zéros
};
#define UI_PSRAM_MAX 24
inline ui_psram_inscrit_t *ui_psram_liste(int **nb) // une seule liste pour tout le programme (fonction inline)
{
    static ui_psram_inscrit_t liste[UI_PSRAM_MAX]; // zéros dès le chargement : utilisable par les constructeurs statiques
    static int n = 0;
    *nb = &n;
    return liste;
}
static inline void *ui_psram_donner(ui_psram_inscrit_t &e)
{
    void *p = ui_psram_calloc(1, e.taille);
    if (p && e.depart)
        memcpy(p, e.depart, e.taille);
    *e.ptr = p;
    return p;
}
// renvoie la mémoire si elle est déjà disponible (simulateur), nullptr sinon (ESP32 : donnée par ui_psram_allouer)
inline void *ui_psram_inscrire(void **ptr, size_t taille, const void *depart)
{
    int *nb;
    ui_psram_inscrit_t *l = ui_psram_liste(&nb);
    ui_psram_inscrit_t e = {ptr, taille, depart};
#ifdef ARDUINO
    if (*nb < UI_PSRAM_MAX)
    {
        l[(*nb)++] = e;
        return nullptr;
    }
#endif
    return ui_psram_donner(e);
}
// ESP32 : en toute première ligne de setup() ; octets placés en PSRAM (le reste : RAM interne)
inline size_t ui_psram_allouer(int *nb_tableaux = nullptr)
{
    int *nb;
    ui_psram_inscrit_t *l = ui_psram_liste(&nb);
    size_t en_psram = 0;
    for (int i = 0; i < *nb; i++)
    {
        void *p = ui_psram_donner(l[i]);
#ifdef ARDUINO
        if (p && esp_ptr_external_ram(p))
            en_psram += l[i].taille;
#endif
    }
    if (nb_tableaux)
        *nb_tableaux = *nb;
    return en_psram;
}

// type *nom : n cases de type, en PSRAM, valeur de départ depart (pointeur vers une constante, nullptr : zéros)
#define UI_PSRAM(type, nom, n, depart) type *nom = (type *)ui_psram_inscrire((void **)&nom, sizeof(type) * (n), depart)
