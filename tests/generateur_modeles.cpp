// ArtPrint Slicer - licence AGPL v3
// Fabrique de modèles d'essai : des milliers de pièces différentes, pour
// éprouver le slicer en grand nombre.
//
// Utilisation :
//     generateur_modeles liste
//     generateur_modeles <famille> <graine> <sortie.stl>
//
// Chaque famille est une forme paramétrée (un arbre, un vase, une pieuvre,
// une équerre...). La graine choisit les paramètres au hasard : la même
// graine redonne toujours le même modèle. Le programme écrit sur la sortie
// standard une ligne qui décrit le modèle (famille, graine, paramètres,
// nombre de triangles, dimensions, contrôle de propreté).
//
// Un modèle est « propre » quand sa surface est fermée (chaque arête a
// exactement deux triangles), bien orientée, et d'un seul morceau. Sinon
// le programme renvoie un code d'erreur et n'écrit pas le fichier.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "formes_organiques.hpp"
#include "modeles_vingt.hpp"
#include "stl.hpp"

using namespace artprint;
using namespace essai;

namespace {

const double pi = 3.14159265358979323846;

// --- Le hasard, et le journal des paramètres tirés -------------------------------

struct Alea {
    std::mt19937_64 moteur;
    std::ostringstream journal;

    double u(const char* nom, double a, double b) {
        const double v = a + (b - a) * std::uniform_real_distribution<double>(0.0, 1.0)(moteur);
        journal << ' ' << nom << '=' << std::round(v * 100.0) / 100.0;
        return v;
    }
    int n(const char* nom, int a, int b) {
        const int v = std::uniform_int_distribution<int>(a, b)(moteur);
        journal << ' ' << nom << '=' << v;
        return v;
    }
    bool oui(const char* nom, double probabilite) {
        const bool v = std::uniform_real_distribution<double>(0.0, 1.0)(moteur) < probabilite;
        journal << ' ' << nom << '=' << (v ? 1 : 0);
        return v;
    }
    // Tirages qui ne sont pas notés (détails).
    double u(double a, double b) { return a + (b - a) * std::uniform_real_distribution<double>(0.0, 1.0)(moteur); }
    int n(int a, int b) { return std::uniform_int_distribution<int>(a, b)(moteur); }
};

// --- Briques de construction -----------------------------------------------------

// Un morceau de branche : un boudin dont le rayon change d'un bout à l'autre.
struct Morceau {
    Vec3 a, b;
    double rayon_a = 3.0, rayon_b = 3.0;
};

// Réunion fondue d'une forme de base et d'une liste de morceaux.
Distance avec_morceaux(Distance base, std::vector<Morceau> morceaux, double fondu) {
    return [base = std::move(base), morceaux = std::move(morceaux), fondu](const Vec3& p) {
        double f = base ? base(p) : 1e9;
        for (const Morceau& m : morceaux) {
            f = fondre(f, boudin_conique(p, m.a, m.b, m.rayon_a, m.rayon_b), fondu);
        }
        return f;
    };
}

// Prisme droit à « pans » côtés (6 : hexagone), d'axe vertical, posé sur le plateau.
double prisme(const Vec3& p, int pans, double rayon, double hauteur) {
    double f = -1e9;
    for (int k = 0; k < pans; ++k) {
        const double a = 2.0 * pi * k / pans;
        f = std::max(f, p.x * std::cos(a) + p.y * std::sin(a) - rayon);
    }
    return std::max(f, std::abs(p.z - hauteur / 2.0) - hauteur / 2.0);
}

// Cylindre droit vertical (bords vifs), de z0 à z1.
double cylindre(const Vec3& p, double x, double y, double rayon, double z0, double z1) {
    const double horizontal = std::hypot(p.x - x, p.y - y) - rayon;
    const double vertical = std::abs(p.z - (z0 + z1) / 2.0) - (z1 - z0) / 2.0;
    return std::min(std::max(horizontal, vertical), 0.0) + std::hypot(std::max(horizontal, 0.0), std::max(vertical, 0.0));
}

// Tronc : 0 rond sur un pied, 1 carré, 2 à pans, 3 conique.
double tronc(const Vec3& p, int type, double hauteur, double rayon) {
    switch (type) {
        case 1:
            return std::min(brique(p, {0, 0, hauteur / 2.0}, {rayon, rayon, hauteur / 2.0}, 1.0),
                            brique(p, {0, 0, 1.5}, {rayon + 4.0, rayon + 4.0, 1.5}, 0.8));
        case 2:
            return std::min(prisme(p, 6, rayon, hauteur), prisme(p, 6, rayon + 4.0, 3.0));
        case 3:
            return boudin_conique(p, {0, 0, 0}, {0, 0, hauteur}, rayon * 1.5, rayon * 0.7);
        default:
            return tronc_simple(p, hauteur, rayon);
    }
}

// Bout de branche : 0 rien, 1 boule, 2 évasé, 3 palette.
void finir(std::vector<Morceau>& morceaux, const Vec3& bout, const Vec3& direction, double rayon, int type) {
    if (type == 1) {
        morceaux.push_back({bout, bout + direction * 0.5, rayon * 1.7, rayon * 1.7});
    } else if (type == 2) {
        morceaux.push_back({bout, bout + direction * (2.0 * rayon), rayon, rayon * 1.9});
    } else if (type == 3) {
        const Vec3 cote = normaliser(produit_vectoriel(direction, {0.3, 0.2, 1.0}));
        morceaux.push_back({bout - cote * (1.5 * rayon), bout + cote * (1.5 * rayon), rayon * 1.1, rayon * 1.1});
    }
}

// Longueur d'une branche raccourcie pour que son bout reste au-dessus du plateau.
double longueur_permise(const Vec3& depart, const Vec3& direction, double longueur, double rayon) {
    if (direction.z >= 0.0) {
        return longueur;
    }
    return std::max(4.0, std::min(longueur, (depart.z - rayon - 3.0) / -direction.z));
}

// Coque de révolution : paroi d'épaisseur donnée autour du profil rayon(z, angle),
// fond plein jusqu'à « fond », ouverte en haut. « pleine » : pas de creux.
Distance coque(std::function<double(double, double)> rayon, double hauteur, double epaisseur, double fond, bool pleine) {
    return [rayon = std::move(rayon), hauteur, epaisseur, fond, pleine](const Vec3& p) {
        const double r = std::hypot(p.x, p.y);
        const double profil = rayon(p.z, std::atan2(p.y, p.x));
        const double dehors = std::max(r - profil, p.z - hauteur);
        if (pleine) {
            return dehors;
        }
        const double creux = std::max(r - (profil - epaisseur), fond - p.z);
        return std::max(dehors, -creux);
    };
}

// --- Les familles ---------------------------------------------------------------

// 1. Un tronc et une seule branche : tous les angles, toutes les tailles.
Distance branche_simple(Alea& h) {
    const int type = h.n("tronc", 0, 3);
    const double haut = h.u("hauteur", 30, 70), rayon = h.u("rayon", 4, 8);
    const double inclinaison = h.u("inclinaison", 20, 115), azimut = h.u("azimut", 0, 360);
    const double ra = h.u("rayon_branche", 2, 4.5), rb = ra * h.u("affinage", 0.6, 1.0);
    const Vec3 depart{0, 0, haut * h.u("attache", 0.35, 0.85)};
    const Vec3 d = vers(inclinaison, azimut);
    const double longueur = longueur_permise(depart, d, h.u("longueur", 12, 40), ra);
    std::vector<Morceau> m{{depart, depart + d * longueur, ra, rb}};
    finir(m, depart + d * longueur, d, rb, h.n("bout", 0, 3));
    return avec_morceaux([=](const Vec3& p) { return tronc(p, type, haut, rayon); }, m, h.u("fondu", 1, 3));
}

// 2. Une couronne de branches au même niveau.
Distance couronne(Alea& h) {
    const int type = h.n("tronc", 0, 3), nombre = h.n("branches", 2, 10);
    const double haut = h.u("hauteur", 30, 65), rayon = h.u("rayon", 4.5, 8);
    const double inclinaison = h.u("inclinaison", 30, 100), vrille = h.u("vrille", -40, 40);
    const double ra = h.u("rayon_branche", 2, 3.8), rb = ra * h.u("affinage", 0.6, 1.0);
    const double longueur = h.u("longueur", 12, 32);
    const bool brindilles = h.oui("brindilles", 0.2);
    const int bout = h.n("bout", 0, 3);
    const Vec3 depart{0, 0, haut * h.u("attache", 0.4, 0.9)};
    std::vector<Morceau> m;
    for (int k = 0; k < nombre; ++k) {
        const double azimut = 360.0 * k / nombre;
        const Vec3 d = vers(inclinaison, azimut + vrille);
        const double l = longueur_permise(depart, d, longueur, ra);
        m.push_back({depart, depart + d * l, ra, rb});
        finir(m, depart + d * l, d, rb, bout);
        if (brindilles) {
            const Vec3 milieu = depart + d * (0.6 * l);
            m.push_back({milieu, milieu + vers(std::max(10.0, inclinaison - 45.0), azimut + 70.0) * (0.4 * l), rb, rb * 0.8});
        }
    }
    return avec_morceaux([=](const Vec3& p) { return tronc(p, type, haut, rayon); }, m, h.u("fondu", 1, 2.5));
}

// 3. Plusieurs étages de branches.
Distance etages(Alea& h) {
    const int type = h.n("tronc", 0, 3), niveaux = h.n("etages", 2, 3);
    const double haut = h.u("hauteur", 50, 85), rayon = h.u("rayon", 5, 8);
    std::vector<Morceau> m;
    for (int e = 0; e < niveaux; ++e) {
        const int nombre = h.n("branches", 3, 6);
        const double inclinaison = h.u("inclinaison", 35, 95), longueur = h.u("longueur", 10, 26);
        const double ra = h.u(2.0, 3.4), decalage = h.u(0, 90);
        const Vec3 depart{0, 0, haut * (0.3 + 0.6 * e / std::max(1, niveaux - 1))};
        for (int k = 0; k < nombre; ++k) {
            const Vec3 d = vers(inclinaison, decalage + 360.0 * k / nombre);
            const double l = longueur_permise(depart, d, longueur, ra);
            m.push_back({depart, depart + d * l, ra, ra * 0.75});
        }
    }
    return avec_morceaux([=](const Vec3& p) { return tronc(p, type, haut, rayon); }, m, h.u("fondu", 1, 2.5));
}

// Branches qui se divisent : « enfants » par nœud, « profondeur » niveaux.
void diviser(Alea& h, std::vector<Morceau>& m, const Vec3& depart, double inclinaison, double azimut, double longueur,
             double rayon, int profondeur, int enfants_max, double ecart, bool hasard) {
    if (m.size() > 40) {
        return;
    }
    const Vec3 d = vers(inclinaison, azimut);
    const double l = longueur_permise(depart, d, longueur, rayon);
    const Vec3 bout = depart + d * l;
    m.push_back({depart, bout, rayon, rayon * 0.8});
    if (profondeur <= 0 || rayon < 1.5) {
        return;
    }
    const int enfants = hasard ? h.n(1, enfants_max) : enfants_max;
    const double tour = h.u(0, 360);
    for (int k = 0; k < enfants; ++k) {
        const double a = tour + 360.0 * k / enfants + (hasard ? h.u(-30, 30) : 0.0);
        // La nouvelle direction s'écarte de l'ancienne de « ecart » degrés, du côté « a ».
        const Vec3 cote = normaliser(produit_vectoriel(d, std::abs(d.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}));
        const Vec3 autre = produit_vectoriel(d, cote);
        const double e = en_radians(ecart * (hasard ? h.u(0.6, 1.3) : 1.0));
        const Vec3 nouvelle = normaliser(d * std::cos(e) + (cote * std::cos(en_radians(a)) + autre * std::sin(en_radians(a))) * std::sin(e));
        const double i = en_degres(std::acos(std::clamp(nouvelle.z, -1.0, 1.0)));
        diviser(h, m, bout, std::min(i, 110.0), en_degres(std::atan2(nouvelle.y, nouvelle.x)), longueur * 0.75, rayon * 0.8,
                profondeur - 1, enfants_max, ecart, hasard);
    }
}

// 4. Fourches régulières (en Y, en trident), en cascade.
Distance fourches(Alea& h) {
    const double haut = h.u("hauteur", 16, 36), rayon = h.u("rayon", 4, 6.5);
    std::vector<Morceau> m;
    diviser(h, m, {0, 0, 0}, 0.0, 0.0, haut, rayon, h.n("profondeur", 1, 3), h.n("enfants", 2, 3), h.u("ecart", 20, 75), false);
    return avec_morceaux([=](const Vec3& p) { return palet(p, {0, 0, 1.5}, rayon + 5.0, 1.0) - 0.5; }, m, h.u("fondu", 1, 2.5));
}

// 5. Corail : des divisions au hasard.
Distance corail(Alea& h) {
    const double haut = h.u("hauteur", 14, 30), rayon = h.u("rayon", 4.5, 7);
    std::vector<Morceau> m;
    diviser(h, m, {0, 0, 0}, 0.0, 0.0, haut, rayon, h.n("profondeur", 2, 4), h.n("enfants", 2, 3), h.u("ecart", 25, 65), true);
    return avec_morceaux([=](const Vec3& p) { return palet(p, {0, 0, 1.5}, rayon + 5.0, 1.0) - 0.5; }, m, h.u("fondu", 1, 2.5));
}

// 6. Des branches en spirale le long du tronc.
Distance spirale(Alea& h) {
    const int type = h.n("tronc", 0, 3), nombre = h.n("branches", 5, 12);
    const double haut = h.u("hauteur", 55, 90), rayon = h.u("rayon", 5, 7.5);
    const double pas_angle = h.u("pas", 40, 140), inclinaison = h.u("inclinaison", 45, 95);
    const double longueur = h.u("longueur", 10, 24), ra = h.u("rayon_branche", 2, 3.2);
    std::vector<Morceau> m;
    for (int k = 0; k < nombre; ++k) {
        const Vec3 depart{0, 0, haut * (0.25 + 0.7 * k / (nombre - 1.0))};
        const Vec3 d = vers(inclinaison, pas_angle * k);
        m.push_back({depart, depart + d * longueur_permise(depart, d, longueur, ra), ra, ra * 0.75});
    }
    return avec_morceaux([=](const Vec3& p) { return tronc(p, type, haut, rayon); }, m, h.u("fondu", 1, 2));
}

// 7. Une branche courbe : en arc (vers le haut ou vers le bas), en S, en hélice.
Distance branche_courbe(Alea& h) {
    const int type = h.n("tronc", 0, 3), forme = h.n("forme", 0, 2);
    const double haut = h.u("hauteur", 40, 75), rayon = h.u("rayon", 4.5, 8);
    const double ra = h.u("rayon_branche", 2.2, 4), rb = ra * h.u("affinage", 0.6, 1.0);
    const double azimut = h.u("azimut", 0, 360);
    std::vector<Vec3> points;
    Vec3 ici{0, 0, haut * h.u("attache", 0.35, 0.75)};
    points.push_back(ici);
    if (forme == 2) {
        // Hélice autour du tronc.
        const double tours = h.u("tours", 0.3, 1.2), ecart = rayon + h.u("ecart", 6, 16), montee = h.u("montee", 4, 30);
        const int pas = static_cast<int>(std::ceil(tours * 16.0));
        for (int k = 1; k <= pas; ++k) {
            const double t = static_cast<double>(k) / pas, a = en_radians(azimut) + 2.0 * pi * tours * t;
            const double e = ecart * std::min(1.0, 3.0 * t);
            points.push_back({e * std::cos(a), e * std::sin(a), ici.z + montee * t});
        }
    } else {
        const double debut = h.u("inclinaison_debut", 40, 100), fin = forme == 0 ? h.u("inclinaison_fin", 0, 110) : debut;
        const double milieu = forme == 1 ? h.u("inclinaison_milieu", 10, 110) : 0.5 * (debut + fin);
        const double pas = h.u("longueur", 18, 45) / 8.0;
        for (int k = 0; k < 8; ++k) {
            const double t = k / 7.0;
            const double i = forme == 1 ? debut + (milieu - debut) * std::sin(pi * t) : debut + (fin - debut) * t;
            const Vec3 d = vers(i, azimut);
            if (ici.z + d.z * pas < ra + 3.0) {
                break;
            }
            ici = ici + d * pas;
            points.push_back(ici);
        }
    }
    const double fondu = h.u("fondu", 1, 2.5);
    return [=](const Vec3& p) { return fondre(tronc(p, type, haut, rayon), chemin(p, points, ra, rb), fondu); };
}

// 8. Un tronc penché ou courbe, avec quelques branches.
Distance tronc_penche(Alea& h) {
    const double rayon = h.u("rayon", 4.5, 7), courbure = h.u("inclinaison_fin", 10, 50), longueur = h.u("longueur", 40, 80);
    const double azimut = h.u("azimut", 0, 360);
    const int branches = h.n("branches", 0, 4);
    std::vector<Vec3> axe{{0, 0, 0}};
    for (int k = 1; k <= 10; ++k) {
        axe.push_back(axe.back() + vers(courbure * k / 10.0, azimut) * (longueur / 10.0));
    }
    std::vector<Morceau> m;
    for (int b = 0; b < branches; ++b) {
        const Vec3 depart = axe[static_cast<std::size_t>(h.n(3, 9))];
        const Vec3 d = vers(h.u(25, 100), h.u(0, 360));
        const double ra = h.u(2, 3.4);
        m.push_back({depart, depart + d * longueur_permise(depart, d, h.u(10, 26), ra), ra, ra * 0.75});
    }
    const double fondu = h.u("fondu", 1, 2.5);
    return avec_morceaux([=](const Vec3& p) {
        return fondre(palet(p, {0, 0, 1.5}, rayon + 6.0, 1.0) - 0.5, chemin(p, axe, rayon, rayon * 0.8), 2.0);
    }, m, fondu);
}

// 9. Un socle large et des bras qui montent de son bord (châssis, chandelier).
Distance socle_bras(Alea& h) {
    const bool carre = h.oui("socle_carre", 0.3);
    const double large = h.u("socle", 14, 30), epais = h.u("epaisseur", 2.5, 6);
    const int bras = h.n("bras", 3, 8), bout = h.n("bout", 0, 3);
    const double inclinaison = h.u("inclinaison", -30, 75), longueur = h.u("longueur", 14, 38);
    const double ra = h.u("rayon_bras", 2.4, 4.2), coude = h.u("coude", 0, 60);
    std::vector<Morceau> m;
    for (int k = 0; k < bras; ++k) {
        const double azimut = 360.0 * k / bras + 20.0;
        const Vec3 depart = Vec3{0, 0, epais} + vers(90, azimut) * (large - ra - 1.0);
        // Inclinaison négative : le bras monte en revenant vers le centre.
        const Vec3 d = inclinaison >= 0.0 ? vers(inclinaison, azimut) : vers(-inclinaison, azimut + 180.0);
        const Vec3 milieu = depart + d * (0.55 * longueur);
        const Vec3 d2 = vers(std::max(0.0, std::abs(inclinaison) - coude), inclinaison >= 0.0 ? azimut : azimut + 180.0);
        m.push_back({depart, milieu, ra, ra * 0.9});
        m.push_back({milieu, milieu + d2 * (0.45 * longueur), ra * 0.9, ra * 0.8});
        finir(m, milieu + d2 * (0.45 * longueur), d2, ra * 0.8, bout);
    }
    return avec_morceaux([=](const Vec3& p) {
        return carre ? brique(p, {0, 0, epais / 2.0}, {large, large, epais / 2.0}, 1.0)
                     : palet(p, {0, 0, epais / 2.0}, large - 1.0, epais / 2.0 - 1.0) - 1.0;
    }, m, h.u("fondu", 1.5, 3));
}

// Profil d'un vase : un rayon de base, une pente, deux ondulations, des pans.
std::function<double(double, double)> profil_de_vase(Alea& h, double hauteur) {
    const double base = h.u("rayon", 9, 22), pente = h.u("pente", -0.12, 0.22);
    const double a1 = h.u("onde1", 0, 6), l1 = h.u(18, 70), p1 = h.u(0, 6.28);
    const double a2 = h.u("onde2", 0, 2.5), l2 = h.u(8, 20), p2 = h.u(0, 6.28);
    const int pans = h.oui("pans", 0.25) ? h.n("nombre_pans", 3, 8) : 0;
    const double relief = h.u(0.04, 0.12), torsion = h.u(-0.05, 0.05);
    const double levre = h.oui("levre", 0.35) ? h.u("levre_mm", -5, 6) : 0.0;
    return [=](double z, double angle) {
        double r = base + pente * z + a1 * std::sin(2.0 * pi * z / l1 + p1) + a2 * std::sin(2.0 * pi * z / l2 + p2);
        // Lèvre : le haut s'ouvre (positif) ou se referme (négatif) sur les 8 derniers mm.
        r += levre * std::pow(std::clamp((z - (hauteur - 8.0)) / 8.0, 0.0, 1.0), 2.0);
        if (pans > 0) {
            r *= 1.0 + relief * std::cos(pans * (angle + torsion * z));
        }
        return std::max(r, 5.0);
    };
}

// 10. Vases et pots : des coques de révolution.
Distance vase(Alea& h) {
    const double hauteur = h.u("hauteur", 30, 90);
    return coque(profil_de_vase(h, hauteur), hauteur, h.u("paroi", 1.2, 2.6), h.u("fond", 1.2, 3), h.oui("plein", 0.15));
}

// 11. Tasses : une anse, parfois une paille posée dedans.
Distance tasse_variee(Alea& h) {
    const double hauteur = h.u("hauteur", 25, 55), rayon = h.u("rayon", 12, 22), pente = h.u("pente", -0.05, 0.15);
    const double paroi = h.u("paroi", 1.4, 2.6), fond = h.u("fond", 1.5, 3);
    const Distance corps = coque([=](double z, double) { return rayon + pente * z; }, hauteur, paroi, fond, false);
    const int anses = h.n("anses", 0, 2);
    const double grand = h.u("anse", 7, 0.4 * hauteur + 4.0), petit = h.u("anse_epaisseur", 1.8, 3);
    const double centre_z = hauteur * h.u(0.45, 0.6);
    const bool paille = h.oui("paille", 0.4);
    const double rayon_paille = h.u("paille_rayon", 1.8, 2.8), depasse = h.u("paille_depasse", 8, 30);
    return [=](const Vec3& p) {
        double f = corps(p);
        const double creux = std::max(std::hypot(p.x, p.y) - (rayon + pente * p.z - paroi), fond - p.z);
        for (int k = 0; k < anses; ++k) {
            const Vec3 q = k == 0 ? p : Vec3{-p.x, -p.y, p.z};
            const double anse = anneau_debout(q, {rayon + pente * centre_z + 0.35 * grand, 0, centre_z}, grand, petit);
            f = std::min(f, std::max(anse, -creux + 0.0));
        }
        if (paille) {
            // Posée au fond d'un côté, appuyée sur le bord de l'autre.
            const Vec3 bas{-(rayon - paroi - rayon_paille - 1.0), 0, fond + rayon_paille * 0.5};
            const Vec3 bord{rayon + pente * hauteur - paroi * 0.5, 0, hauteur + rayon_paille * 0.6};
            const Vec3 d = normaliser(bord - bas);
            f = std::min(f, boudin(p, bas, bord + d * depasse, rayon_paille));
        }
        return f;
    };
}

// 12. Coupes sur pied, bols, entonnoirs.
Distance coupe(Alea& h) {
    const double pied = h.u("pied", 8, 40), rayon_pied = h.u("rayon_pied", 3, 6.5);
    const double rayon = h.u("rayon", 12, 28), paroi = h.u("paroi", 1.4, 2.6), socle = h.u("socle", 10, 20);
    const int forme = h.n("forme", 0, 2);
    const double profondeur = rayon * h.u("profondeur", 0.5, 1.0), ouverture = h.u("ouverture", 0.5, 1.6);
    return [=](const Vec3& p) {
        double f = fondre(palet(p, {0, 0, 1.5}, socle, 1.0) - 0.5, boudin(p, {0, 0, 0}, {0, 0, pied + 1.0}, rayon_pied), 2.0);
        const double r = std::hypot(p.x, p.y), z = p.z - pied;
        double bol;
        if (forme == 0) {
            // Demi-boule creuse.
            const double dehors = std::max(boule(p, {0, 0, pied + rayon}, rayon), z - profondeur);
            bol = std::max(dehors, -boule(p, {0, 0, pied + rayon}, rayon - paroi));
        } else {
            // Cône (entonnoir) ou cloche : le rayon grandit avec la hauteur.
            const double profil = forme == 1 ? rayon_pied + ouverture * std::max(z, 0.0)
                                             : rayon_pied + rayon * std::sqrt(std::clamp(z / profondeur, 0.0, 1.0));
            const double dehors = std::max({r - profil, z - profondeur, -z});
            const double creux = std::max(r - (profil - paroi * std::hypot(1.0, ouverture)), paroi - z);
            bol = std::max(dehors, -creux);
        }
        return fondre(f, bol, 1.0);
    };
}

// 13. Tuyaux creux : coudes, S, Y.
Distance tuyau(Alea& h) {
    const double rayon = h.u("rayon", 5, 10), paroi = h.u("paroi", 1.4, 2.4);
    const int sorties = h.n("sorties", 1, 3);
    const double droit = h.u("droit", 6, 25);
    const bool collerette = h.oui("collerette", 0.3);
    std::vector<std::vector<Vec3>> axes;
    for (int s = 0; s < sorties; ++s) {
        std::vector<Vec3> axe{{0, 0, -4}, {0, 0, droit}};
        const double fin = h.u("inclinaison", sorties == 1 ? 20.0 : 25.0, 90), azimut = 360.0 * s / sorties + h.u(-15, 15);
        const double longueur = h.u("longueur", 18, 45);
        for (int k = 1; k <= 8; ++k) {
            axe.push_back(axe.back() + vers(fin * k / 8.0, azimut) * (longueur / 8.0));
        }
        axes.push_back(axe);
    }
    return [=](const Vec3& p) {
        double dehors = 1e9, dedans = 1e9;
        for (const std::vector<Vec3>& axe : axes) {
            dehors = std::min(dehors, chemin(p, axe, rayon, rayon, 0.01));
            std::vector<Vec3> long_axe = axe;
            const Vec3 d = normaliser(axe.back() - axe[axe.size() - 2]);
            if (collerette) {
                dehors = std::min(dehors, boudin(p, axe.back() - d * 3.0, axe.back(), rayon + 2.5) );
                dehors = std::max(dehors, -1e9);
            }
            long_axe.push_back(axe.back() + d * 8.0);
            dedans = std::min(dedans, chemin(p, long_axe, rayon - paroi, rayon - paroi, 0.01));
        }
        // Les bouts sont coupés droit, à leur extrémité.
        for (const std::vector<Vec3>& axe : axes) {
            const Vec3 d = normaliser(axe.back() - axe[axe.size() - 2]);
            const double au_dela = produit_scalaire(p - axe.back(), d);
            if (au_dela > 0.0 && norme(p - axe.back() - d * au_dela) < rayon + 4.0) {
                dehors = std::max(dehors, au_dela);
            }
        }
        return std::max(dehors, -dedans);
    };
}

// 14. Pieuvres : un corps au centre, des tentacules en spirale autour.
Distance pieuvre(Alea& h) {
    const double corps = h.u("corps", 8, 16), haut = corps * h.u("hauteur", 0.9, 1.8);
    const int tentacules = h.n("tentacules", 4, 10), forme = h.n("forme", 0, 2);
    const double rt = h.u("rayon_tentacule", 2, 3.6), enroulement = h.u("enroulement", -22, 22);
    const double portee = h.u("portee", 16, 38), levee = h.u("levee", 0, 22);
    const bool yeux = h.oui("yeux", 0.5);
    std::vector<std::vector<Vec3>> chemins;
    for (int t = 0; t < tentacules; ++t) {
        const double depart = 360.0 * t / tentacules;
        std::vector<Vec3> c;
        for (int k = 0; k <= 12; ++k) {
            const double s = k / 12.0, a = en_radians(depart + enroulement * k);
            const double r = corps * 0.6 + portee * s;
            double z = rt * 0.9;
            if (forme == 1) {
                z += levee * std::pow(s, 3.0);                  // le bout se relève
            } else if (forme == 2) {
                z += levee * std::sin(pi * std::min(1.0, 1.25 * s)) * 0.8;  // une arche qui retombe sur le plateau
            }
            c.push_back({r * std::cos(a), r * std::sin(a), z});
        }
        chemins.push_back(c);
    }
    return [=](const Vec3& p) {
        // Le corps : une boule étirée vers le haut, posée sur le plateau.
        const Vec3 q{p.x, p.y, (p.z - haut * 0.45) * corps / haut};
        double f = (norme(q) - corps) * std::min(1.0, haut / corps);
        for (const std::vector<Vec3>& c : chemins) {
            f = fondre(f, chemin(p, c, rt * 1.3, rt * 0.7, 1.2), 2.0);
        }
        if (yeux) {
            f = fondre(f, boule(p, {corps * 0.75, corps * 0.35, haut * 0.75}, corps * 0.22), 0.8);
            f = fondre(f, boule(p, {corps * 0.75, -corps * 0.35, haut * 0.75}, corps * 0.22), 0.8);
        }
        return f;
    };
}

// 15. Créatures : un corps, des pattes, un cou, une tête, une queue.
Distance creature(Alea& h) {
    const bool debout = h.oui("debout", 0.4);
    const double corps = h.u("corps", 5, 9), longueur = h.u("longueur", 14, 30), jambes = h.u("jambes", 10, 26);
    const double patte = h.u("patte", 2.2, 3.6), ecart = h.u("ecart", 0, 25);
    std::vector<Morceau> m;
    if (debout) {
        // Deux jambes, un torse vertical, deux bras, une tête.
        const Vec3 bassin{0, 0, jambes}, epaules{0, 0, jambes + longueur};
        m.push_back({bassin, epaules, corps, corps * 0.9});
        for (const double cote : {-1.0, 1.0}) {
            m.push_back({{0, cote * (corps * 0.5 + ecart * 0.2), 0}, bassin + Vec3{0, cote * corps * 0.4, 0}, patte, patte * 1.2});
            const Vec3 d = vers(h.u("bras", 30, 150), cote * 90.0);
            m.push_back({epaules + Vec3{0, cote * corps * 0.6, -2}, epaules + Vec3{0, cote * corps * 0.6, -2} + d * h.u(10, 22), patte * 0.9, patte * 0.7});
        }
        m.push_back({epaules, epaules + Vec3{0, 0, corps * 1.5}, corps * 0.5, corps * h.u("tete", 0.7, 1.2)});
    } else {
        // Quatre pattes, un corps horizontal, un cou, une tête, une queue.
        const Vec3 arriere{-longueur / 2.0, 0, jambes}, avant{longueur / 2.0, 0, jambes + h.u(-3, 5)};
        m.push_back({arriere, avant, corps, corps * 0.9});
        for (const double bout : {-1.0, 1.0}) {
            for (const double cote : {-1.0, 1.0}) {
                const Vec3 haut = (bout < 0 ? arriere : avant) + Vec3{0, cote * corps * 0.5, 0};
                const Vec3 pied{haut.x + bout * ecart * 0.3, haut.y + cote * ecart * 0.3, 0};
                m.push_back({pied, haut, patte, patte * 1.2});
            }
        }
        const Vec3 cou = avant + vers(h.u("cou", 10, 70), 0) * h.u("cou_long", 6, 20);
        m.push_back({avant, cou, corps * 0.6, corps * 0.45});
        m.push_back({cou, cou + Vec3{corps * 0.8, 0, 0}, corps * h.u("tete", 0.5, 0.8), corps * 0.4});
        if (h.oui("queue", 0.7)) {
            m.push_back({arriere, arriere + vers(h.u(40, 130), 180) * h.u(8, 20), corps * 0.4, 1.3});
        }
        if (h.oui("cornes", 0.3)) {
            for (const double cote : {-1.0, 1.0}) {
                m.push_back({cou, cou + vers(35, cote * 70.0) * h.u(6, 14), 1.8, 1.2});
            }
        }
    }
    return avec_morceaux(nullptr, m, h.u("fondu", 1.5, 3));
}

// 16. Blobs : des boules fondues les unes dans les autres.
Distance blob(Alea& h) {
    const int nombre = h.n("boules", 3, 9);
    const double fondu = h.u("fondu", 2, 6);
    std::vector<std::pair<Vec3, double>> boules;
    const double premier = h.u("rayon", 7, 14);
    boules.push_back({{0, 0, premier * 0.6}, premier});
    for (int k = 1; k < nombre; ++k) {
        const auto& [centre, rayon] = boules[static_cast<std::size_t>(h.n(0, k - 1))];
        const double r = h.u(5, 12);
        Vec3 c = centre + vers(h.u(20, 110), h.u(0, 360)) * (0.75 * (rayon + r));
        c.z = std::max(c.z, r * 0.5);
        boules.push_back({c, r});
    }
    return [=](const Vec3& p) {
        double f = 1e9;
        for (const auto& [centre, rayon] : boules) {
            f = fondre(f, boule(p, centre, rayon), fondu);
        }
        return f;
    };
}

// 17. Cactus, mains, bois de cerf.
Distance cactus(Alea& h) {
    const int forme = h.n("forme", 0, 2);
    std::vector<Morceau> m;
    Distance base;
    if (forme == 0) {
        // Cactus : des bras qui sortent à l'horizontale puis montent.
        const double haut = h.u("hauteur", 40, 75), rayon = h.u("rayon", 5, 8);
        const int bras = h.n("bras", 1, 4);
        m.push_back({{0, 0, 0}, {0, 0, haut}, rayon, rayon * 0.95});
        for (int k = 0; k < bras; ++k) {
            const Vec3 depart{0, 0, haut * h.u(0.25, 0.7)};
            const double azimut = 360.0 * k / bras + h.u(-25, 25), rb = rayon * h.u(0.5, 0.75);
            const Vec3 coude = depart + vers(h.u("sortie", 70, 100), azimut) * (rayon + h.u(6, 14));
            m.push_back({depart, coude, rb, rb});
            m.push_back({coude, coude + Vec3{0, 0, h.u(8, 26)}, rb, rb * 0.9});
        }
        base = [=](const Vec3& p) { return palet(p, {0, 0, 1.5}, rayon + 5.0, 1.0) - 0.5; };
    } else if (forme == 1) {
        // Main : un poignet, une paume, des doigts en éventail, un pouce.
        const double paume = h.u("paume", 9, 14), poignet = h.u("poignet", 10, 24);
        const int doigts = h.n("doigts", 3, 5);
        const Vec3 centre{0, 0, poignet + paume};
        m.push_back({{0, 0, 0}, {0, 0, poignet}, paume * 0.55, paume * 0.5});
        m.push_back({{0, -paume * 0.45, poignet + paume * 0.6}, {0, paume * 0.45, poignet + paume * 0.6}, paume * 0.55, paume * 0.55});
        const double eventail = h.u("eventail", 15, 60);
        for (int k = 0; k < doigts; ++k) {
            const double a = eventail * (2.0 * k / (doigts - 1.0) - 1.0);
            const Vec3 pied = centre + Vec3{0, paume * 0.7 * (2.0 * k / (doigts - 1.0) - 1.0), 0};
            m.push_back({pied, pied + vers(std::abs(a), a >= 0 ? 90.0 : -90.0) * h.u(12, 24), 2.4, 1.8});
        }
        m.push_back({{0, -paume * 0.6, poignet + paume * 0.4}, Vec3{0, -paume * 0.6, poignet + paume * 0.4} + vers(h.u("pouce", 50, 95), -90) * h.u(9, 16), 2.6, 2.0});
        base = [=](const Vec3& p) { return palet(p, {0, 0, 1.5}, paume, 1.0) - 0.5; };
    } else {
        // Bois de cerf : deux tiges courbes, chacune avec des pointes.
        const double rayon = h.u("rayon", 3, 4.5);
        const int pointes = h.n("pointes", 1, 4);
        for (const double cote : {-1.0, 1.0}) {
            Vec3 ici{0, cote * 4.0, 3};
            const double ouverture = h.u("ouverture", 15, 50), azimut = cote * 90.0;
            for (int k = 0; k < 5; ++k) {
                const Vec3 suite = ici + vers(ouverture * (0.4 + 0.15 * k), azimut) * h.u(9, 13);
                m.push_back({ici, suite, rayon * (1.0 - 0.1 * k), rayon * (0.9 - 0.1 * k)});
                if (k >= 1 && k <= pointes) {
                    m.push_back({suite, suite + vers(h.u(20, 70), azimut + h.u(60, 120)) * h.u(7, 15), rayon * 0.6, rayon * 0.4});
                }
                ici = suite;
            }
        }
        base = [=](const Vec3& p) { return brique(p, {0, 0, 2.5}, {9, 12, 2.5}, 1.0); };
    }
    return avec_morceaux(base, m, h.u("fondu", 1.2, 2.5));
}

// 18. Champignons, parapluies, tables : un chapeau large porté par un pied.
Distance champignon_varie(Alea& h) {
    const int forme = h.n("forme", 0, 3);
    const double pied = h.u("pied", 12, 45), rayon_pied = h.u("rayon_pied", 3.5, 8), chapeau = h.u("chapeau", 10, 28);
    const double epais = h.u("epaisseur", 1.5, 5);
    const int pieds = forme == 3 ? h.n("pieds", 3, 4) : 1;
    return [=](const Vec3& p) {
        double f = 1e9;
        for (int k = 0; k < pieds; ++k) {
            const Vec3 base = pieds == 1 ? Vec3{0, 0, 0} : vers(90, 360.0 * k / pieds + 45.0) * (chapeau * 0.75);
            f = std::min(f, boudin(p, base, {base.x, base.y, pied}, pieds == 1 ? rayon_pied : rayon_pied * 0.6));
        }
        f = std::min(f, palet(p, {0, 0, 1.2}, pieds == 1 ? rayon_pied + 5.0 : 2.0, 0.8) - 0.4);
        double dessus;
        if (forme == 0 || forme == 3) {
            dessus = palet(p, {0, 0, pied + epais / 2.0}, chapeau - 1.0, std::max(0.3, epais / 2.0 - 1.0)) - 1.0;  // disque plat
        } else if (forme == 1) {
            dessus = std::max(boule(p, {0, 0, pied - chapeau * 0.3}, chapeau), pied - p.z);  // dôme
        } else {
            // Parapluie : une coque conique mince qui retombe.
            const double r = std::hypot(p.x, p.y), z = pied + epais + 0.5 * (chapeau - r) * 0.6;
            dessus = std::max(std::abs(p.z - z) - epais / 2.0, r - chapeau);
        }
        return fondre(f, dessus, 1.5);
    };
}

// 19. Arches, ponts, anses, anneaux debout.
Distance arche(Alea& h) {
    const int forme = h.n("forme", 0, 3);
    const double portee = h.u("portee", 10, 32), haut = h.u("hauteur", 8, 40), rayon = h.u("rayon", 3, 6);
    const double pente = h.u("pente", -12, 12);
    const int piles = h.n("piles", 2, 3);
    return [=](const Vec3& p) {
        double f = 1e9;
        if (forme == 3) {
            // Anneau debout sur un petit pied.
            f = fondre(brique(p, {0, 0, 2}, {portee * 0.6, rayon + 3.0, 2}, 0.8),
                       anneau_debout(p, {0, 0, portee + rayon + 1.0}, portee, rayon), 2.0);
            return f;
        }
        for (int k = 0; k < piles; ++k) {
            const double x = portee * (2.0 * k / (piles - 1.0) - 1.0);
            f = std::min(f, boudin(p, {x, 0, 0}, {x, 0, haut + (forme == 1 ? pente * x / portee : 0.0)}, rayon));
            f = std::min(f, palet(p, {x, 0, 1.2}, rayon + 3.0, 0.8) - 0.4);
        }
        if (forme == 0) {
            // Arche : un demi-anneau posé sur les piles.
            f = fondre(f, std::max(anneau_debout(p, {0, 0, haut}, portee, rayon), haut - p.z - rayon), 1.0);
        } else if (forme == 1) {
            f = fondre(f, boudin(p, {-portee, 0, haut - pente}, {portee, 0, haut + pente}, rayon), 1.0);  // pont droit
        } else {
            f = fondre(f, brique(p, {0, 0, haut}, {portee + rayon, rayon * 1.6, rayon * 0.6}, 0.8), 1.0);   // tablier plat
        }
        return f;
    };
}

// 20. Plaques percées : trous, fentes, bossages.
Distance plaque(Alea& h) {
    const double lx = h.u("largeur", 14, 38), ly = h.u("profondeur", 10, 30), epais = h.u("epaisseur", 2.5, 8);
    const int trous = h.n("trous", 1, 6), bossages = h.n("bossages", 0, 3);
    std::vector<std::array<double, 3>> perces, bosses;
    for (int k = 0; k < trous; ++k) {
        perces.push_back({h.u(-lx + 6, lx - 6), h.u(-ly + 6, ly - 6), h.u(1.5, 4.5)});
    }
    for (int k = 0; k < bossages; ++k) {
        bosses.push_back({h.u(-lx + 6, lx - 6), h.u(-ly + 6, ly - 6), h.u(3, 6)});
    }
    const double haut_bosse = h.u("bossage_hauteur", 4, 18);
    return [=](const Vec3& p) {
        double f = brique(p, {0, 0, epais / 2.0}, {lx, ly, epais / 2.0}, 0.8);
        for (const auto& b : bosses) {
            f = std::min(f, cylindre(p, b[0], b[1], b[2], 0.0, epais + haut_bosse));
        }
        for (const auto& t : perces) {
            f = std::max(f, -cylindre(p, t[0], t[1], t[2], -5.0, epais + haut_bosse + 5.0));
        }
        return f;
    };
}

// 21. Équerres, crochets, potences.
Distance equerre_variee(Alea& h) {
    const int forme = h.n("forme", 0, 2);
    const double large = h.u("largeur", 8, 18), epais = h.u("epaisseur", 3, 6), haut = h.u("hauteur", 20, 55);
    const double bras = h.u("bras", 14, 38);
    const int renforts = h.n("renforts", 0, 2);
    const double fin_crochet = h.u("crochet", 20, 130);
    return [=](const Vec3& p) {
        double f;
        if (forme == 0) {
            // Équerre : une semelle, un mur, des renforts en biais.
            f = std::min(brique(p, {bras / 2.0, 0, epais / 2.0}, {bras / 2.0, large, epais / 2.0}, 0.8),
                         brique(p, {epais / 2.0, 0, haut / 2.0}, {epais / 2.0, large, haut / 2.0}, 0.8));
            for (int k = 0; k < renforts; ++k) {
                const double y = renforts == 1 ? 0.0 : large * (k == 0 ? -0.6 : 0.6);
                f = std::min(f, boudin(p, {bras * 0.8, y, epais}, {epais, y, haut * 0.8}, epais * 0.45));
            }
            f = std::max(f, -cylindre(p, bras * 0.65, 0, 2.2, -5, epais + 5));
        } else if (forme == 1) {
            // Crochet : un mur et un bras qui sort puis remonte (ou redescend).
            f = brique(p, {0, 0, haut / 2.0}, {epais / 2.0, large, haut / 2.0}, 0.8);
            f = std::min(f, brique(p, {0, 0, 1.5}, {epais + 4.0, large, 1.5}, 0.6));
            std::vector<Vec3> c{{0, 0, haut * 0.7}};
            for (int k = 1; k <= 8; ++k) {
                c.push_back(c.back() + vers(90.0 - (90.0 - fin_crochet) * 0.0 - (fin_crochet - 20.0) * k / 8.0 + 20.0 * 0.0, 0) * (bras / 8.0));
            }
            f = fondre(f, chemin(p, c, epais * 0.6, epais * 0.5), 1.5);
        } else {
            // Potence : un mât, un bras horizontal, une jambe de force.
            f = fondre(palet(p, {0, 0, 1.5}, epais + 6.0, 1.0) - 0.5, boudin(p, {0, 0, 0}, {0, 0, haut}, epais), 2.0);
            f = fondre(f, boudin(p, {0, 0, haut - epais}, {bras, 0, haut - epais}, epais * 0.7), 1.5);
            if (renforts > 0) {
                f = fondre(f, boudin(p, {0, 0, haut * 0.5}, {bras * 0.7, 0, haut - epais}, epais * 0.5), 1.0);
            }
        }
        return f;
    };
}

// 22. Boîtes : parois, cloisons, rebord rentrant.
Distance boite(Alea& h) {
    const bool ronde = h.oui("ronde", 0.3);
    const double lx = h.u("largeur", 12, 30), ly = h.u("profondeur", 10, 28), haut = h.u("hauteur", 10, 45);
    const double paroi = h.u("paroi", 1.4, 3), fond = h.u("fond", 1.2, 3);
    const int cloisons = h.n("cloisons", 0, 3);
    const double rebord = h.oui("rebord", 0.3) ? h.u("rebord_mm", 1.5, 5) : 0.0;
    return [=](const Vec3& p) {
        double dehors, creux;
        if (ronde) {
            dehors = cylindre(p, 0, 0, lx, 0, haut);
            creux = cylindre(p, 0, 0, lx - paroi, fond, haut + 5);
        } else {
            dehors = brique(p, {0, 0, haut / 2.0}, {lx, ly, haut / 2.0}, 1.0);
            creux = brique(p, {0, 0, fond + haut / 2.0 + 2.5}, {lx - paroi, ly - paroi, haut / 2.0 + 2.5}, 0.6);
        }
        double f = std::max(dehors, -creux);
        for (int k = 0; k < cloisons; ++k) {
            const double x = lx * (2.0 * (k + 1) / (cloisons + 1.0) - 1.0);
            f = std::min(f, std::max(dehors, std::abs(p.x - x) - paroi / 2.0));
        }
        if (rebord > 0.0) {
            // Un rebord qui rentre vers l'intérieur, tout en haut.
            const double anneau = ronde ? std::max(cylindre(p, 0, 0, lx, haut - 2.0, haut), -cylindre(p, 0, 0, lx - paroi - rebord, haut - 5, haut + 5))
                                        : std::max(brique(p, {0, 0, haut - 1.0}, {lx, ly, 1.0}, 0.3),
                                                   -brique(p, {0, 0, haut}, {lx - paroi - rebord, ly - paroi - rebord, 6.0}, 0.3));
            f = std::min(f, anneau);
        }
        return f;
    };
}

// 23. Cylindres étagés : arbres, poulies, bobines, boutons.
Distance cylindres(Alea& h) {
    const int etages = h.n("etages", 2, 6);
    const bool perce = h.oui("perce", 0.4);
    std::vector<std::array<double, 3>> disques;
    double z = 0.0, plus_petit = 1e9;
    for (int k = 0; k < etages; ++k) {
        const double rayon = h.u("r", 5, 24), haut = h.u("h", 3, 16);
        disques.push_back({rayon, z, z + haut});
        plus_petit = std::min(plus_petit, rayon);
        z += haut;
    }
    const double trou = std::min(plus_petit * 0.5, h.u(1.5, 5.0)), total = z;
    return [=](const Vec3& p) {
        double f = 1e9;
        for (const auto& d : disques) {
            f = std::min(f, cylindre(p, 0, 0, d[0], d[1], d[2]));
        }
        return perce ? std::max(f, -cylindre(p, 0, 0, trou, -5, total + 5)) : f;
    };
}

// 24. Engrenages, étoiles, roues torsadées.
Distance engrenage(Alea& h) {
    const int dents = h.n("dents", 5, 24);
    const double rayon = h.u("rayon", 10, 26), creux = h.u("dent", 0.06, 0.22), epais = h.u("epaisseur", 4, 16);
    const double raideur = h.u("raideur", 1, 4), torsion = h.u("torsion", -0.04, 0.04);
    const double moyeu = h.u("moyeu", 0, 12), rayon_moyeu = rayon * h.u(0.25, 0.5);
    const bool perce = h.oui("perce", 0.6);
    return [=](const Vec3& p) {
        const double r = std::hypot(p.x, p.y), a = std::atan2(p.y, p.x) + torsion * p.z;
        const double profil = rayon * (1.0 + creux * std::clamp(raideur * std::cos(dents * a), -1.0, 1.0));
        double f = std::max(r - profil, std::abs(p.z - epais / 2.0) - epais / 2.0);
        f = std::min(f, cylindre(p, 0, 0, rayon_moyeu, 0, epais + moyeu));
        return perce ? std::max(f, -cylindre(p, 0, 0, rayon_moyeu * 0.45, -5, epais + moyeu + 5)) : f;
    };
}

// 25. Formes d'essai des surplombs : murs penchés, cônes renversés, boules, tores, prismes torsadés, trous.
Distance surplomb(Alea& h) {
    const int forme = h.n("forme", 0, 6);
    const double angle = h.u("angle", 10, 75), haut = h.u("hauteur", 15, 50), taille = h.u("taille", 6, 16);
    const int pans = h.n("pans", 3, 8);
    const double torsion = h.u("torsion", 0.01, 0.08);
    return [=](const Vec3& p) {
        const double pente = std::tan(en_radians(angle));
        switch (forme) {
            case 0: {  // mur penché, sur une semelle
                const double mur = std::max({std::abs(p.x - p.z * pente) - taille * 0.3, std::abs(p.y) - taille, p.z - haut});
                return std::min(mur, brique(p, {haut * pente * 0.4, 0, 1.5}, {taille + haut * pente * 0.6, taille + 2.0, 1.5}, 0.6));
            }
            case 1: {  // cône renversé : il s'élargit en montant
                return std::max(std::hypot(p.x, p.y) - (taille * 0.5 + p.z * pente), p.z - haut);
            }
            case 2:  // boule sur un pied
                return fondre(boudin(p, {0, 0, 0}, {0, 0, haut * 0.5}, taille * 0.35), boule(p, {0, 0, haut * 0.5 + taille}, taille), 1.5);
            case 3:  // tore couché sur un pied en croix
                return std::min({std::hypot(std::hypot(p.x, p.y) - taille * 1.4, p.z - haut * 0.5) - taille * 0.4,
                                 boudin(p, {-taille * 1.4, 0, haut * 0.5}, {taille * 1.4, 0, haut * 0.5}, taille * 0.3),
                                 boudin(p, {0, 0, 0}, {0, 0, haut * 0.5}, taille * 0.4)});
            case 4: {  // prisme torsadé
                const double a = torsion * p.z, c = std::cos(a), s = std::sin(a);
                return prisme({p.x * c - p.y * s, p.x * s + p.y * c, p.z}, pans, taille, haut);
            }
            case 5: {  // mur percé de trous horizontaux
                double f = brique(p, {0, 0, haut / 2.0}, {taille * 0.35, taille * 1.6, haut / 2.0}, 0.6);
                for (int k = 0; k < 3; ++k) {
                    const Vec3 c{0, taille * (k - 1.0), haut * 0.55};
                    f = std::max(f, -(std::hypot(p.y - c.y, p.z - c.z) - (1.5 + 1.2 * k)));
                }
                return f;
            }
            default: {  // pyramide à degrés renversée : chaque marche déborde
                double f = 1e9;
                for (int k = 0; k < 4; ++k) {
                    const double demi = taille * 0.5 + k * taille * 0.25 * pente;
                    f = std::min(f, brique(p, {0, 0, haut * (k + 0.5) / 4.0}, {demi, demi, haut / 8.0}, 0.4));
                }
                return f;
            }
        }
    };
}

using Famille = Distance (*)(Alea&);
const std::vector<std::pair<std::string, Famille>>& familles() {
    static const std::vector<std::pair<std::string, Famille>> liste = {
        {"branche_simple", branche_simple}, {"couronne", couronne}, {"etages", etages}, {"fourches", fourches},
        {"corail", corail}, {"spirale", spirale}, {"branche_courbe", branche_courbe}, {"tronc_penche", tronc_penche},
        {"socle_bras", socle_bras}, {"vase", vase}, {"tasse", tasse_variee}, {"coupe", coupe}, {"tuyau", tuyau},
        {"pieuvre", pieuvre}, {"creature", creature}, {"blob", blob}, {"cactus", cactus},
        {"champignon", champignon_varie}, {"arche", arche}, {"plaque", plaque}, {"equerre", equerre_variee},
        {"boite", boite}, {"cylindres", cylindres}, {"engrenage", engrenage}, {"surplomb", surplomb},
    };
    return liste;
}

// --- Maillage, contrôle et écriture -------------------------------------------------

// Cherche la boîte qui contient la forme, puis en extrait la surface.
Maillage mailler(const Distance& distance) {
    const Distance posee = [&](const Vec3& p) { return std::max(distance(p), -p.z); };
    const double grossier = 2.0;
    Vec3 mini{1e9, 1e9, 1e9}, maxi{-1e9, -1e9, -1e9};
    for (double z = 0.0; z <= 160.0; z += grossier) {
        for (double y = -110.0; y <= 110.0; y += grossier) {
            for (double x = -110.0; x <= 110.0; x += grossier) {
                if (posee({x, y, z}) < grossier) {
                    mini = {std::min(mini.x, x), std::min(mini.y, y), std::min(mini.z, z)};
                    maxi = {std::max(maxi.x, x), std::max(maxi.y, y), std::max(maxi.z, z)};
                }
            }
        }
    }
    if (mini.x > maxi.x) {
        return {};
    }
    const double marge = grossier + 1.0;
    mini = {mini.x - marge, mini.y - marge, 0.0};
    maxi = {maxi.x + marge, maxi.y + marge, maxi.z + marge};
    const double volume = (maxi.x - mini.x) * (maxi.y - mini.y) * (maxi.z - mini.z);
    const double pas = std::clamp(std::cbrt(volume / 1.5e6), 0.55, 1.0);
    return surface(posee, {mini.x, mini.y, -pas * 1.5}, maxi, pas);
}

// Nombre de morceaux séparés du maillage (sommets reliés par des triangles).
std::size_t morceaux(const Maillage& m) {
    std::vector<std::uint32_t> chef(m.sommets.size());
    std::iota(chef.begin(), chef.end(), 0u);
    const auto trouver = [&](std::uint32_t a) {
        while (chef[a] != a) {
            a = chef[a] = chef[chef[a]];
        }
        return a;
    };
    for (const Triangle& t : m.triangles) {
        chef[trouver(t.sommets[1])] = trouver(t.sommets[0]);
        chef[trouver(t.sommets[2])] = trouver(t.sommets[0]);
    }
    std::size_t nombre = 0;
    for (std::uint32_t k = 0; k < chef.size(); ++k) {
        nombre += trouver(k) == k;
    }
    return nombre;
}

void ecrire_stl(const Maillage& m, const std::filesystem::path& chemin_fichier) {
    std::ofstream f(chemin_fichier, std::ios::binary);
    char entete[80] = "modele d'essai ArtPrint";
    f.write(entete, 80);
    const auto n = static_cast<std::uint32_t>(m.triangles.size());
    f.write(reinterpret_cast<const char*>(&n), 4);
    for (const Triangle& t : m.triangles) {
        const float normale[3] = {0, 0, 0};
        f.write(reinterpret_cast<const char*>(normale), 12);
        for (int i = 0; i < 3; ++i) {
            f.write(reinterpret_cast<const char*>(&m.sommets[t.sommets[i]]), 12);
        }
        const std::uint16_t attribut = 0;
        f.write(reinterpret_cast<const char*>(&attribut), 2);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "liste") {
        for (const auto& [nom, fonction] : familles()) {
            std::cout << nom << '\n';
        }
        return 0;
    }
    if (argc != 4) {
        std::cerr << "Utilisation : generateur_modeles liste | <famille> <graine> <sortie.stl>\n";
        return 2;
    }
    const std::string famille = argv[1];
    const auto graine = static_cast<std::uint64_t>(std::strtoull(argv[2], nullptr, 10));
    for (std::size_t k = 0; k < familles().size(); ++k) {
        if (familles()[k].first != famille) {
            continue;
        }
        Alea hasard;
        hasard.moteur.seed(graine * 1000003ULL + k * 7919ULL + 17ULL);
        const Distance distance = familles()[k].second(hasard);
        const Maillage maillage = mailler(distance);
        if (maillage.triangles.empty()) {
            std::cerr << "Modèle vide.\n";
            return 1;
        }
        const RapportFermeture fermeture = verifier_fermeture(maillage);
        const std::size_t nombre = morceaux(maillage);
        const BoiteEnglobante boite = calculer_boite(maillage);
        const Point3 taille = boite.dimensions();
        const bool propre = fermeture.ferme && fermeture.orientation_coherente && nombre == 1;
        std::cout << "famille=" << famille << " graine=" << graine << " triangles=" << maillage.triangles.size()
                  << " dimensions=" << std::round(taille.x * 10) / 10 << "x" << std::round(taille.y * 10) / 10 << "x"
                  << std::round(taille.z * 10) / 10 << " morceaux=" << nombre << " propre=" << (propre ? 1 : 0) << " |"
                  << hasard.journal.str() << '\n';
        if (!propre) {
            return 1;
        }
        artprint::ecrire_stl(maillage, std::filesystem::path(argv[3]));
        // Contrôle sur le fichier lui-même, relu comme le slicer le lira : deux
        // sommets distincts mais au même endroit y sont confondus, ce qui peut
        // coller deux parois l'une à l'autre.
        const RapportFermeture relu = verifier_fermeture(lire_stl(argv[3]).maillage);
        if (!relu.ferme || !relu.orientation_coherente) {
            std::filesystem::remove(argv[3]);
            std::cerr << "Fichier relu pas propre : " << relu.message << '\n';
            return 1;
        }
        return 0;
    }
    std::cerr << "Famille inconnue : " << famille << "\n";
    return 2;
}
