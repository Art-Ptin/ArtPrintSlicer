// ArtPrint Slicer - licence AGPL v3
// Objets d'essai « organiques » : des formes lisses, d'une seule pièce,
// fabriquées à partir de boules et de boudins fondus les uns dans les autres.
//
// Chaque forme est décrite par une fonction « distance à la surface »
// (négative dans la matière). La surface est ensuite extraite sur une grille :
// chaque petit cube est découpé en six tétraèdres, et chaque tétraèdre
// traversé par la surface donne un ou deux triangles. Les sommets sont
// partagés entre voisins : le maillage obtenu est toujours un volume fermé.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <unordered_map>
#include <vector>

#include "geometrie.hpp"
#include "maillage.hpp"

namespace essai {

using namespace artprint;

using Distance = std::function<double(const Vec3&)>;

// --- Formes de base ---------------------------------------------------------------

inline double boule(const Vec3& p, const Vec3& centre, double rayon) { return norme(p - centre) - rayon; }

// Boudin : tous les points à moins de « rayon » du segment [a, b].
inline double boudin(const Vec3& p, const Vec3& a, const Vec3& b, double rayon) {
    const Vec3 ab = b - a;
    const double t = std::clamp(produit_scalaire(p - a, ab) / produit_scalaire(ab, ab), 0.0, 1.0);
    return norme(p - (a + ab * t)) - rayon;
}

// Boudin dont le rayon change d'un bout à l'autre (une branche qui s'affine).
inline double boudin_conique(const Vec3& p, const Vec3& a, const Vec3& b, double rayon_a, double rayon_b) {
    const Vec3 ab = b - a;
    const double t = std::clamp(produit_scalaire(p - a, ab) / produit_scalaire(ab, ab), 0.0, 1.0);
    return norme(p - (a + ab * t)) - (rayon_a + (rayon_b - rayon_a) * t);
}

// Disque épais aux bords arrondis, couché à plat (un palet).
inline double palet(const Vec3& p, const Vec3& centre, double rayon, double demi_epaisseur) {
    const double horizontal = std::hypot(p.x - centre.x, p.y - centre.y) - rayon;
    const double vertical = std::abs(p.z - centre.z) - demi_epaisseur;
    return std::min(std::max(horizontal, vertical), 0.0) +
           std::hypot(std::max(horizontal, 0.0), std::max(vertical, 0.0));
}

// Anneau (tore) dans un plan vertical contenant l'axe X.
inline double anneau_debout(const Vec3& p, const Vec3& centre, double grand_rayon, double petit_rayon) {
    const double dans_le_plan = std::hypot(p.x - centre.x, p.z - centre.z) - grand_rayon;
    return std::hypot(dans_le_plan, p.y - centre.y) - petit_rayon;
}

// Réunion de deux formes, avec un raccord arrondi de largeur « fondu ».
inline double fondre(double a, double b, double fondu) {
    const double h = std::clamp(0.5 + 0.5 * (b - a) / fondu, 0.0, 1.0);
    return b + (a - b) * h - fondu * h * (1.0 - h);
}

// Direction donnée par son inclinaison (0 = verticale) et son angle vu de dessus, en degrés.
inline Vec3 vers(double inclinaison, double azimut) {
    const double i = en_radians(inclinaison), a = en_radians(azimut);
    return {std::sin(i) * std::cos(a), std::sin(i) * std::sin(a), std::cos(i)};
}

// --- Extraction de la surface -------------------------------------------------------

// « mini » et « maxi » : la boîte à explorer ; « pas » : la finesse de la grille.
inline Maillage surface(const Distance& distance, const Vec3& mini, const Vec3& maxi, double pas) {
    const int nx = static_cast<int>(std::ceil((maxi.x - mini.x) / pas)) + 1;
    const int ny = static_cast<int>(std::ceil((maxi.y - mini.y) / pas)) + 1;
    const int nz = static_cast<int>(std::ceil((maxi.z - mini.z) / pas)) + 1;
    const auto indice = [&](int i, int j, int k) {
        return (static_cast<std::uint64_t>(k) * (ny + 1) + j) * (nx + 1) + i;
    };
    const auto position = [&](int i, int j, int k) {
        return Vec3{mini.x + i * pas, mini.y + j * pas, mini.z + k * pas};
    };
    std::vector<float> valeurs(static_cast<std::size_t>(nx + 1) * (ny + 1) * (nz + 1));
    for (int k = 0; k <= nz; ++k) {
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                double v = distance(position(i, j, k));
                if (std::abs(v) < 1e-7) {
                    v = -1e-7;  // jamais pile sur la surface
                }
                valeurs[indice(i, j, k)] = static_cast<float>(v);
            }
        }
    }

    Maillage maillage;
    std::unordered_map<std::uint64_t, std::uint32_t> sommets_d_arete;
    struct Coin {
        std::uint64_t numero;
        Vec3 point;
        double valeur;
    };
    const auto sur_l_arete = [&](const Coin& a, const Coin& b) {
        const Coin& bas = a.numero < b.numero ? a : b;
        const Coin& haut = a.numero < b.numero ? b : a;
        // Clé de l'arête : les numéros de ses deux coins, côte à côte.
        const std::uint64_t utilisee = (bas.numero << 32) | haut.numero;
        const auto trouve = sommets_d_arete.find(utilisee);
        if (trouve != sommets_d_arete.end()) {
            return trouve->second;
        }
        const double t = bas.valeur / (bas.valeur - haut.valeur);
        const Vec3 p = bas.point + (haut.point - bas.point) * t;
        const auto numero = static_cast<std::uint32_t>(maillage.sommets.size());
        maillage.sommets.push_back({static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
        sommets_d_arete.emplace(utilisee, numero);
        return numero;
    };
    // Un sommet de la surface : son numéro, et le milieu de l'arête qui le
    // porte. Le sens d'un triangle est décidé avec ces milieux : le résultat
    // est le même qu'avec les vrais points, mais il reste sûr même quand le
    // triangle est minuscule.
    struct Sommet {
        std::uint32_t numero;
        Vec3 milieu;
    };
    const auto sommet = [&](const Coin& a, const Coin& b) {
        return Sommet{sur_l_arete(a, b), (a.point + b.point) * 0.5};
    };
    // Ajoute un triangle tourné vers l'extérieur (à l'opposé du coin « dedans »).
    const auto triangle = [&](Sommet a, Sommet b, Sommet c, const Vec3& dedans) {
        if (a.numero == b.numero || b.numero == c.numero || a.numero == c.numero) {
            return;
        }
        const Vec3 normale = produit_vectoriel(b.milieu - a.milieu, c.milieu - a.milieu);
        if (produit_scalaire(normale, a.milieu - dedans) < 0.0) {
            std::swap(b, c);
        }
        maillage.triangles.push_back({{a.numero, b.numero, c.numero}});
    };

    // Les six tétraèdres d'un cube, autour de sa grande diagonale (coins 0 et 6).
    static const int coins[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                                    {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    static const int tetraedres[6][4] = {{0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6},
                                         {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}};
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                Coin cube[8];
                bool dedans = false, dehors = false;
                for (int c = 0; c < 8; ++c) {
                    const int a = i + coins[c][0], b = j + coins[c][1], d = k + coins[c][2];
                    cube[c] = {indice(a, b, d), position(a, b, d), valeurs[indice(a, b, d)]};
                    (cube[c].valeur < 0.0 ? dedans : dehors) = true;
                }
                if (!dedans || !dehors) {
                    continue;
                }
                for (const auto& t : tetraedres) {
                    std::array<const Coin*, 4> interieurs{}, exterieurs{};
                    int ni = 0, ne = 0;
                    for (int c = 0; c < 4; ++c) {
                        const Coin& coin = cube[t[c]];
                        (coin.valeur < 0.0 ? interieurs[ni++] : exterieurs[ne++]) = &coin;
                    }
                    if (ni == 0 || ni == 4) {
                        continue;
                    }
                    const Vec3 point_dedans = interieurs[0]->point;
                    if (ni == 1) {
                        triangle(sommet(*interieurs[0], *exterieurs[0]), sommet(*interieurs[0], *exterieurs[1]),
                                 sommet(*interieurs[0], *exterieurs[2]), point_dedans);
                    } else if (ni == 3) {
                        // Le coin intérieur de référence : le centre des trois.
                        const Vec3 centre = (interieurs[0]->point + interieurs[1]->point + interieurs[2]->point) * (1.0 / 3.0);
                        triangle(sommet(*exterieurs[0], *interieurs[0]), sommet(*exterieurs[0], *interieurs[1]),
                                 sommet(*exterieurs[0], *interieurs[2]), centre);
                    } else {
                        // Deux coins dedans, deux dehors : la surface est un quadrilatère.
                        const Vec3 centre = (interieurs[0]->point + interieurs[1]->point) * 0.5;
                        const Sommet a = sommet(*interieurs[0], *exterieurs[0]);
                        const Sommet b = sommet(*interieurs[0], *exterieurs[1]);
                        const Sommet c = sommet(*interieurs[1], *exterieurs[1]);
                        const Sommet d = sommet(*interieurs[1], *exterieurs[0]);
                        triangle(a, b, c, centre);
                        triangle(a, c, d, centre);
                    }
                }
            }
        }
    }
    return maillage;
}

// Fabrique une forme posée à plat : tout ce qui est sous la hauteur zéro est retiré.
inline Maillage forme_posee(const Distance& distance, const Vec3& mini, const Vec3& maxi, double pas = 0.7) {
    const Distance coupee = [&](const Vec3& p) { return std::max(distance(p), -p.z); };
    return surface(coupee, {mini.x, mini.y, -pas * 1.5}, maxi, pas);
}

// --- Les objets -------------------------------------------------------------------

// Arbre : un tronc qui s'affine, trois branches et une brindille sur l'une d'elles.
inline Maillage arbre_organique() {
    const Vec3 a{0, 0, 24}, b{0, 0, 40}, c{0, 0, 52};
    const Vec3 bout_a = a + vers(62, 20) * 30.0;
    const Distance d = [=](const Vec3& p) {
        double f = boudin_conique(p, {0, 0, -2}, {0, 0, 70}, 7.0, 4.0);
        f = fondre(f, boudin_conique(p, a, bout_a, 4.0, 2.8), 3.0);
        f = fondre(f, boudin_conique(p, a + vers(62, 20) * 16.0, a + vers(62, 20) * 16.0 + vers(20, 60) * 16.0, 2.6, 2.0), 2.0);
        f = fondre(f, boudin_conique(p, b, b + vers(70, 150) * 26.0, 3.6, 2.6), 3.0);
        f = fondre(f, boudin_conique(p, c, c + vers(50, 270) * 22.0, 3.2, 2.4), 3.0);
        return f;
    };
    return forme_posee(d, {-34, -30, 0}, {36, 26, 76});
}

// Châssis de drone « organique » : un corps central bombé et quatre bras qui
// montent vers l'extérieur, terminés par un support de moteur.
inline Maillage chassis_drone() {
    const Distance d = [](const Vec3& p) {
        double f = palet(p, {0, 0, 6}, 16.0, 5.0) - 1.5;
        for (int bras = 0; bras < 4; ++bras) {
            const double azimut = 45.0 + 90.0 * bras;
            const Vec3 depart = Vec3{0, 0, 7} + vers(90, azimut) * 12.0;
            const Vec3 bout = depart + vers(62, azimut) * 44.0;
            f = fondre(f, boudin_conique(p, depart, bout, 4.2, 3.4), 3.5);
            f = fondre(f, palet(p, bout + Vec3{0, 0, 1.5}, 7.0, 2.0) - 1.0, 2.0);
        }
        return f;
    };
    return forme_posee(d, {-56, -56, 0}, {56, 56, 36}, 0.8);
}

// Chandelier : un pied, une tige et trois bras courbes qui partent de côté
// puis se redressent, chacun terminé par une coupelle.
inline Maillage chandelier() {
    const Distance d = [](const Vec3& p) {
        double f = palet(p, {0, 0, 2.5}, 15.0, 1.5) - 1.0;
        f = fondre(f, boudin(p, {0, 0, 2}, {0, 0, 44}, 4.0), 3.0);
        for (int bras = 0; bras < 3; ++bras) {
            const double azimut = 30.0 + 120.0 * bras;
            // Arc de cercle : part à l'horizontale, finit à la verticale.
            Vec3 precedent = Vec3{0, 0, 26};
            for (int pas = 1; pas <= 6; ++pas) {
                const double angle = en_radians(90.0 * pas / 6.0);
                const Vec3 point = Vec3{0, 0, 26} + vers(90, azimut) * (22.0 * std::sin(angle)) +
                                   Vec3{0, 0, 22.0 * (1.0 - std::cos(angle))};
                f = fondre(f, boudin(p, precedent, point, 2.8), 1.5);
                precedent = point;
            }
            f = fondre(f, palet(p, precedent + Vec3{0, 0, 2.0}, 5.5, 1.6) - 0.8, 1.5);
        }
        return f;
    };
    return forme_posee(d, {-32, -32, 0}, {32, 32, 56});
}

// Crochet : une tige verticale qui se courbe et redescend.
inline Maillage crochet() {
    const Distance d = [](const Vec3& p) {
        double f = palet(p, {0, 0, 2.0}, 11.0, 1.2) - 0.8;
        f = fondre(f, boudin(p, {0, 0, 1}, {0, 0, 34}, 3.6), 2.5);
        Vec3 precedent{0, 0, 34};
        for (int pas = 1; pas <= 8; ++pas) {
            const double angle = en_radians(200.0 * pas / 8.0);
            const Vec3 point{12.0 * (1.0 - std::cos(angle)), 0.0, 34.0 + 12.0 * std::sin(angle)};
            f = fondre(f, boudin(p, precedent, point, 3.2), 1.0);
            precedent = point;
        }
        return f;
    };
    return forme_posee(d, {-14, -14, 0}, {30, 14, 52});
}

// Tuyau en Y, creux : un tube vertical qui se sépare en deux.
inline Maillage tuyau_en_y() {
    const auto tubes = [](const Vec3& p, double rayon) {
        double f = boudin(p, {0, 0, -4}, {0, 0, 26}, rayon);
        f = fondre(f, boudin(p, {0, 0, 26}, Vec3{0, 0, 26} + vers(48, 0) * 34.0, rayon), 2.0);
        f = fondre(f, boudin(p, {0, 0, 26}, Vec3{0, 0, 26} + vers(48, 180) * 34.0, rayon), 2.0);
        return f;
    };
    const Distance d = [=](const Vec3& p) {
        // La paroi : le gros tube moins le petit ; les bouts sont ouverts par deux plans.
        double f = std::max(tubes(p, 7.0), -tubes(p, 4.6));
        for (const double azimut : {0.0, 180.0}) {
            const Vec3 axe = vers(48, azimut);
            f = std::max(f, produit_scalaire(p - (Vec3{0, 0, 26} + axe * 30.0), axe));
        }
        return f;
    };
    return forme_posee(d, {-36, -10, 0}, {36, 10, 56}, 0.6);
}

// Champignon : un pied et un chapeau large, en surplomb tout autour.
inline Maillage champignon() {
    const Distance d = [](const Vec3& p) {
        double f = boudin(p, {0, 0, -2}, {0, 0, 22}, 5.0);
        // Chapeau : une demi-boule aplatie posée sur le pied.
        const Vec3 q{p.x, p.y, (p.z - 22.0) * 1.8};
        f = fondre(f, std::max(norme(q) - 18.0, 22.0 - p.z), 1.5);
        return f;
    };
    return forme_posee(d, {-20, -20, 0}, {20, 20, 34});
}

// Tasse : un corps plein et une anse. L'anse forme une boucle.
inline Maillage tasse() {
    const Distance d = [](const Vec3& p) {
        double f = boudin(p, {0, 0, 8}, {0, 0, 30}, 12.0);
        f = fondre(f, anneau_debout(p, {14, 0, 20}, 9.0, 2.6), 1.5);
        return f;
    };
    return forme_posee(d, {-14, -14, 0}, {28, 14, 44});
}

// Main : une paume et cinq doigts en éventail.
inline Maillage main_ouverte() {
    const Distance d = [](const Vec3& p) {
        double f = boudin(p, {0, 0, -2}, {0, 0, 18}, 8.0);
        f = fondre(f, palet(p, {0, 0, 24}, 12.0, 4.0) - 2.0, 4.0);
        const double inclinaisons[5] = {75.0, 35.0, 8.0, 30.0, 55.0};
        const double azimuts[5] = {0.0, 0.0, 0.0, 180.0, 180.0};
        for (int doigt = 0; doigt < 5; ++doigt) {
            const Vec3 depart = Vec3{0, 0, 26} + vers(90, azimuts[doigt]) * (std::sin(en_radians(inclinaisons[doigt])) * 9.0);
            f = fondre(f, boudin_conique(p, depart, depart + vers(inclinaisons[doigt], azimuts[doigt]) * 26.0, 3.2, 2.4), 2.0);
        }
        return f;
    };
    return forme_posee(d, {-38, -16, 0}, {40, 16, 58});
}

// Équerre technique : une semelle, un montant et un bossage incliné.
inline Maillage equerre() {
    const Distance d = [](const Vec3& p) {
        const auto pave = [&](const Vec3& centre, const Vec3& demi) {
            const Vec3 q{std::abs(p.x - centre.x) - demi.x, std::abs(p.y - centre.y) - demi.y, std::abs(p.z - centre.z) - demi.z};
            return std::min(std::max({q.x, q.y, q.z}), 0.0) +
                   norme({std::max(q.x, 0.0), std::max(q.y, 0.0), std::max(q.z, 0.0)}) - 0.8;
        };
        double f = pave({0, 0, 2.5}, {20, 12, 1.7});
        f = std::min(f, pave({-16, 0, 20}, {3.2, 11, 19}));
        f = fondre(f, boudin(p, {-14, 0, 26}, Vec3{-14, 0, 26} + vers(55, 0) * 26.0, 5.0), 2.0);
        return f;
    };
    return forme_posee(d, {-24, -16, 0}, {22, 16, 48}, 0.6);
}

// Arbre à deux niveaux : une grosse branche de côté, qui porte elle-même une
// branche repartant dans une autre direction.
inline Maillage arbre_double() {
    const Vec3 depart{0, 0, 26};
    const Vec3 coude = depart + vers(65, 0) * 30.0;
    const Distance d = [=](const Vec3& p) {
        double f = boudin_conique(p, {0, 0, -2}, {0, 0, 62}, 7.0, 5.0);
        f = fondre(f, boudin_conique(p, depart, coude, 4.5, 3.6), 3.0);
        f = fondre(f, boudin_conique(p, coude, coude + vers(15, 200) * 24.0, 3.4, 2.6), 2.0);
        f = fondre(f, boudin_conique(p, coude, coude + vers(80, 60) * 20.0, 3.0, 2.4), 2.0);
        return f;
    };
    return forme_posee(d, {-14, -12, 0}, {44, 28, 68});
}

// Buisson : six branches autour du tronc, à deux hauteurs, dans toutes les directions.
inline Maillage buisson() {
    const Distance d = [](const Vec3& p) {
        double f = boudin_conique(p, {0, 0, -2}, {0, 0, 58}, 7.5, 5.5);
        for (int branche = 0; branche < 6; ++branche) {
            const double azimut = 60.0 * branche + (branche % 2 ? 15.0 : 0.0);
            const Vec3 depart{0, 0, branche % 2 ? 40.0 : 24.0};
            f = fondre(f, boudin_conique(p, depart, depart + vers(branche % 2 ? 58.0 : 68.0, azimut) * 26.0, 3.4, 2.6), 2.5);
        }
        return f;
    };
    return forme_posee(d, {-34, -34, 0}, {34, 34, 64});
}

// Potence : un mât et un bras tout à fait horizontal.
inline Maillage potence() {
    const Distance d = [](const Vec3& p) {
        double f = palet(p, {0, 0, 2.0}, 13.0, 1.2) - 0.8;
        f = fondre(f, boudin(p, {0, 0, 1}, {0, 0, 52}, 5.0), 2.5);
        f = fondre(f, boudin(p, {0, 0, 44}, {34, 0, 44}, 3.6), 2.0);
        return f;
    };
    return forme_posee(d, {-16, -16, 0}, {40, 16, 60});
}

// Tube courbe : il part du plateau à la verticale, puis se couche peu à peu
// jusqu'à l'inclinaison donnée, en suivant un arc de cercle.
struct TubeCourbe {
    double rayon_tube = 5.0;
    double rayon_courbure = 30.0;
    double hauteur_droite = 6.0;   // partie verticale avant la courbe
    double inclinaison_finale = 70.0;

    // Point de l'axe et direction, après avoir tourné de « angle » degrés.
    Vec3 point(double angle) const {
        const double a = en_radians(angle);
        return {rayon_courbure * (1.0 - std::cos(a)), 0.0, hauteur_droite + rayon_courbure * std::sin(a)};
    }
    Vec3 direction(double angle) const { return vers(angle, 0.0); }

    Maillage maillage() const {
        const TubeCourbe tube = *this;
        const Distance d = [tube](const Vec3& p) {
            double f = boudin(p, {0, 0, -3}, {0, 0, tube.hauteur_droite}, tube.rayon_tube);
            Vec3 precedent = tube.point(0.0);
            for (double angle = 5.0; angle <= tube.inclinaison_finale + 1e-9; angle += 5.0) {
                const Vec3 suivant = tube.point(angle);
                f = std::min(f, boudin(p, precedent, suivant, tube.rayon_tube));
                precedent = suivant;
            }
            return f;
        };
        const Vec3 bout = point(inclinaison_finale);
        return forme_posee(d, {-rayon_tube - 2, -rayon_tube - 2, 0},
                           {bout.x + rayon_tube + 2, rayon_tube + 2, bout.z + rayon_tube + 2}, 0.6);
    }
};

}  // namespace essai
