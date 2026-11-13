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
| C | le plateau qui tourne, dans les deux sens, entre -360° et +360° |

Le slicer raisonne dans le **repère de la pièce** (fixé au plateau) : il
décide où doit être la pointe de la buse et dans quelle direction elle doit
pointer. La **cinématique** (`src/cinematique.*`) traduit ensuite cela en
valeurs X, Y, Z, B, C, à partir de réglages mesurés sur la machine (aucune
cote n'est écrite dans le code).

## Le contrat G-code version 1

C'est l'accord entre le slicer et le firmware de l'imprimante ArtPrint
(réglage `contrat_gcode = 1`, par défaut).

- Première ligne du fichier : `; CONTRAT_GCODE: 1`.
- X, Y, Z sont la position de la **pointe de la buse**, dans le repère de la
  machine (origine au centre du plateau, sur sa surface), la rotation C déjà
  appliquée. Le firmware calcule lui-même où placer le pivot de la tête : le
  slicer n'ajoute plus cette correction.
- B : 0 quand la buse est verticale, positif quand la tête s'incline. Il ne
  dépasse jamais `b_inclinaison_max` (50° par défaut). Une partie dont les
  couches demandent plus est signalée avant l'export, dans le choix de la
  stratégie et dans les messages du tranchage.
- C reste entre `c_min` et `c_max` (-360° et +360° par défaut). Avant une
  limite : retrait du filament, buse relevée, plateau déroulé, reprise.
- Z de la pointe n'est jamais négatif.
- Début : une seule ligne `ARTPRINT_PRINT_START BUSE=... PLATEAU=...`
  (températures des réglages). Fin : le retrait final, puis
  `ARTPRINT_PRINT_END`.
- Chaque ligne G0 ou G1 de déplacement porte X Y Z B C. Quand C change
  pendant une extrusion, le trajet est découpé en segments courts.
- La durée estimée tient compte des vitesses de rotation du firmware
  (`vitesse_b_max`, `vitesse_c_max`).
- Les commentaires `;PARTIE`, `;COUCHE`, `;Z`, `;TYPE` et l'extrusion
  relative ne changent pas.

### Au-delà de 90° : imprimer par en dessous

Si la mécanique le permet, les réglages `b_min`, `b_max` et
`b_inclinaison_max` peuvent dépasser 90° (110° par exemple). La buse pointe
alors vers le haut : une branche qui redescend est imprimée d'aplomb, par en
dessous, au lieu d'être rendue au tronc. Une couche courbe peut regarder
vers le bas jusqu'à 10° de plus que la limite de la machine ; au-delà, sa
branche est imprimée avec le tronc, comme avant.

À ces inclinaisons, le corps de la tête passe plus bas que la pointe de la
buse : le contrôle des collisions avec la vraie forme de la machine
(`modele_machine`) est indispensable avant d'imprimer. Près du plateau,
`b_limite_pres_plateau` et `hauteur_bascule_b` s'appliquent toujours.

Avec `contrat_gcode = 0`, le slicer écrit l'ancien format : les positions des
axes (correction du pivot comprise) et les commandes G-code habituelles de
début et de fin. Un test automatique (`tests/test_strategie.cpp`) contrôle
que les deux formats décrivent exactement les mêmes mouvements.

## L'idée : une stratégie d'impression

Un modèle peut s'imprimer de deux façons. Le slicer les propose et mesure,
pour chacune, la **surface en surplomb** : celle qui serait imprimée dans le
vide et demanderait des supports.

1. **Classique (3 axes)** : couches horizontales, tête verticale.
2. **Couches courbes (5 axes continu)** : le tronc d'abord, en couches
   horizontales ; puis chaque branche, imprimée contre lui en couches qui
   épousent sa surface (voir plus bas).

Deux anciennes stratégies à couches orientées existent encore dans le code
(« autre pose » et « par branches », décrite ci-dessous) : elles ne sont plus
proposées et ne servent plus qu'aux essais (`OptionsStrategie::couches_orientees`).

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
5 axes continu du slicer. Pour comparer, ou pour un premier essai prudent
sur la machine, le réglage `pivot_continu` (onglet Machine) peut être
décoché (0 dans le fichier de réglages) : la tête pivote alors entre deux
couches, pendant un déplacement à vide.

## Les couches courbes : le tronc d'abord, puis les branches contre lui

La stratégie « Couches courbes (5 axes continu) » ne coupe plus le modèle
par des plans. Elle sépare d'elle-même le tronc des branches
(`src/champ_couches.*`, `src/couches_courbes.*`).

### Le tronc : lisse, et à nu là où une branche viendra

Le **tronc** est tout ce qui tient debout tout seul : chaque point du tronc
a de la matière sous lui, sans interruption, jusqu'au plateau. Il est
imprimé le premier, en entier, en couches horizontales. Il ne porte aucune
amorce de branche : sur un tronc rond, chaque couche est un simple rond.

Là où une branche viendra se coller, le tronc est laissé **à nu** : ses
parois s'arrêtent au bord de cette zone et son remplissage arrive jusqu'à
la surface, où il reste apparent. La branche se soude ainsi sur le
remplissage, pas sur une paroi lisse.

Cette zone à nu est calculée **au plus juste** :

- la limite entre le tronc et la branche est la surface sur laquelle se pose
  la première couche de la branche ; elle est cherchée plus finement que les
  petits cubes de l'analyse (au dixième de millimètre) ;
- une paroi du tronc n'est retirée que là où une couche de la branche
  viendra **vraiment** la recouvrir (le point tombe dans le contour d'une
  couche courbe assez large pour recevoir un cordon). Partout ailleurs, le
  tronc garde sa peau jusqu'au ras de la branche ;
- les premières couches d'une branche arrivent souvent en biais sur la
  surface : chacune dépasse de la suivante sur une bande. Sur cette bande,
  ses cordons intérieurs restent visibles : ils sont imprimés comme des
  **parois extérieures** (même soin, même vitesse).

Sous le pied, le tronc est **rempli en plein** sur 1,5 mm : la première
couche de la branche se pose sur de la matière, pas sur les vides du
remplissage. Une « branche » très mince (moins de 1,7 mm d'épaisseur : une
pellicule, un collier autour du tronc) n'est pas découpée en couches
courbes : le tronc l'imprime avec sa propre paroi.

Une branche que le slicer ne sait pas découper en couches courbes (voir les
limites) est **rendue au tronc** : elle est imprimée avec lui, en couches
horizontales, et les branches qu'elle porte sont imprimées contre elle.

### Les branches : des couches qui épousent le tronc

Une **branche** est un morceau en surplomb accroché au tronc. Pour chaque
point de la branche, on calcule la longueur du chemin le plus court, dans
la branche, jusqu'à la surface du tronc. Une couche est l'ensemble des
points qui ont la même longueur de chemin :

- la première couche épouse la surface du tronc, quelle que soit sa forme
  (plate, ronde, bombée) ;
- les suivantes s'en écartent d'une épaisseur de couche à chaque fois, et
  se redressent peu à peu en travers de la branche ;
- l'épaisseur est régulière partout, car la longueur augmente de 1 mm par
  mm parcouru.

Une branche qui se divise est partagée en « tiges », d'une fourche à la
suivante ; une tige est imprimée après celle qui la porte. Un morceau trop
petit, ou trop bombé pour être dessiné sur une carte (un chapeau qui fait
tout le tour d'un pied), reste avec le tronc.

### Tracer sur une surface courbe

Les parois et le remplissage sont calculés par les outils habituels, qui
travaillent à plat :

1. le bord de la couche est relevé sur la peau de la branche, là où la
   longueur de chemin a la bonne valeur ;
2. la couche est regardée le long de sa direction moyenne, et son contour
   est dessiné sur un plan : sa **carte** ;
3. les trajectoires sont calculées sur la carte ;
4. chaque point est reposé sur la surface ; un segment est recoupé tant que
   la surface s'écarte de la ligne droite de plus de 0,02 mm.

Là où la surface est vue de biais, le débit est augmenté d'autant (dans la
limite de + 50 %).

### La tête, et ce que la machine permet

En chaque point, la buse prend l'inclinaison de la surface. Contre un tronc
vertical, cela veut dire une buse presque à l'horizontale : c'est exigeant
pour la machine.

**Déplacements stratégiques.** Quand la vraie forme de la machine est connue
(réglage « modele_machine »), chaque branche est essayée « à blanc » avant
d'être écrite, dans plusieurs positions : la tête penchée du côté le plus
proche, puis de l'autre côté (le plateau fait demi-tour), puis de moins en
moins penchée, de 10° en 10°. Chaque essai est contrôlé aussitôt contre la
machine et la matière déjà imprimée (`VerificationSuivie`, une vérification
« au fur et à mesure » qu'on peut copier pour repartir du même point). La
première position qui ne touche rien est gardée ; seule la branche en cause
est recalculée, pas tout le tranchage. S'il ne reste aucune position sans
contact, le tranchage est refusé.

**Petits chevauchements.** S'il ne reste que quelques endroits (une trentaine au plus) où un cordon
de branche chevauche de la matière déjà posée (au ras du pied, le plus
souvent), ces bouts de cordon de 3 mm environ ne sont pas imprimés, et une
remarque le dit.

Avec la forme réelle d'une tête, trois choses limitent cette stratégie :

- le **bloc chauffant** est tout près de la pointe : la buse doit rester
  bien en face de la surface, sinon il touche la matière ;
- près du plateau, l'inclinaison de la tête est limitée, et une tête à
  l'horizontale descend sous la pointe : une branche qui part trop bas ne
  peut pas être imprimée ainsi ;
- le **bras** qui porte la tête doit pouvoir passer à côté du tronc déjà
  imprimé.

## Les zones orientées : la direction choisie à la main

Le slicer ne trouve pas toujours seul une partie à incliner (une pièce sans
branche, par exemple). L'utilisateur peut alors désigner lui-même des
**zones** : un point de la pièce, une inclinaison et une direction.

À chaque zone, le modèle est coupé par un plan qui passe par le point et qui
est perpendiculaire à la direction demandée (`strategie_par_zones`, dans
`strategie.hpp`). Ce qui est sous le plan est imprimé d'abord, à plat ; ce qui
est au-dessus est imprimé ensuite en couches planes empilées le long de la
direction, la tête inclinée (B) et le plateau tourné (C). Une zone plus haute
peut être prise dans une zone déjà détachée : elle est alors portée par elle.

Contrôles faits avant de trancher, avec un message et une solution :
l'inclinaison demandée contre les butées de B, la limite près du plateau pour
une zone qui descend bas, et un plan qui ne détache rien. Le G-code passe
ensuite par le vérificateur comme les autres ; au ras du plateau, les petits
bouts de cordon qui chevaucheraient le bas déjà posé ne sont pas imprimés.

Dans l'interface : commande `trancher` avec `"zones"`, et `apercu_zones` pour
connaître les parties sans trancher. Une zone est limitée à 90° pour l'instant.

## La peau courbe (option)

Réglage « Peau courbe (dessus lisse) », onglet Qualité (`peau_courbe = 1`
dans un fichier de réglages). Elle s'ajoute à n'importe quelle stratégie.

Avec des couches planes, un dessus bombé est imprimé en marches d'escalier.
Avec cette option (`src/peau_courbe.*`) :

1. le slicer cherche le **dessus** de la pièce : les faces tournées vers le
   haut, sans rien au-dessus d'elles, dont la pente ne dépasse pas
   `peau_courbe_pente_max` (30° par défaut), avec assez de matière dessous ;
2. ce dessus est **abaissé** d'un peu plus d'une épaisseur de couche, et
   l'intérieur est tranché normalement : les couches planes s'arrêtent sous
   la surface ;
3. une **dernière passe** (un tour le long du bord, puis des allers-retours
   serrés) est reposée sur la vraie surface : la buse monte et descend pour
   la suivre.

Pendant cette passe, la tête s'incline pour rester d'aplomb sur la surface :
B et C changent en continu. Le plateau ne tourne pas de plus de 4° par
millimètre de cordon ; quand il ne peut pas suivre (près d'un sommet, où la
pente change de direction très vite), la tête penche au mieux dans le sens
que permet la position du plateau. Près d'un mur, elle se redresse pour ne
pas le toucher. Sans le réglage `distance_pivot_buse`, la passe est faite
buse verticale.

Si, malgré cela, le vérificateur trouve que la tête toucherait la pièce ou
la machine pendant cette passe (en s'inclinant près d'une branche voisine,
par exemple), le slicer **renonce à la peau** : le dessus est imprimé en
couches planes, et une remarque le dit.

Un dessus plat est laissé tel quel (il est déjà lisse). Deux dessus
voisins séparés par une falaise (le dessus d'un tronc et celui d'une
branche) sont traités séparément, et les endroits trop étroits pour deux
cordons sont laissés aux couches planes.

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
`trancher` accepte un champ `strategie` (`classique` ou `couches_courbes`).

## Réglages de la machine utilisés

- `distance_pivot_buse` : indispensable dès que la tête s'incline.
- `hauteur_bascule_b` : tant qu'elle n'est pas renseignée, l'inclinaison
  est limitée partout à `b_limite_pres_plateau`.
- `tete_demi_largeur`, `tete_hauteur` : l'encombrement simplifié de la tête,
  un bloc qui part du pivot et s'étend du côté opposé à la buse. Quand ils
  sont renseignés, la hauteur de bascule ne sert plus : pour chaque hauteur
  de la pointe, le slicer calcule jusqu'où la tête peut s'incliner en restant
  à la marge de sécurité du plateau (`inclinaison_permise`, dans
  `cinematique.hpp`). Par prudence, toute la tête est comptée à la largeur du
  bloc, de la pointe de la buse au bout du bloc. La limite près du plateau
  reste permise partout. Ce calcul ne regarde que le plateau : la tête contre
  la pièce déjà imprimée demande le vrai modèle de la machine.
- `b_min`, `b_max` : inclinaisons extrêmes, comptées depuis la verticale.
- `b_inclinaison_max` : limite du firmware (50° par défaut), jamais dépassée.
- `c_min`, `c_max` : limites du plateau (-360° et +360° par défaut).
- `vitesse_b_max`, `vitesse_c_max` : vitesses de rotation du firmware, pour
  estimer la durée.
- `contrat_gcode` : format du fichier (voir plus haut).
- `b_vertical` : valeur de B, dans le G-code, quand la buse est verticale
  (0 par défaut). À régler si la machine affiche une autre valeur pour la
  buse verticale.
- `pivot_continu` : 1 (oui) par défaut. Sur une branche courbe, la tête
  pivote pendant qu'elle imprime. Avec 0 (non), elle pivote entre deux
  couches, sans extruder : plus prudent pour un premier essai sur la machine.
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

Pour les couches courbes, ils contrôlent que le tronc et les branches sont
bien séparés, que le tronc imprimé est lisse (aucun cordon hors de son
rayon), qu'il est à nu là où une branche viendra (aucune paroi dans cette
zone), que la première couche d'une branche épouse le tronc, que le tronc
est imprimé en entier avant les branches et que le G-code est accepté.

Les tests (`tests/test_strategie.cpp`) fabriquent deux arbres, les tranchent
par branches, exigent zéro erreur du vérificateur, puis provoquent des
fautes (une branche imprimée deux fois, une inclinaison trop forte) pour
s'assurer qu'elles sont bien refusées. Ils raccourcissent aussi la course
d'un axe pour s'assurer que la stratégie proposée s'adapte et reste
tranchable, et contrôlent le pivot continu (jamais plus de 0,25° par ligne).

## Limites connues

- **Couches courbes : exigeantes pour la machine.** Avec la forme réelle
  d'une tête, une branche basse ou serrée contre un tronc haut est souvent
  refusée (voir plus haut).
- **Couches courbes : des formes ne sont pas encore traitées** : une
  fourche où le tronc s'arrête, une arche ou une anse (attaquées par leurs
  deux bouts), un chapeau ou un plateau qui fait tout le tour d'un pied,
  des bras qui partent tout près du plateau. Vingt modèles d'essai suivent
  ces progrès : voir [essais-vingt-modeles.md](essais-vingt-modeles.md).
- **Couches courbes : proposées seulement si elles servent** (au moins 20 %
  de surface en surplomb en moins que l'impression classique).
- **Couches courbes : pas de peau courbe** sur le tronc avec cette stratégie.
- **Déplacements stratégiques : l'orientation seulement.** Le slicer choisit
  le côté et l'inclinaison de la tête pour chaque branche ; il ne change pas
  encore l'ordre des branches ni le trajet des déplacements à vide.
- **La vitesse d'avance n'est pas corrigée pour les axes de rotation.** Le
  « F » d'une ligne est la vitesse voulue pour la pointe ; la façon dont le
  firmware la partage entre les cinq axes reste à vérifier sur la machine
  (les rotations sont très lentes ici : quelques dixièmes de degré par
  couche).
- **Peau courbe : tout ou rien.** Si la tête toucherait quelque chose à un
  seul endroit, la peau est abandonnée pour toute la pièce. La pente suivie
  est limitée (30° par défaut) : le bas d'un dôme reste en marches. Une
  seule passe, d'une seule épaisseur.
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
4. Couches courbes : choisir l'orientation de la tête d'après la forme de
   la machine avant de trancher (au lieu d'essayer puis de recommencer),
   mettre du remplissage plein sous le pied des branches, et traiter les
   formes qui s'élargissent.
5. Choix de la stratégie dans le logiciel installé (aujourd'hui : ligne de
   commande, interface de programmation et maquette).
