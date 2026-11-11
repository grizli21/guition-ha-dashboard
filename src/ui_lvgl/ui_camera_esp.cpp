// 26/10/2026 : lecture des images des caméras sur l'ESP32 (voir ui_camera.h) : 28/10, flux MJPEG de go2rtc (une requête,
// puis les images poussées par le serveur, une par partie), connexion gardée ouverte, Nagle coupé, 1,5 s au plus par image.
// Appelée depuis la tâche de lecture continue de ui_camera, une image à la fois : pas d'accès concurrent à la connexion.
#ifdef ARDUINO
#include "ui_camera.h"
#include "ui_diag.h"
#include <Arduino.h>
#include <string.h>
#include <lwip/sockets.h>
#include <esp_heap_caps.h>
#include <lwip/netdb.h>
#include <stdint.h>

// 26/10 : socket lwIP direct plutôt que WiFiClient : mesuré sur la chambre 1 (banc autonome, 10 s chacun, 37 Ko par
// image) : WiFiClient available/read 78 ms par image, recv() direct 64 ms ; le reste est le débit permis par la fenêtre TCP
// de 5 760 octets (fixée à la compilation de l'Arduino, comme celui de la production)
// une connexion par canal (0 seul utilisé ; la seconde chaîne essayée le 26/10 n'apportait que 4 % : PSRAM partagée)
struct cnx_t
{
    int fd = -1;
    char hote[48] = "";
    uint16_t port = 0;
    char envoyee[96] = "";    // url du flux demandé
    char tete[512];           // 27/10 : en-têtes de la partie en cours (et le début de la suivante, s'il est déjà arrivé)
    size_t ntete = 0;
    bool flux = false;        // flux demandé (une requête, puis les images poussées par le serveur)
    bool flux_ouvert = false; // en-tête HTTP du flux déjà lu : ne restent que les parties (une image chacune)
};
static cnx_t g_cnx[2];

static void fermer(cnx_t &k)
{
    if (k.fd >= 0)
        close(k.fd);
    k.fd = -1;
    k.envoyee[0] = 0;
    k.ntete = 0;
    k.flux = k.flux_ouvert = false;
}

static bool ouvrir(cnx_t &k, const char *hote, uint16_t port)
{
    fermer(k);
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_pton(AF_INET, hote, &a.sin_addr) != 1)
    {
        struct hostent *h = gethostbyname(hote);
        if (!h)
            return false;
        memcpy(&a.sin_addr, h->h_addr, sizeof a.sin_addr);
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    const int un = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &un, sizeof un); // Nagle coupé, comme la production
    struct timeval tv = {1, 500000};                           // 1,5 s au plus pour une réponse (comme la production)
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0)
    {
        close(fd);
        return false;
    }
    k.fd = fd;
    snprintf(k.hote, sizeof k.hote, "%s", hote);
    k.port = port;
    return true;
}

// « http://hote:port/chemin » -> hote, port, chemin
static bool decouper(const char *url, char *hote, size_t nh, uint16_t &port, const char *&chemin)
{
    if (strncmp(url, "http://", 7) != 0)
        return false;
    const char *h = url + 7, *c = strchr(h, '/'), *p = strchr(h, ':');
    if (!c)
        return false;
    port = 80;
    const char *fin_hote = c;
    if (p && p < c)
    {
        port = (uint16_t)atoi(p + 1);
        fin_hote = p;
    }
    const size_t n = (size_t)(fin_hote - h);
    if (n == 0 || n >= nh)
        return false;
    memcpy(hote, h, n);
    hote[n] = 0;
    chemin = c;
    return true;
}

// en-têtes (réponse HTTP, ou partie du flux MJPEG) lus dans k.tete jusqu'à la ligne vide : leur longueur, 0 si échec
static size_t lire_tete(cnx_t &k, uint32_t debut)
{
    for (;;)
    {
        for (size_t i = 3; i < k.ntete; i++)
            if (k.tete[i - 3] == '\r' && k.tete[i - 2] == '\n' && k.tete[i - 1] == '\r' && k.tete[i] == '\n')
                return i + 1;
        if (k.ntete >= sizeof k.tete - 1 || millis() - debut >= 1500)
            return 0; // en-têtes trop longs ou réponse trop lente
        const int r = recv(k.fd, k.tete + k.ntete, sizeof k.tete - 1 - k.ntete, 0);
        if (r <= 0)
            return 0;
        k.ntete += r;
    }
}

// cherche nom dans les en-têtes k.tete[0, fin) (chaîne terminée le temps de la recherche)
static const char *chercher(cnx_t &k, size_t fin, const char *nom, char *copie, size_t taille)
{
    const char c = k.tete[fin];
    k.tete[fin] = 0;
    const char *t = strcasestr(k.tete, nom);
    snprintf(copie, taille, "%s", t ? t + strlen(nom) : "");
    k.tete[fin] = c;
    return t ? copie : nullptr;
}

// une image : la partie suivante du flux MJPEG (url go2rtc …/api/stream.mjpeg?src=…, ou chemin « / » : port de streaming
// d'une caméra) ; le JPEG (Content-Length octets) reçu au début de buf. 28/10 : seuls les flux sont lus (les requêtes
// image par image, keep-alive, de motionEye ont été retirées le 02/11)
static bool lire(int canal, const char *url, uint8_t *buf, size_t max, size_t *n)
{
    if (canal < 0 || canal > 1)
        return false;
    cnx_t &k = g_cnx[canal];
    char hote[48];
    uint16_t port;
    const char *chemin;
    if (!buf || !decouper(url, hote, sizeof hote, port, chemin))
        return false;
    if (strcmp(chemin, "/") != 0 && !strstr(chemin, "stream.mjpeg"))
        return false; // pas un flux MJPEG
    if (k.fd >= 0 && k.flux && strcmp(k.envoyee, url) != 0)
        fermer(k); // le flux d'une autre caméra est en route : connexion neuve
    if ((k.fd < 0 || strcmp(hote, k.hote) != 0 || port != k.port) && !ouvrir(k, hote, port))
        return false;
    // 27/10 : HTTP/1.0 : le serveur envoie alors le flux tel quel (en HTTP/1.1, découpé en morceaux de 16 Ko au milieu des
    // images) ; une seule requête, les images arrivent dès qu'elles existent : ni doublon demandé ni aller-retour
    if (!k.flux)
    {
        char req[160];
        const int lr = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", chemin, hote);
        if (send(k.fd, req, lr, 0) != lr)
        {
            fermer(k);
            return false;
        }
        k.flux = true;
        snprintf(k.envoyee, sizeof k.envoyee, "%s", url);
    }
    const uint32_t debut = millis();
    // 27/10 : en-têtes lus dans un petit tampon à part, puis le JPEG reçu DIRECTEMENT à sa place dans buf, arrêté pile à
    // sa fin (Content-Length) : plus de recopie de 38 Ko dans la PSRAM après coup (14 à 17 ms par image mesurés, la PSRAM
    // étant partagée avec le décodeur et l'écran)
    char val[32];
    size_t fin_tete = lire_tete(k, debut);
    if (fin_tete && !k.flux_ouvert) // l'en-tête HTTP du flux, une fois : 200 et multipart, puis la 1re partie
    {
        const bool ok = strncmp(k.tete, "HTTP/1.", 7) == 0 && strncmp(k.tete + 8, " 200", 4) == 0 &&
                        chercher(k, fin_tete, "multipart", val, sizeof val);
        k.ntete -= fin_tete;
        memmove(k.tete, k.tete + fin_tete, k.ntete);
        k.flux_ouvert = ok;
        fin_tete = ok ? lire_tete(k, debut) : 0;
    }
    const size_t corps = fin_tete && chercher(k, fin_tete, "Content-Length:", val, sizeof val) ? (size_t)atol(val) : 0;
    if (!fin_tete || !corps || corps > max)
    {
        fermer(k);
        return false;
    }
    const uint32_t debut_corps = millis();
    ui_camera_t_attente = debut_corps - debut;
    // le début du JPEG, reçu avec les en-têtes ; au-delà (corps plus court que ce qui a été lu) : la réponse suivante
    size_t pos = LV_MIN(k.ntete - fin_tete, corps);
    memcpy(buf, k.tete + fin_tete, pos);
    k.ntete -= fin_tete + pos;
    memmove(k.tete, k.tete + fin_tete + pos, k.ntete);
    while (pos < corps)
    {
        if (millis() - debut >= 1500)
        {
            fermer(k);
            return false;
        }
        const int r = recv(k.fd, buf + pos, corps - pos, 0);
        if (r <= 0)
        {
            fermer(k);
            return false;
        }
        pos += r;
    }
    const uint32_t fin_reception = millis();
    ui_camera_t_reception = fin_reception - debut_corps;
    *n = corps;
    return *n > 2 && buf[0] == 0xFF && buf[1] == 0xD8;
}

// Deux tâches de fond créées à la demande et rendues à la fermeture : 0 = décodage (cœur 1), 1 = lecture
// continue (cœur 0, près du Wi-Fi) ; piles en RAM interne (8 Ko décodage, 4 Ko lecture).
static TaskHandle_t g_tache[2] = {nullptr, nullptr};
static void (*volatile g_travail[2])(void *) = {nullptr, nullptr};
static volatile bool g_arret[2] = {false, false};  // demande : la tâche sort de sa boucle au prochain réveil
static volatile bool g_en_vie[2] = {false, false}; // la tâche existe (sa pile de RAM interne est prise)
static volatile size_t g_piles_rendues = 0;        // octets de pile rendus depuis la demande d'arrêt
static portMUX_TYPE g_tache_verrou = portMUX_INITIALIZER_UNLOCKED;
#define PILE_TACHE(c) ((c) ? 4096u : 8192u) // profondeurs passées à xTaskCreatePinnedToCore (en octets sur ESP-IDF)
static void boucle(void *canal)
{
    const int c = (int)(intptr_t)canal;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (g_arret[c])
            break; // 08/11 : le travail en cours a fini (ulTaskNotifyTake ne revient qu'après son retour) : on se rend
        if (g_travail[c])
            g_travail[c](nullptr);
    }
    // ici la tâche ne tient plus aucun tampon de la caméra : son travail est revenu. Elle rend sa pile elle-même.
    portENTER_CRITICAL(&g_tache_verrou);
    g_en_vie[c] = false;
    g_tache[c] = nullptr;
    g_arret[c] = false; // tâche suivante : aucune demande en suspens
    g_piles_rendues += PILE_TACHE(c);
    portEXIT_CRITICAL(&g_tache_verrou);
    vTaskDelete(NULL);
}
static bool lancer(int canal, void (*travail)(void *))
{
    if (canal < 0 || canal > 1)
        return false;
    portENTER_CRITICAL(&g_tache_verrou);
    const bool en_vie = g_en_vie[canal];
    const bool arret_demande = g_arret[canal];
    portEXIT_CRITICAL(&g_tache_verrou);
    if (en_vie && arret_demande)
        return false; // elle va se rendre : ne pas lui confier un travail qu'elle n'exécutera pas
    if (!en_vie &&
        xTaskCreatePinnedToCore(boucle, canal == 0 ? "camera" : "camera_lit1", canal ? 4096 : 8192, (void *)(intptr_t)canal, 1,
                                &g_tache[canal], canal ? 0 : 1) != pdPASS) // 0 décodage (cœur 1), 1 lecture (cœur 0)
    {
        g_tache[canal] = nullptr;
        return false;
    }
    if (!en_vie)
    {
        portENTER_CRITICAL(&g_tache_verrou);
        g_en_vie[canal] = true;
        g_arret[canal] = false; // tâche neuve : on repart sans demande d'arrêt
        portEXIT_CRITICAL(&g_tache_verrou);
    }
    g_travail[canal] = travail;
    xTaskNotifyGive(g_tache[canal]);
    return true;
}

// 08/11 : demande d'arrêt des deux tâches (la boucle de l'écran ne rendra les tampons qu'après leur départ)
static void arreter_taches()
{
    g_piles_rendues = 0;
    for (int c = 0; c < 2; c++)
    {
        TaskHandle_t h;
        portENTER_CRITICAL(&g_tache_verrou);
        h = g_tache[c];
        const bool en_vie = g_en_vie[c];
        g_arret[c] = true;
        portEXIT_CRITICAL(&g_tache_verrou);
        if (en_vie && h)
            xTaskNotifyGive(h); // réveille celle qui attend son travail : elle sort de sa boucle et se rend
    }
}
static bool taches_rendues(size_t *octets)
{
    if (octets)
        *octets = g_piles_rendues;
    return !g_en_vie[0] && !g_en_vie[1];
}

// 29/10 : diagnostic (panne « chambre 1 sourde » après Biblio) : octets reçus et pas encore lus sur chaque connexion de la
// caméra ; chaque paquet en attente garde un tampon de réception du Wi-Fi (32 en tout ; pas de copie L2 -> L3)
size_t ui_camera_etat_connexions(char *s, size_t n)
{
    size_t l = 0;
    for (int c = 0; c < 2; c++)
    {
        int attente = -1;
        if (g_cnx[c].fd >= 0)
            lwip_ioctl(g_cnx[c].fd, FIONREAD, &attente);
        l += snprintf(s + l, l < n ? n - l : 0, "connexion caméra %d : %s, %s, %d octets reçus non lus\n", c,
                      g_cnx[c].fd >= 0 ? "OUVERTE" : "fermée", g_cnx[c].flux ? "flux" : "-", attente);
    }
    return l;
}

// branché avant le premier appel (constructeur statique : rien à appeler depuis le setup)
static struct branchement_camera_t // 26/10 : nom propre au fichier (deux « branchement_t » = un seul pour l'éditeur de liens : la pause de la caméra devenait ESP.restart)
{
    branchement_camera_t()
    {
        ui_camera_lire = lire;
        ui_camera_lancer = lancer;
        ui_camera_pause = []() { delay(1); };
        ui_camera_arreter_taches = arreter_taches;   // 08/11 : la caméra rend ses tâches de fond en se fermant
        ui_camera_taches_rendues = taches_rendues;
        ui_camera_couper = []() // 29/10 : la boucle seule, quand aucune tâche de la caméra ne lit
        {
            for (int c = 0; c < 2; c++)
                if (g_cnx[c].fd >= 0)
                {
                    int attente = 0;
                    lwip_ioctl(g_cnx[c].fd, FIONREAD, &attente);
                    ui_trace("[camera] connexion %d coupée (caméra quittée) ; %d octets reçus non lus libérés\n", c, attente);
                    fermer(g_cnx[c]);
                }
        };

    }
} g_branchement;
#endif
