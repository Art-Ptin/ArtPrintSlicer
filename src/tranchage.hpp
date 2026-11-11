// ArtPrint Slicer - licence AGPL v3
// Tranchage : coupe le modèle 3D par des plans horizontaux et rend, pour
// chaque plan, les contours de la matière.
#pragma once

#include <cstddef>
#include <vector>

#include "maillage.hpp"
#include "polygones.hpp"

namespace artprint {

struct ResultatCoupe {
    // Pour chaque hauteur demandée, les contours de la matière.
    std::vector<Polygones> couches;
    // Nombre de contours restés ouverts (modèle abîmé) : ils sont ignorés.
    std::size_t contours_ouverts = 0;
};

// Coupe le maillage aux hauteurs données (en mm, dans l'ordre croissant).
// Le maillage doit être fermé et bien orienté (voir verifier_fermeture).
ResultatCoupe couper_maillage(const Maillage& maillage, const std::vector<double>& hauteurs);

}  // namespace artprint
