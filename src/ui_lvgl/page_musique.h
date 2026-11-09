// 22/10/2026 : page Musique (maquette arrêtée le 22/10) : télécommande de Music Assistant (via HA / MQTT).
// Onglets Lecture | Biblio | Radios | Sortie ; Playlist par le bouton ☰ (rattachée à Lecture).
// Lecture : pochette RONDE dans un anneau : arc épais = VOLUME (à glisser), liseré fin = progression ;
// glisser à gauche / à droite sur la pochette = morceau suivant / précédent (fondu au changement).
// Les listes (file, artistes, albums, radios, lecteurs) sont remplies par l'appelant ; la page ne fait qu'afficher.
#pragma once
#include <lvgl.h>

#define MU_FILE_MAX 20
#define MU_LISTE_MAX 16
#define MU_LOGO 80       // logo des radios demandé à MA (une des tailles qu'il sert : 80/160/256…)
#define MU_LOGO_LISTE 34 // ... réduit pour la liste des radios
#define MU_RADIOS_MAX 40 // 25/10 : les radios FAVORITES de Music Assistant (retour d'usage : « la source est MA, rien en dur »)
#define MU_ARTISTES_MAX 64 // artistes d'UNE lettre (23/10 : 537 artistes dans MA, 60 au plus pour une lettre)

struct mu_piste_t
{
    char titre[48];
    char artiste[32];
    int duree; // s
};
struct mu_element_t // artiste, album ou radio
{
    char nom[40];
    char detail[24];         // « 6 albums », « 1998 »…
    char uri[64];            // identifiant Music Assistant : « library://album/12 », « radiobrowser://radio/<uuid> », n° d'artiste
    const lv_img_dsc_t *img; // pochette / logo (nullptr : dégradé + nom, en attendant ou faute d'image)
};
struct mu_lecteur_t
{
    char nom[24];
    char appareil[24];
    bool en_ligne;
    bool groupe; // joue en même temps que le lecteur choisi
};

struct mu_etat_t
{
    // en cours
    char titre[64], artiste[40], album[48];
    int duree, position; // s (duree = 0 : radio en direct)
    bool lecture, alea, repete;
    int volume;
    const lv_img_dsc_t *pochette; // nullptr : pochette dessinée (dégradé + nom de l'album)
    // listes
    mu_piste_t file[MU_FILE_MAX];
    int nb_file, piste; // piste = index en cours dans file[]
    int premiere, nb_total; // file[] = une fenêtre de la file : rang de file[0], et nombre de morceaux de toute la file
    mu_element_t artistes[MU_ARTISTES_MAX], albums[MU_LISTE_MAX], radios[MU_RADIOS_MAX];
    int nb_artistes, nb_albums, nb_radios;
    char lettre;      // lettre dont les artistes sont dans artistes[] ('#' = chiffres et signes)
    char lettres[32]; // lettres qui ont au moins un artiste (« #ABCD… ») : les autres sont grisées
    mu_lecteur_t lecteurs[6];
    int nb_lecteurs, lecteur; // lecteur choisi
    // 24/10 : chaîne piCore (26/10 ; ex-moOde : Pi + ampli Onkyo sur la même prise) et ampli, par HA
    int chaine;               // mu_chaine_t
    bool ampli;               // ampli allumé (puissance de la prise > 10 W)
    int entree;               // entrée de l'ampli choisie depuis l'écran (1..3), 0 = inconnue (l'ampli ne la dit pas)
};
extern mu_etat_t *mu_etat_p; // 29/10 : en PSRAM (ui_psram.h)
#define mu_etat (*mu_etat_p)

enum mu_chaine_t
{
    MU_CH_ETEINTE,
    MU_CH_ARRET,     // arrêt propre en cours (~30 s : lecteur, puis ampli et prise)
    MU_CH_DEMARRAGE, // prise allumée, lecteur pas encore dans Music Assistant
    MU_CH_PRETE
};

enum mu_action_t
{
    MU_LECTURE_PAUSE,
    MU_SUIVANT,
    MU_PRECEDENT,
    MU_VOLUME,
    MU_ALEA,
    MU_REPETE,
    MU_JOUER_PISTE,   // arg = index dans la file
    MU_VIDER_FILE,
    MU_OUVRIR_ARTISTE, // arg = index : l'appelant remplit albums puis appelle page_musique_maj()
    MU_LIRE_ALBUM,     // arg = index
    MU_AJOUTER_ALBUM,  // arg = index
    MU_JOUER_RADIO,    // arg = index
    MU_LECTEUR,        // arg = index du lecteur choisi
    MU_GROUPE,         // arg = index du lecteur ajouté / retiré
    MU_AMPLI,  // mu_etat.ampli = l'état voulu
    MU_CHAINE, // arg = 1 marche, 0 arrêt (propre)
    MU_ENTREE, // arg = 1..3
    MU_LETTRE, // arg = la lettre choisie (caractère) : l'appelant remplit artistes[] puis appelle page_musique_maj()
    MU_LOGOS   // 25/10 : la vue Radios est affichée : logos à demander (une fois ; ils arrivent par page_musique_logo_radio)
};
extern void (*mu_commande)(mu_action_t action, int arg);

void page_musique_creer(lv_obj_t *zone);
void page_musique_quitter();
void page_musique_maj();
void page_musique_maj_lecture(); // seulement l'onglet Lecture s'il est affiché (position qui avance) : jamais de reconstruction
void page_musique_pochette_album(int i); // 24/10 : la pochette de l'album i est arrivée : posée dans sa case, sans reconstruire
void page_musique_logo_radio(int i);     // 25/10 : le logo de la radio i est arrivé : posé dans sa ligne
bool page_musique_affichee();
