// ArtPrint Slicer - licence AGPL v3
// Tests du slicer 3 axes : découpe en couches, parois, remplissage, et
// G-code complet relu par le vérificateur. Les modèles sont fabriqués ici.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "maillage.hpp"
#include "slicer.hpp"
#include "trajectoires.hpp"
#include "tranchage.hpp"

using namespace artprint;

namespace {

int echecs = 0;

#define VERIFIER(condition)                                                     \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "ÉCHEC (ligne " << __LINE__ << ") : " #condition "\n"; \
            ++echecs;                                                           \
        }                                                                       \
    } while (0)

// Vérifie que le G-code est accepté ; sinon affiche les erreurs trouvées.
#define VERIFIER_ACCEPTE(rapport)                       \
    do {                                                \
        VERIFIER((rapport).valide());                   \
        for (const auto& erreur_ : (rapport).erreurs) { \
            std::cerr << decrire(erreur_);              \
        }                                               \
    } while (0)

bool proche(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

// Ajoute un rectangle (a, b, c, d dans le sens inverse des aiguilles d'une
// montre vu de l'extérieur) sous forme de deux triangles.
void quad(ConstructeurMaillage& m, Point3 a, Point3 b, Point3 c, Point3 d) {
    m.ajouter_triangle(a, b, c);
    m.ajouter_triangle(a, c, d);
}

// Ajoute un pavé plein.
void pave(ConstructeurMaillage& m, float x0, float y0, float z0, float x1, float y1, float z1) {
    quad(m, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0});  // dessous
    quad(m, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1});  // dessus
    quad(m, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1});  // devant
    quad(m, {x1, y1, z0}, {x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1});  // derrière
    quad(m, {x0, y1, z0}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1});  // gauche
    quad(m, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1});  // droite
}

Maillage cube(float cote, float hauteur) {
    ConstructeurMaillage m;
    pave(m, 0, 0, 0, cote, cote, hauteur);
    return m.terminer();
}

// Cadre carré de 30 mm percé d'un trou carré de 10 mm, haut de 6 mm.
Maillage cadre() {
    const float e = 15, i = 5, h = 6;
    ConstructeurMaillage m;
    // Murs extérieurs.
    quad(m, {-e, -e, 0}, {e, -e, 0}, {e, -e, h}, {-e, -e, h});
    quad(m, {e, -e, 0}, {e, e, 0}, {e, e, h}, {e, -e, h});
    quad(m, {e, e, 0}, {-e, e, 0}, {-e, e, h}, {e, e, h});
    quad(m, {-e, e, 0}, {-e, -e, 0}, {-e, -e, h}, {-e, e, h});
    // Murs du trou (tournés vers l'intérieur du trou).
    quad(m, {i, -i, 0}, {-i, -i, 0}, {-i, -i, h}, {i, -i, h});
    quad(m, {i, i, 0}, {i, -i, 0}, {i, -i, h}, {i, i, h});
    quad(m, {-i, i, 0}, {i, i, 0}, {i, i, h}, {-i, i, h});
    quad(m, {-i, -i, 0}, {-i, i, 0}, {-i, i, h}, {-i, -i, h});
    // Dessus et dessous : quatre trapèzes chacun.
    const Point3 ext[4] = {{-e, -e, 0}, {e, -e, 0}, {e, e, 0}, {-e, e, 0}};
    const Point3 inte[4] = {{-i, -i, 0}, {i, -i, 0}, {i, i, 0}, {-i, i, 0}};
    for (int k = 0; k < 4; ++k) {
        const int s = (k + 1) % 4;
        auto a = [&](Point3 p, float z) { p.z = z; return p; };
        quad(m, a(ext[k], h), a(ext[s], h), a(inte[s], h), a(inte[k], h));
        quad(m, a(ext[s], 0), a(ext[k], 0), a(inte[k], 0), a(inte[s], 0));
    }
    return m.terminer();
}

void test_coupe_du_cube() {
    const ResultatCoupe coupe = couper_maillage(cube(20, 20), {0.1, 10.0, 19.9, 25.0});
    VERIFIER(coupe.contours_ouverts == 0);
    VERIFIER(coupe.couches.size() == 4);
    for (int i = 0; i < 3; ++i) {
        VERIFIER(coupe.couches[i].size() == 1);
        VERIFIER(proche(surface(coupe.couches[i]), 400.0, 1e-6));
    }
    VERIFIER(coupe.couches[3].empty());  // au-dessus du cube : rien
}

void test_coupe_avec_trou() {
    const Maillage m = cadre();
    VERIFIER(verifier_fermeture(m).ferme && verifier_fermeture(m).orientation_coherente);
    const ResultatCoupe coupe = couper_maillage(m, {3.0});
    VERIFIER(coupe.contours_ouverts == 0);
    VERIFIER(coupe.couches[0].size() == 2);  // le bord et le trou
    VERIFIER(proche(surface(coupe.couches[0]), 900.0 - 100.0, 1e-6));
}

void test_coupe_volumes_superposes() {
    // Deux pavés qui se chevauchent : la coupe donne une seule forme.
    ConstructeurMaillage m;
    pave(m, 0, 0, 0, 10, 10, 10);
    pave(m, 5, 0, 0, 15, 10, 10);
    const ResultatCoupe coupe = couper_maillage(m.terminer(), {5.0});
    VERIFIER(coupe.couches[0].size() == 1);
    VERIFIER(proche(surface(coupe.couches[0]), 150.0, 1e-6));
}

// Colonne à huit côtés faite d'étages de 0,3 mm : beaucoup de plans de coupe
// tombent presque exactement sur une rangée de sommets. Chaque couche doit
// quand même donner un contour complet.
void test_coupe_sur_des_sommets() {
    const int cotes = 8, etages = 20;
    auto sommet = [&](int i, int j) {
        const double angle = 2.0 * 3.14159265358979323846 * (i % cotes) / cotes;
        return Point3{static_cast<float>(10.0 * std::cos(angle)),
                      static_cast<float>(10.0 * std::sin(angle)), static_cast<float>(0.3 * j)};
    };
    ConstructeurMaillage m;
    for (int i = 0; i < cotes; ++i) {
        for (int j = 0; j < etages; ++j) {
            quad(m, sommet(i, j), sommet(i + 1, j), sommet(i + 1, j + 1), sommet(i, j + 1));
        }
        m.ajouter_triangle({0, 0, 0}, sommet(i + 1, 0), sommet(i, 0));
        const float haut = static_cast<float>(0.3 * etages);
        m.ajouter_triangle({0, 0, haut}, sommet(i, etages), sommet(i + 1, etages));
    }
    const Maillage colonne = m.terminer();
    VERIFIER(verifier_fermeture(colonne).ferme);

    std::vector<double> hauteurs;
    for (double bas = 0.0; bas + 0.1 < 6.0; bas += 0.2) {
        hauteurs.push_back(bas + 0.1);
    }
    // Hauteurs exactement égales à celles des sommets, pour faire bonne mesure.
    for (int j = 1; j < etages; ++j) {
        hauteurs.push_back(static_cast<double>(static_cast<float>(0.3 * j)));
    }
    std::sort(hauteurs.begin(), hauteurs.end());

    const ResultatCoupe coupe = couper_maillage(colonne, hauteurs);
    VERIFIER(coupe.contours_ouverts == 0);
    const double attendu = 2.0 * std::sqrt(2.0) * 100.0;  // octogone de rayon 10
    for (const Polygones& couche : coupe.couches) {
        VERIFIER(couche.size() == 1);
        VERIFIER(proche(surface(couche), attendu, 0.01));
    }
}

void test_modele_ouvert() {
    Maillage m = cube(20, 20);
    m.triangles.pop_back();
    // Le contour ne se referme pas : il est signalé et ignoré.
    const ResultatCoupe coupe = couper_maillage(m, {10.0});
    VERIFIER(coupe.contours_ouverts >= 1);
    VERIFIER(coupe.couches[0].empty());
}

void test_lignes_de_remplissage() {
    // Carré de 10 mm, de 0,5 à 10,5 : les lignes tombent sur y = 1, 2, ... 10.
    const Polygones carre{{en_unites(Point2{0.5, 0.5}), en_unites(Point2{10.5, 0.5}),
                           en_unites(Point2{10.5, 10.5}), en_unites(Point2{0.5, 10.5})}};
    const auto chemins = lignes_de_remplissage(carre, 1.0, 0.0);
    // Un seul zigzag continu qui couvre tout le carré.
    VERIFIER(chemins.size() == 1);
    if (chemins.empty()) {
        return;
    }
    double longueur = 0.0;
    for (std::size_t i = 1; i < chemins[0].size(); ++i) {
        longueur += distance(chemins[0][i - 1], chemins[0][i]);
    }
    // 10 lignes de 10 mm reliées par 9 raccords de 1 mm.
    VERIFIER(proche(longueur, 109.0, 0.01));
    for (const Point2& p : chemins[0]) {
        VERIFIER(p.x > 0.5 - 1e-6 && p.x < 10.5 + 1e-6 && p.y > 0.5 && p.y < 10.5);
    }
    // Zone vide : aucun chemin.
    VERIFIER(lignes_de_remplissage({}, 1.0, 45.0).empty());
}

void test_parois_du_cube() {
    ReglagesImpression r;
    r.jupe_boucles = 0;
    const ResultatCoupe coupe = couper_maillage(cube(20, 20), {0.1, 0.3});
    const auto couches = calculer_trajectoires(coupe.couches, r);
    VERIFIER(couches.size() == 2);
    VERIFIER(proche(couches[0].z, 0.2, 1e-9) && proche(couches[1].z, 0.4, 1e-9));

    // Les parois viennent en premier : interne, puis externe.
    const auto& traits = couches[0].traits;
    VERIFIER(traits.size() > 2);
    if (traits.size() < 2) {
        return;
    }
    VERIFIER(traits[0].type == TypeTrait::paroi_interne && traits[0].ferme);
    VERIFIER(traits[1].type == TypeTrait::paroi_externe && traits[1].ferme);
    // L'axe de la paroi externe est à une demi-largeur du bord : carré de
    // 20 - 0,45 mm de côté.
    double longueur = 0.0;
    const auto& points = traits[1].points;
    for (std::size_t i = 0; i < points.size(); ++i) {
        longueur += distance(points[i], points[(i + 1) % points.size()]);
    }
    VERIFIER(proche(longueur, 4.0 * (20.0 - 0.45), 0.01));
    // La couture est à l'arrière (Y le plus grand).
    for (const Point2& p : points) {
        VERIFIER(p.y <= points[0].y + 1e-9);
    }
    // Première couche d'un cube : tout l'intérieur est plein.
    for (const Trait& trait : traits) {
        VERIFIER(trait.type != TypeTrait::remplissage);
    }
}

void test_trancher_cube() {
    const ResultatTranchage resultat = trancher(cube(20, 20), Reglages{});
    VERIFIER_ACCEPTE(resultat.verification);
    VERIFIER(resultat.couches == 100);
    VERIFIER(resultat.gcode.find(";COUCHE:100\n") != std::string::npos);
    VERIFIER(resultat.gcode.find(";COUCHE:101\n") == std::string::npos);
    // Contrat du G-code : première ligne, macros du firmware, et les cinq
    // axes sur chaque ligne de mouvement (buse verticale, plateau immobile).
    VERIFIER(resultat.gcode.rfind("; CONTRAT_GCODE: 1\n", 0) == 0);
    VERIFIER(resultat.gcode.find("\nARTPRINT_PRINT_START BUSE=210 PLATEAU=0\n") != std::string::npos);
    VERIFIER(resultat.gcode.find("\nARTPRINT_PRINT_END\n") != std::string::npos);
    VERIFIER(resultat.gcode.find("G28") == std::string::npos && resultat.gcode.find("M104") == std::string::npos &&
             resultat.gcode.find("M84") == std::string::npos && resultat.gcode.find("G92") == std::string::npos);
    {
        std::istringstream lignes(resultat.gcode);
        std::size_t mouvements = 0, complets = 0;
        for (std::string ligne; std::getline(lignes, ligne);) {
            if (ligne.rfind("G0 ", 0) == 0 || (ligne.rfind("G1 ", 0) == 0 && ligne.find(" X") != std::string::npos)) {
                ++mouvements;
                complets += ligne.find(" X") != std::string::npos && ligne.find(" Y") != std::string::npos &&
                            ligne.find(" Z") != std::string::npos && ligne.find(" B0.000 C0.000") != std::string::npos;
            }
        }
        VERIFIER(mouvements > 1000 && complets == mouvements);
    }
    VERIFIER(resultat.gcode.find(";TYPE:paroi externe\n") != std::string::npos);
    VERIFIER(resultat.gcode.find(";TYPE:remplissage\n") != std::string::npos);
    // Un cube de 20 mm à 15 % de remplissage consomme environ 1,1 m de filament.
    VERIFIER(resultat.longueur_filament > 900.0 && resultat.longueur_filament < 1400.0);
    VERIFIER(resultat.duree_estimee > 0.0);
    // Autant de rétractions que de déplacements à vide après le premier.
    VERIFIER(resultat.verification.retractions > 100);
}

void test_trancher_piece_percee() {
    const ResultatTranchage resultat = trancher(cadre(), Reglages{});
    VERIFIER_ACCEPTE(resultat.verification);
    VERIFIER(resultat.couches == 30);
}

void test_densites_extremes() {
    Reglages reglages;
    reglages.impression.densite_remplissage = 100.0;
    const ResultatTranchage plein = trancher(cube(10, 4), reglages);
    VERIFIER_ACCEPTE(plein.verification);
    VERIFIER(plein.gcode.find(";TYPE:remplissage\n") == std::string::npos);

    reglages.impression.densite_remplissage = 0.0;
    const ResultatTranchage creux = trancher(cube(10, 4), reglages);
    VERIFIER_ACCEPTE(creux.verification);
    VERIFIER(creux.longueur_filament < plein.longueur_filament);
}

void test_autres_reglages() {
    // Couches épaisses, première couche différente, trois parois, pas de levée.
    Reglages reglages;
    reglages.impression.hauteur_couche = 0.3;
    reglages.impression.hauteur_premiere_couche = 0.25;
    reglages.impression.nombre_parois = 3;
    reglages.impression.levee_z = 0.0;
    reglages.impression.jupe_boucles = 2;
    reglages.machine.centre_plateau_x = 150.0;
    reglages.machine.centre_plateau_y = 150.0;
    const ResultatTranchage resultat = trancher(cadre(), reglages);
    VERIFIER_ACCEPTE(resultat.verification);
}

bool refuse(const Maillage& m, const Reglages& reglages, const char* attendu) {
    try {
        trancher(m, reglages);
    } catch (const ErreurTranchage& erreur) {
        return std::string(erreur.what()).find(attendu) != std::string::npos;
    }
    return false;
}

void test_refus() {
    Maillage ouvert = cube(20, 20);
    ouvert.triangles.pop_back();
    VERIFIER(refuse(ouvert, Reglages{}, "volume fermé"));
    VERIFIER(refuse(cube(250, 10), Reglages{}, "dépasse du plateau"));
    VERIFIER(refuse(cube(20, 300), Reglages{}, "trop haut"));
    VERIFIER(refuse(Maillage{}, Reglages{}, "aucun triangle"));
    VERIFIER(refuse(cube(20, 0.05f), Reglages{}, "plus fin qu'une couche"));

    // Deux cubes l'un au-dessus de l'autre sans se toucher : celui du haut flotte.
    ConstructeurMaillage flottant;
    pave(flottant, 0, 0, 0, 10, 10, 5);
    pave(flottant, 0, 0, 8, 10, 10, 12);
    VERIFIER(refuse(flottant.terminer(), Reglages{}, "flotte en l'air"));
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"coupe d'un cube", test_coupe_du_cube},
        {"coupe d'une pièce percée", test_coupe_avec_trou},
        {"coupe de volumes superposés", test_coupe_volumes_superposes},
        {"coupe au ras des sommets", test_coupe_sur_des_sommets},
        {"coupe d'un modèle ouvert", test_modele_ouvert},
        {"lignes de remplissage", test_lignes_de_remplissage},
        {"parois d'un cube", test_parois_du_cube},
        {"tranchage complet d'un cube", test_trancher_cube},
        {"tranchage d'une pièce percée", test_trancher_piece_percee},
        {"remplissage à 0 % et à 100 %", test_densites_extremes},
        {"autres réglages", test_autres_reglages},
        {"modèles refusés", test_refus},
    };

    for (const auto& [nom, test] : tests) {
        const int avant = echecs;
        try {
            test();
        } catch (const std::exception& erreur) {
            std::cerr << "ÉCHEC : exception inattendue : " << erreur.what() << '\n';
            ++echecs;
        }
        std::cout << (echecs == avant ? "[OK]    " : "[ÉCHEC] ") << nom << '\n';
    }

    if (echecs == 0) {
        std::cout << "Tous les tests du slicer sont passés.\n";
        return 0;
    }
    std::cout << echecs << " vérification(s) en échec.\n";
    return 1;
}
