# Écran sur batterie

[English](README.en.md) | **Français**

Une variante de l'écran Guition ESP32-S3-4848S040 qui fonctionne **sans fil** : une batterie lithium,
un radar de présence qui réveille l'écran quand quelqu'un approche, et un firmware qui passe l'essentiel
de son temps en veille profonde. C'est le même firmware que les écrans muraux, avec la page « Batterie et
capteur » en plus.

![Les quatre pièces du boîtier dans le trancheur](apercu_boitier.jpg)

*Page en cours de rédaction : références de la batterie, du module de charge et de l'ampli, et photos à venir.*

## Matériel

| Élément | Détail |
|---|---|
| Écran | Guition ESP32-S3-4848S040 (le même que les écrans muraux) |
| Mesure de la tension | pont diviseur 100 kΩ / 47 kΩ sur le GPIO 1 (ADC) ([ui_batterie_esp.cpp](../../src/ui_lvgl/ui_batterie_esp.cpp)) |
| Détection de présence | radar 24 GHz **HLK-LD2410S** : sortie OUT sur le GPIO 2 (réveille l'ESP32-S3 de sa veille profonde, réveil ext0) ; port série sur les GPIO 41 (RX de l'ESP32) et 40 (TX), 115 200 bauds, pour les mesures et les réglages |
| Boîtier | dessiné de A à Z, 4 pièces imprimées en 3D : voir [Boîtier](#boîtier) |

## Modifications de la carte

- **Amplificateur audio de la carte décâblé.**
- **Pont diviseur** ajouté pour lire la tension de la batterie sur le GPIO 1.
- **Radar LD2410S** câblé : sortie OUT sur le GPIO 2 pour réveiller l'écran, port série sur les GPIO 40 et 41.

## Consommation mesurée

Mesures faites à l'alimentation de laboratoire, réglée à la tension de la batterie.

| État | Courant | Conditions |
|---|---|---|
| Veille profonde | **1,86 mA** à 3,7 V | radar actif (une trame toutes les 2 s) ; mesuré avec l'ancien firmware |
| Éveillé | **~70 mA** | ESP32-S3 à 80 MHz, **sans** rétroéclairage |
| Radar LD2410S seul | 68 µA à 0,5 Hz, 635 µA à 8 Hz | valeurs de la notice du fabricant (V1.04), non mesurées ici |

## Ce que fait le firmware pour économiser

- **Veille profonde** dès que personne n'est là, après le délai de veille réglé : la dalle passe en veille
  profonde (commande DSTB du ST7701), et le radar la réveille (GPIO 2).
- **Démarrage court** au réveil : pas d'écran de démarrage, borne Wi-Fi et canal gardés en mémoire RTC
  pour se reconnecter sans balayage. Mesuré : **0,69 à 0,72 s** jusqu'à l'écran prêt, contre 1,39 s
  avec l'ancien firmware (avant le passage à LVGL).
- **Mode économie** après 10 s sans toucher : luminosité de repos, processeur à 80 MHz, Wi-Fi en
  économie légère (`WIFI_PS_MIN_MODEM`).
- **Wi-Fi coupé après la synchronisation** (option) : la radio s'éteint une fois les données reçues de
  Home Assistant, et se rallume au premier toucher.
- **Puissance d'émission Wi-Fi réglable** (option : 7 dBm, moins de courant, moins de portée).
- Essayé sans gain : `WIFI_PS_MAX_MODEM` (tout le trafic est rattrapé d'un coup au réveil de la radio).

La page **Batterie et capteur** montre la charge, l'historique des réveils sur 24 h (radar, toucher,
minuterie) et le détail du dernier réveil, en millisecondes : Wi-Fi, MQTT, synchronisation, coupure.
La tension est lissée sur 64 lectures (les 4 extrêmes écartés) ; la charge est déduite de la tension
(au-dessus de 4,15 V).

## La bibliothèque du radar, modifiée

La bibliothèque [HLK-LD2410S](https://github.com/phuongnamzz/HLK-LD2410S) est incluse dans
[lib/HLK-LD2410S](../../lib/HLK-LD2410S), avec ces modifications (datées en commentaire dans le code) :

- **Compilation** avec l'ESP-IDF 5 : un `return` manquait.
- **Commandes fiables** : la trame part d'un seul envoi (elle partait en 19 morceaux), et la réponse est
  cherchée par son en-tête et l'écho de la commande, comme dans ESPHome. Avant, elle était lue comme un bloc
  de taille fixe au début du tampon : une trame de mesure arrivée juste avant la décalait, et le passage en
  configuration ratait au premier essai. Jusqu'à 3 renvois si le capteur ne répond pas.
- **Seuils du firmware 1.1.1** du capteur : 16 seuils de déclenchement et 16 seuils de maintien, d'après son
  protocole de 2024 (la bibliothèque suivait une notice de 2023).
- **Énergie par zone en dB**, comme l'outil Hi-Link, pour la comparer directement aux seuils.
- **Réglage automatique** : il n'attend plus indéfiniment quand le capteur cesse d'envoyer sa progression.
- **Points de mesure** (trames brutes, commandes acquittées, compteurs) pour le journal et la page web du radar.

## Boîtier

Dessiné de A à Z sous Fusion, pour l'écran avec sa batterie et le radar. Quatre pièces, au format STL
(sur GitHub, ouvrez un fichier : il s'affiche en 3D) :

| Pièce | Fichier | Dimensions |
|---|---|---|
| Boîtier inférieur | [modeles/boitier_inferieur.stl](modeles/boitier_inferieur.stl) | 87,1 × 87,5 × 25,0 mm |
| Dos d'écran | [modeles/dos_ecran.stl](modeles/dos_ecran.stl) | 86,9 × 87,3 × 7,7 mm |
| Couvercle | [modeles/couvercle.stl](modeles/couvercle.stl) | 82,5 × 82,9 × 3,0 mm |
| Pivot d'écran | [modeles/pivot_ecran.stl](modeles/pivot_ecran.stl) | 18,0 × 13,1 × 12,3 mm |

**Licence des fichiers 3D :** [Creative Commons BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/)
— libre d'imprimer, de modifier et de partager en citant l'auteur, **sans usage commercial**. Ces fichiers ne
sont pas couverts par la licence MIT du code (voir [LICENSE](LICENSE)).
