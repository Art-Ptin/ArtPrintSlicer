// ArtPrint Slicer - licence AGPL v3
#include "couches_courbes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <unordered_map>

#include "decoupe.hpp"
#include "slicer.hpp"

namespace artprint {

namespace {

using Face = std::array<std::uint32_t, 3>;

std::uint64_t cle_arete(std::uint32_t a, std::uint32_t b) {
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
}

// Recoupe les triangles dont une arête dépasse « longueur_max » : le champ
// n'est pas droit le long d'une grande arête, il faut des petits triangles
// pour le suivre. Une arête est coupée de la même façon dans les deux
// triangles qui la partagent : le maillage reste fermé.
void raffiner(std::vector<Vec3>& sommets, std::vector<Face>& faces, double longueur_max) {
    for (int passe = 0; passe < 24; ++passe) {
        std::unordered_map<std::uint64_t, std::uint32_t> milieux;
        const auto milieu = [&](std::uint32_t a, std::uint32_t b) -> std::int64_t {
            if (norme(sommets[a] - sommets[b]) <= longueur_max) {
                return -1;
            }
            const auto [trouve, nouveau] = milieux.try_emplace(cle_arete(a, b), 0);
            if (nouveau) {
                trouve->second = static_cast<std::uint32_t>(sommets.size());
                sommets.push_back((sommets[a] + sommets[b]) * 0.5);
            }
            return trouve->second;
        };
        std::vector<Face> suite;
        suite.reserve(faces.size() * 2);
        bool change = false;
        for (const Face& f : faces) {
            const std::int64_t m[3] = {milieu(f[0], f[1]), milieu(f[1], f[2]), milieu(f[2], f[0])};
            const int nombre = (m[0] >= 0) + (m[1] >= 0) + (m[2] >= 0);
            if (nombre == 0) {
                suite.push_back(f);
                continue;
            }
            change = true;
            const auto s = [](std::int64_t valeur) { return static_cast<std::uint32_t>(valeur); };
            if (nombre == 3) {
                suite.push_back({f[0], s(m[0]), s(m[2])});
                suite.push_back({s(m[0]), f[1], s(m[1])});
                suite.push_back({s(m[2]), s(m[1]), f[2]});
                suite.push_back({s(m[0]), s(m[1]), s(m[2])});
                continue;
            }
            // Une ou deux arêtes coupées : on tourne le triangle pour que la
            // première arête coupée soit (a, b).
            int depart = 0;
            if (nombre == 1) {
                depart = m[0] >= 0 ? 0 : m[1] >= 0 ? 1 : 2;
            } else {
                depart = m[0] < 0 ? 1 : m[1] < 0 ? 2 : 0;  // l'arête non coupée est juste avant
            }
            const std::uint32_t a = f[depart], b = f[(depart + 1) % 3], c = f[(depart + 2) % 3];
            const std::uint32_t mab = s(m[depart]);
            if (nombre == 1) {
                suite.push_back({a, mab, c});
                suite.push_back({mab, b, c});
            } else {
                const std::uint32_t mbc = s(m[(depart + 1) % 3]);
                suite.push_back({mab, b, mbc});
                suite.push_back({a, mab, mbc});
                suite.push_back({a, mbc, c});
            }
        }
        faces.swap(suite);
        if (!change) {
            break;
        }
    }
}

// Lignes où la surface du maillage a exactement la valeur « niveau » : des
// boucles fermées dans l'espace.
std::vector<std::vector<Vec3>> boucles_de_niveau(const std::vector<Vec3>& sommets,
                                                 const std::vector<Face>& faces,
                                                 const std::vector<double>& valeurs,
                                                 const std::vector<std::array<float, 2>>& bornes,
                                                 double niveau) {
    struct Segment {
        std::uint64_t cles[2];
        bool vu = false;
    };
    std::vector<Segment> segments;
    std::unordered_map<std::uint64_t, std::array<int, 2>> voisins;
    std::unordered_map<std::uint64_t, Vec3> points;
    for (std::size_t n = 0; n < faces.size(); ++n) {
        if (niveau < bornes[n][0] || niveau > bornes[n][1]) {
            continue;
        }
        const Face& f = faces[n];
        const bool dessus[3] = {valeurs[f[0]] >= niveau, valeurs[f[1]] >= niveau, valeurs[f[2]] >= niveau};
        Segment segment{};
        int trouves = 0;
        for (int e = 0; e < 3 && trouves < 2; ++e) {
            const std::uint32_t a = f[e], b = f[(e + 1) % 3];
            if (dessus[e] == dessus[(e + 1) % 3]) {
                continue;
            }
            const std::uint64_t cle = cle_arete(a, b);
            segment.cles[trouves++] = cle;
            if (!points.contains(cle)) {
                const double t = std::clamp((niveau - valeurs[a]) / (valeurs[b] - valeurs[a]), 0.0, 1.0);
                points.emplace(cle, sommets[a] + (sommets[b] - sommets[a]) * t);
            }
        }
        if (trouves != 2) {
            continue;
        }
        const int numero = static_cast<int>(segments.size());
        for (const std::uint64_t cle : segment.cles) {
            auto [trouve, nouveau] = voisins.try_emplace(cle, std::array<int, 2>{numero, -1});
            if (!nouveau) {
                trouve->second[1] = numero;
            }
        }
        segments.push_back(segment);
    }
    std::vector<std::vector<Vec3>> boucles;
    for (std::size_t depart = 0; depart < segments.size(); ++depart) {
        if (segments[depart].vu) {
            continue;
        }
        std::vector<Vec3> boucle;
        int courant = static_cast<int>(depart);
        std::uint64_t cle = segments[depart].cles[0];
        bool fermee = false;
        while (courant >= 0 && !segments[courant].vu) {
            segments[courant].vu = true;
            boucle.push_back(points.at(cle));
            // On sort du segment par son autre arête, puis on passe au triangle voisin.
            cle = segments[courant].cles[0] == cle ? segments[courant].cles[1] : segments[courant].cles[0];
            const std::array<int, 2>& suivants = voisins.at(cle);
            const int suivant = suivants[0] == courant ? suivants[1] : suivants[0];
            if (suivant == static_cast<int>(depart)) {
                fermee = true;
            }
            courant = suivant;
        }
        if (fermee && boucle.size() >= 3) {
            boucles.push_back(std::move(boucle));
        }
    }
    return boucles;
}

}  // namespace

bool poser_sur_la_surface(const ChampCouches& champ, const CoucheCourbe& couche, const Point2& p,
                          double niveau, double& hauteur, Vec3& point) {
    const Vec3 pied = couche.origine + couche.u * p.x + couche.v * p.y;
    double s = hauteur;
    double f = 0.0;
    // Si le départ est hors de la zone calculée, on cherche le long de w un
    // endroit où le champ est connu.
    if (!champ.valeur(pied + couche.w * s, f)) {
        bool trouve = false;
        for (int n = 1; n <= 80 && !trouve; ++n) {
            for (const double essai : {s + n * champ.pas, s - n * champ.pas}) {
                if (champ.valeur(pied + couche.w * essai, f)) {
                    s = essai;
                    trouve = true;
                    break;
                }
            }
        }
        if (!trouve) {
            return false;
        }
    }
    double connu = s;          // dernière hauteur où le champ était connu
    double meilleur = s, plus_petit = 1e9;
    for (int iteration = 0; iteration < 60; ++iteration) {
        const Vec3 ici = pied + couche.w * s;
        if (!champ.valeur(ici, f)) {
            // Sorti de la zone : on revient à mi-chemin du dernier point connu.
            if (std::abs(s - connu) < 1e-5) {
                break;
            }
            s = 0.5 * (s + connu);
            continue;
        }
        connu = s;
        const double ecart = niveau - f;
        if (std::abs(ecart) < plus_petit) {
            plus_petit = std::abs(ecart);
            meilleur = s;
        }
        if (std::abs(ecart) < 2e-4) {
            break;
        }
        Vec3 direction;
        double pente = 1.0;
        double montee = 1.0;
        if (champ.gradient(ici, direction, pente)) {
            montee = pente * produit_scalaire(direction, couche.w);
        }
        s += std::clamp(ecart / std::max(montee, 0.2), -0.6, 0.6);
    }
    // Au bord de la matière, la surface peut passer juste hors de la zone
    // calculée : le point connu le plus proche convient s'il est à quelques
    // centièmes de millimètre.
    if (plus_petit > 0.05) {
        return false;
    }
    hauteur = meilleur;
    point = pied + couche.w * meilleur;
    return true;
}

bool poser_sur_la_surface(const ChampCouches& champ, const CoucheCourbe& couche, const Point2& p,
                          double niveau, Vec3& point) {
    double hauteur = 0.0;
    return poser_sur_la_surface(champ, couche, p, niveau, hauteur, point);
}

std::vector<CoucheCourbe> trancher_selon_champ(const Maillage& partie, const ChampCouches& champ,
                                               const std::vector<double>& dessus,
                                               const std::vector<double>& epaisseurs,
                                               std::size_t numero) {
    std::vector<Vec3> sommets;
    sommets.reserve(partie.sommets.size());
    for (const Point3& p : partie.sommets) {
        sommets.push_back({p.x, p.y, p.z});
    }
    std::vector<Face> faces;
    faces.reserve(partie.triangles.size());
    for (const Triangle& t : partie.triangles) {
        faces.push_back(t.sommets);
    }
    raffiner(sommets, faces, std::max(0.8, 2.0 * champ.pas));

    // Valeur du champ en chaque sommet. Un sommet juste hors de la zone
    // calculée prend la valeur du point connu le plus proche.
    std::vector<double> valeurs(sommets.size(), 0.0);
    for (std::size_t n = 0; n < sommets.size(); ++n) {
        if (champ.valeur(sommets[n], valeurs[n])) {
            continue;
        }
        bool trouve = false;
        for (double rayon = 0.5 * champ.pas; rayon <= 3.0 * champ.pas && !trouve; rayon += 0.5 * champ.pas) {
            for (const Vec3& d : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0}, Vec3{0, 0, 1},
                                  Vec3{0, 0, -1}}) {
                if (champ.valeur(sommets[n] + d * rayon, valeurs[n])) {
                    trouve = true;
                    break;
                }
            }
        }
        if (!trouve) {
            throw ErreurTranchage(std::format(
                "La partie {} sort de la zone où les couches courbes sont calculées.", numero));
        }
    }
    std::vector<std::array<float, 2>> bornes(faces.size());
    for (std::size_t n = 0; n < faces.size(); ++n) {
        const Face& f = faces[n];
        bornes[n] = {static_cast<float>(std::min({valeurs[f[0]], valeurs[f[1]], valeurs[f[2]]}) - 1e-4),
                     static_cast<float>(std::max({valeurs[f[0]], valeurs[f[1]], valeurs[f[2]]}) + 1e-4)};
    }

    // Pour chaque niveau : les boucles, leur centre et la direction moyenne.
    struct Brut {
        std::size_t niveau;
        std::vector<std::vector<Vec3>> boucles;
        Vec3 centre, direction;
    };
    std::vector<Brut> bruts;
    for (std::size_t k = 0; k < dessus.size(); ++k) {
        Brut brut;
        brut.niveau = k;
        brut.boucles = boucles_de_niveau(sommets, faces, valeurs, bornes, dessus[k] - epaisseurs[k] / 2.0);
        if (brut.boucles.empty()) {
            if (!bruts.empty() && k + 1 < dessus.size()) {
                // Un trou au milieu de la partie est permis s'il n'y a plus rien au-dessus.
                bool suite = false;
                for (std::size_t j = k + 1; j < dessus.size() && !suite; ++j) {
                    suite = !boucles_de_niveau(sommets, faces, valeurs, bornes, dessus[j] - epaisseurs[j] / 2.0).empty();
                }
                if (suite) {
                    throw ErreurTranchage(std::format(
                        "La couche {} de la partie {} est vide : un morceau flotte en l'air et ne "
                        "peut pas être imprimé.", k + 1, numero));
                }
                break;
            }
            continue;
        }
        double longueur = 0.0;
        Vec3 somme, direction;
        for (const std::vector<Vec3>& boucle : brut.boucles) {
            for (std::size_t i = 0; i < boucle.size(); ++i) {
                const Vec3& a = boucle[i];
                const Vec3& b = boucle[(i + 1) % boucle.size()];
                const double l = norme(b - a);
                const Vec3 milieu = (a + b) * 0.5;
                somme = somme + milieu * l;
                longueur += l;
                Vec3 d;
                double pente;
                if (champ.gradient(milieu, d, pente)) {
                    direction = direction + d * l;
                }
            }
        }
        if (longueur < 1e-6 || norme(direction) < 1e-6) {
            continue;
        }
        brut.centre = somme * (1.0 / longueur);
        brut.direction = normaliser(direction);
        bruts.push_back(std::move(brut));
    }

    // La direction des cartes est adoucie d'une couche à l'autre.
    for (int passe = 0; passe < 4; ++passe) {
        std::vector<Vec3> adoucies(bruts.size());
        for (std::size_t n = 0; n < bruts.size(); ++n) {
            const Vec3& avant = bruts[n == 0 ? 0 : n - 1].direction;
            const Vec3& apres = bruts[n + 1 < bruts.size() ? n + 1 : n].direction;
            adoucies[n] = normaliser(avant + bruts[n].direction * 2.0 + apres);
        }
        for (std::size_t n = 0; n < bruts.size(); ++n) {
            bruts[n].direction = adoucies[n];
        }
    }

    std::vector<CoucheCourbe> couches;
    Vec3 u_avant{1, 0, 0};
    bool premiere = true;
    for (Brut& brut : bruts) {
        CoucheCourbe couche;
        couche.niveau = brut.niveau;
        couche.origine = brut.centre;
        couche.w = brut.direction;
        if (couche.w.z > std::cos(en_radians(0.5))) {
            couche.w = {0, 0, 1};  // pratiquement horizontale : carte vue du dessus
        }
        if (premiere && couche.w.z < 1.0 - 1e-12) {
            repere_autour(couche.w, couche.u, couche.v);
        } else {
            // La carte glisse depuis celle du dessous, sans vriller.
            couche.u = normaliser(u_avant - couche.w * produit_scalaire(u_avant, couche.w));
            couche.v = produit_vectoriel(couche.w, couche.u);
        }
        premiere = false;
        u_avant = couche.u;

        Polygones dessin;
        double cosinus = 1.0;
        for (const std::vector<Vec3>& boucle : brut.boucles) {
            Contour contour;
            contour.reserve(boucle.size());
            for (const Vec3& p : boucle) {
                const Vec3 r = p - couche.origine;
                contour.push_back(en_unites(Point2{produit_scalaire(r, couche.u), produit_scalaire(r, couche.v)}));
                Vec3 d;
                double pente;
                if (champ.gradient(p, d, pente)) {
                    cosinus = std::min(cosinus, produit_scalaire(d, couche.w));
                }
            }
            dessin.push_back(std::move(contour));
        }
        couche.bombement = en_degres(std::acos(std::clamp(cosinus, -1.0, 1.0)));
        if (couche.bombement > 72.0) {
            throw ErreurTranchage(std::format(
                "La partie {} est trop bombée pour être imprimée en couches courbes : une couche "
                "tourne de {:.0f}° d'un bord à l'autre.", numero, couche.bombement));
        }
        couche.contour = Clipper2Lib::SimplifyPaths(
            Clipper2Lib::Union(dessin, Clipper2Lib::FillRule::EvenOdd), 0.005 * unites_par_mm);
        if (!couche.contour.empty()) {
            couches.push_back(std::move(couche));
        }
    }
    if (couches.empty()) {
        throw ErreurTranchage(std::format("La partie {} est plus fine qu'une couche : rien à imprimer.", numero));
    }
    return couches;
}

}  // namespace artprint
