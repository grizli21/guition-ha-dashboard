#!/usr/bin/env python3
"""Zoom plus proche voisin d'une zone d'une capture du simulateur (comparaison de liserés d'icônes).
Usage : zoom_png.py entree.png sortie.png x0 y0 x1 y1 [facteur=2]"""
import sys

from PIL import Image


def main():
    e, s = sys.argv[1], sys.argv[2]
    x0, y0, x1, y1 = (int(v) for v in sys.argv[3:7])
    f = int(sys.argv[7]) if len(sys.argv) > 7 else 2
    im = Image.open(e).convert("RGB").crop((x0, y0, x1, y1))
    im.resize((im.width * f, im.height * f), Image.NEAREST).save(s)
    print("%s : zone %dx%d -> %dx%d x%d" % (s, im.width, im.height, im.width * f, im.height * f, f))


if __name__ == "__main__":
    main()
