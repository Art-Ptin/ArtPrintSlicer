# ArtPrint Slicer - licence AGPL v3
"""Prépare la liste des modèles d'un essai de masse.

Utilisation :
    python outils/essais_de_masse/preparer.py <generateur_modeles> <dossier_de_travail> [dossier_de_pieces ...]

1. Fabrique des modèles avec « generateur_modeles » : un nombre donné par
   famille (voir REPARTITION), chacun avec une graine différente. Un modèle
   qui n'est pas propre (surface ouverte, plusieurs morceaux) est remplacé
   par le suivant.
2. Ajoute les pièces trouvées dans les dossiers donnés (.stl, .obj, .off,
   .ply) : elles sont converties en STL binaire. Les fichiers identiques
   (même contenu) ne sont pris qu'une fois.

Écrit <dossier_de_travail>/liste.jsonl (une ligne par modèle : identifiant,
source, famille, graine, paramètres) et <dossier_de_travail>/modeles/*.stl
pour les pièces ajoutées. Les modèles fabriqués ne sont pas gardés sur le
disque : avec la même graine, le générateur redonne le même modèle.
"""
import hashlib
import json
import pathlib
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

# Nombre de modèles fabriqués par famille. Les familles « à branches », le
# cœur du 5 axes, sont les plus nombreuses ; les autres couvrent ce que les
# gens impriment vraiment (récipients, pièces mécaniques, figurines).
REPARTITION = {
    "branche_simple": 90, "couronne": 80, "etages": 60, "fourches": 60, "corail": 70, "spirale": 45,
    "branche_courbe": 70, "tronc_penche": 45, "socle_bras": 60,
    "vase": 90, "tasse": 55, "coupe": 45, "tuyau": 55,
    "pieuvre": 70, "creature": 55, "blob": 45, "cactus": 55, "champignon": 45, "arche": 55,
    "plaque": 40, "equerre": 50, "boite": 45, "cylindres": 40, "engrenage": 40, "surplomb": 55,
}


def ecrire_stl(chemin, triangles):
    """triangles : tableau (n, 3, 3) de flottants."""
    triangles = np.asarray(triangles, dtype=np.float32)
    enregistrement = np.zeros(len(triangles), dtype=[("n", "<f4", 3), ("s", "<f4", (3, 3)), ("a", "<u2")])
    enregistrement["s"] = triangles
    with open(chemin, "wb") as f:
        f.write(b"piece d'essai".ljust(80, b" "))
        f.write(struct.pack("<I", len(triangles)))
        f.write(enregistrement.tobytes())


def lire_stl(chemin):
    donnees = pathlib.Path(chemin).read_bytes()
    if len(donnees) >= 84:
        nombre = struct.unpack_from("<I", donnees, 80)[0]
        if len(donnees) == 84 + 50 * nombre and nombre > 0:
            brut = np.frombuffer(donnees, dtype=[("n", "<f4", 3), ("s", "<f4", (3, 3)), ("a", "<u2")], offset=84)
            return np.array(brut["s"], dtype=np.float64)
    # STL en texte.
    points = []
    for ligne in donnees.decode("latin-1", "replace").splitlines():
        mots = ligne.split()
        if len(mots) == 4 and mots[0].lower() == "vertex":
            points.append([float(m) for m in mots[1:]])
    if not points or len(points) % 3:
        raise ValueError("STL illisible")
    return np.array(points, dtype=np.float64).reshape(-1, 3, 3)


def trianguler(sommets, faces):
    triangles = []
    for face in faces:
        for k in range(1, len(face) - 1):
            triangles.append([sommets[face[0]], sommets[face[k]], sommets[face[k + 1]]])
    return np.array(triangles, dtype=np.float64)


def lire_obj(chemin):
    sommets, faces = [], []
    for ligne in open(chemin, encoding="latin-1", errors="replace"):
        mots = ligne.split()
        if not mots:
            continue
        if mots[0] == "v" and len(mots) >= 4:
            sommets.append([float(m) for m in mots[1:4]])
        elif mots[0] == "f" and len(mots) >= 4:
            face = [int(m.split("/")[0]) for m in mots[1:]]
            faces.append([i - 1 if i > 0 else len(sommets) + i for i in face])
    return trianguler(sommets, faces)


def lire_off(chemin):
    mots = open(chemin, encoding="latin-1", errors="replace").read().split()
    if mots[0].upper().endswith("OFF") and len(mots[0]) > 3:
        mots = ["OFF", mots[0][3:]] + mots[1:]
    position = 1 if mots[0].upper() in ("OFF", "COFF", "NOFF") else 0
    nombre_sommets, nombre_faces = int(mots[position]), int(mots[position + 1])
    position += 3
    sommets = [[float(mots[position + 3 * i + k]) for k in range(3)] for i in range(nombre_sommets)]
    position += 3 * nombre_sommets
    faces = []
    for _ in range(nombre_faces):
        taille = int(mots[position])
        faces.append([int(m) for m in mots[position + 1:position + 1 + taille]])
        position += 1 + taille
    return trianguler(sommets, faces)


def lire_ply(chemin):
    donnees = pathlib.Path(chemin).read_bytes()
    fin = donnees.index(b"end_header\n") + len(b"end_header\n")
    entete = donnees[:fin].decode("latin-1").splitlines()
    if not any(l.startswith("format ascii") for l in entete):
        raise ValueError("PLY binaire non géré")
    nombre_sommets = next(int(l.split()[2]) for l in entete if l.startswith("element vertex"))
    nombre_faces = next(int(l.split()[2]) for l in entete if l.startswith("element face"))
    lignes = donnees[fin:].decode("latin-1").splitlines()
    sommets = [[float(m) for m in l.split()[:3]] for l in lignes[:nombre_sommets]]
    faces = []
    for l in lignes[nombre_sommets:nombre_sommets + nombre_faces]:
        mots = l.split()
        faces.append([int(m) for m in mots[1:1 + int(mots[0])]])
    return trianguler(sommets, faces)


LECTEURS = {".stl": lire_stl, ".obj": lire_obj, ".off": lire_off, ".ply": lire_ply}


def fabriquer(generateur, dossier, famille, numero):
    """Fabrique le modèle « numero » de la famille ; essaie d'autres graines s'il n'est pas propre."""
    for essai in range(8):
        graine = numero + 1000 * essai
        identifiant = f"g_{famille}_{graine:04d}"
        fichier = dossier / f"{identifiant}.stl"
        sortie = subprocess.run([generateur, famille, str(graine), str(fichier)], capture_output=True, text=True)
        if sortie.returncode == 0 and fichier.exists():
            # Le fichier n'est pas gardé : la campagne le refabrique au moment de s'en servir.
            fichier.unlink()
            description, _, parametres = sortie.stdout.strip().partition(" |")
            return {"id": identifiant, "source": "fabrique", "famille": famille, "fichier": None,
                    "graine": graine, "parametres": parametres.strip(),
                    "triangles": int(description.split("triangles=")[1].split()[0])}
    return None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    generateur, travail = sys.argv[1], pathlib.Path(sys.argv[2])
    dossier = travail / "modeles"
    dossier.mkdir(parents=True, exist_ok=True)
    liste = []

    taches = [(famille, numero) for famille, nombre in REPARTITION.items() for numero in range(1, nombre + 1)]
    with ThreadPoolExecutor(4) as bassin:
        for fiche in bassin.map(lambda t: fabriquer(generateur, dossier, *t), taches):
            if fiche:
                liste.append(fiche)
    print(f"modèles fabriqués : {len(liste)} sur {len(taches)} demandés")

    vus = set()
    for racine in sys.argv[3:]:
        racine = pathlib.Path(racine)
        pris = 0
        for chemin in sorted(racine.rglob("*")):
            lecteur = LECTEURS.get(chemin.suffix.lower())
            if not lecteur or not chemin.is_file() or chemin.stat().st_size > 60e6 or chemin.stat().st_size < 400:
                continue
            empreinte = hashlib.sha1(chemin.read_bytes()).hexdigest()
            if empreinte in vus:
                continue
            vus.add(empreinte)
            try:
                triangles = lecteur(chemin)
            except Exception as erreur:  # fichier illisible : il est laissé de côté
                print(f"  laissé : {chemin} ({erreur})")
                continue
            if len(triangles) < 12 or len(triangles) > 900000 or not np.isfinite(triangles).all():
                continue
            identifiant = f"r_{racine.name}_{empreinte[:10]}"
            ecrire_stl(dossier / f"{identifiant}.stl", triangles)
            liste.append({"id": identifiant, "source": "piece_reelle", "famille": racine.name,
                          "fichier": f"{identifiant}.stl", "origine": str(chemin.relative_to(racine.parent)),
                          "triangles": int(len(triangles))})
            pris += 1
        print(f"{racine} : {pris} pièces")

    with open(travail / "liste.jsonl", "w", encoding="utf-8") as f:
        for fiche in liste:
            f.write(json.dumps(fiche, ensure_ascii=False) + "\n")
    print(f"total : {len(liste)} modèles dans {travail / 'liste.jsonl'}")


if __name__ == "__main__":
    main()
