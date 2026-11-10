# Glossaire français → anglais (interface du tableau de bord)

Un terme français = un terme anglais, utilisé **partout de la même façon**. Traduire, c'est choisir :
ce fichier fige les choix. Anglais court et naturel, comme une vraie appli domotique (référence :
Home Assistant en anglais). À place égale, on préfère le plus court (écran 480 × 480, boutons petits).

Mécanisme : macro `TXT("français", "english")` de `src/ui_lvgl/ui_langue.h` — voir ce fichier pour la
liste de ce qu'il ne faut **pas** traduire (protocole MQTT/JSON, modes, identifiants, clés NVS, banc).

## Navigation et pages

| Français | Anglais | Notes |
|---|---|---|
| Chauffage | Heating | |
| Graphes | Charts | |
| Qualité de l'air | Air Quality | titre ; « qualité de l'air » en phrase → « air quality » |
| Ouvrants | Covers | convention Home Assistant (domaine `cover`) ; Store/Volet/Toile en dessous |
| Croquettes | Feeder | la page du distributeur de croquettes (convention HA) |
| Café | Coffee | |
| Eau chaude | Hot Water | |
| VMC | Ventilation | pas « VMC » : un anglophone ne lira pas l'acronyme |
| Poêle | Stove | poêle à pellets ; « Pellet Stove » si la place le permet et lève un doute |
| Musique | Music | |
| Lumières | Lights | |
| Panneau LED | LED Panel | |
| Imprimante 3D | 3D Printer | |
| PC | PC | |
| Réglages | Settings | onglets et page |
| Informations | Info | titre de page (court) |
| Batterie et capteur | Battery & Sensor | |
| Onglet « Ballon » | Tank | |
| Onglet « Courbe » | Chart | |
| Onglet « Suivi » | Status | |
| Onglet « Fichiers » | Files | |
| Onglet « Marche » | Power | |
| Onglet « Feu » | Fire | |
| Onglet « Système » | System | |
| Onglet « Processus » | Processes | |
| Onglet « Sortie » | Output | musique |
| Onglet « Bibliothèque » | Library | musique |
| Onglet « Radios » | Radios | |
| Onglet « Semaine » | Week | |
| Onglet « Diag » | Diagnostics | |

## Chauffage / eau chaude / VMC / poêle

| Français | Anglais | Notes |
|---|---|---|
| Consigne | Setpoint | partout (jamais « target ») |
| Hors-gel | Frost | mode et état (« Frost » sur le bouton 88 px ; « Anti-freeze » trop long) |
| Éco | Eco | |
| Confort | Comfort | |
| Arrêt | Off | état comme bouton |
| Marche | On | |
| au repos | Idle | état ballon/VMC |
| en chauffe | heating | « Stop (heating, 80 °C) » |
| Chauffe forcée / forcé | Boost | bouton et état (convention chauffe-eau) |
| au repos (batterie) | idle | |
| hors ligne | offline | |
| Ballon | Tank | « Water heater » si une phrase l'exige |
| Douches | showers | unité écrite : « 4 showers » |
| Énergie (jour) | energy | |
| VMC — Petite vitesse | Low | |
| VMC — Grande vitesse | High | |
| Fumées | Flue gas | anneau : « Flue » si court |
| Tirage | Draft | |
| Recharger (le poêle) | Refill | pastille : « Refill stove » |
| Corps (de poêle) | Body | |
| Brassage | Stirring | |
| Seuil de recharge | Refill threshold | |
| Gains du PID / limite intégrale | PID gains / integral limit | |

## Café, bouilloire, croquettes

| Français | Anglais | Notes |
|---|---|---|
| Bouilloire | Kettle | |
| Chauffer à 80 °C maintenant | Heat to 80 °C now | |
| Arrêter (en chauffe…) | Stop (heating…) | |
| Programmé | Scheduled | |
| Annuler la programmation | Cancel | |
| dans 15 min | in 15 min | |
| demain / aujourd'hui | tomorrow / today | |
| Bol | Bowl | |
| Stock | Stock | |
| Servir | Serve | |
| Tare | Tare | déjà anglais en domotique |
| Repas | Meals | |
| Objectif | Goal | croquettes ; « Target » pour les consignes |
| Séance | Movie | « Fin de séance » → « End Movie » |

## Ouvrants / lumière / panneau LED

| Français | Anglais | Notes |
|---|---|---|
| Store | Blind | store enrollable |
| Volet | Shutter | |
| Toile | Screen | toile de projection |
| Ouvert / Fermé | Open / Closed | |
| Lumière | Light | |
| Luminosité | Brightness | |
| Température (couleur) | Color temp. | |
| Longueurs d'onde | Wavelengths | |
| Minuterie | Timer | |
| Illimité | Unlimited | durées ; « Never » pour la veille |
| Intensité | Brightness | panneau LED |

## Réglages / infos / système

| Français | Anglais | Notes |
|---|---|---|
| Jour / Nuit | Day / Night | |
| Veille | Sleep | écran et PC ; « Never » pour jamais |
| Jamais | Never | veille |
| Retour à l'accueil | Return home | |
| Pièce | Room | |
| Caméra | Camera | |
| Sonnette | Doorbell | |
| Carillon | Chime | |
| Wi-Fi | Wi-Fi | inchangé |
| Journal (page Infos) | Log | « Journal vide » → « No entries » |
| Remise à zéro | Reset | |
| Santé de l'écran | Screen health | |
| Appareils | Devices | |
| Redémarrer | Restart | |

## Imprimante 3D / PC / musique

| Français | Anglais | Notes |
|---|---|---|
| Prête | Ready | |
| Éteinte | Off | |
| En pause / Pause | Paused / Pause | |
| Arrêter | Stop | |
| Préchauffes | Preheats | |
| Caisson | Enclosure | |
| Carte mère | Mainboard | |
| Filtration | Filtration | |
| Couche 84 / 136 | Layer 84 / 136 | |
| Réveiller | Wake | PC |
| Aléatoire | Shuffle | |
| Répéter | Repeat | |
| Vider | Clear | playlist |
| Lire l'album | Play Album | |
| Ajouter à la file | Add to Queue | |
| Volume | Volume | |

## Pièces par défaut (noms codés ; les noms venant de HA ne sont pas traduits)

| Français | Anglais | Notes |
|---|---|---|
| Séjour | Living Room | onglet lumières : « Living » (sinon chevauche « Workshop ») |
| Atelier | Workshop | |
| Bureau | Office | |
| Chambre | Bedroom | |
| Cuisine | Kitchen | |
| Toutes | All | filtre des lumières |

## Nombres, dates, unités

- Séparateur décimal : **virgule en français, point en anglais** (`SEP_DEC` / `SEP_CAR` de `ui_langue.h`).
- Heures en 24 h dans les deux langues ; « 21h30 » → « 21:30 ».
- Jours : Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday (abbr. Mon, Tue, Wed, Thu, Fri, Sat, Sun).
- Mois : January, February, March, April, May, June, July, August, September, October, November, December.
- Unités : min, h, s, W, kWh, °C, % restent tels quels ; « douches » → « showers ».
