#!/usr/bin/env python3
"""Images LVGL de la page « IA » (11/11/2026 — maquette validée le 10/11).

Reprend le dessin de scene_ia.py (maquette) : grille 95 x 103 cases agrandie x4 = 380 x 412, la carte
unique, et écrit dans lib/icones/ des fichiers .c au format LVGL 9.6 (lv_image_dsc_t) :

  colonne_ia_nuit / colonne_ia_jour   1 x 412  RVB565   bandes horizontales du fond (ciel, herbe, terre),
    répétées sur toute la largeur par LVGL (LV_IMAGE_ALIGN_TILE). 1re ligne = UI_CARTE (0x1B2027 en
    RVB565) : posée en bas de la carte, la soudure est invisible.
  etoiles_ia / lune_ia (nuit) ; soleil_ia / nuage1_ia / nuage2_ia (jour)   RVB565A8 : petits décors
    posés sur la colonne, boîte englobante serrée (les boîtes sont affichées à la génération).
  buisson_ia / touffes_ia             RVB565A8   identiques jour et nuit (vérifié à la génération).
  clawd_ia / clawd_ia_repos           88 x 64  RVB565A8  le personnage, yeux ouverts / fermés (« repos »).
  marque_ia_reflechit / attente / repos        RVB565A8  « + » et carrés, « ! », deux « z » (maquette).
  icon_ia / icon_ia_etincelle / icon_ia_bulle  64 x 64  A8 (recolorables, comme icon_pc) : 3 variantes
    d'icône de rail, liseré adouci par smooth() d'icon2lvgl.py au réglage retenu (--smooth --soft 0.5).

Le fond complet 380 x 412 (626 Ko la paire) ne sort PLUS en .c : scene() reste la RÉFÉRENCE de
comparaison — main() vérifie (assert) que colonne + morceaux, dans l'ordre de dessin, la reproduisent
au pixel près.

Repères (pixels de scène x4, origine en haut à gauche de la scène) — c'est page_ia.cpp qui pose les
objets, ces constantes y sont commentées : Clawd case (26,57), + de la pensée case (37,52).

Régénérer après tout changement de dessin :  python3 tools/gen_scene_ia.py
"""
import pathlib
import random

from PIL import Image

from icon2lvgl import smooth  # même outil que les icônes du rail : adoucissement retenu (--smooth --soft 0.5)

RACINE = pathlib.Path(__file__).resolve().parent.parent
P = 4                 # agrandissement maquette : 1 case = 4 px
GW, GH = 95, 103      # grille de la scène (maquette)
SOL = 73              # ligne d'herbe
CARTE = (24, 32, 32)  # UI_CARTE 0x1B2027 vu en RVB565 (mesuré sur la capture de la maquette)


def hexa(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


def melange(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def rvb565(c):
    return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | (c[2] >> 3)


# ---------------------------------------------------------------- la scène (sans Clawd ni marques)
# 11/11 (soir) : la scène sert de RÉFÉRENCE de comparaison. Elle n'est plus écrite en .c (deux fonds
# complets = 626 Ko de flash). Le dessin reste identique, mais chaque élément (soleil, lune, nuages,
# étoiles, buisson, touffes) est aussi rangé dans un calque = dict {(x,y): couleur}. Le fond complet
# est remplacé par : une COLONNE 1 x 412 (bandes horizontales, répétée par LVGL, LV_IMAGE_ALIGN_TILE)
# + les calques en petites images posées au même endroit. La fonction vérifie à chaque génération que
# colonne + calques redonnent la scène de référence au pixel près.
def scene(jour):
    g = [[None] * GW for _ in range(GH)]
    calq = {nom: {} for nom in ("soleil", "nuage1", "nuage2", "etoiles", "lune", "touffes", "buisson")}
    nuit1, nuit2 = (hexa("#24507A"), hexa("#6FA6D2")) if jour else (hexa("#1A1830"), hexa("#262040"))
    for y in range(SOL):  # ciel : couleur de carte en haut (sous les jauges), puis nuit/jour, par bandes
        if y < 46:
            c = melange(CARTE, nuit1, (y // 4) * 4 / 46)
        else:
            c = melange(nuit1, nuit2, ((y - 46) // 4) * 4 / (SOL - 46))
        for x in range(GW):
            g[y][x] = c
    rnd = random.Random(7)  # mêmes étoiles que la maquette : suite figée par la graine
    if jour:
        soleil = hexa("#FFD866")
        cx, cy, r = 13, 56, 4
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + 1:
                    g[y][x] = soleil
                    calq["soleil"][(x, y)] = soleil
        for k in range(r + 2, r + 4):
            for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1)]:
                g[cy + dy * k][cx + dx * k] = soleil
                calq["soleil"][(cx + dx * k, cy + dy * k)] = soleil
        blanc, ombre = hexa("#F4F7FA"), hexa("#C9D8E6")
        # nuages : le premier ne doit JAMAIS passer au-dessus de la tête de Clawd — ses marques (« + », « ! »,
        # « z ») occupent les cases 36..47 x 47..55 et se lisaient mal dessous (relecture Claude du 11/11,
        # captures 174-177). Déplacé à droite et plus haut (52..63 x 45..48, à 5 cases des marques).
        for k, (nx, ny, l) in enumerate([(52, 47, 12), (66, 57, 9)]):
            for x in range(nx, nx + l):
                g[ny][x] = blanc
                calq["nuage%d" % (k + 1)][(x, ny)] = blanc
                g[ny + 1][x] = ombre
                calq["nuage%d" % (k + 1)][(x, ny + 1)] = ombre
            for x in range(nx + 2, nx + l - 3):
                g[ny - 1][x] = blanc
                calq["nuage%d" % (k + 1)][(x, ny - 1)] = blanc
            for x in range(nx + 4, nx + l - 6):
                g[ny - 2][x] = blanc
                calq["nuage%d" % (k + 1)][(x, ny - 2)] = blanc
    else:
        for _ in range(40):
            x, y = rnd.randrange(GW), rnd.randrange(47, SOL - 6)
            while 34 <= x <= 49 and 45 <= y <= 56:  # jamais d'étoile dans la zone des marques de Clawd (+2 de marge)
                x, y = rnd.randrange(GW), rnd.randrange(47, SOL - 6)
            lum = rnd.choice([0x55, 0x70, 0x90, 0xC0])
            g[y][x] = (lum, lum, min(255, lum + 20))
            calq["etoiles"][(x, y)] = (lum, lum, min(255, lum + 20))
        for x, y in [(70, 50), (52, 62), (88, 58)]:  # trois étoiles en croix (celle de 44,52 décalée : zone des marques)
            for dx, dy in [(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)]:
                g[y + dy][x + dx] = (0xE8, 0xE4, 0xD0) if (dx, dy) == (0, 0) else (0x80, 0x7C, 0x90)
                calq["etoiles"][(x + dx, y + dy)] = g[y + dy][x + dx]
        lune = hexa("#EDE6CC")
        cx, cy, r = 13, 56, 5
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + 2 and (x - cx - 3) ** 2 + (y - cy + 2) ** 2 > (r - 1) ** 2:
                    g[y][x] = lune
                    calq["lune"][(x, y)] = lune
    herbe, herbe2, terre, terre2 = hexa("#3E6B2F"), hexa("#2F5424"), hexa("#2A2320"), hexa("#231D1B")
    if jour:
        herbe, herbe2 = hexa("#5C9E3F"), hexa("#43782F")  # herbe plus claire le jour (maquette jour)
    for y in range(SOL, GH):
        for x in range(GW):
            g[y][x] = herbe if y == SOL else herbe2 if y == SOL + 1 else (terre if (y // 3) % 2 else terre2)
    for x in [6, 7, 19, 24, 25, 45, 46, 58, 63, 64]:  # touffes d'herbe
        g[SOL - 1][x] = herbe
        calq["touffes"][(x, SOL - 1)] = herbe
        if x % 2:
            g[SOL - 2][x] = herbe
            calq["touffes"][(x, SOL - 2)] = herbe
    vert, vert_c, vert_f = hexa("#4FA33A"), hexa("#7CCB5A"), hexa("#357A2A")  # buisson
    for (bx, by, br) in [(74, 66, 6), (83, 64, 7), (90, 67, 5), (79, 61, 4)]:
        for y in range(by - br, by + br + 1):
            for x in range(bx - br, bx + br + 1):
                if 0 <= x < GW and y < SOL and (x - bx) ** 2 + (y - by) ** 2 <= br * br:
                    g[y][x] = vert
                    calq["buisson"][(x, y)] = vert
    for (bx, by, br) in [(73, 64, 2), (82, 60, 3), (89, 65, 2)]:
        for y in range(by - br, by + br + 1):
            for x in range(bx - br, bx + br + 1):
                if 0 <= x < GW and (x - bx) ** 2 + (y - by) ** 2 <= br * br:
                    g[y][x] = vert_c
                    calq["buisson"][(x, y)] = vert_c
    for x, y in [(76, 70), (80, 71), (86, 70), (90, 71), (70, 71)]:
        g[y][x] = vert_f
        g[y][x + 1] = vert_f
        calq["buisson"][(x, y)] = calq["buisson"][(x + 1, y)] = vert_f
    return g, calq


def colonne(jour):
    """couleur de bande de chaque ligne de la scène (le fond ne varie qu'avec y) : c'est la colonne
    1 x 410 répétée par LVGL. Formules IDENTIQUES à scene() — la vérification de main() l'impose."""
    nuit1, nuit2 = (hexa("#24507A"), hexa("#6FA6D2")) if jour else (hexa("#1A1830"), hexa("#262040"))
    herbe, herbe2, terre, terre2 = hexa("#3E6B2F"), hexa("#2F5424"), hexa("#2A2320"), hexa("#231D1B")
    if jour:
        herbe, herbe2 = hexa("#5C9E3F"), hexa("#43782F")
    hors = []
    for y in range(GH):
        if y < SOL:
            hors.append(melange(CARTE, nuit1, (y // 4) * 4 / 46) if y < 46
                        else melange(nuit1, nuit2, ((y - 46) // 4) * 4 / (SOL - 46)))
        else:
            hors.append(herbe if y == SOL else herbe2 if y == SOL + 1 else (terre if (y // 3) % 2 else terre2))
    return hors


def morceau(calque):
    """calque -> matrice de cases (couleur, alpha) à partir de sa boîte englobante (alpha 0 ailleurs)."""
    xs = [p[0] for p in calque]
    ys = [p[1] for p in calque]
    x0, y0 = min(xs), min(ys)
    img = [[(None, 0)] * (max(xs) - x0 + 1) for _ in range(max(ys) - y0 + 1)]
    for (x, y), c in calque.items():
        img[y - y0][x - x0] = (c, 255)
    return img, x0, y0


# ---------------------------------------------------------------- Clawd et marques (images séparées)
MOTIF = [
    ".#########.",
    ".#########.",
    "##o#####o##",  # les yeux : demi-blocs creux (clawd.svg officiel)
    "###########",
    ".#########.",
    ".#########.",
    ".#.#...#.#.",
    ".#.#...#.#.",
]
ORANGE, OEUIL = hexa("#D97757"), hexa("#1A1520")
CREME = (0xE8, 0xE4, 0xD0)
OX, OY = 26, SOL - 16  # Clawd en cases de scène : coin haut gauche du motif (maquette)


def clawd(repos):
    """le motif 11 x 8, 2 x 2 cases par bloc, en image autonome (alpha 0 hors silhouette)."""
    h, l = len(MOTIF) * 2, len(MOTIF[0]) * 2
    img = [[(None, 0)] * l for _ in range(h)]
    for j, ligne in enumerate(MOTIF):
        for i, ch in enumerate(ligne):
            if ch == ".":
                continue
            if ch == "o":
                # yeux : 2 x 2 cases sombre ; « repos » (yeux fermés) : le trait du bas seulement
                for dy in (range(1, 2) if repos else range(0, 2)):
                    for dx in range(2):
                        img[j * 2 + dy][i * 2 + dx] = (OEUIL, 255)
                continue
            for dy in range(2):
                for dx in range(2):
                    img[j * 2 + dy][i * 2 + dx] = (ORANGE, 255)
    return img


def marque(cellules, x0, y0):
    """cellules : liste de rectangles (x, y, w, h) en cases de scène, à partir du coin (x0, y0) de l'image."""
    xs = [c[0] for c in cellules] + [c[0] + c[2] for c in cellules]
    ys = [c[1] for c in cellules] + [c[1] + c[3] for c in cellules]
    l, h = max(xs) - x0, max(ys) - y0
    img = [[(None, 0)] * l for _ in range(h)]
    for (x, y, w, hh, c) in cellules:
        for dy in range(hh):
            for dx in range(w):
                img[y - y0 + dy][x - x0 + dx] = (c, 255)
    return img


# « + » et deux carrés qui montent, comme la maquette (cases seules, pas de blocs 2 x 2)
PENSEE = [(OX + 11, OY - 5, 1, 1, ORANGE), (OX + 10, OY - 5, 1, 1, ORANGE), (OX + 12, OY - 5, 1, 1, ORANGE),
          (OX + 11, OY - 6, 1, 1, ORANGE), (OX + 11, OY - 4, 1, 1, ORANGE),
          (OX + 17, OY - 7, 1, 1, CREME), (OX + 19, OY - 9, 2, 1, CREME)]
EXCLAMATION = [(OX + 11, OY - 10, 2, 5, ORANGE), (OX + 11, OY - 4, 2, 2, ORANGE)]
Z1 = [(OX + 13, OY - 6, 3, 1, CREME), (OX + 14, OY - 5, 1, 1, CREME), (OX + 13, OY - 4, 3, 1, CREME)]
Z2 = [(OX + 17, OY - 10, 4, 1, CREME), (OX + 19, OY - 9, 1, 1, CREME), (OX + 18, OY - 8, 1, 1, CREME),
      (OX + 17, OY - 7, 4, 1, CREME)]


# ---------------------------------------------------------------- icônes du rail (3 variantes, A8 64 px)
def pixels_grille(lignes, cell):
    """motif texte ('#' = plein, 'o' = trou) -> matrice d'alpha, une case de `cell` px."""
    h, l = len(lignes) * cell, len(lignes[0]) * cell
    a = [[0] * l for _ in range(h)]
    for j, ligne in enumerate(lignes):
        for i, ch in enumerate(ligne):
            if ch == ".":
                continue
            for dy in range(cell):
                for dx in range(cell):
                    a[j * cell + dy][i * cell + dx] = 255
    return a


def centre(carre, mat):
    """place `mat` au centre d'un canevas carré 64 (alpha 0 partout ailleurs)."""
    a = [[0] * carre for _ in range(carre)]
    dy, dx = (carre - len(mat)) // 2, (carre - len(mat[0])) // 2
    for j, ligne in enumerate(mat):
        for i, v in enumerate(ligne):
            a[dy + j][dx + i] = v
    return a


def grille_a_trous(lignes, cell):  # 'o' = trou (les yeux de Clawd, en négatif dans la silhouette)
    a = pixels_grille(lignes, cell)
    for j, ligne in enumerate(lignes):
        for i, ch in enumerate(ligne):
            if ch == "o":
                for dy in range(cell):
                    for dx in range(cell):
                        a[j * cell + dy][i * cell + dx] = 0
    return a


# ---------------------------------------------------------------- écriture des .c LVGL 9.6
EN_TETE = """// Image de la page « IA » — RÉGÉNÉRÉE par tools/gen_scene_ia.py (ne pas éditer à la main).
// Dessin de la maquette validée du 10/11/2026 (la maquette, voir en-tête), grille x4.
#ifdef __has_include
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

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {n}_map[] = {{
"""

PIED = """}};

const lv_image_dsc_t {n} = {{
  .header.magic = LV_IMAGE_HEADER_MAGIC,   /* lv_image_dsc.h:25 = 0x19 */
  .header.cf = {cf},
  .header.w = {w},
  .header.h = {h},
  .header.stride = {stride},
  .data_size = sizeof({n}_map),
  .data = {n}_map,
}};
"""


def ecrire_c(nom, octets, w, h, cf, stride):
    lignes = []
    for i in range(0, len(octets), 16):
        lignes.append("  " + ", ".join("0x%02x" % v for v in octets[i:i + 16]) + ",")
    contenu = EN_TETE.format(U=nom.upper(), n=nom) + "\n".join(lignes) + PIED.format(
        n=nom, cf=cf, w=w, h=h, stride=stride)
    p = RACINE / "lib/icones" / ("%s.c" % nom)
    p.write_text(contenu, encoding="utf-8")
    print("%s : %dx%d %s, %d o" % (nom, w, h, cf.split("FORMAT_")[1], len(octets)))


def grossir(lignes, f=P):
    """duplication nearest-neighbor : la grille de cases devient l'image x4 (comme la maquette)."""
    out = []
    for ligne in lignes:
        etiree = [v for v in ligne for _ in range(f)]
        for _ in range(f):
            out.append(etiree)
    return out


def bytes_rvb565(grille):
    out = bytearray()
    for ligne in grossir(grille):
        for c in ligne:
            v = rvb565(c)
            out += bytes([v & 0xFF, v >> 8])  # petit-boutien, comme LVGL l'écrit en mémoire
    return bytes(out)


def bytes_rvb565a8(img):
    """LVGL 9.6 (mesuré dans lib/lvgl-9.6.0/src/draw/sw/lv_draw_sw_img.c:318-355) : RGB565A8 est PLANAIRE,
    pas entrelacé — tout le plan RVB565 (stride = 2 x largeur), puis tout le plan A8 (stride = largeur)."""
    couleurs, alphas = bytearray(), bytearray()
    for ligne in grossir(img):
        for (c, a) in ligne:
            v = rvb565(c) if c else 0
            couleurs += bytes([v & 0xFF, v >> 8])  # petit-boutien, comme LVGL l'écrit en mémoire
            alphas.append(a)
    return bytes(couleurs + alphas)


def bytes_a8(mat):
    return bytes(v for ligne in mat for v in ligne)


def bytes_colonne(cols):
    """colonne 1 x 412 : une couleur par ligne de grille, répétée P fois en hauteur seulement."""
    out = bytearray()
    for c in cols:
        v = rvb565(c)
        for _ in range(P):
            out += bytes([v & 0xFF, v >> 8])
    return bytes(out)


def main():
    # Soir du 11/11 : les deux fonds complets 380 x 412 (626 Ko de flash) sortent de lib/icones.
    # Fond = colonne 1 x 412 (bandes horizontales) répétée par LVGL (LV_IMAGE_ALIGN_TILE, supporté en
    # 9.6 : lv_image.c:908 + lv_draw_image.c:276, décodeur ouvert UNE fois puis boucle sur les carreaux)
    # + petits morceaux posés dessus (calques). La scène complète reste la RÉFÉRENCE : on vérifie ici
    # que colonne + morceaux, dans l'ordre de dessin, la redonnent au pixel près.
    ORDRE = ("etoiles", "lune", "soleil", "nuage1", "nuage2", "touffes", "buisson")  # ordre de scene()
    calques = {}
    for jour in (False, True):
        g, calq = scene(jour)
        sfx = "jour" if jour else "nuit"
        calques[sfx] = calq
        cols = colonne(jour)
        comp = [[cols[y]] * GW for y in range(GH)]
        for nom in ORDRE:
            for (x, y), c in calq[nom].items():
                comp[y][x] = c
        assert comp == g, "colonne + morceaux ne reproduisent PAS la scène de référence (%s)" % sfx
        ecrire_c("colonne_ia_" + sfx, bytes_colonne(cols), 1, GH * P, "LV_COLOR_FORMAT_RGB565", 2)
        for nom in ORDRE:
            if not calq[nom]:
                continue
            img, x0, y0 = morceau(calq[nom])
            # « buisson » (mêmes verts jour/nuit) est écrit une fois plus bas ; « touffes » prennent la
            # couleur d'herbe de la phase : un morceau par phase.
            nom_c = nom + "_ia" if nom != "touffes" else "touffes_ia_" + sfx
            if nom == "buisson":
                continue
            ecrire_c(nom_c, bytes_rvb565a8(img), len(img[0]) * P, len(img) * P,
                     "LV_COLOR_FORMAT_RGB565A8", len(img[0]) * P * 2)
            print("   %s : cases x %d..%d, y %d..%d (page_ia.cpp pose à x=%d, y=%d)"
                  % (nom_c, x0, x0 + len(img[0]) - 1, y0, y0 + len(img) - 1, x0 * P, y0 * P))
    assert calques["nuit"]["buisson"] == calques["jour"]["buisson"], "buisson différent entre jour et nuit"
    img, x0, y0 = morceau(calques["nuit"]["buisson"])
    ecrire_c("buisson_ia", bytes_rvb565a8(img), len(img[0]) * P, len(img) * P,
             "LV_COLOR_FORMAT_RGB565A8", len(img[0]) * P * 2)
    print("   buisson_ia : cases x %d..%d, y %d..%d (page_ia.cpp pose à x=%d, y=%d)"
          % (x0, x0 + len(img[0]) - 1, y0, y0 + len(img) - 1, x0 * P, y0 * P))
    for nom, img in [("clawd_ia", clawd(False)), ("clawd_ia_repos", clawd(True))]:
        ecrire_c(nom, bytes_rvb565a8(img), len(img[0]) * P, len(img) * P, "LV_COLOR_FORMAT_RGB565A8", len(img[0]) * P * 2)
    for nom, cel in [("marque_ia_reflechit", PENSEE), ("marque_ia_attente", EXCLAMATION),
                     ("marque_ia_repos", Z1 + Z2)]:
        img = marque(cel, min(c[0] for c in cel), min(c[1] for c in cel))
        ecrire_c(nom, bytes_rvb565a8(img), len(img[0]) * P, len(img) * P, "LV_COLOR_FORMAT_RGB565A8", len(img[0]) * P * 2)
    # variante 1 : silhouette de Clawd (yeux en négatif), variante 2 : + une étincelle, variante 3 : bulle de pensée
    s1 = grille_a_trous(MOTIF, 5)
    v1 = centre(64, s1)
    v2 = centre(64, s1)
    # étincelle : un « + » de 3 x 3 cases de 5 px centré sur (50, 8), à droite au-dessus de la tête
    for (x, y) in [(50, 8), (45, 8), (55, 8), (50, 3), (50, 13)]:
        for dy in range(5):
            for dx in range(5):
                v2[y - 2 + dy][x - 2 + dx] = 255
    bulle = pixels_grille(["...####....",
                           ".#########.",
                           "###########",
                           "###########",
                           ".#########.",
                           "...####....",
                           "....##.....",
                           "......##...",
                           ".........#."], 5)
    v3 = centre(64, bulle)
    for nom, mat in [("icon_ia", v1), ("icon_ia_etincelle", v2), ("icon_ia_bulle", v3)]:
        # 11/11 soir : le liseré « dur » à côté des autres icônes du rail (relecture du 11/11) — même
        # traitement que le réglage retenu d'icon2lvgl.py --smooth --soft 0.5 : smooth() réutilisé
        # (agrandi x8, flou, seuil, réduction par moyenne, flou final 0,5 px sur l'alpha).
        im = Image.new("RGBA", (64, 64), (255, 255, 255, 0))
        im.putalpha(Image.frombytes("L", (64, 64), bytes_a8(mat)))
        ecrire_c(nom, smooth(im, 0.5).getchannel("A").tobytes(), 64, 64, "LV_COLOR_FORMAT_A8", 64)


if __name__ == "__main__":
    main()
