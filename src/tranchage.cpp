// ArtPrint Slicer - licence AGPL v3
//
// Principe : un triangle traversé par un plan horizontal donne un petit
// segment. Ce segment va d'une arête du triangle à une autre. Comme chaque
// arête est partagée par deux triangles, les segments s'enchaînent d'eux-mêmes
// d'arête en arête et forment des contours fermés, sans aucune comparaison
// de coordonnées (donc sans problème d'arrondi).
#include "tranchage.hpp"

#include <algorithm>
#include <unordered_map>

namespace artprint {

namespace {

using CleArete = std::uint64_t;

CleArete cle_arete(std::uint32_t a, std::uint32_t b) {
    if (a > b) {
        std::swap(a, b);
    }
    return (static_cast<CleArete>(a) << 32) | b;
}

// Un segment de coupe : il part du point situé sur l'arête « depart » et
// arrive sur l'arête « arrivee ».
struct Segment {
    CleArete depart;
    CleArete arrivee;
    PointEntier point_depart;
};

// Point où l'arête (a, b) traverse le plan. Les sommets sont toujours pris
// dans le même ordre : les deux triangles voisins trouvent le même point.
PointEntier point_de_coupe(const Maillage& m, std::uint32_t a, std::uint32_t b, double z) {
    if (a > b) {
        std::swap(a, b);
    }
    const Point3& pa = m.sommets[a];
    const Point3& pb = m.sommets[b];
    const double t = (z - pa.z) / (static_cast<double>(pb.z) - pa.z);
    return en_unites(Point2{pa.x + t * (static_cast<double>(pb.x) - pa.x),
                            pa.y + t * (static_cast<double>(pb.y) - pa.y)});
}

}  // namespace

ResultatCoupe couper_maillage(const Maillage& maillage, const std::vector<double>& hauteurs) {
    ResultatCoupe resultat;
    resultat.couches.resize(hauteurs.size());
    std::vector<std::vector<Segment>> segments(hauteurs.size());

    for (const Triangle& triangle : maillage.triangles) {
        const std::uint32_t* s = triangle.sommets.data();
        const Point3 p[3] = {maillage.sommets[s[0]], maillage.sommets[s[1]],
                             maillage.sommets[s[2]]};
        const double z_min = std::min({p[0].z, p[1].z, p[2].z});
        const double z_max = std::max({p[0].z, p[1].z, p[2].z});

        auto premier = std::lower_bound(hauteurs.begin(), hauteurs.end(), z_min);
        for (auto it = premier; it != hauteurs.end() && *it <= z_max; ++it) {
            const double z = *it;
            // Un sommet exactement sur le plan compte comme « au-dessus ».
            const bool dessus[3] = {p[0].z >= z, p[1].z >= z, p[2].z >= z};
            const int nombre = dessus[0] + dessus[1] + dessus[2];
            if (nombre == 0 || nombre == 3) {
                continue;
            }
            // Le sommet seul de son côté : les deux arêtes qui en partent
            // traversent le plan.
            const bool seul_dessus = nombre == 1;
            int seul = 0;
            while (dessus[seul] != seul_dessus) {
                ++seul;
            }
            const std::uint32_t a = s[seul], b = s[(seul + 1) % 3], c = s[(seul + 2) % 3];
            PointEntier p1 = point_de_coupe(maillage, a, b, z);
            PointEntier p2 = point_de_coupe(maillage, a, c, z);
            CleArete k1 = cle_arete(a, b);
            CleArete k2 = cle_arete(a, c);

            // Sens du segment : la matière doit être à sa gauche. Avec des
            // sommets rangés dans le sens inverse des aiguilles d'une montre
            // (vu de l'extérieur), c'est le cas quand le sommet seul est
            // au-dessus du plan ; sinon il faut retourner le segment. Cette
            // règle ne dépend d'aucun calcul : elle reste juste même quand
            // le segment est minuscule.
            if (!seul_dessus) {
                std::swap(p1, p2);
                std::swap(k1, k2);
            }
            segments[static_cast<std::size_t>(it - hauteurs.begin())].push_back({k1, k2, p1});
        }
    }

    for (std::size_t couche = 0; couche < hauteurs.size(); ++couche) {
        // Pour chaque arête de départ : le segment qui en part.
        std::unordered_map<CleArete, const Segment*> suivant;
        suivant.reserve(segments[couche].size() * 2);
        for (const Segment& segment : segments[couche]) {
            suivant[segment.depart] = &segment;
        }

        Polygones contours;
        while (!suivant.empty()) {
            const CleArete debut = suivant.begin()->first;
            Contour contour;
            CleArete cle = debut;
            bool ferme = false;
            for (;;) {
                const auto trouve = suivant.find(cle);
                if (trouve == suivant.end()) {
                    break;  // chaîne interrompue : modèle abîmé
                }
                const Segment* segment = trouve->second;
                suivant.erase(trouve);
                contour.push_back(segment->point_depart);
                cle = segment->arrivee;
                if (cle == debut) {
                    ferme = true;
                    break;
                }
            }
            if (ferme && contour.size() >= 3) {
                contours.push_back(std::move(contour));
            } else if (!ferme) {
                ++resultat.contours_ouverts;
            }
        }
        // Réunit les contours : les trous sont retirés, les volumes qui se
        // chevauchent sont fusionnés.
        resultat.couches[couche] =
            Clipper2Lib::Union(contours, Clipper2Lib::FillRule::Positive);
    }
    return resultat;
}

}  // namespace artprint
