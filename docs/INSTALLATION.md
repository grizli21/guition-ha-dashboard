# Installation

**English** | [Français](en/INSTALLATION.md)

[← Retour au README](../README.fr.md)

Tout ce qu'il faut pour passer du dépôt cloné à un écran qui parle à Home Assistant.

## Prérequis

- [PlatformIO Core](https://platformio.org/install) (CLI `pio`).
- Un PC Linux avec `g++` et `zlib` pour le simulateur ; `python3` (et `pillow` pour `tools/banc.py`).
- Un broker MQTT et Home Assistant avec l'intégration MQTT.

## 1. Configurer les secrets

Les fichiers de secrets ne sont **pas** dans le dépôt — copiez les gabarits et remplissez-les :

```bash
cp src/secrets_mqtt.h.exemple  src/secrets_mqtt.h          # broker, utilisateur, mot de passe
cp src/secrets_ma.h.exemple    src/secrets_ma.h            # serveur et jeton Music Assistant
cp src/ecran/secrets_wifi.h.exemple src/ecran/secrets_wifi.h  # SSID, mot de passe, plan d'IP local
```

## 2. Déclarer ses écrans

Dans [src/ecran/ecran.cpp](../src/ecran/ecran.cpp), la table `ECRANS[]` associe chaque écran à sa MAC :

```c
static const ecran_t ECRANS[] = {
    // { MAC (placeholder), dernier octet de l'IP, pièce, rotation, batterie }
    {{0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x01}, 10, "cuisine",  0, false},
    {{0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x02}, 11, "sejour",   0, true },
    // … remplacez les MAC par celles de vos écrans — la trace [ecran] du premier démarrage les affiche (pio device monitor)
};
```

Les adresses du dépôt sont des **placeholders** : mettez vos MAC, et l'IP statique se construit
avec `RESEAU_OCTET_1/2/3` + le dernier octet déclaré.

## 3. Compiler et flasher

```bash
pio device list                                             # trouve ton port série (ttyACM0 ou ttyUSB0 selon le câble)
pio run -e ecran-usb -t upload --upload-port /dev/ttyACM0    # premier flash : par câble (ou /dev/ttyUSB0)
pio run -e ecran -t upload --upload-port 192.168.1.41        # ensuite : OTA, sans câble
pio device monitor                                          # journal série, 115 200 bauds
```

L'OTA passe par `ArduinoOTA` (protocole d'upload `espota` du dépôt, 15 s de timeout) : une fois le
premier flash fait par câble, plus besoin d'ouvrir l'écran. L'environnement `banc-chambre1-v9` est
réservé aux mesures de rendu — ne pas le flasher sans savoir pourquoi.

**Langue** : l'interface est en français par défaut. Pour l'anglais, décommentez `-D UI_LANG_EN` dans
`platformio.ini` (au simulateur : `make -C simu UI_LANG_EN=1`). Le dialogue MQTT avec Home Assistant ne
change pas : seuls les textes affichés sont traduits.

## Mode démo : essayer sans Home Assistant

Copiez les trois gabarits de secrets (étape 1) **sans les remplir**, décommentez `-D MODE_DEMO` dans
`platformio.ini` et flashez par câble (`ecran-usb`) : pas d'écran à déclarer. L'écran ne cherche ni Wi-Fi
ni MQTT ; au démarrage, il rejoue un jeu de
messages MQTT embarqué ([src/ui_lvgl/ui_demo.cpp](../src/ui_lvgl/ui_demo.cpp)) par le même code de lecture
qu'avec Home Assistant, et toutes les pages se remplissent (heure fixée à 21:30). Les boutons du chauffage,
de la VMC, des lumières, des ouvrants, de l'eau chaude, du poêle et du café répondent comme si Home
Assistant renvoyait l'état ; la musique répond à un faux Music Assistant. Restent vides : caméras,
pochettes et logos de radios (ils arrivent par le réseau). Au simulateur : `make -C simu MODE_DEMO=1`.

## 4. Home Assistant

Le contrat complet (topics, JSON, conventions) tient dans [CONTRAT_MQTT.md](../CONTRAT_MQTT.md) :
deux automatisations par page (« Écran · page · état » qui publie `etat` en retain, « Écran · page ·
commandes » qui exécute les `set`). [ha_propositions/](../ha_propositions/) contient **23 automatisations
prêtes à l'emploi** pour HA (les miennes, adaptées : remplacez les entités d'exemple par les vôtres).
La page Musique ne passe pas par MQTT : l'écran parle directement à **Music Assistant** (HTTP + jeton).

## 5. Simulateur PC

```bash
make -C simu                 # compile le VRAI code des pages, joue le scénario, écrit simu/captures/*.png
make -C simu asan            # mêmes pages sous AddressSanitizer / UBSan
python3 simu/galerie.py galerie.html   # assemble les captures en une page HTML consultable
```

Il rejoue les messages MQTT d'un relevé réel anonymisé et produit ~280 captures, dont les images
de cette page. Le détail est dans [docs/DEVELOPPEMENT.md](DEVELOPPEMENT.md#simulateur).
