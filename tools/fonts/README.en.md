# Source fonts — to download, not redistributed here

**English** | [Français](README.md)

This folder receives the source fonts used by `tools/polices_ui.sh` to generate the interface's
bitmap fonts (`lib/ressources/*.c`). The source files are **not** redistributed in this repository
(licenses): fetch them here once before rerunning the script.

| File to place here | Origin | License |
|---|---|---|
| `Montserrat-Regular.ttf`, `Montserrat-SemiBold.ttf`, `Montserrat-Bold.ttf` | [Google Fonts — Montserrat](https://fonts.google.com/specimen/Montserrat) | SIL Open Font License 1.1 (the text accompanies the download) |
| `FontAwesome5-Solid+Brands+Regular.woff` | [FontAwesome 5 Free](https://fontawesome.com) (the file also appears in `scripts/built_in_font/` of the [lvgl/lvgl](https://github.com/lvgl/lvgl) repository, versions 8.x) | font SIL OFL, icons CC BY 4.0 |
| `3x5_K-Type.otf` (**option**, see below) | “3x5” pixel font by Keith Bates / K-Type: [k-type.com](https://www.k-type.com/fonts/3x5/) | free for personal use only ([terms](https://www.k-type.com/licences/)) |

The fonts **generated** from Montserrat and FontAwesome (`lib/ressources/*.c`) are in the
repository: that is what the display compiles. To regenerate them: `bash tools/polices_ui.sh`
(uses `npx lv_font_conv`).

## Option: the “3x5” pixel font in the captures

The README captures show the numeric values (temperature, times, grams, percentages) in the
“3x5” pixel font by K-Type. Its license (personal use) forbids redistributing it: by default, the
firmware therefore shows these values in **Montserrat Bold**. To get the captures' rendering back,
on your own displays:

1. Download the font at [k-type.com/fonts/3x5](https://www.k-type.com/fonts/3x5/) and place
   `3x5.otf` here under the name `3x5_K-Type.otf`. Read [its terms](https://www.k-type.com/licences/):
   commercial use requires a license.
2. Run `bash tools/polices_ui.sh`: it generates `lib/ressources/k3x5_*.c` (ignored by git).
3. Add `-D POLICE_3X5` to the `build_flags` of `[env]` in `platformio.ini`, then recompile.
   In the simulator: `make -C simu propre && make -C simu POLICE_3X5=1`.
