// ArtPrint Slicer - licence AGPL v3
// Champ de couches : pour chaque point du modèle, la longueur du chemin le
// plus court jusqu'au plateau en restant dans la matière.
//
// Les couches courbes sont les surfaces où cette longueur est la même.
// Dans une partie droite posée sur le plateau (un tronc), le chemin le plus
// court est une verticale : la longueur vaut la hauteur, et les couches sont
// des plans horizontaux, comme en 3 axes. Sous un surplomb (une branche), le
// chemin contourne le coin où la branche quitte le tronc : les couches
// s'arrondissent autour de ce coin et entrent dans la branche. Comme la
// longueur augmente partout de 1 mm par mm parcouru, deux couches voisines
// sont partout à la même distance l'une de l'autre : leur épaisseur est
// régulière.
#pragma once

#include <cstddef>
#include <vector>

#include "geometrie.hpp"
#include "maillage.hpp"

namespace artprint {

struct ChampCouches {
    int nx = 0, ny = 0, nz = 0;
    double pas = 1.0;  // côté d'un cube de la grille, en mm
    Vec3 origine;      // coin de la grille
    // Longueur du chemin pour chaque cube ; négatif infini hors de la zone
    // calculée (la matière et une petite marge autour).
    std::vector<float> valeurs;
    double maximum = 0.0;  // plus grande longueur dans la matière

    std::size_t indice(int i, int j, int k) const {
        return (static_cast<std::size_t>(k) * ny + j) * nx + i;
    }
    bool defini(int i, int j, int k) const;
    // Longueur du chemin en un point quelconque, interpolée entre les cubes
    // voisins. Rend « faux » si le point est trop loin de la matière.
    bool valeur(const Vec3& p, double& resultat) const;
    // Direction dans laquelle la longueur augmente le plus vite (la normale
    // à la couche), et vitesse de cette augmentation (1 dans l'idéal).
    // Rend « faux » si le point est trop loin de la matière.
    bool gradient(const Vec3& p, Vec3& direction, double& pente) const;
};

// Calcule le champ d'un modèle fermé posé sur le plateau (dessous à la
// hauteur zéro). « pas » : côté d'un cube en mm ; 0 = choisi d'après la
// taille du modèle.
ChampCouches calculer_champ_couches(const Maillage& pose, double pas = 0.0);

}  // namespace artprint
