// ArtPrint Slicer - licence AGPL v3
#include "trajectoires.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

#include "parallele.hpp"

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

// Clé d'une case de grille. Les voisines se cherchent en décalant le NUMÉRO
// de case (un entier), jamais la coordonnée : ajouter la taille d'une case à
// une coordonnée presque nulle et négative donne exactement la taille de la
// case, et la recherche sauterait une case.
std::int64_t cle_de_case(double x, double y, double taille, int dx = 0, int dy = 0) {
    const std::int64_t i = static_cast<std::int64_t>(std::floor(x / taille)) + dx;
    const std::int64_t j = static_cast<std::int64_t>(std::floor(y / taille)) + dy;
    return i * 4294967296LL + (j & 0xffffffffLL);
}

// Cordons uniques au milieu des passages étroits. « libre » : la zone où ce
// niveau de paroi peut se poser. Là où elle fait moins de deux largeurs de
// cordon, deux parois ne tiennent pas côte à côte : elles se recouvriraient.
// On y pose un seul cordon, au milieu, de la largeur du passage (entre une
// demi-largeur et deux largeurs ; plus étroit, rien n'est imprimé).
void ajouter_comblements(std::vector<Trait>& traits, const Polygones& libre, double largeur, TypeTrait type) {
    const double jeu = 0.03 * largeur;
    const Polygones couvert = decaler(decaler(libre, -(largeur - jeu)), largeur - jeu);
    Polygones etroit = soustraire(libre, couvert);
    etroit = decaler(decaler(etroit, -0.2 * largeur), 0.2 * largeur);  // les miettes sont retirées
    if (etroit.empty()) {
        return;
    }
    struct Arete {
        Point2 a, b;
    };
    std::vector<Arete> aretes;
    for (const Contour& contour : etroit) {
        for (std::size_t i = 0; i < contour.size(); ++i) {
            aretes.push_back({en_mm(contour[i]), en_mm(contour[(i + 1) % contour.size()])});
        }
    }
    // Grille : pour chaque case, les arêtes qui la traversent (à une case près).
    const double case_grille = 2.5 * largeur;
    const auto cle = [&](double x, double y) { return cle_de_case(x, y, case_grille); };
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> cases;
    for (std::uint32_t n = 0; n < aretes.size(); ++n) {
        const Arete& a = aretes[n];
        const int pas = std::max(1, static_cast<int>(std::ceil(distance(a.a, a.b) / (0.5 * case_grille))));
        std::int64_t derniere = std::numeric_limits<std::int64_t>::min();
        for (int k = 0; k <= pas; ++k) {
            const double t = static_cast<double>(k) / pas;
            const std::int64_t c = cle(a.a.x + (a.b.x - a.a.x) * t, a.a.y + (a.b.y - a.a.y) * t);
            if (c != derniere) {
                cases[c].push_back(n);
                derniere = c;
            }
        }
    }
    // Distance, le long de la demi-droite (q, n), jusqu'au bord d'en face.
    const auto en_face = [&](const Point2& q, const Point2& n, std::uint32_t propre) {
        double plus_pres = std::numeric_limits<double>::max();
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                const auto trouve = cases.find(cle_de_case(q.x, q.y, case_grille, dx, dy));
                if (trouve == cases.end()) {
                    continue;
                }
                for (const std::uint32_t k : trouve->second) {
                    if (k == propre) {
                        continue;
                    }
                    const Arete& a = aretes[k];
                    const double ex = a.b.x - a.a.x, ey = a.b.y - a.a.y;
                    const double det = ex * n.y - ey * n.x;
                    if (std::abs(det) < 1e-12) {
                        continue;
                    }
                    const double ax = a.a.x - q.x, ay = a.a.y - q.y;
                    const double t = (ex * ay - ey * ax) / det;       // le long de la demi-droite
                    const double s = (n.x * ay - n.y * ax) / det;     // le long de l'arête
                    if (t > 0.02 * largeur && s >= -1e-9 && s <= 1.0 + 1e-9) {
                        plus_pres = std::min(plus_pres, t);
                    }
                }
            }
        }
        return plus_pres;
    };
    // Points déjà posés : un passage n'est suivi qu'une fois (ses deux bords
    // donnent le même milieu).
    struct Pose {
        Point2 p;
        double passage;
        std::size_t chaine, rang;
    };
    const double case_points = 1.8 * largeur;
    const auto cle_point = [&](double x, double y) { return cle_de_case(x, y, case_points); };
    std::unordered_map<std::int64_t, std::vector<Pose>> poses;
    std::size_t numero_chaine = 0;
    struct Milieu {
        Point2 p;
        double passage;
    };
    std::vector<Milieu> chaine;
    const auto finir = [&]() {
        // La chaîne est découpée là où la largeur du passage change beaucoup :
        // chaque morceau a une seule largeur de cordon.
        std::size_t debut = 0;
        while (debut + 1 < chaine.size()) {
            std::size_t fin = debut + 1;
            double somme = chaine[debut].passage, longueur = 0.0;
            while (fin < chaine.size() && std::abs(chaine[fin].passage - chaine[debut].passage) <= 0.3 * chaine[debut].passage) {
                somme += chaine[fin].passage;
                longueur += distance(chaine[fin - 1].p, chaine[fin].p);
                ++fin;
            }
            if (longueur >= largeur) {
                Trait trait;
                trait.type = type;
                trait.largeur = std::clamp(somme / static_cast<double>(fin - debut), 0.5 * largeur, 1.9 * largeur);
                for (std::size_t k = debut; k < fin; ++k) {
                    // (Deux points presque confondus donneraient un segment sans matière.)
                    if (trait.points.empty() || distance(trait.points.back(), chaine[k].p) > 0.05) {
                        trait.points.push_back(chaine[k].p);
                    }
                }
                if (trait.points.size() >= 2) {
                    traits.push_back(std::move(trait));
                }
            }
            debut = fin < chaine.size() ? fin - 1 : fin;
            if (fin - debut <= 1 && fin < chaine.size()) {
                debut = fin;
            }
        }
        chaine.clear();
        ++numero_chaine;
    };
    const double pas = 0.4 * largeur;
    std::uint32_t numero_arete = 0;
    for (const Contour& contour : etroit) {
        for (std::size_t i = 0; i < contour.size(); ++i, ++numero_arete) {
            const Arete& arete = aretes[numero_arete];
            const double longueur = distance(arete.a, arete.b);
            if (longueur < 1e-6) {
                continue;
            }
            const Point2 d{(arete.b.x - arete.a.x) / longueur, (arete.b.y - arete.a.y) / longueur};
            const Point2 n{-d.y, d.x};  // vers l'intérieur de la zone
            const int nombre = std::max(1, static_cast<int>(std::floor(longueur / pas)));
            for (int k = 0; k < nombre; ++k) {
                const double t = (k + 0.5) / nombre;
                const Point2 q{arete.a.x + (arete.b.x - arete.a.x) * t, arete.a.y + (arete.b.y - arete.a.y) * t};
                const double passage = en_face(q, n, numero_arete);
                if (passage < 0.5 * largeur || passage > 2.1 * largeur) {
                    finir();
                    continue;
                }
                const Point2 milieu{q.x + n.x * passage / 2.0, q.y + n.y * passage / 2.0};
                bool deja = false;
                for (int dx = -1; dx <= 1 && !deja; ++dx) {
                    for (int dy = -1; dy <= 1 && !deja; ++dy) {
                        const auto trouve = poses.find(cle_de_case(milieu.x, milieu.y, case_points, dx, dy));
                        if (trouve == poses.end()) {
                            continue;
                        }
                        for (const Pose& pose : trouve->second) {
                            const bool voisin = pose.chaine == numero_chaine && pose.rang + 5 >= chaine.size();
                            // Deux cordons côte à côte ne doivent pas se recouvrir.
                            const double ecart = std::max(0.6 * largeur, 0.45 * (std::min(passage, 1.9 * largeur) +
                                                                                 std::min(pose.passage, 1.9 * largeur)));
                            if (!voisin && distance(pose.p, milieu) < ecart) {
                                deja = true;
                                break;
                            }
                        }
                    }
                }
                if (deja) {
                    finir();
                    continue;
                }
                // Un saut (le bord tourne brusquement) coupe aussi la chaîne.
                if (!chaine.empty() && distance(chaine.back().p, milieu) > 1.5 * largeur) {
                    finir();
                }
                poses[cle_point(milieu.x, milieu.y)].push_back({milieu, passage, numero_chaine, chaine.size()});
                chaine.push_back({milieu, passage});
            }
        }
        finir();
    }
}

// Filet de sécurité des parois : coupe toute portion de cordon dont l'axe
// tomberait dans un cordon déjà tracé (une boucle qui se pince, deux boucles
// qui se frôlent). « rangs » : pour chaque trait à partir de « premier », son
// rang de priorité (le plus petit est gardé en premier : les parois externes
// passent avant les autres). L'ordre d'impression n'est pas changé.
// À partir du rang « rang_remplissage », ce sont des lignes de remplissage :
// elles touchent les parois par leurs bouts, c'est voulu ; seul un passage
// qui longe un cordon sur plus d'une largeur et demie est coupé (un raccord
// collé contre une paroi qui fait des vagues).
const int rang_remplissage = 1000;
void retirer_les_recouvrements(std::vector<Trait>& traits, std::size_t premier, const std::vector<int>& rangs,
                               double largeur) {
    // Un axe est « dans » un cordon voisin à moins d'une demi-largeur, plus
    // la finesse du contrôle (0,1 mm).
    const double seuil = 0.5 * largeur + 0.1;
    const double pas = 0.25 * largeur;
    const double voisinage = 3.0 * largeur;  // le long du même trait, ses voisins immédiats ne comptent pas
    struct Pose {
        Point2 p;
        std::size_t trait;
        double abscisse;
    };
    const double case_grille = seuil;
    const auto cle = [&](double x, double y) { return cle_de_case(x, y, case_grille); };
    std::unordered_map<std::int64_t, std::vector<Pose>> poses;
    std::vector<std::size_t> ordre(traits.size() - premier);
    for (std::size_t k = 0; k < ordre.size(); ++k) {
        ordre[k] = premier + k;
    }
    std::stable_sort(ordre.begin(), ordre.end(),
                     [&](std::size_t a, std::size_t b) { return rangs[a - premier] < rangs[b - premier]; });
    std::vector<std::vector<Trait>> remplacants(traits.size());
    std::vector<bool> change(traits.size(), false);
    struct Echantillon {
        Point2 p;
        std::size_t segment;  // numéro du segment d'origine
        double abscisse;
        bool garde;
    };
    for (const std::size_t numero : ordre) {
        const Trait& trait = traits[numero];
        const std::size_t nombre = trait.points.size();
        if (nombre < 2) {
            continue;
        }
        const std::size_t segments = trait.ferme ? nombre : nombre - 1;
        double total = 0.0;
        for (std::size_t i = 0; i < segments; ++i) {
            total += distance(trait.points[i], trait.points[(i + 1) % nombre]);
        }
        std::vector<Echantillon> echantillons;
        double abscisse = 0.0;
        bool tout_garde = true;
        for (std::size_t i = 0; i < segments; ++i) {
            const Point2& a = trait.points[i];
            const Point2& b = trait.points[(i + 1) % nombre];
            const double longueur = distance(a, b);
            const int morceaux = std::max(1, static_cast<int>(std::ceil(longueur / pas)));
            for (int k = 0; k < morceaux; ++k) {
                const double t = (k + 0.5) / morceaux;
                Echantillon e{{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}, i, abscisse + longueur * t, true};
                for (int dx = -1; dx <= 1 && e.garde; ++dx) {
                    for (int dy = -1; dy <= 1 && e.garde; ++dy) {
                        const auto trouve = poses.find(cle_de_case(e.p.x, e.p.y, case_grille, dx, dy));
                        if (trouve == poses.end()) {
                            continue;
                        }
                        for (const Pose& pose : trouve->second) {
                            if (distance(pose.p, e.p) >= seuil) {
                                continue;
                            }
                            if (pose.trait == numero) {
                                const double ecart = std::abs(pose.abscisse - e.abscisse);
                                if (ecart < voisinage || (trait.ferme && total - ecart < voisinage)) {
                                    continue;
                                }
                            }
                            e.garde = false;
                            break;
                        }
                    }
                }
                if (e.garde) {
                    poses[cle(e.p.x, e.p.y)].push_back({e.p, numero, e.abscisse});
                }
                echantillons.push_back(e);
            }
            abscisse += longueur;
        }
        // Remplissage : un contact court est gardé.
        if (rangs[numero - premier] >= rang_remplissage) {
            for (std::size_t k = 0; k < echantillons.size();) {
                if (echantillons[k].garde) {
                    ++k;
                    continue;
                }
                std::size_t fin = k;
                while (fin < echantillons.size() && !echantillons[fin].garde) {
                    ++fin;
                }
                if (echantillons[fin - 1].abscisse - echantillons[k].abscisse < 1.5 * largeur) {
                    for (std::size_t i = k; i < fin; ++i) {
                        echantillons[i].garde = true;
                        poses[cle(echantillons[i].p.x, echantillons[i].p.y)].push_back(
                            {echantillons[i].p, numero, echantillons[i].abscisse});
                    }
                }
                k = fin;
            }
        }
        for (const Echantillon& e : echantillons) {
            tout_garde = tout_garde && e.garde;
        }
        if (tout_garde) {
            continue;
        }
        // Le trait est refait : une suite de morceaux ouverts, là où il est gardé.
        change[numero] = true;
        std::size_t depart = 0;
        if (trait.ferme) {
            // On part d'un échantillon retiré, pour ne pas couper un morceau en deux.
            while (depart < echantillons.size() && echantillons[depart].garde) {
                ++depart;
            }
        }
        Trait morceau;
        morceau.type = trait.type;
        morceau.largeur = trait.largeur;
        const auto finir = [&]() {
            double longueur = 0.0;
            for (std::size_t i = 1; i < morceau.points.size(); ++i) {
                longueur += distance(morceau.points[i - 1], morceau.points[i]);
            }
            if (morceau.points.size() >= 2 &&
                longueur >= (rangs[numero - premier] >= rang_remplissage ? 0.5 : 1.5) * largeur) {
                remplacants[numero].push_back(morceau);
            }
            morceau.points.clear();
        };
        const std::size_t tour = trait.ferme ? echantillons.size() : echantillons.size() - depart;
        std::size_t dernier_segment = 0;
        for (std::size_t n = 0; n < tour; ++n) {
            const Echantillon& e = echantillons[(depart + n) % echantillons.size()];
            if (!e.garde) {
                finir();
                continue;
            }
            if (!morceau.points.empty()) {
                // Les sommets d'origine entre l'échantillon précédent et celui-ci.
                for (std::size_t s = dernier_segment; s != e.segment; s = (s + 1) % segments) {
                    morceau.points.push_back(trait.points[(s + 1) % nombre]);
                }
            }
            morceau.points.push_back(e.p);
            dernier_segment = e.segment;
        }
        finir();
    }
    if (std::none_of(change.begin(), change.end(), [](bool c) { return c; })) {
        return;
    }
    std::vector<Trait> refaits(traits.begin(), traits.begin() + static_cast<std::ptrdiff_t>(premier));
    for (std::size_t i = premier; i < traits.size(); ++i) {
        if (!change[i]) {
            refaits.push_back(std::move(traits[i]));
        } else {
            for (Trait& morceau : remplacants[i]) {
                refaits.push_back(std::move(morceau));
            }
        }
    }
    traits = std::move(refaits);
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
    // Les bords de la zone, pour vérifier qu'un raccord ne les traverse pas :
    // dans une zone qui tourne (autour d'un trou, d'une bosse), un raccord
    // tiré tout droit couperait le coin et repasserait sur les parois.
    struct Bord {
        Point2 a, b;
    };
    std::vector<Bord> bords;
    for (const Contour& contour : zone_raccords) {
        for (std::size_t i = 0; i < contour.size(); ++i) {
            bords.push_back({en_mm(contour[i]), en_mm(contour[(i + 1) % contour.size()])});
        }
    }
    const auto traverse_un_bord = [&](const Point2& p, const Point2& q) {
        const auto cote = [](const Point2& a, const Point2& b, const Point2& c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };
        const double x0 = std::min(p.x, q.x), x1 = std::max(p.x, q.x);
        const double y0 = std::min(p.y, q.y), y1 = std::max(p.y, q.y);
        for (const Bord& bord : bords) {
            if (std::max(bord.a.x, bord.b.x) < x0 || std::min(bord.a.x, bord.b.x) > x1 ||
                std::max(bord.a.y, bord.b.y) < y0 || std::min(bord.a.y, bord.b.y) > y1) {
                continue;
            }
            const double c1 = cote(p, q, bord.a), c2 = cote(p, q, bord.b);
            const double c3 = cote(bord.a, bord.b, p), c4 = cote(bord.a, bord.b, q);
            if (((c1 > 0.0 && c2 < 0.0) || (c1 < 0.0 && c2 > 0.0)) && ((c3 > 0.0 && c4 < 0.0) || (c3 < 0.0 && c4 > 0.0))) {
                return true;
            }
        }
        return false;
    };
    auto raccord_possible = [&](const Point2& p, const Point2& q) {
        if (distance(p, q) > raccord_max) {
            return false;
        }
        if (traverse_un_bord(p, q)) {
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
                                                      const ReglagesImpression& r,
                                                      const std::vector<Polygones>* pleins) {
    const double largeur = r.largeur_extrusion;
    // Distance entre la paroi la plus intérieure et le bord du remplissage.
    const double retrait_lignes = largeur * (0.5 - r.recouvrement_remplissage);

    std::vector<CoucheTrajectoires> couches(contours.size());
    // Chaque couche se calcule seule, à partir des contours : elles sont
    // réparties sur tous les cœurs du processeur.
    const auto traiter = [&](std::size_t c) {
        CoucheTrajectoires& couche = couches[c];
        couche.hauteur = c == 0 ? r.hauteur_premiere_couche : r.hauteur_couche;
        couche.z = r.hauteur_premiere_couche + static_cast<double>(c) * r.hauteur_couche;
        const Polygones& matiere = contours[c];
        if (matiere.empty()) {
            return;
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
                // La jupe fait le tour de la pièce par l'extérieur. Entre
                // deux parties proches (les pattes d'une pieuvre...), elle ne
                // rentre pas dans les creux de moins de quatre largeurs : elle
                // s'y pincerait et repasserait sur elle-même. Les boucles
                // enfermées entre les parties ne sont pas tracées.
                const Polygones autour = decaler_arrondi(
                    decaler_arrondi(exterieurs, r.jupe_distance + (i + 2.0) * largeur), -2.0 * largeur);
                Polygones tour;
                for (const Contour& contour : autour) {
                    if (Clipper2Lib::IsPositive(contour)) {
                        tour.push_back(contour);
                    }
                }
                ajouter_boucles(couche.traits, tour, TypeTrait::jupe, largeur);
            }
        }

        // Parois : de l'intérieur vers l'extérieur. L'axe du cordon extérieur
        // est à une demi-largeur du bord de la pièce.
        // Un cordon n'est posé que là où il tient en entier : dans un passage
        // de moins de deux largeurs, les cordons des deux bords se
        // recouvriraient. Le tracé est donc rétréci d'une largeur, puis
        // regonflé d'une demi-largeur. Dans ces passages étroits, un cordon
        // unique est posé au milieu (voir ajouter_comblements).
        const double jeu = 0.03 * largeur;
        const std::size_t premiere_paroi = couche.traits.size();
        std::vector<int> rangs;
        for (int i = r.nombre_parois - 1; i >= 0; --i) {
            const TypeTrait type = i == 0 ? TypeTrait::paroi_externe : TypeTrait::paroi_interne;
            const Polygones trace = decaler(decaler(matiere, -((1.0 + i) * largeur - jeu)), 0.5 * largeur - jeu);
            ajouter_boucles(couche.traits, trace, type, 2.0 * largeur, largeur);
            rangs.resize(couche.traits.size() - premiere_paroi, 2 * i);
            ajouter_comblements(couche.traits, i == 0 ? matiere : decaler(matiere, -i * largeur), largeur, type);
            rangs.resize(couche.traits.size() - premiere_paroi, 2 * i + 1);
        }

        // Filet de sécurité, à la fin de la couche (parois puis remplissage).
        const auto filet = [&]() {
            rangs.resize(couche.traits.size() - premiere_paroi, rang_remplissage);
            retirer_les_recouvrements(couche.traits, premiere_paroi, rangs, largeur);
        };

        // Intérieur : ce qui reste une fois les parois posées.
        const Polygones interieur = decaler(matiere, -r.nombre_parois * largeur);
        if (interieur.empty()) {
            filet();
            return;
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
            // Et les zones qu'on a demandé de remplir.
            if (pleins && c < pleins->size() && !(*pleins)[c].empty()) {
                plein = unir(plein, intersecter((*pleins)[c], interieur));
            }
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
        filet();
    };
    en_parallele(contours.size(), traiter);
    return couches;
}

}  // namespace artprint
