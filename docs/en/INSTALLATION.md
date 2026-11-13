# Installation

**English** | [Français](../INSTALLATION.md)

[← Back to README](../../README.md)

Everything needed to go from a cloned repository to a screen that talks to Home Assistant.

## Prerequisites

- [PlatformIO Core](https://platformio.org/install) (`pio` CLI).
- A Linux PC with `g++` and `zlib` for the simulator; `python3` (and `pillow` for `tools/banc.py`).
- An MQTT broker and Home Assistant with the MQTT integration.

## 1. Set up the secrets

The secrets files are **not** in the repository — copy the templates and fill them in:

```bash
cp src/secrets_mqtt.h.exemple  src/secrets_mqtt.h          # broker, user, password
cp src/secrets_ma.h.exemple    src/secrets_ma.h            # Music Assistant server and token
cp src/ecran/secrets_wifi.h.exemple src/ecran/secrets_wifi.h  # SSID, password, local IP plan
```

## 2. Declare your screens

In [src/ecran/ecran.cpp](../../src/ecran/ecran.cpp), the `ECRANS[]` table maps each screen to its MAC:

```c
static const ecran_t ECRANS[] = {
    // { MAC (placeholder), last IP octet, room, rotation, battery }
    {{0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x01}, 10, "cuisine",  0, false},
    {{0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x02}, 11, "sejour",   0, true },
    // … replace the MACs with those of your screens — the [ecran] trace on first boot prints them (pio device monitor)
};
```

The addresses in the repository are **placeholders**: put in your MACs, and the static IP is built
from `RESEAU_OCTET_1/2/3` + the declared last octet.

## 3. Build and flash

```bash
pio device list                                             # find your serial port (ttyACM0 or ttyUSB0 depending on the cable)
pio run -e ecran-usb -t upload --upload-port /dev/ttyACM0    # first flash: over cable (or /dev/ttyUSB0)
pio run -e ecran -t upload --upload-port 192.168.1.41        # then: OTA, no cable
pio device monitor                                          # serial log, 115,200 baud
```

OTA goes through `ArduinoOTA` (the repository's `espota` upload protocol, 15 s timeout): once the
first flash is done over cable, there is no need to open the screen again. The `banc-chambre1-v9`
environment is reserved for rendering measurements — do not flash it without knowing why.

**Language**: the interface is in French by default. For English, uncomment `-D UI_LANG_EN` in
`platformio.ini` (in the simulator: `make -C simu UI_LANG_EN=1`). The MQTT dialogue with Home Assistant
does not change: only the displayed texts are translated.

## Settings: 64 Ko of the spiffs partition is reserved for them

Since 13 Nov 2026 the display's settings (brightness, sleep timer, radar, battery) no longer live in
NVS but in **raw flash memory**: the last 16 sectors (64 Ko) of the `spiffs` partition are reserved
for them. That partition has been unused since the move to LVGL: if it contains anything else (old
LittleFS files), it is erased on the first boot. Why this detour: a normal NVS write (about fifteen
flash operations, 3.5 to 6 ms) made the image **glitch at 16-17 MHz — seen by eye — while a raw write
(32 bytes, 0.2 ms) or erasing a sector (34 ms) does not glitch it; the exact cause inside NVS was
never found.** NVS is still used, for Wi-Fi and radio calibration only.

## Demo mode: try it without Home Assistant

Copy the three secrets templates (step 1) **without filling them in**, uncomment `-D MODE_DEMO` in
`platformio.ini` and flash over cable (`ecran-usb`): no screen to declare. The screen looks for neither
Wi-Fi nor MQTT; at startup, it replays an embedded set of MQTT
messages ([src/ui_lvgl/ui_demo.cpp](../../src/ui_lvgl/ui_demo.cpp)) through the same reading code as with
Home Assistant, and every page fills up (time fixed at 15:30). The heating, ventilation, lights, covers,
hot water, stove and coffee buttons respond as if Home Assistant returned the state; music responds to a
fake Music Assistant. Still empty: cameras, album art and radio logos (they arrive over the network). In
the simulator: `make -C simu MODE_DEMO=1`.

## 4. Home Assistant

The full contract (topics, JSON, conventions) is in [CONTRAT_MQTT.en.md](../../CONTRAT_MQTT.en.md):
two automations per page (« Écran · page · état », which publishes `etat` with retain, and « Écran ·
page · commandes », which executes the `set`). [ha_propositions/](../../ha_propositions/) contains **24
ready-to-use automations** for HA (mine, adapted: replace the example entities with your own). The Music
page does not go through MQTT: the screen talks directly to **Music Assistant** (HTTP + token).

## 5. PC simulator

```bash
make -C simu                 # compiles the ACTUAL page code, runs the scenario, writes simu/captures/*.png
make -C simu asan            # same pages under AddressSanitizer / UBSan
python3 simu/galerie.py galerie.html   # assembles the captures into a browsable HTML page
```

It replays the MQTT messages from an anonymized real-world capture and produces about 280 captures,
including the images on this page. The detail is in
[docs/en/DEVELOPPEMENT.md](DEVELOPPEMENT.md#simulator).
