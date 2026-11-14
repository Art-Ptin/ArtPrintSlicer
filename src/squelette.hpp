// ArtPrint Slicer - licence AGPL v3
// Squelette d'un modèle : le « tronc » et les « branches » dont il est fait.
//
// Méthode : le modèle est rempli de petits cubes (voxels). Pour chaque cube,
// on mesure le chemin le plus court, à l'intérieur de la matière, jusqu'à la
// base posée sur le plateau. Les cubes situés à la même distance forment des
// tranches ; quand une tranche se sépare en plusieurs morceaux, c'est qu'une
// branche part. Chaque branche est décrite par la suite des centres de ses
// tranches : cela donne sa direction, sa longueur et son épaisseur.
// Une branche courbe est partagée en tronçons à peu près droits, chacun porté
// par le précédent.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "geometrie.hpp"
#include "maillage.hpp"

namespace artprint {

// Le modèle rempli de petits cubes : un voxel est « plein » si son centre
// est dans la matière.
struct GrilleVoxels {
    int nx = 0, ny = 0, nz = 0;
    double pas = 1.0;  // côté d'un voxel, en mm
    Vec3 origine;      // coin de la grille
    std::vector<std::uint8_t> plein;

    std::size_t indice(int i, int j, int k) const {
        return (static_cast<std::size_t>(k) * ny + j) * nx + i;
    }
    Vec3 centre(int i, int j, int k) const {
        return {origine.x + (i + 0.5) * pas, origine.y + (j + 0.5) * pas, origine.z + (k + 0.5) * pas};
    }
    // Vrai si ce point de l'espace est dans la matière.
    bool contient(const Vec3& p) const {
        const int i = static_cast<int>(std::floor((p.x - origine.x) / pas));
        const int j = static_cast<int>(std::floor((p.y - origine.y) / pas));
        const int k = static_cast<int>(std::floor((p.z - origine.z) / pas));
        return i >= 0 && j >= 0 && k >= 0 && i < nx && j < ny && k < nz && plein[indice(i, j, k)] != 0;
    }
};

GrilleVoxels remplir_voxels(const Maillage& maillage, double pas);

// Côté de voxel adapté à la taille du modèle (environ 140 voxels sur le plus grand côté).
double pas_voxels_conseille(const Maillage& maillage);

// Une tranche d'une branche.
struct TrancheSquelette {
    Vec3 centre;
    double rayon = 0.0;   // rayon d'un disque de même section, en mm
    double volume = 0.0;  // mm³
};

struct BrancheSquelette {
    int parent = -1;           // branche d'où elle part (-1 : elle part du plateau)
    // Vrai si cette branche est la suite d'une branche courbe (le tronçon
    // suivant de la même tige), et non une vraie bifurcation.
    bool suite_courbe = false;
    std::vector<int> enfants;  // branches qui partent de son extrémité
    std::vector<TrancheSquelette> tranches;  // de la base vers l'extrémité
    Vec3 direction;            // direction générale, longueur 1
    double longueur = 0.0;     // mm, le long des centres
    double volume = 0.0;       // mm³, la branche seule
    double volume_total = 0.0; // mm³, avec toutes les branches qu'elle porte
};

struct Squelette {
    std::vector<BrancheSquelette> branches;  // reliées par « parent » et « enfants »
    double pas = 0.0;            // côté d'un voxel, en mm
    double epaisseur_tranche = 0.0;  // mm
    std::size_t voxels = 0;      // nombre de voxels de matière
    bool boucles = false;        // vrai si la matière forme une anse (deux chemins se rejoignent)
};

// Calcule le squelette d'un modèle fermé, posé sur le plateau (dessous à la
// hauteur zéro). « pas » : côté d'un voxel en mm ; 0 = choisi d'après la
// taille du modèle.
Squelette calculer_squelette(const Maillage& pose, double pas = 0.0);

}  // namespace artprint
