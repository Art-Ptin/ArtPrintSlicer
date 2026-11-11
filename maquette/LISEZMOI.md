# Maquette de l'interface

Ce dossier contient la **maquette** de la future interface d'ArtPrint Slicer :
imprimante, bibliothèque de bobines, procédé détaillé par onglets, aperçu avec
tableau des trajectoires. Elle sert à régler le design et l'emplacement des
commandes avant de les reporter dans le vrai logiciel (dossier `ui/`).

- `index.html` : la page et sa feuille de style.
- `app.js` : le comportement de la page.
- `catalogue.js` : les paramètres du procédé, les profils, les matières. Un
  paramètre sans champ `moteur` est « prévu » : affiché, mais pas encore
  appliqué par le slicer.
- `pont.js` : un moteur **simulé**, qui rend des réponses préenregistrées.
- `generer_donnees.py` : préenregistre ces réponses avec le vrai slicer.

La vue 3D (`vue3d.js`) et la bibliothèque three.js sont celles du dossier `ui/`.
