// ArtPrint Slicer - licence AGPL v3
//
// Construction :
//   1. Chaque triangle est échantillonné finement ; les cubes touchés sont
//      marqués « surface ».
//   2. Une « transformée de distance » exacte (méthode de Felzenszwalb et
//      Huttenlocher, trois passes le long de X, Y puis Z) donne pour chaque
//      cube le cube de surface le plus proche.
//   3. Les cubes enfermés dans une pièce (inatteignables depuis l'extérieur
//      sans traverser la surface) reçoivent la distance zéro.
#include "champ_distance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace artprint {

namespace {

constexpr float infini = 1e20f;

// Transformée de distance à une dimension : pour chaque case q, cherche la
// case source p qui rend (q - p)² + f[p] minimal. « source » suit la case de
// surface d'origine.
void transformer_1d(const float* f, const std::int32_t* source_entree, int n, float* resultat,
                    std::int32_t* source_sortie, std::vector<int>& v, std::vector<float>& z) {
    int k = 0;
    v[0] = 0;
    z[0] = -infini;
    z[1] = infini;
    for (int q = 1; q < n; ++q) {
        float s;
        for (;;) {
            const int p = v[k];
            s = ((f[q] + static_cast<float>(q) * q) - (f[p] + static_cast<float>(p) * p)) /
                (2.0f * (q - p));
            if (s > z[k]) {
                break;
            }
            --k;
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = infini;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < static_cast<float>(q)) {
            ++k;
        }
        const int p = v[k];
        resultat[q] = static_cast<float>(q - p) * (q - p) + f[p];
        source_sortie[q] = source_entree[p];
    }
}

}  // namespace

ChampDistance::ChampDistance(const GroupeMachine& groupe, double pas, double portee)
    : pas_(pas), portee_(portee) {
    // Boîte englobante de toutes les pièces.
    Vec3 mini{1e30, 1e30, 1e30}, maxi{-1e30, -1e30, -1e30};
    bool trouve = false;
    for (const PieceMachine& piece : groupe.pieces) {
        for (std::size_t i = 0; i + 2 < piece.triangles.size(); i += 3) {
            const Vec3 p{piece.triangles[i], piece.triangles[i + 1], piece.triangles[i + 2]};
            mini = {std::min(mini.x, p.x), std::min(mini.y, p.y), std::min(mini.z, p.z)};
            maxi = {std::max(maxi.x, p.x), std::max(maxi.y, p.y), std::max(maxi.z, p.z)};
            trouve = true;
        }
    }
    if (!trouve) {
        boite_min_ = boite_max_ = {};
        return;
    }
    boite_min_ = mini;
    boite_max_ = maxi;
    const double bord = portee + 2.0 * pas;
    origine_ = mini - Vec3{bord, bord, bord};
    nx_ = static_cast<int>(std::ceil((maxi.x - mini.x + 2.0 * bord) / pas)) + 1;
    ny_ = static_cast<int>(std::ceil((maxi.y - mini.y + 2.0 * bord) / pas)) + 1;
    nz_ = static_cast<int>(std::ceil((maxi.z - mini.z + 2.0 * bord) / pas)) + 1;
    const std::size_t total = static_cast<std::size_t>(nx_) * ny_ * nz_;
    surface_.assign(total, 0);
    pieces_.assign(total, 0);

    // 1. Marque les cubes traversés par les triangles.
    const double fin = pas * 0.45;  // écart entre deux points d'échantillonnage
    for (std::size_t numero = 0; numero < groupe.pieces.size(); ++numero) {
        const std::vector<float>& t = groupe.pieces[numero].triangles;
        for (std::size_t i = 0; i + 8 < t.size(); i += 9) {
            const Vec3 a{t[i], t[i + 1], t[i + 2]}, b{t[i + 3], t[i + 4], t[i + 5]},
                c{t[i + 6], t[i + 7], t[i + 8]};
            const double plus_long =
                std::max({norme(b - a), norme(c - a), norme(c - b)});
            const int n = std::max(1, static_cast<int>(std::ceil(plus_long / fin)));
            for (int u = 0; u <= n; ++u) {
                for (int w = 0; w <= n - u; ++w) {
                    const double s = static_cast<double>(u) / n, r = static_cast<double>(w) / n;
                    const Vec3 p = a + (b - a) * s + (c - a) * r;
                    const int ci = static_cast<int>((p.x - origine_.x) / pas);
                    const int cj = static_cast<int>((p.y - origine_.y) / pas);
                    const int ck = static_cast<int>((p.z - origine_.z) / pas);
                    const std::size_t cube = indice(ci, cj, ck);
                    surface_[cube] = 1;
                    pieces_[cube] = static_cast<std::uint16_t>(numero);
                }
            }
        }
    }

    // 2. Transformée de distance : carré de la distance (en cubes) et cube
    // de surface le plus proche.
    std::vector<float> carre(total);
    std::vector<std::int32_t> source(total);
    for (std::size_t i = 0; i < total; ++i) {
        carre[i] = surface_[i] ? 0.0f : infini;
        source[i] = static_cast<std::int32_t>(i);
    }
    const int plus_grand = std::max({nx_, ny_, nz_});
    std::vector<float> f(plus_grand), d(plus_grand), z(plus_grand + 1);
    std::vector<std::int32_t> se(plus_grand), ss(plus_grand);
    std::vector<int> v(plus_grand);
    auto passe = [&](int n, std::size_t pas_indice, auto&& depart_de) {
        for (int q = 0; q < n; ++q) {
            const std::size_t i = depart_de + static_cast<std::size_t>(q) * pas_indice;
            f[q] = carre[i];
            se[q] = source[i];
        }
        transformer_1d(f.data(), se.data(), n, d.data(), ss.data(), v, z);
        for (int q = 0; q < n; ++q) {
            const std::size_t i = depart_de + static_cast<std::size_t>(q) * pas_indice;
            carre[i] = d[q];
            source[i] = ss[q];
        }
    };
    for (int k = 0; k < nz_; ++k) {
        for (int j = 0; j < ny_; ++j) {
            passe(nx_, 1, indice(0, j, k));
        }
    }
    for (int k = 0; k < nz_; ++k) {
        for (int i = 0; i < nx_; ++i) {
            passe(ny_, static_cast<std::size_t>(nx_), indice(i, 0, k));
        }
    }
    for (int j = 0; j < ny_; ++j) {
        for (int i = 0; i < nx_; ++i) {
            passe(nz_, static_cast<std::size_t>(nx_) * ny_, indice(i, j, 0));
        }
    }

    // 3. Intérieur des pièces : on part d'un coin (dehors) et on se propage
    // dans les cubes libres ; ceux qu'on n'atteint jamais sont enfermés.
    std::vector<std::uint8_t> dehors(total, 0);
    std::vector<std::size_t> pile{0};
    dehors[0] = 1;
    const std::ptrdiff_t voisins[6] = {1, -1, nx_, -nx_, static_cast<std::ptrdiff_t>(nx_) * ny_,
                                       -static_cast<std::ptrdiff_t>(nx_) * ny_};
    while (!pile.empty()) {
        const std::size_t cube = pile.back();
        pile.pop_back();
        const int i = static_cast<int>(cube % nx_);
        const int j = static_cast<int>((cube / nx_) % ny_);
        const int k = static_cast<int>(cube / (static_cast<std::size_t>(nx_) * ny_));
        const bool possible[6] = {i + 1 < nx_, i > 0, j + 1 < ny_, j > 0, k + 1 < nz_, k > 0};
        for (int n = 0; n < 6; ++n) {
            if (!possible[n]) {
                continue;
            }
            const std::size_t voisin = static_cast<std::size_t>(
                static_cast<std::ptrdiff_t>(cube) + voisins[n]);
            if (!dehors[voisin] && !surface_[voisin]) {
                dehors[voisin] = 1;
                pile.push_back(voisin);
            }
        }
    }

    distances_.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        distances_[i] = dehors[i] ? std::sqrt(carre[i]) * static_cast<float>(pas) : 0.0f;
        pieces_[i] = pieces_[static_cast<std::size_t>(source[i])];
    }
    // Les numéros de pièce ont été recopiés depuis les cubes de surface :
    // ceux-ci gardent leur propre numéro (source = eux-mêmes).
}

ChampDistance::Lecture ChampDistance::lire(const Vec3& point) const {
    if (distances_.empty()) {
        return {std::numeric_limits<double>::max(), 0};
    }
    // Position dans la grille, comptée entre les centres des cubes.
    const double x = (point.x - origine_.x) / pas_ - 0.5;
    const double y = (point.y - origine_.y) / pas_ - 0.5;
    const double z = (point.z - origine_.z) / pas_ - 0.5;
    if (x < 0.0 || y < 0.0 || z < 0.0 || x >= nx_ - 1 || y >= ny_ - 1 || z >= nz_ - 1) {
        // Hors de la grille : la distance à la boîte du groupe, au moins la portée.
        const double dx = std::max({boite_min_.x - point.x, 0.0, point.x - boite_max_.x});
        const double dy = std::max({boite_min_.y - point.y, 0.0, point.y - boite_max_.y});
        const double dz = std::max({boite_min_.z - point.z, 0.0, point.z - boite_max_.z});
        return {std::max(portee_, std::sqrt(dx * dx + dy * dy + dz * dz)), 0};
    }
    const int i = static_cast<int>(x), j = static_cast<int>(y), k = static_cast<int>(z);
    const double fx = x - i, fy = y - j, fz = z - k;
    // Moyenne pondérée des huit cubes voisins.
    double distance = 0.0;
    for (int c = 0; c < 8; ++c) {
        const double poids = ((c & 1) ? fx : 1.0 - fx) * ((c & 2) ? fy : 1.0 - fy) *
                             ((c & 4) ? fz : 1.0 - fz);
        distance += poids * distances_[indice(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1))];
    }
    const std::size_t proche =
        indice(i + (fx > 0.5 ? 1 : 0), j + (fy > 0.5 ? 1 : 0), k + (fz > 0.5 ? 1 : 0));
    return {distance, pieces_[proche]};
}

std::vector<ChampDistance::PointSurface> ChampDistance::points_de_surface() const {
    std::vector<PointSurface> points;
    for (int k = 0; k < nz_; ++k) {
        for (int j = 0; j < ny_; ++j) {
            for (int i = 0; i < nx_; ++i) {
                const std::size_t cube = indice(i, j, k);
                if (surface_[cube]) {
                    points.push_back({origine_ + Vec3{(i + 0.5) * pas_, (j + 0.5) * pas_,
                                                      (k + 0.5) * pas_},
                                      pieces_[cube]});
                }
            }
        }
    }
    return points;
}

}  // namespace artprint
