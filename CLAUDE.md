# ArtPrint-Slicer

## Méthode « salle blanche » (obligatoire)

- Tout le code est écrit à partir de zéro. La seule source autorisée sur Unlayered est le document de spécifications privé, voir CLAUDE.local.md.

## Le projet

- ArtPrint Slicer : slicer 5 axes continu, open source, licence AGPL v3.
- Langage C++20 avec CMake, interface en HTML/JavaScript dans WebView2, installateur NSIS, Windows 64 bits.

## La machine

- La tête s'incline en B autour d'un pivot. Le plateau rond (rayon 150 mm) tourne en C. Hauteur d'impression 260 mm.
- La distance entre le pivot B et la pointe de la buse est un réglage, à mesurer sur la machine. Aucune valeur fixe dans le code.
- Firmware Klipper sur carte BTT Octopus, avec B et C déclarés comme axes G-code. Format des lignes : G0/G1 X Y Z B C F, angles en degrés.
- Objectif : 5 axes continu, B et C peuvent bouger pendant l'extrusion (Unlayered ne fait que du 3+2).

## Physique, à vérifier avant chaque livraison

- Jamais de couche imprimée dans ou sous une matière déjà imprimée.
- Les déplacements à vide sont distincts des extrusions et affichés d'une autre couleur dans l'aperçu.
- Rétraction avant chaque déplacement à vide.
- C passe toujours par le chemin le plus court (de +170° à -170°, il tourne de 20°, pas de 340°).
- Les limites des axes B et C sont vérifiées.

## Façon de travailler

- Tu me parles en français, simplement : je débute en programmation.
- Chaque commande que je dois taper va dans son propre bloc à copier, une commande par bloc.
- Aucun commit ni push sans mon accord, après ma relecture.

## Collisions avec l'imprimante

Règles permanentes pour la détection de collisions. Elles s'appliquent à tout le code du slicer.

### Forme de la machine

- Le slicer vérifie les collisions avec la **vraie forme de l'imprimante**, pas avec une pyramide ou un cône approximatif comme le fait Unlayered.
- La forme vient du **fichier STEP de l'assemblage complet**, placé dans le dossier `machine/` à la racine du projet.
- **Le dossier `machine/` est PRIVÉ.** Il est dans le `.gitignore` et ne doit jamais partir sur GitHub : le dépôt est public et la mécanique doit rester secrète. Ne jamais copier de géométrie, de cotes ou d'extraits du STEP dans le code, les tests, les commentaires, les messages de commit ou la documentation versionnée.
- Le chemin du STEP est lu depuis un **réglage**, jamais écrit en dur dans le code.
- Le modèle de l'imprimante va encore changer : on doit pouvoir **remplacer le STEP sans toucher au code**. Tout ce qui dépend de la machine (groupes, axes, butées, pivots) vient du STEP et des réglages, pas de constantes dans le code.

### Groupes mobiles

Les pièces du STEP sont séparées en groupes qui bougent ensemble. Pour chaque groupe, on note son **axe de mouvement** et ses **butées** :

| Groupe | Mouvement |
|---|---|
| Bâti fixe | aucun |
| Chariot Y + plateau | translation Y |
| Plateau rotatif | rotation C |
| Axe Z | translation Z |
| Chariot X | translation X |
| Tête | inclinaison B autour de son pivot |

### Ce qui est testé

- **Chaque déplacement** est testé : extrusions **et** déplacements à vide.
- On vérifie **plusieurs positions intermédiaires** le long du mouvement, pas seulement le point d'arrivée, surtout quand B ou C changent pendant le mouvement (5 axes continu).
- Trois types de contact sont testés :
  1. la tête et le chariot contre la **matière déjà imprimée** ;
  2. la tête et le chariot contre le **plateau** ;
  3. contre le **bâti** ou les **autres pièces de la machine**.

### Réglages

- **Marge de sécurité** : réglable, **2 mm** par défaut.
- **Inclinaison de B** : limitée à **55°** près du plateau, jusqu'à la limite de la machine plus haut (réglages « butées de B » et « inclinaison maximale de B » ; au-delà de 90°, la buse imprime par en dessous). La **hauteur de bascule** entre les deux limites est un réglage (valeur à mesurer sur la machine, pas de valeur inventée dans le code).
- **Chemin du fichier STEP** : réglage (voir plus haut).

### En cas de collision

Le tranchage est **refusé**, avec un message **en français** qui indique :

- la ligne de G-code concernée ;
- la position de la machine (X, Y, Z, B, C) ;
- les deux pièces qui se touchent ;
- une solution possible.
