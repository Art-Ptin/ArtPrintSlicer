# Essai de masse : 2 681 modèles

Un essai de nuit du slicer sur 2 681 modèles différents, chacun avec ses
propres réglages tirés au hasard. Le but : trouver les défauts que les tests
habituels ne voient pas, et mesurer honnêtement ce que le slicer sait faire.

## Les modèles

- **1 420 modèles fabriqués** par `tests/generateur_modeles.cpp` : 25 familles
  de formes (branches, vases, pieuvres, coraux, engrenages, tasses...), chaque
  modèle avec ses propres dimensions. Le générateur ne garde que les modèles
  propres : surface fermée, bien orientée, d'un seul morceau, contrôlée sur le
  fichier STL relu.
- **1 261 pièces réelles**, une pièce par fichier, prises dans des projets
  publics : Voron (2, Trident, 0, Switchwire, Stealthburner, Legacy,
  Afterburner), Original Prusa i3, Jubilee, Thor, Poppy Humanoid, Mendel90, et
  trois collections de modèles d'essai (common-3d-test-models,
  libigl-tutorial-data, meshes). Ces fichiers ne sont pas copiés dans ce
  dépôt (chacun a sa licence) ; `docs/essais/essai-de-masse-liste.jsonl` dit
  d'où vient chaque pièce.

Les grandes banques de fichiers (Thingi10K, Thingiverse, Printables) n'étaient
pas joignables depuis la machine d'essai.

## Ce qui varie d'un modèle à l'autre

Pour chaque modèle, `outils/essais_de_masse/campagne.py` tire au hasard : la
taille, la pose sur le plateau (parfois couché ou retourné), la stratégie
(ondulation ou classique), le chemin d'entrée (ligne de commande ou commandes
de l'interface), et les réglages : largeur et hauteur de cordon, parois,
remplissage, jupe, vitesses, rétraction, limites de B (50, 70, 90 ou 110°),
distance pivot-buse, hauteur de bascule. Ce sont des valeurs d'essai, pas
celles d'une vraie machine.

Chaque G-code accepté par le slicer est ensuite relu par le banc, sans passer
par le slicer.

## Ce que l'essai a fait corriger

- parois qui se chevauchaient dans les murs fins ;
- jupe qui se pinçait entre deux parties proches ;
- remplissage dont les liaisons traversaient une paroi ;
- calculs trop longs (grille de balayage, plan des branches gardé en mémoire,
  budget de temps pour les reprises) ;
- modèles à beaucoup de branches qui restaient refusés : au dernier repli,
  toutes les branches restantes passent à plat ;
- générateur : deux parois collées dans le fichier STL n'étaient pas vues.

## Limites de cet essai

- Le slicer a été corrigé pendant la nuit. Les cas refusés ou trop longs ont
  été rejoués avec la dernière version ; les cas déjà vérifiés avec une version
  précédente n'ont pas tous été rejoués.
- « Vérifié » veut dire : accepté par le vérificateur du slicer et par le
  contrôle indépendant. Aucun de ces fichiers n'a été imprimé.
- Les collisions avec la vraie forme de la machine ne sont pas testées ici (le
  modèle de machine est privé et absent de la machine d'essai).
- Sur près de deux modèles sur trois en ondulation, la tête ne s'incline pas :
  le modèle n'a pas de branche, ou ses branches ont été repassées à plat par
  sécurité.
- Les erreurs sont des refus avant tranchage, avec un message : presque toutes
  viennent de fichiers réels dont la surface n'est pas fermée. Le slicer ne
  répare pas encore ces fichiers.
- 29 modèles demandaient plus de 200 s ; rejoués avec plus de temps, ils sont
  tous vérifiés (le plus long : 436 s).

## Refaire l'essai

    python outils/essais_de_masse/preparer.py <generateur_modeles> <dossier>
    python outils/essais_de_masse/campagne.py <artprint-slicer> <dossier> --generateur=<generateur_modeles>
    python outils/essais_de_masse/bilan.py <dossier>/resultats-1.jsonl

Les résultats complets, modèle par modèle : `docs/essais/essai-de-masse.jsonl`.

## Résultats

**2681 modèles tranchés.** vérifié : 2560 (95.5 %), erreur : 121 (4.5 %).

## Par origine

| | modèles | vérifié | erreur | part vérifiée |
|---|---:|---:|---:|---:|
| modèles fabriqués | 1420 | 1419 | 1 | 99.9 % |
| pièces réelles | 1261 | 1141 | 120 | 90.5 % |

## Par stratégie demandée

| | modèles | vérifié | erreur | part vérifiée |
|---|---:|---:|---:|---:|
| ondulation | 1832 | 1755 | 77 | 95.8 % |
| classique | 849 | 805 | 44 | 94.8 % |

## Par chemin d'entrée

| | modèles | vérifié | erreur | part vérifiée |
|---|---:|---:|---:|---:|
| ligne de commande | 2106 | 2009 | 97 | 95.4 % |
| commandes de l'interface | 575 | 551 | 24 | 95.8 % |

## Par inclinaison maximale de la tête (ondulation seulement)

| | modèles | vérifié | erreur | part vérifiée |
|---|---:|---:|---:|---:|
| 50° | 299 | 286 | 13 | 95.7 % |
| 70° | 298 | 288 | 10 | 96.6 % |
| 90° | 638 | 613 | 25 | 96.1 % |
| 110° | 597 | 568 | 29 | 95.1 % |

## Par famille de modèles fabriqués

| | modèles | vérifié | erreur | part vérifiée |
|---|---:|---:|---:|---:|
| branche_simple | 90 | 90 | 0 | 100.0 % |
| vase | 90 | 90 | 0 | 100.0 % |
| couronne | 80 | 80 | 0 | 100.0 % |
| branche_courbe | 70 | 70 | 0 | 100.0 % |
| corail | 70 | 70 | 0 | 100.0 % |
| pieuvre | 70 | 70 | 0 | 100.0 % |
| etages | 60 | 60 | 0 | 100.0 % |
| fourches | 60 | 60 | 0 | 100.0 % |
| socle_bras | 60 | 60 | 0 | 100.0 % |
| arche | 55 | 55 | 0 | 100.0 % |
| cactus | 55 | 54 | 1 | 98.2 % |
| creature | 55 | 55 | 0 | 100.0 % |
| surplomb | 55 | 55 | 0 | 100.0 % |
| tasse | 55 | 55 | 0 | 100.0 % |
| tuyau | 55 | 55 | 0 | 100.0 % |
| equerre | 50 | 50 | 0 | 100.0 % |
| blob | 45 | 45 | 0 | 100.0 % |
| boite | 45 | 45 | 0 | 100.0 % |
| champignon | 45 | 45 | 0 | 100.0 % |
| coupe | 45 | 45 | 0 | 100.0 % |
| spirale | 45 | 45 | 0 | 100.0 % |
| tronc_penche | 45 | 45 | 0 | 100.0 % |
| cylindres | 40 | 40 | 0 | 100.0 % |
| engrenage | 40 | 40 | 0 | 100.0 % |
| plaque | 40 | 40 | 0 | 100.0 % |

## Par dépôt de pièces réelles

| | modèles | vérifié | erreur | part vérifiée |
|---|---:|---:|---:|---:|
| mendel90 | 231 | 229 | 2 | 99.1 % |
| voron-2 | 155 | 152 | 3 | 98.1 % |
| voron-0 | 133 | 122 | 11 | 91.7 % |
| jubilee | 122 | 120 | 2 | 98.4 % |
| voron-trident | 100 | 100 | 0 | 100.0 % |
| voron-switchwire | 77 | 77 | 0 | 100.0 % |
| thor | 72 | 68 | 4 | 94.4 % |
| meshes | 64 | 43 | 21 | 67.2 % |
| voron-legacy | 57 | 56 | 1 | 98.2 % |
| voron-stealthburner | 53 | 45 | 8 | 84.9 % |
| poppy-humanoid | 52 | 24 | 28 | 46.2 % |
| original-prusa-i3 | 45 | 39 | 6 | 86.7 % |
| libigl-tutorial-data | 39 | 21 | 18 | 53.8 % |
| voron-afterburner | 37 | 33 | 4 | 89.2 % |
| common-3d-test-models | 24 | 12 | 12 | 50.0 % |

## Ce que l'ondulation a fait

- 1755 modèles vérifiés en ondulation ;
- sur 641 d'entre eux, la tête s'incline vraiment (les autres n'ont pas de branche : c'est le même tranchage qu'en classique) ;
- sur 96, la tête dépasse 90° (impression par en dessous).
- 500 : au moins une partie repassée à plat ;
- 345 : des petits bouts de cordon retirés au ras d'une branche ;
- 206 : au moins une partie bridée par la limite d'inclinaison ;

## Les refus, par type de faute

Aucun.

## Les erreurs, par message

- 111 : Le modèle n'est pas un volume fermé
- 8 : La première couche est vide
- 1 : La couche 83 (à 19.80 mm) est vide
- 1 : La couche 337 (à 40.46 mm) est vide

## Contrôle indépendant des G-code vérifiés

2560 fichiers relus par le banc, sans passer par le slicer : première ligne du contrat, cinq axes sur chaque déplacement, pointe jamais sous le plateau, inclinaison et rotation dans les limites, rétraction avant chaque déplacement à vide.
Aucune faute trouvée.

## Temps de calcul

Médiane : 15 s par modèle ; 9 sur 10 en moins de 95 s ; le plus long : 436 s ; total : 24.4 heures de calcul.
