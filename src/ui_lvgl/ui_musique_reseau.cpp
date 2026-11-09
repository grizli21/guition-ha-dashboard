// 24/10/2026 : transport HTTP de l'API de Music Assistant (ui_musique) ; ESP32 seulement (le simulateur rejoue des
// réponses enregistrées). Comme ui_images_reseau : la requête bloque la boucle, donc LVGL, le temps de la réponse.
#ifdef ARDUINO
#include "ui_musique.h"
#include <Arduino.h>
#include "ui_diag.h"
#include <HTTPClient.h>
#include <WiFi.h> // WiFiClient (Arduino 3 : plus inclus par HTTPClient.h)
#include <esp_heap_caps.h>
#include "../secrets_ma.h" // adresse de MA + jeton (hors git)

#define TAILLE_MAX (1024 * 1024) // la plus grosse réponse demandée : tous les artistes, 392 Ko le 24/10

static bool requete(const char *corps, char **reponse, size_t *taille)
{
    const uint32_t t0 = millis();
    HTTPClient http;
    http.setConnectTimeout(1000); // réseau local : MA arrêté ne fige pas l'écran plus d'1 s
    http.setTimeout(3000);
    http.useHTTP10(true); // pas de réponse découpée (« chunked ») : on lit la longueur annoncée, ou jusqu'à la fermeture
    if (!http.begin(MA_SERVEUR "/api"))
        return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " MA_JETON);
    const int code = http.POST((uint8_t *)corps, strlen(corps));
    char *buf = nullptr;
    size_t lu = 0;
    if (code == HTTP_CODE_OK)
    {
        const int n = http.getSize(); // -1 : longueur non annoncée
        size_t cap = n > 0 ? n : 64 * 1024;
        if (cap <= TAILLE_MAX)
            buf = (char *)ps_malloc(cap + 1);
        WiFiClient *s = http.getStreamPtr();
        uint32_t calme = millis();
        while (buf && (n < 0 || lu < (size_t)n) && millis() - calme < 3000)
        {
            if (lu == cap) // longueur non annoncée : on agrandit
            {
                char *b = cap * 2 <= TAILLE_MAX ? (char *)ps_realloc(buf, cap * 2 + 1) : nullptr;
                if (!b)
                {
                    free(buf);
                    buf = nullptr;
                    break;
                }
                buf = b;
                cap *= 2;
            }
            int k = s->read((uint8_t *)buf + lu, cap - lu);
            if (k > 0)
            {
                lu += k;
                calme = millis();
            }
            else if (!s->connected())
                break;
            else
                delay(1);
        }
        if (buf && n > 0 && lu != (size_t)n) // réponse coupée
        {
            free(buf);
            buf = nullptr;
        }
    }
    http.end();
    // à lire sur le banc : la durée des grosses réponses (artistes, albums, file) bloque l'écran d'autant ; 24/10 : les
    // relevés rapides (toutes les 5 s) ne sont plus tracés, ils chassaient le reste des 3 Ko de traces
    const uint32_t duree = millis() - t0;
    if (duree >= 100 || code != HTTP_CODE_OK || lu > 16 * 1024)
        ui_trace("[MA] %.48s : HTTP %d, %u octets en %lu ms (RAM interne : libre %u Ko, plus bas %u Ko, à %lu ms)\n", corps + 12, code,
                 (unsigned)lu, (unsigned long)duree, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned long)millis());
    if (!buf)
        return false;
    buf[lu] = 0;
    *reponse = buf;
    *taille = lu;
    return true;
}

// 24/10 : tâche de fond sur le cœur 0 (celui du Wi-Fi ; LVGL tourne sur le 1), qui se termine avec son travail
// le travail passe en paramètre de la tâche (25/10 : une variable commune, deux tâches lancées ensemble = la mauvaise)
static void tache(void *travail)
{
    ((void (*)(void *))travail)(nullptr);
    vTaskDelete(nullptr);
}
static bool en_fond(void (*travail)(void *))
{
    return xTaskCreatePinnedToCore(tache, "ma_fond", 12288, (void *)travail, 1, nullptr, 0) == pdPASS;
}

// branché avant le premier appel (constructeur statique : rien à appeler depuis le setup)
static struct branchement_musique_t // 26/10 : nom propre au fichier (deux « branchement_t » = un seul pour l'éditeur de liens : la pause de la caméra devenait ESP.restart)
{
    branchement_musique_t()
    {
        ui_ma_requete = requete;
        ui_ma_en_fond = en_fond;
        ui_ma_serveur = MA_SERVEUR;
    }
} g_branchement;
#endif
