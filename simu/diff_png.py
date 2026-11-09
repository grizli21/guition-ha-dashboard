#!/usr/bin/env python3
"""05/11/2026 : compare deux PNG du simulateur (480x480 RGB, sans filtre, IDAT unique)
et affiche ou se situent les pixels qui different (boite englobante + nombre).
Usage : python3 diff_png.py a.png b.png [max_lignes]"""
import sys, zlib

def decode(chemin):
    d = open(chemin, 'rb').read()
    pos, idat, largeur, hauteur = 8, b'', 0, 0
    while pos < len(d):
        n = int.from_bytes(d[pos:pos+4], 'big'); typ = d[pos+4:pos+8]; corps = d[pos+8:pos+8+n]
        if typ == b'IHDR':
            largeur, hauteur = int.from_bytes(corps[0:4], 'big'), int.from_bytes(corps[4:8], 'big')
        elif typ == b'IDAT':
            idat += corps
        pos += 12 + n
    brut = zlib.decompress(idat)
    rp = largeur * 3 + 1
    assert all(brut[y*rp] == 0 for y in range(hauteur)), 'filtre PNG inattendu'
    return largeur, hauteur, brut, rp

def main():
    a, b = decode(sys.argv[1]), decode(sys.argv[2])
    if a[0:2] != b[0:2]:
        print('tail differente'); return
    L, H, ba, rp = a
    bb = b[2]
    n = 0; x1 = y1 = 10**9; x2 = y2 = -1; lignes = []
    for y in range(H):
        for x in range(L):
            i = y * rp + 1 + 3 * x
            if ba[i:i+3] != bb[i:i+3]:
                n += 1
                x1, y1 = min(x1, x), min(y1, y); x2, y2 = max(x2, x), max(y2, y)
                if len(lignes) < int(sys.argv[3] if len(sys.argv) > 3 else 8):
                    lignes.append((x, y, tuple(ba[i:i+3]), tuple(bb[i:i+3])))
    print(f'pixels differents : {n} / {L*H}   englobant : ({x1},{y1})-({x2},{y2})'
          if n else 'aucune difference')
    for x, y, ca, cb in lignes:
        print(f'  ({x},{y}) avant {ca} -> apres {cb}')

main()
