// ArtPrint Slicer - licence AGPL v3
#include "trajectoires.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "geometrie.hpp"

namespace artprint {

namespace {

// Vrai si le point est dans la matière de la zone (les trous sont exclus).
bool est_dans(const Polygones& zone, const PointEntier& point) {
    int enroulement = 0;
    for (const Contour& contour : zone) {
        const auto position = Clipper2Lib::PointInPolygon(point, contour);
        if (position == Clipper2Lib::PointInPolygonResult::IsOn) {
            return true;
        }
        if (position == Clipper2Lib::PointInPolygonResult::IsInside) {
            enroulement += Clipper2Lib::IsPositive(contour) ? 1 : -1;
        }
    }
    return enroulement > 0;
}

// Transforme un contour en trait fermé qui démarre à l'arrière de la pièce
// (le point le plus loin en Y) : la couture reste alignée d'une couche à l'autre.
Trait boucle(const Contour& contour, TypeTrait type) {
    Trait trait;
    trait.type = type;
    trait.ferme = true;
    std::size_t depart = 0;
    for (std::size_t i = 1; i < contour.size(); ++i) {
        if (contour[i].y > contour[depart].y ||
            (contour[i].y == contour[depart].y && contour[i].x < contour[depart].x)) {
            depart = i;
        }
    }
    trait.points.reserve(contour.size());
    for (std::size_t i = 0; i < contour.size(); ++i) {
        trait.points.push_back(en_mm(contour[(depart + i) % contour.size()]));
    }
    return trait;
}

// Ajoute les boucles d'un niveau de paroi, en simplifiant les points inutiles.
// « largeur_cordon » (facultatif) : une boucle écrasée, dont les deux côtés
// sont à moins d'une demi-largeur l'un de l'autre, est laissée de côté ; le
// cordon s'y recouvrirait lui-même.
void ajouter_boucles(std::vector<Trait>& traits, const Polygones& contours, TypeTrait type,
                     double longueur_minimale, double largeur_cordon = 0.0) {
    for (const Contour& contour :
         Clipper2Lib::SimplifyPaths(contours, 0.002 * unites_par_mm)) {
        if (contour.size() < 3 ||
            Clipper2Lib::Length(contour, true) / unites_par_mm < longueur_minimale) {
            continue;
        }
        if (largeur_cordon > 0.0 && Clipper2Lib::IsPositive(contour) &&
            decaler({contour}, -0.25 * largeur_cordon).empty()) {
            continue;
        }
        traits.push_back(boucle(contour, type));
    }
}

// Partie d'une couche qui n'est pas recouverte par les « nombre » couches
// voisines dans la direction « pas » (+1 : au-dessus, -1 : en dessous).
Polygones partie_exposee(const std::vector<Polygones>& contours, std::size_t couche, int nombre,
                         int pas, const Polygones& interieur) {
    if (nombre <= 0) {
        return {};
    }
    Polygones couvert;
    for (int i = 1; i <= nombre; ++i) {
        const long long voisine = static_cast<long long>(couche) + static_cast<long long>(i) * pas;
        if (voisine < 0 || voisine >= static_cast<long long>(contours.size())) {
            return interieur;  // rien au-dessus (ou en dessous) : tout est exposé
        }
        const Polygones& autre = contours[static_cast<std::size_t>(voisine)];
        couvert = i == 1 ? autre : intersecter(couvert, autre);
    }
    return soustraire(interieur, couvert);
}

}  // namespace

std::string_view nom_type(TypeTrait type) {
    switch (type) {
        case TypeTrait::jupe:
            return "jupe";
        case TypeTrait::paroi_interne:
            return "paroi interne";
        case TypeTrait::paroi_externe:
            return "paroi externe";
        case TypeTrait::remplissage_plein:
            return "remplissage plein";
        case TypeTrait::remplissage:
            return "remplissage";
    }
    return "";
}

std::vector<std::vector<Point2>> lignes_de_remplissage(const Polygones& zone, double espacement,
                                                       double angle) {
    std::vector<std::vector<Point2>> chemins;
    if (zone.empty() || espacement <= 0.0) {
        return chemins;
    }
    // d : direction des lignes ; n : direction perpendiculaire.
    const double a = en_radians(angle);
    const Point2 d{std::cos(a), std::sin(a)};
    const Point2 n{-std::sin(a), std::cos(a)};

    // Étendue de la zone le long de d et de n.
    double s_min = std::numeric_limits<double>::max(), s_max = -s_min;
    double t_min = s_min, t_max = -s_min;
    for (const Contour& contour : zone) {
        for (const PointEntier& point : contour) {
            const Point2 p = en_mm(point);
            const double s = p.x * d.x + p.y * d.y;
            const double t = p.x * n.x + p.y * n.y;
            s_min = std::min(s_min, s), s_max = std::max(s_max, s);
            t_min = std::min(t_min, t), t_max = std::max(t_max, t);
        }
    }

    // Les lignes sont calées sur une grille fixe (multiples de l'espacement) :
    // elles tombent au même endroit d'une couche à l'autre.
    const long long premiere = static_cast<long long>(std::ceil(t_min / espacement));
    const long long derniere = static_cast<long long>(std::floor(t_max / espacement));
    Polygones lignes;
    for (long long k = premiere; k <= derniere; ++k) {
        const double t = static_cast<double>(k) * espacement;
        lignes.push_back({en_unites(Point2{n.x * t + d.x * (s_min - 1.0), n.y * t + d.y * (s_min - 1.0)}),
                          en_unites(Point2{n.x * t + d.x * (s_max + 1.0), n.y * t + d.y * (s_max + 1.0)})});
    }
    if (lignes.empty()) {
        return chemins;
    }

    // Garde seulement les morceaux de lignes situés dans la zone.
    Clipper2Lib::Clipper64 decoupe;
    decoupe.AddOpenSubject(lignes);
    decoupe.AddClip(zone);
    Polygones inutile, morceaux;
    decoupe.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::NonZero, inutile,
                    morceaux);

    // Range les morceaux par ligne, puis le long de la ligne.
    struct Morceau {
        Point2 a, b;  // a avant b le long de d
        bool utilise = false;
    };
    std::map<long long, std::vector<Morceau>> par_ligne;
    for (const Contour& morceau : morceaux) {
        if (morceau.size() < 2) {
            continue;
        }
        Point2 p = en_mm(morceau.front());
        Point2 q = en_mm(morceau.back());
        if (distance(p, q) < 0.05) {
            continue;  // trop court pour être imprimé
        }
        if (p.x * d.x + p.y * d.y > q.x * d.x + q.y * d.y) {
            std::swap(p, q);
        }
        const double t = 0.5 * ((p.x + q.x) * n.x + (p.y + q.y) * n.y);
        par_ligne[std::llround(t / espacement)].push_back({p, q});
    }

    // Relie les morceaux en zigzag : de la fin d'un morceau au bout le plus
    // proche d'un morceau de la ligne suivante, si le raccord est court et
    // reste dans la zone.
    // Un raccord doit aussi longer le bord de la zone : son milieu ne doit
    // pas tomber au cœur de la zone, sinon il barrerait le remplissage.
    const double raccord_max = 2.0 * espacement;
    const Polygones zone_raccords = decaler(zone, 0.05);
    const Polygones coeur = decaler(zone, -0.4 * espacement);
    auto raccord_possible = [&](const Point2& p, const Point2& q) {
        if (distance(p, q) > raccord_max) {
            return false;
        }
        for (const double f : {0.25, 0.5, 0.75}) {
            if (!est_dans(zone_raccords,
                          en_unites(Point2{p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f}))) {
                return false;
            }
        }
        return !est_dans(coeur, en_unites(Point2{(p.x + q.x) / 2.0, (p.y + q.y) / 2.0}));
    };

    for (auto ligne = par_ligne.begin(); ligne != par_ligne.end(); ++ligne) {
        for (Morceau& premier : ligne->second) {
            if (premier.utilise) {
                continue;
            }
            premier.utilise = true;
            std::vector<Point2> chemin{premier.a, premier.b};
            auto courante = ligne;
            for (;;) {
                auto suivante = std::next(courante);
                if (suivante == par_ligne.end() || suivante->first != courante->first + 1) {
                    break;
                }
                Morceau* meilleur = nullptr;
                bool par_la_fin = false;
                double meilleure_distance = std::numeric_limits<double>::max();
                for (Morceau& candidat : suivante->second) {
                    if (candidat.utilise) {
                        continue;
                    }
                    for (const bool fin : {false, true}) {
                        const Point2& bout = fin ? candidat.b : candidat.a;
                        const double longueur = distance(chemin.back(), bout);
                        if (longueur < meilleure_distance &&
                            raccord_possible(chemin.back(), bout)) {
                            meilleur = &candidat;
                            par_la_fin = fin;
                            meilleure_distance = longueur;
                        }
                    }
                }
                if (!meilleur) {
                    break;
                }
                meilleur->utilise = true;
                chemin.push_back(par_la_fin ? meilleur->b : meilleur->a);
                chemin.push_back(par_la_fin ? meilleur->a : meilleur->b);
                courante = suivante;
            }
            chemins.push_back(std::move(chemin));
        }
    }
    return chemins;
}

std::vector<CoucheTrajectoires> calculer_trajectoires(const std::vector<Polygones>& contours,
                                                      const ReglagesImpression& r) {
    const double largeur = r.largeur_extrusion;
    // Distance entre la paroi la plus intérieure et le bord du remplissage.
    const double retrait_lignes = largeur * (0.5 - r.recouvrement_remplissage);

    std::vector<CoucheTrajectoires> couches(contours.size());
    for (std::size_t c = 0; c < contours.size(); ++c) {
        CoucheTrajectoires& couche = couches[c];
        couche.hauteur = c == 0 ? r.hauteur_premiere_couche : r.hauteur_couche;
        couche.z = r.hauteur_premiere_couche + static_cast<double>(c) * r.hauteur_couche;
        const Polygones& matiere = contours[c];
        if (matiere.empty()) {
            continue;
        }

        // Jupe : sur la première couche, autour de la pièce.
        // Seuls les contours extérieurs comptent : pas de jupe dans les trous.
        if (c == 0) {
            Polygones exterieurs;
            for (const Contour& contour : matiere) {
                if (Clipper2Lib::IsPositive(contour)) {
                    exterieurs.push_back(contour);
                }
            }
            for (int i = r.jupe_boucles - 1; i >= 0; --i) {
                ajouter_boucles(couche.traits,
                                decaler_arrondi(exterieurs, r.jupe_distance + i * largeur),
                                TypeTrait::jupe, largeur);
            }
        }

        // Parois : de l'intérieur vers l'extérieur. L'axe du cordon extérieur
        // est à une demi-largeur du bord de la pièce.
        for (int i = r.nombre_parois - 1; i >= 0; --i) {
            ajouter_boucles(couche.traits, decaler(matiere, -(0.5 + i) * largeur),
                            i == 0 ? TypeTrait::paroi_externe : TypeTrait::paroi_interne,
                            2.0 * largeur, largeur);
        }

        // Intérieur : ce qui reste une fois les parois posées.
        const Polygones interieur = decaler(matiere, -r.nombre_parois * largeur);
        if (interieur.empty()) {
            continue;
        }

        // Zones pleines : là où la pièce n'est pas recouverte au-dessus ou
        // en dessous. Les miettes sont retirées, puis la zone est élargie
        // pour bien s'accrocher au reste.
        Polygones plein;
        if (r.densite_remplissage >= 99.999) {
            plein = interieur;
        } else {
            const Polygones expose =
                unir(partie_exposee(contours, c, r.couches_dessus, +1, interieur),
                     partie_exposee(contours, c, r.couches_dessous, -1, interieur));
            const Polygones nettoye = decaler(expose, -0.3 * largeur);
            plein = intersecter(decaler(nettoye, 2.3 * largeur), interieur);
        }
        const Polygones creux = soustraire(interieur, plein);

        // Remplissage plein : lignes serrées, croisées d'une couche à l'autre.
        const double angle = c % 2 == 0 ? 45.0 : 135.0;
        for (auto& points :
             lignes_de_remplissage(decaler(plein, -retrait_lignes), largeur, angle)) {
            couche.traits.push_back({TypeTrait::remplissage_plein, std::move(points), false});
        }
        // Remplissage intérieur : lignes espacées selon la densité. Elles
        // s'arrêtent à une demi-largeur de la zone pleine : sinon leurs
        // liaisons longeraient le bout des lignes pleines en les recouvrant
        // de moitié (matière déposée deux fois).
        if (r.densite_remplissage > 0.001) {
            const double espacement = largeur * 100.0 / r.densite_remplissage;
            const Polygones zone_creuse =
                soustraire(decaler(creux, -retrait_lignes), decaler(plein, 0.5 * largeur));
            for (auto& points : lignes_de_remplissage(zone_creuse, espacement, angle)) {
                couche.traits.push_back({TypeTrait::remplissage, std::move(points), false});
            }
        }
    }
    return couches;
}

}  // namespace artprint
