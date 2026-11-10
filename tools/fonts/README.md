# Polices sources — à télécharger, non redistribuées ici

[English](README.en.md) | **Français**

Ce dossier reçoit les polices sources utilisées par `tools/polices_ui.sh` pour générer les
polices bitmap de l'interface (`lib/ressources/*.c`). Les fichiers sources ne sont **pas**
redistribués dans ce dépôt (licences) : récupérez-les ici une fois avant de relancer le script.

| Fichier à placer ici | Origine | Licence |
|---|---|---|
| `Montserrat-Regular.ttf`, `Montserrat-SemiBold.ttf`, `Montserrat-Bold.ttf` | [Google Fonts — Montserrat](https://fonts.google.com/specimen/Montserrat) | SIL Open Font License 1.1 (le texte accompagne le téléchargement) |
| `FontAwesome5-Solid+Brands+Regular.woff` | [FontAwesome 5 Free](https://fontawesome.com) (le fichier figure aussi dans `scripts/built_in_font/` du dépôt [lvgl/lvgl](https://github.com/lvgl/lvgl), versions 8.x) | police SIL OFL, icônes CC BY 4.0 |
| `3x5_K-Type.otf` (**option**, voir plus bas) | police pixel « 3x5 » de Keith Bates / K-Type : [k-type.com](https://www.k-type.com/fonts/3x5/) | gratuite pour un usage personnel seulement ([conditions](https://www.k-type.com/licences/)) |

Les polices **générées** à partir de Montserrat et FontAwesome (`lib/ressources/*.c`) sont dans
le dépôt : c'est ce que compile l'écran. Pour les régénérer : `bash tools/polices_ui.sh`
(utilise `npx lv_font_conv`).

## Option : la police pixel « 3x5 » des captures

Les captures du README montrent les valeurs chiffrées (température, heures, grammes,
pourcentages) dans la police pixel « 3x5 » de K-Type. Sa licence (usage personnel) interdit de
la redistribuer : par défaut, le firmware affiche donc ces valeurs en **Montserrat Bold**. Pour
retrouver le rendu des captures, sur vos propres écrans :

1. Téléchargez la police sur [k-type.com/fonts/3x5](https://www.k-type.com/fonts/3x5/) et placez
   `3x5.otf` ici sous le nom `3x5_K-Type.otf`. Lisez [ses conditions](https://www.k-type.com/licences/) :
   un usage commercial demande une licence.
2. Lancez `bash tools/polices_ui.sh` : il génère `lib/ressources/k3x5_*.c` (ignorés par git).
3. Ajoutez `-D POLICE_3X5` aux `build_flags` de `[env]` dans `platformio.ini`, puis recompilez.
   Au simulateur : `make -C simu propre && make -C simu POLICE_3X5=1`.
