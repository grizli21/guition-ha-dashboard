#!/usr/bin/env python3
# 22/10/2026 : assemble les captures du simulateur (simu/captures/*.png) en une page HTML autonome (images intégrées),
# publiée comme artefact pour voir le rendu sans écran.  Usage : python3 galerie.py sortie.html
import base64, html, os, sys

ICI = os.path.dirname(os.path.abspath(__file__))

# (fichier, titre, légende) ; regroupées par page
SECTIONS = [
    ("Chauffage", [
        ("01_thermostat", "Repos", "Pièce à l’arrêt : anneau assombri, « OFF » en haut à gauche, extérieur dans l’ouverture du bas."),
    ]),
    ("Graphes", [
        ("02_graphe", "Repos", "24 h glissantes jusqu’à 21h30. Corrigé : « maintenant » chevauchait « max 18,5° à 21h30 » ; l’heure n’apparaît plus que pendant la lecture au doigt."),
        ("03_graphe_doigt", "Doigt posé", "Doigt posé à 11h45 : curseur vertical, bulle, valeur du haut en ambre."),
    ]),
    ("Eau chaude", [
        ("05_eau_chaude", "Auto, au repos", "Eau à 52°, consigne 55° en haut à gauche, régime éco actif."),
        ("06_eau_chaude_reglage", "Doigt sur l’anneau", "Consigne glissée à 62° : valeur en ambre au centre, anneau assombri au-delà."),
        ("07_eau_chaude_force", "Forcé", "Chauffe forcée : « Chauffe » en orange, nouvelle consigne 62° en haut à gauche."),
        ("08_eau_chaude_arret", "Arrêt", "Anneau entièrement assombri, pas de bouton, « OFF »."),
    ]),
    ("VMC", [
        ("09_vmc", "Maison en grande vitesse", "L’humidité de la salle de bain (82 %) dépasse 80 % : grande vitesse automatique, chiffre et phrase en bleu. Sous-sol en petite vitesse."),
        ("10_vmc_150ms", "150 ms plus tard", "Les hélices ont tourné : un tour en 0,9 s en grande vitesse, en 2,4 s en petite. Il a fallu passer l’icône en couleur : LVGL 8.4 ne fait pas tourner les icônes en alpha seul."),
        ("11_vmc_sous_sol_arret", "Sous-sol à l’arrêt", "Hélice grise et immobile, « Arrêt » en gris."),
    ]),
    ("Réglages", [
        ("12_reglages", "Haut de la page", "Luminosité jour / nuit en ambre, veille (« Jamais » remplace l’ancien Always On), pièce de l’écran, retour à l’accueil."),
        ("13_reglages_bas", "Défilée jusqu’en bas", "Le doigt virtuel a remonté la page : caméra à la sonnette, éclairage terrasse, mélodie et volume du carillon, Wi-Fi. Le « · » entre l’IP et le signal manquait dans les polices : remplacé par des espaces."),
    ]),
    ("Informations", [
        ("14_infos", "Haut de la page", "Santé de l’écran en 2 colonnes, liaisons avec point vert ou rouge."),
        ("15_infos_bas", "Défilée jusqu’en bas", "Journal (la plus récente en haut, alerte en ambre), actions."),
        ("16_infos_confirmation", "Remise à zéro", "1er appui : le bouton passe au rouge et demande un second appui, sans fenêtre."),
    ]),
    ("Ouvrants", [
        ("17_ouvrants", "Repos", "Store à 60 %, volet ouvert, toile levée. Corrigé au simulateur : le nom et le pourcentage se chevauchaient (nom passé en 14 px)."),
        ("18_ouvrants_glisse", "Volet réglé au doigt", "Le doigt descend dans le curseur : lames grises, poignée blanche pendant le glissé, pourcentage en ambre ; position envoyée au relâchement."),
        ("19_ouvrants_seance", "Séance", "« Séance » allume la Fire TV ; l’automatisation de HA (simulée) descend la toile. Le bouton devient « Fin de séance »."),
    ]),
    ("Café", [
        ("20_cafe", "Café, à 21h30", "Heure sans jour à choisir : 06:45 est passé, donc « demain ». Raccourcis : dans 15 min + les 2 dernières heures."),
        ("21_cafe_programme", "Programmé", "Heure en ambre, « dans 9 h 15 », le bouton devient « Annuler la programmation »."),
        ("22_bouilloire_chauffe", "Bouilloire en chauffe", "5 températures ; « Chauffer à 80 °C maintenant » devient « Arrêter (en chauffe, 80 °C) », en orange."),
    ]),
    ("Croquettes", [
        ("23_croquettes", "Croquettes", "Bol et stock (3x5 moyenne taille), 4 repas : interrupteur, heure, quantité."),
        ("24_croquettes_heure", "Réglage d’une heure", "Un tap sur l’heure : elle passe en ambre et les − / + règlent l’heure (par 15 min) au lieu de la quantité."),
        ("25_croquettes_semaine", "Semaine", "7 barres (aujourd’hui en ambre), ligne d’objectif en pointillés = somme des repas actifs, points servis / manqués."),
        ("26_croquettes_dimanche", "Jour touché", "Dimanche : 2 repas sur 3 (point rouge), −16 g par rapport à l’objectif. Corrigé : la barre gardait le toucher pour elle."),
    ]),
    ("Qualité de l’air", [
        ("27_air", "Atelier et séjour", "ESPurifier à l’atelier, Xiaomi au séjour derrière le poêle. PM2,5 colorée selon le niveau, courbe des 24 h (échelle adaptée au maximum), info, modes."),
    ]),
    ("Poêle", [
        ("28_poele", "Feu : régulation", "Anneau des fumées (dégradé, assombri au-delà de la valeur), repère blanc de consigne, anneau intérieur du corps, brassage qui tourne, tirage du PID."),
        ("29_poele_recharger", "Recharger, au levier", "Pastille rouge « Recharger le poêle » ; mode Levier : tirage gris à 100 %."),
        ("30_poele_courbe", "Courbe 3 h", "Fumées (dégradé), corps, consigne et seuil de recharge en pointillés, tirage en bande bleue."),
        ("31_poele_courbe_1h", "Courbe 1 h", "Même courbe sur la dernière heure."),
        ("32_poele_reglages", "Réglages", "Consigne, tirage mini, alerte recharge, gains du PID, limite intégrale, avec − / +."),
    ]),
    ("Lumières", [
        ("33_lumieres", "Les 3 lampadaires sélectionnés", "Disque chromatique calculé au démarrage, jauge de luminosité, jauge de température. Corrigés : noms sur 2 lignes, « hors ligne » qui débordait, valeur coupée par le repère."),
        ("34_lumieres_couleur", "Couleur choisie au disque", "Un toucher sur le disque : les 3 lampadaires passent en couleur ; luminosité glissée à 72 %."),
        ("35_lumieres_appui_long", "Appui long", "La lampe fenêtre est ajoutée à la sélection (coche et cadre ambre)."),
        ("36_lumieres_atelier", "Filtre par pièce", "Onglet Séjour : seules les lampes du séjour restent."),
        ("208_lumieres_galerie", "Galerie du README", "Une lampe sélectionnée, la pipette posée sur une couleur du disque, luminosité à 72 %."),
    ]),
    ("Musique", [
        ("37_musique", "Lecture", "Pochette ronde — ici une pochette libre de droit (CommoNasm, CC0, voir docs/CREDITS.md) servie comme par Music Assistant ; arc de volume, liseré de progression, temps dans l’ouverture."),
        ("38_musique_volume", "Volume au doigt", "L’arc passe en ambre vif pendant le glissé, le volume en haut à gauche aussi."),
        ("39_musique_suivant", "Glissé sur la pochette", "Morceau suivant, avec le fondu de la pochette (40 % → 100 %)."),
        ("40_musique_playlist", "Playlist", "Aléatoire et Répéter en haut, morceau en cours en ambre, Vider avec confirmation."),
        ("41_musique_biblio", "Bibliothèque", "Artistes avec leur initiale."),
        ("42_musique_albums", "Albums d’un artiste", "Grille de pochettes, Lire l’album / Ajouter à la file."),
        ("43_musique_radios", "Radios", "Grille de logos (dessinés en attendant les logos en cache)."),
        ("44_musique_sortie", "Sortie", "Lecteurs de Music Assistant (+ = groupe synchronisé) et ampli du salon."),
    ]),
    ("Imprimante 3D", [
        ("45_imprimante", "Impression à 62 %", "La miniature se remplit de bas en haut avec les couches (couche 84 / 136) ; la partie à venir reste en transparence."),
        ("46_imprimante_pause_arret", "Pause puis Arrêter", "En pause : anneau gris. « Arrêter » se confirme sur place, bouton rouge plein."),
        ("47_imprimante_reglages", "Réglages", "Préchauffes, consignes, interrupteurs du caisson."),
        ("48_imprimante_fichiers", "Fichiers", "Petite miniature par fichier (à demander au slicer : la grande réduite par zoom ne s’affichait pas)."),
    ]),
    ("PC", [
        ("49_pc", "Système", "16 fils, RAM utilisée / cache, les 3 cartes graphiques, réseau, disque, usage Claude. Corrigé : colonnes qui se chevauchaient en 3x5."),
        ("50_pc_processus", "Processus par CPU", "Tri par CPU, mémoire ou VRAM."),
        ("51_pc_vram", "Processus par VRAM", "Même liste triée par VRAM."),
        ("52_pc_veille", "PC en veille", "Plus de données : bouton Réveiller (Wake-on-LAN de HA). Corrigé : l’icône agrandie par zoom ne s’affichait pas."),
    ]),

]

def main(sortie):
    blocs = ""
    for titre_section, caps in SECTIONS:
        cartes = ""
        for f, t, d in caps:
            chemin = os.path.join(ICI, "captures", f + ".png")
            if not os.path.exists(chemin):
                continue
            b = base64.b64encode(open(chemin, "rb").read()).decode()
            cartes += (f'<figure><img src="data:image/png;base64,{b}" width="480" height="480" alt="{html.escape(t)}">'
                       f'<figcaption><strong>{html.escape(t)}</strong><span>{html.escape(d)}</span></figcaption></figure>\n')
        if cartes:
            blocs += f'<section><h2>{html.escape(titre_section)}</h2><div class="grille">\n{cartes}</div></section>\n'
    page = f'''<title>Simulateur des écrans</title>
<meta name="claude:viewport-fit-sides" content="cover">
<style>
/* galerie sobre, du même noir que les écrans ; captures au pixel près */
:root {{ --fond:#0B0E12; --carte:#151A20; --bord:#262C35; --texte:#E8EAED; --doux:#9AA3AE; --ambre:#FFB547;
  --police:'Montserrat', system-ui, sans-serif; color-scheme: dark }}
body {{ background:var(--fond); color:var(--texte); font-family:var(--police); font-size:15px; line-height:1.5;
  padding-left:max(16px, env(safe-area-inset-left, 0px)); padding-right:max(16px, env(safe-area-inset-right, 0px)); padding-block:24px 40px }}
main {{ max-width:1040px; margin:0 auto; display:flex; flex-direction:column; gap:28px }}
h1 {{ font-weight:700; font-size:26px; margin:0; text-wrap:balance }}
h2 {{ font-weight:600; font-size:18px; margin:0 0 12px; color:var(--ambre); letter-spacing:.02em }}
p.chapo {{ margin:0; color:var(--doux); max-width:65ch }}
p.chapo b {{ color:var(--ambre); font-weight:600 }}
.grille {{ display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 480px), 1fr)); gap:20px }}
figure {{ margin:0; background:var(--carte); border:1px solid var(--bord); border-radius:16px; overflow:hidden; display:flex; flex-direction:column }}
figure img {{ display:block; width:100%; height:auto; image-rendering:pixelated; max-width:100% }}
figcaption {{ padding:12px 14px 14px; display:flex; flex-direction:column; gap:2px; min-width:0 }}
figcaption strong {{ font-weight:600 }}
figcaption span {{ color:var(--doux); font-size:14px }}
</style>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Montserrat:wght@400;600;700&display=swap">
<main>
  <h1>Simulateur des écrans</h1>
  <p class="chapo">Rendu du <b>vrai code LVGL</b> de <code>src/ui_lvgl/</code>, compilé sur le PC avec la même configuration que l’écran (couleurs 16 bits) et des données fictives. Une capture = 480 × 480 pixels de la dalle.</p>
{blocs}</main>
'''
    open(sortie, "w").write(page)

if __name__ == "__main__":
    main(sys.argv[1])
