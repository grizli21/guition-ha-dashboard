#!/usr/bin/env python3
# 07/11/2026 : convertit lib/icones/icon_panneau_led.c du format 8.4 (lv_img_dsc_t, ALPHA_4BIT)
# au format 9.6 (lv_image_dsc_t, header.magic/cf/stride), règle A4 -> A8 identique à
# .banc_v9_esp/convertir_images.py (commit 85e5d24) : premier pixel = nibble haut, alpha v*17.
import re, sys

CHEMIN = "lib/icones/icon_panneau_led.c"
nom = "icon_panneau_led"

src = open(CHEMIN, encoding="utf-8").read()

m = re.search(r"uint8_t %s_map\[\] = \{(.*?)\n\};" % nom, src, re.S)
octets = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))]
assert len(octets) == 2048, "attendu 2048 octets A4 (64x64/2), lu %d" % len(octets)

# A4 -> A8 : chaque octet = deux pixels 4 bits, le premier est le nibble haut ; v -> v*17
a8 = []
for b in octets:
    a8.append((b >> 4) * 17)
    a8.append((b & 0x0F) * 17)
assert len(a8) == 4096

lignes = []
for i in range(0, len(a8), 16):
    lignes.append("  " + " ".join("0x%02x," % v for v in a8[i:i+16]))
corps = "\n".join(lignes)

nouvelle_map = (
    "const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_ICON_PANNEAU_LED "
    "uint8_t %s_map[] = {\n"
    "  /* converti du 8.4 (patch panneau_led_84.patch) vers 9.6, regle A4 -> A8 de 85e5d24 : "
    "premier pixel = nibble haut, alpha v*17 */\n"
    "  /* config lue : LV_COLOR_DEPTH 16, LV_COLOR_16_SWAP 1 (sans objet ici : image A8) */\n"
    "%s\n};" % (nom, corps))

src = re.sub(r"const LV_ATTRIBUTE_MEM_ALIGN.*?uint8_t %s_map\[\] = \{.*?\n\};" % nom,
             nouvelle_map.replace("\\", "\\\\"), src, flags=re.S)

ancien_desc = re.search(r"const lv_img_dsc_t %s = \{.*?\};" % nom, src, re.S)
assert ancien_desc, "descripteur 8.4 introuvable"
nouveau_desc = (
    "const lv_image_dsc_t %s = {\n"
    "  .header.magic = LV_IMAGE_HEADER_MAGIC,   /* lv_image_dsc.h:25 = 0x19 */\n"
    "  .header.cf = LV_COLOR_FORMAT_A8,\n"
    "  .header.w = 64,\n"
    "  .header.h = 64,\n"
    "  .header.stride = 64,\n"
    "  .data_size = sizeof(%s_map),\n"
    "  .data = %s_map,\n};" % (nom, nom, nom))
src = src[:ancien_desc.start()] + nouveau_desc + src[ancien_desc.end():]

open(CHEMIN, "w", encoding="utf-8").write(src)
print("ok : %d octets A4 -> %d octets A8" % (len(octets), len(a8)))
