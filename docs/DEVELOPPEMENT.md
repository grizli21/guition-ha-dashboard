# Développement

← [Retour au README](../README.md)

## Outillage

| Outil | Usage |
|---|---|
| [tools/banc.py](../tools/banc.py) | piloter un écran réel depuis le PC : capture PNG, appuis/glissés simulés, journal Telnet, dump de plantage (HTTP :8080, réservé au PC `RESEAU_PC`) |
| [tools/guetter_dumps.py](../tools/guetter_dumps.py) | espionner les topics `dashboard_guition/` du broker (paho-mqtt 1.x) |
| [tools/gen_conf_v9.py](../tools/gen_conf_v9.py) | régénère `conf_v9/lv_conf.h` depuis le gabarit LVGL 9.6 |
| [tools/polices_ui.sh](../tools/polices_ui.sh) | régénère les polices Montserrat / K-Type 3x5 (`lv_font_conv`) |
| [tools/icon2lvgl.py](../tools/icon2lvgl.py) · [tools/creer_ico_mdi.py](../tools/creer_ico_mdi.py) | convertir des icônes SVG/PNG en descripteurs LVGL (les sources Material Design Icons sont dans `tools/mdi/`) |
| [tools/comparer_captures.py](../tools/comparer_captures.py) | diff image à image entre deux jeux de captures |

## Ajouter une page

1. Créer `src/ui_lvgl/page_x.cpp` sur le gabarit des autres : `page_x_creer(parent)` /
   `page_x_quitter()`, à partir de n'importe quelle page existante.
2. L'ajouter à la table `pages[]` de [src/ecran/ecran.cpp](../src/ecran/ecran.cpp) (icône, titre,
   fonctions) — le rail de droite se met à jour tout seul.
3. Définir le contrat d'abord : ajouter la section dans [CONTRAT_MQTT.md](../CONTRAT_MQTT.md)
   (`etat` / `set`), puis l'automatisation côté HA sur le gabarit de `ha_propositions/`.
4. Vérifier au simulateur : ajouter un état dans `simu/donnees/` et une capture au scénario
   (`simu/simu.cpp`), `make -C simu`.

## Diagnostics

- **Journal série** : `pio device monitor` (115 200 bauds).
- **Journal à distance** : Telnet sur le port 23 de l'écran, sans redémarrer —
  `python3 tools/banc.py journal 60`.
- **Banc PC** : HTTP sur le port 8080, réservé à la machine `RESEAU_PC` — capture d'écran,
  appuis simulés, traces, dump du dernier plantage (`tools/banc.py dump`).
- **Chien de garde** : redémarre la boucle figée ; la raison du redémarrage et le plantage
  précédent s'affichent page Informations.
- **Page Informations** : santé (RAM/PSRAM, température CPU, version git), liaison MQTT/Wi-Fi au
  vert ou au rouge, journal des événements.

## Simulateur

```bash
make -C simu                 # compile le VRAI code des pages, joue le scénario, écrit simu/captures/*.png
make -C simu asan            # mêmes pages sous AddressSanitizer / UBSan
python3 simu/galerie.py galerie.html   # assemble les captures en une page HTML consultable
```

Le simulateur rejoue les messages MQTT d'un relevé réel anonymisé ([simu/donnees/](../simu/donnees/))
et produit ~280 captures, dont les images des trois GIF du README. C'est lui qui a fabriqué la
galerie du README — le code affiché est
bit à bit celui de l'écran (mêmes sources, même `lv_conf.h`, couleurs 16 bits).

## Arborescence

```
board/           json de carte (esp32-s3-devkitc-1-n16r8v)
conf_v9/         lv_conf.h LVGL 9.6 (généré par tools/gen_conf_v9.py)
docs/images/     galerie (captures du simulateur) · docs/patches/ patchs LVGL
ha_propositions/ 23 automatisations Home Assistant (YAML)
include/         wrapper LovyanGFX de la dalle, en-têtes communs
lib/             ressources (polices/images), icones, lvgl-9.6.0 patchée + bibliothèques amont
simu/            simulateur PC (Makefile, scénario, données MQTT relevées et anonymisées)
src/ecran/       firmware : Wi-Fi, table ECRANS[], OTA, banc distant, diagnostics
src/ui_lvgl/     les 18 pages LVGL + shell, graphes, MQTT, caméra, musique…
tools/           banc PC, convertisseurs d'icônes/polices, guetteur MQTT
CONTRAT_MQTT.md  contrat MQTT complet (la référence)
```
