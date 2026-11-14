// ArtPrint Slicer - licence AGPL v3
// Quarante modèles d'essai de plus pour les couches courbes, et une pièce
// « banc d'essai » qui réunit sur un même socle les difficultés classiques
// d'une imprimante (surplombs de plus en plus forts, pont, colonne fine,
// boule, pointe).
//
// Les quarante sont des familles : la même forme, avec un réglage qui change
// (l'inclinaison d'une branche, le nombre de branches, la grosseur du
// tronc...). On voit ainsi à partir de quelle valeur le slicer décroche.
#pragma once

#include <cmath>
#include <format>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "modeles_vingt.hpp"

namespace essai {

// Une branche droite sur un tronc rond : inclinaison, rayon du tronc, rayon de la branche.
inline Maillage une_branche(double inclinaison, double rayon_tronc, double rayon_branche, double longueur = 24.0) {
    const Distance d = [=](const Vec3& p) {
        const Vec3 pied{0, 0.17, 26.3};
        return fondre(tronc_simple(p, 46, rayon_tronc), boudin(p, pied, pied + vers(inclinaison, 0) * longueur, rayon_branche),
                      2.0);
    };
    return forme_posee(d, {-16, -16, 0}, {12.0 + longueur, 16, 58}, 0.66);
}

// Des branches réparties autour du tronc, au même niveau.
inline Maillage couronne(int nombre, double inclinaison) {
    const Distance d = [=](const Vec3& p) {
        double f = tronc_simple(p, 44, 6.0);
        for (int n = 0; n < nombre; ++n) {
            const Vec3 pied{0, 0, 25.2};
            f = fondre(f, boudin_conique(p, pied, pied + vers(inclinaison, 11.0 + 360.0 * n / nombre) * 22.0, 3.0, 2.3), 2.0);
        }
        return f;
    };
    return forme_posee(d, {-30, -30, 0}, {30, 30, 54}, 0.68);
}

// Une branche en arc de cercle : elle part à l'horizontale et tourne de « angle » degrés vers le haut.
inline Maillage branche_en_arc(double rayon_arc, double angle) {
    const Distance d = [=](const Vec3& p) {
        std::vector<Vec3> points;
        for (int n = 0; n <= 8; ++n) {
            const double a = en_radians(angle * n / 8.0);
            points.push_back({rayon_arc * std::sin(a), 0.2, 20.3 + rayon_arc * (1.0 - std::cos(a))});
        }
        return fondre(tronc_simple(p, 40, 5.5), chemin(p, points, 3.2, 2.6, 1.2), 2.0);
    };
    return forme_posee(d, {-14, -14, 0}, {34, 14, 60}, 0.66);
}

// Une fourche : le tronc se partage en deux bras écartés de « ecart » degrés de la verticale.
inline Maillage fourche(double ecart) {
    const Distance d = [=](const Vec3& p) {
        double f = tronc_simple(p, 24, 5.0);
        f = fondre(f, boudin_conique(p, {0, 0.1, 24}, Vec3{0, 0.1, 24} + vers(ecart, 0) * 26.0, 4.0, 3.0), 3.0);
        return fondre(f, boudin_conique(p, {0, 0.1, 24}, Vec3{0, 0.1, 24} + vers(ecart, 180) * 26.0, 4.0, 3.0), 3.0);
    };
    return forme_posee(d, {-32, -14, 0}, {32, 14, 56}, 0.66);
}

// Un arbre à deux étages : des branches, et sur chacune un rameau.
inline Maillage deux_etages(int nombre, double inclinaison_rameau) {
    const Distance d = [=](const Vec3& p) {
        double f = tronc_simple(p, 56, 6.0);
        for (int n = 0; n < nombre; ++n) {
            const double azimut = 20.0 + 360.0 * n / nombre;
            const Vec3 pied{0, 0, 22.0 + 7.0 * n};
            const Vec3 bout = pied + vers(60, azimut) * 22.0;
            f = fondre(f, boudin_conique(p, pied, bout, 3.2, 2.4), 2.0);
            const Vec3 milieu = pied + (bout - pied) * 0.55;
            f = fondre(f, boudin_conique(p, milieu, milieu + vers(inclinaison_rameau, azimut + 90.0) * 12.0, 2.0, 1.5), 1.5);
        }
        return f;
    };
    return forme_posee(d, {-38, -38, 0}, {38, 38, 74}, 0.7);
}

// Un tronc à six pans avec une branche sur une face et une sur une arête.
inline Maillage tronc_a_pans() {
    const Distance d = [](const Vec3& p) {
        double f = -1e9;
        for (int n = 0; n < 6; ++n) {
            const double a = en_radians(60.0 * n);
            f = std::max(f, p.x * std::cos(a) + p.y * std::sin(a) - 6.0);
        }
        f = std::max(f, p.z - 46.0);
        f = fondre(f, boudin(p, {0, 0.1, 30}, Vec3{0, 0.1, 30} + vers(60, 0) * 24.0, 3.0), 1.5);
        return fondre(f, boudin(p, {0, 0, 20}, Vec3{0, 0, 20} + vers(65, 150) * 22.0, 2.8), 1.5);
    };
    return forme_posee(d, {-28, -22, 0}, {30, 22, 50}, 0.66);
}

// Une branche terminée par une forme : boule, cône évasé ou palette plate.
inline Maillage branche_et_bout(int forme) {
    const Distance d = [=](const Vec3& p) {
        const Vec3 pied{0, 0.15, 24.2};
        const Vec3 bout = pied + vers(55, 0) * 22.0;
        double f = fondre(tronc_simple(p, 44, 5.5), boudin(p, pied, bout, 2.6), 2.0);
        if (forme == 0) {
            return fondre(f, boule(p, bout, 6.0), 1.5);
        }
        if (forme == 1) {
            return fondre(f, boudin_conique(p, bout, bout + vers(55, 0) * 9.0, 2.6, 7.0), 1.5);
        }
        return fondre(f, brique(p, bout + vers(55, 0) * 5.0, {6, 6, 1.6}, 0.6), 1.5);
    };
    return forme_posee(d, {-14, -14, 0}, {38, 14, 54}, 0.66);
}

// Deux troncs reliés par une traverse (un pont) à la hauteur donnée.
inline Maillage deux_troncs(double pente) {
    const Distance d = [=](const Vec3& p) {
        double f = boudin(p, {-14, 0, 0}, {-14, 0, 40}, 4.5);
        f = std::min(f, boudin(p, {14, 0, 0}, {14, 0, 40}, 4.5));
        return fondre(f, boudin(p, {-14, 0.1, 22.0}, {14, 0.1, 22.0 + 28.0 * std::tan(en_radians(pente))}, 3.0), 2.0);
    };
    return forme_posee(d, {-22, -8, 0}, {22, 8, 48}, 0.66);
}

// Une branche en hélice autour du tronc (elle tourne en montant).
inline Maillage helice(double tours) {
    const Distance d = [=](const Vec3& p) {
        std::vector<Vec3> points;
        for (int n = 0; n <= 16; ++n) {
            const double t = n / 16.0, a = 2.0 * std::numbers::pi * tours * t;
            const double r = 5.0 + 12.0 * std::min(1.0, 3.0 * t);
            points.push_back({r * std::cos(a), r * std::sin(a), 16.0 + 26.0 * t});
        }
        return fondre(tronc_simple(p, 48, 5.5), chemin(p, points, 3.0, 2.2, 1.2), 2.0);
    };
    return forme_posee(d, {-24, -24, 0}, {24, 24, 56}, 0.68);
}

// Le banc d'essai : un socle, et dessus cinq épreuves.
//  - un éventail : cinq branches de plus en plus couchées (30°, 45°, 60°, 75°, 90°) ;
//  - un pont entre deux piliers ;
//  - une colonne fine terminée par une boule ;
//  - une pointe (un cône) ;
//  - une branche courbe terminée par une coupelle.
inline Maillage banc_d_essai() {
    const Distance d = [](const Vec3& p) {
        double f = brique(p, {0, 0, 0.5}, {34, 22, 2.5}, 1.0);  // (le dessous arrondi est sous le plateau, coupé)
        // L'éventail, sur un mât.
        f = fondre(f, boudin(p, {-18, -4, 0}, {-18, -4, 62}, 5.0), 2.0);
        const double inclinaisons[5] = {30, 45, 60, 75, 90};
        for (int n = 0; n < 5; ++n) {
            const Vec3 pied{-18, -4, 15.0 + 9.5 * n};
            f = fondre(f, boudin_conique(p, pied, pied + vers(inclinaisons[n], 25.0 + 72.0 * n) * 15.0, 2.6, 2.0), 1.5);
        }
        // Le pont.
        f = fondre(f, boudin(p, {8, 12, 0}, {8, 12, 22}, 3.5), 1.5);
        f = fondre(f, boudin(p, {28, 12, 0}, {28, 12, 22}, 3.5), 1.5);
        f = fondre(f, brique(p, {18, 12, 21}, {12, 3.0, 1.8}, 0.6), 1.2);
        // La colonne fine et sa boule.
        f = fondre(f, boudin(p, {26, -12, 0}, {26, -12, 30}, 1.8), 1.5);
        f = fondre(f, boule(p, {26, -12, 33}, 5.0), 1.2);
        // La pointe.
        f = fondre(f, boudin_conique(p, {10, -12, 0}, {10, -12, 26}, 5.0, 0.6), 1.5);
        // La branche courbe et sa coupelle.
        const Vec3 depart{0, 4, 2};
        f = fondre(f, chemin(p, {depart, {0, 4, 12}, {2, 8, 20}, {6, 13, 26}, {6, 16, 34}}, 3.4, 2.6, 1.5), 1.5);
        f = fondre(f, palet(p, {6, 16, 36}, 5.5, 1.4) - 0.7, 1.5);
        return f;
    };
    return forme_posee(d, {-40, -28, 0}, {38, 26, 70}, 0.9);
}

// La liste des quarante, dans l'ordre.
inline std::vector<std::pair<std::string, std::function<Maillage()>>> les_quarante() {
    std::vector<std::pair<std::string, std::function<Maillage()>>> liste;
    int numero = 0;
    const auto ajouter = [&](const std::string& nom, std::function<Maillage()> fabriquer) {
        liste.emplace_back(std::format("w{:02d}_{}", ++numero, nom), std::move(fabriquer));
    };
    // Une branche, de plus en plus couchée (1 à 7).
    for (const double inclinaison : {35.0, 45.0, 55.0, 65.0, 75.0, 85.0, 100.0}) {
        ajouter(std::format("branche_{:.0f}", inclinaison), [=] { return une_branche(inclinaison, 5.0, 3.2); });
    }
    // Grosseur relative de la branche et du tronc (8 à 12).
    ajouter("branche_fine", [] { return une_branche(60, 6.0, 1.2); });
    ajouter("branche_grosse", [] { return une_branche(60, 5.0, 4.6); });
    ajouter("tronc_fin", [] { return une_branche(60, 3.0, 2.6); });
    ajouter("tronc_gros", [] { return une_branche(60, 9.0, 3.0); });
    ajouter("branche_longue", [] { return une_branche(65, 6.0, 3.0, 40.0); });
    // Couronnes : de deux à huit branches au même niveau (13 à 17).
    for (const int nombre : {2, 3, 4, 5, 8}) {
        ajouter(std::format("couronne_{}", nombre), [=] { return couronne(nombre, 62); });
    }
    // Couronnes couchées ou relevées (18, 19).
    ajouter("couronne_a_plat", [] { return couronne(4, 88); });
    ajouter("couronne_relevee", [] { return couronne(4, 40); });
    // Branches en arc (20 à 23).
    ajouter("arc_serre", [] { return branche_en_arc(10, 90); });
    ajouter("arc_large", [] { return branche_en_arc(20, 90); });
    ajouter("arc_demi_tour", [] { return branche_en_arc(12, 150); });
    ajouter("arc_leger", [] { return branche_en_arc(26, 45); });
    // Fourches plus ou moins ouvertes (24 à 27).
    for (const double ecart : {25.0, 40.0, 55.0, 70.0}) {
        ajouter(std::format("fourche_{:.0f}", ecart), [=] { return fourche(ecart); });
    }
    // Arbres à deux étages (28 à 30).
    ajouter("deux_etages_3", [] { return deux_etages(3, 30); });
    ajouter("deux_etages_4", [] { return deux_etages(4, 45); });
    ajouter("deux_etages_5", [] { return deux_etages(5, 20); });
    // Troncs et bouts particuliers (31 à 34).
    ajouter("tronc_a_pans", tronc_a_pans);
    ajouter("bout_boule", [] { return branche_et_bout(0); });
    ajouter("bout_evase", [] { return branche_et_bout(1); });
    ajouter("bout_palette", [] { return branche_et_bout(2); });
    // Ponts entre deux troncs (35 à 37).
    ajouter("pont_plat", [] { return deux_troncs(0); });
    ajouter("pont_en_pente", [] { return deux_troncs(20); });
    ajouter("pont_raide", [] { return deux_troncs(40); });
    // Hélices (38, 39).
    ajouter("helice_demi_tour", [] { return helice(0.5); });
    ajouter("helice_un_tour", [] { return helice(1.0); });
    // Et le banc d'essai (40).
    ajouter("banc_d_essai", banc_d_essai);
    return liste;
}

}  // namespace essai
