# 3D Printer page — what you need, what to enable

**English** | [Français](../IMPRIMANTE.md)

The 3D Printer page (printer icon in the rail) shows the state of a **Klipper** printer driven by
**Moonraker**, relayed by Home Assistant (contract: [CONTRAT_MQTT.en.md](../../CONTRAT_MQTT.en.md),
§ 3D Printer). Four tabs: **Status** (progress ring around the slicer thumbnail, temperatures, fine
particulate badge), **Settings** (preheat Off/PLA/PETG/ABS, nozzle/bed/enclosure targets,
Light/Filtration/Heating/Mainboard switches, homing), **Files** (the 12 most recent, with thumbnail,
material, duration — a tap opens the preview and “Start print”), **Power** (turn on, turn off, pause,
stop with on-the-spot confirmation).

## What you need

- a printer running **Klipper** with **Moonraker** (Mainsail or Fluidd doesn’t matter: they aren’t
  involved here — Moonraker is what talks to the broker);
- the **same MQTT broker** as the screens (local 1883, no TLS, like the rest of the project);
- **Home Assistant** with the proposed automations (below);
- a **slicer that writes 48×48 and 300×300 PNG thumbnails** into the G-code (OrcaSlicer does this out
  of the box on Klipper profiles; settings below);
- **optional**: one or two cameras as **MJPEG streams** (the author uses go2rtc on a NAS).

## 1. On the printer: enable MQTT in Moonraker

In `moonraker.conf`, a `[mqtt]` section. Minimal example; options checked against the
[Moonraker documentation](https://moonraker.readthedocs.io/en/latest/configuration/#mqtt):

```ini
[mqtt]
address: 192.168.1.10        # your broker (the one Home Assistant uses)
port: 1883
instance_name: MY_PRINTER    # prefix of every topic; the author’s was CR10SPRO
publish_split_status: True   # one topic per value: {instance}/klipper/state/{object}/{field}
status_objects:
  print_stats
  display_status=progress
  extruder=temperature,target
  heater_bed=temperature,target
  heater_generic Chauffage=temperature,target
  output_pin ENCLOSURE_LIGHT=value
  output_pin ALIM_CHAUFFAGE=value
  controller_fan FILTRATION_CAISSON=speed
```

- `instance_name`: **“CR10SPRO” appears throughout the proposed YAML files** — it was the author’s
  instance name. Use yours, and replace `CR10SPRO` in the three YAML files (sensor topics, command
  automation topic).
- `status_objects`: only listed objects get published (the `object=field,field` syntax comes from the
  docs). `Chauffage`, `ENCLOSURE_LIGHT`, `ALIM_CHAUFFAGE`, `FILTRATION_CAISSON` are names from the
  author’s `printer.cfg`: use your own object names, or drop those lines.
- With `publish_split_status: True`, each value goes to its own topic
  (`{instance}/klipper/state/{object}/{field}`, payload `{"eventtime":…, "value":…}`) — that’s what
  the HA sensor listens to. The default (`False`) publishes everything on a single
  `{instance}/klipper/status` topic, which the proposed YAML does not handle.
- Moonraker also publishes its own status, retained: `{instance}/moonraker/status`
  (`{"server":"online"}` on connect, `{"server":"offline"}` via its last-will message —
  [mqtt component source](https://github.com/Arksine/moonraker/blob/master/moonraker/components/mqtt.py)).
  That’s what tells you the printer is “on” from the software side.
- `enable_moonraker_api` (default `True`): JSON-RPC commands come in through
  `{instance}/moonraker/api/request`.

Restart Moonraker; check its log (`[mqtt] connecting…`) and your broker
(`mosquitto_sub -t 'MY_PRINTER/#'`).

## 2. On Home Assistant: the three YAML files

In [ha_propositions/](../../ha_propositions/), three files (adopt them after reading — replace
`CR10SPRO`, the `192.168.1.30` IPs, and the example entities with your own):

1. [imprimante_configuration.yaml](../../ha_propositions/imprimante_configuration.yaml) — goes in
   `configuration.yaml`: a single template sensor (`sensor.ecran_etat_klipper`) listening to
   `{instance}/klipper/state/#` + `{instance}/moonraker/status`, keeping the useful values in an
   attribute (temperatures update every second: excluded from the recorder).
2. [imprimante_dashboard_guition.yaml](../../ha_propositions/imprimante_dashboard_guition.yaml) —
   goes in `automations.yaml`: “Écran · imprimante · état” (publishes
   `dashboard_guition/imprimante/etat`, retained) and “Écran · imprimante · commandes” (receives
   `dashboard_guition/imprimante/set`, with bounded actions).
3. [imprimante_fichiers.yaml](../../ha_propositions/imprimante_fichiers.yaml) — the file list: a
   `rest_command` to Moonraker (`/server/files/list`, `/server/files/metadata`), then one retained
   topic per file (`imprimante/fichiers/0` to `/11`, 0 = most recent).

You also need **two HA scripts you write yourself**: `script.allumer_imprimante` and
`script.eteindre_imprimante`. The author’s drive a smart plug (a Zigbee relay cutting the machine’s
power); “turn off” = clean Klipper shutdown first, then cut the plug. Without these scripts, the
Power tab’s Turn on/Turn off buttons do nothing.

## 3. Thumbnails: two sizes, generated by the slicer

The firmware never draws a thumbnail: it downloads the PNGs the **slicer** embedded in the G-code and
that **Moonraker extracts** into `gcodes/.thumbs/<file without extension>-48x48.png` and
`-300x300.png`. The screen builds the URL itself (base published by the files YAML + URL-encoded file
name + `-48x48.png` for the list, `-300x300.png` for the ring, downscaled when downloading).

Set the sizes in your slicer:

- **OrcaSlicer**: the `thumbnails` setting (Printer Settings, advanced mode), value
  `48x48/PNG,300x300/PNG` — see the [OrcaSlicer wiki, Advanced Printer
  Settings](https://www.orcaslicer.com/wiki/printer_settings/basic%20information/printer_basic_information_advanced.html)
  (Klipper profiles already include it: the author’s “MyKlipper 0.4 nozzle” produced two on 23/10).
- **PrusaSlicer / SuperSlicer**: the printer profile’s “G-code thumbnails” field, same value
  `48x48/PNG, 300x300/PNG` — the [Fluidd docs
  summary](https://docs.fluidd.xyz/features/printing/) lists the exact menu paths per version.

Without thumbnails at those sizes, the list and the ring show “no thumbnail” — everything else works.

## 4. Cameras (optional)

The two views (enclosure, bed) are **MJPEG streams** — at the author’s, go2rtc on the NAS (cropping
and sub-streams prepared there, ~384×304; the rate depends on each setup). The URLs live in
[src/ui_lvgl/ui_camera.h](../../src/ui_lvgl/ui_camera.h) (`UI_CAM_IMPRIMANTE`, `UI_CAM_PLATEAU`,
overridable with `-D` at compile time): adapt host, port and `src=`, then rebuild. With no camera (or
a wrong URL), the corner button still cycles STL → enclosure → bed views; the zone simply stays empty
(`[camera]` traces go to serial/telnet, nothing is displayed).

## 5. How a command travels

The screen publishes to `dashboard_guition/imprimante/set`; HA validates the action against the known
list and forwards it:

| Button | What goes out |
|---|---|
| Turn on / Turn off | `script.allumer_imprimante` / `script.eteindre_imprimante` (yours to write) |
| Pause / Resume / Stop | `printer.print.pause` / `resume` / `cancel` (Moonraker’s MQTT API) |
| Start (file preview) | `printer.print.start` + file name |
| Preheats, ± targets | `printer.gcode.script`: M104 (nozzle), M140 (bed), M141 (enclosure), bounded |
| Light / Heating / Filtration | `SET_PIN PIN=ENCLOSURE_LIGHT`, `SET_PIN PIN=ALIM_CHAUFFAGE`, `SET_FAN_SPEED FAN=FILTRATION_CAISSON` (names from your `printer.cfg`) |
| Mainboard | `switch.turn_on/off` on your plug (e.g. `switch.prise_zigbee_1`) |
| Home all axes | `printer.gcode.script` G28 (button disabled when the printer is off or printing) |

Everything goes through `{instance}/moonraker/api/request` (JSON-RPC), as described in the
[Moonraker docs](https://moonraker.readthedocs.io/en/latest/configuration/#mqtt).

## 6. What depends on your machine — and what the page shows without it

| Dependency | Without it, the page shows… (read in the code) |
|---|---|
| Enclosure object (`heater_generic Chauffage`) | Enclosure row at 0° (YAML default) |
| `output_pin ENCLOSURE_LIGHT` / `ALIM_CHAUFFAGE`, `controller_fan FILTRATION_CAISSON` | Light/Heating/Filtration switches shown, off; tapping them sends a `SET_PIN`/`SET_FAN_SPEED` Klipper rejects (silent at the screen, an error in Moonraker’s log) |
| Plug power sensor (`sensor.prise_imprimante_puissance` in the example YAML) | “--” instead of watts (Power tab) |
| Particulate sensor (`sensor.aqm_pm1`) | “--” on the PM badge in Status |
| Mainboard plug (`switch.prise_zigbee_1`) | mainboard field at 0; replace with your entity |
| Slicer thumbnails | “no thumbnail” (list and ring) |
| Cameras | camera zone stays empty, see § 4 |

## 7. No printer at all

The page **does not hide itself**: the rail is a fixed page list
([src/ecran/ecran.cpp](../../src/ecran/ecran.cpp), `pages[]` table). With nothing received, the
default state is “off”: Status shows “Printer off”, the Power tab shows “Off” with a “Turn printer on”
button — which publishes a command nobody executes unless you installed the automations. To remove
the page from the rail, remove its line in `pages[]` and rebuild (the simulator, on the other hand,
can visit any page).

## See also

- [CONTRAT_MQTT.en.md](../../CONTRAT_MQTT.en.md) § 3D Printer — exact message formats.
- [docs/en/INSTALLATION.md](INSTALLATION.md) § 4 — HA automations in general.
- Simulator: `make -C simu` captures the tabs (`simu/captures/45_imprimante.png`,
  `47_imprimante_reglages.png`, `48b_imprimante_apercu.png`).
