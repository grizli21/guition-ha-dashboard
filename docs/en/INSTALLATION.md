# Installation

**English** | [Français](../INSTALLATION.md)

[← Back to README](../../README.md)

Everything needed to go from a cloned repository to a screen that talks to Home Assistant.

## Prerequisites

- [PlatformIO Core](https://platformio.org/install) (`pio` CLI).
- A Linux PC with `g++` and `zlib` for the simulator; `python3` (and `pillow` for `tools/banc.py`).
- An MQTT broker and Home Assistant with the MQTT integration.

### Before the first build: pin `pioarduino` to 6.2.0

The platform (pioarduino 55.03.312) creates its own Python environment, `~/.platformio/penv`, and installs
the latest `pioarduino` package in it. Since 26 Oct 2026 that is **6.2.0.post1**, which the same platform
then rejects (“not compatible with PlatformIO Core v6.2.0-post.1 and depends on PlatformIO Core >=6.2.0”):
the build fails after about ten minutes, right after the ESP-IDF libraries are recompiled (seen by the CI on
13 Nov 2026). Workaround: create that environment **before** the first build, with 6.2.0 — the platform keeps
an already installed package when it fits.

Linux / macOS:

```bash
python3 -m venv ~/.platformio/penv
~/.platformio/penv/bin/python -m pip install "pioarduino==6.2.0" "platformio==6.2.0"
```

Windows (PowerShell):

```powershell
py -m venv "$env:USERPROFILE\.platformio\penv"
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install "pioarduino==6.2.0" "platformio==6.2.0"
```

If the VS Code extension has already created `penv`, keep it and run only the second command (it brings
`pioarduino` back to 6.2.0). Then use the `pio` from that environment (`~/.platformio/penv/bin/pio`, or
`...\penv\Scripts\pio.exe`), or the VS Code extension's, which is the same one.

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

## On Windows 10/11

**What is verified here: the build chain is exercised by the CI workflow
(`.github/workflows/compilation.yml`, which builds the `ecran` environment on `windows-latest`).
Everything else — USB/OTA flashing, the driver, demo mode seen from a real Windows machine — has not
been run on Windows yet: to be validated in real conditions.**

### Tooling

1. [Git for Windows](https://git-scm.com/install/windows). Its installer sets `core.autocrlf=true`:
   the repository ships a `.gitattributes` that keeps scripts, the Makefile and the simulator data
   files LF — don't delete it, or `make -C simu` breaks under WSL.
2. [Python 3](https://www.python.org/downloads/windows/) (3.11 or newer) — the installer defaults are fine.
3. PlatformIO, your choice:
   - **VS Code**: the “PlatformIO IDE” extension (ant logo in the status bar, then its terminal);
   - **command line**: PlatformIO's Python environment created as in
     [Before the first build](#before-the-first-build-pin-pioarduino-to-620), then its `pio` wherever this
     page says `pio` (that is what the CI workflow does). With the VS Code extension, do that step too:
     without it, the first build fails.
4. **The terminal**: run `pio` from **PowerShell** or the VS Code terminal, **never from Git Bash** (MSYS):
   the ESP-IDF tooling stops there with “MSys/Mingw is not supported” (seen by the CI on 13 Nov 2026).

### Long paths — pitfall no. 1

The `ecran` environment recompiles the ESP-IDF libraries (`custom_sdkconfig` in `platformio.ini`) and
their paths quickly exceed the **260 characters** (`MAX_PATH`) Windows enforces by default: the build
fails halfway through. In this order:

- **clone near the root**: `git clone … C:\gd` — every saved folder counts;
- **enable long paths**: set `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled`
  to `1` (PowerShell command in Microsoft's doc:
  [Maximum Path Length Limitation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation);
  a reboot is recommended, the value is cached per process);
- **tell git**: `git config --system core.longpaths true` (the repository has deep paths under `lib/`).

The ESP-IDF tooling even prints an explicit “Windows Long Path Support is disabled” warning when the
key is missing (field report: [PlatformIO community](https://community.platformio.org/t/build-inconsistency-despite-pinned-versions-esp32-s3-arduino-framework/52949)).

### Build and flash

Same as Linux, with Windows ports:

```bat
pio device list                                           (or Device Manager > “Ports (COM & LPT)”)
pio run -e ecran-usb -t upload --upload-port COM5         first flash, over cable
pio run -e ecran -t upload --upload-port 192.168.1.41     afterwards, OTA without a cable
```

If no COM port shows up after plugging in: the USB-serial adapter on these boards is usually a
**CH340/CH341** (check the part number printed on your board); install the official CH341SER driver
from the manufacturer, [WCH](https://www.wch-ic.com/downloads/CH341SER_EXE.html).

Reminder that applies everywhere: the `ecran` environment **requires** an `upload_port`
(`upload_port = 192.168.1.43` in `platformio.ini`) even for a plain compile — without it PlatformIO
fails while recompiling the libraries. A compile never tries to send anything.

**Demo mode**: exactly as described below (uncomment `-D MODE_DEMO`, flash over cable).

### Simulator: through WSL, not natively

The simulator is written for Linux (g++, Makefile). On Windows 10/11:

```bat
wsl --install -d Ubuntu
```

then, inside Ubuntu — preferably on the Linux disk (`~`), not under `/mnt/c` (slow):

```bash
sudo apt-get update && sudo apt-get install -y build-essential zlib1g-dev git
git clone https://github.com/grizli21/guition-ha-dashboard.git && cd guition-ha-dashboard
make -C simu        # writes simu/captures/*.png, no graphical display needed
```

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
The 3D Printer page additionally needs setup on the printer side (Klipper + Moonraker with MQTT, slicer
thumbnails): see [IMPRIMANTE.md](IMPRIMANTE.md).

## 5. PC simulator

```bash
make -C simu                 # compiles the ACTUAL page code, runs the scenario, writes simu/captures/*.png
make -C simu asan            # same pages under AddressSanitizer / UBSan
python3 simu/galerie.py galerie.html   # assembles the captures into a browsable HTML page
```

It replays the MQTT messages from an anonymized real-world capture and produces about 280 captures,
including the images on this page. The detail is in
[docs/en/DEVELOPPEMENT.md](DEVELOPPEMENT.md#simulator).
