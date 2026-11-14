# ArtPrint Slicer

**ArtPrint Slicer** est un slicer **5 axes continu**, libre et open source, conçu pour l'imprimante 3D **ArtPrint**.

L'imprimante ArtPrint combine :

- une **tête inclinable** (axe **B**) ;
- un **plateau rond rotatif** (axe **C**).

Le slicer génère des trajectoires où ces deux axes bougent en même temps que X, Y et Z.

## État du projet

**En cours de développement.** Les fonctionnalités, les formats et les réglages peuvent encore changer.

Ce qui fonctionne aujourd'hui :

- **Logiciel Windows avec fenêtre et installateur** : ouvrir un STL, régler, trancher, voir l'aperçu couche par couche en couleurs, exporter le G-code.
- **Tranchage 3 axes** : parois, remplissage, couches pleines du dessus et du dessous, jupe, rétractions.
- **Vérificateur de G-code** : chaque G-code produit est relu et refusé s'il viole une règle physique (cordon dans ou sous la matière, déplacement sans rétraction, courses et limites des axes...).
- **Cinématique 5 axes** (tête inclinable B, plateau rotatif C), déjà utilisée par le vérificateur.
- **Collisions avec la vraie forme de l'imprimante** : la tête, le bras et le module Z sont contrôlés à chaque mouvement contre la matière déjà imprimée, le plateau et le bâti. La forme vient du fichier STEP de la machine, converti par `outils/convertir_machine.py` (le STEP et le modèle converti restent privés, dans le dossier `machine/`).

- **Deux stratégies d'impression** : classique (3 axes) ou **ondulation** (5 axes continu), proposée pour tout modèle. Au moment de trancher en ondulation, une fenêtre demande comment imprimer chaque partie inclinée. Voir [docs/5-axes.md](docs/5-axes.md).

Sur une branche courbe, la tête et le plateau tournent pendant l'extrusion : c'est le premier mouvement 5 axes continu du slicer.

- **Ondulation (couches courbes, 5 axes continu)** : pour une forme en arbre, le tronc est imprimé d'abord, lisse et sans amorce de branche, laissé à nu là où une branche viendra ; chaque branche est ensuite imprimée contre lui, en couches qui épousent sa surface. Avec la vraie forme de la machine, le slicer essaie chaque branche dans plusieurs positions (plateau retourné, tête moins inclinée) et garde celle qui ne touche rien. L'orientation de chaque branche peut aussi être choisie à la main (couches courbes, tête moins inclinée, ou à plat avec le tronc) : la jonction se refait toute seule.

- **Peau courbe (option)** : une dernière passe suit la forme du dessus de la pièce pour effacer les marches d'escalier.

Ce qui n'existe pas encore : les supports.

## Installation (Windows 64 bits)

Lancer `ArtPrintSlicer-Setup-x.y.z.exe`, puis ouvrir « ArtPrint Slicer » depuis le menu Démarrer ou le bureau. La fenêtre s'appuie sur le composant Microsoft Edge WebView2, déjà présent sur Windows 11 et sur presque tous les Windows 10.

## Ligne de commande

Le programme `artprint-slicer.exe` est installé à côté du logiciel. Trancher un modèle (le fichier de réglages est facultatif, voir `exemples/reglages.exemple.txt`) :

```
artprint-slicer trancher modele.stl sortie.gcode reglages.txt
```

Vérifier un G-code :

```
artprint-slicer verifier fichier.gcode reglages.txt
```

## Compilation

Avec Visual Studio 2022 et CMake :

```
cmake --preset msvc
```

```
cmake --build --preset msvc
```

```
ctest --preset msvc
```

Avec NSIS installé, la cible `installateur` fabrique le `Setup.exe`.

## Bibliothèques utilisées

Voir `LICENCES-TIERCES.txt` : Clipper2 (Boost), nlohmann/json (MIT), three.js (MIT), Microsoft Edge WebView2 SDK.

## Licence

Ce projet est distribué sous licence **GNU Affero General Public License v3.0** (AGPL v3). Le texte complet se trouve dans le fichier [LICENSE](LICENSE).
