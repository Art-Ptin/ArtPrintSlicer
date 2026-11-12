// ArtPrint Slicer - licence AGPL v3
#include "champ_couches.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>

#include "squelette.hpp"

namespace artprint {

namespace {

constexpr float hors_zone = -std::numeric_limits<float>::infinity();
// Marge de cubes vides autour du modèle, et épaisseur de la bande calculée
// autour de la matière (pour interpoler jusqu'à la surface et un peu au-delà).
constexpr int marge = 4;
constexpr int bande = 2;

}  // namespace

bool ChampCouches::defini(int i, int j, int k) const {
    return i >= 0 && j >= 0 && k >= 0 && i < nx && j < ny && k < nz &&
           valeurs[indice(i, j, k)] != hors_zone;
}

bool ChampCouches::valeur(const Vec3& p, double& resultat) const {
    const double gx = (p.x - origine.x) / pas - 0.5;
    const double gy = (p.y - origine.y) / pas - 0.5;
    const double gz = (p.z - origine.z) / pas - 0.5;
    const int i0 = static_cast<int>(std::floor(gx));
    const int j0 = static_cast<int>(std::floor(gy));
    const int k0 = static_cast<int>(std::floor(gz));
    const double fx = gx - i0, fy = gy - j0, fz = gz - k0;
    double somme = 0.0, poids = 0.0;
    for (int dk = 0; dk <= 1; ++dk) {
        for (int dj = 0; dj <= 1; ++dj) {
            for (int di = 0; di <= 1; ++di) {
                if (!defini(i0 + di, j0 + dj, k0 + dk)) {
                    continue;
                }
                const double w = (di ? fx : 1.0 - fx) * (dj ? fy : 1.0 - fy) * (dk ? fz : 1.0 - fz);
                somme += w * valeurs[indice(i0 + di, j0 + dj, k0 + dk)];
                poids += w;
            }
        }
    }
    // Il faut la plupart des huit cubes voisins : sinon l'interpolation
    // serait faussée par ceux qui manquent.
    if (poids < 0.85) {
        return false;
    }
    resultat = somme / poids;
    return true;
}

bool ChampCouches::gradient(const Vec3& p, Vec3& direction, double& pente) const {
    const double h = 0.75 * pas;
    Vec3 g;
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    double ici = 0.0;
    const bool centre = valeur(p, ici);
    for (int a = 0; a < 3; ++a) {
        double plus = 0.0, moins = 0.0;
        const bool a_plus = valeur(p + axes[a] * h, plus);
        const bool a_moins = valeur(p - axes[a] * h, moins);
        double d = 0.0;
        if (a_plus && a_moins) {
            d = (plus - moins) / (2.0 * h);
        } else if (a_plus && centre) {
            d = (plus - ici) / h;
        } else if (a_moins && centre) {
            d = (ici - moins) / h;
        } else {
            return false;
        }
        (a == 0 ? g.x : a == 1 ? g.y : g.z) = d;
    }
    pente = norme(g);
    if (pente < 1e-6) {
        return false;
    }
    direction = g * (1.0 / pente);
    return true;
}

ChampCouches calculer_champ_couches(const Maillage& pose, double pas) {
    ChampCouches champ;
    if (pose.triangles.empty()) {
        return champ;
    }
    if (pas <= 0.0) {
        const Point3 taille = calculer_boite(pose).dimensions();
        pas = std::clamp(static_cast<double>(std::max({taille.x, taille.y, taille.z})) / 200.0, 0.3, 1.2);
    }
    const GrilleVoxels g = remplir_voxels(pose, pas);
    champ.pas = pas;
    champ.nx = g.nx + 2 * marge, champ.ny = g.ny + 2 * marge, champ.nz = g.nz + 2 * marge;
    champ.origine = g.origine - Vec3{marge * pas, marge * pas, marge * pas};
    const int nx = champ.nx, ny = champ.ny, nz = champ.nz;
    const std::size_t total = static_cast<std::size_t>(nx) * ny * nz;
    const auto indice = [&](int i, int j, int k) { return champ.indice(i, j, k); };

    // 1. La zone calculée : la matière, élargie de quelques cubes.
    std::vector<std::uint8_t> matiere(total, 0), zone(total, 0);
    for (int k = 0; k < g.nz; ++k) {
        for (int j = 0; j < g.ny; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                if (g.plein[g.indice(i, j, k)]) {
                    matiere[indice(i + marge, j + marge, k + marge)] = 1;
                }
            }
        }
    }
    zone = matiere;
    // Élargissement axe par axe (un cube plein grossit en un pavé).
    for (int axe = 0; axe < 3; ++axe) {
        std::vector<std::uint8_t> suite(total, 0);
        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    bool plein = false;
                    for (int d = -bande; d <= bande && !plein; ++d) {
                        const int a = i + (axe == 0 ? d : 0), b = j + (axe == 1 ? d : 0), c = k + (axe == 2 ? d : 0);
                        plein = a >= 0 && b >= 0 && c >= 0 && a < nx && b < ny && c < nz && zone[indice(a, b, c)];
                    }
                    suite[indice(i, j, k)] = plein;
                }
            }
        }
        zone.swap(suite);
    }

    champ.valeurs.assign(total, hors_zone);
    const auto hauteur = [&](int k) { return champ.origine.z + (k + 0.5) * pas; };

    // 2. Les colonnes posées sur le plateau : tant que la colonne monte sans
    // quitter la zone, le chemin le plus court est la verticale.
    std::vector<std::uint8_t> direct(total, 0);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            int k = 0;
            while (k < nz && !zone[indice(i, j, k)]) {
                ++k;
            }
            if (k == nz || hauteur(k) > 1.5 * pas) {
                continue;  // cette colonne ne part pas du plateau
            }
            for (; k < nz && zone[indice(i, j, k)]; ++k) {
                direct[indice(i, j, k)] = 1;
                champ.valeurs[indice(i, j, k)] = static_cast<float>(hauteur(k));
            }
        }
    }

    // 3. Le reste (ce qui est en surplomb) : le chemin part d'un cube déjà
    // connu, en ligne droite tant que la ligne reste dans la zone. Chaque
    // cube retient sa « source » : le point d'où part sa dernière ligne droite.
    struct Voisin {
        int di, dj, dk;
    };
    std::vector<Voisin> voisins;
    for (int dk = -1; dk <= 1; ++dk) {
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di || dj || dk) {
                    voisins.push_back({di, dj, dk});
                }
            }
        }
    }
    const auto coordonnees = [&](std::uint32_t n, int& i, int& j, int& k) {
        i = static_cast<int>(n % nx);
        j = static_cast<int>((n / nx) % ny);
        k = static_cast<int>(n / (static_cast<std::size_t>(nx) * ny));
    };
    // Vrai si la ligne droite entre deux cubes reste dans la zone.
    const auto visible = [&](int i0, int j0, int k0, int i1, int j1, int k1) {
        const int di = i1 - i0, dj = j1 - j0, dk = k1 - k0;
        const int pas_ligne = 2 * std::max({std::abs(di), std::abs(dj), std::abs(dk)});
        for (int s = 1; s < pas_ligne; ++s) {
            const double t = static_cast<double>(s) / pas_ligne;
            const int i = static_cast<int>(std::lround(i0 + di * t));
            const int j = static_cast<int>(std::lround(j0 + dj * t));
            const int k = static_cast<int>(std::lround(k0 + dk * t));
            if (!zone[indice(i, j, k)]) {
                return false;
            }
        }
        return true;
    };

    std::vector<std::uint32_t> source(total, 0);
    std::vector<std::uint8_t> fini(total, 0);
    using Entree = std::pair<float, std::uint32_t>;
    std::priority_queue<Entree, std::vector<Entree>, std::greater<Entree>> file;
    const float infini = std::numeric_limits<float>::max();
    std::vector<float> longueur(total, infini);
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const std::size_t n = indice(i, j, k);
                if (!direct[n]) {
                    continue;
                }
                longueur[n] = champ.valeurs[n];
                source[n] = static_cast<std::uint32_t>(n);
                fini[n] = 1;
                // Seuls les cubes au bord d'un surplomb servent de départ.
                for (const Voisin& v : voisins) {
                    const int a = i + v.di, b = j + v.dj, c = k + v.dk;
                    if (a >= 0 && b >= 0 && c >= 0 && a < nx && b < ny && c < nz && zone[indice(a, b, c)] &&
                        !direct[indice(a, b, c)]) {
                        file.push({longueur[n], static_cast<std::uint32_t>(n)});
                        break;
                    }
                }
            }
        }
    }
    while (!file.empty()) {
        const auto [d, n] = file.top();
        file.pop();
        if (d > longueur[n]) {
            continue;
        }
        fini[n] = 1;
        int i, j, k;
        coordonnees(n, i, j, k);
        const std::uint32_t s = source[n];
        int si, sj, sk;
        coordonnees(s, si, sj, sk);
        for (const Voisin& v : voisins) {
            const int a = i + v.di, b = j + v.dj, c = k + v.dk;
            if (a < 0 || b < 0 || c < 0 || a >= nx || b >= ny || c >= nz) {
                continue;
            }
            const std::size_t m = indice(a, b, c);
            if (!zone[m] || direct[m] || fini[m] || longueur[m] <= d) {
                continue;
            }
            // En ligne droite depuis la source de ce cube, si elle est visible...
            float candidat;
            std::uint32_t origine_candidat;
            if (s != n && visible(a, b, c, si, sj, sk)) {
                candidat = longueur[s] + static_cast<float>(pas * std::sqrt(static_cast<double>(
                    (a - si) * (a - si) + (b - sj) * (b - sj) + (c - sk) * (c - sk))));
                origine_candidat = s;
            } else {
                // ... sinon le chemin tourne ici : ce cube devient la source.
                candidat = d + static_cast<float>(pas * std::sqrt(static_cast<double>(
                    v.di * v.di + v.dj * v.dj + v.dk * v.dk)));
                origine_candidat = n;
            }
            if (candidat < longueur[m]) {
                longueur[m] = candidat;
                source[m] = origine_candidat;
                file.push({candidat, static_cast<std::uint32_t>(m)});
            }
        }
    }
    for (std::size_t n = 0; n < total; ++n) {
        if (zone[n] && longueur[n] != infini) {
            champ.valeurs[n] = longueur[n];
            if (matiere[n]) {
                champ.maximum = std::max(champ.maximum, static_cast<double>(longueur[n]));
            }
        }
    }
    return champ;
}

}  // namespace artprint
