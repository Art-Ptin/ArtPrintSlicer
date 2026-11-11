// ArtPrint Slicer - licence AGPL v3
#include "cinematique.hpp"

#include <algorithm>
#include <cmath>

namespace artprint {

namespace {

// En dessous de cette inclinaison (en degrés), la buse est considérée verticale.
constexpr double b_vertical = 1e-9;

// Direction de la buse (pointe vers tête) dans le repère fixe de la machine.
Vec3 direction_machine(const ReglagesMachine& machine, double b_degres) {
    const double b = en_radians(b_degres) * machine.sens_b;
    if (machine.axe_pivot_b == AxePivot::y) {
        return {std::sin(b), 0.0, std::cos(b)};
    }
    return {0.0, std::sin(b), std::cos(b)};
}

double distance_pivot(const ReglagesMachine& machine, double b_degres) {
    if (machine.distance_pivot_buse) {
        return *machine.distance_pivot_buse;
    }
    if (std::abs(b_degres) > b_vertical) {
        throw ErreurReglageManquant(
            "Le réglage « distance_pivot_buse » (distance entre le pivot B et la pointe de "
            "la buse) n'est pas renseigné. Il faut le mesurer sur la machine.");
    }
    return 0.0;  // buse verticale : la distance n'intervient pas
}

Vec3 centre_plateau(const ReglagesMachine& machine) {
    return {machine.centre_plateau_x, machine.centre_plateau_y, 0.0};
}

}  // namespace

PoseOutil pose_dans_piece(const ReglagesMachine& machine, const PositionMachine& position) {
    const double longueur = distance_pivot(machine, position.b);
    const Vec3 direction = direction_machine(machine, position.b);
    const Vec3 pivot{position.x, position.y, position.z + longueur};
    const Vec3 pointe = pivot - direction * longueur;

    // Le plateau a tourné de C : vue depuis la pièce, la buse a tourné de -C.
    const double angle = -en_radians(position.c) * machine.sens_c;
    return {tourner_autour_de_z(pointe - centre_plateau(machine), angle),
            tourner_autour_de_z(direction, angle)};
}

double angle_le_plus_proche(double angle, double reference) {
    return angle - 360.0 * std::round((angle - reference) / 360.0);
}

std::optional<PositionMachine> position_machine(const ReglagesMachine& machine,
                                                const PoseOutil& pose, double c_precedent) {
    const Vec3 d = normaliser(pose.direction);
    const double inclinaison = en_degres(std::acos(std::clamp(d.z, -1.0, 1.0)));

    // Candidats (B, C). Buse verticale : C garde sa valeur, il n'a aucun effet.
    struct Candidat {
        double b;
        double c;
    };
    std::optional<Candidat> meilleur;
    auto proposer = [&](double b, double c) {
        if (b < machine.b_min - 1e-9 || b > machine.b_max + 1e-9) {
            return;
        }
        if ((machine.c_min && c < *machine.c_min) || (machine.c_max && c > *machine.c_max)) {
            return;
        }
        if (!meilleur || std::abs(c - c_precedent) < std::abs(meilleur->c - c_precedent)) {
            meilleur = Candidat{b, c};
        }
    };

    if (inclinaison < 1e-7) {
        proposer(0.0, c_precedent);
    } else {
        // Angle de la direction vue de dessus, dans la pièce.
        const double azimut = en_degres(std::atan2(d.y, d.x));
        // Angle que la machine sait donner à la tête, vue de dessus.
        const double azimut_machine = machine.axe_pivot_b == AxePivot::y ? 0.0 : 90.0;
        // Deux solutions : pencher d'un côté, ou de l'autre avec le plateau
        // tourné d'un demi-tour.
        for (const double signe : {1.0, -1.0}) {
            const double b = signe * inclinaison * machine.sens_b;
            const double cible = azimut_machine + (signe > 0.0 ? 0.0 : 180.0);
            const double c = machine.sens_c * (cible - azimut);
            proposer(b, angle_le_plus_proche(c, c_precedent));
        }
    }
    if (!meilleur) {
        return std::nullopt;
    }

    const double longueur = distance_pivot(machine, meilleur->b);
    const double angle = en_radians(meilleur->c) * machine.sens_c;
    const Vec3 pointe = tourner_autour_de_z(pose.pointe, angle) + centre_plateau(machine);
    const Vec3 pivot = pointe + direction_machine(machine, meilleur->b) * longueur;
    return PositionMachine{pivot.x, pivot.y, pivot.z - longueur, meilleur->b, meilleur->c};
}

}  // namespace artprint
