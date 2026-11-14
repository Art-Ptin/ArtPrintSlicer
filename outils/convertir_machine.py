# ArtPrint Slicer - licence AGPL v3
"""Convertit le fichier STEP de l'imprimante en « modèle de machine » pour le slicer.

Le STEP reste privé : ni lui, ni le fichier produit, ni le fichier de groupes ne
doivent être publiés (ils vont dans le dossier machine/, ignoré par git).

Utilisation :
    python outils/convertir_machine.py machine/imprimante.step machine/groupes.txt machine/machine.apm

Le fichier de groupes dit quelles pièces du STEP bougent ensemble. Chaque ligne
associe un nom à une expression régulière, cherchée dans le chemin de la pièce
dans l'assemblage (« Assemblage / Sous-ensemble / Pièce ») :

    groupe.bati       = ...   pièces fixes
    groupe.chariot_y  = ...   pièces qui suivent l'axe Y (sous le plateau)
    groupe.plateau    = ...   plateau rotatif (axes Y et C)
    groupe.axe_z      = ...   pièces qui suivent l'axe Z
    groupe.chariot_x  = ...   pièces qui suivent X et Z
    groupe.tete       = ...   tête inclinable (axes X, Z et B)
    ignorer           = ...   pièces souples à ne pas prendre (câbles, courroies, tubes)
    piece.buse        = ...   la buse : son point le plus bas est la pointe
    piece.pivot_b     = ...   l'axe du pivot B (un cylindre)
    piece.plateau     = ...   le plateau : son dessus et son centre donnent l'origine
    precision         = 0.15  finesse de la conversion en triangles, en mm

Le modèle est écrit dans le repère de la pièce : origine au centre du plateau,
sur sa surface, Z vers le haut. Le STEP doit montrer la machine avec la buse
verticale (B = 0).

Nécessite : pip install cadquery-ocp
"""
import re
import struct
import sys

from OCP.Bnd import Bnd_Box
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.GeomAbs import GeomAbs_Cylinder
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDF import TDF_Label
from OCP.TDocStd import TDocStd_Document
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS
from OCP.XCAFDoc import XCAFDoc_DocumentTool, XCAFDoc_ShapeTool
from OCP.collections import Sequence_TDF_Label

GROUPES = ["bati", "chariot_y", "plateau", "axe_z", "chariot_x", "tete"]  # code = position


def lire_configuration(chemin):
    config = {}
    for numero, ligne in enumerate(open(chemin, encoding="utf-8"), 1):
        ligne = ligne.split("#")[0].strip()
        if not ligne:
            continue
        if "=" not in ligne:
            sys.exit(f"{chemin}, ligne {numero} : il manque le signe « = ».")
        cle, valeur = (morceau.strip() for morceau in ligne.split("=", 1))
        config[cle] = valeur
    for cle in [f"groupe.{g}" for g in GROUPES] + ["piece.buse", "piece.pivot_b", "piece.plateau"]:
        if cle not in config:
            sys.exit(f"{chemin} : il manque la ligne « {cle} = ... ».")
    return config


def lire_pieces(chemin_step):
    """Rend la liste (chemin dans l'assemblage, forme placée) de toutes les pièces."""
    document = TDocStd_Document(TCollection_ExtendedString("machine"))
    lecteur = STEPCAFControl_Reader()
    lecteur.SetNameMode(True)
    if lecteur.ReadFile(chemin_step) != IFSelect_RetDone:
        sys.exit(f"Impossible de lire le fichier STEP « {chemin_step} ».")
    lecteur.Transfer(document)
    outil = XCAFDoc_DocumentTool.ShapeTool_s(document.Main())

    def nom(etiquette):
        attribut = TDataStd_Name()
        if etiquette.FindAttribute(TDataStd_Name.GetID_s(), attribut):
            return attribut.Get().ToExtString()
        return "?"

    pieces = []

    def parcourir(etiquette, placement, chemin):
        n = nom(etiquette)
        if XCAFDoc_ShapeTool.IsReference_s(etiquette):
            cible = TDF_Label()
            XCAFDoc_ShapeTool.GetReferredShape_s(etiquette, cible)
            placement = placement.Multiplied(XCAFDoc_ShapeTool.GetLocation_s(etiquette))
            n = nom(cible) if nom(cible) != "?" else n
            etiquette = cible
        chemin = f"{chemin} / {n}"
        if XCAFDoc_ShapeTool.IsAssembly_s(etiquette):
            enfants = Sequence_TDF_Label()
            XCAFDoc_ShapeTool.GetComponents_s(etiquette, enfants, False)
            for i in range(1, enfants.Length() + 1):
                parcourir(enfants.Value(i), placement, chemin)
        else:
            pieces.append((chemin, XCAFDoc_ShapeTool.GetShape_s(etiquette).Moved(placement)))

    racines = Sequence_TDF_Label()
    outil.GetFreeShapes(racines)
    for i in range(1, racines.Length() + 1):
        parcourir(racines.Value(i), TopLoc_Location(), "")
    return pieces


def boite(forme):
    b = Bnd_Box()
    BRepBndLib.AddOptimal_s(forme, b, False, False)
    mini, maxi = b.CornerMin(), b.CornerMax()
    return (mini.X(), mini.Y(), mini.Z(), maxi.X(), maxi.Y(), maxi.Z())


def trianguler(forme, precision):
    """Rend les triangles de la forme : une liste à plat de 9 nombres par triangle."""
    BRepMesh_IncrementalMesh(forme, precision, False, 0.5, True)
    triangles = []
    explorateur = TopExp_Explorer(forme, TopAbs_FACE)
    while explorateur.More():
        face = TopoDS.Face(explorateur.Current())
        placement = TopLoc_Location()
        maillage = BRep_Tool.Triangulation_s(face, placement)
        if maillage is not None:
            transformation = placement.Transformation()
            points = [maillage.Node(i).Transformed(transformation)
                      for i in range(1, maillage.NbNodes() + 1)]
            inverse = face.Orientation() == TopAbs_REVERSED
            for i in range(1, maillage.NbTriangles() + 1):
                a, b, c = maillage.Triangle(i).Get()
                if inverse:
                    b, c = c, b
                for indice in (a, b, c):
                    p = points[indice - 1]
                    triangles += [p.X(), p.Y(), p.Z()]
        explorateur.Next()
    return triangles


def axe_du_cylindre(forme):
    """Rend (point, direction) de l'axe du plus gros cylindre de la forme."""
    meilleur = None
    explorateur = TopExp_Explorer(forme, TopAbs_FACE)
    while explorateur.More():
        surface = BRepAdaptor_Surface(TopoDS.Face(explorateur.Current()))
        if surface.GetType() == GeomAbs_Cylinder:
            cylindre = surface.Cylinder()
            if meilleur is None or cylindre.Radius() > meilleur[0]:
                axe = cylindre.Axis()
                meilleur = (cylindre.Radius(),
                            (axe.Location().X(), axe.Location().Y(), axe.Location().Z()),
                            (axe.Direction().X(), axe.Direction().Y(), axe.Direction().Z()))
        explorateur.Next()
    return meilleur


def unique(pieces, motif, role):
    trouvees = [(c, f) for c, f in pieces if re.search(motif, c)]
    if len(trouvees) != 1:
        sys.exit(f"« {role} » : {len(trouvees)} pièce(s) correspondent à « {motif} », "
                 "il en faut exactement une.")
    return trouvees[0][1]


def texte(t):
    octets = t.encode("utf-8")
    return struct.pack("<H", len(octets)) + octets


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    chemin_step, chemin_groupes, chemin_sortie = sys.argv[1:]
    config = lire_configuration(chemin_groupes)
    precision = float(config.get("precision", "0.15").replace(",", "."))
    print("Lecture du STEP...")
    pieces = lire_pieces(chemin_step)
    print(f"  {len(pieces)} pièces")

    # Origine : centre du plateau, sur sa surface.
    b = boite(unique(pieces, config["piece.plateau"], "piece.plateau"))
    origine = ((b[0] + b[3]) / 2, (b[1] + b[4]) / 2, b[5])
    rayon_plateau = (b[3] - b[0]) / 2
    # Pointe de la buse : le point le plus bas de la buse, sur son axe.
    b = boite(unique(pieces, config["piece.buse"], "piece.buse"))
    pointe = ((b[0] + b[3]) / 2 - origine[0], (b[1] + b[4]) / 2 - origine[1], b[2] - origine[2])
    # Pivot B : le point de son axe situé à la verticale de la buse.
    cylindre = axe_du_cylindre(unique(pieces, config["piece.pivot_b"], "piece.pivot_b"))
    if cylindre is None:
        sys.exit("« piece.pivot_b » : cette pièce ne contient aucun cylindre.")
    _, point_axe, direction = cylindre
    relatif = [point_axe[i] - origine[i] for i in range(3)]
    t = sum((pointe[i] - relatif[i]) * direction[i] for i in range(3))
    pivot = [relatif[i] + t * direction[i] for i in range(3)]

    ignorer = config.get("ignorer", "")
    groupes = {g: [] for g in GROUPES}
    sans_groupe = []
    for chemin, forme in pieces:
        nom = chemin.split(" / ")[-1]
        if ignorer and re.search(ignorer, nom):
            continue
        for g in GROUPES[::-1]:  # la tête d'abord : le groupe le plus précis l'emporte
            if re.search(config[f"groupe.{g}"], chemin):
                groupes[g].append((nom, forme))
                break
        else:
            sans_groupe.append(chemin)
    if sans_groupe:
        print(f"  Attention : {len(sans_groupe)} pièce(s) sans groupe, mises dans le bâti :")
        for chemin in sans_groupe[:10]:
            print("     ", chemin)
        groupes["bati"] += [(c.split(" / ")[-1], f) for c, f in
                            [(c, f) for c, f in pieces if c in set(sans_groupe)]]

    print("Conversion en triangles...")
    with open(chemin_sortie, "wb") as sortie:
        sortie.write(b"APM1")
        sortie.write(struct.pack("<I", len(GROUPES)))
        total = 0
        for code, g in enumerate(GROUPES):
            sortie.write(texte(g) + struct.pack("<BI", code, len(groupes[g])))
            compte = 0
            for nom, forme in groupes[g]:
                triangles = trianguler(forme, precision)
                for i in range(0, len(triangles), 3):
                    for k in range(3):
                        triangles[i + k] -= origine[k]
                sortie.write(texte(nom) + struct.pack("<I", len(triangles) // 9))
                sortie.write(struct.pack(f"<{len(triangles)}f", *triangles))
                compte += len(triangles) // 9
            total += compte
            print(f"  {g:10s} : {len(groupes[g]):4d} pièces, {compte:8d} triangles")
        sortie.write(struct.pack("<9f", *pivot, *direction, *pointe))
    print(f"Modèle écrit : {chemin_sortie} ({total} triangles)")
    print(f"  Rayon du plateau         : {rayon_plateau:.1f} mm")
    print(f"  Pointe de la buse        : X {pointe[0]:.2f}  Y {pointe[1]:.2f}  Z {pointe[2]:.2f} mm")
    print(f"  Distance pivot B - buse  : {pivot[2] - pointe[2]:.2f} mm "
          "(à reporter dans le réglage « distance_pivot_buse »)")
    print(f"  Direction de l'axe du pivot B : {direction[0]:.2f} {direction[1]:.2f} {direction[2]:.2f}")


if __name__ == "__main__":
    main()
