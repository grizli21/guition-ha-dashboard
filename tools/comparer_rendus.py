#!/usr/bin/env python3
"""Comparer deux dossiers de captures du simulateur (audit « rien n'a changé au rendu »).

    tools/comparer_rendus.py dossier_A dossier_B [--pixels]

PNG par PNG : d'abord les OCTETS (identique = identique, le plus rapide). Avec --pixels,
les fichiers différents sont comparés au PIXEL (boîte englobante + nombre de pixels différents)
— décodage PNG sans dépendance, mêmes hypothèses que simu/diff_png.py (RGB, filtre 0).
Les .tsv (journal d'audit du simulateur) sont comparés comme texte.
Sortie : liste des captures identiques (silencieuse), différentes (nom + écart), manquantes.
Code retour : 0 si tout est identique, 1 sinon.

Sert à établir la liste des captures INSTABLES (sensibles au temps : animations, horloge)
en comparant deux runs du code INCHANGÉ, puis à prouver qu'une refactorisation ne change
rien : tout doit être identique SAUF la liste des instables connue.
"""
import os, sys, zlib

def lire_png(chemin):
    d = open(chemin, 'rb').read()
    pos, idat, L, H = 8, b'', 0, 0
    while pos < len(d):
        n = int.from_bytes(d[pos:pos+4], 'big'); typ = d[pos+4:pos+8]; corps = d[pos+8:pos+8+n]
        if typ == b'IHDR':
            L, H = int.from_bytes(corps[0:4], 'big'), int.from_bytes(corps[4:8], 'big')
        elif typ == b'IDAT':
            idat += corps
        pos += 12 + n
    brut = zlib.decompress(idat)
    rp = L * 3 + 1
    assert all(brut[y*rp] == 0 for y in range(H)), 'filtre PNG inattendu : utiliser PIL'
    return L, H, brut, rp

def diff_pixels(a, b):
    (La, Ha, ba, rpa), (Lb, Hb, bb, rpb) = lire_png(a), lire_png(b)
    if (La, Ha) != (Lb, Hb):
        return 'tailles %dx%d vs %dx%d' % (La, Ha, Lb, Hb)
    n = 0; x1 = y1 = 10**9; x2 = y2 = -1
    for y in range(Ha):
        oa, ob = y*rpa+1, y*rpb+1
        if ba[oa:oa+rpa-1] != bb[ob:ob+rpb-1]:
            for x in range(La):
                i, j = oa+3*x, ob+3*x
                if ba[i:i+3] != bb[j:j+3]:
                    n += 1
                    x1 = min(x1, x); x2 = max(x2, x); y1 = min(y1, y); y2 = max(y2, y)
    if n == 0:
        return 'identique au pixel (octets différents : métadonnées)'
    return '%d px différents, boîte (%d,%d)-(%d,%d) sur %dx%d' % (n, x1, y1, x2, y2, La, Ha)

def noms(d):
    return {f for f in os.listdir(d) if f.endswith(('.png', '.tsv'))}

def main():
    args = [a for a in sys.argv[1:] if a != '--pixels']
    pixels = '--pixels' in sys.argv[1:]
    if len(args) != 2:
        print(__doc__); return 2
    A, B = args
    na, nb = noms(A), noms(B)
    diffs, instables = [], []
    for f in sorted(na | nb):
        if f not in na or f not in nb:
            diffs.append((f, 'MANQUANTE dans ' + (B if f not in nb else A)))
            continue
        ba, bb = open(os.path.join(A, f), 'rb').read(), open(os.path.join(B, f), 'rb').read()
        if ba == bb:
            continue
        if f.endswith('.tsv'):
            la, lb = ba.decode().splitlines(), bb.decode().splitlines()
            n = sum(1 for x, y in zip(la, lb) if x != y)
            diffs.append((f, 'texte : %d lignes diffèrent' % n))
        elif pixels:
            diffs.append((f, diff_pixels(os.path.join(A, f), os.path.join(B, f))))
        else:
            diffs.append((f, 'octets différents (ajouter --pixels pour le détail)'))
    identiques = len(na & nb) - len(diffs)
    print('%d captures : %d identiques, %d différentes' % (len(na | nb), identiques, len(diffs)))
    for f, why in diffs:
        print('  %s : %s' % (f, why))
    return 1 if diffs else 0

sys.exit(main())
