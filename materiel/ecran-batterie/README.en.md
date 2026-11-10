# Battery-powered display

**English** | [Français](README.md)

A variant of the Guition ESP32-S3-4848S040 display that runs **wire-free**: a lithium battery,
a presence radar that wakes the display when someone approaches, and firmware that spends most of
its time in deep sleep. It is the same firmware as the wall displays, with the “Battery & Sensor”
page on top.

![The four case parts in the slicer](apercu_boitier.jpg)

*Page being written: battery, charge module and amplifier references, and photos to come.*

## Hardware

| Item | Detail |
|---|---|
| Display | Guition ESP32-S3-4848S040 (the same as the wall displays) |
| Voltage measurement | 100 kΩ / 47 kΩ voltage divider on GPIO 1 (ADC) ([ui_batterie_esp.cpp](../../src/ui_lvgl/ui_batterie_esp.cpp)) |
| Presence detection | 24 GHz **HLK-LD2410S** radar: OUT output on GPIO 2 (wakes the ESP32-S3 from deep sleep, ext0 wake); serial port on GPIO 41 (the ESP32's RX) and 40 (TX), 115,200 baud, for measurements and settings |
| Case | designed from A to Z, 4 parts printed in 3D: see [Case](#case) |

## Board modifications

- **The board's audio amplifier disconnected.**
- **Voltage divider** added to read the battery voltage on GPIO 1.
- **LD2410S radar** wired: OUT output on GPIO 2 to wake the display, serial port on GPIO 40 and 41.

## Measured consumption

Measurements made with a lab power supply, set to the battery voltage.

| State | Current | Conditions |
|---|---|---|
| Deep sleep | **1.86 mA** at 3.7 V | radar active (one report every 2 s); measured with the old firmware |
| Awake | **~70 mA** | ESP32-S3 at 80 MHz, backlight **off** |
| LD2410S radar alone | 68 µA at 0.5 Hz, 635 µA at 8 Hz | values from the manufacturer's manual (V1.04), not measured here |

## What the firmware does to save power

- **Deep sleep** as soon as nobody is around, after the configured sleep delay: the panel goes into
  deep sleep (the ST7701's DSTB command), and the radar wakes it (GPIO 2).
- **Short boot** on wake: no startup screen, Wi-Fi endpoint and channel kept in RTC memory to
  reconnect without scanning. Measured: **0.69 to 0.72 s** until the screen is ready, against 1.39 s
  with the old firmware (before the move to LVGL).
- **Economy mode** after 10 s without touching: idle brightness, processor at 80 MHz, Wi-Fi in
  light power save (`WIFI_PS_MIN_MODEM`).
- **Wi-Fi off after synchronization** (option): the radio turns off once the data has been received
  from Home Assistant, and turns back on at the first touch.
- **Adjustable Wi-Fi transmit power** (option: 7 dBm, less current, less range).
- Tried without gain: `WIFI_PS_MAX_MODEM` (all the traffic is caught up at once on the radio's wake).

The **Battery & Sensor** page shows the charge, the wake-up history over 24 h (radar, touch,
timer) and the detail of the last wake-up, in milliseconds: Wi-Fi, MQTT, synchronization, cut-off.
The voltage is smoothed over 64 readings (the 4 extremes discarded); the charge is inferred from
the voltage (above 4.15 V).

## The radar library, modified

The [HLK-LD2410S](https://github.com/phuongnamzz/HLK-LD2410S) library is included in
[lib/HLK-LD2410S](../../lib/HLK-LD2410S), with these modifications (dated in comments in the code):

- **Compilation** with ESP-IDF 5: a `return` was missing.
- **Reliable commands**: the frame goes out in a single write (it used to go out in 19 pieces), and
  the reply is searched for by its header and the echo of the command, as in ESPHome. Before, it was
  read as a fixed-size block at the start of the buffer: a measurement frame arriving just before
  shifted it, and the switch to configuration missed on the first try. Up to 3 retries if the sensor
  does not answer.
- **Thresholds of the sensor's firmware 1.1.1**: 16 trigger thresholds and 16 hold thresholds,
  following its 2024 protocol (the library followed a 2023 manual).
- **Per-zone energy in dB**, like the Hi-Link tool, to compare it directly with the thresholds.
- **Automatic tuning**: it no longer waits indefinitely when the sensor stops sending its progress.
- **Measurement points** (raw frames, acknowledged commands, counters) for the log and the radar's
  web page.

## Case

Designed from A to Z in Fusion, for the display with its battery and the radar. Four parts, in STL
format (on GitHub, open a file: it displays in 3D):

| Part | File | Dimensions |
|---|---|---|
| Bottom case | [modeles/boitier_inferieur.stl](modeles/boitier_inferieur.stl) | 87.1 × 87.5 × 25.0 mm |
| Display back | [modeles/dos_ecran.stl](modeles/dos_ecran.stl) | 86.9 × 87.3 × 7.7 mm |
| Cover | [modeles/couvercle.stl](modeles/couvercle.stl) | 82.5 × 82.9 × 3.0 mm |
| Display pivot | [modeles/pivot_ecran.stl](modeles/pivot_ecran.stl) | 18.0 × 13.1 × 12.3 mm |

**License of the 3D files:** [Creative Commons BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/)
— free to print, modify and sharing with attribution, **no commercial use**. These files are not
covered by the code's MIT license (see [LICENSE](LICENSE)).
