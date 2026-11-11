# 07/11 (inventaire de nettoyage, point 2) : le générateur entre DANS LE DÉPÔT et écrit DANS LE DÉPÔT.
#   Avant : ce script vivait hors dépôt (.banc_v9_esp/gen_conf.py), lisait le gabarit hors dépôt
#   (.banc_v9_esp/lib/lvgl/lv_conf_template.h) et écrivait .banc_v9_esp/include/lv_conf.h, qu'il fallait
#   recopier à la main dans conf_v9/. Maintenant : gabarit = lib/lvgl-9.6.0/lv_conf_template.h (suivi),
#   sortie = conf_v9/lv_conf.h (suivi), et platformio.ini pointe sur « -I conf_v9 » relatif.
#   Le dépôt contient donc de quoi régénérer sa propre configuration 9.6.
# 05/11/2026 — ETAPE 1a : fabrique include/lv_conf.h (LVGL 9.6) du banc de compilation HORS DEPOT.
# Point de depart : lib/lvgl/lv_conf_template.h (les noms d'options sont pris dans la 9.6, pas devines).
# Valeurs reprises de include/lv_conf.h 8.4 du depot (lecture par grep) quand l'option existe encore.
import re, sys, pathlib

racine = pathlib.Path(__file__).resolve().parents[1]            # tools/ -> racine du dépôt
src = racine / "lib" / "lvgl-9.6.0" / "lv_conf_template.h"      # gabarit 9.6, désormais suivi par git
dst = racine / "conf_v9" / "lv_conf.h"                          # c'est le -I conf_v9 qui gagne au build v9

VALEURS = {
    # memoire / stdlib  (8.4 : LV_MEM_CUSTOM 1 + ps_malloc ; 9.6 : meme mecanisme, autres noms.
    #   06/11 mission RAM (retour d'usage : « 100k de moins ce n'est pas acceptable ») : CUSTOM, plus CLIB.
    #   CLIB laissait les petits blocs LVGL dans le tas interne (mesure : libre 41 656 -> 14 996 o,
    #   plus gros bloc 7 156 o). CUSTOM = les 4 fonctions lv_*_core de src/ui_lvgl/ui_mem_v9.cpp,
    #   routees sur PSRAM (heap_caps_malloc MALLOC_CAP_SPIRAM, repli interne), comme ps_malloc en 8.4.
    #   Contrat lu lib/lvgl-9.6.0/include/lvgl/stdlib/lv_mem.h:122-141 (src/stdlib/lv_mem.h = stub).
    #   Reglage reproductible ICI : ne plus editer conf_v9/lv_conf.h a la main, relancer ce script
    #   puis recopier include/lv_conf.h vers conf_v9/lv_conf.h (c'est le -I qui gagne au build v9).
    "LV_USE_STDLIB_MALLOC": "LV_STDLIB_CUSTOM",
    "LV_USE_STDLIB_STRING": "LV_STDLIB_CLIB",
    # frequence (8.4 : LV_DISP_DEF_REFR_PERIOD 20 + LV_INDEV_DEF_READ_PERIOD 20 ; 9.6 : une seule option)
    "LV_DEF_REFR_PERIOD": "2",  # 11/11 : LVGL calé sur la bascule de l'écran, effet faible (l'essentiel vient du XIP) ; lecture du tactile ramenée à 10 ms dans ecran.cpp
    "LV_DPI_DEF": "130",
    "LV_DRAW_BUF_ALIGN": "4",
    "LV_DRAW_LAYER_SIMPLE_BUF_SIZE": "24576",
    # dessin logiciel (8.4 : LV_DRAW_COMPLEX 1)
    "LV_USE_DRAW_SW": "1",
    "LV_DRAW_SW_COMPLEX": "1",
    # 06/11 — ETAPE 2-lien : le vecteur _DebugExceptionVector (0x40374280) saute vers xt_debugexception
    # avec l'instruction « j » dont l'immédiat est signé sur 18 bits NON multipliés (décodé dans le .elf 8.4 :
    # j à 0x40374283 -> 0x4037dfb4, champ 0x9d2d = 40 237 pour 40 244 octets parcourus) => portée ±131 071 o.
    # Mesuré dans les map : fenêtre vecteur->xt_debugexception = 40 244 o en 8.4 (passe), 135 280 o en 9.6 (échoue).
    # Dans cette fenêtre 9.6 : 115 507 o de kernels de blend (un fichier par format de destination, tous marqués
    # LV_ATTRIBUTE_FAST_MEM = IRAM_ATTR par lv_attributes_v9.h) contre 20 512 o en 8.4 (un seul fichier).
    # Formats de destination jamais utilisés ici (écran RGB565, canvas RGB565, images RGB565/RGB565A8,
    # polices 4 bpp, snapshot caméra ARGB8888) : RGB565_SWAPPED (16 021 o IRAM) et I1 (11 132 o IRAM).
    # ARGB8888 et ARGB8888_PREMULTIPLIED conservés : lv_draw_sw_img.c:497-503 bascule en premultiplié pour
    # toute image ARGB8888 redimensionnée avec antialias (page caméra).
    "LV_DRAW_SW_SUPPORT_RGB565_SWAPPED": "1",  # 06/11 : OBLIGATOIRE en 9.6, equivalent de LV_COLOR_16_SWAP=1 de la 8.4 (lv_refr.c:27) : la dalle attend du RGB565 gros boutiste
    "LV_DRAW_SW_SUPPORT_I1": "0",
    # 06/11 chantier D (note MESSAGE_CLAUDE_RAM_96.md, chiffre d'IRAM verifie par l'agent parent :
    # .iram0.text 143 703 o en 8.4 -> 218 855 o en 9.6, soit +75 152 o pris sur le tas interne ;
    # le noyau 9.6 est un fichier par format de destination, marquee LV_ATTRIBUTE_FAST_MEM = IRAM).
    # Ces quatre formats ne sont utilises nulle part dans src/ (grep strict FORMAT_RGB888|XRGB|L8|AL88 :
    # 0 occurrence ; ecran RGB565, images RGB565/RGB565A8, polices 4 bpp, camera ARGB8888).
    # A8 CONSERVE a 1 : les 49 icones + la rotation/le zoom des images A8 (lv_draw_sw_transform.c:108,270,1342,
    # l'helice de la page Qualite de l'air). RGB565, RGB565_SWAPPED, RGB565A8, ARGB8888 et
    # ARGB8888_PREMULTIPLIED conserves (dalle, 6 images, photo camera CAM_PHOTO_CF ui_camera.cpp:46).
    "LV_DRAW_SW_SUPPORT_RGB888": "0",
    "LV_DRAW_SW_SUPPORT_XRGB8888": "0",
    "LV_DRAW_SW_SUPPORT_L8": "0",
    "LV_DRAW_SW_SUPPORT_AL88": "0",
    "LV_GRADIENT_MAX_STOPS": "2",
    "LV_DRAW_SW_DRAW_UNIT_CNT": "1",
    "LV_USE_SNAPSHOT": "1",
    "LV_USE_VECTOR_GRAPHIC": "0",
    "LV_USE_THORVG": "0",
    # journaux (8.4 : LV_USE_LOG 1, niveau WARN, LV_LOG_PRINTF 0 -> lv_log_register_print_cb)
    "LV_USE_LOG": "1",
    "LV_LOG_LEVEL": "LV_LOG_LEVEL_WARN",
    "LV_LOG_PRINTF": "0",
    # assertions (8.4 : NULL 1, MALLOC 1, STYLE 0, MEM_INTEGRITY 0, OBJ 0)
    "LV_USE_ASSERT": "1",
    "LV_USE_ASSERT_NULL": "1",
    "LV_USE_ASSERT_MALLOC": "1",
    "LV_USE_ASSERT_STYLE": "0",
    "LV_USE_ASSERT_MEM_INTEGRITY": "0",
    "LV_USE_ASSERT_OBJ": "0",
    # moniteurs (8.4 : PERF 0, MEM 0, REFR_DEBUG 0)
    "LV_USE_PERF_MONITOR": "0",
    "LV_USE_MEM_MONITOR": "0",
    "LV_USE_SYSMON": "0",
    "LV_USE_REFR_DEBUG": "0",
    # cache des en-tetes d'image (8.4 : LV_IMG_CACHE_DEF_SIZE 256)
    "LV_IMAGE_HEADER_CACHE_DEF_CNT": "256",
    # images (8.4 : LV_USE_PNG 1 -> LV_USE_LODEPNG)
    "LV_USE_LODEPNG": "1",
    "LV_USE_BMP": "0",
    "LV_USE_GIF": "0",
    "LV_USE_QRCODE": "0",
    "LV_USE_FREETYPE": "0",
    "LV_USE_TINY_TTF": "0",
    "LV_USE_IMGFONT": "0",
    # polices actives dans la conf 8.4 du depot
    "LV_FONT_MONTSERRAT_10": "1", "LV_FONT_MONTSERRAT_12": "1", "LV_FONT_MONTSERRAT_14": "1",
    "LV_FONT_MONTSERRAT_16": "1", "LV_FONT_MONTSERRAT_18": "1", "LV_FONT_MONTSERRAT_20": "1",
    "LV_FONT_MONTSERRAT_22": "1", "LV_FONT_MONTSERRAT_24": "1", "LV_FONT_MONTSERRAT_26": "1",
    "LV_FONT_MONTSERRAT_28": "1", "LV_FONT_MONTSERRAT_30": "1", "LV_FONT_MONTSERRAT_32": "1",
    "LV_FONT_MONTSERRAT_44": "1", "LV_FONT_MONTSERRAT_48": "1",
    "LV_FONT_SOURCE_HAN_SANS_SC_16_CJK": "1",   # 8.4 : LV_FONT_SIMSUN_16_CJK 1
    "LV_FONT_FMT_TXT_LARGE": "1",
    "LV_USE_FONT_COMPRESSED": "1",
    "LV_USE_FONT_PLACEHOLDER": "1",
    # attributs (8.4 : LV_ATTRIBUTE_MEM_ALIGN aligned(4), LV_ATTRIBUTE_FAST_MEM IRAM_ATTR)
    "LV_ATTRIBUTE_USE_CUSTOM_INCLUDE": "1",
    "LV_ATTRIBUTE_CUSTOM_INCLUDE": '"lv_attributes_v9.h"',
    # exemples et demos hors firmware
    "LV_BUILD_EXAMPLES": "0",
    "LV_BUILD_DEMOS": "0",
    # systemes d'exploitation / pilotes d'hote : rien
    "LV_USE_OS": "LV_OS_NONE",
    "LV_USE_LINUX_DRM": "0", "LV_USE_LINUX_FBDEV": "0", "LV_USE_SDL": "0", "LV_USE_EVDEV": "0",
    "LV_USE_X11": "0", "LV_USE_WAYLAND": "0", "LV_USE_WINDOWS": "0", "LV_USE_QNX": "0",
    "LV_USE_OPENGLES": "0", "LV_USE_NUTTX": "0", "LV_USE_UEFI": "0", "LV_USE_LOVYAN_GFX": "0",
    "LV_USE_FS_POSIX": "0", "LV_USE_FS_STDIO": "0", "LV_USE_FS_LITTLEFS": "0",
    "LV_USE_FS_ARDUINO_ESP_LITTLEFS": "0",
    # autres options 8.4 conservees a l'identique
    "LV_USE_GRIDNAV": "0", "LV_USE_FRAGMENT": "0", "LV_USE_IME_PINYIN": "0", "LV_USE_MONKEY": "0",
    "LV_BIG_ENDIAN_SYSTEM": "0", "LV_USE_MATRIX": "0", "LV_USE_FLOAT": "0",
}

txt = src.read_text()
# activer le contenu du gabarit
txt = txt.replace('#if 0 /* Set this to "1" to enable content */', '#if 1 /* banc v9 : contenu actif */', 1)

manques = []
for nom, val in VALEURS.items():
    motif = re.compile(r"^(\s*#define\s+" + nom + r")\s+\S.*$", re.M)
    nouveau = motif.sub(lambda m: m.group(1) + " " + val, txt)
    if nouveau == txt and not re.search(r"^\s*#define\s+" + nom + r"\b", txt, re.M):
        manques.append(nom)
    txt = nouveau

# options 8.4 sans equivalent 9.6 : verifiees absentes du gabarit (on ne les invente pas)
SUPPRIMEES = ["LV_COLOR_16_SWAP", "LV_COLOR_SCREEN_TRANSP", "LV_MEM_CUSTOM", "LV_TICK_CUSTOM",
              "LV_DISP_DEF_REFR_PERIOD", "LV_INDEV_DEF_READ_PERIOD", "LV_DRAW_COMPLEX",
              "LV_IMG_CACHE_DEF_SIZE", "LV_DISP_ROT_MAX_BUF", "LV_USE_USER_DATA", "LV_ENABLE_GC",
              "LV_USE_IMG", "LV_USE_BTN", "LV_USE_IMGBTN", "LV_USE_PNG", "LV_USE_THEME_BASIC",
              "LV_FONT_SIMSUN_16_CJK", "LV_SPRINTF_CUSTOM", "LV_USE_LARGE_COORD"]
absentes = [o for o in SUPPRIMEES if not re.search(r"^\s*#define\s+" + re.escape(o) + r"\b", txt, re.M)]

dst.write_text(txt)
print("ecrit :", dst)
print("options du gabarit non trouvees (a ne pas inventer) :", ", ".join(manques) if manques else "aucune")
print("options 8.4 sans equivalent dans le gabarit 9.6 :", len(absentes), "/", len(SUPPRIMEES))
