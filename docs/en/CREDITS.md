# License and credits

**English** | [Français](../CREDITS.md)

[← Back to README](../../README.md)


Code in this repository: **MIT** ([LICENSE](../../LICENSE)). Exception: the 3D files in `materiel/` have their own license, stated in their folder. Libraries included in `lib/`:

| Library | License |
|---|---|
| [LVGL 9.6.0](https://lvgl.io) (vendored, patched) | MIT |
| [LovyanGFX 1.2.19](https://github.com/lovyan03/LovyanGFX) (vendored, modified: double screen buffering, [patch](../patches/lovyangfx_double_tampon_1.2.19.patch); CJK fonts removed) | FreeBSD 2-clause (read in `license.txt`) |
| [ArduinoJson 7.4.2](https://arduinojson.org) | MIT |
| [PubSubClient](https://pubsubclient.knolleary.net) | MIT |
| [ESP32Ping](https://github.com/marian-craciunescu/ESP32Ping) | LGPL 2.1 |
| [JPEGDEC 1.8.4](https://github.com/bitbank2/JPEGDEC) (**PC simulator only**, excluded from the firmware) | Apache 2.0 |
| [HLK-LD2410S](https://github.com/phuongnamzz/HLK-LD2410S) (modified radar driver: [why](../../materiel/ecran-batterie/README.en.md#the-radar-library-modified)) | MIT |

Provided by the framework (not included in the repository, downloaded at build time):

| Component | License |
|---|---|
| [Arduino-ESP32 3.x](https://github.com/espressif/arduino-esp32) (via [pioarduino](https://github.com/pioarduino/platform-espressif32)) | LGPL 2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache 2.0 |
| [esp_new_jpeg](https://components.espressif.com/components/espressif/esp_new_jpeg) (the screen's JPEG decoder: camera and thumbnails) | ESPRESSIF MIT (use on Espressif products) |

Fonts and icons: the source fonts — [Montserrat](https://fonts.google.com/specimen/Montserrat)
(SIL OFL 1.1), [FontAwesome 5 Free](https://fontawesome.com) (OFL, icons CC BY 4.0) — are **not
redistributed** in the repository: see [tools/fonts/README.en.md](../../tools/fonts/README.en.md) to fetch them
and regenerate the bitmap fonts (`lib/ressources/*.c`, in the repository). The “3x5” pixel font by Keith
Bates / [K-Type](https://www.k-type.com/fonts/3x5/), visible in the captures, is free for personal use
only ([terms](https://www.k-type.com/licences/)): it is **neither provided nor generated** here, it is
the `POLICE_3X5` option. Icon sources
[Material Design Icons](https://pictogrammers.com/library/mdi/) (Apache 2.0) in
`tools/mdi/`. The simulator's demo images (cameras, album art, logos) are generated test
patterns, free of rights.

Developed with the help of AI assistants: Claude (Anthropic) and Qwen (run locally).
