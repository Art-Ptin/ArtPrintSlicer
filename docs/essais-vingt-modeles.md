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

## Résultats

Machine idéale d'essai (aucune forme de machine), réglages par défaut.
« Vérifié » : le G-code passe le vérificateur sans aucune faute.

| N° | Modèle | Ce qu'il éprouve | Résultat |
|---|---|---|---|
| 1 | Branche horizontale | tête couchée à 90° | ✅ vérifié |
| 2 | Branche raide (20°) | pied très allongé | ✅ vérifié |
| 3 | Branche tombante (110°) | branche qui retombe | ✅ vérifié |
| 4 | Socle et quatre bras | bras qui partent à 5 mm du plateau | ❌ refusé |
| 5 | Étoile | six branches au même niveau | ✅ vérifié (4 bouts de cordon retirés) |
| 6 | Spirale | huit branches en hélice | ✅ vérifié (3 bouts retirés) |
| 7 | Fourche en Y | le tronc s'arrête et se partage | ❌ refusé |
| 8 | Fourches en cascade | quatorze branches sur trois niveaux | ✅ vérifié (2 bouts retirés) |
| 9 | Tronc penché (15°) | le tronc lui-même penche | ❌ refusé |
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
| 20 | Corail | seize branches et rameaux | ✅ vérifié (27 bouts retirés ; une branche imprimée avec le tronc) |

Treize modèles sur vingt sont vérifiés en couches courbes.

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
- **Calcul inutile.** Un modèle sans surplomb ne déclenche plus le calcul
  des couches courbes.

## Ce qui reste à faire

- **Fourche où le tronc s'arrête (n° 7).** Le tronc « continue » dans le
  début des deux bras, qui sont alors attaqués de côté. Il faudrait arrêter
  le tronc à la fourche.
- **Branche sous un tronc penché (n° 9).** Sa première couche regarde vers
  le bas.
- **Bras qui partent près du plateau (n° 4).** Sous la hauteur de bascule,
  la tête ne peut pas s'incliner assez : c'est une limite de la machine. Le
  slicer devrait imprimer le bas de ces bras avec le socle.
- **Arche et anse (n° 13 et 14).** La branche est attaquée par ses deux
  bouts ; la moitié du haut devrait être imprimée tête en bas.
- **Chapeau et table (n° 15 et 16).** La couche fait tout le tour du pied :
  il faut une couche « en anneau », que la carte plane ne sait pas dessiner.
- **Coin à nu entre deux branches voisines (n° 5).** Entre deux pieds très
  proches, un petit coin du tronc reste sans paroi.
