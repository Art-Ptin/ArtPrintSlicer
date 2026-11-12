# L'application : une seule interface

Le logiciel installé et la simulation ont **la même interface** : celle du
dossier `maquette/`. Seul le « pont » change, le petit fichier qui parle au
slicer :

- `maquette/pont.js` : pont de **simulation**. Il rejoue des tranchages
  enregistrés ; il sert à essayer l'interface dans un navigateur.
- `maquette/pont-reel.js` : le **vrai** pont. Il envoie chaque demande au
  cœur du slicer (C++), par WebView2 dans le logiciel installé.

À la construction, `outils/assembler_interface.cmake` rassemble l'interface du
logiciel : les fichiers de la maquette, le vrai pont à la place du pont de
simulation, la vue 3D et three.js. L'installateur embarque ce dossier.

## L'ondulation

La stratégie 5 axes s'appelle **Ondulation**. Elle est proposée pour **tout**
modèle fermé, même quand elle n'apporte rien : une remarque le dit, et la
durée est comparée à celle du classique après le tranchage.

Quand on clique sur « Trancher le plateau » avec l'ondulation, une fenêtre
demande comment imprimer chaque partie inclinée :

- ondulation, tête d'aplomb sur les couches (d'office) ;
- ondulation, tête moins inclinée (60° ou 45° au plus) ;
- à plat, avec le corps de la pièce (couches horizontales).

La jonction avec le reste de la pièce est refaite toute seule.

Le slicer ne refuse pas un modèle que le classique sait imprimer : une
partie qu'il ne sait pas imprimer en ondulation est imprimée à plat, et une
remarque le signale. Au pire, l'ondulation donne le même tranchage que le
classique.

## Après le tranchage

En haut de la vue, les **caractéristiques** :

- filament : grammes et mètres (d'après la bobine montée) ;
- supports : 0 g (le slicer n'en fabrique pas encore) ;
- préparation : chauffe et palpage du plateau, 5 minutes d'office (réglage
  « duree_preparation ») ;
- impression : durée et nombre de couches ;
- durée totale (préparation + impression) et coût ;
- pour l'ondulation : l'écart de durée avec le classique.

En bas, à droite : l'**état** (« G-code vérifié »...), une petite barre
d'avancement, et tout à droite le bouton des **messages** du tranchage.

## Outils branchés

| Outil | Ce qu'il fait |
|---|---|
| Ajouter un objet | ouvre un modèle dans le plateau courant |
| Ajouter un plateau | crée un plateau de plus ; chaque plateau a son modèle |
| Orientation automatique, Poser sur une face | retourne le modèle dans la pose qui demande le moins de supports (commande `transformer_modele`) |
| Mettre à l'échelle | échelle en pour cent de la taille d'origine |
| Agencer les objets | recentre l'objet sur le plateau |
| Direction d'impression par zone | lance l'ondulation et sa fenêtre d'options |

Les autres outils (déplacer, tourner à la main, couper, booléens, texte,
peinture des supports et de la couture, mesure...) affichent encore « à
venir ».

## Limites connues

- **Un seul objet par plateau.** « Agencer » ne fait donc que recentrer.
- **Poser sur une face** choisit la pose tout seul ; on ne clique pas encore
  la face voulue.
- **La barre d'avancement** ne montre pas un pourcentage réel : elle est
  animée pendant le calcul, pleine quand c'est fini.
- **Supports** : toujours 0 g, le slicer n'en fabrique pas.
- **La simulation** ne recalcule rien : les options de l'ondulation,
  l'échelle et l'orientation n'y changent pas le tranchage affiché.
- **Pages Imprimante, Projet, Calibration, Gestionnaire** : ce sont encore
  des maquettes, non reliées à une vraie imprimante.
