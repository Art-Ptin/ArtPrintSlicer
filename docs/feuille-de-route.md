# Feuille de route des options

Cette liste compare ArtPrint Slicer aux grandes familles d'options d'un slicer
complet. L'inventaire de référence est celui de Bambu Studio (environ 590
options, dont 140 de tranchage et le reste pour les matières et les machines).
Seule la **liste des fonctions** a servi de repère : tout le code d'ArtPrint
Slicer est écrit à partir de zéro.

Légende : ✅ fait · 🟡 en partie · ⬜ à faire

## Qualité

| Fonction | État | Remarque |
|---|---|---|
| Hauteur de couche, première couche | ✅ | |
| Largeur d'extrusion | 🟡 | une seule largeur ; à séparer par type de trait |
| Position de la couture | 🟡 | alignée à l'arrière ; à ajouter : plus proche, aléatoire, peinte |
| Ordre des parois (intérieur ou extérieur d'abord) | ⬜ | |
| Parois à largeur variable (pièces fines) | ⬜ | gros morceau |
| Détection des parois fines, comblement des interstices | ⬜ | |
| Hauteur de couche adaptative | ⬜ | |
| Lissage du dessus (repassage) | ⬜ | |
| Compensation du pied d'éléphant, des trous et des contours | ⬜ | simple |
| Une seule paroi sur les surfaces du dessus | ⬜ | |
| Éviter de traverser les parois pendant les trajets | ⬜ | réduit les rétractions |
| Mode vase (spirale) | ⬜ | |

## Solidité

| Fonction | État | Remarque |
|---|---|---|
| Nombre de parois, couches du dessus et du dessous | ✅ | |
| Épaisseur minimale des coques | ⬜ | simple |
| Densité du remplissage | ✅ | |
| Motifs de remplissage (grille, gyroïde, nid d'abeille...) | 🟡 | lignes croisées seulement |
| Motif et direction des surfaces | 🟡 | lignes à 45° alternées |
| Recouvrement remplissage-parois | ✅ | |
| Combinaison de couches de remplissage | ⬜ | |
| Ponts : direction, débit, vitesse | ⬜ | |

## Vitesse

| Fonction | État | Remarque |
|---|---|---|
| Vitesse par type de trait | 🟡 | 5 vitesses ; à détailler (dessus, ponts, petits contours) |
| Ralentissement dans les surplombs | ⬜ | |
| Accélérations et à-coups par type de trait | ⬜ | commandes Klipper |
| Temps minimal par couche, refroidissement | ⬜ | |
| Estimation de durée avec accélérations | ⬜ | aujourd'hui sans accélérations |

## Supports et adhérence

| Fonction | État | Remarque |
|---|---|---|
| Jupe | ✅ | |
| Bordure, radeau | ⬜ | |
| Détection des surplombs | ⬜ | préalable aux supports |
| Supports classiques et arborescents | ⬜ | gros morceau ; le 5 axes en réduit le besoin |
| Supports peints à la main | ⬜ | |

## Matière et machine

| Fonction | État | Remarque |
|---|---|---|
| Températures, ventilation | 🟡 | une température, ventilateur tout ou rien |
| Rétraction, levée de la buse | ✅ | |
| Essuyage, rétraction au changement de couche | ⬜ | |
| Débit, avance de pression | 🟡 | débit seul |
| Profils de matière et de machine enregistrés | 🟡 | un fichier de réglages |
| G-code de début, de fin et de changement de couche personnalisables | ⬜ | simple, utile pour Klipper |
| Plusieurs matières | ⬜ | pas prévu pour l'instant |

## Préparation de la pièce

| Fonction | État | Remarque |
|---|---|---|
| Ouvrir un STL | ✅ | |
| Ouvrir 3MF, OBJ, STEP | ⬜ | |
| Déplacer, tourner, mettre à l'échelle, poser sur une face | ⬜ | aujourd'hui : posé au centre tel quel |
| Plusieurs pièces, placement automatique | ⬜ | |
| Réparation des modèles ouverts | ⬜ | aujourd'hui : refus avec explication |
| Découpe, modificateurs par zone | ⬜ | |

## Propre à ArtPrint (absent des slicers 3 axes)

| Fonction | État | Remarque |
|---|---|---|
| Cinématique 5 axes (tête B, plateau C) | ✅ | |
| Vérificateur des règles physiques | ✅ | |
| Collisions avec la forme réelle de la machine | ✅ | à accélérer |
| Stratégies d'impression proposées (classique, autre pose, par branches) | ✅ | voir `docs/5-axes.md` |
| Squelette du modèle (tronc et branches) | ✅ | formes en arbre ; les anses sont signalées |
| Découpe par plans, une direction par partie | ✅ | découpe automatique |
| Tranchage multi-directions (tête inclinée, plateau tourné) | ✅ | B et C fixes pendant une partie |
| Inclinaisons choisies avec la forme réelle de la machine | ✅ | les deux façons de pencher la tête sont essayées |
| Inclinaisons choisies selon la course des axes X, Y, Z | ✅ | contrôlée sur toute la surface de la branche |
| Changement d'orientation sûr (reculer, monter, tourner, approcher) | 🟡 | passe toujours par le haut |
| Mesure des parois imprimées dans le vide | ✅ | une mesure, pas encore un refus |
| Couches qui pivotent le long d'une branche courbe | ✅ | B et C changent à chaque couche |
| Banc d'essai sur quatorze objets (dont châssis de drone organique) | ✅ | `tests/banc_strategies.cpp` |
| B et C qui tournent pendant l'extrusion (pivot continu) | ✅ | sur les branches courbes, par pas de 0,25° au plus |
| Couches courbes : le tronc lisse d'abord, puis les branches contre lui | 🟡 | vérifié sur une machine idéale ; exigeant avec la forme réelle de la tête |
| Tronc laissé à nu là où une branche se soude | ✅ | rempli en plein sur 1,5 mm sous le pied |
| Pied de branche au plus juste (le tronc garde sa peau autour, cordons visibles en paroi extérieure) | ✅ | |
| Deux stratégies proposées : classique et couches courbes | ✅ | les anciennes couches orientées ne servent plus qu'aux essais |
| Branche impossible en couches courbes rendue au tronc, les autres gardées | ✅ | une remarque dit laquelle et pourquoi ; chandelier : ses trois bras en couches courbes |
| Déplacements stratégiques : chaque branche essayée à blanc dans plusieurs positions | 🟡 | côté et inclinaison de la tête ; pas encore l'ordre ni les trajets |
| Petits chevauchements réparés (le bout de cordon n'est pas imprimé) | ✅ | une trentaine au plus, avec une remarque |
| Premières couches trop enroulées : découpées au lieu d'abandonner la branche | ✅ | les bords très enroulés restent au tronc |
| Vingt modèles d'essai des couches courbes | ✅ | voir `docs/essais-vingt-modeles.md` |
| Tronc qui penche (jusqu'à 27° environ) | 🟡 | la branche placée sous lui n'est pas encore traitée |
| Repli automatique : une branche fautive est imprimée à plat avec le tronc | ✅ | plus aucun refus sur les soixante modèles d'essai |
| Choix de l'orientation de chaque branche (courbes, tête moins inclinée, à plat) | ✅ | application, ligne de commande et interface de commandes |
| Soixante modèles d'essai et un banc d'essai d'imprimante | ✅ | 52 en couches courbes, 8 en 3 axes |
| Une seule interface : celle de la maquette, branchée sur le vrai slicer | ✅ | voir `docs/application.md` |
| « Ondulation » proposée pour tout modèle, avec sa fenêtre d'options | ✅ | au pire, même tranchage que le classique |
| Caractéristiques après tranchage : grammes, supports, préparation, durée totale | ✅ | supports : toujours 0 g pour l'instant |
| Outils branchés : échelle, orientation automatique, agencer, plateaux | 🟡 | un seul objet par plateau |
| Arche, pont, anse, chapeau, table en couches courbes | ⬜ | voir `docs/essais-vingt-modeles.md` |
| Peau courbe : une dernière passe suit la forme du dessus (option) | 🟡 | tête d'aplomb sur la surface ; abandonnée si elle provoquerait un contact |
| Vérificateur : croisement de cordons sur une couche inclinée | ✅ | toléré sur une courte longueur, comme à plat |
