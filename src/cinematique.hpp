// ArtPrint Slicer - licence AGPL v3
// Cinématique : le lien entre la position des cinq axes de la machine et
// l'endroit où se trouve la buse par rapport à la pièce.
//
// Modèle utilisé (tout est réglable, voir reglages.hpp) :
//   - Le repère de la pièce est fixé au plateau : origine au centre du
//     plateau, sur sa surface, Z vers le haut. Il tourne avec l'axe C.
//   - Quand B = 0, la buse est verticale et X, Y, Z donnent la position de
//     sa pointe.
//   - Quand B change, la tête tourne autour du pivot, situé au-dessus de la
//     pointe à la distance « distance_pivot_buse ». Le firmware ne compense
//     rien : c'est le slicer qui calcule tout.
#pragma once

#include <optional>
#include <stdexcept>

#include "geometrie.hpp"
#include "reglages.hpp"

namespace artprint {

// Position des cinq axes, comme dans une ligne de G-code.
// X, Y, Z en millimètres, B et C en degrés.
struct PositionMachine {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double b = 0.0;
    double c = 0.0;
};

// La buse vue depuis la pièce (repère fixé au plateau).
struct PoseOutil {
    Vec3 pointe;     // position de la pointe de la buse
    Vec3 direction;  // de la pointe vers la tête, longueur 1
};

// Levée quand un réglage indispensable n'a pas été renseigné.
class ErreurReglageManquant : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Calcule où est la buse par rapport à la pièce.
// Lève ErreurReglageManquant si B est incliné et que la distance pivot-buse
// n'est pas renseignée.
PoseOutil pose_dans_piece(const ReglagesMachine& machine, const PositionMachine& position);

// Calcul inverse : la position des axes qui amène la buse à cet endroit de
// la pièce, avec cette orientation. Parmi les solutions possibles, choisit
// celle dont C est le plus proche de « c_precedent » (chemin le plus court).
// Renvoie « rien » si aucune solution ne respecte les butées de B et C.
std::optional<PositionMachine> position_machine(const ReglagesMachine& machine,
                                                const PoseOutil& pose, double c_precedent);

// Position des axes qui amène la pointe de la buse à cet endroit de la pièce
// quand B et C sont déjà choisis (par exemple au milieu d'une rotation).
PositionMachine position_avec_angles(const ReglagesMachine& machine, const Vec3& pointe,
                                     double b, double c);

// Ramène un angle C à la valeur équivalente (à 360° près) la plus proche de
// « reference ». Exemple : -170 près de 170 donne 190.
double angle_le_plus_proche(double angle, double reference);

}  // namespace artprint
