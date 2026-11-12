// ArtPrint Slicer - licence AGPL v3
#include "peau_courbe.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

namespace artprint {

namespace {

Vec3 en_vec(const Point3& p) { return {p.x, p.y, p.z}; }

// Hauteur du plan du triangle à l'aplomb de (x, y), si ce point est dans le
// triangle vu de dessus (avec une petite tolérance sur les bords).
bool hauteur_dans_le_triangle(const Vec3& a, const Vec3& b, const Vec3& c, double x, double y, double& z) {
    const double d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(d) < 1e-12) {
        return false;  // triangle vu par la tranche
    }
    const double l1 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / d;
    const double l2 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / d;
    const double l3 = 1.0 - l1 - l2;
    const double tolerance = -1e-6;
    if (l1 < tolerance || l2 < tolerance || l3 < tolerance) {
        return false;
    }
    z = l1 * a.z + l2 * b.z + l3 * c.z;
    return true;
}

}  // namespace

// Tous les triangles du modèle, rangés par cases vues de dessus.
struct RangementPeau {
    struct Triangle3 {
        Vec3 a, b, c;
    };
    std::vector<Triangle3> triangles;
    double cote = 2.0, x0 = 0.0, y0 = 0.0;
    int nx = 1, ny = 1;
    std::vector<std::vector<std::uint32_t>> cases;

    explicit RangementPeau(const std::vector<const Maillage*>& maillages) {
        double x_min = 1e18, y_min = 1e18, x_max = -1e18, y_max = -1e18;
        for (const Maillage* m : maillages) {
            for (const Triangle& t : m->triangles) {
                triangles.push_back({en_vec(m->sommets[t.sommets[0]]), en_vec(m->sommets[t.sommets[1]]),
                                     en_vec(m->sommets[t.sommets[2]])});
                for (const Vec3& p : {triangles.back().a, triangles.back().b, triangles.back().c}) {
                    x_min = std::min(x_min, p.x), x_max = std::max(x_max, p.x);
                    y_min = std::min(y_min, p.y), y_max = std::max(y_max, p.y);
                }
            }
        }
        x0 = x_min, y0 = y_min;
        nx = std::max(1, static_cast<int>((x_max - x_min) / cote) + 1);
        ny = std::max(1, static_cast<int>((y_max - y_min) / cote) + 1);
        cases.resize(static_cast<std::size_t>(nx) * ny);
        for (std::uint32_t n = 0; n < triangles.size(); ++n) {
            const Triangle3& t = triangles[n];
            const int i0 = colonne(std::min({t.a.x, t.b.x, t.c.x})), i1 = colonne(std::max({t.a.x, t.b.x, t.c.x}));
            const int j0 = ligne(std::min({t.a.y, t.b.y, t.c.y})), j1 = ligne(std::max({t.a.y, t.b.y, t.c.y}));
            for (int j = j0; j <= j1; ++j) {
                for (int i = i0; i <= i1; ++i) {
                    cases[static_cast<std::size_t>(j) * nx + i].push_back(n);
                }
            }
        }
    }
    int colonne(double x) const { return std::clamp(static_cast<int>((x - x0) / cote), 0, nx - 1); }
    int ligne(double y) const { return std::clamp(static_cast<int>((y - y0) / cote), 0, ny - 1); }

    // Surface la plus proche au-dessus (sens = +1) ou au-dessous (sens = -1)
    // du point, à sa verticale. Rend « faux » s'il n'y en a pas.
    bool surface_voisine(const Vec3& p, int sens, double ecart_min, double& z_trouve) const {
        bool trouve = false;
        for (const std::uint32_t n : cases[static_cast<std::size_t>(ligne(p.y)) * nx + colonne(p.x)]) {
            const Triangle3& t = triangles[n];
            double z = 0.0;
            if (!hauteur_dans_le_triangle(t.a, t.b, t.c, p.x, p.y, z) || sens * (z - p.z) < ecart_min) {
                continue;
            }
            if (!trouve || sens * (z - z_trouve) < 0.0) {
                z_trouve = z;
                trouve = true;
            }
        }
        return trouve;
    }

    // Vrai si le point est dans la matière : en montant à la verticale, on
    // traverse la surface un nombre impair de fois.
    bool dedans(const Vec3& p) const {
        if (p.x < x0 || p.y < y0 || p.x > x0 + nx * cote || p.y > y0 + ny * cote) {
            return false;
        }
        int traversees = 0;
        for (const std::uint32_t n : cases[static_cast<std::size_t>(ligne(p.y)) * nx + colonne(p.x)]) {
            const Triangle3& t = triangles[n];
            double z = 0.0;
            // Bords stricts d'un côté : une arête partagée ne compte qu'une fois.
            const double d = (t.b.y - t.c.y) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.y - t.c.y);
            if (std::abs(d) < 1e-12) {
                continue;
            }
            const double l1 = ((t.b.y - t.c.y) * (p.x - t.c.x) + (t.c.x - t.b.x) * (p.y - t.c.y)) / d;
            const double l2 = ((t.c.y - t.a.y) * (p.x - t.c.x) + (t.a.x - t.c.x) * (p.y - t.c.y)) / d;
            const double l3 = 1.0 - l1 - l2;
            if (l1 < 0.0 || l2 < 0.0 || l3 < 0.0 || l1 == 0.0 || l2 == 0.0) {
                continue;
            }
            z = l1 * t.a.z + l2 * t.b.z + l3 * t.c.z;
            traversees += z > p.z;
        }
        return traversees % 2 == 1;
    }
};

bool PeauCourbe::dans_la_matiere(const Vec3& point) const {
    return rangement_ && rangement_->dedans(point);
}

bool PeauCourbe::poser(const Point2& p, Vec3& point, Vec3& normale) const {
    if (faces_.empty()) {
        return false;
    }
    const int i = static_cast<int>((p.x - x0_) / case_), j = static_cast<int>((p.y - y0_) / case_);
    if (i < 0 || j < 0 || i >= nx_ || j >= ny_) {
        return false;
    }
    bool trouve = false;
    for (const std::uint32_t n : cases_[static_cast<std::size_t>(j) * nx_ + i]) {
        const Face& f = faces_[n];
        double z = 0.0;
        if (hauteur_dans_le_triangle(f.a, f.b, f.c, p.x, p.y, z) && (!trouve || z > point.z)) {
            point = {p.x, p.y, z};
            normale = f.normale;
            trouve = true;
        }
    }
    return trouve;
}

PeauCourbe preparer_peau_courbe(const Maillage& partie, const std::vector<const Maillage*>& tout,
                                double epaisseur, double pente_max, double premiere_couche) {
    PeauCourbe peau;
    if (partie.triangles.empty()) {
        return peau;
    }
    const auto rangement_partage = std::make_shared<const RangementPeau>(tout);
    const RangementPeau& rangement = *rangement_partage;
    peau.rangement_ = rangement_partage;
    const double cosinus_min = std::cos(en_radians(pente_max));

    // 1. Les faces du dessus : tournées vers le haut, pas trop raides, sans
    // rien au-dessus d'elles, et avec assez de matière dessous.
    std::vector<std::uint8_t> dessus(partie.triangles.size(), 0);
    double surface_totale = 0.0, z_min = 1e18, z_max = -1e18;
    for (std::size_t n = 0; n < partie.triangles.size(); ++n) {
        const Triangle& t = partie.triangles[n];
        const Vec3 a = en_vec(partie.sommets[t.sommets[0]]);
        const Vec3 b = en_vec(partie.sommets[t.sommets[1]]);
        const Vec3 c = en_vec(partie.sommets[t.sommets[2]]);
        const Vec3 croix = produit_vectoriel(b - a, c - a);
        const double double_aire = norme(croix);
        if (double_aire < 1e-10 || croix.z / double_aire < cosinus_min) {
            continue;
        }
        const Vec3 centre = (a + b + c) * (1.0 / 3.0);
        double z = 0.0;
        if (rangement.surface_voisine(centre, +1, 0.05, z)) {
            continue;  // quelque chose est imprimé au-dessus
        }
        const double dessous = rangement.surface_voisine(centre, -1, 0.02, z) ? z : 0.0;
        if (centre.z - dessous < 5.0 * epaisseur) {
            continue;  // trop mince pour qu'on retire de la matière dessous
        }
        dessus[n] = 1;
        surface_totale += 0.5 * double_aire;
        z_min = std::min({z_min, a.z, b.z, c.z});
        z_max = std::max({z_max, a.z, b.z, c.z});
    }
    // Un dessus plat est déjà lisse ; un tout petit dessus ne vaut pas une passe.
    if (surface_totale < 10.0 || z_max - z_min < epaisseur) {
        return peau;
    }

    // 2. Chaque sommet du dessus est abaissé d'une fois un quart l'épaisseur
    // de la peau (mesurée le long de la normale) : les couches planes, qui
    // suivent la surface en marches d'escalier, restent ainsi sous la peau.
    // Un dessus presque plat est amené pile entre deux couches, pour ne pas
    // frôler un plan de coupe.
    std::vector<Vec3> normales(partie.sommets.size());
    for (std::size_t n = 0; n < partie.triangles.size(); ++n) {
        if (!dessus[n]) {
            continue;
        }
        const Triangle& t = partie.triangles[n];
        const Vec3 a = en_vec(partie.sommets[t.sommets[0]]);
        const Vec3 b = en_vec(partie.sommets[t.sommets[1]]);
        const Vec3 c = en_vec(partie.sommets[t.sommets[2]]);
        const Vec3 croix = produit_vectoriel(b - a, c - a);
        for (const std::uint32_t s : t.sommets) {
            normales[s] = normales[s] + croix;
        }
    }
    peau.abaisse_ = partie;
    for (std::size_t s = 0; s < partie.sommets.size(); ++s) {
        const double longueur = norme(normales[s]);
        if (longueur < 1e-12) {
            continue;
        }
        const double cosinus = std::max(normales[s].z / longueur, cosinus_min);
        double z = partie.sommets[s].z - 1.25 * epaisseur / cosinus;
        if (normales[s].z / longueur > 0.98) {
            // Presque plat : le sommet est amené sur le dessus de la couche
            // la plus proche. Les couches planes s'arrêtent alors net, au
            // lieu de frôler la surface sur une grande étendue.
            z = premiere_couche + std::round((z - premiere_couche) / epaisseur) * epaisseur;
        }
        peau.abaisse_.sommets[s].z = static_cast<float>(z);
    }

    // 3. Les faces d'origine, pour reposer les trajectoires sur la surface,
    // et la zone couverte vue de dessus.
    double x_min = 1e18, y_min = 1e18, x_max = -1e18, y_max = -1e18;
    Polygones triangles_vus;
    for (std::size_t n = 0; n < partie.triangles.size(); ++n) {
        if (!dessus[n]) {
            continue;
        }
        const Triangle& t = partie.triangles[n];
        PeauCourbe::Face face;
        face.a = en_vec(partie.sommets[t.sommets[0]]);
        face.b = en_vec(partie.sommets[t.sommets[1]]);
        face.c = en_vec(partie.sommets[t.sommets[2]]);
        face.normale = normaliser(produit_vectoriel(face.b - face.a, face.c - face.a));
        for (const Vec3& p : {face.a, face.b, face.c}) {
            x_min = std::min(x_min, p.x), x_max = std::max(x_max, p.x);
            y_min = std::min(y_min, p.y), y_max = std::max(y_max, p.y);
        }
        triangles_vus.push_back({en_unites(Point2{face.a.x, face.a.y}), en_unites(Point2{face.b.x, face.b.y}),
                                 en_unites(Point2{face.c.x, face.c.y})});
        peau.faces_.push_back(face);
    }
    peau.x0_ = x_min, peau.y0_ = y_min;
    peau.nx_ = std::max(1, static_cast<int>((x_max - x_min) / peau.case_) + 1);
    peau.ny_ = std::max(1, static_cast<int>((y_max - y_min) / peau.case_) + 1);
    peau.cases_.resize(static_cast<std::size_t>(peau.nx_) * peau.ny_);
    for (std::uint32_t n = 0; n < peau.faces_.size(); ++n) {
        const PeauCourbe::Face& f = peau.faces_[n];
        const auto colonne = [&](double x) { return std::clamp(static_cast<int>((x - peau.x0_) / peau.case_), 0, peau.nx_ - 1); };
        const auto ligne = [&](double y) { return std::clamp(static_cast<int>((y - peau.y0_) / peau.case_), 0, peau.ny_ - 1); };
        for (int j = ligne(std::min({f.a.y, f.b.y, f.c.y})); j <= ligne(std::max({f.a.y, f.b.y, f.c.y})); ++j) {
            for (int i = colonne(std::min({f.a.x, f.b.x, f.c.x})); i <= colonne(std::max({f.a.x, f.b.x, f.c.x})); ++i) {
                peau.cases_[static_cast<std::size_t>(j) * peau.nx_ + i].push_back(n);
            }
        }
    }
    // Les triangles se touchent bord à bord : un petit gonflement puis un
    // retrait les soudent en une zone propre.
    const Polygones exacte = decaler(decaler(Clipper2Lib::Union(triangles_vus, Clipper2Lib::FillRule::NonZero), 0.02), -0.02);

    // 4. Une face du dessus peut être en partie cachée sous autre chose (une
    // branche qui passe au-dessus). La zone est donc contrôlée point par
    // point, sur un quadrillage fin : un point n'est gardé que si la surface
    // la plus haute à sa verticale est bien une face du dessus.
    const double pas = std::clamp(std::max(x_max - x_min, y_max - y_min) / 500.0, 0.15, 0.5);
    const int colonnes = static_cast<int>((x_max - x_min) / pas) + 1, lignes = static_cast<int>((y_max - y_min) / pas) + 1;
    const float aucun = -std::numeric_limits<float>::max();
    std::vector<float> haut_dessus(static_cast<std::size_t>(colonnes) * lignes, aucun), haut_tout = haut_dessus;
    const auto quadriller = [&](const Vec3& a, const Vec3& b, const Vec3& c, std::vector<float>& hauts) {
        const int i0 = std::max(0, static_cast<int>((std::min({a.x, b.x, c.x}) - x_min) / pas - 0.5));
        const int i1 = std::min(colonnes - 1, static_cast<int>((std::max({a.x, b.x, c.x}) - x_min) / pas + 0.5));
        const int j0 = std::max(0, static_cast<int>((std::min({a.y, b.y, c.y}) - y_min) / pas - 0.5));
        const int j1 = std::min(lignes - 1, static_cast<int>((std::max({a.y, b.y, c.y}) - y_min) / pas + 0.5));
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                double z = 0.0;
                if (hauteur_dans_le_triangle(a, b, c, x_min + (i + 0.5) * pas, y_min + (j + 0.5) * pas, z)) {
                    float& haut = hauts[static_cast<std::size_t>(j) * colonnes + i];
                    haut = std::max(haut, static_cast<float>(z));
                }
            }
        }
    };
    for (const PeauCourbe::Face& f : peau.faces_) {
        quadriller(f.a, f.b, f.c, haut_dessus);
    }
    for (const RangementPeau::Triangle3& t : rangement.triangles) {
        quadriller(t.a, t.b, t.c, haut_tout);
    }
    std::vector<std::uint8_t> gardes(haut_dessus.size(), 0);
    for (std::size_t n = 0; n < gardes.size(); ++n) {
        gardes[n] = haut_dessus[n] != aucun && haut_tout[n] <= haut_dessus[n] + 0.02f;
    }
    // Une falaise : deux points voisins gardés, mais à des hauteurs très
    // différentes (le dessus d'un tronc à côté du dessus d'une branche). La
    // passe ne doit pas la traverser : les points proches d'une falaise sont
    // retirés, ce qui sépare la zone en morceaux.
    const double saut = pas * std::tan(en_radians(std::min(pente_max + 25.0, 80.0))) + 0.1;
    std::vector<std::uint8_t> falaise(gardes.size(), 0);
    for (int j = 0; j < lignes; ++j) {
        for (int i = 0; i < colonnes; ++i) {
            const std::size_t n = static_cast<std::size_t>(j) * colonnes + i;
            if (!gardes[n]) {
                continue;
            }
            for (const std::size_t voisin : {i + 1 < colonnes ? n + 1 : n, j + 1 < lignes ? n + colonnes : n}) {
                if (gardes[voisin] && std::abs(haut_dessus[voisin] - haut_dessus[n]) > saut) {
                    falaise[n] = falaise[voisin] = 1;
                }
            }
        }
    }
    const int recul = 3;
    for (int j = 0; j < lignes; ++j) {
        for (int i = 0; i < colonnes; ++i) {
            if (!falaise[static_cast<std::size_t>(j) * colonnes + i]) {
                continue;
            }
            for (int dj = -recul; dj <= recul; ++dj) {
                for (int di = -recul; di <= recul; ++di) {
                    const int a = i + di, b = j + dj;
                    if (a >= 0 && b >= 0 && a < colonnes && b < lignes) {
                        gardes[static_cast<std::size_t>(b) * colonnes + a] = 0;
                    }
                }
            }
        }
    }
    // Les points gardés, regroupés en bandes le long de chaque ligne.
    Polygones bandes;
    for (int j = 0; j < lignes; ++j) {
        for (int i = 0; i < colonnes;) {
            const auto garde = [&](int colonne) {
                return gardes[static_cast<std::size_t>(j) * colonnes + colonne] != 0;
            };
            if (!garde(i)) {
                ++i;
                continue;
            }
            int fin = i;
            while (fin + 1 < colonnes && garde(fin + 1)) {
                ++fin;
            }
            const double xa = x_min + i * pas, xb = x_min + (fin + 1) * pas;
            const double ya = y_min + j * pas, yb = y_min + (j + 1) * pas;
            bandes.push_back({en_unites(Point2{xa, ya}), en_unites(Point2{xb, ya}), en_unites(Point2{xb, yb}),
                              en_unites(Point2{xa, yb})});
            i = fin + 1;
        }
    }
    // Un peu élargie (le quadrillage est en escalier), puis limitée au bord exact des faces.
    const Polygones libre = decaler(Clipper2Lib::Union(bandes, Clipper2Lib::FillRule::NonZero), 1.5 * pas);
    peau.zone_ = intersecter(exacte, libre);
    // Là où quelque chose passe au-dessus, ou près d'une falaise, on recule
    // encore de la largeur du quadrillage.
    const Polygones retiree = soustraire(exacte, libre);
    if (!retiree.empty()) {
        peau.zone_ = soustraire(peau.zone_, decaler(retiree, 2.5 * pas));
    }
    return peau;
}

}  // namespace artprint
