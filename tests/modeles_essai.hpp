// ArtPrint Slicer - licence AGPL v3
// Modèles fabriqués pour les tests du 5 axes : cylindres et petit arbre.
#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

#include "decoupe.hpp"
#include "geometrie.hpp"
#include "maillage.hpp"

namespace essai {

using namespace artprint;

inline Point3 point(const Vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// Ajoute un cylindre fermé (avec ses deux fonds) entre « bas » et « haut ».
inline void ajouter_cylindre(ConstructeurMaillage& modele, const Vec3& bas, const Vec3& haut,
                             double rayon, int cotes = 32) {
    const Vec3 axe = normaliser(haut - bas);
    Vec3 u, v;
    repere_autour(axe, u, v);
    const auto bord = [&](const Vec3& centre, int i) {
        const double angle = 2.0 * std::numbers::pi * (i % cotes) / cotes;  // le dernier point rejoint exactement le premier
        return centre + u * (rayon * std::cos(angle)) + v * (rayon * std::sin(angle));
    };
    for (int i = 0; i < cotes; ++i) {
        const Vec3 b0 = bord(bas, i), b1 = bord(bas, i + 1), h0 = bord(haut, i), h1 = bord(haut, i + 1);
        modele.ajouter_triangle(point(b0), point(b1), point(h1));  // côté
        modele.ajouter_triangle(point(b0), point(h1), point(h0));
        modele.ajouter_triangle(point(bas), point(b1), point(b0));   // fond du bas
        modele.ajouter_triangle(point(haut), point(h0), point(h1));  // fond du haut
    }
}

// Direction donnée par son inclinaison (0 = verticale) et son angle vu de dessus, en degrés.
inline Vec3 direction(double inclinaison, double azimut) {
    const double i = en_radians(inclinaison), a = en_radians(azimut);
    return {std::sin(i) * std::cos(a), std::sin(i) * std::sin(a), std::cos(i)};
}

// Un petit arbre : un tronc vertical et deux branches qui partent de côté.
// Chaque morceau est un volume fermé ; ils se chevauchent là où ils se touchent.
inline Maillage arbre() {
    ConstructeurMaillage modele;
    ajouter_cylindre(modele, {0, 0, 0}, {0, 0, 60}, 6.0);
    const Vec3 depart_a{0, 0, 22}, depart_b{0, 0, 38};
    ajouter_cylindre(modele, depart_a, depart_a + direction(60.0, 0.0) * 34.0, 3.5);
    ajouter_cylindre(modele, depart_b, depart_b + direction(75.0, 140.0) * 30.0, 3.0);
    return modele.terminer();
}

// Un arbre « à plat » d'une seule pièce : la silhouette d'un tronc et de deux
// branches, découpée dans une plaque de 10 mm d'épaisseur. Contrairement à
// arbre(), c'est un seul volume fermé, comme un vrai fichier de modèle.
inline Maillage arbre_plat() {
    struct P {
        double x, z;
    };
    // Contour dans le plan (x, z), dans le sens inverse des aiguilles d'une montre.
    std::vector<P> contour = {{-5, 0},        {5, 0},          {5, 18},        {30.98, 33},
                              {27.48, 39.06}, {5, 26.08},      {5, 50},        {-5, 50},
                              {-5, 36.39},    {-26.45, 44.19}, {-28.5, 38.55}, {-5, 30}};
    const double devant = -5.0, derriere = 5.0;
    ConstructeurMaillage modele;
    const auto p3 = [](const P& p, double y) {
        return Point3{static_cast<float>(p.x), static_cast<float>(y), static_cast<float>(p.z)};
    };
    // Les côtés : un rectangle par segment du contour.
    for (std::size_t i = 0; i < contour.size(); ++i) {
        const P& a = contour[i];
        const P& b = contour[(i + 1) % contour.size()];
        modele.ajouter_triangle(p3(a, devant), p3(b, devant), p3(b, derriere));
        modele.ajouter_triangle(p3(a, devant), p3(b, derriere), p3(a, derriere));
    }
    // Les deux faces : le contour est découpé en triangles en retirant une
    // « oreille » (un coin saillant sans autre point dedans) à chaque tour.
    const auto aire2 = [](const P& a, const P& b, const P& c) {
        return (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
    };
    std::vector<P> reste = contour;
    while (reste.size() >= 3) {
        const std::size_t n = reste.size();
        bool trouve = false;
        for (std::size_t i = 0; i < n && !trouve; ++i) {
            const P& a = reste[(i + n - 1) % n];
            const P& b = reste[i];
            const P& c = reste[(i + 1) % n];
            if (aire2(a, b, c) < -1e-9) {
                continue;  // coin rentrant
            }
            bool vide = true;
            for (std::size_t k = 0; k < n && vide; ++k) {
                if (k == i || k == (i + n - 1) % n || k == (i + 1) % n) {
                    continue;
                }
                const P& q = reste[k];
                vide = !(aire2(a, b, q) > 0 && aire2(b, c, q) > 0 && aire2(c, a, q) > 0);
            }
            if (!vide) {
                continue;
            }
            modele.ajouter_triangle(p3(a, devant), p3(c, devant), p3(b, devant));       // face avant
            modele.ajouter_triangle(p3(a, derriere), p3(b, derriere), p3(c, derriere)); // face arrière
            reste.erase(reste.begin() + static_cast<std::ptrdiff_t>(i));
            trouve = true;
        }
        if (!trouve) {
            break;
        }
    }
    Maillage maillage = modele.terminer();
    // Les faces doivent regarder vers l'extérieur : sinon on les retourne toutes.
    if (volume_maillage(maillage) < 0.0) {
        for (Triangle& t : maillage.triangles) {
            std::swap(t.sommets[1], t.sommets[2]);
        }
    }
    return maillage;
}

}  // namespace essai
