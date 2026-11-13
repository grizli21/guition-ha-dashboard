# Contrat MQTT des écrans muraux (nouvelle interface LVGL)

[English](CONTRAT_MQTT.en.md) | **Français**

Référence du contrat MQTT, première rédaction le 22/10/2026, tenue à jour avec le code.
Le code de l'écran (`src/ui_lvgl/`) et les automatisations HA suivent ce document : on le modifie d'abord, puis le code.

## 1. Principes

1. **L'écran ne parle qu'à HA, par MQTT.** Plus aucun accès direct aux appareils (Moonraker, MPD de moOde, ESPEasy, WLED…).
   Seules exceptions, parce que ce sont des images lourdes, chargées en HTTP : flux de la caméra, miniatures de l'imprimante,
   pochettes et logos de Music Assistant (adresses fournies dans l'état, par HA).
2. **Une seule forme pour toutes les pages** :
   - `dashboard_guition/<page>/etat` : HA → écran, JSON **retenu** (retain), republié à chaque changement ;
   - `dashboard_guition/<page>/set` : écran → HA, JSON `{"action": "...", ...}`, non retenu.
   - Listes : un sous-sujet par élément (`dashboard_guition/lumieres/<id>/etat`, `dashboard_guition/thermostats/<piece>/etat`).
3. **Ce qui concerne un écran précis** : `dashboard_guition/appareil/<nom>/...` (nom = `cuisine`, `sejour`…).
4. **Côté HA, deux automatisations par page** au même gabarit : « Écran · <page> · état » et « Écran · <page> · commandes ».
5. **Nommage** : clés en français, minuscules, sans accent, unités dans le document (pas dans la clé) ; booléens en `0` / `1` ;
   heures en **minutes depuis minuit** ; températures en °C (nombre à virgule) ; pourcentages entiers.
6. **Préfixe `dashboard_guition/`** (22/10) : neuf, il ne gêne pas l'ancien `dashboard/`, qui reste en service pour
   les écrans à l'ancien firmware. Les deux tournent en parallèle, on bascule page par page, puis on supprime `dashboard/` (voir §5).

## 2. État des lieux (relevé le 22/10 : messages retenus du broker + `automations.yaml` + `mqtt_fonctions.h`)

| Sujet actuel | Publié par | Lu par | Constat | Verdict |
|---|---|---|---|---|
| `dashboard/thermostats/<piece>/etat` | HA (auto 1791000000001) | firmware | bon ; `mode` en anglais (`Off`) | → `dashboard_guition/thermostats/<piece>/etat` |
| `thermostat/tele/<piece>` | HA (même auto) | ancien firmware | **doublon exact** du précédent | supprimer après migration |
| `thermostat/tele/command` | firmware + HA | les deux | « resynchro » de l'époque Jeedom | supprimer (le retain suffit) |
| `thermostat/tele/infos1`, `infos2` | HA (1791000000003) | ancien firmware | doublon de `dashboard/infos/etat` | supprimer |
| `thermostat/tele/aqm` | personne (dernier : 09/2026) | ancien firmware | **périmé** (AQM hors ligne) | supprimer |
| `dashboard/infos/etat` | HA (1791000000150) | firmware | fourre-tout : extérieur, puissance, eau, présence, phase, **et** le Xiaomi | éclater (§3 Maison, Air) |
| `dashboard/ballon/etat`, `/set` | HA (179 / 173) | firmware | bon ; `eco_state` `"ECO"`, `relay_state` | → `dashboard_guition/ballon/...` |
| `dashboard/vmc/etat`, `/set` | HA (188 / 191) | firmware | bon | → `dashboard_guition/vmc/...` |
| `dashboard/poele/etat`, `/set` | HA (130 / 131) | firmware | bon | → `dashboard_guition/poele/...` |
| `RegPID_poele/#` | personne (retenu « Connection Lost ») | ancien firmware | **mort** depuis le passage du poêle en ESPHome | supprimer + vider le retenu |
| `dashboard/croquettes/etat`, `/commande` | HA (160 / 161) | firmware | bon, mais `/commande` au lieu de `/set` ; stats faussées (595 g) | → `dashboard_guition/croquettes/...` |
| `Distributeur_croquettes/#` | personne (ESPEasy disparu) | ancien firmware | **vieux retenus** (879 g, « Connection Lost ») | supprimer + vider les retenus |
| `dashboard/lumieres/<id>/etat`, `/set` | HA (181 / 182) | firmware | bon ; ids anciens (`hektar_haute`, 2 lampadaires sur 3, pas de Lidl ni panneau) | → `dashboard_guition/lumieres/...` ids revus |
| `wled/...` | WLED | ancien firmware | accès direct | ne plus lire |
| `dashboard/espurifier/etat`, `/set` | HA (187) | firmware | bon, très détaillé | → `dashboard_guition/air/atelier/...` |
| `dashboard/purificateur/etat`, `/set` | HA (183) | firmware | Xiaomi du séjour | → `dashboard_guition/air/sejour/...` |
| `dashboard/imprimante/etat`, `/set` | HA (189) | firmware | prise et carte mère seulement | → `dashboard_guition/imprimante/...` complété |
| `CR10SPRO/klipper/state/...` | Moonraker (direct) | firmware | accès direct à l'imprimante | HA relaie dans `dashboard_guition/imprimante/etat` |
| `dashboard/moode/etat`, `/set` | HA (185) | firmware | alimentation seulement ; le reste en MPD direct | alimentation → `dashboard_guition/...` (à faire) ; le reste en direct à l'API de Music Assistant (24/10) |
| `dashboard/ampli/set` | firmware | HA (190) | codes RI de l'Onkyo | → `dashboard_guition/musique/set` action `ampli` |
| `dashboard/store/set`, `dashboard/rideau/set` | firmware | HA (184) | commandes sans état publié | → `dashboard_guition/ouvrants/...` avec état |
| `dashboard/cafe/set` | firmware | HA (186) | café programmé (heure) | → `dashboard_guition/cafe/...` + bouilloire |
| `dashboard/carillon/etat` | HA | firmware | — | → `dashboard_guition/carillon/...` |
| sonnette (auto 1791000000040 / 041) | HA | firmware | publie vers une liste de sujets | → `dashboard_guition/sonnette/etat` |

## 3. Nouveau contrat, page par page

Légende : **E** = `etat` (HA → écran, retenu), **S** = `set` (écran → HA). Entre parenthèses : l'entité HA d'origine.

### Maison (commun à plusieurs pages) — `dashboard_guition/maison/etat`
**E** `{"phase":"jour|nuit", "presence":0|1, "ext_temp":6.8, "ext_hum":87, "maj":1792694287}`
(`sensor.openweathermap_temperature`, `sensor.openweathermap_humidite`, `person.exemple`, `sun.sun`). Sert au thermostat (extérieur),
au graphe, aux réglages (nuit). Sans la consommation ni l'eau (bandeau énergie supprimé) : publié aux seuls vrais changements.

### Chauffage — `dashboard_guition/thermostats/<piece>/etat` (`cuisine`, `sejour`, `chambre1`, `chambre2`, `sdb`)
**E** `{"temp":14.5, "hum":69.5, "consigne":15.0, "mode":"arret|horsgel|eco|confort|manuel", "chauffe":0|1, "batterie":59, "signal":164}`
(`climate.<piece>` : `preset_mode` away/eco/comfort/none → horsgel/eco/confort/manuel ; `hvac_mode off` → arret ; `hvac_action heating` → chauffe)
**S** `dashboard_guition/thermostats/<piece>/set` `{"action":"regler", "mode":"eco"}` ou `{"action":"regler", "consigne":19.5}`

### Graphes — `dashboard_guition/thermostats/<pièce>/historique` et `dashboard_guition/maison/historique` (24/10, DANS HA)
Tenus par HA (capteurs modèles `sensor.ecran_historique_chauffage_<pièce>` / `sensor.ecran_historique_exterieur`, déclenchés
toutes les 5 min, restaurés au redémarrage, exclus du recorder) ; publiés retenus EN ENTIER à chaque point par 1792200000031
(un message perdu ne décale rien, ≈ 2,2 Ko par pièce). 288 points = 24 h, le plus récent en dernier :
**E** `thermostats/<pièce>/historique` `{"fin":930, "t":[195,…288], "c":[[0,160],[100,195],…], "h":[53,…288], "ch":"000…a06d…"}` :
`fin` = minute du jour du dernier point ; `t` = °C x10 (-999 = pas de mesure) ; `c` = consigne x10 seulement à ses changements
([rang, valeur], -999 = chauffage coupé) ; `h` = % (0 = pas de mesure) ; `ch` = 1 bit par point (a chauffé À UN MOMENT des 5 min,
vu à chaque changement de hvac_action), 36 octets en hexa, bit 0 = point 0.
**E** `maison/historique` `{"fin":930, "ext":[…288]}` : sonde nord (locale), OpenWeatherMap en secours, °C x10.
Écran : courbes en PSRAM, remplies dès l'arrivée des messages (page affichée ou non), calées à droite tant que HA a moins de 24 h.

### Qualité de l'air — `dashboard_guition/air/atelier/etat` (ESPurifier), `dashboard_guition/air/sejour/etat` (Xiaomi)
**E** `{"pm25":2, "info":"COV 72", "modes":["Arrêt","Fan","1","2","3"], "mode":0, "rpm":1250, "en_ligne":1}`
(atelier : `sensor.espurifier_pm2_5`, `select.espurifier_mode` ; séjour : Xiaomi, `filter_life_remaining` dans `info`)
`rpm` (25/10) : vitesse réelle du ventilateur, arrondie à 50 tr/min (atelier : `sensor.espurifier_ventilateur_vitesse_de_rotation`,
séjour : `sensor.purificateur_motor_speed`) : l'hélice de l'écran tourne à 1 tour/s pour 1 000 tr/min, arrêtée à 0.
**S** `{"action":"mode", "mode":2}`
**E** `dashboard_guition/air/<id>/historique` `{"pas":30, "points":[3,4,5,255,…]}` : 48 points (24 h), le plus récent en dernier,
255 = pas de mesure. **Tenu par HA** (décision du 22/10 : l'écran redémarre, dormira en mode batterie, et deux écrans
doivent montrer la même courbe) : moyenne de la demi-heure (`statistics` average_step), republié en entier à chaque point.

### Ouvrants — `dashboard_guition/ouvrants/<id>/etat` (`store_terrasse`, `volet_cuisine`, `toile`)
**E** `{"position":60, "mouvement":"ouvre|ferme|arret", "en_ligne":1}` ; toile en plus `"firetv":0|1`
(`cover.store_terrasse` = store BANNE : 100 = sorti ; `cover.store_cuisine` = IKEA FYRTUR : 100 = relevé ;
`cover.toile_vp_et_ampli_toile` : 100 = remontée ; `media_player.fire_tv_sejour`). `mouvement` = états opening / closing
de HA (« monte / descend » ne veut rien dire pour un store banne, 22/10).
**S** `{"action":"ouvrir|fermer|stop"}` ou `{"action":"position", "position":30}` ; toile : `{"action":"seance", "on":1}` (allume / éteint la Fire TV ; l'automatisation 1791000000163 fait la toile)

### Croquettes — `dashboard_guition/croquettes/etat`
**E** `{"bol":21.0, "stock":66, "autonomie":6, "vide":0, "repas":[{"h":390,"q":15,"a":1},...4], "jours":[{"g":31,"n":2},...7], "jour_semaine":2}`
(`sensor.distributeur_croquettes_*`, `number.distributeur_croquettes_repas_N_*`, `binary_sensor.distributeur_croquettes_plus_de_croquettes` ; `jours[0]` = aujourd'hui ; `h` en minutes)
**S** `{"action":"repas", "numero":2, "h":765, "q":15, "a":1}`, `{"action":"servir"}`, `{"action":"tare"}`

### Café — `dashboard_guition/cafe/etat`
**E** `{"cafe":{"prog":0|1, "h":405, "allumee":0, "marche":0, "en_ligne":1}, "bouilloire":{"prog":0|1, "h":420, "temp":80, "chauffe":0, "en_ligne":1}, "recentes":[405,435]}`
(`input_boolean/input_datetime.cafe_programme*` + `bouilloire_programme*`, `input_number.bouilloire_programme_temp`,
`input_text.cafe_recentes`, `binary_sensor.cuisine_cafetiere_*`, `binary_sensor.bouilloire_en_chauffe` ; la programmation
est gardée par HA, une seule fois = prochaine occurrence de `h`)
**S** `{"action":"programmer", "appareil":"cafe|bouilloire", "h":405, "on":1, "temp":80}` (`temp` : bouilloire),
`{"action":"maintenant", "appareil":"cafe"}`, `{"action":"maintenant", "appareil":"bouilloire", "temp":80}`,
`{"action":"arreter", "appareil":"cafe|bouilloire"}`. Café lancé à l'heure par 1791000000186 (existant), bouilloire par 1792200000017.

### Eau chaude — `dashboard_guition/ballon/etat`
**E** `{"temp":45.1, "consigne":50, "mode":"arret|auto|force", "eco":1, "chauffe":0, "chaude_jour":5, "froide_jour":17, "kwh_jour":4.4, "debit":0, "puissance":0, "combles":14}`
(`sensor.ballon_eau_chaude_temperature`, `input_number.ballon_consigne`, `input_select.ballon_mode` OFF/AUTO/FORCE, `input_select.ballon_regime` ECO/NORMAL ; `chauffe` = relais `switch.tableau_electrique_chauffe_eau`)
25/10 : `chaude_jour` L (`sensor.ballon_eau_chaude_eau_chaude_journalier`), `froide_jour` L = arrivée de la maison, TOUTE l'eau
(`sensor.arrivee_eau_journalier`, affichée « eau totale »), `kwh_jour` (`sensor.jour_chauffe_eau`), `debit` L/min d'eau chaude
(ESP du ballon), `puissance` W arrondie à 50 (tore de l'Ecocompteur, relevé toutes les 10 s), `combles` °C.
**E** `dashboard_guition/ballon/historique` `{"fin":1090, "t":[…288 dixièmes de °C, -999 = pas de mesure…], "c":[[rang, consigne °C], …],
"l":[[rang, litres], …], "ch":"72 hexa", "hc":"72 hexa"}` : 24 h, un point toutes les 5 min, le plus récent en dernier ; `c` =
changements de consigne (escalier) ; `l` = litres d'eau chaude des points où on a tiré (compteur de l'Ecocompteur, pas de remise à
zéro) ; `ch` = 1 bit par point (bit j de l'octet k = point 8k+j) : le relais a chauffé ; `hc` = idem, heures creuses (téléinfo).
Tenu par HA (`sensor.ecran_historique_ballon`, exclu du recorder), republié en entier (≈ 2 Ko) par 1792200000033.
**S** `{"action":"regler", "consigne":55}` / `{"action":"regler", "mode":"force"}` / `{"action":"regler", "eco":0}`

### VMC — `dashboard_guition/vmc/<zone>/etat` (`maison`, `sous_sol`)
**E** `{"vitesse":0|1|2, "auto":0|1, "hum":82, "hum_lieu":"salle de bain", "seuil":80}` (sous-sol : `seuil` 0 = pas d'automatisme)
(`fan.vmc_maison` / `fan.vmc_sous_sol` 0/50/100 %, `input_boolean.vmc_maison_forcee`, humidité de la SdB)
**S** `{"action":"vitesse", "vitesse":2}`

### Poêle — `dashboard_guition/poele/etat`
**E** `{"fumees":19, "corps":24.1, "tirage":100, "mode":"auto|manuel|levier", "tirage_manuel":100, "feu":"eteint|allumage|regulation|recharger", "consigne":240, "tirage_min":30, "recharge":180, "kp":3.0, "ki":0.01, "kd":10.0, "limite":30, "brassage":0, "en_ligne":1}`
(`sensor.poele_*`, `select.poele_mode_tirage` Auto / Manuel distance / Manuel local, `number.poele_*`, `binary_sensor.poele_recharger`,
`binary_sensor.poele_regulation_en_marche`). `feu` : recharger > régulation (PID, démarre au-dessus de 100 °C) > allumage
(fumées en hausse de plus de 3 °C en 5 min, `sensor.poele_fumees_variation_5_min`) > éteint. `brassage` = purificateur
Xiaomi du séjour en préréglage Fan, vitesse 1-3 (0 = éteint), confirmé le 23/10.
**E** `dashboard_guition/poele/historique` `{"pas":5, "fumees":[…144…], "corps":[…], "tirage":[…]}` : 12 h (maquette, 23/10),
-1 = pas de mesure, tenu par HA (`sensor.ecran_historique_poele`, exclu du recorder), republié en entier (≈ 2 Ko).
**S** `{"action":"regler", "<clé>":<valeur>}` (une ou plusieurs clés : `mode`, `tirage_manuel`, `consigne`, `tirage_min`, `recharge`,
`kp`, `ki`, `kd`, `limite`), `{"action":"brassage", "niveau":2}`

### Musique — PAS par MQTT : l'écran parle directement à Music Assistant (24/10)
Décidé le 24/10 : comme la production parlait directement à MPD, la page Musique parle directement à l'API de
Music Assistant (`POST http://<serveur MA>:8095/api {"command", "args"}`, jeton de longue durée créé dans MA,
`src/secrets_ma.h`, hors git). Pas d'automatisation HA (la 1792200000026 proposée est abandonnée). Code : `ui_musique.cpp`.
Pourquoi pas par HA : `music_assistant.get_queue` ne rend que le morceau en cours et le suivant, `get_library` n'a pas les
années ; et le jeton de HA est refusé hors du réseau interne des add-ons.
- Bibliothèque : `music/artists/library_items` (tous, noms seulement, UNE fois ; lettre = nom de tri de MA, « The Beatles » -> B),
  `music/artists/artist_albums` (par année, 16 au plus). Pochettes : `<serveur>/imageproxy/<proxy_id>?size=160|256` (JPEG, sans jeton).
- État : `players/all` + `player_queues/get` toutes les 5 s, et 1,5 s après une commande, SEULEMENT quand la page est affichée.
  File : `player_queues/items`, 20 morceaux à partir de 2 avant celui en cours.
- Commandes : `player_queues/play_pause|next|previous|shuffle|repeat|play_index|clear|play_media|transfer`, `players/cmd/volume_set|set_members`.
- Radios : 33 choisies le 23/10, jouées par leur identifiant Radio Browser (`radiobrowser://radio/<uuid>`), sans être dans la bibliothèque de MA.
- Lecteurs : liste fixe (piCore éteint disparaît de la liste de MA) : Salon (piCore, ex-moOde), PC-musique, Projecteur.
- Tailles mesurées le 24/10 (lues avec un filtre, en PSRAM) : 537 artistes = 392 Ko, 27 albums = 112 Ko, file de 20 = 58 Ko,
  état d'une file = 12 Ko, lecteurs = 10 Ko. Durée sur l'ESP32 : à mesurer au banc (journal série `[MA]`).

Chaîne piCore (26/10 : le Pi est passé de moOde à piCorePlayer) et ampli : par HA (MQTT), automatisation 1792200000026 :
**E** `dashboard_guition/musique/etat` `{"picore":"eteint|arret|demarrage|pret", "ampli":0|1}` (retenu) : eteint = prise coupée ;
arret = `script.picore_arret` en cours (~3 s : pause, ampli, prise) ; demarrage = prise allumée, lecteur pas encore dans MA ;
pret = lecteur disponible dans MA ; ampli = puissance de la prise piCore > 10 W.
**S** `{"action":"picore","on":0|1}` (`switch.chaine_picore` : arrêt par `script.picore_arret` ; une marche pendant l'arrêt attend sa fin),
`{"action":"ampli","on":0|1}`, `{"action":"entree","n":1|2|3}` (RI : l'ampli ne dit pas son entrée, l'écran montre la dernière choisie).
Lecture sur le Salon chaîne éteinte : l'écran demande la marche et garde la lecture, envoyée à MA quand piCore y apparaît (3 min au plus).
Extinction : 1792200000027 coupe la chaîne après 1 h sans lecture dans MA (24/10).

### Lumières — `dashboard_guition/lumieres/<id>/etat`
ids : `lampadaire_1`, `lampadaire_2`, `lampadaire_3`, `lampe_fenetre`, `globe_lunaire`, `bandeau_wled`, `bandeau_lidl`, `panneau_led`, `bandeau_bureau`, `lampe_bureau`, `chevet`
**E** `{"nom":"Lampadaire 1", "piece":"Séjour", "couleur":1, "on":1, "en_ligne":1, "lum":60, "mode":"couleur|blanc", "teinte":30, "sat":100, "kelvin":2700}`
(lum / mode / teinte / sat / kelvin seulement si allumée : HA ne les donne pas pour une lampe éteinte, l'écran garde le dernier
réglage). Nom et pièce tenus par HA (renommer sans reflasher) ; les onglets de pièces restent ceux de la page ;
`couleur` = l'entité a un mode couleur (hs, rgb, rgbw, rgbww, xy).
Entités : lampadaire_1/2/3 = `light.yeelight_lampe_1` / `…0x335ce36` / `…0x335d5d1`, lampe_fenetre = `light.lampe_fenetre`,
globe_lunaire = `light.globe_lunaire`, bandeau_wled = `light.bandeau_atelier`, bandeau_lidl = `light.ampoule_couleur_livarno`,
panneau_led = `light.ampoule_couleur_ledvance`, bandeau_bureau = `light.bandeau_bureau`, lampe_bureau = `light.degagement_lampe_de_bureau`,
chevet = `light.yeelight_bslamp1`.
**S** `{"action":"regler", "on":1, "lum":60}` / `{"action":"regler", "teinte":200, "sat":80}` / `{"action":"regler", "kelvin":3000}`
(HA : `light.turn_on` `hs_color` / `color_temp_kelvin` / `brightness_pct`)

### Panneau LED — `dashboard_guition/panneau_led/etat` et `/set` (page écrite le 02/11/2026, côté HA installé le 07/11/2026)
Panneau de luminothérapie rouge / infrarouge de l'atelier (intégration ESPHome `panneau_led_rouge` dans HA).
**E** `{"seance":0|1, "allumees":0|1, "longueurs":"all|rouge_jaune|nir", "mode":"minuterie|illimite", "duree":15, "restant":9,
"toutes":80, "i590":100, "i630":100, "i660":100, "i810":0, "i830":0, "i850":0, "i940":0, "radiateur":43, "ventilos":20, "en_ligne":1}`
(`switch.panneau_led_rouge_seance` → `seance` ; `binary_sensor.panneau_led_rouge_led_allumees` → `allumees` (LED réellement
allumées, qui peut différer un instant du switch) ; `select.panneau_led_rouge_longueurs_d_onde` ALL / RED-YELLOW / NIR →
`longueurs` ; `select.panneau_led_rouge_mode` Minuterie / Illimité → `mode` ; `number.panneau_led_rouge_duree_minuterie`
1 à 29 min → `duree` ; `sensor.panneau_led_rouge_temps_restant` minutes restantes → `restant`, **-1 = inconnu** (mode
Illimité, ou rien de connu) ; `number.panneau_led_rouge_intensite_toutes` → `toutes` et `_intensite_590_nm`, `_630_nm`,
`_660_nm`, `_810_nm`, `_830_nm`, `_850_nm`, `_940_nm` → `i590`…`i940`, tous en 0-100 % ;
`sensor.panneau_led_rouge_temperature_radiateur` °C → `radiateur` ; `sensor.panneau_led_rouge_vitesse_ventilateurs` % →
`ventilos`). Le panneau coupe ses ventilateurs à fond à 45 °C de radiateur et se coupe en sécurité à 50 °C : c'est le
panneau qui le fait, l'écran ne montre que la température (ambre à 45, rouge à 50).
**S** `{"action":"seance", "on":1|0}`, `{"action":"regler", "longueurs":"all|rouge_jaune|nir"}`, `{"action":"regler",
"mode":"minuterie|illimite"}`, `{"action":"regler", "duree":12}`, `{"action":"regler", "<intensité>":65}` où `<intensité>`
est `toutes`, `i590`, `i630`, `i660`, `i810`, `i830`, `i850` ou `i940` (l'écran envoie une seule intensité par message, au
relâchement du curseur ; `toutes` règle les sept longueurs d'onde comme le fait le panneau). L'écran garde ce qu'il a
commandé pendant 3 s
(GARDE_MS) et applique ensuite le dernier état reçu, comme pour le Poêle.
Côté HA (installé le 07/11, patch `contrat_panneau_23cfea3.patch` de Claude, 8.4 `23cfea3`) : « Écran · panneau LED ·
état » (1792200000035, republie l'état retenu à chaque changement de ces entités, délai 1 s, queued max 2) et
« Écran · panneau LED · commandes » (1792200000036, reçoit `set`, appelle `switch.turn_on/off`, `select.select_option`,
`number.set_value` bornés). `en_ligne` : 0 si le panneau est hors tension (entités `unavailable`) ; l'écran ne l'exploite
pas encore (il afficherait « Arrêt »).

### Imprimante 3D — `dashboard_guition/imprimante/etat` et `dashboard_guition/imprimante/fichiers`
**E** `{"etat":"eteinte|prete|impression|pause", "progression":62, "couche":84, "couches":136, "reste_min":62, "fin":1360, "fichier":"support_camera_v3", "buse":234, "buse_c":235, "plateau":70, "plateau_c":70, "caisson":27, "caisson_c":30, "lumiere":1, "filtration":0, "chauffage":1, "carte_mere":1, "puissance":212, "pm1":0.6, "vignette":"http://…"}`
puissance = W de la prise de l'imprimante (`sensor.0x00158d0003933633_power`), pm1 = µg/m³ de l'AQM (`sensor.aqm_pm1`) ; -1 = inconnu.
**E** `dashboard_guition/imprimante/fichiers` `{"nb":12, "base":"http://<ip imprimante>/server/files/gcodes/.thumbs/"}` +
`dashboard_guition/imprimante/fichiers/<0-11>` `{"nom":"piege souris", "fichier":"piege souris_0.2mm_PLA_Generic Klipper Printer_2h2m.gcode", "matiere":"PLA · 38 g", "date":"13/09", "minutes":122}`
(un sous-sujet par fichier, rang 0 = le plus récent ; ~160 o chacun). L'écran fabrique l'adresse des miniatures :
`<base><fichier sans .gcode, encodé>-48x48.png` (liste) / `-300x300.png` (anneau, réduite à 160 px au téléchargement).
**S** `{"action":"allumer|eteindre|pause_reprise|arreter|origine"}`, `{"action":"lancer", "fichier":"…"}`, `{"action":"consignes", "buse":235, "plateau":70, "caisson":30}`, `{"action":"inter", "nom":"lumiere", "on":1}`
(HA relaie Klipper : `sensor.ecran_etat_klipper` écoute `CR10SPRO/klipper/state/#` + `CR10SPRO/moonraker/status` publiés par
Moonraker ; éteinte = prise coupée ou Moonraker offline. Commandes vers Klipper par l'API MQTT de Moonraker
(`CR10SPRO/moonraker/api/request`, JSON-RPC : printer.print.pause/resume/cancel/start, printer.gcode.script M104/M140/M141,
SET_PIN, SET_FAN_SPEED, G28) ; prise et arrêt propre par `script.allumer_imprimante` / `script.eteindre_imprimante` ;
carte mère = prise IKEA `switch.prise_zigbee_1`.)
Fichiers : HA demande `server/files/list` puis les métadonnées des 12 plus récents à Moonraker (rest_command, auto 1792200000025).
Miniatures : le profil OrcaSlicer de l'imprimante (« MyKlipper 0.4 nozzle ») en fait déjà deux, **48×48 et 300×300** PNG,
extraites par Moonraker dans `.thumbs/<nom>-48x48.png` / `-300x300.png` (vu le 23/10) : la liste prend la 48, l'anneau la 300.

### PC — `dashboard_guition/pc/etat` et `dashboard_guition/pc/processus` (1 fois par seconde, non retenus)
**Publié par un agent sur le PC** — un petit script (Python sans dépendance externe suffit) qui publie ces deux sujets
une fois par seconde vers le broker, avec un compte MQTT dédié (« pc ») — pas par HA.
**E** `pc/etat` (≈ 580 o, + 61 o par carte depuis le 08/11) `{"cpu":10, "freq":3524, "cpu_temp":56, "cpu_w":39, "coeurs":[…16], "ram":27.2, "cache":30.7, "ram_total":62.0, "swap":23,
"gpus":[{"nom":"3090", "role":"llama-server-aurore", "charge":0, "temp":29, "w":7, "vram":23.3, "vram_total":24.0, "ps":"P2", "f":1500, "fm":9626, "f_max":2115, "fm_max":9876},…3], "net_bas":0.01, "net_haut":0.01,
"disque_l":0.0, "disque_e":0.0, "claude_5h":68, "claude_semaine":45, "depuis":"1 j 15 h", "heure":"11:59"}` (Mo/s, Go ; rôle d'une carte = service
systemd du processus qui y occupe le plus de VRAM ; `heure` = horloge du PC, l'écran n'en a pas).
Depuis le 08/11, chaque carte porte cinq champs de plus, publiés par `telemetrie-pc` (`/config/telemetrie-pc/pc.py`) : `ps` (chaîne
« P0 »..« P12 », état d'alimentation), `f` et `fm` (MHz courants du GPU et de la mémoire), `f_max` et `fm_max` (maxima de la carte, `null` si
inconnus). Relevé réel du 08/11 vers 19 h : 3090 P2 1500/9626 (max 2115/9876) ; 3060 P2 1500/8401 (max 2310/8601) ; 3090' P2 1500/9626 (max 2100/9876).
L'écran affiche `ps` à côté des watts (« 53° · 99 W · P2 ») et les deux fréquences actuelles, chacune devant son icône (maquette validée par
08/11, prod 8.4 `0c1a947`) ; **un champ absent ou `null` s'affiche « - »** — l'écran remet ses valeurs à zéro à chaque trame, il n'affiche
jamais une fréquence périmée. `f_max` et `fm_max` sont reçus et gardés, pas encore affichés.
**E** `pc/processus` (≈ 900 o) `[{"nom":"llama-server", "role":"llama-server-aurore", "cpu":0.0, "mem":1.9, "vram":28.4},…12]` (6 premiers en CPU,
3 en mémoire, 3 en VRAM ; rôle = service systemd, vide pour les applications du bureau).
L'écran passe en « PC en veille » quand plus rien n'arrive depuis 10 s.
**S** `dashboard_guition/pc/set` `{"action":"reveiller|veille"}` -> HA 1792200000028 (DANS HA le 24/10) : `switch.eveille_ordi_cachy` on (WoL) / off (veille habituelle).

### Page « IA » — `dashboard_guition/pc/ia` et `dashboard_guition/pc/ia_activite` (RETENUS ; agent du PC, page du rail écrite le 11/11/2026)
Publiés par l'agent du PC (Claude côté PC), pas par HA. Les deux sont **retenus** (l'écran les attend pour se
déclarer complet, comme `pc/ia` depuis le 31/10) et affichés par la page « IA » du rail, placée juste après PC
(maquette validée le 10/11). L'onglet « IA » de la page PC (31/10) est retiré.
**E** `pc/ia` (≈ 150 o) — format INCHANGÉ du 31/10, mais avec **UN SEUL compte**, celui en cours ; l'écran lit le
premier élément du tableau et ignore les autres :
`{"comptes":[{"nom":"compte-un@exemple.invalid", "h5":92, "h5_reset":1792710000, "semaine":13, "semaine_reset":1793228400, "maj":1792703000}]}`
(`h5`/`semaine` : % utilisés ; `h5_reset`/`semaine_reset` : heure Unix de la réinitialisation, 0 si pas de fenêtre ;
`maj` : heure Unix de la lecture par l'agent — si elle a plus de 10 min, l'écran écrit « lu il y a … »).
Les champs `claude_5h`/`claude_semaine` de `pc/etat` (24/10) ne sont lus par rien : l'usage Claude vit dans `pc/ia`.
**E** `pc/ia_activite` (≈ 400 o, NOUVEAU 11/11) — ce que fait l'agent Claude et ce qu'il a produit :
`{"etat":"reflechit|attente|repos", "alerte":"autorisation|question|", "tokens":612345, "outils":411, "prompts":14, "sessions":2, "histo":[30 entiers], "maj":1792703000}`
(`tokens` : tokens produits au total ; `outils` : appels d'outils ; `prompts` : prompts traités ; `sessions` : sessions
ouvertes ; `histo` : tokens produits par tranche de 10 min sur 5 h, **la plus ancienne d'abord**, la dernière = la
tranche en cours ; `maj` : heure Unix du message — 0 ou absent = l'écran met l'heure de sa réception).
Affichage : « réfléchit… » orange / « autorisation demandée » ou « question pour toi » rouges (l'alerte passe avant
l'état) / « au repos » gris. **Message absent ou vieux de plus de 2 min : « au repos »**, Clawd ferme les yeux, et les
derniers chiffres reçus restent affichés. L'écran ne publie rien sur ces deux sujets.

### Sonnette — `dashboard_guition/sonnette/etat`
**E** `{"sonne":1, "depuis":1792694300}` (retombe à 0 à l'ouverture de la porte) → l'écran montre la caméra si le réglage est actif.
DANS HA le 27/10 (1791000000040 : sonne 1 ; 041 : sonne 0), non retenu ; écran : `ui_sonnette` (plein écran, 60 s, toucher).

### Carillon — `dashboard_guition/carillon/etat`, `/set`
**E** `{"melodie":0, "volume":50}` ; **S** `{"action":"regler", "melodie":1}` / `{"action":"regler", "volume":60}`

## 4. Taille des messages

L'ancien firmware (PubSubClient, tampon par défaut de 256 octets) **ignore en silence** tout message plus grand, sujet compris
(vu le 17/10 : `dashboard/infos/etat` à 291 octets n'arrivait plus). Le module `ui_mqtt` du nouveau firmware agrandit ce tampon
(`setBufferSize`, 4 Ko au moins) : les listes de la musique et des fichiers de l'imprimante dépassent largement 256 octets.

## 5. Ce qui est propre à chaque écran — `dashboard_guition/appareil/<nom>/...` (codé le 24/10 : `ui_appareil.cpp`)

| Sujet | Sens | Contenu |
|---|---|---|
| `statut` | écran → HA, retenu, **dernière volonté** | `en_ligne` / `hors_ligne` |
| `infos` | écran → HA, retenu, toutes les 60 s | `{"depuis":"2 h 05 min", "redemarrage":"mise sous tension", "ram":180, "psram":7000, "cpu_temp":48, "version":"Oct 24 2026", "wifi":"mon_reseau", "ip":"192.168.1.41", "rssi":-60}` (Ko) |
| `reglages` | écran → HA, retenu, au démarrage et à chaque changement | `{"lum_jour":75, "lum_nuit":30, "veille_s":60, "piece":"cuisine", "retour_s":60, "camera":1}` (veille / retour : 0 = jamais) |
| `reglages/set` | HA → écran | mêmes clés (toutes facultatives), pour régler un écran depuis HA ; appliqué, gardé, republié |
| `journal` | écran → HA, non retenu | `{"texte":"Démarrage (mise sous tension) · données en 4,2 s", "alerte":0}` : démarrage, Wi-Fi, MQTT, HA muet ; inscrit dans le journal de bord de HA par 1792200000032 (« Écran <nom> », ⚠️ si alerte) |
| `reveil` | écran → HA | (à faire, radar) `{"cause":"radar|minuterie|froid", …}` |

Heure de l'écran : celle de HA (`maj` de `maison/etat`), comme la production (pas de NTP), fuseau CET/CEST ; réglée au 1er message,
puis seulement si elle retarde de plus d'une minute (un retenu peut dater de 5 min). Les réglages de l'écran sont gardés dans sa mémoire flash (comme la production, qui les écrivait dans LittleFS). Rétroéclairage, fondu d'1 s comme la production :
`lum_nuit` quand `maison/etat` dit nuit, éteint après `veille_s` sans toucher.

### Réglages de la maison et appareils (HA, les mêmes pour tous les écrans)
**E** `dashboard_guition/maison/reglages` (1792200000029, retenu) `{"melodie":"Mario", "melodies":["Mario","Simpsons","Halloween","Noël"], "volume":40,
"terrasse_porte":1, "terrasse_mouvement":1}` ; **S** `maison/reglages/set` `{"action":"melodie","nom":"Simpsons"}`, `{"action":"volume","volume":60}`,
`{"action":"terrasse_porte|terrasse_mouvement","on":0|1}` (active / désactive les automatisations d'allumage temporisé).
**E** `dashboard_guition/maison/appareils` (1792200000030, retenu, chaque minute) `{"liste":[["Bouilloire","Cuisine",0],…]}` = [nom (30 car.), pièce,
en ligne] des appareils ESPHome + Zigbee de HA, sauf l'étiquette « Écran : ignorer » ; hors ligne en tête puis par pièce (≈ 2 Ko).
Liaisons de la page Infos : Wi-Fi (l'écran), Home Assistant (un message de HA depuis moins de 3 min), puis cette liste.

## 6. Migration

1. Valider ce document.
2. HA : créer les automatisations `dashboard_guition/…` (état + commandes) **à côté** des anciennes `dashboard/…`, page par page ; vérifier avec une lecture seule du broker.
3. Écran : module unique `src/ui_lvgl/ui_mqtt.cpp` (JSON → données des pages, commandes → `set`), testé au simulateur sur un relevé réel du broker.
4. Flash du banc à la maison, essai page par page.
5. Quand tous les écrans ont la nouvelle interface : supprimer les automatisations `dashboard/…` et `thermostat/tele/…`, vider les retenus morts
   (`RegPID_poele/#`, `Distributeur_croquettes/#`, `thermostat/tele/aqm`).

## 7. Décisions

- Préfixe : `dashboard_guition/` (22/10).
- Clés : **français sans accent partout** (22/10) : l'écran ne connaît pas le vocabulaire de HA (`fan_level`,
  `hvac_action`…), c'est l'automatisation qui traduit ; les noms sont les mêmes du JSON jusqu'au code de la page.
- Heures : **minutes depuis minuit** (22/10) : un entier, aucun texte à analyser sur l'ESP, calculs immédiats ;
  côté HA `{{ h * 60 + m }}`. Les instants (dernier réveil, « depuis ») restent en horodatage Unix.
