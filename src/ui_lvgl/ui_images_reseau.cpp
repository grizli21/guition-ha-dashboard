// 23/10/2026 : téléchargement HTTP des images (miniatures de l'imprimante, pochettes de MA) ; ESP32 seulement (le simulateur lit des fichiers locaux).
#ifdef ARDUINO
#include "ui_images.h"
#include <Arduino.h>
#include <HTTPClient.h>

#define TAILLE_MAX (128 * 1024) // miniature 300 px du slicer ~10 Ko ; pochette PNG 160 px de MA jusqu'à ~76 Ko (vu le 23/10)

static bool telecharger(const char *url, uint8_t **donnees, size_t *taille)
{
    HTTPClient http;
    http.setConnectTimeout(1000); // réseau local : une imprimante qui ne répond pas ne fige pas l'écran plus d'1 s
    http.setTimeout(2000);
    if (!http.begin(url))
        return false;
    bool ok = false;
    if (http.GET() == HTTP_CODE_OK)
    {
        int n = http.getSize();
        if (n > 0 && n <= TAILLE_MAX)
        {
            uint8_t *buf = (uint8_t *)ps_malloc(n);
            if (buf && http.getStream().readBytes(buf, n) == (size_t)n)
            {
                *donnees = buf;
                *taille = n;
                ok = true;
            }
            else
                free(buf);
        }
    }
    http.end();
    return ok;
}

// 25/10 : tâche de fond sur le cœur 0 (celui du Wi-Fi ; LVGL tourne sur le 1), qui se termine avec son travail
// le travail passe en paramètre de la tâche (25/10 : une variable commune, deux tâches lancées ensemble = la mauvaise)
static void tache(void *travail)
{
    ((void (*)(void *))travail)(nullptr);
    vTaskDelete(nullptr);
}
static bool en_fond(void (*travail)(void *))
{
    return xTaskCreatePinnedToCore(tache, "img_fond", 16384, (void *)travail, 1, nullptr, 0) == pdPASS;
}

// branché avant le premier appel (constructeur statique : rien à appeler depuis le setup)
static struct branchement_images_t // 26/10 : nom propre au fichier (deux « branchement_t » = un seul pour l'éditeur de liens : la pause de la caméra devenait ESP.restart)
{
    branchement_images_t()
    {
        ui_images_telecharger = telecharger;
        ui_images_en_fond = en_fond;
    }
} g_branchement;
#endif
