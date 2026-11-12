# Maquette de l'interface

Ce dossier contient la **maquette** de la future interface d'ArtPrint Slicer.
Elle sert à régler le design et l'emplacement des commandes avant de les
reporter dans le vrai logiciel (dossier `ui/`).

## Ce qu'on y trouve

- Barre du haut sur deux rangées : menu Fichier, enregistrer, annuler,
  refaire ; puis les pages Accueil, Préparation, Aperçu, Imprimante, Projet,
  Calibration et Gestionnaire, et les boutons « Trancher le plateau » et
  « Exporter le G-code » avec leur petite flèche de menu.
- Préparation et Aperçu : colonne des réglages à gauche (Imprimante, Filament,
  Procédé, chacune repliable par la flèche de son bandeau), vue 3D, barre
  d'outils, trajectoires de l'aperçu, ligne d'état en bas.
- Stratégie d'impression : pour chaque modèle, les façons de l'imprimer
  proposées par le slicer (classique, autre pose, par branches) ; la pastille
  « 3 axes | 5 axes » suit ce choix. Pièces d'exemple : deux arbres.
- Imprimante : caméra, progression de l'impression, températures et
  déplacements des cinq axes X, Y, Z, B, C. C'est une **simulation** : les
  touches ne changent que les valeurs affichées, rien n'est envoyé à une
  machine.
- Gestionnaire : les bobines de filament, par matière, avec ce qu'il en reste.

## Fichiers

- `index.html` : la page. `style.css` : sa feuille de style.
- `tokens.css` : **toutes** les couleurs, tailles et espacements. La feuille de
  style ne contient aucune valeur en dur.
- `app.js` : le comportement de la page. `pages.js` : les pages Imprimante et
  Gestionnaire. `menu.js` : les menus déroulants. `icones.js` : les icônes.
- `catalogue.js` : les paramètres du procédé, les profils, les matières. Un
  paramètre sans champ `moteur` est « prévu » : affiché, mais pas encore
  appliqué par le slicer.
- `pont.js` : un moteur **simulé**, qui rend des réponses préenregistrées.
- `generer_donnees.py` : préenregistre ces réponses avec le vrai slicer.
- `planche.html` : la planche de design (couleurs, typographie, icônes).

La vue 3D (`vue3d.js`) et la bibliothèque three.js sont celles du dossier `ui/`.

## Ce qui n'est pas dans le dépôt

- `donnees/` : les tranchages préenregistrés.
- `prive/` : la petite image de l'imprimante. Elle vient du modèle 3D privé de
  la machine ; sans elle, la maquette affiche une icône à la place.
