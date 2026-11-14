// ArtPrint Slicer - licence AGPL v3
// Champ de distance : une grille de petits cubes autour d'un groupe de pièces.
// Chaque cube connaît la distance à la pièce la plus proche et son numéro.
// Savoir si un point touche le groupe devient une simple lecture de grille.
#pragma once

#include <cstdint>
#include <vector>

#include "geometrie.hpp"
#include "machine.hpp"

namespace artprint {

class ChampDistance {
public:
    // « pas » : côté d'un cube en mm. « portee » : distance utile autour du
    // groupe ; au-delà, le champ rend simplement « loin ».
    ChampDistance(const GroupeMachine& groupe, double pas, double portee);

    struct Lecture {
        double distance;  // mm jusqu'à la surface ; 0 à l'intérieur d'une pièce
        int piece;        // numéro de la pièce la plus proche dans le groupe
    };
    // Distance au point (repère du modèle). Hors de la grille : une distance
    // au moins égale à la portée.
    Lecture lire(const Vec3& point) const;

    // Boîte englobante du groupe (sans la portée).
    const Vec3& mini() const { return boite_min_; }
    const Vec3& maxi() const { return boite_max_; }
    double pas() const { return pas_; }
    double portee() const { return portee_; }
    bool vide() const { return distances_.empty(); }

    // Points de la surface du groupe : le centre des cubes traversés par une
    // pièce, avec le numéro de cette pièce.
    struct PointSurface {
        Vec3 position;
        int piece;
    };
    std::vector<PointSurface> points_de_surface() const;

private:
    std::size_t indice(int i, int j, int k) const {
        return (static_cast<std::size_t>(k) * ny_ + j) * nx_ + i;
    }

    double pas_;
    double portee_;
    Vec3 origine_;  // coin de la grille
    Vec3 boite_min_, boite_max_;
    int nx_ = 0, ny_ = 0, nz_ = 0;
    std::vector<float> distances_;
    std::vector<std::uint16_t> pieces_;
    std::vector<std::uint8_t> surface_;  // 1 : cube traversé par une pièce
};

}  // namespace artprint
