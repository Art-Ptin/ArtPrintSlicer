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
3. **Décider quelles branches méritent leur propre direction** : celles qui
   s'écartent de plus de 45° de la partie qui les porte (au-delà, elles
   s'imprimeraient dans le vide).
4. **Choisir l'inclinaison de la tête** pour chaque branche : celle de la
   branche si la machine le permet, sinon la plus proche possible (limites
   de B, limite près du plateau et, si la forme de la machine est connue,
   plus aucun contact entre la tête et ce qui est déjà imprimé).
5. **Couper le modèle par un plan** au pied de chaque branche
   (`src/decoupe.*`). Les deux morceaux sont refermés : ce sont de nouveau
   des volumes que le slicer sait trancher.
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

## Ce qui est vérifié

Chaque G-code multi-directions passe les mêmes contrôles que le 3 axes :
rétraction avant chaque déplacement à vide, C par le chemin le plus court,
limites de B et de C, courses des axes, jamais de cordon dans ou sous la
matière déjà imprimée, et collisions avec la machine quand son modèle est
renseigné.

Les tests (`tests/test_strategie.cpp`) fabriquent deux arbres, les tranchent
par branches, exigent zéro erreur du vérificateur, puis provoquent des
fautes (une branche imprimée deux fois, une inclinaison trop forte) pour
s'assurer qu'elles sont bien refusées.

## Limites connues

- **Ce n'est pas encore du 5 axes continu.** Pendant qu'une partie
  s'imprime, B et C ne bougent pas ; ils ne bougent qu'entre deux parties.
- **Pas de supports.** Ce qui reste en surplomb (surtout le pied des
  branches) est annoncé en mm², mais rien n'est généré pour le soutenir.
- **Le vérificateur ne contrôle pas qu'un cordon est posé sur quelque
  chose.** Une couche imprimée dans le vide n'est pas refusée.
- **Formes en arbre seulement.** Une anse (deux branches qui se rejoignent)
  est signalée, et la découpe peut y être imparfaite.
- **Une seule façon de pencher la tête est essayée** (la cinématique en a
  deux, d'un côté ou de l'autre avec le plateau retourné).
- **Le changement d'orientation passe toujours par le haut.** Il n'y a pas
  encore de recherche du trajet le plus court ou le plus sûr.
- **L'ordre des parties est simple** : chaque partie après celle qui la
  porte, de la plus basse à la plus haute.

## La suite, vers le 5 axes continu

1. Contrôle « cordon posé sur quelque chose » dans le vérificateur.
2. Supports, limités à ce que le 5 axes ne résout pas.
3. Essai de l'autre façon de pencher la tête, et trajets de changement
   d'orientation plus courts (reculer le bras au lieu de monter).
4. **Couches qui pivotent** le long d'une branche courbe : la direction
   change un peu à chaque couche au lieu de rester fixe par partie.
5. **Couches courbes** : B et C bougent pendant l'extrusion. C'est le vrai
   5 axes continu ; les étapes précédentes en sont la base.
6. Choix de la stratégie dans le logiciel installé (aujourd'hui : ligne de
   commande, interface de programmation et maquette).
