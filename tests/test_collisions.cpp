// ArtPrint Slicer - licence AGPL v3
// Tests de la détection de collisions. La machine utilisée ici est inventée
// pour les tests, faite de simples blocs : ce n'est pas la vraie imprimante.
//
//   - plateau : dalle de 300 mm, dessus à z = 0 ;
//   - tête : une buse fine (z 0 à 6) sous un bloc chauffant (z 6 à 30),
//     pivot B à 40 mm au-dessus de la pointe ;
//   - bras : barre qui part de la tête vers +X, à 34 mm au-dessus de la pointe ;
//   - module Z : bloc à droite, guidé par une colonne du bâti ;
//   - bâti : la colonne, et une butée haute au-dessus du module Z.
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <utility>

#include "champ_distance.hpp"
#include "collisions.hpp"
#include "machine.hpp"
#include "verificateur.hpp"

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

// Un pavé sous forme de 12 triangles.
PieceMachine pave(const char* nom, float x0, float y0, float z0, float x1, float y1, float z1) {
    PieceMachine piece;
    piece.nom = nom;
    const float s[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                           {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    const int faces[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                              {2, 3, 7}, {2, 7, 6}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    for (const auto& face : faces) {
        for (const int sommet : face) {
            piece.triangles.insert(piece.triangles.end(), s[sommet], s[sommet] + 3);
        }
    }
    return piece;
}

ModeleMachine machine_de_test() {
    ModeleMachine m;
    m.groupes.push_back({"bati", Mouvement::fixe,
                         {pave("colonne", 200, -60, -20, 240, -45, 400),
                          pave("butée haute", 180, -40, 300, 260, 40, 310)}});
    m.groupes.push_back({"chariot_y", Mouvement::chariot_y,
                         {pave("moteur C", -30, -30, -60, 30, 30, -10)}});
    m.groupes.push_back({"plateau", Mouvement::plateau,
                         {pave("plateau", -150, -150, -5, 150, 150, 0)}});
    // Le module Z touche la colonne (y = -45 pour les deux) : c'est son guidage.
    m.groupes.push_back({"axe_z", Mouvement::axe_z,
                         {pave("module Z", 180, -45, 20, 260, 40, 80)}});
    m.groupes.push_back({"chariot_x", Mouvement::chariot_x,
                         {pave("profilé du bras", -10, -10, 34, 300, 10, 54)}});
    m.groupes.push_back({"tete", Mouvement::tete,
                         {pave("buse", -1, -1, 0, 1, 1, 6),
                          pave("bloc chauffant", -8, -6, 6, 8, 6, 30)}});
    m.pivot_b = {0, 0, 40};
    m.direction_pivot = {0, 1, 0};
    m.pointe_buse = {0, 0, 0};
    return m;
}

ReglagesMachine reglages_de_test() {
    ReglagesMachine r;
    r.distance_pivot_buse = 40.0;
    return r;
}

// Dépose une colonne de matière verticale (un point par millimètre).
void colonne_de_matiere(DetecteurCollisions& d, double x, double y, double hauteur) {
    for (double z = 0.5; z < hauteur; z += 1.0) {
        d.deposer({x, y, z});
    }
}

void test_champ_de_distance() {
    GroupeMachine groupe{"g", Mouvement::tete, {pave("cube", 0, 0, 0, 10, 10, 10)}};
    const ChampDistance champ(groupe, 0.5, 8.0);
    // Au centre du cube : dedans, distance nulle.
    VERIFIER(champ.lire({5, 5, 5}).distance == 0.0);
    // À 3 mm d'une face, à la demi-finesse près.
    VERIFIER(std::abs(champ.lire({13, 5, 5}).distance - 3.0) < 0.5);
    VERIFIER(std::abs(champ.lire({5, 5, -4}).distance - 4.0) < 0.5);
    // Dans le prolongement d'un coin : distance en diagonale.
    VERIFIER(std::abs(champ.lire({13, 14, 5}).distance - 5.0) < 0.6);
    // Très loin : au moins la portée.
    VERIFIER(champ.lire({200, 0, 0}).distance >= 8.0);
    VERIFIER(!champ.points_de_surface().empty());
}

void test_position_libre() {
    const ModeleMachine m = machine_de_test();
    DetecteurCollisions d(m, reglages_de_test());
    // Buse posée sur le plateau, puis levée : rien ne touche. Le module Z
    // frotte sur sa colonne : c'est une liaison, pas une collision.
    VERIFIER(!d.controler({0, 0, 0.2, 0, 0}));
    VERIFIER(!d.controler({0, 0, 50, 0, 0}));
    VERIFIER(!d.controler({40, -30, 120, 0, 90}));
}

void test_tete_contre_matiere() {
    const ModeleMachine m = machine_de_test();
    DetecteurCollisions d(m, reglages_de_test());
    colonne_de_matiere(d, 20.0, 0.0, 20.0);  // une tour de 20 mm, à 20 mm du centre
    // Buse au centre : le bloc chauffant (large de ±8 mm) est à 12 mm de la tour.
    VERIFIER(!d.controler({0, 0, 0.2, 0, 0}));
    // On avance vers la tour par petits pas : le bloc chauffant finit par la toucher.
    std::optional<Contact> contact;
    double x = 0.0;
    for (; x <= 16.0 && !contact; x += 0.1) {
        contact = d.controler({x, 0, 0.2, 0, 0});
    }
    VERIFIER(contact.has_value());
    if (contact) {
        VERIFIER(contact->type == TypeContact::matiere);
        VERIFIER(contact->piece == "bloc chauffant");
        VERIFIER(contact->obstacle == "matière déjà imprimée");
        // Le bord du bloc (x + 8) arrive à 2 mm de la tour (x = 20) vers x = 10.
        VERIFIER(x > 9.0 && x < 11.5);
    }
    // En passant au-dessus de la tour (buse à 25 mm), tout va bien.
    DetecteurCollisions dessus(m, reglages_de_test());
    colonne_de_matiere(dessus, 20.0, 0.0, 20.0);
    VERIFIER(!dessus.controler({20, 0, 25, 0, 0}));
}

void test_zone_libre_de_la_buse() {
    const ModeleMachine m = machine_de_test();
    DetecteurCollisions d(m, reglages_de_test());
    // De la matière juste sous et autour de la pointe : c'est la couche en
    // cours d'impression, ce n'est pas une collision.
    for (double x = -3.0; x <= 3.0; x += 1.0) {
        for (double y = -3.0; y <= 3.0; y += 1.0) {
            d.deposer({x, y, 0.1});
        }
    }
    VERIFIER(!d.controler({0, 0, 0.3, 0, 0}));
}

void test_tete_inclinee_contre_plateau() {
    const ModeleMachine m = machine_de_test();
    DetecteurCollisions d(m, reglages_de_test());
    // Buse verticale au ras du plateau : normal.
    VERIFIER(!d.controler({0, 0, 0.2, 0, 0}));
    // Tête inclinée de 70° en gardant la pointe au ras du plateau : le bloc
    // chauffant passe à travers le plateau.
    const auto incline = position_machine(
        reglages_de_test(), {{0, 0, 0.2}, {std::sin(en_radians(70)), 0, std::cos(en_radians(70))}}, 0);
    VERIFIER(incline.has_value());
    const auto contact = d.controler(*incline);
    VERIFIER(contact.has_value());
    VERIFIER(contact && contact->type == TypeContact::plateau);
    VERIFIER(contact && contact->piece == "bloc chauffant" && contact->obstacle == "plateau");
    // La même inclinaison, 60 mm plus haut : aucun problème.
    DetecteurCollisions haut(m, reglages_de_test());
    const auto plus_haut = position_machine(
        reglages_de_test(), {{0, 0, 60}, {std::sin(en_radians(70)), 0, std::cos(en_radians(70))}}, 0);
    VERIFIER(plus_haut && !haut.controler(*plus_haut));
}

void test_bras_contre_piece_haute() {
    // Une pièce haute de 60 mm se trouve du côté du bras. Quand la buse
    // travaille bas de l'autre côté, le bras passe à travers la pièce.
    const ModeleMachine m = machine_de_test();
    DetecteurCollisions d(m, reglages_de_test());
    colonne_de_matiere(d, 60.0, 0.0, 60.0);
    const auto contact = d.controler({0, 0, 0.2, 0, 0});
    VERIFIER(contact.has_value());
    VERIFIER(contact && contact->piece == "profilé du bras" &&
             contact->obstacle == "matière déjà imprimée");
    // En tournant le plateau d'un demi-tour, la pièce haute passe de l'autre
    // côté, là où le bras ne va pas : la buse atteint le même endroit de la
    // pièce (devenu l'opposé sur la machine) sans collision.
    DetecteurCollisions tourne(m, reglages_de_test());
    colonne_de_matiere(tourne, 60.0, 0.0, 60.0);
    VERIFIER(!tourne.controler({0, 0, 0.2, 0, 180}));
}

void test_module_z_contre_butee() {
    const ModeleMachine m = machine_de_test();
    DetecteurCollisions d(m, reglages_de_test());
    // Le module Z (dessus à 80 mm) monte vers la butée haute (dessous à 300 mm).
    std::optional<Contact> contact;
    double z = 0.0;
    for (; z <= 230.0 && !contact; z += 0.5) {
        contact = d.controler({0, 0, z, 0, 0});
    }
    VERIFIER(contact.has_value());
    VERIFIER(contact && contact->type == TypeContact::machine);
    VERIFIER(contact && contact->piece == "module Z" && contact->obstacle == "butée haute");
    // Contact réel à 220 mm ; avec 2 mm de marge, l'alerte tombe vers 218 mm.
    VERIFIER(z > 215.0 && z < 219.5);
}

void test_controles_sautes() {
    // Loin de tout, des milliers de petits pas ne coûtent que quelques
    // contrôles complets.
    const ModeleMachine m = machine_de_test();
    DetecteurCollisions d(m, reglages_de_test());
    for (int i = 0; i < 5000; ++i) {
        VERIFIER(!d.controler({-50 + i * 0.01, 0, 100, 0, 0}));
    }
    if (d.controles_complets() >= 400) {
        std::cerr << "contrôles complets : " << d.controles_complets() << '\n';
    }
    VERIFIER(d.controles_complets() < 400);
}

void test_reglage_incoherent() {
    ReglagesMachine r = reglages_de_test();
    r.distance_pivot_buse = 55.0;  // le modèle dit 40 mm
    bool refuse = false;
    try {
        const ModeleMachine m = machine_de_test();
        DetecteurCollisions d(m, r);
    } catch (const ErreurMachine&) {
        refuse = true;
    }
    VERIFIER(refuse);
}

void test_dans_le_verificateur() {
    // Le modèle est écrit dans un fichier, puis utilisé par le vérificateur.
    const std::filesystem::path fichier =
        std::filesystem::temp_directory_path() / "artprint_machine_test.apm";
    ecrire_modele_machine(fichier, machine_de_test());
    const ModeleMachine relu = lire_modele_machine(fichier);
    VERIFIER(relu.groupes.size() == 6 && relu.groupes[5].pieces[1].nom == "bloc chauffant");
    VERIFIER(relu.pivot_b.z == 40.0);

    Reglages reglages;
    reglages.machine = reglages_de_test();
    const std::u8string chemin = fichier.u8string();
    reglages.machine.modele_machine.assign(chemin.begin(), chemin.end());

    // Un petit carré imprimé au centre : accepté.
    std::string g = "G90\nM83\nG0 X-5 Y-5 Z0.2\n";
    for (int couche = 1; couche <= 10; ++couche) {
        g += std::format("G1 E-1\nG0 Z{:.1f}\nG0 X-5 Y-5\nG0 Z{:.1f}\nG1 E1\n",
                         couche * 0.2 + 0.4, couche * 0.2);
        g += "G1 X5 Y-5 E0.4\nG1 X5 Y5 E0.4\nG1 X-5 Y5 E0.4\nG1 X-5 Y-5 E0.4\n";
    }
    const RapportVerification bon = verifier_gcode_texte(g, reglages);
    for (const auto& erreur : bon.erreurs) {
        std::cerr << decrire(erreur);
    }
    VERIFIER(bon.valide());

    // Une tour de 40 mm imprimée à X = 60, puis la buse redescend au centre :
    // le bras traverse la tour.
    std::string tour = "G90\nM83\nG0 X60 Y0 Z0.2\n";
    for (int couche = 1; couche <= 200; ++couche) {
        tour += std::format("G1 E-1\nG0 Z{:.1f}\nG1 E1\nG1 X61 Y0 E0.04\nG1 X60.5 Y0.5 E0.03\n"
                            "G1 X60 Y0 E0.03\n",
                            couche * 0.2);
    }
    tour += "G1 E-1\nG0 Z60\nG0 X0 Y0\nG0 Z1\n";
    const RapportVerification mauvais = verifier_gcode_texte(tour, reglages);
    VERIFIER(mauvais.contient(TypeErreur::collision_matiere));
    for (const auto& erreur : mauvais.erreurs) {
        if (erreur.type == TypeErreur::collision_matiere) {
            VERIFIER(erreur.probleme.find("profilé du bras") != std::string::npos);
            VERIFIER(erreur.probleme.find("matière déjà imprimée") != std::string::npos);
            VERIFIER(erreur.texte_ligne == "G0 Z1");
        }
    }

    // Fichier de modèle introuvable : erreur claire, pas de plantage.
    reglages.machine.modele_machine = "nexiste_pas.apm";
    VERIFIER(verifier_gcode_texte("G0 X1\n", reglages).contient(TypeErreur::modele_machine));
    std::filesystem::remove(fichier);
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"champ de distance d'un cube", test_champ_de_distance},
        {"positions sans contact", test_position_libre},
        {"tête contre la matière imprimée", test_tete_contre_matiere},
        {"zone libre autour de la buse", test_zone_libre_de_la_buse},
        {"tête inclinée contre le plateau", test_tete_inclinee_contre_plateau},
        {"bras contre une pièce haute, puis plateau tourné", test_bras_contre_piece_haute},
        {"module Z contre sa butée haute", test_module_z_contre_butee},
        {"contrôles inutiles sautés", test_controles_sautes},
        {"réglage incohérent avec le modèle", test_reglage_incoherent},
        {"collisions dans le vérificateur de G-code", test_dans_le_verificateur},
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
        std::cout << "Tous les tests de collisions sont passés.\n";
        return 0;
    }
    std::cout << echecs << " vérification(s) en échec.\n";
    return 1;
}
