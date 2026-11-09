# Polices sources — à télécharger, non redistribuées ici

Ce dossier était censé contenir les polices sources utilisées par `tools/polices_ui.sh`
pour générer les polices bitmap de l'interface (`lib/ressources/montserrat_*.c`, `k3x5_*.c`).
Les fichiers sources ne sont **pas** redistribués dans ce dépôt (licences) : récupérez-les ici
une fois avant de relancer le script.

| Fichier à placer ici | Origine | Licence |
|---|---|---|
| `Montserrat-Regular.ttf`, `Montserrat-SemiBold.ttf`, `Montserrat-Bold.ttf` | [Google Fonts — Montserrat](https://fonts.google.com/specimen/Montserrat) | SIL Open Font License 1.1 (le texte accompagne le téléchargement) |
| `3x5_K-Type.otf` | police pixel « 3x5 » de K-Type : [ktype.co.uk](https://ktype.co.uk/freebies.html) | gratuite à l'usage — vérifier les conditions de l'auteur avant toute redistribution |
| `FontAwesome5-Solid+Brands+Regular.woff` | [FontAwesome 5 Free](https://fontawesome.com) (le fichier figure aussi dans `scripts/built_in_font/` du dépôt [lvgl/lvgl](https://github.com/lvgl/lvgl), versions 8.x) | police SIL OFL, icônes CC BY 4.0 |

Les polices **générées** (`lib/ressources/*.c`) sont dans le dépôt : c'est ce que compile
l'écran. Pour les régénérer : `bash tools/polices_ui.sh` (utilise `npx lv_font_conv`).
