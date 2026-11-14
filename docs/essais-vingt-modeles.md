# Les vingt modèles d'essai des couches courbes

Vingt modèles servent à « entraîner » le slicer : on les tranche tous en
couches courbes, on regarde ce qui est refusé ou mal imprimé, on corrige,
et on recommence. Chacun éprouve une difficulté précise.

Ils sont décrits dans `tests/modeles_vingt.hpp`. Le banc d'essai les
fabrique et les tranche :

```
build/linux/tests/banc_strategies - dossier_de_sortie v
```

(« v » ne garde que les modèles dont le nom contient cette lettre ; ajouter
`stl` à la fin écrit seulement les fichiers STL, sans trancher.)

## Soixante modèles, aucun refus

Aux vingt premiers modèles s'ajoutent quarante autres
(`tests/modeles_quarante.hpp`, noms en « w01_ »... « w40_ ») : des familles où
un seul réglage change (une branche de 35° à 100°, de deux à huit branches en
couronne, des fourches de 25° à 70°, des arcs, des hélices, des ponts...), et
une pièce « banc d'essai d'imprimante » (éventail de surplombs de 30° à 90°,
pont, colonne fine et boule, pointe, bras courbe à coupelle).

Sur les soixante, **aucun n'est refusé** :

- 52 sont tranchés avec la stratégie « couches courbes » et vérifiés ;
- 8 n'ont que la stratégie classique, parce que le slicer ne sait pas encore
  les découper en couches courbes : les trois ponts, l'arche, l'anse, le
  champignon, la table et l'arc en demi-tour.

Ces chiffres ont été mesurés de nouveau avec les limites du firmware
(contrat G-code version 1 : B limité à 50°, C entre -360° et +360°) : même
résultat, 52 en couches courbes et 8 en classique, tous vérifiés. Mais sur
50 des 52, au moins une branche demande plus de 50° d'inclinaison : la tête
s'arrête à 50°, elle n'est pas d'aplomb sur ces couches, et le slicer le
signale. Le rendu réel de ces branches reste à juger sur la machine.

Avec une machine dont la tête peut basculer jusqu'à 110° (butées et
inclinaison maximale de B réglées à 110), les soixante modèles sont encore
tous vérifiés : 53 en couches courbes (l'arc en demi-tour s'y ajoute), 7 en
classique. Sur 14 modèles, la tête dépasse 90° à certains endroits (jusqu'à
110° pour la branche qui retombe, le tronc penché et l'arc en demi-tour), et
plus aucune branche n'est bridée par la limite. Ces essais sont faits sans
le modèle de la machine : les collisions du corps de la tête, qui passe plus
bas que la buse à ces angles, restent à contrôler avec la vraie forme.

Parmi les 52, quelques-uns ont une ou plusieurs branches imprimées **à plat,
avec le tronc** : le socle à quatre bras (ses quatre bras, trop près du
plateau), le tronc penché (une branche), le banc d'essai (un morceau). C'est
le « repli » : quand une branche provoquerait des fautes en couches courbes,
le slicer l'imprime en couches horizontales avec le tronc et refait le
tranchage, au lieu de refuser toute la pièce. Une remarque le dit.

## Choisir l'orientation d'une branche

Le même mécanisme sert au choix de l'utilisateur. Dans l'application, avec
la stratégie « couches courbes », chaque branche a sa liste :

- couches courbes, tête d'aplomb (d'office) ;
- couches courbes, tête moins inclinée (60° ou 45° au plus) ;
- à plat, avec le tronc (couches horizontales).

La jonction avec le reste de la pièce est recalculée toute seule. En ligne
de commande : `--a-plat=3,5` après `--strategie=2`. Par l'interface de
commandes : le champ `branches` de la commande `trancher`.

## Résultats des vingt premiers


Machine idéale d'essai (aucune forme de machine), réglages par défaut.
« Vérifié » : le G-code passe le vérificateur sans aucune faute.

| N° | Modèle | Ce qu'il éprouve | Résultat |
|---|---|---|---|
| 1 | Branche horizontale | tête couchée à 90° | ✅ vérifié |
| 2 | Branche raide (20°) | pied très allongé | ✅ vérifié |
| 3 | Branche tombante (110°) | branche qui retombe | ✅ vérifié |
| 4 | Socle et quatre bras | bras qui partent à 5 mm du plateau | ✅ vérifié, ses quatre bras à plat |
| 5 | Étoile | six branches au même niveau | ✅ vérifié |
| 6 | Spirale | huit branches en hélice | ✅ vérifié |
| 7 | Fourche en Y | le tronc s'arrête et se partage | ✅ vérifié (7 bouts de cordon retirés) |
| 8 | Fourches en cascade | quatorze branches sur trois niveaux | ✅ vérifié (2 bouts retirés) |
| 9 | Tronc penché (15°) | le tronc lui-même penche | ✅ vérifié, une branche à plat |
| 10 | Tronc carré | branche sur une face plate, près d'une arête | ✅ vérifié |
| 11 | Branche carrée | arêtes vives | ✅ vérifié |
| 12 | Branche en S | branche qui change deux fois de direction | ✅ vérifié |
| 13 | Arche | arc attaqué par ses deux bouts | ⬜ 3 axes seulement |
| 14 | Anse | anneau accroché en deux endroits | ⬜ 3 axes seulement |
| 15 | Champignon | chapeau tout autour du pied | ⬜ 3 axes seulement |
| 16 | Table | plateau large sur un pied fin | ⬜ 3 axes seulement |
| 17 | Trompette | branche qui s'élargit | ✅ vérifié |
| 18 | Fin et gros | diamètres très différents | ✅ vérifié (1 bout retiré) |
| 19 | Sucette | boule au bout d'une tige | ✅ vérifié |
| 20 | Corail | quatorze branches et rameaux | ✅ vérifié (5 bouts retirés ; une branche imprimée avec le tronc) |

Seize modèles sur vingt sont vérifiés avec la stratégie couches courbes (deux d'entre eux avec des branches à plat).

## Ce que la campagne a fait corriger

- **Première couche trop enroulée.** Sur un tronc rond, la première couche
  d'une branche presque aussi large que lui s'enroule autour de lui. Avant,
  toute la branche était abandonnée (imprimée avec le tronc). Maintenant,
  sur les quatre ou cinq premières couches, on ne garde que la partie que la
  tête peut atteindre depuis sa direction ; les bords très enroulés, une
  fine pellicule, restent imprimés par le tronc.
- **Trajectoires qui sortent de la couche.** Un bout de trajectoire qui ne
  trouve pas la surface est retiré, au lieu de faire refuser toute la pièce.
- **Tronc qui penche.** Le slicer cherche une direction de tronc pour tout
  le modèle (droite, ou penchée jusqu'à 27° environ).
- **Pellicules et colliers.** Une « branche » de moins de 1,7 mm
  d'épaisseur accrochée au tronc (une pellicule, ou le collier que forment
  des raccords arrondis qui se rejoignent) est imprimée par le tronc, avec
  sa paroi lisse. C'est ce qui a débloqué la fourche en Y et nettoyé
  l'étoile.
- **Pied plein.** Sous le pied d'une branche, le tronc est rempli en plein
  sur 1,5 mm : la première couche de la branche se pose sur de la matière.
- **Calcul inutile.** Un modèle sans surplomb ne déclenche plus le calcul
  des couches courbes.

## Ce qui reste à faire

- **Branche sous un tronc penché (n° 9).** Sa première couche regarde vers
  le bas.
- **Bras qui partent près du plateau (n° 4).** Sous la hauteur de bascule,
  la tête ne peut pas s'incliner assez : c'est une limite de la machine. Le
  slicer devrait imprimer le bas de ces bras avec le socle.
- **Arche et anse (n° 13 et 14).** La branche est attaquée par ses deux
  bouts ; la moitié du haut devrait être imprimée tête en bas.
- **Chapeau et table (n° 15 et 16).** La couche fait tout le tour du pied :
  il faut une couche « en anneau », que la carte plane ne sait pas dessiner.
- **Bord du pied.** Il reste un léger liseré de petits reliefs là où la
  paroi du tronc s'arrête et où la branche commence.

## Et les pièces de test qui existent déjà

La pièce de test d'imprimante la plus connue est le **3DBenchy** (un petit
bateau de 60 mm, de Creative Tools). Il est dans le domaine public (licence
CC0) depuis 2025 ; ses fichiers STL et STEP se téléchargent sur 3dbenchy.com
et sur le dépôt GitHub `CreativeTools/3DBenchy`. Le fichier n'est pas copié
dans ce dépôt : il suffit de le télécharger et de l'ouvrir dans le slicer.

Résultat : le 3DBenchy est tranché et vérifié en 3 axes (240 couches). Les
couches courbes ne lui sont pas proposées : ses surplombs sont un toit, des
arches et des ouvertures, les formes que le slicer ne sait pas encore
découper en couches courbes (voir plus haut). C'est un bon objectif.

D'autres pièces de test courantes : le test « tout-en-un » (surplombs,
ponts, trous, cotes), le cube de calibration, et le test de Kickstarter et
Autodesk. Leurs licences sont à vérifier une par une avant de les utiliser.
