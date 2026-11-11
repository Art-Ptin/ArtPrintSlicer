# ArtPrint Slicer - licence AGPL v3
"""Préenregistre les réponses du vrai slicer pour la maquette de l'interface.

Utilisation :
    python maquette/generer_donnees.py chemin/vers/artprint-slicer piece1.stl piece2.stl ...

Écrit maquette/donnees/base.json (version et réglages) et un fichier par pièce
(modèle et résultat du tranchage). Ces fichiers ne sont pas versionnés.
Le nom des pièces attendues par la maquette est dans maquette/pont.js.
"""
import json
import pathlib
import subprocess
import sys


def appeler(programme, demandes):
    entree = "\n".join(json.dumps(d) for d in demandes) + "\n"
    sortie = subprocess.run([programme, "api"], input=entree, capture_output=True, text=True, check=True)
    return [json.loads(ligne) for ligne in sortie.stdout.strip().split("\n")]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    programme, pieces = sys.argv[1], sys.argv[2:]
    dossier = pathlib.Path(__file__).parent / "donnees"
    dossier.mkdir(exist_ok=True)
    infos, reglages = appeler(programme, [{"commande": "infos"}, {"commande": "reglages"}])
    (dossier / "base.json").write_text(
        json.dumps({"infos": infos, "reglages": reglages["reglages"]}, ensure_ascii=False, separators=(",", ":")),
        encoding="utf-8")
    for piece in pieces:
        modele, tranche = appeler(programme, [{"commande": "charger_modele", "chemin": piece}, {"commande": "trancher"}])
        if not (modele["ok"] and tranche["ok"]):
            sys.exit(f"{piece} : {modele.get('erreur') or tranche.get('erreur')}")
        nom = pathlib.Path(piece).stem
        (dossier / f"{nom}.json").write_text(
            json.dumps({"modele": modele["modele"], "resultat": tranche["resultat"]}, ensure_ascii=False, separators=(",", ":")),
            encoding="utf-8")
        print(f"{nom} : {tranche['resultat']['couches']} couches")


if __name__ == "__main__":
    main()
