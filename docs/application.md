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
| Ajouter un plateau | crée un plateau de plus, dessiné à côté, presque transparent, avec son numéro ; un clic dessus (ou sur son bouton) le choisit |
| Orientation automatique | retourne le modèle dans la pose qui demande le moins de supports |
| Agencer les objets | remet l'objet au centre du plateau |
| Déplacer | place la pièce à une distance donnée du centre, en X et en Y |
| Tourner | trois angles, autour de X, de Y et de Z, depuis la pose du fichier |
| Mettre à l'échelle | échelle en pour cent de la taille d'origine |
| Poser sur une face | choisit laquelle des six faces va contre le plateau, ou la pose automatique |
| Mesurer | dimensions, volume, poids plein, position, surface à soutenir |
| Stratégie de tranchage | mène au choix de la stratégie |
| Direction d'impression par zone | lance l'ondulation et sa fenêtre d'options |
| Simulation de la machine | fait défiler les couches du G-code, de la première à la dernière |

Toutes ces transformations passent par la commande `transformer_modele` du
slicer (échelle, rotation, déplacement, orientation automatique) : le
tranchage se fait sur la pièce telle qu'elle est posée.

**Sept outils affichent encore « à venir »** : hauteur de couche variable,
couper, opérations booléennes, assembler, texte en relief, peindre les
supports, peindre la couture. Ils demandent des fonctions que le cœur du
slicer n'a pas (plusieurs objets, découpe de maillage, supports).

La stratégie se choisit avec deux cartes, « Classique » et « Ondulation »,
qui disent chacune ce qu'elle laisserait à soutenir.

Dans la vue 3D, la pièce non tranchée est montrée **lisse** (on ne voit pas
les triangles du fichier, seulement ses vraies arêtes). Le plateau est noir,
gradué tous les 25 mm à partir de 50 mm, avec le nom du logiciel à l'avant.

## Limites connues

- **Un seul objet par plateau.** « Agencer » ne fait donc que recentrer.
- **Poser sur une face** propose les six faces de la boîte de la pièce ; on
  ne clique pas encore une face quelconque dans la vue.
- **Les plateaux voisins** sont dessinés vides : leur pièce n'apparaît que
  lorsqu'on les choisit.
- **La barre d'avancement** ne montre pas un pourcentage réel : elle est
  animée pendant le calcul, pleine quand c'est fini.
- **Supports** : toujours 0 g, le slicer n'en fabrique pas.
- **La simulation** ne recalcule rien : les options de l'ondulation,
  l'échelle et l'orientation n'y changent pas le tranchage affiché.
- **Pages Imprimante, Projet, Calibration, Gestionnaire** : ce sont encore
  des maquettes, non reliées à une vraie imprimante.
