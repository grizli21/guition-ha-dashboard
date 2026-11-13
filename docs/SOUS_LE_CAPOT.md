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
  [lovyangfx_double_tampon_1.3.0.patch](patches/lovyangfx_double_tampon_1.3.0.patch)), alloue deux imagebuffers plein
  écran, LVGL dessine dans celui qui n'est pas montré, la
  dalle bascule au VSYNC — plus de copie vers la dalle (mesuré : 51 ms, pire 81, page Informations)
  et **77 ko de RAM interne rendus** ([src/ecran/ecran.cpp](../src/ecran/ecran.cpp)). C'est la condition
  du chemin rapide : sans mémoire à décaler, rien à décaler. Le mode direct à une seule mémoire a été
  essayé et refusé à l'usage (clignotement et déchirure).
- **Dalle à 17 MHz (60 images/s) : c'est le XIP qui la tient.** La flash et la PSRAM partagent le même
  bus. Sans XIP, chaque écriture en flash (Wi-Fi, réglages, OTA) coupe la PSRAM un instant, et le code lu
  en flash (4 fils) occupe ce bus plus longtemps que s'il était lu en PSRAM (8 fils). Or le DMA de l'écran
  lit la PSRAM en continu : il manque de données, l'image glisse ou saute. Avec le XIP
  (`CONFIG_SPIRAM_XIP_FROM_PSRAM`, `FETCH_INSTRUCTIONS`, `RODATA` dans [platformio.ini](../platformio.ini)),
  le programme est recopié en PSRAM au démarrage et le problème disparaît — c'est le remède recommandé par
  [Espressif](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html).
  Essai A/B à 17 MHz, seul le XIP changeant : sans XIP l'image saute fort, avec XIP elle est propre. Coût
  mesuré : **2,55 Mo de PSRAM** (réserve 8,39 → 5,83 Mo). À 12 MHz sans XIP, l'image tenait déjà, mais
  dès 14 MHz elle glissait. En plus, LVGL est calé sur la bascule de l'écran (`LV_DEF_REFR_PERIOD 2`,
  tactile relu toutes les 10 ms, I2C à 400 kHz) : à 20 ms, son rythme tombait sur celui de la dalle
  (20,3 ms à 14 MHz) et faisait sautiller l'image — un effet réel, mais faible à côté du XIP.
- **Les réglages vivent en flash brute, plus dans le NVS (13/11).** Une écriture NVS — une quinzaine
  d'opérations flash, **3,5 à 6 ms mesurés** par interception des appels `esp_partition_*` — faisait
  glitcher l'image à 16 et 17 MHz, **constaté à l'œil** ; pas à 15 MHz. Une écriture brute de 32 o
  (**0,2 ms**), une rafale de 20 ou l'effacement d'un secteur (**34 ms**) ne glitchent pas, même à
  17 MHz. La cause exacte dans NVS **n'a pas été trouvée** — deux hypothèses ont été mesurées et
  écartées : PSRAM bloquée pendant l'écriture (débit CPU inchangé) et invalidation du cache (rien
  n'était projeté sur la zone NVS). Les réglages écran, énergie, radar et batterie tiennent dans un
  enregistrement de 32 o + CRC16, écrit à la suite dans les **16 derniers secteurs (64 ko)** de la
  partition `spiffs` (inutilisée depuis LVGL, effacée au premier démarrage si elle contient autre
  chose) ; une coupure de courant ne perd rien : le secteur du dernier enregistrement valide n'est
  jamais effacé. Retrouvé après redémarrage : **7 ms**. Le NVS ne sert plus qu'au Wi-Fi et à la
  calibration radio. Avec les réglages en flash brute, le bus de la dalle tient 17 MHz sans glitch
  constaté.
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
- **L'anneau sombre et l'anneau atténué sont, eux aussi, précalculés et tramés (13/11).** À 19 % de
  mélange (teinte sombre), le RGB565 ne garde que quelques teintes : le dégradé de l'anneau avançait
  par paliers irréguliers (retour d'usage). L'anneau sombre complet (270° de dégradé) est calculé
  **une fois**, dégradé continu tramé Bayer 4×4, dessiné **sous** les segments (320 × 320 RGB565A8
  planaire = **300 ko de PSRAM**) ; le niveau atténué utilise la même image en **texture** des arcs
  atténués (`lv_draw_arc img_src`) — **2 × 300 ko** au total. Redessin de la page mesuré sur l'écran :
  **103 → 98-100 ms** par image ; la vraie motivation est l'aspect. En cas d'échec d'allocation, les
  segments sombres sont dessinés en couleur unie comme avant. Ces images précalculées (deux anneaux,
  lueur, palette) restent en PSRAM pour toujours : **environ 900 ko**, voulu.
- **La page « IA » : le ciel vient de Home Assistant et ne redessine que la bande qui change.**
  Soleil et lune parcourent un arc recalculé chaque seconde depuis les horodatages de
  `maison/ciel` ; la phase de lune est dessinée en cases d'après son âge ; le fond du ciel est une
  **colonne de 16 px répété en tuiles** (`LV_IMAGE_ALIGN_TILE`) ; la pluie tombe par **couloirs fixes
  de 4 px**, une goutte par couloir, la colonne changeant à chaque chute. Deux patchs LVGL rendent
  ça tenable : [lvgl_9.6.0_tuiles_zone_utile.patch](patches/lvgl_9.6.0_tuiles_zone_utile.patch) ne
  décode que les tuiles qui touchent la zone à redessiner — mesuré sur l'écran, une bande de 4 px
  (11 700 px) passe de **60 à 29 ms** par image (380 tuiles de 1 px étaient décodées à chaque fois) —
  et [lvgl_9.6.0_elagage_parcours.patch](patches/lvgl_9.6.0_elagage_parcours.patch) saute les objets
  hors de la zone avant toute recherche de style, plus la bordure quand la zone ne touche que
  l'intérieur de l'objet : sous la pluie, **−1,1 ms par image, 42 → 40 % du temps CPU** (mesuré A-B-A ;
  sortie du simulateur identique au pixel). Le tampon d'invalidation de LVGL tient **32 zones**
  (`LV_INV_BUF_SIZE`, défaut) : au-delà LVGL redessine tout l'écran (**151 ms** mesuré) — d'où les
  couloirs étroits (20 au plus) et une seule zone par couloir.
- **La caméra rend tout à la fermeture.** Les tampons d'image MJPEG (2 × 460 800 o), les tampons JPEG
  (3 × 250 000 o), la bande de décodage, les calques de capture d'éléments et les piles des tâches de
  fond sont rendus quand la caméra se ferme : **1,67 Mo de tampons** — mesuré sur l'écran, PSRAM libre
  **4,07 → 5,88 Mo** et RAM interne **87 → 113 ko** dès la fermeture. Les tâches de fond se suppriment
  elles-mêmes, jamais tuées de l'extérieur : on les surprendrait en plein `recv()`. Côté flux : la
  fenêtre TCP portée de 5 760 o à **32 ko** a donné **15,3 à 15,9 images différentes par seconde** (4,5
  au départ) ; un flux MJPEG jamais refermé saturait **22 des 32 tampons de réception Wi-Fi** et
  rendait l'écran sourd (MQTT et banc muets) — l'ouverture/fermeture propre du flux est un correctif
  visible dans [src/ui_lvgl/ui_camera.cpp](../src/ui_lvgl/ui_camera.cpp).
- **Les calques photo de la caméra ne sont repris que sur changement (13/11).** Les éléments posés sur
  la caméra (textes, jauges d'autres pages) sont photographiés (`lv_snapshot` ARGB8888) pour que le
  flux MJPEG ne les efface pas. Ils étaient repris **toutes les 300 ms**, changés ou non, l'ancienne
  photo gardée 1 s (jusqu'à **24 vivantes**) : la PSRAM est tombée à zéro page Imprimante, caméra vide,
  à l'allumage de l'imprimante. Chaque élément porte désormais une **empreinte** (position, visibilité,
  textes, images, valeurs d'arc et de barre) : la photo n'est reprise que si l'empreinte change, ou au
  plus tard toutes les **5 s** (secours pour un changement que l'empreinte ne voit pas). Mesuré en
  usage : 2 photos vivantes, 16 ko. Trace des 30 s : photos vivantes, octets, PSRAM libre.
- **Chien de garde du radar (13/11).** Un écran sur batterie ne dormait plus depuis 4 h 49 : le radar
  LD2410S annonçait une présence **permanente** (7,1 m, broche haute, seuils inchangés) — une relecture
  de sa configuration (`/radar/config`) l'a débloqué à 2 s près. D'où : présence ininterrompue depuis
  **1 h** ET aucun toucher depuis **1 h** → relecture de la configuration (bloque la boucle **~1,6 s**,
  personne ne touche l'écran), **au plus une par heure** ; une ligne au journal de HA avant, une **15 s
  après** (présence encore là = vraie présence ; partie = le radar était bloqué).
- **Migration LVGL 8.4 → 9.6 : des leçons consignées dans le code.** Première
  compilation en 9.6 : **295 erreurs dans 40 sources**. La librairie vendorisée porte aujourd'hui
  **cinq patchs** ([patches/](patches/)) : le chemin rapide de défilement, les tuiles limitées à la
  zone utile et l'élagage du parcours par zone (appliqués), la sonde d'invalidation et la mesure de
  profondeur de pile (protégés par drapeaux de compilation, non appliqués). Les pièges rencontrés, tous tracés en commentaire dans les sources : la 9.6 ne prend plus
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
