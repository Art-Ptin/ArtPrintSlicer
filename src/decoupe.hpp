// ArtPrint Slicer - licence AGPL v3
// Découpe d'un modèle par un plan : sépare un morceau (par exemple une
// branche) du reste du modèle. Les deux morceaux sont refermés par un
// « bouchon » sur le plan de coupe : ce sont de nouveau des volumes fermés,
// que le slicer sait trancher.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "geometrie.hpp"
#include "maillage.hpp"

namespace artprint {

// Un plan : un point du plan et sa normale (direction perpendiculaire).
struct Plan {
    Vec3 point;
    Vec3 normale;
};

struct ResultatDecoupe {
    Maillage partie;  // le morceau du côté de la normale, autour des graines
    Maillage reste;   // tout le reste du modèle
    double surface_coupe = 0.0;  // surface de la section, en mm²
    Vec3 centre_coupe;           // centre de la section
    std::size_t boucles = 0;     // nombre de contours de la section
};

// Coupe le maillage par le plan. Du côté de la normale, seuls les morceaux
// les plus proches des « graines » (des points situés dans la matière à
// détacher) sont séparés ; les autres morceaux restent dans « reste ».
// Renvoie « rien » si aucun morceau ne peut être séparé proprement.
std::optional<ResultatDecoupe> couper_par_plan(const Maillage& maillage, const Plan& plan,
                                               const std::vector<Vec3>& graines);

// Volume du maillage fermé, en mm³.
double volume_maillage(const Maillage& maillage);

// Deux directions perpendiculaires à « w » et entre elles : (u, v, w) forme
// un repère direct. « w » doit être de longueur 1.
void repere_autour(const Vec3& w, Vec3& u, Vec3& v);

// Exprime le maillage dans le repère (origine, u, v, w) : les nouvelles
// coordonnées x, y, z sont mesurées le long de u, v, w.
Maillage dans_repere(const Maillage& maillage, const Vec3& origine, const Vec3& u,
                     const Vec3& v, const Vec3& w);

// Distance d'un point à un triangle, en mm.
double distance_point_triangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c);

}  // namespace artprint
