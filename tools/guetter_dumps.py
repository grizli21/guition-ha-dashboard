#!/usr/bin/env python3
"""
24/10/2026 (retour d'usage : « le dump, récupère-le à la volée ») : guette les plantages des écrans sans attendre qu'on le dise.
Lecture SEULE du broker : abonné à dashboard_guition/appareil/+/dump (publié retenu par l'écran au redémarrage qui suit
un plantage). Le dump déjà retenu à l'abonnement est ignoré ; chaque nouveau est imprimé et gardé dans dumps/, ses piles
d'appels (« Backtrace: ») décodées en fonctions avec le firmware du banc (.pio/build/ecran-usb/firmware.elf :
il doit être celui qui est flashé).

    python3 tools/guetter_dumps.py   (besoin de paho-mqtt 1.x)

Identifiants : ceux du projet (src/secrets_mqtt.h), jamais affichés.
"""
import os
import re
import subprocess
import time

import paho.mqtt.client as mqtt

SECRETS = os.path.join(os.path.dirname(__file__), "..", "src", "secrets_mqtt.h")
RACINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DOSSIER = os.path.join(RACINE, "dumps")
ELF = os.path.join(RACINE, ".pio/build/ecran-usb/firmware.elf")
ADDR2LINE = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line")


def decoder(texte):
    """Chaque ligne « Backtrace: 0xPC:0xSP … » suivie de ses fonctions."""
    sortie = []
    for ligne in texte.splitlines():
        sortie.append(ligne)
        if "Backtrace" in ligne and os.path.exists(ELF):
            pcs = re.findall(r"(0x4[0-9a-fA-F]{7}):0x", ligne)
            if pcs:
                r = subprocess.run([ADDR2LINE, "-pfiaC", "-e", ELF] + pcs, capture_output=True, text=True)
                sortie += ["    " + l.replace(RACINE + "/", "") for l in r.stdout.splitlines()]
    return "\n".join(sortie)


def secret(nom, texte):
    return re.search(r'#define\s+' + nom + r'\s+"([^"]*)"', texte).group(1)


def recu(client, _, m):
    if m.retain:  # l'ancien dump, déjà lu
        return
    nom = m.topic.split("/")[2]
    os.makedirs(DOSSIER, exist_ok=True)
    chemin = os.path.join(DOSSIER, f"{time.strftime('%Y%m%d-%H%M%S')}_{nom}.txt")
    texte = decoder(m.payload.decode(errors="replace"))
    with open(chemin, "w") as f:
        f.write(texte)
    print(f"=== DUMP {nom} {time.strftime('%H:%M:%S')} ({chemin}) ===", flush=True)
    print(texte, flush=True)


texte = open(SECRETS).read()
c = mqtt.Client()  # paho 1.x (venv ESPHome)
c.username_pw_set(secret("MQTT_UTILISATEUR", texte), secret("MQTT_MOT_DE_PASSE", texte))
c.on_connect = lambda client, u, f, rc: (client.subscribe("dashboard_guition/appareil/+/dump"),
                                            print(f"[guet] abonné ({rc})", flush=True))
c.on_message = recu
c.connect(secret("MQTT_SERVEUR", texte), 1883)
c.loop_forever()
