# ArtPrint Slicer - licence AGPL v3
"""Essai de masse : tranche chaque modèle d'une liste avec des réglages tirés au hasard.

Utilisation :
    python outils/essais_de_masse/campagne.py <artprint-slicer> <dossier_de_travail> [options]

Options :
    --reglages=<fichier>   réglages de départ (par défaut exemples/reglages.exemple.txt)
    --parallele=4          nombre de tranchages en même temps
    --delai=240            temps maximal par modèle, en secondes
    --tirage=1             numéro du tirage : change tous les réglages tirés au hasard
    --seulement=<texte>    ne traite que les modèles dont l'identifiant contient ce texte
    --limite=N             s'arrête après N modèles
    --resultats=<fichier>  fichier de résultats (par défaut <dossier>/resultats-<tirage>.jsonl)
    --generateur=<chemin>  le programme generateur_modeles (par défaut celui de build/linux)
    --entree=commande      impose le chemin d'entrée (commande ou interface) à tous les modèles
    --garder               garde les fichiers de travail dans <dossier>/tmp (pour rejouer un cas)

Le dossier de travail est celui de « preparer.py » (liste.jsonl et modeles/).
Pour chaque modèle, le script tire au hasard (toujours pareil pour un même
modèle et un même tirage) : la taille, la pose, les réglages d'impression,
les limites de la machine, la stratégie, et le chemin d'entrée (ligne de
commande ou commandes de l'interface). Il lance le slicer, relit le G-code
produit pour contrôler lui-même les règles du contrat, et ajoute une ligne
au fichier de résultats. Relancé, il reprend là où il s'était arrêté.

Ce que dit le résultat :
    verifie   le G-code est produit et accepté par le vérificateur ;
    refuse    le vérificateur a trouvé des fautes : le slicer n'a rien écrit ;
    erreur    le slicer a renoncé avec un message (modèle ouvert, trop grand...) ;
    delai     le temps maximal est dépassé ;
    plantage  le programme s'est arrêté brutalement : c'est un bogue.
"""
import json
import pathlib
import random
import re
import struct
import subprocess
import sys
import threading
import time
import zlib
from concurrent.futures import ThreadPoolExecutor

import numpy as np

DEPOT = pathlib.Path(__file__).resolve().parents[2]
FORCER = {}   # choix imposés (option --entree=commande, pour rejouer un cas)
GARDER = []   # non vide : les fichiers de travail (STL posé, réglages, G-code) sont gardés
GENERATEUR = [str(DEPOT / "build" / "linux" / "tests" / "generateur_modeles")]
TYPE_STL = [("n", "<f4", 3), ("s", "<f4", (3, 3)), ("a", "<u2")]


def lire_stl(chemin):
    donnees = pathlib.Path(chemin).read_bytes()
    brut = np.frombuffer(donnees, dtype=TYPE_STL, offset=84)
    return np.array(brut["s"], dtype=np.float64)


def ecrire_stl(chemin, triangles):
    enregistrement = np.zeros(len(triangles), dtype=TYPE_STL)
    enregistrement["s"] = triangles.astype(np.float32)
    with open(chemin, "wb") as f:
        f.write(b"piece d'essai".ljust(80, b" "))
        f.write(struct.pack("<I", len(triangles)))
        f.write(enregistrement.tobytes())


def tirer(fiche, tirage):
    """Tous les choix au hasard pour ce modèle. Même modèle, même tirage : mêmes choix."""
    h = random.Random(zlib.crc32(fiche["id"].encode()) * 1000 + tirage)
    largeur = h.choice([0.4, 0.45, 0.45, 0.5, 0.6])
    couche = h.choice([c for c in (0.12, 0.16, 0.2, 0.2, 0.24, 0.28) if c <= 0.62 * largeur])
    b_max = h.choice([50, 70, 90, 90, 110, 110])
    bascule = h.choice([None, 3, 8, 15])
    mecanique = fiche["source"] == "piece_reelle" or fiche["famille"] in ("plaque", "boite", "cylindres", "engrenage")
    choix = {
        "taille": round(h.uniform(28, 95), 1),          # plus grande dimension, en mm
        "pose": h.choices(["origine", "x90", "y90", "x180"], [84, 6, 6, 4])[0],
        "strategie": h.choices(["couches_courbes", "classique"], [55, 45] if mecanique else [82, 18])[0],
        "entree": h.choices(["commande", "interface"], [78, 22])[0],
        "orientation_auto": h.random() < 0.35,           # (interface seulement)
        "decalage": [round(h.uniform(-40, 40), 1), round(h.uniform(-40, 40), 1)] if h.random() < 0.25 else None,
        "branche": h.choice([None, None, "a_plat", "incline45", "incline60", "cote"]),  # (interface, ondulation)
        "reglages": {
            "largeur_extrusion": largeur,
            "hauteur_couche": couche,
            "hauteur_premiere_couche": h.choice([couche, 0.2, 0.24]) if couche <= 0.24 else couche,
            "nombre_parois": h.choice([1, 2, 2, 3, 4]),
            "densite_remplissage": h.choice([0, 10, 15, 15, 20, 35, 60]),
            "couches_dessus": h.randint(2, 6),
            "couches_dessous": h.randint(2, 5),
            "jupe_boucles": h.choice([0, 1, 1, 2]),
            "vitesse_paroi_externe": h.choice([20, 25, 35, 50]),
            "vitesse_remplissage": h.choice([40, 60, 90]),
            "vitesse_a_vide": h.choice([100, 120, 180]),
            "retraction_longueur": h.choice([0.8, 2, 2, 4]),
            "levee_z": h.choice([0.2, 0.2, 0.4, 0.6]),
            "peau_courbe": 1 if h.random() < 0.08 else 0,
            "pivot_continu": 1 if h.random() < 0.8 else 0,
            "contrat_gcode": 1 if h.random() < 0.88 else 0,
            "distance_pivot_buse": h.choice([37.5, 55, 55, 70]),
            "b_max": b_max,
            "b_min": 0 if h.random() < 0.2 else -b_max,   # 0 : la tête ne penche que d'un côté
            "b_inclinaison_max": b_max,
            "b_limite_pres_plateau": h.choice([40, 55, 55, 70]),
            "hauteur_bascule_b": bascule,
            "vitesse_b_max": h.choice([5, 5, 20]),
            "vitesse_c_max": h.choice([10, 10, 40]),
        },
    }
    return choix


def fichier_reglages(base, reglages, chemin):
    lignes = [l for l in base.splitlines() if l.split("=")[0].strip().lstrip("# ").strip() not in reglages]
    for cle, valeur in reglages.items():
        if valeur is not None:
            lignes.append(f"{cle} = {valeur}")
    pathlib.Path(chemin).write_text("\n".join(lignes) + "\n", encoding="utf-8")


def poser(triangles, pose, taille):
    """Tourne le modèle (quarts de tour), le met à la taille voulue. Rend aussi le facteur d'échelle."""
    if pose == "x90":
        triangles = triangles[:, :, [0, 2, 1]] * np.array([1.0, -1.0, 1.0])
    elif pose == "y90":
        triangles = triangles[:, :, [2, 1, 0]] * np.array([-1.0, 1.0, 1.0])
    elif pose == "x180":
        triangles = triangles * np.array([1.0, -1.0, -1.0])
    dimensions = triangles.reshape(-1, 3).max(axis=0) - triangles.reshape(-1, 3).min(axis=0)
    echelle = taille / max(float(dimensions.max()), 1e-9)
    return triangles * echelle, echelle


def classer(probleme):
    p = probleme.lower()
    for mot, nom in (("dans de la matière", "dans_matiere"), ("sous de la matière", "sous_matiere"),
                     ("touche", "collision"), ("rétract", "retraction"), ("sous le plateau", "sous_plateau"),
                     ("sous la", "sous_plateau"), ("firmware n'accepte", "limite_b"), ("inclinée", "limite_b"),
                     ("axe b", "limite_b"), ("plateau tourne", "limite_c"), ("axe c", "limite_c"),
                     ("demi-tour", "c_chemin_long"), ("volume", "hors_volume"), ("course", "limite_course"),
                     ("reprise", "extrusion_sans_reprise")):
        if mot in p:
            return nom
    return "autre"


MOT = re.compile(r"([XYZBCE])(-?\d+\.?\d*)")


def relire_gcode(chemin, reglages):
    """Contrôle indépendant du fichier : les règles du contrat G-code, relues ici sans le slicer."""
    bilan = {"lignes": 0, "b_max": 0.0, "c_min": 0.0, "c_max": 0.0, "z_min": 1e9, "fautes": []}
    contrat = reglages.get("contrat_gcode", 1) == 1
    limite_b = float(reglages.get("b_inclinaison_max", 50))
    fautes = set()
    premiere = True
    retracte = False
    a_extrude = False
    with open(chemin, encoding="utf-8", errors="replace") as f:
        for ligne in f:
            if premiere:
                premiere = False
                if contrat and ligne.strip() != "; CONTRAT_GCODE: 1":
                    fautes.add("premiere_ligne")
            debut = ligne[:3]
            if debut not in ("G0 ", "G1 "):
                if contrat and ligne[:1] in "GM" and ligne.split()[0] in ("G28", "G90", "M83", "M104", "M109", "M84", "G92"):
                    fautes.add("commande_retiree:" + ligne.split()[0])
                continue
            mots = dict(MOT.findall(ligne.split(";")[0]))
            bilan["lignes"] += 1
            if not any(k in mots for k in "XYZBC"):
                if "E" in mots and float(mots["E"]) != 0.0:
                    retracte = float(mots["E"]) < 0
                continue
            if contrat and not all(k in mots for k in "XYZBC"):
                fautes.add("axes_manquants")
            extrude = debut == "G1 " and "E" in mots and float(mots["E"]) > 0
            if not extrude and a_extrude and not retracte:
                fautes.add("deplacement_sans_retraction")
            if extrude and retracte:
                fautes.add("extrusion_sans_reprise")
            a_extrude = a_extrude or extrude
            if "B" in mots:
                bilan["b_max"] = max(bilan["b_max"], abs(float(mots["B"])))
            if "C" in mots:
                c = float(mots["C"])
                bilan["c_min"], bilan["c_max"] = min(bilan["c_min"], c), max(bilan["c_max"], c)
            if "Z" in mots and contrat:
                bilan["z_min"] = min(bilan["z_min"], float(mots["Z"]))
    if contrat:
        if bilan["z_min"] < -1e-6:
            fautes.add("z_negatif")
        if bilan["c_min"] < -360.001 or bilan["c_max"] > 360.001:
            fautes.add("c_hors_limites")
    if bilan["b_max"] > limite_b + 0.001:
        fautes.add("b_au_dela_de_la_limite")
    bilan["fautes"] = sorted(fautes)
    bilan["b_max"] = round(bilan["b_max"], 2)
    return bilan


def lire_sortie(texte):
    """Tire les nombres utiles du texte affiché par « artprint-slicer trancher »."""
    r = {"remarques": [], "problemes": {}, "premier_probleme": None, "nombre_fautes": 0}
    for ligne in texte.splitlines():
        ligne = ligne.strip()
        if ligne.startswith("Stratégie :"):
            r["strategie"] = ligne[12:]
            m = re.search(r"\((\d+) partie", ligne)
            r["parties"] = int(m.group(1)) if m else 1
        elif ligne.startswith("Couches :"):
            r["couches"] = int(ligne.split(":")[1])
        elif ligne.startswith("Filament :"):
            r["filament_m"] = float(ligne.split(":")[1].split()[0])
        elif ligne.startswith("Durée estimée :"):
            r["duree_estimee"] = ligne.split(":", 1)[1].split("(")[0].strip()
        elif ligne.startswith("5 axes continu :"):
            r["extrusions_en_rotation"] = int(ligne.split(":")[1].split()[0])
        elif ligne.startswith("Parois imprimées dans le vide"):
            m = re.search(r": (\d+) mm sur (\d+) mm", ligne)
            if m:
                r["vide_mm"], r["cordon_mm"] = int(m.group(1)), int(m.group(2))
        elif ligne.startswith("Remarque :"):
            r["remarques"].append(ligne[11:200])
        elif ligne.startswith("Problème :"):
            nom = classer(ligne)
            r["problemes"][nom] = r["problemes"].get(nom, 0) + 1
            r["premier_probleme"] = r["premier_probleme"] or ligne[11:220]
        elif ligne.startswith("Erreur"):
            r["erreur"] = ligne[:300]
        m = re.search(r"viole (\d+)", ligne) or re.search(r"(\d+) erreur\(s\)", ligne)
        if m:
            r["nombre_fautes"] = max(r["nombre_fautes"], int(m.group(1)))
    return r


def par_la_commande(slicer, stl, gcode, reglages, choix, delai):
    commande = [slicer, "trancher", str(stl), str(gcode), str(reglages),
                "--strategie=" + ("2" if choix["strategie"] == "couches_courbes" else "1")]
    try:
        sortie = subprocess.run(commande, capture_output=True, timeout=delai)
    except subprocess.TimeoutExpired:
        return {"resultat": "delai"}
    texte = sortie.stdout.decode("utf-8", "replace") + sortie.stderr.decode("utf-8", "replace")
    r = lire_sortie(texte)
    if sortie.returncode == 0 and "G-code vérifié" in texte:
        r["resultat"] = "verifie"
    elif sortie.returncode < 0 or sortie.returncode > 2 and not r.get("erreur") and not r["problemes"]:
        r["resultat"] = "plantage"
        r["fin"] = texte[-400:]
    elif r["problemes"]:
        r["resultat"] = "refuse"
    else:
        r["resultat"] = "erreur"
        r.setdefault("erreur", texte.strip().splitlines()[-1][:300] if texte.strip() else "aucun message")
    r["code"] = sortie.returncode
    return r


def par_l_interface(slicer, stl, gcode, valeurs, choix, echelle, delai):
    """Les mêmes étapes que l'application : ouvrir, régler, poser, proposer, trancher, enregistrer."""
    processus = subprocess.Popen([slicer, "api"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL)
    minuteur = threading.Timer(delai, processus.kill)
    minuteur.start()
    r = {"remarques": [], "problemes": {}, "premier_probleme": None, "nombre_fautes": 0}

    def demander(demande):
        processus.stdin.write((json.dumps(demande) + "\n").encode())
        processus.stdin.flush()
        ligne = processus.stdout.readline()
        if not ligne:
            raise EOFError
        reponse = json.loads(ligne)
        if not reponse.get("ok"):
            raise RuntimeError(reponse.get("erreur", "refus sans message"))
        return reponse

    try:
        demander({"commande": "charger_modele", "chemin": str(stl)})
        refus = demander({"commande": "definir_reglages",
                          "valeurs": {k: ("" if v is None else str(v)) for k, v in valeurs.items()}})["refus"]
        if refus:
            raise RuntimeError("réglages refusés : " + json.dumps(refus, ensure_ascii=False)[:200])
        pose = {"commande": "transformer_modele", "echelle": echelle,
                "orientation": "auto" if choix["orientation_auto"] else "origine"}
        if choix["decalage"]:
            pose["decalage"] = choix["decalage"]
        demander(pose)
        strategies = demander({"commande": "strategies"})["strategies"]
        demande = {"commande": "trancher", "strategie": choix["strategie"]}
        courbes = next((s for s in strategies if s["type"] == "couches_courbes"), None)
        if choix["strategie"] == "couches_courbes" and courbes and len(courbes["parties"]) >= 2 and choix["branche"]:
            partie = 2 + zlib.crc32(str(stl).encode()) % (len(courbes["parties"]) - 1)
            demande["branches"] = [{"a_plat": {"partie": partie, "mode": "a_plat"},
                                    "incline45": {"partie": partie, "mode": "courbes", "inclinaison_max": 45},
                                    "incline60": {"partie": partie, "mode": "courbes", "inclinaison_max": 60},
                                    "cote": {"partie": partie, "mode": "courbes", "cote": -1}}[choix["branche"]]]
            r["branche_choisie"] = demande["branches"][0]
        if courbes:
            r["parties_proposees"] = len(courbes["parties"])
        resultat = demander(demande)["resultat"]
        verification = resultat["verification"]
        r.update(couches=resultat["couches"], filament_m=resultat["filament_m"], duree_s=resultat["duree_s"],
                 remarques=[t[:190] for t in resultat["remarques"]], vide_mm=verification["dans_le_vide_mm"],
                 extrusions_en_rotation=verification["extrusions_en_rotation"],
                 nombre_fautes=verification["nombre_erreurs"])
        for e in verification["erreurs"]:
            nom = classer(e["probleme"])
            r["problemes"][nom] = r["problemes"].get(nom, 0) + 1
            r["premier_probleme"] = r["premier_probleme"] or e["probleme"][:210]
        if verification["valide"]:
            demander({"commande": "enregistrer_gcode", "chemin": str(gcode)})
            r["resultat"] = "verifie"
        else:
            r["resultat"] = "refuse"
    except EOFError:
        code = processus.wait()
        r["resultat"] = "delai" if not minuteur.is_alive() else "plantage"
        r["code"] = code
    except RuntimeError as erreur:
        r["resultat"] = "erreur"
        r["erreur"] = str(erreur)[:300]
    finally:
        minuteur.cancel()
        processus.kill()
        processus.wait()
    return r


def essayer(fiche, slicer, travail, base, tirage, delai):
    choix = tirer(fiche, tirage)
    if FORCER:
        choix.update(FORCER)
    tmp = travail / "tmp"
    stl, gcode, reglages = tmp / f"{fiche['id']}.stl", tmp / f"{fiche['id']}.gcode", tmp / f"{fiche['id']}.txt"
    ligne = {"id": fiche["id"], "source": fiche["source"], "famille": fiche["famille"], "tirage": tirage, "choix": choix}
    debut = time.time()
    try:
        if fiche["source"] == "fabrique":
            # Modèle fabriqué : il est refait à partir de sa famille et de sa graine.
            origine = tmp / f"{fiche['id']}_origine.stl"
            subprocess.run([GENERATEUR[0], fiche["famille"], str(fiche["graine"]), str(origine)],
                           capture_output=True, check=True)
        else:
            origine = travail / "modeles" / fiche["fichier"]
        triangles = lire_stl(origine)
        ligne["triangles"] = int(len(triangles))
        if choix["entree"] == "interface":
            # L'interface fait elle-même la mise à l'échelle et la pose.
            dimensions = triangles.reshape(-1, 3).max(axis=0) - triangles.reshape(-1, 3).min(axis=0)
            echelle = float(np.clip(choix["taille"] / max(float(dimensions.max()), 1e-9), 0.011, 99.0))
            r = par_l_interface(slicer, origine, gcode, choix["reglages"], choix, echelle, delai)
        else:
            pose, echelle = poser(triangles, choix["pose"], choix["taille"])
            ecrire_stl(stl, pose)
            fichier_reglages(base, choix["reglages"], reglages)
            r = par_la_commande(slicer, stl, gcode, reglages, choix, delai)
        ligne["echelle"] = round(echelle, 4)
        ligne.update(r)
        if r["resultat"] == "verifie" and gcode.exists():
            ligne["gcode"] = relire_gcode(gcode, choix["reglages"])
    except Exception as erreur:  # une panne du script lui-même : notée, la campagne continue
        ligne["resultat"] = "panne_du_script"
        ligne["erreur"] = repr(erreur)[:300]
    if not GARDER:
        for fichier in (stl, gcode, reglages, tmp / f"{fiche['id']}_origine.stl"):
            fichier.unlink(missing_ok=True)
    ligne["secondes"] = round(time.time() - debut, 1)
    return ligne


def main():
    arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
    options = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    if len(arguments) != 2:
        sys.exit(__doc__)
    slicer, travail = arguments[0], pathlib.Path(arguments[1])
    tirage = int(options.get("tirage", 1))
    delai = float(options.get("delai", 240))
    if "entree" in options:
        FORCER["entree"] = options["entree"]
    if "--garder" in sys.argv:
        GARDER.append(True)
    if "generateur" in options:
        GENERATEUR[0] = options["generateur"]
    base = pathlib.Path(options.get("reglages", DEPOT / "exemples" / "reglages.exemple.txt")).read_text(encoding="utf-8")
    sortie = pathlib.Path(options.get("resultats", travail / f"resultats-{tirage}.jsonl"))
    (travail / "tmp").mkdir(exist_ok=True)
    faits = set()
    if sortie.exists():
        faits = {json.loads(l)["id"] for l in sortie.read_text(encoding="utf-8").splitlines() if l.strip()}
    fiches = [json.loads(l) for l in (travail / "liste.jsonl").read_text(encoding="utf-8").splitlines() if l.strip()]
    fiches = [f for f in fiches if f["id"] not in faits and options.get("seulement", "") in f["id"]]
    # Un ordre mélangé (toujours le même) : toutes les familles avancent ensemble.
    random.Random(tirage).shuffle(fiches)
    if "limite" in options:
        fiches = fiches[:int(options["limite"])]
    print(f"{len(fiches)} modèles à trancher ({len(faits)} déjà faits)", flush=True)
    verrou = threading.Lock()
    compte = {"n": 0}

    def travailler(fiche):
        ligne = essayer(fiche, slicer, travail, base, tirage, delai)
        with verrou:
            with open(sortie, "a", encoding="utf-8") as f:
                f.write(json.dumps(ligne, ensure_ascii=False) + "\n")
            compte["n"] += 1
            compte[ligne["resultat"]] = compte.get(ligne["resultat"], 0) + 1
            if compte["n"] % 25 == 0:
                print(json.dumps(compte, ensure_ascii=False), flush=True)

    with ThreadPoolExecutor(int(options.get("parallele", 4))) as bassin:
        list(bassin.map(travailler, fiches))
    print("fini :", json.dumps(compte, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
