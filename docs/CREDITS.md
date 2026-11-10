# Licence et crédits

**English** | [Français](en/CREDITS.md)

[← Retour au README](../README.fr.md)


Code de ce dépôt : **MIT** ([LICENSE](../LICENSE)). Exception : les fichiers 3D de `materiel/` ont leur propre licence, indiquée dans leur dossier. Bibliothèques incluses dans `lib/` :

| Bibliothèque | Licence |
|---|---|
| [LVGL 9.6.0](https://lvgl.io) (vendorisée, patchée) | MIT |
| [LovyanGFX 1.2.19](https://github.com/lovyan03/LovyanGFX) (vendorisée, modifiée : double tampon de l'écran, [patch](patches/lovyangfx_double_tampon_1.2.19.patch) ; polices CJK retirées) | FreeBSD 2 clauses (lu dans `license.txt`) |
| [ArduinoJson 7.4.2](https://arduinojson.org) | MIT |
| [PubSubClient](https://pubsubclient.knolleary.net) | MIT |
| [ESP32Ping](https://github.com/marian-craciunescu/ESP32Ping) | LGPL 2.1 |
| [JPEGDEC 1.8.4](https://github.com/bitbank2/JPEGDEC) (**simulateur PC seulement**, exclu du firmware) | Apache 2.0 |
| [HLK-LD2410S](https://github.com/phuongnamzz/HLK-LD2410S) (driver radar modifié : [pourquoi](../materiel/ecran-batterie/README.md#la-bibliothèque-du-radar-modifiée)) | MIT |

Fournis par le framework (non inclus dans le dépôt, téléchargés à la compilation) :

| Composant | Licence |
|---|---|
| [Arduino-ESP32 3.x](https://github.com/espressif/arduino-esp32) (via [pioarduino](https://github.com/pioarduino/platform-espressif32)) | LGPL 2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache 2.0 |
| [esp_new_jpeg](https://components.espressif.com/components/espressif/esp_new_jpeg) (décodeur JPEG de l'écran : caméra et vignettes) | ESPRESSIF MIT (usage sur produits Espressif) |

Polices et icônes : les polices sources — [Montserrat](https://fonts.google.com/specimen/Montserrat)
(SIL OFL 1.1), [FontAwesome 5 Free](https://fontawesome.com) (OFL, icônes CC BY 4.0) — ne sont **pas
redistribuées** dans le dépôt : voir [tools/fonts/README.md](../tools/fonts/README.md) pour les récupérer et
régénérer les polices bitmap (`lib/ressources/*.c`, dans le dépôt). La police pixel « 3x5 » de Keith Bates /
[K-Type](https://www.k-type.com/fonts/3x5/), visible sur les captures, est gratuite pour un usage personnel
seulement ([conditions](https://www.k-type.com/licences/)) : elle n'est **ni fournie ni générée** ici, c'est
l'option `POLICE_3X5`. Sources
d'icônes [Material Design Icons](https://pictogrammers.com/library/mdi/) (Apache 2.0) dans
`tools/mdi/`. Les images de démonstration du simulateur (caméras, pochettes, logos) sont des mires
générées, libres de droit.

Développé avec l'aide d'assistants IA : Claude (Anthropic) et Qwen (exécuté en local).
