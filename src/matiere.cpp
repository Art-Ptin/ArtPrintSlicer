// ArtPrint Slicer - licence AGPL v3
#include "matiere.hpp"

#include <cmath>
#include <stdexcept>

namespace artprint {

namespace {

// Numéro du petit cube qui contient cette coordonnée.
std::int64_t indice(double coordonnee, double pas) {
    return static_cast<std::int64_t>(std::floor(coordonnee / pas));
}

// Clé d'un bloc : ses trois numéros rangés dans un seul entier de 64 bits
// (21 bits chacun, décalés pour accepter les coordonnées négatives).
std::uint64_t cle_bloc(std::int64_t i, std::int64_t j, std::int64_t k) {
    constexpr std::int64_t decalage = 1 << 20;
    constexpr std::uint64_t masque = (1u << 21) - 1;
    return ((static_cast<std::uint64_t>((i >> 4) + decalage) & masque) << 42) |
           ((static_cast<std::uint64_t>((j >> 4) + decalage) & masque) << 21) |
           (static_cast<std::uint64_t>((k >> 4) + decalage) & masque);
}

// Numéro du cube à l'intérieur de son bloc, de 0 à 4095.
unsigned position_dans_bloc(std::int64_t i, std::int64_t j, std::int64_t k) {
    return static_cast<unsigned>((i & 15) | ((j & 15) << 4) | ((k & 15) << 8));
}

}  // namespace

GrilleMatiere::GrilleMatiere(double pas) : pas_(pas) {
    if (!(pas > 0.0)) {
        throw std::invalid_argument("Le pas de la grille de matière doit être positif.");
    }
}

void GrilleMatiere::deposer(const Vec3& pointe, const Vec3& direction, double largeur,
                            double hauteur) {
    const double rayon = largeur / 2.0;
    const Vec3 centre = pointe - direction * (hauteur / 2.0);
    // Boule qui contient tout le disque : on ne regarde que les cubes dedans.
    const double portee = std::sqrt(rayon * rayon + hauteur * hauteur / 4.0);

    const std::int64_t i0 = indice(centre.x - portee, pas_), i1 = indice(centre.x + portee, pas_);
    const std::int64_t j0 = indice(centre.y - portee, pas_), j1 = indice(centre.y + portee, pas_);
    const std::int64_t k0 = indice(centre.z - portee, pas_), k1 = indice(centre.z + portee, pas_);

    for (std::int64_t k = k0; k <= k1; ++k) {
        for (std::int64_t j = j0; j <= j1; ++j) {
            for (std::int64_t i = i0; i <= i1; ++i) {
                const Vec3 cube{(static_cast<double>(i) + 0.5) * pas_,
                                (static_cast<double>(j) + 0.5) * pas_,
                                (static_cast<double>(k) + 0.5) * pas_};
                const Vec3 ecart = cube - pointe;
                // Position le long de la buse : 0 à la pointe, négatif dessous.
                const double axial = produit_scalaire(ecart, direction);
                if (axial > 0.0 || axial < -hauteur) {
                    continue;
                }
                const double radial2 = produit_scalaire(ecart, ecart) - axial * axial;
                if (radial2 > rayon * rayon) {
                    continue;
                }
                const std::uint64_t cle = cle_bloc(i, j, k);
                if (cle != cle_recente_ || !bloc_recent_) {
                    bloc_recent_ = &blocs_[cle];  // (les blocs ne bougent pas en mémoire)
                    cle_recente_ = cle;
                }
                Bloc& bloc = *bloc_recent_;
                const unsigned position = position_dans_bloc(i, j, k);
                const std::uint64_t bit = std::uint64_t{1} << (position & 63);
                if (!(bloc.bits[position >> 6] & bit)) {
                    bloc.bits[position >> 6] |= bit;
                    ++cubes_remplis_;
                }
            }
        }
    }
}

bool GrilleMatiere::occupe(const Vec3& point) const {
    const std::int64_t i = indice(point.x, pas_);
    const std::int64_t j = indice(point.y, pas_);
    const std::int64_t k = indice(point.z, pas_);
    const Bloc* bloc = trouver(cle_bloc(i, j, k));
    if (!bloc) {
        return false;
    }
    const unsigned position = position_dans_bloc(i, j, k);
    return (bloc->bits[position >> 6] >> (position & 63)) & 1u;
}

GrilleMatiere::Bloc* GrilleMatiere::trouver(std::uint64_t cle) const {
    if (cle == cle_recente_) {
        return bloc_recent_;
    }
    const auto trouve = blocs_.find(cle);
    // (Un bloc absent est retenu aussi : « rien ici » se redemande souvent.)
    cle_recente_ = cle;
    bloc_recent_ = trouve == blocs_.end() ? nullptr : const_cast<Bloc*>(&trouve->second);
    return bloc_recent_;
}

}  // namespace artprint
