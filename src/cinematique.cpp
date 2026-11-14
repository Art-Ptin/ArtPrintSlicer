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
                                                const PoseOutil& pose, double c_precedent,
                                                int cote) {
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
        // Au-delà d'une butée de C, la même orientation du plateau existe un
        // tour plus loin dans l'autre sens : c'est elle qui est proposée (il
        // faudra dérouler le plateau, à vide, pour y aller).
        if (machine.c_min && machine.c_max && *machine.c_max - *machine.c_min >= 360.0 - 1e-9) {
            while (c > *machine.c_max + 1e-9) {
                c -= 360.0;
            }
            while (c < *machine.c_min - 1e-9) {
                c += 360.0;
            }
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
            if (cote != 0 && signe * cote < 0.0) {
                continue;  // ce côté n'est pas celui demandé
            }
            const double b = signe * inclinaison * machine.sens_b;
            const double cible = azimut_machine + (signe > 0.0 ? 0.0 : 180.0);
            const double c = machine.sens_c * (cible - azimut);
            proposer(b, angle_le_plus_proche(c, c_precedent));
        }
    }
    if (!meilleur) {
        return std::nullopt;
    }

    return position_avec_angles(machine, pose.pointe, meilleur->b, meilleur->c);
}

PositionMachine position_avec_angles(const ReglagesMachine& machine, const Vec3& pointe_piece,
                                     double b, double c) {
    const double longueur = distance_pivot(machine, b);
    const double angle = en_radians(c) * machine.sens_c;
    const Vec3 pointe = tourner_autour_de_z(pointe_piece, angle) + centre_plateau(machine);
    const Vec3 pivot = pointe + direction_machine(machine, b) * longueur;
    return PositionMachine{pivot.x, pivot.y, pivot.z - longueur, b, c};
}

double inclinaison_machine_max(const ReglagesMachine& machine) {
    return std::min(std::max(std::abs(machine.b_min), std::abs(machine.b_max)), machine.b_inclinaison_max);
}

bool encombrement_tete_connu(const ReglagesMachine& machine) {
    return machine.tete_demi_largeur && machine.tete_hauteur && machine.distance_pivot_buse;
}

double point_bas_de_la_tete(const ReglagesMachine& machine, double inclinaison) {
    if (!encombrement_tete_connu(machine)) {
        return 0.0;
    }
    const double angle = std::abs(inclinaison) * std::acos(-1.0) / 180.0;
    // Par prudence, toute la tête est comptée à la largeur du bloc, de la
    // pointe de la buse au bout du bloc : ses coins les plus bas sont à
    // hauteur de la pointe (tête peu inclinée) ou au bout du bloc (au-delà de 90°).
    const double bout = (*machine.distance_pivot_buse + *machine.tete_hauteur) * std::cos(angle);
    return std::min(0.0, bout) - *machine.tete_demi_largeur * std::sin(angle);
}

bool inclinaison_admise(const ReglagesMachine& machine, double z_pointe, double b) {
    const double inclinaison = std::abs(b);
    const double butee = inclinaison_machine_max(machine);
    if (inclinaison > butee + 1e-6) {
        return false;
    }
    if (inclinaison <= machine.b_limite_pres_plateau + 1e-6) {
        return true;
    }
    if (!encombrement_tete_connu(machine)) {
        return machine.hauteur_bascule_b && z_pointe >= *machine.hauteur_bascule_b;
    }
    // (La recherche de « inclinaison_permise » avance par demi-degrés : même arrondi ici.)
    const double essai = std::ceil(inclinaison * 2.0 - 1e-9) / 2.0;
    return z_pointe + point_bas_de_la_tete(machine, std::min(essai, butee)) >= machine.marge_securite;
}

double inclinaison_permise(const ReglagesMachine& machine, double z_pointe) {
    const double butee = inclinaison_machine_max(machine);
    if (!encombrement_tete_connu(machine)) {
        const bool haut = machine.hauteur_bascule_b && z_pointe >= *machine.hauteur_bascule_b;
        return haut ? butee : std::min(butee, machine.b_limite_pres_plateau);
    }
    // Jusqu'à la limite près du plateau, rien à calculer : elle est permise
    // partout. Au-delà, plus la tête penche, plus elle descend : on cherche la
    // plus grande inclinaison où elle reste à la marge de sécurité du plateau.
    double permise = std::min(butee, machine.b_limite_pres_plateau);
    for (double essai = permise + 0.5; essai <= butee + 1e-9; essai += 0.5) {
        if (z_pointe + point_bas_de_la_tete(machine, essai) < machine.marge_securite) {
            break;
        }
        permise = essai;
    }
    return std::min(butee, permise);
}

double inclinaison_des_couches_max(const ReglagesMachine& machine) {
    return std::clamp(inclinaison_machine_max(machine) + 10.0, 100.0, 170.0);
}

PositionMachine vers_gcode(const ReglagesMachine& machine, const PositionMachine& axes) {
    if (machine.contrat_gcode < 1) {
        return axes;
    }
    // La pointe est sous le pivot, à la distance pivot-buse le long de la buse.
    const double longueur = distance_pivot(machine, axes.b);
    const Vec3 direction = direction_machine(machine, axes.b);
    return {axes.x - direction.x * longueur, axes.y - direction.y * longueur,
            axes.z + longueur - direction.z * longueur, axes.b, axes.c};
}

PositionMachine depuis_gcode(const ReglagesMachine& machine, const PositionMachine& ecrit) {
    if (machine.contrat_gcode < 1) {
        return ecrit;
    }
    const double longueur = distance_pivot(machine, ecrit.b);
    const Vec3 direction = direction_machine(machine, ecrit.b);
    return {ecrit.x + direction.x * longueur, ecrit.y + direction.y * longueur,
            ecrit.z - longueur + direction.z * longueur, ecrit.b, ecrit.c};
}

}  // namespace artprint
