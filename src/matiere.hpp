// ArtPrint Slicer - licence AGPL v3
// Mémoire de la matière déjà déposée, sous forme de petits cubes (voxels).
// L'espace est découpé en blocs de 16 x 16 x 16 cubes ; seuls les blocs qui
// contiennent de la matière sont gardés en mémoire.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "geometrie.hpp"

namespace artprint {

class GrilleMatiere {
public:
    // « pas » : côté d'un petit cube, en millimètres.
    explicit GrilleMatiere(double pas);

    double pas() const { return pas_; }

    // Dépose un morceau de cordon sous la buse : un disque de diamètre
    // « largeur », d'épaisseur « hauteur », posé juste sous la pointe et
    // perpendiculaire à la direction de la buse.
    void deposer(const Vec3& pointe, const Vec3& direction, double largeur, double hauteur);

    // Vrai s'il y a déjà de la matière à cet endroit.
    bool occupe(const Vec3& point) const;

    // Nombre de petits cubes remplis.
    std::size_t cubes_remplis() const { return cubes_remplis_; }

private:
    struct Bloc {
        std::array<std::uint64_t, 64> bits{};  // 4096 cubes, un bit chacun
    };

    double pas_;
    std::size_t cubes_remplis_ = 0;
    std::unordered_map<std::uint64_t, Bloc> blocs_;
    // Le dernier bloc touché : la buse avance de proche en proche, c'est
    // presque toujours le même que la fois d'avant.
    mutable std::uint64_t cle_recente_ = ~std::uint64_t{0};
    mutable Bloc* bloc_recent_ = nullptr;
    Bloc* trouver(std::uint64_t cle) const;
};

}  // namespace artprint
