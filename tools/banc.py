#!/usr/bin/env python3
"""
24/10/2026 : piloter le banc (firmware de l'écran, env « ecran ») depuis le PC, comme le simulateur : capture de l'écran en PNG et
appuis simulés (src/ecran/banc_distant.cpp, port 8080, réservé au PC défini par RESEAU_PC dans secrets_wifi.h).

    python3 tools/banc.py capture ecran.png            l'image affichée
    python3 tools/banc.py toucher 130 24 [ms]          un appui (coordonnées de l'écran, comme le simulateur)
    python3 tools/banc.py glisser 200 400 200 100 [ms] un glissé
    python3 tools/banc.py journal [secondes]           le journal série en direct (Telnet :23, sans redémarrer l'écran)
    python3 tools/banc.py dump                         le plantage précédent (dernières traces, étape en cours)
    python3 tools/banc.py traces                       les dernières traces de la session en cours
    python3 tools/banc.py figer                        fige la boucle exprès (essai du chien de garde : redémarre)
    python3 tools/banc.py stress [s]                   bouton large dont le style bascule à chaque image (aucun toucher)
    python3 tools/banc.py sonnette [0|1]               vue caméra de la sonnette (27/10 ; sans faire sonner le carillon)
    python3 tools/banc.py sync 0|1                     recopie aussitôt entre les deux mémoires d'image (27/10, essai A/B)

Banc : variable BANC (défaut : IP de l'écran du banc, ex. 192.168.1.41). ⚠️ Un appui simulé agit comme un vrai doigt : les boutons commandent
VRAIMENT les appareils.
"""
import codecs
import os
import socket
import sys
import time
import urllib.request

from PIL import Image

BANC = os.environ.get("BANC", "192.168.1.41")

# 05/11 soir (incident : gestes partis sur la cuisine par oubli de BANC) : une commande qui change l'etat de
# l'ecran exige une cible EPINGLEE a la main. Le defaut 191 reste pour la lecture seule (capture, journal).
DANGEREUX = ("toucher", "glisser", "stress", "dodo", "sync", "sonnette")
if len(sys.argv) > 1 and sys.argv[1] in DANGEREUX and "BANC" not in os.environ:
    sys.exit("BANC doit pointer explicitement sur l'ecran vise (191 = cuisine, 193 = chambre 1) : geste refuse")

L = H = 480


def appeler(chemin, delai=10):
    # 05/11 : le banc repond 403 quand un geste est refuse par le verrou firmware — on rend le message, pas une trace.
    try:
        with urllib.request.urlopen(f"http://{BANC}:8080{chemin}", timeout=delai) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        motif = e.read().decode(errors="replace").strip()
        sys.exit("banc %s a refuse (%d) : %s" % (BANC, e.code, motif or e.reason))


def capture(sortie):
    brut = appeler("/ecran", 40)
    if len(brut) != L * H * 2:
        sys.exit(f"capture incomplète : {len(brut)} octets au lieu de {L * H * 2}")
    img = Image.new("RGB", (L, H))
    px = []
    for i in range(0, len(brut), 2):
        v = brut[i] << 8 | brut[i + 1]  # RGB565, octets inversés (LV_COLOR_16_SWAP)
        r, g, b = v >> 11 & 31, v >> 5 & 63, v & 31
        px.append((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    img.putdata(px)
    img.save(sortie)
    print(sortie)


def telnet(secondes, commande=None):
    s = socket.create_connection((BANC, 23), timeout=5)
    s.settimeout(0.3)
    if commande:
        s.sendall(commande.encode() + b"\n")
    fin = time.time() + secondes
    utf8 = codecs.getincrementaldecoder("utf-8")(errors="replace")  # un accent peut être coupé entre deux paquets
    while time.time() < fin:
        try:
            b = s.recv(4096)
            if not b:
                break
            sys.stdout.write(utf8.decode(b))
            sys.stdout.flush()
        except socket.timeout:
            pass
    sys.stdout.write(utf8.decode(b"", final=True))
    s.close()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, a = sys.argv[1], sys.argv[2:]
    if cmd == "capture" and len(a) == 1:
        capture(a[0])
    elif cmd == "toucher" and len(a) in (2, 3):
        ms = a[2] if len(a) == 3 else 150
        print(appeler(f"/toucher?x={a[0]}&y={a[1]}&ms={ms}").decode().strip())
    elif cmd == "glisser" and len(a) in (4, 5):
        ms = a[4] if len(a) == 5 else 400
        print(appeler(f"/glisser?x0={a[0]}&y0={a[1]}&x1={a[2]}&y1={a[3]}&ms={ms}").decode().strip())
    elif cmd == "journal" and len(a) <= 1:
        telnet(float(a[0]) if a else 10)
    elif cmd == "sonnette" and len(a) <= 1:
        print(appeler(f"/sonnette?etat={a[0] if a else 1}").decode().strip())
    elif cmd == "sync" and len(a) == 1:
        print(appeler(f"/sync?v={a[0]}").decode().strip())
    elif cmd == "stress" and len(a) <= 1:
        print(appeler(f"/stress?s={a[0] if a else 60}").decode().strip())
    elif cmd in ("dump", "traces", "figer") and not a:
        telnet(3, cmd)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
