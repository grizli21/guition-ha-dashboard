#!/usr/bin/env python3
"""Icônes de bibliothèque pour la page PC (maquette GPU, 08/11) : Material Design Icons rasterisées en alpha 8 bits.

Sources (SVG officiels, recopiés à l'identique dans tools/mdi/) :
  https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/chip.svg     -> mdi:chip   (fréquence GPU)
  https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/memory.svg   -> mdi:memory (fréquence mémoire)
Material Design Icons, dépôt Templarian/MaterialDesign, licence Apache-2.0, récupérés le 08/11/2026. Ces deux icônes
sont celles qu'Home Assistant affiche pour « mdi:chip » et « mdi:memory ».

Rasterisation : rsvg-convert (anticrénelage), canal alpha -> niveaux d'alpha, comme les icônes de lib/icones.
Dépendances : rsvg-convert, Pillow.
"""
import pathlib
import subprocess
import sys

TAILLE = 16  # px ; UI_POLICE_LEGENDE = montserrat_sb_12_ui, line_height 16 (lib/ressources/montserrat_sb_12_ui.c:1761)

ICONES = [
    ("icon_mdi_chip", "mdi-chip.svg", "mdi:chip — fréquence GPU"),
    ("icon_mdi_memory", "mdi-memory.svg", "mdi:memory — fréquence mémoire"),
]

RACINE = pathlib.Path(__file__).resolve().parent.parent


def alpha(svg, taille):
    png = svg.with_suffix(".%d.png" % taille)
    subprocess.run(["rsvg-convert", "-w", str(taille), "-h", str(taille), "-o", str(png), str(svg)], check=True)
    from PIL import Image
    im = Image.open(png).convert("RGBA")
    donnees = list(im.split()[3].tobytes())  # canal alpha brut, sans dépendre de getdata()
    png.unlink()
    return im.width, im.height, donnees


def ecrire(nom, commentaire, svg, taille):
    w, h, vals = alpha(svg, taille)
    lignes = []
    for i in range(0, len(vals), 12):
        lignes.append("  " + ", ".join("0x%02x" % v for v in vals[i:i + 12]) + ",")
    niveaux = len(set(vals))
    nomma = nom.upper()
    contenu = f"""#ifdef __has_include
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

// {nom} : {w}x{h}, {commentaire}.
// Icône de bibliothèque Material Design Icons (licence Apache-2.0), SVG officiel recopié dans
// tools/mdi/{svg.name}, rasterisée à {taille} px avec anticrénelage par tools/creer_ico_mdi.py
// ({niveaux} niveaux d'alpha différents). Source :
// https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/{svg.name.split('-')[1]}

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_{nomma}
#define LV_ATTRIBUTE_IMG_{nomma}
#endif

const LV_ATTRIBUTE_MEM_ALIGN uint8_t {nom}_map[] = {{
{chr(10).join(lignes)}
}};

#if LVGL_VERSION_MAJOR >= 9
const lv_image_dsc_t {nom} = {{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_A8,
  .header.w = {w},
  .header.h = {h},
  .header.stride = {w},
  .data_size = sizeof({nom}_map),
  .data = {nom}_map,
}};
#else
const lv_img_dsc_t {nom} = {{
  .header.always_zero = 0,
  .header.w = {w},
  .header.h = {h},
  .header.cf = LV_IMG_CF_ALPHA_8BIT,
  .data_size = sizeof({nom}_map),
  .data = {nom}_map,
}};
#endif
"""
    p = RACINE / "lib/icones" / ("%s.c" % nom)
    p.write_text(contenu, encoding="utf-8")
    print("%s : %dx%d, %d octets, %d niveaux d'alpha" % (p, w, h, len(vals), niveaux))


for nom, f, comment in ICONES:
    svg = RACINE / "tools/mdi" / f
    if not svg.exists():
        sys.exit("SVG manquant : %s" % svg)
    ecrire(nom, comment, svg, TAILLE)
