# ArtPrint Slicer - licence AGPL v3
"""Préenregistre les réponses du vrai slicer pour la maquette de l'interface.

Utilisation :
    python maquette/generer_donnees.py chemin/vers/artprint-slicer piece1.stl piece2.stl ...

Écrit maquette/donnees/base.json (version et réglages), un fichier par pièce
(modèle, résultat du tranchage classique, stratégies proposées) et un fichier
par autre stratégie (« piece-couches_courbes.json »...). Ces fichiers ne sont
pas versionnés.
Le nom des pièces attendues par la maquette est dans maquette/pont.js.

Pour les stratégies 5 axes, la machine a besoin de deux réglages qui se
mesurent sur l'imprimante. La simulation utilise ici des valeurs d'EXEMPLE,
inventées, qui ne décrivent aucune machine réelle.
"""
import json
import pathlib
import subprocess
import sys


def alleger(objet):
    """Arrondit les coordonnées au centième de millimètre : les fichiers restent sous 16 Mo."""
    if "positions" in objet:
        objet["positions"] = [round(v, 2) for v in objet["positions"]]
    for couche in objet.get("apercu", []):
        couche["m"] = [round(v, 2) if i % 7 != 6 else int(v) for i, v in enumerate(couche["m"])]
    return objet


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
    exemple = {"commande": "definir_reglages", "valeurs": {"distance_pivot_buse": "37,5", "hauteur_bascule_b": "10",
                                                           "b_min": "-110", "b_max": "110", "b_inclinaison_max": "110"}}
    for piece in pieces:
        modele, tranche = appeler(programme, [{"commande": "charger_modele", "chemin": piece}, {"commande": "trancher"}])
        if not (modele["ok"] and tranche["ok"]):
            sys.exit(f"{piece} : {modele.get('erreur') or tranche.get('erreur')}")
        ouvrir = [{"commande": "charger_modele", "chemin": piece}, exemple]
        strategies = appeler(programme, ouvrir + [{"commande": "strategies"}])[-1]["strategies"]
        nom = pathlib.Path(piece).stem
        tranchees = []
        for strategie in strategies:
            if strategie["type"] == "classique":
                tranchees.append(strategie)
                continue
            reponse = appeler(programme, ouvrir + [{"commande": "trancher", "strategie": strategie["type"]}])[-1]
            if reponse["ok"] and not reponse["resultat"]["verification"]["valide"]:
                # Le slicer refuserait ce tranchage : la simulation ne le propose pas.
                print(f"  {strategie['nom']} : refusé par le vérificateur "
                      f"({reponse['resultat']['verification']['nombre_erreurs']} faute(s))")
            elif reponse["ok"]:
                tranchees.append(strategie)
                (dossier / f"{nom}-{strategie['type']}.json").write_text(
                    json.dumps(alleger(reponse["resultat"]), ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
            else:
                print(f"  {strategie['nom']} : {reponse['erreur']}")
        strategies = tranchees
        (dossier / f"{nom}.json").write_text(
            json.dumps({"modele": alleger(modele["modele"]), "resultat": alleger(tranche["resultat"]), "strategies": strategies},
                       ensure_ascii=False, separators=(",", ":")),
            encoding="utf-8")
        print(f"{nom} : {tranche['resultat']['couches']} couches, {len(strategies)} stratégie(s)")


if __name__ == "__main__":
    main()
