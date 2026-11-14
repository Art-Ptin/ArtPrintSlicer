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

Quand la carte « Ondulation » est choisie, un panneau **Ondulation · 5 axes**
s'ouvre dans la colonne de gauche, sous les deux cartes. Il n'y a plus de
fenêtre au moment de trancher : tout se règle là.

### Les zones orientées

Une zone dit : « à partir de ce point de la pièce, imprime dans une autre
direction ». Le bas de la pièce est imprimé à plat ; ce qui est au-dessus du
plan de coupe est imprimé ensuite, couche par couche le long de la direction
choisie, la tête inclinée et le plateau tourné.

- **Ajouter une zone en cliquant** : le bouton met la souris en attente, puis
  un clic sur la pièce pose la zone à cet endroit. L'outil « Direction
  d'impression par zone » de la barre fait la même chose.
- **Le haut en diagonale** : pose une zone à mi-hauteur, inclinée de 45°.
- Chaque zone a trois réglages (un curseur et un champ) : l'**inclinaison**
  (0° = tête verticale), la **direction** (le côté vers lequel les couches
  penchent : 0° vers la droite du plateau, 90° vers le fond) et la
  **hauteur** du point de coupe.
- Dans la vue, un disque montre le plan de coupe et une flèche la direction
  d'impression.
- Sous les zones, le slicer dit ce qu'elles donnent sans attendre le
  tranchage : le nombre de parties, leur volume, ce qui reste à soutenir, ou
  pourquoi une zone ne peut pas être imprimée (avec une solution).

Les zones passent par le même vérificateur que le reste : un G-code où la
tête toucherait la pièce, ou imprimerait dans la matière, est refusé.

Limites : une zone ne dépasse pas 90° d'inclinaison pour l'instant ; près du
plateau elle est limitée comme tout le reste (limite près du plateau, puis
encombrement de la tête ou hauteur de bascule) ; si le modèle est déplacé, tourné ou mis à l'échelle,
les zones sont retirées (leurs points ne seraient plus au bon endroit). Avec
des zones, les parties que le slicer trouve tout seul ne sont pas utilisées.

### Sans zone : les parties trouvées par le slicer

Sans zone, le panneau liste les parties que le slicer sait incliner tout
seul, avec un choix pour chacune :

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

Le fichier G-code suit le **contrat G-code version 1** du firmware ArtPrint
(voir `docs/5-axes.md`). Si une partie demande plus d'inclinaison que la
machine n'en permet (réglage « b_inclinaison_max », 50° par défaut), un
message le dit avant de trancher, dans le choix de la stratégie, et après,
dans les messages.

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
| Couper | coupe la pièce à plat, à une hauteur donnée, et garde le bas ou le haut |
| Mesurer | dimensions, volume, poids plein, position, surface à soutenir |
| Stratégie de tranchage | mène au choix de la stratégie |
| Direction d'impression par zone | choisit l'ondulation et attend un clic sur la pièce pour y poser une zone orientée |
| Simulation de la machine | fait défiler les couches du G-code, de la première à la dernière |

Toutes ces transformations passent par la commande `transformer_modele` du
slicer (échelle, rotation, déplacement, orientation automatique) : le
tranchage se fait sur la pièce telle qu'elle est posée.

**Six outils affichent encore « à venir »** : hauteur de couche variable,
opérations booléennes, assembler, texte en relief, peindre les supports,
peindre la couture. Ils demandent des fonctions que le cœur du slicer n'a
pas (plusieurs objets, supports).

## Fluidité : les calculs en arrière-plan

Déplacer, tourner ou mettre à l'échelle la pièce n'affiche plus d'attente.
Un déplacement est montré tout de suite (la pièce glisse à l'écran, le slicer
suit) et ne recalcule rien. Après une rotation ou une mise à l'échelle, les
façons d'imprimer la pièce sont cherchées en arrière-plan : les cartes
« Classique » et « Ondulation » affichent « calcul… » et un petit point
clignote dans la ligne d'état. On peut continuer à travailler pendant ce
temps ; le tranchage attend que les gestes en cours soient pris en compte.

## Les poignées dans la vue 3D

- **Déplacer** : deux flèches au pied de la pièce (rouge pour X, verte pour
  Y) ; on en tire une pour glisser le long de cet axe. On peut toujours
  attraper la pièce elle-même pour la glisser librement.
- **Tourner** : trois anneaux (rouge autour de X, vert autour de Y, bleu
  autour de Z). On en tire un pour tourner la pièce ; l'angle s'affiche
  pendant le geste (touche Maj : par pas de 15°). Les rotations se composent
  autour des axes du plateau.
- **Poser sur une face** : les faces planes du contour de la pièce (son
  enveloppe convexe) sont grisées ; un clic sur l'une d'elles la pose contre
  le plateau.
- Chaque panneau d'outil a ses champs sur une rangée, un par axe, avec la
  lettre de l'axe dans sa couleur.
- **Le repère des axes**, en bas à gauche, suit la vue : X, Y, Z, et en
  ondulation les deux rotations de la machine, B (la tête s'incline) et C (le
  plateau tourne).

(`ui/manipulateurs.js` ; l'enveloppe convexe vient de three.js.)

## Imprimante et calibration

- **Imprimante › Fichiers** : la liste des G-code exportés depuis le slicer
  (nom, durée, poids, date, chemin).
- **Imprimante › Mise à jour** : la version du slicer, celle du firmware
  quand l'imprimante sera reliée, et le profil de la machine.
- **Calibration** : six fiches, une par mesure à faire sur la machine
  (distance pivot-buse, encombrement de la tête, butées de B, sens de B et
  C, courses, rétraction), avec les valeurs déjà renseignées.
- **Mode développeur** (interrupteur de la page Calibration) : déplacements
  des cinq axes et commandes G-code, avec un journal.

**L'imprimante n'est pas encore reliée au slicer.** Rien n'est envoyé à la
machine : le journal du mode développeur montre les commandes qui
partiraient, et l'envoi d'un fichier dit qu'il faut d'abord cette liaison.

## Projet, annuler, exporter

- **Annuler / Refaire** (flèches en haut à gauche, Ctrl+Z et Ctrl+Y) : la
  pose de la pièce, son échelle, les coupes, la stratégie et les zones
  orientées. Les paramètres du procédé n'en font pas partie. L'historique
  repart de zéro quand on change de plateau.
- **Enregistrer le projet** (disquette, Ctrl+S, menu Fichier) : un fichier
  `.artprint` qui garde le chemin du modèle, sa pose, les coupes, la
  stratégie, les zones et tous les réglages. Il ne contient pas le modèle
  lui-même : si le fichier STL est déplacé, le projet ne le retrouve plus.
- **Ouvrir un projet**, **Nouveau projet** : menu Fichier.
- **Exporter en STL** : le modèle tel qu'il est posé (échelle, rotation,
  coupes), en STL binaire.

Encore « à venir » dans les menus : l'export 3MF, « Trancher tous les
plateaux », l'envoi direct à l'imprimante et la synchronisation Klipper.

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
- **La simulation** ne recalcule rien : les zones et les options de l'ondulation,
  l'échelle et l'orientation n'y changent pas le tranchage affiché.
- **Pages Imprimante, Projet, Calibration, Gestionnaire** : ce sont encore
  des maquettes, non reliées à une vraie imprimante.
