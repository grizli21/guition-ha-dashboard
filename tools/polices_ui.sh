#!/usr/bin/env bash
# 22/10/2026 : régénère les polices de la NOUVELLE interface (src/ui_lvgl/) dans lib/ressources/.
# Jeu de caractères : tout le latin-1 (accents, ° µ · ² ³ « ») + ’ – — … − ≈ ← ↑ → ↓ ≤ ≥ (manquaient : vus au simulateur).
# Les polices 12 et 14 « _fr » servent aussi aux pages de production : on en fait des copies « _ui » sans les toucher.
# Lancer depuis la racine du dépôt :  bash tools/polices_ui.sh
# Prérequis : les polices sources (Montserrat, FontAwesome woff ; en option la 3x5 de K-Type) dans tools/fonts/ — voir tools/fonts/README.md.
set -euo pipefail
cd "$(dirname "$0")/.."

TEXTE="32-126,160-255,8211,8212,8217,8230,8722,8776,8592-8595,8804,8805"
# symboles LVGL (FontAwesome) utilisés dans les boutons et titres
SYMB="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"
# Récupérer une fois : FontAwesome5-Solid+Brands+Regular.woff (scripts/built_in_font du dépôt lvgl, licence FA Free)
FA=tools/fonts/FontAwesome5-Solid+Brands+Regular.woff
CONV="npx --yes lv_font_conv@1.5.2"
OPTS="--bpp 4 --format lvgl --lv-include lvgl.h --force-fast-kern-format"

gen() { # taille police sortie [symboles]
    local taille=$1 police=$2 sortie=$3 symb=${4:-}
    if [ -n "$symb" ]; then
        $CONV $OPTS --size "$taille" --font "$police" -r "$TEXTE" --font "$FA" -r "$symb" -o "lib/ressources/$sortie.c"
    else
        $CONV $OPTS --size "$taille" --font "$police" -r "$TEXTE" -o "lib/ressources/$sortie.c"
    fi
    echo "$sortie.c"
}

gen 12 tools/fonts/Montserrat-SemiBold.ttf montserrat_sb_12_ui # 31/10 : légende en SemiBold (Regular 12 : traits d'un pixel, peu lisible)
gen 14 tools/fonts/Montserrat-Regular.ttf montserrat_14_ui
gen 16 tools/fonts/Montserrat-SemiBold.ttf montserrat_sb_16_fr "$SYMB"
gen 20 tools/fonts/Montserrat-SemiBold.ttf montserrat_sb_20_fr "$SYMB"
gen 22 tools/fonts/Montserrat-SemiBold.ttf montserrat_sb_22_fr
gen 30 tools/fonts/Montserrat-SemiBold.ttf montserrat_sb_30_fr "61523,61524"
# 27/10 : nom de la pièce du thermostat en gras (retour d'usage) ; ses flèches < > un peu plus grandes (36 au lieu de 30)
gen 30 tools/fonts/Montserrat-Bold.ttf montserrat_b_30_fr
gen 16 tools/fonts/Montserrat-Bold.ttf montserrat_b_16_fr # 31/10 : boutons de mode du thermostat (retour d'usage : plus épais)
$CONV $OPTS --size 36 --font "$FA" -r "61523,61524" -o lib/ressources/chevrons_36.c
echo "chevrons_36.c"

# Valeurs chiffrées, police par défaut : Montserrat Bold (UI_POLICE_TEMP / _TEMP_PETITE / _3X5_P / _3X5_M dans ui_theme.h).
# Mêmes hauteurs de chiffres que la 3x5 (17 / 25 / 33 / 66 px), un peu réduites à 32 et 40 : Montserrat est plus large.
for t in 24 32 40; do
    $CONV --bpp 4 --size $t --font tools/fonts/Montserrat-Bold.ttf -r 32-126,160-255 --format lvgl --lv-include lvgl.h -o lib/ressources/montserrat_b_${t}_fr.c
    echo "montserrat_b_${t}_fr.c"
done
$CONV --bpp 4 --size 94 --font tools/fonts/Montserrat-Bold.ttf -r 32,37,44-46,48-57,176 --format lvgl --lv-include lvgl.h -o lib/ressources/montserrat_b_94_num.c
echo "montserrat_b_94_num.c"

# OPTION POLICE_3X5 : police pixel « 3x5 » de K-Type (gratuite pour un usage personnel seulement, NON fournie).
# Si tools/fonts/3x5_K-Type.otf est présent, génère ses 4 fichiers (ignorés par git), puis compiler avec -D POLICE_3X5.
# Tailles où un carré tombe sur un nombre entier de pixels (37 -> 6 px, 49 -> 8 px).
K3X5=tools/fonts/3x5_K-Type.otf
if [ -f "$K3X5" ]; then
    for t in 25 37; do
        $CONV --bpp 4 --size $t --font "$K3X5" -r 32-126,160-255 --format lvgl --lv-include lvgl.h -o lib/ressources/k3x5_${t}.c
        echo "k3x5_${t}.c"
    done
    $CONV --bpp 4 --size 49 --font "$K3X5" -r 32-126,160-255 --format lvgl --lv-include lvgl.h -o lib/ressources/k3x5_49_num.c
    echo "k3x5_49_num.c"
    $CONV --bpp 4 --size 99 --font "$K3X5" -r 32,37,44-46,48-57,176 --format lvgl --lv-include lvgl.h -o lib/ressources/k3x5_99_num.c
    echo "k3x5_99_num.c"
else
    echo "($K3X5 absent : option POLICE_3X5 non générée, voir tools/fonts/README.md)"
fi
