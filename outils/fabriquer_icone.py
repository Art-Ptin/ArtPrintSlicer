# ArtPrint Slicer - licence AGPL v3
"""Assemble les images de l'icône (voir fabriquer_icone.js) en un fichier .ico.

Utilisation :
    python outils/fabriquer_icone.py <dossier_des_png> <sortie.ico>

Les petites tailles sont écrites sans compression (lues par tous les outils,
dont celui qui fabrique l'installateur) ; la taille 256 est en PNG.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

TAILLES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def image_bmp(image):
    """Une image au format des icônes : en-tête, pixels BGRA de bas en haut, masque."""
    largeur, hauteur = image.size
    pixels = image.convert("RGBA").transpose(Image.FLIP_TOP_BOTTOM).tobytes("raw", "BGRA")
    ligne_masque = ((largeur + 31) // 32) * 4
    masque = bytes(ligne_masque * hauteur)  # tout visible : la transparence est dans les pixels
    entete = struct.pack("<IiiHHIIiiII", 40, largeur, hauteur * 2, 1, 32, 0, len(pixels) + len(masque), 0, 0, 0, 0)
    return entete + pixels + masque


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    dossier, sortie = Path(sys.argv[1]), Path(sys.argv[2])
    donnees = []
    for taille in TAILLES:
        chemin = dossier / f"icone-{taille}.png"
        if taille == 256:
            donnees.append(chemin.read_bytes())
        else:
            donnees.append(image_bmp(Image.open(chemin)))
    entete = struct.pack("<HHH", 0, 1, len(TAILLES))
    position = 6 + 16 * len(TAILLES)
    table = b""
    for taille, bloc in zip(TAILLES, donnees):
        table += struct.pack("<BBBBHHII", taille % 256, taille % 256, 0, 0, 1, 32, len(bloc), position)
        position += len(bloc)
    sortie.write_bytes(entete + table + b"".join(donnees))
    print(f"{sortie} : {len(TAILLES)} tailles, {position} octets")


if __name__ == "__main__":
    main()
