#!/usr/bin/env python3
"""Convertisseur d'icônes -> descripteurs LVGL 8.4 (lv_img_dsc_t) pour le Guition 4848S040.

Entrées acceptées (mélangeables) :
  *.svg                      rendu via rsvg-convert (ou ImageMagick en secours)
  *.png *.jpg *.bmp ...      tout ce que Pillow lit
  icons.h (ancien Lovyan)    tableaux `const uint8_t xxx_Icon[] PROGMEM` 1 bit/pixel (drawBitmap)

Formats de sortie (--fmt) :
  a4   LV_IMG_CF_ALPHA_4BIT  16 niveaux d'alpha, recolorable (img_recolor / style_img_recolor), 0,5 o/px  [défaut]
  a8   LV_IMG_CF_ALPHA_8BIT  256 niveaux d'alpha, recolorable, 1 o/px
  a1   LV_IMG_CF_ALPHA_1BIT  0/1, recolorable, 1/8 o/px
  rgb  LV_IMG_CF_TRUE_COLOR_ALPHA  couleurs RGB565 + alpha (icône multicolore), 3 o/px

Les icônes recolorables (a1/a4/a8) ne gardent que la FORME : la couleur se met côté code
  lv_obj_set_style_img_recolor(img, lv_color_hex(0x99FE99), 0);
  lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);

Exemples :
  icon2lvgl.py -o lib/icones -s 64 lampe.svg flamme.png
  icon2lvgl.py -o lib/icones --fmt rgb -s 48 logo.png
  icon2lvgl.py -o lib/icones --legacy --smooth --soft 0.5 src/icons.h # toutes les icônes Lovyan (réglage retenu)
  icon2lvgl.py --legacy --smooth --soft 0.5 -s 28 --suffix _28 --only tempEXT_Icon,humiEXT_Icon \
      --header lib/icones/icones_28.h src/icons.h            # versions réduites (28 px)
  icon2lvgl.py -o lib/icones --legacy src/icons.h --only flamme_Icon,wifi3_Icon
  icon2lvgl.py -o lib/icones --luma --invert -s 64 scan_noir_sur_blanc.png
"""
import argparse, io, re, shutil, subprocess, sys
from pathlib import Path
from PIL import Image, ImageFilter

HEADER = """#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_{U}
#define LV_ATTRIBUTE_IMG_{U}
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_{U} uint8_t {n}_map[] = {{
"""
FOOTER = """}};

const lv_img_dsc_t {n} = {{
  .header.cf = {cf},
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = {size},
  .data = {n}_map,
}};
"""
CF = {"a1": "LV_IMG_CF_ALPHA_1BIT", "a4": "LV_IMG_CF_ALPHA_4BIT",
      "a8": "LV_IMG_CF_ALPHA_8BIT", "rgb": "LV_IMG_CF_TRUE_COLOR_ALPHA"}


def ident(s):
    s = re.sub(r"\W", "_", s).strip("_").lower()
    return s if not s[:1].isdigit() else "i_" + s


def load_svg(path, size):
    if shutil.which("rsvg-convert"):
        cmd = ["rsvg-convert", "-a", "-w", str(size), "-h", str(size), str(path)]
        # -a garde le ratio : la plus grande dimension tient dans size
        png = subprocess.run(cmd, check=True, capture_output=True).stdout
        return Image.open(io.BytesIO(png)).convert("RGBA")
    if shutil.which("magick"):
        cmd = ["magick", "-background", "none", "-density", "384", str(path),
               "-resize", f"{size}x{size}", "png:-"]
        return Image.open(io.BytesIO(subprocess.run(cmd, check=True, capture_output=True).stdout)).convert("RGBA")
    sys.exit("SVG : installer rsvg-convert ou ImageMagick")


def fit(img, size, pad):
    """Réduit en gardant le ratio dans size x size (marge pad px) ; reste de taille native si size=0."""
    if not size:
        return img
    inner = max(1, size - 2 * pad)
    img = img.copy()
    img.thumbnail((inner, inner), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(img, ((size - img.width) // 2, (size - img.height) // 2))
    return canvas


def load_raster(path, size, pad, luma, invert):
    img = Image.open(path).convert("RGBA")
    if luma:  # icône sans alpha (noir sur blanc...) : la luminance devient l'alpha
        g = img.convert("L")
        if not invert:
            g = Image.eval(g, lambda v: 255 - v)  # sombre = opaque
        img = Image.merge("RGBA", (*(Image.new("L", g.size, 255),) * 3, g))
    return fit(img, size, pad)


def parse_legacy(path, only, size_override):
    """Tableaux 1 bit MSB d'abord (Adafruit drawBitmap). Taille : commentaire //WxH, sinon --size, sinon carré déduit."""
    txt = Path(path).read_text(errors="replace")
    pat = re.compile(r"const\s+uint8_t\s+(\w+)\s*\[\]\s*(?:PROGMEM)?\s*=\s*\{\s*(?://\s*(\d+)\s*x\s*(\d+))?(.*?)\};", re.S)
    out = []
    for m in pat.finditer(txt):
        name, w, h, body = m.group(1), m.group(2), m.group(3), m.group(4)
        if only and name not in only:
            continue
        data = bytes(int(x, 16) for x in re.findall(r"0[xX]([0-9a-fA-F]{1,2})", body))
        if w:
            w, h = int(w), int(h)
        elif size_override:
            w = h = size_override
        else:
            side = int((len(data) * 8) ** 0.5)
            w = h = next((s for s in range(side, 7, -1) if (s + 7) // 8 * s == len(data)), 0)
        if not w or (w + 7) // 8 * h != len(data):  # commentaire absent ou faux : carré déduit de la taille
            side = int((len(data) * 8) ** 0.5)
            sq = next((t for t in range(side, 7, -1) if (t + 7) // 8 * t == len(data)), 0)
            if not sq:
                print(f"  ! {name}: {len(data)} o ne forment pas un carré (utiliser --legacy-size)", file=sys.stderr)
                continue
            if w:
                print(f"  ~ {name}: commentaire {w}x{h} faux, {len(data)} o = {sq}x{sq}", file=sys.stderr)
            w = h = sq
        stride = (w + 7) // 8
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        px = img.load()
        for y in range(h):
            for x in range(w):
                if data[y * stride + x // 8] & (0x80 >> (x % 8)):
                    px[x, y] = (255, 255, 255, 255)
        out.append((re.sub(r"_?[iI]con$", "", name), img))
    return out


def smooth(img, soft=0.0, f=8):
    """Anticrénelage d'un bitmap 1 bit : agrandi x8, flou, seuil (contour lissé), puis réduit par moyenne.
    Les bords gagnent des niveaux d'alpha intermédiaires, les droites restent nettes.
    soft > 0 : flou final de ce rayon (px) -> liseré doux sur TOUS les bords, droites comprises (≈0.5 : bord à ~50 %)."""
    a = img.getchannel("A")
    w, h = a.size
    big = a.resize((w * f, h * f), Image.NEAREST).filter(ImageFilter.GaussianBlur(f * 0.6))
    big = big.point(lambda v: 255 if v >= 128 else 0)
    alpha = big.resize((w, h), Image.BOX)
    if soft > 0:
        alpha = alpha.filter(ImageFilter.GaussianBlur(soft))
    out = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    out.putalpha(alpha)
    return out


def encode(img, fmt, swap):
    w, h = img.size
    px = img.load()
    buf = bytearray()
    if fmt == "a8":
        for y in range(h):
            buf += bytes(px[x, y][3] for x in range(w))
    elif fmt == "a4":
        for y in range(h):
            row = [px[x, y][3] >> 4 for x in range(w)]
            if w % 2:
                row.append(0)
            buf += bytes((row[i] << 4) | row[i + 1] for i in range(0, len(row), 2))
    elif fmt == "a1":
        for y in range(h):
            for x0 in range(0, w, 8):
                b = 0
                for k in range(8):
                    if x0 + k < w and px[x0 + k, y][3] >= 128:
                        b |= 0x80 >> k
                buf.append(b)
    else:  # rgb : RGB565 puis alpha ; swap = octet de poids fort d'abord (LV_COLOR_16_SWAP 1)
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                buf += bytes((c >> 8, c & 0xFF) if swap else (c & 0xFF, c >> 8))
                buf.append(a)
    return bytes(buf)


def write_c(outdir, name, img, fmt, swap):
    data = encode(img, fmt, swap)
    n = "icon_" + name if not name.startswith("icon_") else name
    lines = []
    for i in range(0, len(data), 16):
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ", ")
    txt = HEADER.format(U=n.upper(), n=n) + "\n".join(lines) + "\n" + \
        FOOTER.format(n=n, cf=CF[fmt], w=img.width, h=img.height, size=len(data))
    (outdir / f"{n}.c").write_text(txt)
    return n, len(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="svg / png / ... / icons.h (avec --legacy)")
    ap.add_argument("-o", "--outdir", default=str(Path(__file__).resolve().parent.parent / "lib" / "icones"), help="dossier de sortie des .c (défaut : lib/icones)")
    ap.add_argument("-s", "--size", type=int, default=0, help="côté carré en px (0 = taille native du raster ; SVG : 64)")
    ap.add_argument("--fmt", choices=CF, default="a4")
    ap.add_argument("--pad", type=int, default=0, help="marge transparente en px")
    ap.add_argument("--luma", action="store_true", help="image sans alpha : luminance -> alpha (sombre = opaque)")
    ap.add_argument("--invert", action="store_true", help="avec --luma : clair = opaque")
    ap.add_argument("--legacy", action="store_true", help="les entrées .h sont des tableaux 1 bit Lovyan")
    ap.add_argument("--only", default="", help="legacy : noms de tableaux séparés par des virgules")
    ap.add_argument("--legacy-size", type=int, default=0, help="legacy : côté si pas de commentaire //WxH")
    ap.add_argument("--smooth", action="store_true", help="legacy : anticrénelage des bitmaps 1 bit (bords lissés)")
    ap.add_argument("--soft", type=float, default=0.0,
                    help="avec --smooth : liseré doux sur tous les bords, rayon en px (0 = non ; 0.5 ≈ bord à 50 %%)")
    ap.add_argument("--no-swap", action="store_true", help="rgb : petit-boutiste (LV_COLOR_16_SWAP 0)")
    ap.add_argument("--suffix", default="", help="ajouté au nom (ex. _28 pour une version réduite : icon_tempext_28)")
    ap.add_argument("--header", help="écrit aussi un .h de LV_IMG_DECLARE pour toutes les icônes générées")
    a = ap.parse_args()

    outdir = Path(a.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    only = {s for s in a.only.split(",") if s}
    items = []  # (nom, image)
    for inp in a.inputs:
        p = Path(inp)
        if p.suffix == ".h":
            for n, i in parse_legacy(p, only, a.legacy_size):
                if a.smooth:
                    i = smooth(i, a.soft)
                if a.size and a.size != i.width:  # réduction après lissage : les bords restent doux
                    i = fit(i, a.size, a.pad)
                items.append((n, i))
        elif p.suffix.lower() == ".svg":
            items.append((ident(p.stem), fit(load_svg(p, a.size or 64), a.size or 64, a.pad)))
        else:
            items.append((ident(p.stem), load_raster(p, a.size, a.pad, a.luma, a.invert)))

    done = []
    for name, img in items:
        if img.getchannel("A").getextrema()[1] == 0:
            print(f"  ! {name}: image entièrement transparente (essayer --luma ?)", file=sys.stderr)
        n, size = write_c(outdir, ident(name) + a.suffix, img, a.fmt, not a.no_swap)
        done.append(n)
        print(f"{n}.c  {img.width}x{img.height}  {CF[a.fmt]}  {size} o")
    if a.header and done:
        Path(a.header).write_text("#pragma once\n#include <lvgl.h>\n\n" +
                                  "".join(f"LV_IMG_DECLARE({n});\n" for n in done))
        print(f"{a.header} ({len(done)} déclarations)")


if __name__ == "__main__":
    main()
