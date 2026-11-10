// 10/11/2026 : choix de la langue de l'interface à la COMPILATION.
//   -D UI_LANG_EN  -> anglais ; sans drapeau -> français (langue par défaut de la production).
//   Simulateur : make -C simu UI_LANG_EN=1 ; firmware : ligne commentée dans platformio.ini ([env:ecran]).
//
// TXT("texte français", "english text") choisit l'un des deux au préprocesseur : l'autre chaîne disparaît
// du binaire (coût nul à l'exécution, zéro octet pour la langue non retenue), et les deux langues restent
// côte à côte dans le code, faciles à relire. Voir docs/GLOSSAIRE_EN.md pour le vocabulaire.
//
// ⛔ NE JAMAIS envelopper dans TXT ce qui sert au PROTOCOLE : sujets MQTT, clés et valeurs JSON échangées,
// tableaux de modes ("arret", "horsgel", "eco", "confort"…), chaînes comparées à ce qui arrive de HA,
// identifiants d'entités, clés NVS, commandes du banc HTTP et de telnet, journaux de diagnostic (série/telnet).
//
// SEP_DEC / SEP_CAR : séparateur décimal affiché (virgule en français, point en anglais).
//   ui_label_texte_fmt(o, "%.1f" SEP_DEC "%d", ...) ou, dans les helpers qui remplacent le point de
//   snprintf : *p = SEP_CAR.
#pragma once

#ifdef UI_LANG_EN
#define TXT(fr, en) en
#define SEP_DEC "."
#define SEP_CAR '.'
#else
#define TXT(fr, en) fr
#define SEP_DEC ","
#define SEP_CAR ','
#endif
