#include "ui_musique.h"
#include "ui_psram.h" // 29/10 : tableaux de texte en PSRAM
#include "page_musique.h"
#include "ui_images.h"
#include "ui_mqtt.h"
#include "ui_diag.h"
#include "ui_theme.h"
#include <ArduinoJson.h>
#include <lvgl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool (*ui_ma_requete)(const char *corps, char **reponse, size_t *taille) = nullptr;
bool (*ui_ma_en_fond)(void (*travail)(void *)) = nullptr;
const char *ui_ma_serveur = "";

// Réponses lourdes : chaque élément vient avec toutes ses métadonnées (mesuré le 24/10 : 537 artistes = 392 Ko,
// 27 albums = 112 Ko, file de 20 morceaux = 58 Ko, état d'une file = 12 Ko, lecteurs = 10 Ko). Lues avec un filtre
// ArduinoJson : seuls les champs utiles restent, en PSRAM (lv_mem_alloc = ps_malloc).
// 05/11/2026 (etape 2, migration 9.6) : en 8.4 « lv_mem_alloc » est ps_malloc (include/lv_conf.h:49-76),
// donc ces buffers JSON vivent en PSRAM. En 9.6 la conf du banc choisit LV_USE_STDLIB_MALLOC = LV_STDLIB_CLIB
// (.banc_v9_esp/conf_v9/lv_conf.h:37) : « lv_malloc » = malloc du clib, RAM INTERNE. Ces buffers font jusqu'a
// 112 ko (27 albums) : ils doivent rester en PSRAM d'une version a l'autre. ps_malloc/ps_realloc sont declares
// dans esp32-hal-psram.h (il n'y a pas de ps_free : c'est free()).
#if LVGL_VERSION_MAJOR >= 9
#  define MU_MEM_ALLOC(n)      ps_malloc(n)
#  define MU_MEM_FREE(p)       free(p)
#  define MU_MEM_REALLOC(p, n) ps_realloc(p, n)
#else
#  define MU_MEM_ALLOC(n)      lv_mem_alloc(n)
#  define MU_MEM_FREE(p)       lv_mem_free(p)
#  define MU_MEM_REALLOC(p, n) lv_mem_realloc(p, n)
#endif

struct psram_t : ArduinoJson::Allocator
{
    void *allocate(size_t n) override { return MU_MEM_ALLOC(n); }
    void deallocate(void *p) override { MU_MEM_FREE(p); }
    void *reallocate(void *p, size_t n) override { return MU_MEM_REALLOC(p, n); }
};
static psram_t g_psram;

// Les lecteurs de la maison : piCore (ex-moOde, 26/10) éteint DISPARAÎT de la liste de MA (vu le 24/10), d'où une liste fixe ; absent ou
// indisponible = hors ligne. Le 0 est le salon (ampli : page_musique).
static const struct
{
    const char *id, *nom, *appareil;
} LECTEURS[] = {
    {"aa:bb:cc:dd:ee:01", "Salon", "piCore"},
    {"aa:bb:cc:dd:ee:02", "PC-musique", ""},
    {"upexemple0001", "Projecteur", ""},
};
#define NB_LECTEURS (int)(sizeof LECTEURS / sizeof *LECTEURS)

// 25/10 : plus de liste de radios en dur (retour d'usage : « la source est MA sinon rien, et rien en dur ») : ce sont les
// radios FAVORITES de Music Assistant, lues en fond avec la bibliothèque (lire_radios)

#define POCHETTE_GRILLE ((UI_ZONE_L - 2 * UI_MARGE - 16) / 3) // case de la grille des albums (page_musique)
#define POCHETTE_LECTURE 228                                  // pochette ronde de l'onglet Lecture (page_musique)
#define RELECTURE 5                                           // s entre deux lectures de l'état, page affichée
#define GARDE 3000                                            // ms : après une commande, l'écran garde ce qu'il affiche

static int g_lecteur = 0;                  // index dans LECTEURS
static uint32_t g_garde = 0;               // lecture / aléa / répète / volume touchés à l'écran à cet instant
static bool g_relire = false;              // une commande vient de partir : relire l'état sans attendre
static uint32_t g_relire_t = 0;
static char g_pochette[72];                // proxy_id de la pochette en cours (rien : dessinée)
static int g_index = -1, g_nb_items = -1;  // file : relue quand le morceau en cours ou la taille change

// 24/10 (retour d'usage) : lecture demandée sur le Salon, chaîne éteinte -> HA allume, l'écran garde la demande et l'envoie
// quand piCore apparaît dans MA (moOde : 65 à 90 s mesurés le 10/10), même hors de la page Musique ; abandon après 3 min
#define ATTENTE_MAX 180000
static char g_attente_cmd[40], g_attente_args[224];
static uint32_t g_attente_t = 0;

// ---------------------------------------------------------------------------------------------------- requêtes
// args : JSON écrit à la main, qui ne contient que des identifiants, des uri et des nombres (rien à échapper)
static char *appel(size_t *n, const char *commande, const char *args, va_list va)
{
    if (!ui_ma_requete)
        return nullptr;
    char etape[48];
    snprintf(etape, sizeof etape, "ma:%s", commande);
    ui_diag_etape(etape);
    char a[256], corps[320];
    vsnprintf(a, sizeof a, args, va);
    snprintf(corps, sizeof corps, "{\"command\":\"%s\",\"args\":%s}", commande, a);
    char *r = nullptr;
    return ui_ma_requete(corps, &r, n) ? r : nullptr;
}

static bool demander(JsonDocument &rep, JsonDocument &filtre, const char *commande, const char *args, ...)
{
    va_list va;
    va_start(va, args);
    size_t n = 0;
    char *r = appel(&n, commande, args, va);
    va_end(va);
    if (!r)
        return false;
    const uint32_t t0 = lv_tick_get();
    bool ok = !deserializeJson(rep, (const char *)r, n, DeserializationOption::Filter(filtre));
    free(r);
    // 24/10 : avec « [MA] … en N ms » (HTTP) et « [lent] ma:… » (tout le bloc), sépare réseau / analyse / construction
    if (lv_tick_elaps(t0) >= 20)
        ui_trace("[MA] analyse JSON %s : %u ms\n", commande, (unsigned)lv_tick_elaps(t0));
    return ok;
}

// 25/10 : les commandes partent EN FOND, dans l'ordre (MA ne répond à play_media qu'une fois la lecture lancée : 1,5 s
// figées en lançant une radio, vu sur Chambre 1) ; leur réponse est inutile. L'état est relu une fois la dernière partie.
#define FILE_CMD 6
typedef char cmd_t[320];
static UI_PSRAM(cmd_t, g_cmd, FILE_CMD, nullptr); // 29/10 : en PSRAM (ui_psram.h)
static volatile int g_cmd_tete = 0, g_cmd_queue = 0; // tête : la prochaine à envoyer (fond) ; queue : la prochaine place (boucle)
static volatile bool g_cmd_tache = false, g_cmd_fin = false;
// 25/10 : latence d'une commande : appui du doigt, clic (relâché), envoi, réponse de MA (lv_tick = millis sur l'ESP32)
volatile uint32_t mu_appui_ms = 0;
static uint32_t g_cmd_appui[FILE_CMD], g_cmd_clic[FILE_CMD];

static void envoyer_cmds(void *)
{
    while (g_cmd_tete != g_cmd_queue)
    {
        char *r = nullptr;
        size_t n = 0;
        const uint32_t envoi = lv_tick_get();
        if (ui_ma_requete(g_cmd[g_cmd_tete], &r, &n))
            free(r);
        const uint32_t clic = g_cmd_clic[g_cmd_tete], appui = g_cmd_appui[g_cmd_tete];
        ui_trace("[MA] commande %.36s : appui -> clic %ld ms, clic -> envoi %lu ms, réponse de MA en %lu ms\n", g_cmd[g_cmd_tete] + 12,
                 appui && clic >= appui && clic - appui < 5000 ? (long)(clic - appui) : -1L, (unsigned long)(envoi - clic),
                 (unsigned long)(lv_tick_get() - envoi));
        g_cmd_tete = (g_cmd_tete + 1) % FILE_CMD;
    }
    g_cmd_fin = true;
    g_cmd_tache = false;
}

static void lancer_cmds() // dans la boucle
{
    if (g_cmd_tache || g_cmd_tete == g_cmd_queue || !ui_ma_requete)
        return;
    g_cmd_tache = true;
    if (!ui_ma_en_fond || !ui_ma_en_fond(envoyer_cmds))
        envoyer_cmds(nullptr); // simulateur (ou tâche impossible) : tout de suite
}

static void commander(const char *commande, const char *args, ...)
{
    char a[256];
    va_list va;
    va_start(va, args);
    vsnprintf(a, sizeof a, args, va);
    va_end(va);
    const int suivante = (g_cmd_queue + 1) % FILE_CMD;
    if (suivante == g_cmd_tete)
    {
        ui_trace("[MA] file des commandes pleine : %s oubliée\n", commande);
        return;
    }
    snprintf(g_cmd[g_cmd_queue], sizeof g_cmd[g_cmd_queue], "{\"command\":\"%s\",\"args\":%s}", commande, a);
    g_cmd_clic[g_cmd_queue] = lv_tick_get();
    g_cmd_appui[g_cmd_queue] = mu_appui_ms;
    g_cmd_queue = suivante;
    lancer_cmds();
}

// ---------------------------------------------------------------------------------------------------- bibliothèque
// Tous les artistes (noms seulement) lus UNE fois, puis la bande alphabétique se sert dans la mémoire : MA ne sait pas
// filtrer par première lettre (sa recherche est une sous-chaîne).
struct artiste_t
{
    char nom[40];
    int id;
    char lettre;
};
static artiste_t *g_artistes = nullptr;
static int g_nb_artistes = 0;

// Lettre de la bande : celle du nom de tri de MA (« beatles, the » -> B), accents retirés ; chiffres et signes = « # »
static char lettre_de(const char *s)
{
    uint8_t c = (uint8_t)s[0];
    if (c == 0xC3) // lettre accentuée (UTF-8, U+00C0..U+00FF) : sa lettre de base
    {
        static const char BASE[] = "AAAAAAACEEEEIIIIDNOOOOO#OUUUUYTS";
        uint8_t d = (uint8_t)s[1];
        c = d >= 0x80 && d <= 0xBF ? BASE[(d - 0x80) % 32] : '#';
    }
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    return c >= 'A' && c <= 'Z' ? (char)c : '#';
}

static void choisir_lettre(char l)
{
    mu_etat.lettre = l;
    int n = 0;
    for (int i = 0; i < g_nb_artistes && n < MU_ARTISTES_MAX; i++)
        if (g_artistes[i].lettre == l)
        {
            mu_element_t &a = mu_etat.artistes[n++];
            snprintf(a.nom, sizeof a.nom, "%s", g_artistes[i].nom);
            snprintf(a.uri, sizeof a.uri, "%d", g_artistes[i].id); // n° de l'artiste dans la bibliothèque de MA
            a.detail[0] = 0;
            a.img = nullptr;
        }
    mu_etat.nb_artistes = n;
}

// 24/10 (retour d'usage : « plusieurs secondes à l'affichage de la Musique, le premier », « un précache à faire au démarrage ») :
// la bibliothèque, 392 Ko, prend 2,6 s sur le Wi-Fi de l'écran (0,1 s pour le PC : c'est le débit de l'ESP32) + 0,26 s
// d'analyse, et figeait l'écran à la première entrée. Elle est lue EN FOND (autre cœur) 10 s après le démarrage, puis
// toutes les heures (nouvel essai 30 s après un échec) ; la boucle reprend le résultat (biblio_tic).
static volatile bool g_fond_occupe = false, g_fond_pret = false;
static artiste_t *g_fond_artistes = nullptr;
static int g_fond_nb = 0;
static uint32_t g_fond_ms = 0, g_biblio_t = 0;
static bool g_biblio_ok = false, g_biblio_tentee = false; // tentee : une lecture est revenue (réussie ou non)

// radios favorites de MA, lues en fond avec les artistes (nom, adresse MA, identifiant du logo pour son proxy)
struct radio_t
{
    char nom[40], uri[40], logo[72];
};
static radio_t *g_fond_radios = nullptr;
static int g_fond_nb_radios = -1; // -1 : pas lues (échec)
typedef char logo_t[72];
static UI_PSRAM(logo_t, g_logos, MU_RADIOS_MAX, nullptr); // 29/10 : en PSRAM (ui_psram.h)
static bool g_logos_demandes = false;

static void lire_radios()
{
    char *rep = nullptr;
    size_t n = 0;
    g_fond_nb_radios = -1;
    if (!ui_ma_requete("{\"command\":\"music/radios/library_items\",\"args\":{\"favorite\":true,\"order_by\":\"sort_name\","
                       "\"limit\":40}}",
                       &rep, &n))
        return;
    JsonDocument f, r(&g_psram);
    f[0]["name"] = true;
    f[0]["uri"] = true;
    f[0]["metadata"]["images"][0]["proxy_id"] = true;
    if (!deserializeJson(r, (const char *)rep, n, DeserializationOption::Filter(f)))
    {
        JsonArrayConst l = r.as<JsonArrayConst>();
        g_fond_radios = (radio_t *)MU_MEM_ALLOC(MU_RADIOS_MAX * sizeof(radio_t));
        g_fond_nb_radios = 0;
        for (JsonVariantConst x : l)
        {
            if (!g_fond_radios || g_fond_nb_radios >= MU_RADIOS_MAX)
                break;
            radio_t &d = g_fond_radios[g_fond_nb_radios++];
            const char *nom = x["name"] | "";
            while (*nom == ' ')
                nom++;
            snprintf(d.nom, sizeof d.nom, "%s", nom);
            ui_texte_propre(d.nom); // 25/10 : affichable par nos polices (étiquettes de MA)
            for (int k = strlen(d.nom) - 1; k >= 0 && d.nom[k] == ' '; k--) // « BFM Business » (espace final dans MA)
                d.nom[k] = 0;
            snprintf(d.uri, sizeof d.uri, "%s", x["uri"] | "");
            snprintf(d.logo, sizeof d.logo, "%s", x["metadata"]["images"][0]["proxy_id"] | "");
        }
    }
    free(rep);
}

static void installer_radios()
{
    if (g_fond_nb_radios < 0 || !g_fond_radios)
        return;
    mu_etat.nb_radios = g_fond_nb_radios;
    for (int i = 0; i < g_fond_nb_radios; i++)
    {
        mu_element_t &e = mu_etat.radios[i];
        snprintf(e.nom, sizeof e.nom, "%s", g_fond_radios[i].nom);
        snprintf(e.uri, sizeof e.uri, "%s", g_fond_radios[i].uri);
        e.detail[0] = 0;
        if (strcmp(g_logos[i], g_fond_radios[i].logo)) // un autre logo à cette place : redemandé
        {
            snprintf(g_logos[i], sizeof g_logos[i], "%s", g_fond_radios[i].logo);
            e.img = nullptr;
            g_logos_demandes = false;
        }
    }
    MU_MEM_FREE(g_fond_radios);
    g_fond_radios = nullptr;
}

static void logo_pret(int e, const lv_img_dsc_t *img)
{
    const int i = e - UI_IMG_RADIOS;
    if (i < 0 || i >= mu_etat.nb_radios)
        return;
    mu_etat.radios[i].img = img;
    page_musique_logo_radio(i);
}

// la vue Radios est affichée : les logos que MA sait servir (sinon rien : la ligne garde son carré vide)
static void demander_logos()
{
    if (g_logos_demandes || !*ui_ma_serveur)
        return;
    g_logos_demandes = true;
    for (int i = 0; i < mu_etat.nb_radios; i++)
        if (g_logos[i][0])
        {
            char url[200];
            snprintf(url, sizeof url, "%s/imageproxy/%s?size=80", ui_ma_serveur, g_logos[i]);
            ui_images_demander(UI_IMG_RADIOS + i, url, MU_LOGO_LISTE, logo_pret);
        }
}

// en fond : ni LVGL, ni mu_etat, ni traces (la boucle s'en charge en reprenant le résultat)
static void lire_artistes(void *)
{
    const uint32_t t0 = lv_tick_get();
    artiste_t *t = nullptr;
    int nb = 0;
    char *rep = nullptr;
    size_t n = 0;
    if (ui_ma_requete && ui_ma_requete("{\"command\":\"music/artists/library_items\",\"args\":{\"album_artists_only\":true,"
                                       "\"order_by\":\"sort_name\",\"limit\":2000}}",
                                       &rep, &n))
    {
        JsonDocument f, r(&g_psram);
        f[0]["item_id"] = true;
        f[0]["name"] = true;
        f[0]["sort_name"] = true;
        if (!deserializeJson(r, (const char *)rep, n, DeserializationOption::Filter(f)))
        {
            JsonArrayConst l = r.as<JsonArrayConst>();
            t = (artiste_t *)MU_MEM_ALLOC(LV_MAX(l.size(), 1) * sizeof(artiste_t));
            for (JsonVariantConst a : l)
            {
                const char *nom = a["name"] | "";
                if (!t || (uint8_t)nom[0] <= ' ' || nom[0] == 0x7f) // un artiste « \x7f » dans la bibliothèque (vu le 24/10)
                    continue;
                artiste_t &x = t[nb++];
                snprintf(x.nom, sizeof x.nom, "%s", nom);
                ui_texte_propre(x.nom); // 25/10 : affichable par nos polices (étiquettes de MA)
                x.id = a["item_id"].is<const char *>() ? atoi(a["item_id"].as<const char *>()) : a["item_id"].as<int>();
                x.lettre = lettre_de(a["sort_name"] | nom);
            }
        }
        free(rep);
    }
    if (ui_ma_requete)
        lire_radios();
    g_fond_artistes = t;
    g_fond_nb = nb;
    g_fond_ms = lv_tick_elaps(t0);
    g_fond_pret = true;
    g_fond_occupe = false;
}

static void charger_artistes()
{
    installer_radios();
    if (!g_fond_artistes) // résultat repris (ou échec) : rien à installer
        return;
    MU_MEM_FREE(g_artistes);
    g_artistes = g_fond_artistes;
    g_nb_artistes = g_fond_nb;
    g_fond_artistes = nullptr;
    int k = 0; // lettres qui ont au moins un artiste, dans l'ordre de la bande
    for (const char *c = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ"; *c && k < (int)sizeof mu_etat.lettres - 1; c++)
        for (int i = 0; i < g_nb_artistes; i++)
            if (g_artistes[i].lettre == *c)
            {
                mu_etat.lettres[k++] = *c;
                break;
            }
    mu_etat.lettres[k] = 0;
    if (k) // la lettre en cours si elle existe encore, sinon A
        choisir_lettre(mu_etat.lettre && strchr(mu_etat.lettres, mu_etat.lettre) ? mu_etat.lettre
                       : strchr(mu_etat.lettres, 'A')                          ? 'A'
                                                                               : mu_etat.lettres[0]);
}

// dans la boucle, chaque seconde : lance la lecture en fond quand il faut, installe son résultat quand il est là
static void biblio_tic()
{
    if (g_fond_pret)
    {
        g_fond_pret = false;
        g_biblio_tentee = true;
        g_biblio_ok = g_fond_artistes != nullptr;
        ui_trace("[MA] bibliothèque lue en fond : %d artistes, %d radios en %u ms\n", g_fond_nb, g_fond_nb_radios, (unsigned)g_fond_ms);
        charger_artistes();
        if (g_biblio_ok && page_musique_affichee())
            page_musique_maj();
    }
    // la 1re lecture : lancée par l'écran de démarrage (ui_musique_precharger, réseau prêt), sinon à 20 s ; ensuite toutes
    // les heures, ou 30 s après un échec
    if (!ui_ma_en_fond || g_fond_occupe || (!g_biblio_t && lv_tick_get() < 20000) || ui_musique_a_la_demande)
        return;
    if (g_biblio_t && lv_tick_elaps(g_biblio_t) < (g_biblio_ok ? 3600000u : 30000u))
        return;
    ui_musique_precharger();
}

bool ui_musique_a_la_demande = false;

void ui_musique_demander_biblio()
{
    if (!ui_musique_a_la_demande || g_fond_occupe)
        return;
    if (!g_biblio_t || lv_tick_elaps(g_biblio_t) >= (g_biblio_ok ? 3600000u : 30000u))
        ui_musique_precharger();
}

void ui_musique_precharger()
{
    if (!ui_ma_en_fond || g_fond_occupe)
        return;
    g_biblio_t = lv_tick_get() | 1;
    g_fond_occupe = true;
    if (!ui_ma_en_fond(lire_artistes))
        g_fond_occupe = false;
}

bool ui_musique_biblio_prete() { return !ui_ma_en_fond || g_biblio_tentee || ui_musique_a_la_demande; }

static void pochette_prete(int e, const lv_img_dsc_t *img)
{
    int i = e - UI_IMG_POCHETTES;
    if (i < 0 || i >= mu_etat.nb_albums)
        return;
    mu_etat.albums[i].img = img;
    page_musique_pochette_album(i); // 24/10 : sa case seulement (avant : toute la Biblio reconstruite, par pochette)
}

static const char *vignette(JsonVariantConst images) // proxy_id de la première image « thumb », ou nullptr
{
    for (JsonVariantConst im : images.as<JsonArrayConst>())
        if (!strcmp(im["type"] | "", "thumb") && im["proxy_id"].is<const char *>())
            return im["proxy_id"].as<const char *>();
    return nullptr;
}

// Albums d'un artiste, par année (les inconnues à la fin), les MU_LISTE_MAX premiers
static void ouvrir_artiste(int i)
{
    mu_etat.nb_albums = 0;
    if (i < 0 || i >= mu_etat.nb_artistes)
        return;
    JsonDocument f, r(&g_psram);
    f[0]["name"] = true;
    f[0]["uri"] = true;
    f[0]["year"] = true;
    f[0]["metadata"]["images"][0]["type"] = true;
    f[0]["metadata"]["images"][0]["proxy_id"] = true;
    if (!demander(r, f, "music/artists/artist_albums", "{\"item_id\":\"%s\",\"provider_instance_id_or_domain\":\"library\"}",
                  mu_etat.artistes[i].uri))
        return;
    JsonArrayConst l = r.as<JsonArrayConst>();
    auto annee = [&](int k) { int a = l[k]["year"] | 0; return a ? a : 9999; };
    int ordre[64], n = 0;
    for (int k = 0; k < (int)l.size() && n < 64; k++) // tri par insertion : quelques dizaines d'albums
    {
        int j = n++;
        for (; j > 0 && annee(ordre[j - 1]) > annee(k); j--)
            ordre[j] = ordre[j - 1];
        ordre[j] = k;
    }
    n = LV_MIN(n, MU_LISTE_MAX);
    for (int k = 0; k < n; k++)
    {
        JsonVariantConst v = l[ordre[k]];
        mu_element_t &a = mu_etat.albums[k];
        snprintf(a.nom, sizeof a.nom, "%s", v["name"] | "");
        ui_texte_propre(a.nom); // 25/10 : affichable par nos polices (étiquettes de MA)
        snprintf(a.uri, sizeof a.uri, "%s", v["uri"] | "");
        int an = v["year"] | 0;
        if (an > 0)
            snprintf(a.detail, sizeof a.detail, "%d", an);
        else
            a.detail[0] = 0;
        a.img = nullptr;
    }
    mu_etat.nb_albums = n; // avant les pochettes : une pochette déjà chargée est rendue tout de suite
    for (int k = 0; k < n; k++)
    {
        const char *px = vignette(l[ordre[k]]["metadata"]["images"]);
        char url[200];
        if (px && *ui_ma_serveur)
        {
            snprintf(url, sizeof url, "%s/imageproxy/%s?size=160", ui_ma_serveur, px); // JPEG (MA : 0 / 80 / 160 / 256… seulement)
            ui_images_demander(UI_IMG_POCHETTES + k, url, POCHETTE_GRILLE, pochette_prete);
        }
        else
            ui_images_oublier(UI_IMG_POCHETTES + k);
    }
}

// ---------------------------------------------------------------------------------------------------- lecture
static bool garde() { return g_garde && lv_tick_elaps(g_garde) < GARDE; }

static bool lire_lecteurs(bool choisir)
{
    JsonDocument f, r(&g_psram);
    f[0]["player_id"] = true;
    f[0]["available"] = true;
    f[0]["playback_state"] = true;
    f[0]["volume_level"] = true;
    f[0]["group_members"] = true;
    if (!demander(r, f, "players/all", "{\"return_unavailable\":true}"))
        return false;
    JsonVariantConst p[NB_LECTEURS];
    for (JsonVariantConst x : r.as<JsonArrayConst>())
        for (int i = 0; i < NB_LECTEURS; i++)
            if (!strcmp(x["player_id"] | "", LECTEURS[i].id))
                p[i] = x;
    for (int i = 0; i < NB_LECTEURS; i++)
        mu_etat.lecteurs[i].en_ligne = p[i]["available"] | false;
    if (choisir && !g_attente_t) // en arrivant sur la page : le lecteur qui joue, sinon celui choisi (même éteint : le Salon s'allume à la demande)
        for (int i = NB_LECTEURS - 1; i >= 0; i--)
            if (!strcmp(p[i]["playback_state"] | "", "playing"))
                g_lecteur = i;
    mu_etat.lecteur = g_lecteur;
    for (int i = 0; i < NB_LECTEURS; i++) // joue avec le lecteur choisi
    {
        mu_etat.lecteurs[i].groupe = false;
        if (i != g_lecteur)
            for (JsonVariantConst m : p[g_lecteur]["group_members"].as<JsonArrayConst>())
                if (!strcmp(m | "", LECTEURS[i].id))
                    mu_etat.lecteurs[i].groupe = true;
    }
    if (!garde() && p[g_lecteur]["volume_level"].is<int>())
        mu_etat.volume = p[g_lecteur]["volume_level"].as<int>();
    return true;
}

static void pochette_lecture_prete(int, const lv_img_dsc_t *img)
{
    mu_etat.pochette = img;
    page_musique_maj_lecture();
}

static void changer_pochette(const char *px)
{
    if (!strcmp(px, g_pochette))
        return;
    snprintf(g_pochette, sizeof g_pochette, "%s", px);
    mu_etat.pochette = nullptr; // l'ancienne est libérée par la demande suivante : la page ne doit plus la montrer
    page_musique_maj_lecture();
    if (!*px || !*ui_ma_serveur)
    {
        ui_images_oublier(UI_IMG_LECTURE);
        return;
    }
    char url[200];
    snprintf(url, sizeof url, "%s/imageproxy/%s?size=256", ui_ma_serveur, px);
    ui_images_demander(UI_IMG_LECTURE, url, POCHETTE_LECTURE, pochette_lecture_prete);
}

// File : une fenêtre de MU_FILE_MAX morceaux qui commence 2 avant celui en cours
static void lire_file(int index)
{
    const int debut = LV_MAX(0, index - 2);
    JsonDocument f, r(&g_psram);
    f[0]["name"] = true;
    f[0]["duration"] = true;
    f[0]["media_item"]["name"] = true;
    f[0]["media_item"]["artists"][0]["name"] = true;
    mu_etat.nb_file = 0;
    if (!demander(r, f, "player_queues/items", "{\"queue_id\":\"%s\",\"limit\":%d,\"offset\":%d}", LECTEURS[g_lecteur].id, MU_FILE_MAX, debut))
        return;
    int n = 0;
    for (JsonVariantConst v : r.as<JsonArrayConst>())
    {
        if (n >= MU_FILE_MAX)
            break;
        mu_piste_t &p = mu_etat.file[n++];
        snprintf(p.titre, sizeof p.titre, "%s", v["media_item"]["name"] | (v["name"] | ""));
        ui_texte_propre(p.titre); // 25/10 : affichable par nos polices (étiquettes de MA)
        snprintf(p.artiste, sizeof p.artiste, "%s", v["media_item"]["artists"][0]["name"] | "");
        ui_texte_propre(p.artiste); // 25/10 : affichable par nos polices (étiquettes de MA)
        p.duree = v["duration"] | 0;
    }
    mu_etat.nb_file = n;
    mu_etat.premiere = debut;
    mu_etat.piste = index - debut;
}

static void lire_etat()
{
    JsonDocument f, r(&g_psram);
    f["state"] = true;
    f["shuffle_enabled"] = true;
    f["repeat_mode"] = true;
    f["current_index"] = true;
    f["elapsed_time"] = true;
    f["items"] = true;
    JsonObject c = f["current_item"].to<JsonObject>();
    c["name"] = true;
    c["duration"] = true;
    c["image"]["proxy_id"] = true;
    c["streamdetails"]["stream_title"] = true;
    c["media_item"]["name"] = true;
    c["media_item"]["media_type"] = true;
    c["media_item"]["artists"][0]["name"] = true;
    c["media_item"]["album"]["name"] = true;
    mu_etat_t &m = mu_etat;
    if (!demander(r, f, "player_queues/get", "{\"queue_id\":\"%s\"}", LECTEURS[g_lecteur].id) || r["current_item"].isNull())
    {
        snprintf(m.titre, sizeof m.titre, "%s", "Rien en lecture");
        m.artiste[0] = m.album[0] = 0;
        m.duree = m.position = 0;
        m.lecture = false;
        m.nb_file = m.nb_total = 0;
        g_index = g_nb_items = -1;
        changer_pochette("");
        return;
    }
    JsonVariantConst ci = r["current_item"], mi = ci["media_item"];
    if (!strcmp(mi["media_type"] | "", "radio")) // radio : le titre diffusé s'il y en a un, la radio en sous-titre
    {
        const char *flux = ci["streamdetails"]["stream_title"] | "";
        snprintf(m.titre, sizeof m.titre, "%s", *flux ? flux : (mi["name"] | ""));
        ui_texte_propre(m.titre); // 25/10 : affichable par nos polices (étiquettes de MA)
        snprintf(m.artiste, sizeof m.artiste, "%s", mi["name"] | "");
        ui_texte_propre(m.artiste); // 25/10 : affichable par nos polices (étiquettes de MA)
        for (int k = strlen(m.artiste) - 1; k >= 0 && m.artiste[k] == ' '; k--) // comme la liste des radios (lire_radios)
            m.artiste[k] = 0;
        m.album[0] = 0;
        m.duree = m.position = 0;
    }
    else
    {
        snprintf(m.titre, sizeof m.titre, "%s", mi["name"] | (ci["name"] | ""));
        ui_texte_propre(m.titre); // 25/10 : affichable par nos polices (étiquettes de MA)
        snprintf(m.artiste, sizeof m.artiste, "%s", mi["artists"][0]["name"] | "");
        ui_texte_propre(m.artiste); // 25/10 : affichable par nos polices (étiquettes de MA)
        snprintf(m.album, sizeof m.album, "%s", mi["album"]["name"] | "");
        ui_texte_propre(m.album); // 25/10 : affichable par nos polices (étiquettes de MA)
        m.duree = ci["duration"] | 0;
        m.position = LV_MIN((int)(r["elapsed_time"] | 0.0f), m.duree);
    }
    if (!garde())
    {
        m.lecture = !strcmp(r["state"] | "", "playing");
        m.alea = r["shuffle_enabled"] | false;
        m.repete = strcmp(r["repeat_mode"] | "off", "off") != 0;
    }
    m.nb_total = r["items"] | 0;
    const int index = r["current_index"] | 0;
    if (index != g_index || m.nb_total != g_nb_items)
    {
        g_index = index;
        g_nb_items = m.nb_total;
        lire_file(index);
    }
    changer_pochette(ci["image"]["proxy_id"] | "");
}

// Ce qui change l'affichage des autres onglets que Lecture (qui, lui, se rafraîchit sans être reconstruit)
static void empreinte(char *s, size_t n)
{
    int lec = 0;
    for (int i = 0; i < NB_LECTEURS; i++)
        lec |= (mu_etat.lecteurs[i].en_ligne << (2 * i)) | (mu_etat.lecteurs[i].groupe << (2 * i + 1));
    snprintf(s, n, "%s|%s|%d|%d|%d|%d|%d|%d|%d|%d", mu_etat.titre, mu_etat.artiste, mu_etat.lecture, mu_etat.alea, mu_etat.repete,
             mu_etat.lecteur, lec, mu_etat.premiere, mu_etat.piste, mu_etat.nb_file);
}

static void synchro(bool entree)
{
    char avant[160], apres[160];
    empreinte(avant, sizeof avant);
    if (lire_lecteurs(entree))
        lire_etat();
    else // MA ne répond pas : rien ne répond
        for (int i = 0; i < NB_LECTEURS; i++)
            mu_etat.lecteurs[i].en_ligne = false;
    if (g_attente_t) // la lecture attend piCore : on le dit à la place de « Rien en lecture »
    {
        snprintf(mu_etat.titre, sizeof mu_etat.titre, "%s", "piCore démarre…");
        mu_etat.artiste[0] = 0; // pas d'artiste : la page ne le prend pas pour une radio
        snprintf(mu_etat.album, sizeof mu_etat.album, "%s", "la lecture partira toute seule"); // écrit sur la pochette dessinée
    }
    empreinte(apres, sizeof apres);
    if (strcmp(avant, apres))
        page_musique_maj();
    else
        page_musique_maj_lecture();
}

static void attente_tic()
{
    if (!g_attente_t)
        return;
    if (lv_tick_elaps(g_attente_t) > ATTENTE_MAX)
    {
        g_attente_t = 0; // piCore n'est pas venu : on laisse tomber (la page redit « Rien en lecture »)
        return;
    }
    if (!page_musique_affichee()) // hors de la page, personne ne relit les lecteurs : on le fait toutes les 5 s
    {
        static uint32_t dernier = 0;
        if (lv_tick_elaps(dernier) < RELECTURE * 1000)
            return;
        dernier = lv_tick_get();
        lire_lecteurs(false);
    }
    if (mu_etat.lecteurs[0].en_ligne)
    {
        g_attente_t = 0;
        commander(g_attente_cmd, "%s", g_attente_args);
    }
}

static void tic(lv_timer_t *)
{
    static bool affichee = false;
    static int secondes = 0;
    attente_tic();
    biblio_tic();
    if (g_cmd_fin) // la dernière commande est partie : l'état relu ~0,5 s après (MA a eu le temps de l'appliquer)
    {
        g_cmd_fin = false;
        g_relire = true;
        g_relire_t = lv_tick_get() - 1000;
    }
    lancer_cmds(); // une commande arrivée pendant que la tâche finissait
    if (!page_musique_affichee())
    {
        affichee = false; // on relira tout au retour
        return;
    }
    if (!affichee)
    {
        affichee = true;
        secondes = 0;
        if (!g_nb_artistes && !ui_ma_en_fond) // simulateur : tout de suite (l'ESP32 la lit en fond dès le démarrage)
        {
            lire_artistes(nullptr);
            g_fond_pret = false;
            charger_artistes();
        }
        synchro(true);
        return;
    }
    if (++secondes >= RELECTURE || (g_relire && lv_tick_elaps(g_relire_t) >= 1500)) // MA a eu le temps d'appliquer
    {
        secondes = 0;
        g_relire = false;
        synchro(false);
    }
    else if (mu_etat.lecture && mu_etat.duree && mu_etat.position < mu_etat.duree)
    {
        mu_etat.position++; // entre deux lectures, la position avance à l'écran
        page_musique_maj_lecture();
    }
}

// ---------------------------------------------------------------------------------------------------- commandes
static void publier_ha(const char *message)
{
    if (ui_mqtt_publier)
        ui_mqtt_publier(UI_MQTT_PREFIXE "musique/set", message, false);
}

// Lancer une lecture : tout de suite, ou en attente si le lecteur choisi est le Salon et que piCore n'est pas là
static void jouer(const char *cmd, const char *args, ...)
{
    char a[224];
    va_list va;
    va_start(va, args);
    vsnprintf(a, sizeof a, args, va);
    va_end(va);
    if (g_lecteur != 0 || mu_etat.lecteurs[0].en_ligne)
    {
        commander(cmd, "%s", a);
        return;
    }
    snprintf(g_attente_cmd, sizeof g_attente_cmd, "%s", cmd);
    snprintf(g_attente_args, sizeof g_attente_args, "%s", a);
    g_attente_t = lv_tick_get() | 1; // 0 = pas d'attente
    if (mu_etat.chaine != MU_CH_DEMARRAGE && mu_etat.chaine != MU_CH_PRETE)
        publier_ha("{\"action\":\"picore\",\"on\":1}"); // pendant un arrêt, HA attend qu'il finisse
    g_relire = true;
    g_relire_t = lv_tick_get() - 1500;
}

static void commande(mu_action_t a, int arg)
{
    const char *q = LECTEURS[g_lecteur].id; // file d'attente = celle du lecteur choisi
    switch (a)
    {
    case MU_LECTURE_PAUSE:
        g_garde = lv_tick_get();
        if (mu_etat.lecture) // déjà basculé par la page : l'état voulu
            jouer("player_queues/play_pause", "{\"queue_id\":\"%s\"}", q);
        else
            commander("player_queues/play_pause", "{\"queue_id\":\"%s\"}", q);
        break;
    case MU_SUIVANT:
        commander("player_queues/next", "{\"queue_id\":\"%s\"}", q);
        break;
    case MU_PRECEDENT:
        commander("player_queues/previous", "{\"queue_id\":\"%s\"}", q);
        break;
    case MU_VOLUME:
        g_garde = lv_tick_get();
        commander("players/cmd/volume_set", "{\"player_id\":\"%s\",\"volume_level\":%d}", q, arg);
        break;
    case MU_ALEA:
        g_garde = lv_tick_get();
        commander("player_queues/shuffle", "{\"queue_id\":\"%s\",\"shuffle_enabled\":%s}", q, mu_etat.alea ? "true" : "false");
        break;
    case MU_REPETE:
        g_garde = lv_tick_get();
        commander("player_queues/repeat", "{\"queue_id\":\"%s\",\"repeat_mode\":\"%s\"}", q, mu_etat.repete ? "all" : "off");
        break;
    case MU_JOUER_PISTE:
        jouer("player_queues/play_index", "{\"queue_id\":\"%s\",\"index\":%d}", q, mu_etat.premiere + arg);
        break;
    case MU_VIDER_FILE:
        commander("player_queues/clear", "{\"queue_id\":\"%s\"}", q);
        break;
    case MU_OUVRIR_ARTISTE: // la page se reconstruit juste après
        ouvrir_artiste(arg);
        break;
    case MU_LETTRE:
        choisir_lettre((char)arg);
        break;
    case MU_LOGOS:
        demander_logos();
        break;
    case MU_LIRE_ALBUM:
    case MU_AJOUTER_ALBUM:
        if (arg >= 0 && arg < mu_etat.nb_albums)
            jouer("player_queues/play_media", "{\"queue_id\":\"%s\",\"media\":\"%s\",\"option\":\"%s\"}", q, mu_etat.albums[arg].uri,
                      a == MU_LIRE_ALBUM ? "replace" : "add");
        break;
    case MU_JOUER_RADIO:
        if (arg >= 0 && arg < mu_etat.nb_radios)
            jouer("player_queues/play_media", "{\"queue_id\":\"%s\",\"media\":\"%s\",\"option\":\"replace\"}", q, mu_etat.radios[arg].uri);
        break;
    case MU_LECTEUR: // ce qui joue part avec (transfert de la file) ; sinon on change seulement de lecteur
        if (arg >= 0 && arg < NB_LECTEURS && arg != g_lecteur)
        {
            const int avant = g_lecteur;
            g_lecteur = arg;
            g_attente_t = 0; // une lecture qui attendait le Salon n'a plus lieu d'être
            g_index = g_nb_items = -1;
            if (mu_etat.lecture) // vers le Salon éteint : le transfert attend piCore
                jouer("player_queues/transfer", "{\"source_queue_id\":\"%s\",\"target_queue_id\":\"%s\",\"auto_play\":true}",
                          LECTEURS[avant].id, LECTEURS[arg].id);
            else
            {
                g_relire = true;
                g_relire_t = lv_tick_get() - 1500; // pas de commande à laisser appliquer : relire au prochain tic
            }
        }
        break;
    case MU_GROUPE:
        if (arg >= 0 && arg < NB_LECTEURS)
            commander("players/cmd/set_members", "{\"target_player\":\"%s\",\"%s\":[\"%s\"]}", q,
                      mu_etat.lecteurs[arg].groupe ? "player_ids_to_add" : "player_ids_to_remove", LECTEURS[arg].id);
        break;
    case MU_AMPLI: // chaîne piCore, ampli et son entrée : par HA (MQTT)
        publier_ha(mu_etat.ampli ? "{\"action\":\"ampli\",\"on\":1}" : "{\"action\":\"ampli\",\"on\":0}");
        break;
    case MU_CHAINE:
        if (!arg)
            g_attente_t = 0;
        publier_ha(arg ? "{\"action\":\"picore\",\"on\":1}" : "{\"action\":\"picore\",\"on\":0}");
        break;
    case MU_ENTREE:
    {
        char m[40];
        snprintf(m, sizeof m, "{\"action\":\"entree\",\"n\":%d}", arg);
        publier_ha(m);
        break;
    }
    }
}

void ui_musique_brancher()
{
    mu_commande = commande;
    mu_etat.nb_radios = 0; // les favorites de MA, à la lecture de la bibliothèque
    mu_etat.nb_lecteurs = NB_LECTEURS;
    for (int i = 0; i < NB_LECTEURS; i++)
    {
        snprintf(mu_etat.lecteurs[i].nom, sizeof mu_etat.lecteurs[i].nom, "%s", LECTEURS[i].nom);
        snprintf(mu_etat.lecteurs[i].appareil, sizeof mu_etat.lecteurs[i].appareil, "%s", LECTEURS[i].appareil);
        mu_etat.lecteurs[i].en_ligne = mu_etat.lecteurs[i].groupe = false;
    }
    mu_etat.lecteur = g_lecteur;
    static lv_timer_t *t = nullptr;
    if (!t)
        t = lv_timer_create(tic, 1000, nullptr);
}
