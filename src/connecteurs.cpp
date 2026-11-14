// ArtPrint Slicer - licence AGPL v3
#include "connecteurs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <vector>

#include "decoupe.hpp"

namespace artprint {

namespace {

Vec3 en_vec(const Point3& p) { return {p.x, p.y, p.z}; }

// Vrai si le point est dans la matière du maillage : un rayon tiré du point
// traverse sa surface un nombre impair de fois.
bool dans_la_matiere(const Maillage& maillage, const Vec3& point) {
    // (Une direction quelconque, pour ne pas longer une arête ou une face.)
    const Vec3 d = normaliser(Vec3{0.3712, 0.5239, 0.7661});
    int traversees = 0;
    for (const Triangle& t : maillage.triangles) {
        const Vec3 a = en_vec(maillage.sommets[t.sommets[0]]);
        const Vec3 e1 = en_vec(maillage.sommets[t.sommets[1]]) - a;
        const Vec3 e2 = en_vec(maillage.sommets[t.sommets[2]]) - a;
        const Vec3 p = produit_vectoriel(d, e2);
        const double det = produit_scalaire(e1, p);
        if (std::abs(det) < 1e-12) {
            continue;
        }
        const Vec3 s = point - a;
        const double u = produit_scalaire(s, p) / det;
        if (u < 0.0 || u > 1.0) {
            continue;
        }
        const Vec3 q = produit_vectoriel(s, e1);
        const double v = produit_scalaire(d, q) / det;
        if (v < 0.0 || u + v > 1.0) {
            continue;
        }
        if (produit_scalaire(e2, q) / det > 1e-9) {
            ++traversees;
        }
    }
    return traversees % 2 == 1;
}

// Ajoute au maillage un prisme fermé : un contour (dans le plan u, v autour de
// « origine ») étiré de la hauteur h0 à la hauteur h1 le long de « w ». Le
// contour peut changer de taille entre le bas et le haut (« bas » et « haut »
// ont le même nombre de points, dans le sens inverse des aiguilles d'une
// montre vu depuis w). « en_creux » : faces tournées vers l'intérieur.
void ajouter_prisme(Maillage& maillage, const Vec3& origine, const Vec3& u, const Vec3& v, const Vec3& w,
                    const std::vector<std::array<double, 2>>& bas, const std::vector<std::array<double, 2>>& haut,
                    double h0, double h1, bool en_creux) {
    const auto n = static_cast<std::uint32_t>(bas.size());
    const auto premier = static_cast<std::uint32_t>(maillage.sommets.size());
    const auto poser = [&](const std::array<double, 2>& p, double h) {
        const Vec3 q = origine + u * p[0] + v * p[1] + w * h;
        maillage.sommets.push_back({static_cast<decltype(Point3{}.x)>(q.x), static_cast<decltype(Point3{}.y)>(q.y),
                                    static_cast<decltype(Point3{}.z)>(q.z)});
    };
    for (const auto& p : bas) {
        poser(p, h0);
    }
    for (const auto& p : haut) {
        poser(p, h1);
    }
    // Les centres des deux bouts.
    std::array<double, 2> cb{0.0, 0.0}, ch{0.0, 0.0};
    for (std::uint32_t i = 0; i < n; ++i) {
        cb = {cb[0] + bas[i][0] / n, cb[1] + bas[i][1] / n};
        ch = {ch[0] + haut[i][0] / n, ch[1] + haut[i][1] / n};
    }
    poser(cb, h0);
    poser(ch, h1);
    const std::uint32_t centre_bas = premier + 2 * n, centre_haut = premier + 2 * n + 1;
    const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        Triangle t;
        t.sommets = en_creux ? std::array<std::uint32_t, 3>{a, c, b} : std::array<std::uint32_t, 3>{a, b, c};
        maillage.triangles.push_back(t);
    };
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t j = (i + 1) % n;
        const std::uint32_t b0 = premier + i, b1 = premier + j, t0 = premier + n + i, t1 = premier + n + j;
        triangle(b0, b1, t1);  // le côté
        triangle(b0, t1, t0);
        triangle(centre_bas, b1, b0);   // le bout du bas, tourné vers -w
        triangle(centre_haut, t0, t1);  // le bout du haut, tourné vers +w
    }
}

std::vector<std::array<double, 2>> cercle(double cx, double cy, double rayon, int cotes = 32) {
    std::vector<std::array<double, 2>> points;
    for (int k = 0; k < cotes; ++k) {
        const double a = 2.0 * std::numbers::pi * k / cotes;
        points.push_back({cx + rayon * std::cos(a), cy + rayon * std::sin(a)});
    }
    return points;
}

}  // namespace

int ajouter_connecteurs(Maillage& morceau, const Vec3& centre, const Vec3& sortante,
                        const OptionsConnecteurs& options, std::string& remarque) {
    if (options.type == TypeConnecteur::aucun || morceau.triangles.empty()) {
        return 0;
    }
    const Maillage piece = morceau;  // la pièce sans connecteur, pour savoir où il y a de la matière
    Vec3 u, v;
    repere_autour(sortante, u, v);
    // Les sommets de la face de coupe, dans le plan (u, v).
    std::vector<std::array<double, 2>> section;
    for (const Point3& s : piece.sommets) {
        const Vec3 e = en_vec(s) - centre;
        if (std::abs(produit_scalaire(e, sortante)) < 0.02) {
            section.push_back({produit_scalaire(e, u), produit_scalaire(e, v)});
        }
    }
    if (section.size() < 3) {
        remarque = "Aucun connecteur posé : la face de coupe n'a pas été retrouvée.";
        return 0;
    }
    // La plus grande longueur de la section : sa direction principale.
    double sxx = 0.0, sxy = 0.0, syy = 0.0;
    for (const auto& p : section) {
        sxx += p[0] * p[0];
        sxy += p[0] * p[1];
        syy += p[1] * p[1];
    }
    const double angle = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    const Vec3 longue = u * std::cos(angle) + v * std::sin(angle);
    const Vec3 large = produit_vectoriel(sortante, longue);
    // Vrai s'il y a de la matière sous la face, à (a, b) du centre et à la profondeur donnée.
    const auto plein = [&](double a, double b, double profondeur) {
        return dans_la_matiere(piece, centre + longue * a + large * b - sortante * profondeur);
    };
    // La corde de matière qui passe par le centre, le long de la grande direction.
    const double pas = 0.5;
    const auto bord = [&](double sens, double demi_largeur) {
        double s = 0.0;
        while (s < 400.0 && plein(sens * (s + pas), 0.0, 0.3) && plein(sens * (s + pas), demi_largeur, 0.3) &&
               plein(sens * (s + pas), -demi_largeur, 0.3)) {
            s += pas;
        }
        return s;
    };
    const double jeu = std::clamp(options.jeu, 0.0, 1.0);
    const double d = std::clamp(options.diametre, 1.0, 40.0);
    const double h = std::clamp(options.profondeur, 1.0, 60.0);
    const double depasse = 0.05;  // le connecteur mord à peine à travers la face : pas de faces confondues
    const Vec3 dedans = sortante * -1.0;

    if (options.type == TypeConnecteur::trous || options.type == TypeConnecteur::tenons) {
        const bool trou = options.type == TypeConnecteur::trous;
        const double rayon = trou ? d / 2.0 : std::max(0.4, d / 2.0 - jeu);
        const double garde = d / 2.0 + 1.2;  // de la matière tout autour
        const int nombre = std::clamp(options.nombre, 1, 6);
        const double gauche = bord(-1.0, garde), droite = bord(1.0, garde);
        // Les places : réparties sur la corde, à l'écart des bords.
        std::vector<double> places;
        const double debut = -gauche + garde, fin = droite - garde;
        for (int k = 0; k < nombre; ++k) {
            places.push_back(nombre == 1 ? 0.5 * (debut + fin) : debut + (fin - debut) * k / (nombre - 1));
        }
        int poses = 0;
        double precedente = -1e9;
        for (const double place : places) {
            if (fin < debut || place - precedente < d + 1.5) {
                continue;  // pas la place (ou trop près du précédent)
            }
            // De la matière autour, sur toute la profondeur d'un trou (et un fond de 1 mm).
            bool libre = true;
            for (int k = 0; k < 8 && libre; ++k) {
                const double a = 2.0 * std::numbers::pi * k / 8;
                const double x = place + garde * std::cos(a), y = garde * std::sin(a);
                libre = plein(x, y, 0.3) && (!trou || (plein(x, y, 0.5 * h) && plein(x, y, h + 1.0)));
            }
            if (!libre) {
                continue;
            }
            const auto contour = cercle(place, 0.0, rayon);
            if (trou) {
                // (Dans le repère (longue, large, dedans), le contour tourne à l'envers : d'où « en plein » inversé.)
                ajouter_prisme(morceau, centre, longue, large, dedans, contour, contour, -depasse, h, false);
            } else {
                ajouter_prisme(morceau, centre, longue, large, dedans, contour, contour, -(h - jeu), depasse, true);
            }
            precedente = place;
            ++poses;
        }
        if (poses < nombre) {
            remarque = std::format("{} connecteur(s) posé(s) sur {} demandé(s) : la face de coupe n'a pas assez de "
                                   "matière pour les autres. Solution : un diamètre plus petit, ou moins de connecteurs.",
                                   poses, nombre);
        }
        return poses;
    }

    // Queue d'aronde : un rail en trapèze, plus large loin de la face (il ne
    // peut pas sortir de sa rainure en tirant), le long de la grande direction.
    const bool male = options.type == TypeConnecteur::aronde_male;
    const double base = d, tete = 1.6 * d;
    const double gauche = bord(-1.0, 0.5 * tete + 1.0), droite = bord(1.0, 0.5 * tete + 1.0);
    if (gauche + droite < 2.0 * d) {
        remarque = "Aucune queue d'aronde posée : la face de coupe est trop petite. Solution : une largeur plus petite.";
        return 0;
    }
    if (male) {
        // Le rail : un peu plus court que la corde, et plus petit du jeu.
        const double a0 = -gauche + 0.1 * (gauche + droite), a1 = droite - 0.1 * (gauche + droite);
        const double b = 0.5 * base - jeu, t = 0.5 * tete - jeu, haut = h - jeu;
        // Trapèze dans le plan (large, sortante), étiré le long de « longue ».
        const std::vector<std::array<double, 2>> profil = {{-b, -depasse}, {b, -depasse}, {t, haut}, {-t, haut}};
        ajouter_prisme(morceau, centre, large, sortante, longue, profil, profil, a0, a1, false);
    } else {
        // La rainure : ouverte aux deux bouts (elle traverse la pièce), pour y glisser le rail.
        // (Elle s'arrête juste après le bord de la pièce, là où il est le plus loin sur sa largeur.)
        const auto bout = [&](double sens) {
            double loin = 0.0;
            for (const double b : {-0.5 * tete, 0.0, 0.5 * tete}) {
                double s = 0.0;
                while (s < 400.0 && plein(sens * (s + pas), b, 0.3)) {
                    s += pas;
                }
                loin = std::max(loin, s);
            }
            return loin + pas + 0.3;
        };
        const double a0 = -bout(-1.0), a1 = bout(1.0);
        const double b = 0.5 * base, t = 0.5 * tete;
        const std::vector<std::array<double, 2>> profil = {{-b, depasse}, {-t, -h}, {t, -h}, {b, depasse}};
        ajouter_prisme(morceau, centre, large, sortante, longue, profil, profil, a0, a1, true);
    }
    return 1;
}

}  // namespace artprint
