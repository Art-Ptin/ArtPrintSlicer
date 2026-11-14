// ArtPrint Slicer - licence AGPL v3
#include "decoupe.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace artprint {

namespace {

using Indices = std::array<std::uint32_t, 3>;

Vec3 en_vec(const Point3& p) { return {p.x, p.y, p.z}; }
Point3 en_point(const Vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// Regroupe les sommets reliés entre eux (structure « union-find »).
class Groupes {
public:
    explicit Groupes(std::size_t taille) : parent_(taille) {
        std::iota(parent_.begin(), parent_.end(), 0u);
    }
    std::uint32_t racine(std::uint32_t i) {
        while (parent_[i] != i) {
            parent_[i] = parent_[parent_[i]];
            i = parent_[i];
        }
        return i;
    }
    void unir(std::uint32_t a, std::uint32_t b) { parent_[racine(a)] = racine(b); }

private:
    std::vector<std::uint32_t> parent_;
};

// Construit un maillage à partir de triangles qui partagent une liste de
// sommets : seuls les sommets utilisés sont gardés.
Maillage assembler(const std::vector<Vec3>& sommets, const std::vector<Indices>& triangles) {
    Maillage maillage;
    std::vector<std::uint32_t> nouveau(sommets.size(), std::numeric_limits<std::uint32_t>::max());
    maillage.triangles.reserve(triangles.size());
    for (const Indices& triangle : triangles) {
        Triangle sortie;
        for (int i = 0; i < 3; ++i) {
            std::uint32_t& indice = nouveau[triangle[i]];
            if (indice == std::numeric_limits<std::uint32_t>::max()) {
                indice = static_cast<std::uint32_t>(maillage.sommets.size());
                maillage.sommets.push_back(en_point(sommets[triangle[i]]));
            }
            sortie.sommets[i] = indice;
        }
        maillage.triangles.push_back(sortie);
    }
    return maillage;
}

}  // namespace

double distance_point_triangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    // Recherche du point du triangle le plus proche, région par région
    // (sommets, arêtes, intérieur).
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = produit_scalaire(ab, ap), d2 = produit_scalaire(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) {
        return norme(ap);
    }
    const Vec3 bp = p - b;
    const double d3 = produit_scalaire(ab, bp), d4 = produit_scalaire(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) {
        return norme(bp);
    }
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        return norme(p - (a + ab * (d1 / (d1 - d3))));
    }
    const Vec3 cp = p - c;
    const double d5 = produit_scalaire(ab, cp), d6 = produit_scalaire(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) {
        return norme(cp);
    }
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        return norme(p - (a + ac * (d2 / (d2 - d6))));
    }
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        return norme(p - (b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))));
    }
    const double somme = va + vb + vc;
    if (somme == 0.0) {
        return norme(ap);  // triangle aplati
    }
    return norme(p - (a + ab * (vb / somme) + ac * (vc / somme)));
}

double volume_maillage(const Maillage& maillage) {
    // Somme des volumes (avec signe) des pyramides entre l'origine et chaque triangle.
    double six_fois = 0.0;
    for (const Triangle& t : maillage.triangles) {
        const Vec3 a = en_vec(maillage.sommets[t.sommets[0]]);
        const Vec3 b = en_vec(maillage.sommets[t.sommets[1]]);
        const Vec3 c = en_vec(maillage.sommets[t.sommets[2]]);
        six_fois += produit_scalaire(a, produit_vectoriel(b, c));
    }
    return six_fois / 6.0;
}

void repere_autour(const Vec3& w, Vec3& u, Vec3& v) {
    // On part de l'axe du monde le moins aligné avec w.
    const Vec3 aide = std::abs(w.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
    u = normaliser(produit_vectoriel(aide, w));
    v = produit_vectoriel(w, u);
}

Maillage dans_repere(const Maillage& maillage, const Vec3& origine, const Vec3& u,
                     const Vec3& v, const Vec3& w) {
    Maillage sortie = maillage;
    for (Point3& sommet : sortie.sommets) {
        const Vec3 p = en_vec(sommet) - origine;
        sommet = en_point({produit_scalaire(p, u), produit_scalaire(p, v), produit_scalaire(p, w)});
    }
    return sortie;
}

std::optional<ResultatDecoupe> couper_par_plan(const Maillage& maillage, const Plan& plan,
                                               const std::vector<Vec3>& graines) {
    const Vec3 n = normaliser(plan.normale);
    if (norme(n) == 0.0 || maillage.triangles.empty() || graines.empty()) {
        return std::nullopt;
    }

    // Hauteur de chaque sommet au-dessus du plan. Un sommet posé exactement
    // sur le plan est compté « au-dessus » : aucun cas particulier ensuite.
    std::vector<Vec3> sommets;
    sommets.reserve(maillage.sommets.size() * 2);
    std::vector<double> hauteur;
    hauteur.reserve(maillage.sommets.size());
    for (const Point3& sommet : maillage.sommets) {
        sommets.push_back(en_vec(sommet));
        double h = produit_scalaire(sommets.back() - plan.point, n);
        if (std::abs(h) < 1e-6) {
            h = 1e-6;
        }
        hauteur.push_back(h);
    }

    // Point où une arête traverse le plan : calculé une seule fois par arête,
    // pour que les triangles voisins partagent exactement le même sommet.
    std::unordered_map<std::uint64_t, std::uint32_t> coupes;
    auto point_de_coupe = [&](std::uint32_t a, std::uint32_t b) {
        if (a > b) {
            std::swap(a, b);
        }
        const std::uint64_t cle = (static_cast<std::uint64_t>(a) << 32) | b;
        const auto trouve = coupes.find(cle);
        if (trouve != coupes.end()) {
            return trouve->second;
        }
        const double t = hauteur[a] / (hauteur[a] - hauteur[b]);
        const auto indice = static_cast<std::uint32_t>(sommets.size());
        sommets.push_back(sommets[a] + (sommets[b] - sommets[a]) * t);
        coupes.emplace(cle, indice);
        return indice;
    };

    // Un segment de la section : il longe un triangle du dessus.
    struct SegmentCoupe {
        std::uint32_t de;
        std::uint32_t vers;
        std::size_t triangle_dessus;
    };
    std::vector<Indices> dessus, dessous;
    std::vector<SegmentCoupe> segments;
    for (const Triangle& triangle : maillage.triangles) {
        const Indices& s = triangle.sommets;
        const bool haut[3] = {hauteur[s[0]] > 0.0, hauteur[s[1]] > 0.0, hauteur[s[2]] > 0.0};
        const int nombre = haut[0] + haut[1] + haut[2];
        if (nombre == 3) {
            dessus.push_back(s);
            continue;
        }
        if (nombre == 0) {
            dessous.push_back(s);
            continue;
        }
        // Le sommet seul de son côté : les deux arêtes qui en partent sont coupées.
        const bool seul_dessus = nombre == 1;
        int seul = 0;
        while (haut[seul] != seul_dessus) {
            ++seul;
        }
        const std::uint32_t a = s[seul], b = s[(seul + 1) % 3], c = s[(seul + 2) % 3];
        const std::uint32_t ab = point_de_coupe(a, b), ac = point_de_coupe(a, c);
        // L'ordre des sommets est gardé : l'orientation des triangles ne change pas.
        if (seul_dessus) {
            segments.push_back({ab, ac, dessus.size()});
            dessus.push_back({a, ab, ac});
            dessous.push_back({ab, b, c});
            dessous.push_back({ab, c, ac});
        } else {
            segments.push_back({ac, ab, dessus.size()});
            dessus.push_back({ab, b, c});
            dessus.push_back({ab, c, ac});
            dessous.push_back({a, ab, ac});
        }
    }
    if (dessus.empty() || dessous.empty()) {
        return std::nullopt;  // le plan ne traverse pas le modèle
    }

    // Morceaux du dessus : les triangles reliés par leurs sommets.
    Groupes groupes(sommets.size());
    for (const Indices& t : dessus) {
        groupes.unir(t[0], t[1]);
        groupes.unir(t[1], t[2]);
    }
    // Chaque graine choisit le morceau dont la surface est la plus proche.
    std::unordered_set<std::uint32_t> choisis;
    for (const Vec3& graine : graines) {
        double meilleure = std::numeric_limits<double>::max();
        std::uint32_t morceau = 0;
        for (const Indices& t : dessus) {
            const double d = distance_point_triangle(graine, sommets[t[0]], sommets[t[1]], sommets[t[2]]);
            if (d < meilleure) {
                meilleure = d;
                morceau = groupes.racine(t[0]);
            }
        }
        choisis.insert(morceau);
    }

    ResultatDecoupe resultat;
    std::vector<Indices> triangles_partie, triangles_reste = std::move(dessous);
    for (const Indices& t : dessus) {
        (choisis.count(groupes.racine(t[0])) ? triangles_partie : triangles_reste).push_back(t);
    }

    // La section : les segments des morceaux choisis, mis bout à bout en boucles.
    std::unordered_map<std::uint32_t, std::uint32_t> suivant;
    for (const SegmentCoupe& segment : segments) {
        if (choisis.count(groupes.racine(dessus[segment.triangle_dessus][0]))) {
            suivant[segment.de] = segment.vers;
        }
    }
    if (suivant.empty()) {
        return std::nullopt;  // le morceau choisi ne touche pas le plan
    }
    Vec3 somme_centres{};
    while (!suivant.empty()) {
        const std::uint32_t debut = suivant.begin()->first;
        std::vector<std::uint32_t> boucle;
        std::uint32_t ici = debut;
        for (;;) {
            const auto trouve = suivant.find(ici);
            if (trouve == suivant.end()) {
                return std::nullopt;  // section ouverte : le modèle n'est pas fermé
            }
            boucle.push_back(ici);
            ici = trouve->second;
            suivant.erase(trouve);
            if (ici == debut) {
                break;
            }
        }
        if (boucle.size() < 3) {
            return std::nullopt;
        }
        // Bouchon : un éventail de triangles autour du centre de la boucle.
        // Même pour une boucle biscornue, la surface comptée avec son signe
        // est juste, et les deux morceaux restent des volumes fermés.
        Vec3 centre{};
        for (const std::uint32_t i : boucle) {
            centre = centre + sommets[i];
        }
        centre = centre * (1.0 / static_cast<double>(boucle.size()));
        const auto indice_centre = static_cast<std::uint32_t>(sommets.size());
        sommets.push_back(centre);
        double surface_boucle = 0.0;
        for (std::size_t i = 0; i < boucle.size(); ++i) {
            const std::uint32_t a = boucle[i], b = boucle[(i + 1) % boucle.size()];
            triangles_partie.push_back({indice_centre, b, a});  // tourné vers le bas du morceau
            triangles_reste.push_back({indice_centre, a, b});   // tourné vers le haut du reste
            surface_boucle += 0.5 * produit_scalaire(
                                        produit_vectoriel(sommets[a] - centre, sommets[b] - centre), n);
        }
        resultat.surface_coupe += surface_boucle;
        somme_centres = somme_centres + centre * surface_boucle;
        ++resultat.boucles;
    }
    resultat.centre_coupe = std::abs(resultat.surface_coupe) > 1e-9
                                ? somme_centres * (1.0 / resultat.surface_coupe)
                                : plan.point;
    resultat.partie = assembler(sommets, triangles_partie);
    resultat.reste = assembler(sommets, triangles_reste);
    return resultat;
}

}  // namespace artprint
