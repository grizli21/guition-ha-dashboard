# Sous le capot

**English** | [Français](en/SOUS_LE_CAPOT.md)

← [Retour au README](../README.fr.md)

Tous les chiffres cités ci-dessous ont été mesurés, soit sur l'écran (banc PC ⇄ écran, séries A-B-A,
captures comparées à l'octet), soit au simulateur. Seuls les mécanismes et la trace de leur mesure en
commentaire sont publiés dans ce dépôt ; les journaux de campagne complets sont restés hors du dépôt
public.

- **Défilement « par bande », la LVGL patchée.** Quand un conteneur défile, LVGL repeint toute son
  aire : mesuré en 9.6 brute, **175 336 px par pas de geste** (le conteneur 404 × 434 entier). Le
  [scroll_bande_9.6.patch](patches/scroll_bande_9.6.patch), appliqué à la librairie vendorisée
  (marqueur `.bande-applique`), décale la mémoire d'image du déplacement réellement appliqué et ne
  repeint que deux bandes, entrée et sortie : sur l'écran, **7 676 px et 61 → 9 ms** d'image médiane
  (commande de banc `/defiler`, 30 pas, 180 mesures). Une image de réparation pleine aire part 150 ms
  après le dernier pas. L'éligibilité se statue à la création du conteneur
  ([src/ui_lvgl/ui_theme.cpp](../src/ui_lvgl/ui_theme.cpp), `ui_defilement_rapide`) : une exclusion en
  cours de geste ne défait pas les images déjà sautées — chiffrée un temps en 8.4 à 84 images /
  3 712 609 px de pixels orphelins.
- **Rotation 0, deux mémoires d'image en PSRAM.** Toute la production tourne en rotation 0 : le pilote
  LovyanGFX, **modifié** pour l'occasion (il n'a d'origine qu'une mémoire d'image : voir
  [lovyangfx_double_tampon_1.2.19.patch](patches/lovyangfx_double_tampon_1.2.19.patch)), alloue deux imagebuffers plein
  écran, LVGL dessine dans celui qui n'est pas montré, la
  dalle bascule au VSYNC — plus de copie vers la dalle (mesuré : 51 ms, pire 81, page Informations)
  et **77 ko de RAM interne rendus** ([src/ecran/ecran.cpp](../src/ecran/ecran.cpp)). C'est la condition
  du chemin rapide : sans mémoire à décaler, rien à décaler. Le mode direct à une seule mémoire a été
  essayé et refusé à l'usage (clignotement et déchirure).
- **L'anneau du thermostat : 75 → 24 ms par pas.** L'anneau est décrit élément par élément (135
  segments de 2°, fondus, bouts arrondis, point de la pièce), chacun avec son rectangle d'impact ;
  seule la zone qui change est invalidée (`anneau_invalider`). Mesuré sur l'écran en A-B-A pendant une
  glisse de consigne par pas de 0,5° : **75,5 → 24,0 ms** médians par pas (−68 %),
  **108 900 → ~32 500 px** repeints par pas. Le simulateur a validé l'identité des pixels avant/après.
- **Le même patron sur les graphes.** La lecture au doigt du graphe 24 h repeignait **119 086 px en
  103 ms** par pas de doigt (mesuré) ; ne repeindre que les deux colonnes du curseur et l'ancienne
  place de la bulle l'a ramenée à ~45 ms. Idem sur les courbes : ballon **106 → 30 ms**, poêle
  **51 → 24 ms** par image (écran, A/B).
- **La lueur de chauffe est tramée et précalculée.** Le halo rouge sous l'anneau est une image de
  232 × 232 px (**161 472 o**) calculée une fois, RGB565 + masque alpha planaire, en **trame ordonnée
  (Bayer 4×4)** : le RGB565 n'offre qu'une vingtaine de niveaux de rouge, sans trame les marches sont
  visibles. C'est un choix d'aspect, pas de vitesse.
- **La caméra rend tout à la fermeture.** Les tampons d'image MJPEG (2 × 460 800 o), les tampons JPEG
  (3 × 250 000 o), la bande de décodage, les calques de capture d'éléments et les piles des tâches de
  fond sont rendus quand la caméra se ferme : **1,67 Mo de tampons** — mesuré sur l'écran, PSRAM libre
  **4,07 → 5,88 Mo** et RAM interne **87 → 113 ko** dès la fermeture. Les tâches de fond se suppriment
  elles-mêmes, jamais tuées de l'extérieur : on les surprendrait en plein `recv()`. Côté flux : la
  fenêtre TCP portée de 5 760 o à **32 ko** a donné **15,3 à 15,9 images différentes par seconde** (4,5
  au départ) ; un flux MJPEG jamais refermé saturait **22 des 32 tampons de réception Wi-Fi** et
  rendait l'écran sourd (MQTT et banc muets) — l'ouverture/fermeture propre du flux est un correctif
  visible dans [src/ui_lvgl/ui_camera.cpp](../src/ui_lvgl/ui_camera.cpp).
- **Migration LVGL 8.4 → 9.6 : trois patchs, des leçons consignées dans le code.** Première
  compilation en 9.6 : **295 erreurs dans 40 sources**. La librairie vendorisée porte trois patchs
  ([patches/](patches/)) : le chemin rapide de défilement (appliqué), la sonde
  d'invalidation et la mesure de profondeur de pile (protégés par drapeaux de compilation, non
  appliqués). Les pièges rencontrés, tous tracés en commentaire dans les sources : la 9.6 ne prend plus
  son horloge dans la configuration — sans `lv_tick_set_cb`, plus rien ne se rafraîchit ; le boutisme —
  sans `LV_COLOR_FORMAT_RGB565_SWAPPED` sur l'écran et les deux mémoires, rouge et bleu sont permutés
  sur la dalle ; `lodepng_decode32` ne rend plus des pixels mais un descripteur — mal lu, il coûtait
  **4,44 Mo de PSRAM** perdus, récupérés ; l'allocateur n'envoie en PSRAM que les blocs de plus de
  **512 o** (`heap_caps_malloc_extmem_enable`), sinon le tas interne suffoque.
- **Le simulateur PC ne ment pas, mais il ne chronomètre pas.** Il compile les VRAIES sources des
  pages, avec la même configuration, et produit ~280 captures 480 × 480 : identité des pixels au comparatif
  octet à octet entre deux binaires, appels de dessin et pixels invalidés comptés par primitive,
  scénario complet sous AddressSanitizer/UBSan (zéro rapport). Mais ses microsecondes sont du temps PC,
  **300 à 600 fois** plus rapide que l'écran : tous les chiffres du README qui parlent de temps sont
  mesurés sur l'écran.
- **Chien de garde, dump du plantage précédent, banc.** Le chien de garde de la boucle est à **10 s** :
  un écran figé redémarre et dit où il était — l'étape en cours, ses repères et les dernières traces
  (anneau de 3 072 o) survivent en mémoire RTC et s'affichent page Informations. Le banc PC ne répond
  qu'à la machine désignée : HTTP **8080** (capture d'écran, appuis et glissés simulés), Telnet **23**
  (journal, dump du dernier plantage, sans redémarrer). Verrou non négociable : tout appui synthétique
  qui toucherait un objet cliquable est refusé (**403**) — d'où la route `/defiler`, qui défile sans
  jamais simuler de doigt.
