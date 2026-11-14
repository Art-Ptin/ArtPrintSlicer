// ArtPrint Slicer - licence AGPL v3
// Tests de la cinématique 5 axes. Les valeurs de machine utilisées ici sont
// inventées pour les tests : ce ne sont pas celles de la vraie imprimante.
#include <cmath>
#include <iostream>
#include <utility>

#include "cinematique.hpp"

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

bool proche(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

bool proche(const Vec3& a, const Vec3& b, double tolerance = 1e-9) {
    return norme(a - b) <= tolerance;
}

ReglagesMachine machine_de_test() {
    ReglagesMachine machine;
    machine.distance_pivot_buse = 50.0;  // valeur de test, inventée
    return machine;
}

void test_buse_verticale() {
    // B = 0 et C = 0 : X, Y, Z donnent directement la pointe de la buse.
    const PoseOutil pose = pose_dans_piece(ReglagesMachine{}, {10.0, 20.0, 30.0, 0.0, 0.0});
    VERIFIER(proche(pose.pointe, {10.0, 20.0, 30.0}));
    VERIFIER(proche(pose.direction, {0.0, 0.0, 1.0}));
}

void test_centre_du_plateau() {
    ReglagesMachine machine;
    machine.centre_plateau_x = 100.0;
    machine.centre_plateau_y = 50.0;
    const PoseOutil pose = pose_dans_piece(machine, {110.0, 50.0, 5.0, 0.0, 0.0});
    VERIFIER(proche(pose.pointe, {10.0, 0.0, 5.0}));
}

void test_rotation_du_plateau() {
    // Le plateau tourne de +90° : vue depuis la pièce, la buse tourne de -90°.
    const PoseOutil pose = pose_dans_piece(ReglagesMachine{}, {10.0, 0.0, 5.0, 0.0, 90.0});
    VERIFIER(proche(pose.pointe, {0.0, -10.0, 5.0}));

    ReglagesMachine inverse;
    inverse.sens_c = -1;
    const PoseOutil autre = pose_dans_piece(inverse, {10.0, 0.0, 5.0, 0.0, 90.0});
    VERIFIER(proche(autre.pointe, {0.0, 10.0, 5.0}));
}

void test_inclinaison() {
    // Tête couchée à 90° autour de Y : le pivot est 50 mm au-dessus de
    // (X, Y, Z) et la pointe part de 50 mm sur le côté.
    const ReglagesMachine machine = machine_de_test();
    const PoseOutil pose = pose_dans_piece(machine, {0.0, 0.0, 0.0, 90.0, 0.0});
    VERIFIER(proche(pose.pointe, {-50.0, 0.0, 50.0}));
    VERIFIER(proche(pose.direction, {1.0, 0.0, 0.0}));

    ReglagesMachine autour_de_x = machine;
    autour_de_x.axe_pivot_b = AxePivot::x;
    const PoseOutil autre = pose_dans_piece(autour_de_x, {0.0, 0.0, 0.0, 90.0, 0.0});
    VERIFIER(proche(autre.pointe, {0.0, -50.0, 50.0}));
    VERIFIER(proche(autre.direction, {0.0, 1.0, 0.0}));
}

void test_distance_pivot_obligatoire() {
    // Buse verticale : la distance n'est pas nécessaire.
    pose_dans_piece(ReglagesMachine{}, {0.0, 0.0, 0.0, 0.0, 45.0});
    // Buse inclinée sans la distance : refus.
    bool refuse = false;
    try {
        pose_dans_piece(ReglagesMachine{}, {0.0, 0.0, 0.0, 10.0, 0.0});
    } catch (const ErreurReglageManquant&) {
        refuse = true;
    }
    VERIFIER(refuse);
}

void test_angle_le_plus_proche() {
    VERIFIER(proche(angle_le_plus_proche(-170.0, 170.0), 190.0));
    VERIFIER(proche(angle_le_plus_proche(170.0, -170.0), -190.0));
    VERIFIER(proche(angle_le_plus_proche(10.0, 725.0), 730.0));
    VERIFIER(proche(angle_le_plus_proche(45.0, 30.0), 45.0));
}

// Le calcul inverse puis le calcul direct doivent redonner la pose demandée,
// pour toutes les géométries de machine possibles.
void test_aller_retour() {
    for (const AxePivot axe : {AxePivot::x, AxePivot::y}) {
        for (const int sens_b : {1, -1}) {
            for (const int sens_c : {1, -1}) {
                ReglagesMachine machine = machine_de_test();
                machine.axe_pivot_b = axe;
                machine.sens_b = sens_b;
                machine.sens_c = sens_c;
                machine.centre_plateau_x = 12.0;
                machine.centre_plateau_y = -7.0;

                double c = 0.0;
                for (int i = 0; i < 200; ++i) {
                    const double azimut = en_radians(i * 9.7);
                    const double inclinaison = en_radians(5.0 + (i % 17) * 5.0);
                    const PoseOutil voulue{
                        {30.0 * std::cos(i * 0.3), 30.0 * std::sin(i * 0.2), 5.0 + i * 0.1},
                        {std::sin(inclinaison) * std::cos(azimut),
                         std::sin(inclinaison) * std::sin(azimut), std::cos(inclinaison)}};
                    const auto position = position_machine(machine, voulue, c);
                    VERIFIER(position.has_value());
                    if (!position) {
                        continue;
                    }
                    const PoseOutil obtenue = pose_dans_piece(machine, *position);
                    VERIFIER(proche(obtenue.pointe, voulue.pointe, 1e-7));
                    VERIFIER(proche(obtenue.direction, voulue.direction, 1e-7));
                    // C ne fait jamais plus d'un demi-tour d'un point au suivant.
                    VERIFIER(std::abs(position->c - c) <= 180.0 + 1e-9);
                    c = position->c;
                }
            }
        }
    }
}

void test_buse_verticale_garde_c() {
    // Buse verticale : C n'a aucun effet sur l'orientation, il ne bouge pas.
    const auto position =
        position_machine(machine_de_test(), {{5.0, 0.0, 1.0}, {0.0, 0.0, 1.0}}, 123.0);
    VERIFIER(position.has_value());
    VERIFIER(position && proche(position->c, 123.0) && proche(position->b, 0.0));
}

void test_butees() {
    // B limité à 0..60° : une seule des deux solutions reste possible.
    ReglagesMachine machine = machine_de_test();
    machine.b_min = 0.0;
    machine.b_max = 60.0;
    const double a = en_radians(45.0);
    const auto position =
        position_machine(machine, {{0.0, 0.0, 10.0}, {-std::sin(a), 0.0, std::cos(a)}}, 0.0);
    VERIFIER(position.has_value());
    VERIFIER(position && proche(position->b, 45.0, 1e-9));
    VERIFIER(position && proche(std::abs(position->c), 180.0, 1e-9));

    // Inclinaison impossible : aucune solution.
    const double trop = en_radians(80.0);
    VERIFIER(!position_machine(machine,
                               {{0.0, 0.0, 10.0}, {std::sin(trop), 0.0, std::cos(trop)}}, 0.0));
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"buse verticale", test_buse_verticale},
        {"centre du plateau", test_centre_du_plateau},
        {"rotation du plateau", test_rotation_du_plateau},
        {"inclinaison de la tête", test_inclinaison},
        {"distance pivot-buse obligatoire", test_distance_pivot_obligatoire},
        {"angle le plus proche", test_angle_le_plus_proche},
        {"aller-retour inverse puis direct", test_aller_retour},
        {"buse verticale : C ne bouge pas", test_buse_verticale_garde_c},
        {"butées de B", test_butees},
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
        std::cout << "Tous les tests de cinématique sont passés.\n";
        return 0;
    }
    std::cout << echecs << " vérification(s) en échec.\n";
    return 1;
}
