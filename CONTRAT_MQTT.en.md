# MQTT contract for the wall screens (new LVGL interface)

**English** | [Français](CONTRAT_MQTT.md)

MQTT contract reference, first written on 22 Oct 2026, kept in step with the code.
The screen code (`src/ui_lvgl/`) and the HA automations follow this document: it is updated first, then the code.

## 1. Principles

1. **The screen only talks to HA, over MQTT.** No more direct access to devices (Moonraker, moOde's MPD, ESPEasy, WLED…).
   The only exceptions, because they are heavy images loaded over HTTP: the camera stream, the printer's thumbnails,
   Music Assistant covers and logos (addresses provided in the state, by HA).
2. **One single shape for all pages**:
   - `dashboard_guition/<page>/etat`: HA → screen, **retained** JSON, republished on every change;
   - `dashboard_guition/<page>/set`: screen → HA, JSON `{"action": "...", ...}`, not retained.
   - Lists: one subtopic per element (`dashboard_guition/lumieres/<id>/etat`, `dashboard_guition/thermostats/<piece>/etat`).
3. **What concerns one specific screen**: `dashboard_guition/appareil/<nom>/...` (nom = `cuisine`, `sejour`…).
4. **On the HA side, two automations per page** with the same template: « Écran · <page> · état » and « Écran · <page> · commandes ».
5. **Naming**: keys in French, lowercase, no accents, units in the document (not in the key); booleans as `0` / `1`;
   times as **minutes since midnight**; temperatures in °C (decimal number); percentages as integers.
6. **`dashboard_guition/` prefix** (22/10): new, it doesn't disturb the old `dashboard/`, still in service for
   screens on the old firmware. Both run in parallel, pages are switched over one by one, then `dashboard/` is deleted (see §5).

## 2. Current state (surveyed on 22/10: broker retained messages + `automations.yaml` + `mqtt_fonctions.h`)

| Current topic | Published by | Read by | Finding | Decision |
|---|---|---|---|---|
| `dashboard/thermostats/<piece>/etat` | HA (automation 1791000000001) | firmware | good; `mode` in English (`Off`) | → `dashboard_guition/thermostats/<piece>/etat` |
| `thermostat/tele/<piece>` | HA (same automation) | old firmware | **exact duplicate** of the previous one | delete after migration |
| `thermostat/tele/command` | firmware + HA | both | "resynchro" from the Jeedom era | delete (retained is enough) |
| `thermostat/tele/infos1`, `infos2` | HA (1791000000003) | old firmware | duplicate of `dashboard/infos/etat` | delete |
| `thermostat/tele/aqm` | nobody (last: 09/2026) | old firmware | **obsolete** (AQM offline) | delete |
| `dashboard/infos/etat` | HA (1791000000150) | firmware | catch-all: outdoor, power, water, presence, phase, **and** the Xiaomi | split (§3 Home, Air) |
| `dashboard/ballon/etat`, `/set` | HA (179 / 173) | firmware | good; `eco_state` `"ECO"`, `relay_state` | → `dashboard_guition/ballon/...` |
| `dashboard/vmc/etat`, `/set` | HA (188 / 191) | firmware | good | → `dashboard_guition/vmc/...` |
| `dashboard/poele/etat`, `/set` | HA (130 / 131) | firmware | good | → `dashboard_guition/poele/...` |
| `RegPID_poele/#` | nobody (retained "Connection Lost") | old firmware | **dead** since the stove moved to ESPHome | delete + clear the retained message |
| `dashboard/croquettes/etat`, `/commande` | HA (160 / 161) | firmware | good, but `/commande` instead of `/set`; wrong stats (595 g) | → `dashboard_guition/croquettes/...` |
| `Distributeur_croquettes/#` | nobody (ESPEasy gone) | old firmware | **stale retained messages** (879 g, "Connection Lost") | delete + clear the retained messages |
| `dashboard/lumieres/<id>/etat`, `/set` | HA (181 / 182) | firmware | good; old ids (`hektar_haute`, 2 of 3 floor lamps, no Lidl or panel) | → `dashboard_guition/lumieres/...` revised ids |
| `wled/...` | WLED | old firmware | direct access | stop reading |
| `dashboard/espurifier/etat`, `/set` | HA (187) | firmware | good, very detailed | → `dashboard_guition/air/atelier/...` |
| `dashboard/purificateur/etat`, `/set` | HA (183) | firmware | the living room's Xiaomi | → `dashboard_guition/air/sejour/...` |
| `dashboard/imprimante/etat`, `/set` | HA (189) | firmware | smart plug and mainboard only | → `dashboard_guition/imprimante/...` completed |
| `CR10SPRO/klipper/state/...` | Moonraker (direct) | firmware | direct access to the printer | HA relays it into `dashboard_guition/imprimante/etat` |
| `dashboard/moode/etat`, `/set` | HA (185) | firmware | power only; the rest via direct MPD | power → `dashboard_guition/...` (to do); the rest direct to the Music Assistant API (24/10) |
| `dashboard/ampli/set` | firmware | HA (190) | Onkyo RI codes | → `dashboard_guition/musique/set` action `ampli` |
| `dashboard/store/set`, `dashboard/rideau/set` | firmware | HA (184) | commands with no published state | → `dashboard_guition/ouvrants/...` with state |
| `dashboard/cafe/set` | firmware | HA (186) | scheduled coffee (time) | → `dashboard_guition/cafe/...` + kettle |
| `dashboard/carillon/etat` | HA | firmware | — | → `dashboard_guition/carillon/...` |
| doorbell (automation 1791000000040 / 041) | HA | firmware | publishes to a list of topics | → `dashboard_guition/sonnette/etat` |

## 3. New contract, page by page

Legend: **E** = `etat` (HA → screen, retained), **S** = `set` (screen → HA). In parentheses: the original HA entity.

### Home (shared by several pages) — `dashboard_guition/maison/etat`
**E** `{"phase":"jour|nuit", "presence":0|1, "ext_temp":6.8, "ext_hum":87, "maj":1792694287}`
(`sensor.openweathermap_temperature`, `sensor.openweathermap_humidite`, `person.exemple`, `sun.sun`). Used by the thermostat (outdoor),
the chart and the settings (night). Without power consumption or water (energy banner removed): published only on real changes.

### Sky (the AI page scene) — `dashboard_guition/maison/ciel` (12 Nov 2026, automation 1792200000037)
**E** `{"aube":1794550018, "lever":1794552045, "coucher":1794586954, "crepuscule":1794588979, "lune_age":2.9, "lune_pct":9,
"meteo":"pouring", "nuages":100, "pluie":4.2, "neige":0.0}`: `aube`/`lever`/`coucher`/`crepuscule` = Unix timestamps of the
NEXT sun events (`sun.sun` attributes `next_dawn`/`next_rising`/`next_setting`/`next_dusk`; the screen derives today's
events from them and places the sun itself); `lune_age` (days, 0 = new moon) and `lune_pct` (% lit) — the proposed
`ha_propositions/ciel_dashboard_guition.yaml` computes them with an astronomical approximation (synodic month, accurate
to ~1%); `meteo` = `sensor.openweathermap_condition` (sunny, clear-night, partlycloudy, cloudy, fog, rainy,
pouring, snowy, snowy-rainy, lightning, lightning-rainy, hail, windy…); `nuages` in %; `pluie` / `neige` in mm/h.
Published only on changes (retained).

### Heating — `dashboard_guition/thermostats/<piece>/etat` (`cuisine`, `sejour`, `chambre1`, `chambre2`, `sdb`)
**E** `{"temp":14.5, "hum":69.5, "consigne":15.0, "mode":"arret|horsgel|eco|confort|manuel", "chauffe":0|1, "batterie":59, "signal":164}`
(`climate.<piece>`: `preset_mode` away/eco/comfort/none → horsgel/eco/confort/manuel; `hvac_mode off` → arret; `hvac_action heating` → chauffe)
**S** `dashboard_guition/thermostats/<piece>/set` `{"action":"regler", "mode":"eco"}` or `{"action":"regler", "consigne":19.5}`

### Charts — `dashboard_guition/thermostats/<pièce>/historique` and `dashboard_guition/maison/historique` (24/10, IN HA)
Kept by HA (template sensors `sensor.ecran_historique_chauffage_<pièce>` / `sensor.ecran_historique_exterieur`, triggered
every 5 min, restored on restart, excluded from the recorder); published retained IN FULL at every point by 1792200000031
(a lost message shifts nothing, ≈ 2.2 KB per room). 288 points = 24 h, most recent last:
**E** `thermostats/<pièce>/historique` `{"fin":930, "t":[195,…288], "c":[[0,160],[100,195],…], "h":[53,…288], "ch":"000…a06d…"}`:
`fin` = minute of the day of the last point; `t` = °C x10 (-999 = no measurement); `c` = setpoint x10 only at its changes
([index, value], -999 = heating off); `h` = % (0 = no measurement); `ch` = 1 bit per point (heated AT SOME POINT during the 5 min,
seen at each hvac_action change), 36 bytes in hex, bit 0 = point 0.
**E** `maison/historique` `{"fin":930, "ext":[…288]}`: north probe (local), OpenWeatherMap as fallback, °C x10.
Screen: curves in PSRAM, filled as soon as the messages arrive (page displayed or not), right-aligned while HA has less than 24 h.

### Air Quality — `dashboard_guition/air/atelier/etat` (ESPurifier), `dashboard_guition/air/sejour/etat` (Xiaomi)
**E** `{"pm25":2, "info":"COV 72", "modes":["Arrêt","Fan","1","2","3"], "mode":0, "rpm":1250, "en_ligne":1}`
(workshop: `sensor.espurifier_pm2_5`, `select.espurifier_mode`; living room: Xiaomi, `filter_life_remaining` in `info`)
`rpm` (25/10): actual fan speed, rounded to 50 rpm (workshop: `sensor.espurifier_ventilateur_vitesse_de_rotation`,
living room: `sensor.purificateur_motor_speed`): the screen's propeller turns at 1 rev/s per 1 000 rpm, stopped at 0.
**S** `{"action":"mode", "mode":2}`
**E** `dashboard_guition/air/<id>/historique` `{"pas":30, "points":[3,4,5,255,…]}`: 48 points (24 h), most recent last,
255 = no measurement. **Kept by HA** (decision of 22/10: the screen restarts, will sleep in battery mode, and two screens
must show the same curve): half-hour average (`statistics` average_step), republished in full at every point.

### Covers — `dashboard_guition/ouvrants/<id>/etat` (`store_terrasse`, `volet_cuisine`, `toile`)
**E** `{"position":60, "mouvement":"ouvre|ferme|arret", "en_ligne":1}`; the toile also has `"firetv":0|1`
(`cover.store_terrasse` = awning: 100 = extended; `cover.store_cuisine` = IKEA FYRTUR: 100 = raised;
`cover.toile_vp_et_ampli_toile`: 100 = raised; `media_player.fire_tv_sejour`). `mouvement` = HA's opening / closing
states ("up / down" means nothing for an awning, 22/10).
**S** `{"action":"ouvrir|fermer|stop"}` or `{"action":"position", "position":30}`; toile: `{"action":"seance", "on":1}` (turns the Fire TV on / off; automation 1791000000163 drives the toile)

### Feeder — `dashboard_guition/croquettes/etat`
**E** `{"bol":21.0, "stock":66, "autonomie":6, "vide":0, "repas":[{"h":390,"q":15,"a":1},...4], "jours":[{"g":31,"n":2},...7], "jour_semaine":2}`
(`sensor.distributeur_croquettes_*`, `number.distributeur_croquettes_repas_N_*`, `binary_sensor.distributeur_croquettes_plus_de_croquettes`; `jours[0]` = today; `h` in minutes)
**S** `{"action":"repas", "numero":2, "h":765, "q":15, "a":1}`, `{"action":"servir"}`, `{"action":"tare"}`

### Coffee — `dashboard_guition/cafe/etat`
**E** `{"cafe":{"prog":0|1, "h":405, "allumee":0, "marche":0, "en_ligne":1}, "bouilloire":{"prog":0|1, "h":420, "temp":80, "chauffe":0, "en_ligne":1}, "recentes":[405,435]}`
(`input_boolean/input_datetime.cafe_programme*` + `bouilloire_programme*`, `input_number.bouilloire_programme_temp`,
`input_text.cafe_recentes`, `binary_sensor.cuisine_cafetiere_*`, `binary_sensor.bouilloire_en_chauffe`; the schedule
is kept by HA, single occurrence = next occurrence of `h`)
**S** `{"action":"programmer", "appareil":"cafe|bouilloire", "h":405, "on":1, "temp":80}` (`temp`: kettle),
`{"action":"maintenant", "appareil":"cafe"}`, `{"action":"maintenant", "appareil":"bouilloire", "temp":80}`,
`{"action":"arreter", "appareil":"cafe|bouilloire"}`. Coffee started on time by 1791000000186 (existing), kettle by 1792200000017.

### Hot Water — `dashboard_guition/ballon/etat`
**E** `{"temp":45.1, "consigne":50, "mode":"arret|auto|force", "eco":1, "chauffe":0, "chaude_jour":5, "froide_jour":17, "kwh_jour":4.4, "debit":0, "puissance":0, "combles":14}`
(`sensor.ballon_eau_chaude_temperature`, `input_number.ballon_consigne`, `input_select.ballon_mode` OFF/AUTO/FORCE, `input_select.ballon_regime` ECO/NORMAL; `chauffe` = relay `switch.tableau_electrique_chauffe_eau`)
25/10: `chaude_jour` L (`sensor.ballon_eau_chaude_eau_chaude_journalier`), `froide_jour` L = the house's water inlet, ALL the water
(`sensor.arrivee_eau_journalier`, shown as « eau totale »), `kwh_jour` (`sensor.jour_chauffe_eau`), `debit` L/min of hot water
(the tank's ESP), `puissance` W rounded to 50 (the Ecocompteur's toroid, read every 10 s), `combles` °C.
**E** `dashboard_guition/ballon/historique` `{"fin":1090, "t":[…288 tenths of a °C, -999 = no measurement…], "c":[[index, setpoint °C], …],
"l":[[index, litres], …], "ch":"72 hex", "hc":"72 hex"}`: 24 h, one point every 5 min, most recent last; `c` =
setpoint changes (staircase); `l` = litres of hot water at the points where water was drawn (the Ecocompteur's counter, no reset);
`ch` = 1 bit per point (bit j of byte k = point 8k+j): the relay heated; `hc` = same, off-peak hours (téléinfo).
Kept by HA (`sensor.ecran_historique_ballon`, excluded from the recorder), republished in full (≈ 2 KB) by 1792200000033.
**S** `{"action":"regler", "consigne":55}` / `{"action":"regler", "mode":"force"}` / `{"action":"regler", "eco":0}`

### Ventilation — `dashboard_guition/vmc/<zone>/etat` (`maison`, `sous_sol`)
**E** `{"vitesse":0|1|2, "auto":0|1, "hum":82, "hum_lieu":"salle de bain", "seuil":80}` (basement: `seuil` 0 = no automation)
(`fan.vmc_maison` / `fan.vmc_sous_sol` 0/50/100 %, `input_boolean.vmc_maison_forcee`, humidity of the bathroom)
**S** `{"action":"vitesse", "vitesse":2}`

### Stove — `dashboard_guition/poele/etat`
**E** `{"fumees":19, "corps":24.1, "tirage":100, "mode":"auto|manuel|levier", "tirage_manuel":100, "feu":"eteint|allumage|regulation|recharger", "consigne":240, "tirage_min":30, "recharge":180, "kp":3.0, "ki":0.01, "kd":10.0, "limite":30, "brassage":0, "en_ligne":1}`
(`sensor.poele_*`, `select.poele_mode_tirage` Auto / Manuel distance / Manuel local, `number.poele_*`, `binary_sensor.poele_recharger`,
`binary_sensor.poele_regulation_en_marche`). `feu`: recharger > regulation (PID, starts above 100 °C) > ignition
(flue gas rising by more than 3 °C in 5 min, `sensor.poele_fumees_variation_5_min`) > off. `brassage` = the living room's Xiaomi
air purifier on the Fan preset, speed 1-3 (0 = off), confirmed on 23/10.
**E** `dashboard_guition/poele/historique` `{"pas":5, "fumees":[…144…], "corps":[…], "tirage":[…]}`: 12 h (mockup, 23/10),
-1 = no measurement, kept by HA (`sensor.ecran_historique_poele`, excluded from the recorder), republished in full (≈ 2 KB).
**S** `{"action":"regler", "<clé>":<valeur>}` (one or more keys: `mode`, `tirage_manuel`, `consigne`, `tirage_min`, `recharge`,
`kp`, `ki`, `kd`, `limite`), `{"action":"brassage", "niveau":2}`

### Music — NOT over MQTT: the screen talks directly to Music Assistant (24/10)
Decided on 24/10: as the production unit talked directly to MPD, the Music page talks directly to the
Music Assistant API (`POST http://<serveur MA>:8095/api {"command", "args"}`, long-lived token created in MA,
`src/secrets_ma.h`, outside git). No HA automation (the proposed 1792200000026 is dropped). Code: `ui_musique.cpp`.
Why not through HA: `music_assistant.get_queue` only returns the current and the next track, `get_library` has no
years; and the HA token is refused outside the add-ons' internal network.
- Library: `music/artists/library_items` (all, names only, ONCE; letter = MA's sort name, "The Beatles" -> B),
  `music/artists/artist_albums` (by year, 16 at most). Covers: `<serveur>/imageproxy/<proxy_id>?size=160|256` (JPEG, no token).
- State: `players/all` + `player_queues/get` every 5 s, and 1.5 s after a command, ONLY when the page is displayed.
  Queue: `player_queues/items`, 20 tracks starting 2 before the current one.
- Commands: `player_queues/play_pause|next|previous|shuffle|repeat|play_index|clear|play_media|transfer`, `players/cmd/volume_set|set_members`.
- Radios: 33 chosen on 23/10, played by their Radio Browser id (`radiobrowser://radio/<uuid>`), without being in MA's library.
- Players: fixed list (a powered-off piCore disappears from MA's list): Salon (piCore, ex-moOde), PC-musique, Projecteur.
- Sizes measured on 24/10 (read with a filter, in PSRAM): 537 artists = 392 KB, 27 albums = 112 KB, 20-track queue = 58 KB,
  queue state = 12 KB, players = 10 KB. Duration on the ESP32: to be measured on the bench (serial log `[MA]`).

piCore chain (26/10: the Pi moved from moOde to piCorePlayer) and amplifier: through HA (MQTT), automation 1792200000026:
**E** `dashboard_guition/musique/etat` `{"picore":"eteint|arret|demarrage|pret", "ampli":0|1}` (retained): eteint = outlet cut;
arret = `script.picore_arret` in progress (~3 s: pause, amplifier, outlet); demarrage = outlet on, player not yet in MA;
pret = player available in MA; ampli = piCore outlet power > 10 W.
**S** `{"action":"picore","on":0|1}` (`switch.chaine_picore`: off via `script.picore_arret`; a power-on during the shutdown waits for it to end),
`{"action":"ampli","on":0|1}`, `{"action":"entree","n":1|2|3}` (RI: the amplifier doesn't report its input, the screen shows the last chosen one).
Playing on Salon with the chain off: the screen asks for power-on and keeps the playback, sent to MA when piCore appears there (3 min at most).
Power-down: 1792200000027 cuts the chain after 1 h without playback in MA (24/10).

### Lights — `dashboard_guition/lumieres/<id>/etat`
ids: `lampadaire_1`, `lampadaire_2`, `lampadaire_3`, `lampe_fenetre`, `globe_lunaire`, `bandeau_wled`, `bandeau_lidl`, `panneau_led`, `bandeau_bureau`, `lampe_bureau`, `chevet`
**E** `{"nom":"Lampadaire 1", "piece":"Séjour", "couleur":1, "on":1, "en_ligne":1, "lum":60, "mode":"couleur|blanc", "teinte":30, "sat":100, "kelvin":2700}`
(lum / mode / teinte / sat / kelvin only when on: HA doesn't provide them for an off lamp, the screen keeps the last
setting). Name and room kept by HA (rename without reflashing); the room tabs stay those of the page;
`couleur` = the entity has a color mode (hs, rgb, rgbw, rgbww, xy).
Entities: lampadaire_1/2/3 = `light.yeelight_lampe_1` / `…0x335ce36` / `…0x335d5d1`, lampe_fenetre = `light.lampe_fenetre`,
globe_lunaire = `light.globe_lunaire`, bandeau_wled = `light.bandeau_atelier`, bandeau_lidl = `light.ampoule_couleur_livarno`,
panneau_led = `light.ampoule_couleur_ledvance`, bandeau_bureau = `light.bandeau_bureau`, lampe_bureau = `light.degagement_lampe_de_bureau`,
chevet = `light.yeelight_bslamp1`.
**S** `{"action":"regler", "on":1, "lum":60}` / `{"action":"regler", "teinte":200, "sat":80}` / `{"action":"regler", "kelvin":3000}`
(HA: `light.turn_on` `hs_color` / `color_temp_kelvin` / `brightness_pct`)

### LED Panel — `dashboard_guition/panneau_led/etat` and `/set` (page written on 2 Nov 2026, HA side installed on 7 Nov 2026)
Red / infrared light-therapy panel in the workshop (ESPHome integration `panneau_led_rouge` in HA).
**E** `{"seance":0|1, "allumees":0|1, "longueurs":"all|rouge_jaune|nir", "mode":"minuterie|illimite", "duree":15, "restant":9,
"toutes":80, "i590":100, "i630":100, "i660":100, "i810":0, "i830":0, "i850":0, "i940":0, "radiateur":43, "ventilos":20, "en_ligne":1}`
(`switch.panneau_led_rouge_seance` → `seance`; `binary_sensor.panneau_led_rouge_led_allumees` → `allumees` (LEDs actually
on, which can differ for a moment from the switch); `select.panneau_led_rouge_longueurs_d_onde` ALL / RED-YELLOW / NIR →
`longueurs`; `select.panneau_led_rouge_mode` Minuterie / Illimité → `mode`; `number.panneau_led_rouge_duree_minuterie`
1 to 29 min → `duree`; `sensor.panneau_led_rouge_temps_restant` minutes remaining → `restant`, **-1 = unknown** (Illimité
mode, or nothing known); `number.panneau_led_rouge_intensite_toutes` → `toutes` and `_intensite_590_nm`, `_630_nm`,
`_660_nm`, `_810_nm`, `_830_nm`, `_850_nm`, `_940_nm` → `i590`…`i940`, all in 0-100 %;
`sensor.panneau_led_rouge_temperature_radiateur` °C → `radiateur`; `sensor.panneau_led_rouge_vitesse_ventilateurs` % →
`ventilos`). The panel runs its fans at full speed at 45 °C on the heatsink and shuts itself off for safety at 50 °C: the
panel does this itself, the screen only shows the temperature (amber at 45, red at 50).
**S** `{"action":"seance", "on":1|0}`, `{"action":"regler", "longueurs":"all|rouge_jaune|nir"}`, `{"action":"regler",
"mode":"minuterie|illimite"}`, `{"action":"regler", "duree":12}`, `{"action":"regler", "<intensité>":65}` where `<intensité>`
is `toutes`, `i590`, `i630`, `i660`, `i810`, `i830`, `i850` or `i940` (the screen sends a single brightness per message, on
slider release; `toutes` sets the seven wavelengths as the panel itself does). The screen holds what it
commanded for 3 s
(GARDE_MS) then applies the last state received, as with the Stove.
HA side (installed on 07/11, patch `contrat_panneau_23cfea3.patch` by Claude, 8.4 `23cfea3`): « Écran · panneau LED ·
état » (1792200000035, republishes the retained state on every change of these entities, 1 s delay, queued max 2) and
« Écran · panneau LED · commandes » (1792200000036, receives `set`, calls `switch.turn_on/off`, `select.select_option`,
`number.set_value` with clamped values). `en_ligne`: 0 if the panel is unpowered (entities `unavailable`); the screen doesn't
use it yet (it would show « Arrêt »).

### 3D Printer — `dashboard_guition/imprimante/etat` and `dashboard_guition/imprimante/fichiers`
**E** `{"etat":"eteinte|prete|impression|pause", "progression":62, "couche":84, "couches":136, "reste_min":62, "fin":1360, "fichier":"support_camera_v3", "buse":234, "buse_c":235, "plateau":70, "plateau_c":70, "caisson":27, "caisson_c":30, "lumiere":1, "filtration":0, "chauffage":1, "carte_mere":1, "puissance":212, "pm1":0.6, "vignette":"http://…"}`
puissance = W at the printer's smart plug (`sensor.0x00158d0003933633_power`), pm1 = the AQM's µg/m³ (`sensor.aqm_pm1`); -1 = unknown.
**E** `dashboard_guition/imprimante/fichiers` `{"nb":12, "base":"http://<ip imprimante>/server/files/gcodes/.thumbs/"}` +
`dashboard_guition/imprimante/fichiers/<0-11>` `{"nom":"piege souris", "fichier":"piege souris_0.2mm_PLA_Generic Klipper Printer_2h2m.gcode", "matiere":"PLA · 38 g", "date":"13/09", "minutes":122}`
(one subtopic per file, index 0 = the most recent; ~160 bytes each). The screen builds the thumbnail URL:
`<base><fichier sans .gcode, encodé>-48x48.png` (list) / `-300x300.png` (ring, downscaled to 160 px when downloading).
**S** `{"action":"allumer|eteindre|pause_reprise|arreter|origine"}`, `{"action":"lancer", "fichier":"…"}`, `{"action":"consignes", "buse":235, "plateau":70, "caisson":30}`, `{"action":"inter", "nom":"lumiere", "on":1}`
(HA relays Klipper: `sensor.ecran_etat_klipper` listens to `CR10SPRO/klipper/state/#` + `CR10SPRO/moonraker/status` published by
Moonraker; eteinte = outlet cut or Moonraker offline. Commands to Klipper via Moonraker's MQTT API
(`CR10SPRO/moonraker/api/request`, JSON-RPC: printer.print.pause/resume/cancel/start, printer.gcode.script M104/M140/M141,
SET_PIN, SET_FAN_SPEED, G28); outlet and clean shutdown via `script.allumer_imprimante` / `script.eteindre_imprimante`;
mainboard = IKEA smart plug `switch.prise_zigbee_1`.)
Files: HA asks Moonraker for `server/files/list`, then the metadata of the 12 most recent (rest_command, automation 1792200000025).
Thumbnails: the printer's OrcaSlicer profile ("MyKlipper 0.4 nozzle") already makes two, **48×48 and 300×300** PNG,
extracted by Moonraker into `.thumbs/<nom>-48x48.png` / `-300x300.png` (seen on 23/10): the list uses the 48, the ring the 300.

### PC — `dashboard_guition/pc/etat` and `dashboard_guition/pc/processus` (once per second, not retained)
**Published by an agent on the PC** — a small script (Python with no external dependency is enough) that publishes these two topics
once per second to the broker, with a dedicated MQTT user ("pc") — not by HA.
**E** `pc/etat` (≈ 580 bytes, + 61 bytes per card since 08/11) `{"cpu":10, "freq":3524, "cpu_temp":56, "cpu_w":39, "coeurs":[…16], "ram":27.2, "cache":30.7, "ram_total":62.0, "swap":23,
"gpus":[{"nom":"3090", "role":"llama-server-assistant", "charge":0, "temp":29, "w":7, "vram":23.3, "vram_total":24.0, "ps":"P2", "f":1500, "fm":9626, "f_max":2115, "fm_max":9876},…3], "net_bas":0.01, "net_haut":0.01,
"disque_l":0.0, "disque_e":0.0, "claude_5h":68, "claude_semaine":45, "depuis":"1 j 15 h", "heure":"11:59"}` (MB/s, GB; a card's role = the systemd
service of the process using the most VRAM on it; `heure` = the PC's clock, the screen has none).
Since 08/11, each card carries five more fields, published by `telemetrie-pc` (`/config/telemetrie-pc/pc.py`): `ps` (string
"P0".."P12", power state), `f` and `fm` (current MHz of the GPU and its memory), `f_max` and `fm_max` (the card's maxima, `null` if
unknown). Real reading from 08/11 around 19:00: 3090 P2 1500/9626 (max 2115/9876); 3060 P2 1500/8401 (max 2310/8601); 3090' P2 1500/9626 (max 2100/9876).
The screen shows `ps` next to the watts (« 53° · 99 W · P2 ») and the two current frequencies, each next to its icon (mockup approved on
08/11, prod 8.4 `0c1a947`); **a missing field or `null` shows as "-"** — the screen resets its values to zero on every frame, it never
shows a stale frequency. `f_max` and `fm_max` are received and kept, not displayed yet.
**E** `pc/processus` (≈ 900 bytes) `[{"nom":"llama-server", "role":"llama-server-assistant", "cpu":0.0, "mem":1.9, "vram":28.4},…12]` (first 6 by CPU,
3 by memory, 3 by VRAM; role = systemd service, empty for desktop applications).
The screen switches to « PC en veille » when nothing has arrived for 10 s.
**S** `dashboard_guition/pc/set` `{"action":"reveiller|veille"}` -> HA 1792200000028 (IN HA on 24/10): `switch.eveille_ordi_cachy` on (WoL) / off (usual sleep).

### "AI" page — `dashboard_guition/pc/ia` and `dashboard_guition/pc/ia_activite` (retained; PC agent, rail page written on 11 Nov 2026)
Published by the PC agent (the Claude agent on the PC), not by HA. Both are **retained** (the screen waits for them to
declare itself complete, like `pc/ia` since 31 Oct 2026) and displayed by the "AI" page in the rail, placed right after
PC (mockup approved on 10 Nov 2026). The "AI" tab of the PC page (31 Oct 2026) is removed.
**E** `pc/ia` (≈ 150 bytes) — format UNCHANGED since 31 Oct 2026, but with **ONE account**, the one in use; the screen
reads the first element of the array and ignores the others:
`{"comptes":[{"nom":"compte-un@exemple.invalid", "h5":92, "h5_reset":1792710000, "semaine":13, "semaine_reset":1793228400, "maj":1792703000}]}`
(`h5`/`semaine`: % used; `h5_reset`/`semaine_reset`: Unix time of the reset, 0 if no current window; `maj`: Unix time of
the agent's reading — if older than 10 min the screen writes "read … ago").
The `claude_5h`/`claude_semaine` fields of `pc/etat` (24 Oct 2026) are read by nothing: Claude usage lives in `pc/ia`.
**E** `pc/ia_activite` (≈ 400 bytes, NEW 11 Nov 2026) — what the Claude agent is doing and what it produced:
`{"etat":"reflechit|attente|repos", "alerte":"autorisation|question|", "tokens":612345, "outils":411, "prompts":14, "sessions":2, "histo":[30 integers], "maj":1792703000}`
(`tokens`: total tokens produced; `outils`: tool calls; `prompts`: prompts handled; `sessions`: open sessions; `histo`:
tokens produced per 10-min slice over 5 h, **oldest first**, the last one = the current slice; `maj`: Unix time of the
message — 0 or missing = the screen stamps it on receipt).
Display: "thinking…" orange / "authorization requested" or "question for you" red (the alert comes before the state) /
"idle" grey. **No message, or older than 2 min: "idle"**, Clawd closes its eyes, and the last received numbers stay on
screen. The screen publishes nothing on these two topics.

Both topics are published by [tools/claude_ia/](tools/claude_ia/) (Claude Code hook + Python agent,
step-by-step installation in its README).

### Doorbell — `dashboard_guition/sonnette/etat`
**E** `{"sonne":1, "depuis":1792694300}` (drops back to 0 when the door opens) → the screen shows the camera if the setting is active.
IN HA on 27/10 (1791000000040: sonne 1; 041: sonne 0), not retained; screen: `ui_sonnette` (full screen, 60 s, touch).

### Chime — `dashboard_guition/carillon/etat`, `/set`
**E** `{"melodie":0, "volume":50}`; **S** `{"action":"regler", "melodie":1}` / `{"action":"regler", "volume":60}`

## 4. Message size

The old firmware (PubSubClient, default 256-byte buffer) **silently ignores** any larger message, topic included
(seen on 17/10: `dashboard/infos/etat` at 291 bytes no longer arrived). The new firmware's `ui_mqtt` module enlarges this buffer
(`setBufferSize`, at least 4 KB): the music and printer file lists far exceed 256 bytes.

## 5. What is specific to each screen — `dashboard_guition/appareil/<nom>/...` (coded on 24/10: `ui_appareil.cpp`)

| Topic | Direction | Content |
|---|---|---|
| `statut` | screen → HA, retained, **last will** | `en_ligne` / `hors_ligne` |
| `infos` | screen → HA, retained, every 60 s | `{"depuis":"2 h 05 min", "redemarrage":"mise sous tension", "ram":180, "psram":7000, "cpu_temp":48, "version":"Oct 24 2026", "wifi":"mon_reseau", "ip":"192.168.1.41", "rssi":-60}` (KB) |
| `reglages` | screen → HA, retained, at startup and on every change | `{"lum_jour":75, "lum_nuit":30, "veille_s":60, "piece":"cuisine", "retour_s":60, "camera":1}` (sleep / return: 0 = never) |
| `reglages/set` | HA → screen | same keys (all optional), to configure a screen from HA; applied, kept, republished |
| `journal` | screen → HA, not retained | `{"texte":"Démarrage (mise sous tension) · données en 4,2 s", "alerte":0}`: startup, Wi-Fi, MQTT, HA silent; recorded in HA's logbook by 1792200000032 (« Écran <nom> », ⚠️ if alert) |
| `reveil` | screen → HA | (to do, radar) `{"cause":"radar|minuterie|froid", …}` |

The screen's time: HA's (`maj` of `maison/etat`), like the production unit (no NTP), CET/CEST timezone; set on the 1st message,
then only if it lags by more than a minute (a retained message can be 5 min old). The screen's settings are kept in its flash memory (like the production unit, which wrote them to LittleFS). Backlight, 1 s fade like the production unit:
`lum_nuit` when `maison/etat` says night, off after `veille_s` without a touch.

### Home settings and devices (HA, the same for all screens)
**E** `dashboard_guition/maison/reglages` (1792200000029, retained) `{"melodie":"Mario", "melodies":["Mario","Simpsons","Halloween","Noël"], "volume":40,
"terrasse_porte":1, "terrasse_mouvement":1}`; **S** `maison/reglages/set` `{"action":"melodie","nom":"Simpsons"}`, `{"action":"volume","volume":60}`,
`{"action":"terrasse_porte|terrasse_mouvement","on":0|1}` (enables / disables the timed turn-on automations).
**E** `dashboard_guition/maison/appareils` (1792200000030, retained, every minute) `{"liste":[["Bouilloire","Cuisine",0],…]}` = [name (30 chars.), room,
online] of HA's ESPHome + Zigbee devices, except those labelled « Écran : ignorer »; offline first, then by room (≈ 2 KB).
Links shown on the Info page: Wi-Fi (the screen), Home Assistant (a message from HA less than 3 min ago), then this list.

## 6. Migration

1. Approve this document.
2. HA: create the `dashboard_guition/…` automations (state + commands) **alongside** the old `dashboard/…` ones, page by page; check with a read-only broker client.
3. Screen: single module `src/ui_lvgl/ui_mqtt.cpp` (JSON → page data, commands → `set`), tested in the simulator on a real broker capture.
4. Flash the bench unit at home, try page by page.
5. Once every screen has the new interface: delete the `dashboard/…` and `thermostat/tele/…` automations, clear the dead retained messages
   (`RegPID_poele/#`, `Distributeur_croquettes/#`, `thermostat/tele/aqm`).

## 7. Decisions

- Prefix: `dashboard_guition/` (22/10).
- Keys: **French without accents everywhere** (22/10): the screen doesn't know HA's vocabulary (`fan_level`,
  `hvac_action`…), it is the automation that translates; the names stay the same from the JSON down to the page's code.
- Times: **minutes since midnight** (22/10): an integer, no text to parse on the ESP, immediate calculations;
  on the HA side `{{ h * 60 + m }}`. Instants (last wake-up, "since") stay as Unix timestamps.
