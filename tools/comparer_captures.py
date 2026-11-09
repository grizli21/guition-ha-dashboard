#!/usr/bin/env python3
"""Comparer une capture du simulateur (PNG) avec une capture de l'ECRAN (.raw RVB565 natif petit
boutiste + son PNG, pris par le banc HTTP /ecran). 08/11/2026 (chantier simulateur 9.6).

    tools/comparer_captures.py simu.png ecran.raw|png [sortie.png]

Ce qu'il dit, par canal et en tout : écart moyen, écart max, part de pixels à plus de 8/255.
Il écrit aussi une planche : simulateur | écran | écart amplifié x8 (le noir = identique).
Les DEUX images ne montrent pas les mêmes données (le simulateur est fictif, l'écran reçoit le vrai
MQTT) : ce qu'on vérifie ici, ce sont les positions, les tailles et les COULEURS — pas le contenu.
"""
import sys
from PIL import Image

def lire(chemin):
    if chemin.endswith('.raw'):
        brut = open(chemin, 'rb').read()
        n = 480 * 480
        assert len(brut) == n * 2, '%s : %d octets, attendu %d' % (chemin, len(brut), n * 2)
        im = Image.new('RGB', (480, 480))
        px = []
        for i in range(0, n * 2, 2):
            v = brut[i] | (brut[i + 1] << 8)          # RVB565 natif, petit boutiste (contrôlé 08/11)
            px.append((((v >> 11) * 255 + 16) // 32, (((v >> 5) & 63) * 255 + 32) // 64, ((v & 31) * 255 + 16) // 32))
        im.putdata(px)
        return im
    return Image.open(chemin).convert('RGB')

def main():
    a, b = lire(sys.argv[1]), lire(sys.argv[2])
    sortie = sys.argv[3] if len(sys.argv) > 3 else None
    if a.size != b.size:
        print('TAILLES DIFFÉRENTES : %s vs %s' % (a.size, b.size))
        return 1
    pa, pb = list(a.getdata()), list(b.getdata())
    ecarts = [abs(x - y) for p, q in zip(pa, pb) for x, y in zip(p, q)]
    n = len(ecarts)
    gros = sum(1 for e in ecarts if e > 8)
    print('%-46s écart moyen %.2f/255  max %d  pixels à >8/255 : %.2f %%'
          % (sys.argv[1].split('/')[-1] + ' vs ' + sys.argv[2].split('/')[-1], sum(ecarts) / n, max(ecarts),
             100.0 * gros / n))
    for k, nom in enumerate('RVB'):
        s = [e for i, e in enumerate(ecarts) if i % 3 == k]
        print('   %s : moyen %.2f  max %d' % (nom, sum(s) / len(s), max(s)))
    if sortie:
        l, h = a.size
        planche = Image.new('RGB', (l * 3 + 20, h), (12, 12, 14))
        ecart = Image.new('RGB', (l, h))
        pts = []
        for p, q in zip(pa, pb):
            pts.append((min(255, abs(p[0] - q[0]) * 8), min(255, abs(p[1] - q[1]) * 8), min(255, abs(p[2] - q[2]) * 8)))
        ecart.putdata(pts)
        planche.paste(a, (0, 0)); planche.paste(b, (l + 10, 0)); planche.paste(ecart, (2 * l + 20, 0))
        planche.save(sortie)
        print('   planche : %s (simulateur | écran | écart x8)' % sortie)
    return 0

if __name__ == '__main__':
    sys.exit(main())
