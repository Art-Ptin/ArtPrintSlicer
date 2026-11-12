// ArtPrint Slicer - licence AGPL v3
// Vingt modèles d'essai pour les couches courbes. Chacun éprouve une
// difficulté précise ; ensemble, ils servent à « entraîner » le slicer : on
// les tranche tous, on regarde ce qui est refusé ou mal imprimé, on corrige.
//
// Ils sont fabriqués comme ceux de formes_organiques.hpp : une formule dit,
// pour chaque point, à quelle distance il est de la surface.
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "formes_organiques.hpp"

namespace essai {

// Brique aux arêtes légèrement arrondies, centrée en « centre ».
inline double brique(const Vec3& p, const Vec3& centre, const Vec3& demi, double arrondi = 0.6) {
    const double x = std::abs(p.x - centre.x) - (demi.x - arrondi);
    const double y = std::abs(p.y - centre.y) - (demi.y - arrondi);
    const double z = std::abs(p.z - centre.z) - (demi.z - arrondi);
    return std::min(std::max({x, y, z}), 0.0) +
           std::sqrt(std::pow(std::max(x, 0.0), 2) + std::pow(std::max(y, 0.0), 2) + std::pow(std::max(z, 0.0), 2)) -
           arrondi;
}

// Suite de boudins qui se suivent (une branche courbe passant par ces points).
inline double chemin(const Vec3& p, const std::vector<Vec3>& points, double rayon_debut, double rayon_fin,
                     double fondu = 1.5) {
    double f = 1e9;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double a = static_cast<double>(i - 1) / static_cast<double>(points.size() - 1);
        const double b = static_cast<double>(i) / static_cast<double>(points.size() - 1);
        const double morceau = boudin_conique(p, points[i - 1], points[i], rayon_debut + (rayon_fin - rayon_debut) * a,
                                              rayon_debut + (rayon_fin - rayon_debut) * b);
        f = i == 1 ? morceau : fondre(f, morceau, fondu);
    }
    return f;
}

// Le tronc ordinaire de ces modèles : une colonne ronde sur un petit pied.
inline double tronc_simple(const Vec3& p, double hauteur, double rayon = 5.0) {
    return fondre(palet(p, {0, 0, 1.5}, rayon + 5.0, 1.0) - 0.5, boudin(p, {0, 0, 0}, {0, 0, hauteur}, rayon), 2.0);
}

// 1. Une branche à l'horizontale : le cas le plus simple, tête couchée.
inline Maillage v01_branche_horizontale() {
    const Distance d = [](const Vec3& p) {
        return fondre(tronc_simple(p, 45), boudin(p, {0, 0.21, 27.63}, {26.3, 0.21, 27.63}, 3.37), 2.0);
    };
    return forme_posee(d, {-14, -14, 0}, {34, 14, 54}, 0.63);
}

// 2. Une branche presque droite vers le haut (20° seulement) : pied très allongé.
inline Maillage v02_branche_raide() {
    const Distance d = [](const Vec3& p) {
        return fondre(tronc_simple(p, 40), boudin(p, {0, 0, 18}, Vec3{0, 0, 18} + vers(20, 0) * 34.0, 3.0), 2.5);
    };
    return forme_posee(d, {-14, -14, 0}, {22, 14, 58});
}

// 3. Une branche qui retombe (110° depuis la verticale) : la tête devrait
// regarder vers le haut par en dessous.
inline Maillage v03_branche_tombante() {
    const Distance d = [](const Vec3& p) {
        return fondre(tronc_simple(p, 50), boudin(p, {0, 0.13, 38.07}, Vec3{0, 0.13, 38.07} + vers(110, 0) * 24.0, 3.2), 2.0);
    };
    return forme_posee(d, {-14, -14, 0}, {34, 14, 58});
}

// 4. Un socle large et quatre bras qui montent de son bord (comme un châssis
// de drone) : le pied du bras s'appuie sur deux faces du socle à la fois.
inline Maillage v04_socle_et_bras() {
    const Distance d = [](const Vec3& p) {
        double f = palet(p, {0, 0, 4}, 16.0, 3.0) - 1.0;
        for (int bras = 0; bras < 4; ++bras) {
            const double azimut = 45.0 + 90.0 * bras;
            const Vec3 depart = Vec3{0, 0, 5} + vers(90, azimut) * 13.0;
            f = fondre(f, boudin_conique(p, depart, depart + vers(60, azimut) * 30.0, 3.8, 3.0), 3.0);
        }
        return f;
    };
    return forme_posee(d, {-46, -46, 0}, {46, 46, 28});
}

// 5. Étoile : six branches au même niveau, tout autour du tronc. Chacune doit
// être imprimée sans toucher ses voisines.
inline Maillage v05_etoile() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 44, 6.0);
        for (int n = 0; n < 6; ++n) {
            f = fondre(f, boudin_conique(p, {0, 0, 26}, Vec3{0, 0, 26} + vers(65, 60.0 * n) * 26.0, 3.0, 2.2), 2.0);
        }
        return f;
    };
    return forme_posee(d, {-32, -32, 0}, {32, 32, 52});
}

// 6. Spirale : huit branches qui montent en tournant autour du tronc.
inline Maillage v06_spirale() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 70, 5.5);
        for (int n = 0; n < 8; ++n) {
            const Vec3 pied{0, 0, 14.0 + 6.5 * n};
            f = fondre(f, boudin_conique(p, pied, pied + vers(70, 75.0 * n) * 20.0, 2.8, 2.0), 2.0);
        }
        return f;
    };
    return forme_posee(d, {-28, -28, 0}, {28, 28, 78});
}

// 7. Fourche en Y : le tronc s'arrête et se partage en deux, sans continuer.
inline Maillage v07_fourche_y() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 26, 5.0);
        f = fondre(f, boudin_conique(p, {0, 0, 26}, Vec3{0, 0, 26} + vers(40, 0) * 30.0, 4.2, 3.0), 3.0);
        f = fondre(f, boudin_conique(p, {0, 0, 26}, Vec3{0, 0, 26} + vers(40, 180) * 30.0, 4.2, 3.0), 3.0);
        return f;
    };
    return forme_posee(d, {-28, -14, 0}, {28, 14, 56});
}

// 8. Fourches sur fourches : trois niveaux de branches, de plus en plus fines.
inline Maillage v08_fourches_en_cascade() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 30, 5.5);
        for (int a = 0; a < 2; ++a) {
            const Vec3 bout_a = Vec3{0, 0, 28} + vers(45, 180.0 * a) * 20.0;
            f = fondre(f, boudin_conique(p, {0, 0, 28}, bout_a, 4.0, 3.0), 2.5);
            for (int b = 0; b < 2; ++b) {
                const Vec3 bout_b = bout_a + vers(45, 180.0 * a + (b ? 60.0 : -60.0)) * 15.0;
                f = fondre(f, boudin_conique(p, bout_a, bout_b, 3.0, 2.2), 2.0);
                for (int c = 0; c < 2; ++c) {
                    const Vec3 bout_c = bout_b + vers(35, 180.0 * a + (b ? 60.0 : -60.0) + (c ? 70.0 : -70.0)) * 10.0;
                    f = fondre(f, boudin_conique(p, bout_b, bout_c, 2.2, 1.6), 1.5);
                }
            }
        }
        return f;
    };
    return forme_posee(d, {-40, -30, 0}, {40, 30, 66});
}

// 9. Tronc penché (15°) avec deux branches : le tronc lui-même est en léger
// surplomb d'un côté.
inline Maillage v09_tronc_penche() {
    const Distance d = [](const Vec3& p) {
        const Vec3 haut = vers(15, 0) * 56.0;
        double f = fondre(palet(p, {0, 0, 1.5}, 11.0, 1.0) - 0.5, boudin_conique(p, {0, 0, 0}, haut, 6.0, 4.5), 2.0);
        f = fondre(f, boudin_conique(p, vers(15, 0) * 26.0, vers(15, 0) * 26.0 + vers(60, 180) * 24.0, 3.2, 2.4), 2.5);
        f = fondre(f, boudin_conique(p, vers(15, 0) * 38.0, vers(15, 0) * 38.0 + vers(65, 20) * 20.0, 3.0, 2.2), 2.5);
        return f;
    };
    return forme_posee(d, {-30, -16, 0}, {42, 16, 62});
}

// 10. Tronc carré : la branche part d'une face plate, près d'une arête.
inline Maillage v10_tronc_carre() {
    const Distance d = [](const Vec3& p) {
        double f = brique(p, {0, 0, 24}, {6, 6, 24.5}, 0.8);
        f = fondre(f, boudin(p, {4, 0, 30}, Vec3{4, 0, 30} + vers(60, 0) * 24.0, 3.2), 1.5);
        f = fondre(f, boudin(p, {0, 3, 18}, Vec3{0, 3, 18} + vers(75, 90) * 20.0, 2.8), 1.5);
        return f;
    };
    return forme_posee(d, {-10, -10, 0}, {34, 30, 52});
}

// 11. Branche carrée : une poutre à arêtes vives sur un tronc rond.
inline Maillage v11_branche_carree() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 46, 6.0);
        f = fondre(f, brique(p, {15, 0, 30}, {15, 3.2, 3.2}, 0.5), 1.0);
        return f;
    };
    return forme_posee(d, {-14, -14, 0}, {34, 14, 54});
}

// 12. Branche en S : elle part de côté, monte, puis repart de côté.
inline Maillage v12_branche_en_s() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 40, 5.5);
        f = fondre(f, chemin(p, {{0, 0, 18}, {12, 0, 20}, {19, 0, 27}, {20, 0, 36}, {25, 0, 44}, {35, 0, 47}}, 3.4, 2.6), 2.0);
        return f;
    };
    return forme_posee(d, {-14, -14, 0}, {42, 14, 54});
}

// 13. Arche : deux piliers reliés par un arc. L'arc est attaqué par ses deux
// bouts et doit se refermer au milieu.
inline Maillage v13_arche() {
    const Distance d = [](const Vec3& p) {
        double f = boudin(p, {-16, 0, 0}, {-16, 0, 28}, 4.5);
        f = std::min(f, boudin(p, {16, 0, 0}, {16, 0, 28}, 4.5));
        std::vector<Vec3> arc;
        for (int n = 0; n <= 10; ++n) {
            const double a = en_radians(18.0 * n);
            arc.push_back({-16.0 * std::cos(a), 0, 28.0 + 14.0 * std::sin(a)});
        }
        return fondre(f, chemin(p, arc, 3.6, 3.6, 1.0), 2.0);
    };
    return forme_posee(d, {-24, -8, 0}, {24, 8, 50});
}

// 14. Anse : un anneau accroché au tronc par deux endroits, comme sur une tasse.
inline Maillage v14_anse() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 44, 7.0);
        return fondre(f, anneau_debout(p, {9, 0, 26}, 10.0, 2.6), 2.0);
    };
    return forme_posee(d, {-16, -16, 0}, {26, 16, 52});
}

// 15. Champignon : un chapeau qui déborde tout autour du pied.
inline Maillage v15_champignon() {
    const Distance d = [](const Vec3& p) {
        double f = boudin(p, {0, 0, 0}, {0, 0, 26}, 5.0);
        const double chapeau = std::max(boule(p, {0, 0, 22}, 18.0), 24.0 - p.z);
        return fondre(f, chapeau, 2.5);
    };
    return forme_posee(d, {-22, -22, 0}, {22, 22, 44});
}

// 16. Table : un plateau plat et large sur un pied fin. Le dessous du plateau
// est entièrement dans le vide.
inline Maillage v16_table() {
    const Distance d = [](const Vec3& p) {
        double f = fondre(palet(p, {0, 0, 1.5}, 10.0, 1.0) - 0.5, boudin(p, {0, 0, 0}, {0, 0, 30}, 4.0), 2.0);
        return fondre(f, palet(p, {0, 0, 31}, 20.0, 1.5) - 1.0, 2.0);
    };
    return forme_posee(d, {-24, -24, 0}, {24, 24, 38});
}

// 17. Trompette : une branche qui s'élargit vers son bout.
inline Maillage v17_trompette() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 44, 5.5);
        return fondre(f, boudin_conique(p, {0, 0, 26}, Vec3{0, 0, 26} + vers(60, 0) * 26.0, 2.6, 7.5), 2.0);
    };
    return forme_posee(d, {-14, -14, 0}, {36, 14, 52});
}

// 18. Brindilles et grosse branche : des diamètres très différents sur le même tronc.
inline Maillage v18_fin_et_gros() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 52, 6.5);
        f = fondre(f, boudin(p, {0, 0, 22}, Vec3{0, 0, 22} + vers(60, 0) * 22.0, 5.5), 2.5);
        f = fondre(f, boudin(p, {0, 0, 30}, Vec3{0, 0, 30} + vers(60, 140) * 20.0, 1.3), 1.0);
        f = fondre(f, boudin(p, {0, 0, 38}, Vec3{0, 0, 38} + vers(55, 230) * 18.0, 1.0), 1.0);
        return f;
    };
    return forme_posee(d, {-22, -22, 0}, {32, 22, 60});
}

// 19. Sucette : une boule au bout d'une tige fine, sur le côté.
inline Maillage v19_sucette() {
    const Distance d = [](const Vec3& p) {
        double f = tronc_simple(p, 44, 5.5);
        const Vec3 bout = Vec3{0, 0, 24} + vers(55, 0) * 24.0;
        f = fondre(f, boudin(p, {0, 0, 24}, bout, 2.2), 2.0);
        return fondre(f, boule(p, bout, 7.0), 1.5);
    };
    return forme_posee(d, {-14, -14, 0}, {34, 14, 52});
}

// 20. Corail : douze branches sur trois niveaux, de toutes inclinaisons, avec
// des rameaux. Le modèle le plus chargé.
inline Maillage v20_corail() {
    const Distance d = [](const Vec3& p) {
        double f = fondre(palet(p, {0, 0, 2}, 13.0, 1.5) - 0.5, boudin_conique(p, {0, 0, 0}, {0, 0, 72}, 6.5, 3.5), 2.5);
        for (int n = 0; n < 12; ++n) {
            const double hauteur = 18.0 + 4.3 * n;
            const double azimut = 137.5 * n;  // l'angle d'or : les branches ne se gênent pas
            const double inclinaison = 45.0 + 25.0 * std::sin(1.7 * n);
            const Vec3 pied{0, 0, hauteur};
            const Vec3 bout = pied + vers(inclinaison, azimut) * (22.0 - 0.8 * n);
            f = fondre(f, boudin_conique(p, pied, bout, 3.0 - 0.08 * n, 2.0), 2.0);
            if (n % 3 == 0) {
                const Vec3 milieu = pied + (bout - pied) * 0.6;
                f = fondre(f, boudin_conique(p, milieu, milieu + vers(25, azimut + 80.0) * 11.0, 1.9, 1.4), 1.5);
            }
        }
        return f;
    };
    return forme_posee(d, {-34, -34, 0}, {34, 34, 78});
}

// La liste des vingt, dans l'ordre.
inline std::vector<std::pair<std::string, std::function<Maillage()>>> les_vingt() {
    return {
        {"v01_branche_horizontale", v01_branche_horizontale},
        {"v02_branche_raide", v02_branche_raide},
        {"v03_branche_tombante", v03_branche_tombante},
        {"v04_socle_et_bras", v04_socle_et_bras},
        {"v05_etoile", v05_etoile},
        {"v06_spirale", v06_spirale},
        {"v07_fourche_y", v07_fourche_y},
        {"v08_fourches_en_cascade", v08_fourches_en_cascade},
        {"v09_tronc_penche", v09_tronc_penche},
        {"v10_tronc_carre", v10_tronc_carre},
        {"v11_branche_carree", v11_branche_carree},
        {"v12_branche_en_s", v12_branche_en_s},
        {"v13_arche", v13_arche},
        {"v14_anse", v14_anse},
        {"v15_champignon", v15_champignon},
        {"v16_table", v16_table},
        {"v17_trompette", v17_trompette},
        {"v18_fin_et_gros", v18_fin_et_gros},
        {"v19_sucette", v19_sucette},
        {"v20_corail", v20_corail},
    };
}

}  // namespace essai
