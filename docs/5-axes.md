# Le 5 axes dans ArtPrint Slicer

Ce document explique, simplement, ce que le slicer sait faire en 5 axes
aujourd'hui, comment il s'y prend, et ce qui reste à faire pour arriver au
5 axes **continu**.

## Les cinq axes de la machine

| Axe | Ce qui bouge |
|---|---|
| X | la tête, de gauche à droite |
| Y | le plateau, d'avant en arrière |
| Z | la tête, de bas en haut |
| B | la tête qui s'incline autour de son pivot |
| C | le plateau qui tourne, sans limite, dans les deux sens |

Le slicer raisonne dans le **repère de la pièce** (fixé au plateau) : il
décide où doit être la pointe de la buse et dans quelle direction elle doit
pointer. La **cinématique** (`src/cinematique.*`) traduit ensuite cela en
valeurs X, Y, Z, B, C. Le firmware ne compense rien : c'est le slicer qui
calcule tout, à partir de réglages mesurés sur la machine (aucune cote n'est
écrite dans le code).

## L'idée : une stratégie d'impression

Un modèle peut s'imprimer de plusieurs façons. Le slicer en propose jusqu'à
trois et mesure, pour chacune, la **surface en surplomb** : celle qui serait
imprimée dans le vide et demanderait des supports.

1. **Classique (3 axes)** : couches horizontales, tête verticale.
2. **Autre pose (3 axes)** : le modèle entier est couché ou retourné dans la
   position qui demande le moins de supports, puis imprimé en 3 axes.
3. **Par branches (5 axes)** : le modèle est découpé en parties, et chaque
   partie est imprimée dans sa propre direction.

### L'exemple de l'arbre

Pour un arbre (un tronc et des branches), la stratégie « par branches » fait
ceci :

1. **Remplir le modèle de petits cubes** (voxels) pour savoir où est la
   matière (`src/squelette.*`).
2. **Mesurer, pour chaque cube, le chemin le plus court jusqu'à la base**
   posée sur le plateau, en restant dans la matière. Les cubes à la même
   distance forment des tranches. Quand une tranche se sépare en deux
   morceaux, c'est qu'une **branche** part. On obtient le « squelette » : le
   tronc, les branches, leur direction, leur longueur, leur épaisseur.
3. **Décider quelles branches méritent leur propre direction** : celles
   dont la surface serait imprimée dans le vide (surplomb de plus de 45°) si
   elles restaient avec la partie qui les porte.
4. **Choisir l'inclinaison de la tête** pour chaque branche : celle de la
   branche si la machine le permet, sinon la plus proche possible (limites
   de B, limite près du plateau). Si la forme de la machine est connue, la
   tête est essayée en plusieurs points de la branche, des deux côtés
   possibles (une même orientation s'obtient en penchant d'un côté, ou de
   l'autre avec le plateau retourné), et l'inclinaison est réduite tant
   qu'il y a un contact. Si la course des axes X, Y, Z est renseignée, elle
   est contrôlée de la même façon, sur toute la surface de la branche : une
   tête inclinée déporte les axes, et une grande pièce peut les envoyer en
   butée.
5. **Couper le modèle par un plan** au pied de chaque branche
   (`src/decoupe.*`). Les deux morceaux sont refermés : ce sont de nouveau
   des volumes que le slicer sait trancher. La coupe n'est gardée que si le
   surplomb total a vraiment baissé.
6. **Trancher chaque partie le long de sa direction** (`src/multiaxe.*`) :
   le tronc en couches horizontales, chaque branche en couches
   perpendiculaires à sa direction, avec parois et remplissage comme
   d'habitude.
7. **Changer d'orientation entre deux parties** : rétraction, recul le long
   de la buse, montée au-dessus de tout ce qui est imprimé, rotation de B
   et de C par petits pas (C par le chemin le plus court), descente, puis
   approche le long de la buse.
8. **Faire relire le G-code par le vérificateur**, comme toujours. S'il
   trouve une faute, le tranchage est refusé.

### Les branches courbes : des couches qui pivotent

Une branche courbe (le bras d'un chandelier, un tuyau coudé) est essayée de
deux façons, et la meilleure est gardée :

- **direction fixe** : la branche est partagée en tronçons à peu près
  droits, chacun avec sa direction ;
- **couches qui pivotent** : la branche reste une seule partie, et chaque
  couche est perpendiculaire à la courbe à cet endroit. La tête s'incline
  donc un peu plus à chaque couche, et le plateau suit. Comme deux couches
  voisines ne sont pas parallèles, l'épaisseur varie d'un côté à l'autre :
  le débit suit l'épaisseur locale. Si la courbe est trop serrée pour
  l'épaisseur de la branche, cette façon est écartée.

### Le pivot continu : B et C tournent pendant l'extrusion

Dans les couches qui pivotent, la tête ne s'arrête pas pour changer
d'orientation : pendant qu'elle imprime une couche, elle glisse peu à peu
vers l'orientation de la couche suivante. Chaque ligne de G-code de la
couche porte alors les cinq axes et l'extrudeur :

```
G1 X… Y… Z… B… C… E… F…
```

- B et C avancent au fil de la longueur de cordon déjà déposée dans la
  couche ; à la fin de la couche, la tête est exactement dans l'orientation
  de la suivante.
- Un segment est recoupé pour que B et C ne tournent jamais de plus de
  0,25° d'une ligne à l'autre : la pointe de la buse reste sur sa ligne
  droite (l'écart est inférieur au millième de millimètre).
- La pointe suit exactement le même chemin qu'avant : seule l'orientation
  de la buse change, de quelques dixièmes de degré par couche.
- Une grande rotation (plus de 4°, par exemple quand la branche quitte la
  verticale et que le plateau doit se placer) se fait toujours à vide,
  au-dessus de la pièce.
- Près du plateau, la tête ne prend pas d'avance sur une inclinaison qui
  n'est permise que plus haut.

Le vérificateur rejoue ces mouvements par petits pas, collisions comprises,
et compte les « extrusions en rotation ». C'est le premier vrai mouvement
5 axes continu du slicer. Pour comparer, l'option `pivot_continu` de
`OptionsMultiaxe` peut être mise à faux : la tête pivote alors entre deux
couches, pendant un déplacement à vide.

## L'essayer

Lister les stratégies proposées pour un modèle :

```
artprint-slicer strategies arbre.stl reglages.txt
```

Trancher avec la stratégie numéro 2 de cette liste :

```
artprint-slicer trancher arbre.stl arbre.gcode reglages.txt --strategie=2
```

Dans l'interface, la commande `strategies` rend la même liste, et
`trancher` accepte un champ `strategie` (`classique`, `autre_pose` ou
`par_branches`).

## Réglages de la machine utilisés

- `distance_pivot_buse` : indispensable dès que la tête s'incline.
- `hauteur_bascule_b` : tant qu'elle n'est pas renseignée, l'inclinaison
  est limitée partout à `b_limite_pres_plateau`.
- `b_min`, `b_max` : inclinaisons extrêmes, comptées depuis la verticale.
- `b_vertical` : valeur de B, dans le G-code, quand la buse est verticale
  (0 par défaut). À régler si la machine affiche une autre valeur pour la
  buse verticale.
- `modele_machine` : s'il est renseigné, les inclinaisons sont choisies pour
  éviter tout contact avec la forme réelle de l'imprimante.

## Ce qui est mesuré

Deux mesures permettent de comparer les stratégies :

- la **surface à soutenir** (mm²), estimée sur le modèle : les faces qui
  seraient imprimées dans le vide ;
- les **parois imprimées dans le vide** (mm), mesurées par le vérificateur
  sur le G-code lui-même : la longueur de paroi déposée sans rien dessous.

Le banc d'essai (`tests/banc_strategies.cpp`) fabrique quatorze objets
(arbres, châssis de drone, chandelier, crochet, tuyau en Y creux, champignon,
tasse, main, équerre, arbre à deux niveaux, buisson à six branches, potence),
propose et tranche toutes leurs stratégies, et donne ces mesures. Avec un
fichier de réglages, il utilise la forme réelle de la machine :

```
banc_strategies reglages.txt
```

Les mêmes objets ont aussi été essayés agrandis deux et trois fois (jusqu'à
22 cm de haut), avec la forme réelle de la machine : c'est cet essai qui a
montré qu'il fallait contrôler la course des axes avant de proposer une
inclinaison.

## Ce qui est vérifié

Chaque G-code multi-directions passe les mêmes contrôles que le 3 axes :
rétraction avant chaque déplacement à vide, C par le chemin le plus court,
limites de B et de C, courses des axes, jamais de cordon dans ou sous la
matière déjà imprimée, et collisions avec la machine quand son modèle est
renseigné.

Les tests (`tests/test_strategie.cpp`) fabriquent deux arbres, les tranchent
par branches, exigent zéro erreur du vérificateur, puis provoquent des
fautes (une branche imprimée deux fois, une inclinaison trop forte) pour
s'assurer qu'elles sont bien refusées. Ils raccourcissent aussi la course
d'un axe pour s'assurer que la stratégie proposée s'adapte et reste
tranchable, et contrôlent le pivot continu (jamais plus de 0,25° par ligne).

## Limites connues

- **Le 5 axes continu ne concerne encore que les branches courbes.** B et C
  tournent pendant l'extrusion dans les couches qui pivotent ; partout
  ailleurs, ils ne bougent qu'entre deux parties. Les couches restent
  planes : il n'y a pas encore de couches courbes.
- **La vitesse d'avance n'est pas corrigée pour les axes de rotation.** Le
  « F » d'une ligne est la vitesse voulue pour la pointe ; la façon dont le
  firmware la partage entre les cinq axes reste à vérifier sur la machine
  (les rotations sont très lentes ici : quelques dixièmes de degré par
  couche).
- **Pas de supports.** Ce qui reste en surplomb (surtout le pied des
  branches) est annoncé en mm², mais rien n'est généré pour le soutenir.
- **Le vérificateur ne contrôle pas qu'un cordon est posé sur quelque
  chose.** Une couche imprimée dans le vide n'est pas refusée.
- **Formes en arbre seulement.** Une anse (deux branches qui se rejoignent)
  est signalée, et la découpe peut y être imparfaite.
- **Une branche qui redescend** ne peut pas être imprimée sans support : la
  buse ne peut pas pointer vers le haut.
- **Le contrôle des collisions avant le tranchage ne regarde que quelques
  positions** par branche ; c'est le vérificateur qui rejoue tout le G-code.
- **Le changement d'orientation passe toujours par le haut.** Il n'y a pas
  encore de recherche du trajet le plus court ou le plus sûr.
- **L'ordre des parties est simple** : chaque partie après celle qui la
  porte, de la plus basse à la plus haute.

## La suite, vers le 5 axes continu

1. Contrôle « cordon posé sur quelque chose » dans le vérificateur.
2. Supports, limités à ce que le 5 axes ne résout pas.
3. Trajets de changement d'orientation plus courts (reculer le bras au lieu
   de monter).
4. Des **couches courbes**, où B et C tournent pendant toute l'extrusion :
   le pivot continu des branches courbes en est la base.
5. Choix de la stratégie dans le logiciel installé (aujourd'hui : ligne de
   commande, interface de programmation et maquette).
