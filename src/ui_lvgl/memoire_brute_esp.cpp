// 13/11/2026 : réglages en mémoire flash sans NVS (voir memoire_brute.h).
#ifdef ARDUINO
#include "memoire_brute.h"
#include "ui_diag.h"
#include <Arduino.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <string.h>

#define MB_SECTEURS 16
#define MB_SECTEUR 4096
#define MB_TAILLE 32
#define MB_PAR_SECTEUR (MB_SECTEUR / MB_TAILLE)
#define MB_SIGNATURE 0x424D // « MB »
#define MB_VERSION 1

// enregistrement : signature (2), version (1), parties présentes (1 bit chacune), numéro (4), données (22), somme (2)
struct __attribute__((packed)) mb_enr_t
{
    uint16_t signature;
    uint8_t version;
    uint8_t presentes;
    uint32_t numero;
    uint8_t ecran[9];
    uint8_t energie[3];
    uint8_t radar[1];
    uint8_t batterie[9];
    uint16_t somme;
};
static_assert(sizeof(mb_enr_t) == MB_TAILLE, "un enregistrement = 32 octets");

static const esp_partition_t *g_part = nullptr;
static size_t g_base = 0;   // début de la zone dans la partition
static mb_enr_t g_enr = {}; // dernier état (enregistré ou noté)
static int g_secteur = 0;   // secteur de la prochaine écriture
static int g_place = 0;     // emplacement de la prochaine écriture dans ce secteur
static bool g_pret = false;

static uint8_t *zone(mb_enr_t &e, mb_partie_t p, size_t &n)
{
    switch (p)
    {
    case MB_ECRAN: n = sizeof e.ecran; return e.ecran;
    case MB_ENERGIE: n = sizeof e.energie; return e.energie;
    case MB_RADAR: n = sizeof e.radar; return e.radar;
    default: n = sizeof e.batterie; return e.batterie;
    }
}

static uint16_t somme(const mb_enr_t &e) { return esp_rom_crc16_le(0, (const uint8_t *)&e, MB_TAILLE - 2); }
static bool valide(const mb_enr_t &e) { return e.signature == MB_SIGNATURE && e.version == MB_VERSION && e.somme == somme(e); }
static bool vierge(const uint8_t *o, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (o[i] != 0xFF)
            return false;
    return true;
}

// une fois : la zone, le dernier enregistrement valide, la prochaine place ; les secteurs « étrangers » (ancien LittleFS)
// sont effacés
static bool preparer()
{
    if (g_pret)
        return g_part != nullptr;
    g_pret = true;
    g_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    if (!g_part || g_part->size < MB_SECTEURS * MB_SECTEUR)
    {
        ui_trace("[memoire] pas de partition spiffs : réglages non enregistrés\n");
        g_part = nullptr;
        return false;
    }
    g_base = g_part->size - MB_SECTEURS * MB_SECTEUR;
    const uint32_t t0 = millis();
    static uint8_t secteur[MB_SECTEUR]; // lu d'un coup (RAM interne, une fois au démarrage)
    uint32_t meilleur = 0;
    int meilleur_secteur = -1, effaces = 0;
    int dernier_occupe[MB_SECTEURS];
    for (int s = 0; s < MB_SECTEURS; s++)
    {
        dernier_occupe[s] = -1;
        if (esp_partition_read(g_part, g_base + s * MB_SECTEUR, secteur, MB_SECTEUR) != ESP_OK)
            continue;
        // un emplacement non vide sans notre signature dans un secteur sans aucun enregistrement valide = données
        // étrangères (ancien LittleFS) : secteur effacé. Un enregistrement abîmé par une coupure, à côté d'enregistrements
        // valides, ne fait pas effacer le secteur : il occupe sa place, c'est tout.
        bool etranger = false, a_valide = false;
        int occupe = -1;
        uint32_t num = 0;
        mb_enr_t retenu = {};
        for (int i = 0; i < MB_PAR_SECTEUR; i++)
        {
            const mb_enr_t *e = (const mb_enr_t *)(secteur + i * MB_TAILLE);
            if (vierge((const uint8_t *)e, MB_TAILLE))
                continue;
            occupe = i;
            if (e->signature != MB_SIGNATURE)
                etranger = true;
            else if (valide(*e) && (!a_valide || e->numero > num))
            {
                a_valide = true;
                num = e->numero;
                retenu = *e;
            }
        }
        if (etranger && !a_valide)
        {
            esp_partition_erase_range(g_part, g_base + s * MB_SECTEUR, MB_SECTEUR);
            effaces++;
            continue;
        }
        dernier_occupe[s] = occupe;
        if (a_valide && (meilleur_secteur < 0 || num > meilleur))
        {
            meilleur = num;
            meilleur_secteur = s;
            g_enr = retenu;
        }
    }
    if (meilleur_secteur >= 0)
    {
        g_secteur = meilleur_secteur;
        g_place = dernier_occupe[meilleur_secteur] + 1;
    }
    else
    {
        g_enr = {};
        g_enr.signature = MB_SIGNATURE;
        g_enr.version = MB_VERSION;
        g_secteur = 0;
        g_place = dernier_occupe[0] + 1;
    }
    ui_trace("[memoire] %s (n° %lu, secteur %d, place %d, parties 0x%x) ; %d secteur(s) étranger(s) effacé(s) ; %lu ms\n",
             meilleur_secteur >= 0 ? "réglages retrouvés" : "aucun réglage enregistré", (unsigned long)g_enr.numero, g_secteur,
             g_place, g_enr.presentes, effaces, (unsigned long)(millis() - t0));
    return true;
}

static void enregistrer()
{
    if (!preparer())
        return;
    const uint32_t t0 = micros();
    if (g_place >= MB_PAR_SECTEUR) // secteur plein : le suivant, effacé (le précédent garde le dernier état valide)
    {
        g_secteur = (g_secteur + 1) % MB_SECTEURS;
        g_place = 0;
        esp_partition_erase_range(g_part, g_base + g_secteur * MB_SECTEUR, MB_SECTEUR);
    }
    g_enr.numero++;
    g_enr.somme = somme(g_enr);
    const esp_err_t e = esp_partition_write(g_part, g_base + g_secteur * MB_SECTEUR + g_place * MB_TAILLE, &g_enr, MB_TAILLE);
    g_place++;
    if (e != ESP_OK || g_place == 1)
        ui_trace("[memoire] enregistrement n° %lu (secteur %d) en %lu us : %s\n", (unsigned long)g_enr.numero, g_secteur,
                 (unsigned long)(micros() - t0), esp_err_to_name(e));
}

bool mb_lire(mb_partie_t p, void *dst, size_t n)
{
    size_t taille;
    const uint8_t *z = zone(g_enr, p, taille);
    if (!preparer() || !(g_enr.presentes & (1u << p)) || n > taille)
        return false;
    memcpy(dst, z, n);
    return true;
}

void mb_noter(mb_partie_t p, const void *src, size_t n)
{
    size_t taille;
    uint8_t *z = zone(g_enr, p, taille);
    if (!preparer() || n > taille)
        return;
    memset(z, 0, taille);
    memcpy(z, src, n);
    g_enr.presentes |= 1u << p;
}

void mb_ecrire(mb_partie_t p, const void *src, size_t n)
{
    mb_noter(p, src, n);
    enregistrer();
}

void mb_oublier(mb_partie_t p)
{
    if (!preparer())
        return;
    size_t taille;
    memset(zone(g_enr, p, taille), 0, taille);
    g_enr.presentes &= ~(1u << p);
    enregistrer();
}
#endif
