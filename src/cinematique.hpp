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
// « cote » : une même orientation s'obtient en penchant la tête d'un côté,
// ou de l'autre avec le plateau tourné d'un demi-tour. 0 laisse le choix
// (C le plus proche) ; +1 ou -1 impose le côté.
std::optional<PositionMachine> position_machine(const ReglagesMachine& machine,
                                                const PoseOutil& pose, double c_precedent,
                                                int cote = 0);

// Position des axes qui amène la pointe de la buse à cet endroit de la pièce
// quand B et C sont déjà choisis (par exemple au milieu d'une rotation).
PositionMachine position_avec_angles(const ReglagesMachine& machine, const Vec3& pointe,
                                     double b, double c);

// Ramène un angle C à la valeur équivalente (à 360° près) la plus proche de
// « reference ». Exemple : -170 près de 170 donne 190.
double angle_le_plus_proche(double angle, double reference);

// Plus grande inclinaison de la tête que la machine permet, en degrés :
// ses butées, et la limite du firmware. Elle peut dépasser 90° : la buse
// pointe alors vers le haut, pour imprimer par en dessous.
double inclinaison_machine_max(const ReglagesMachine& machine);
// Vrai si l'encombrement simplifié de la tête est renseigné (réglages
// « tete_demi_largeur », « tete_hauteur » et « distance_pivot_buse »).
bool encombrement_tete_connu(const ReglagesMachine& machine);
// Hauteur du point le plus bas de la tête par rapport à la pointe de la
// buse, pour une inclinaison donnée en degrés (négative : la tête descend
// plus bas que la pointe). Par prudence, toute la tête est comptée à la
// largeur du bloc, de la pointe de la buse au bout du bloc.
double point_bas_de_la_tete(const ReglagesMachine& machine, double inclinaison);
// Plus grande inclinaison permise quand la pointe de la buse est à la
// hauteur « z_pointe » au-dessus du plateau. La limite près du plateau est
// toujours permise. Au-delà : avec l'encombrement de la tête, jusqu'à
// l'inclinaison où elle reste à la marge de sécurité du plateau ; sinon,
// seulement au-dessus de la hauteur de bascule.
double inclinaison_permise(const ReglagesMachine& machine, double z_pointe);
// Vrai si l'inclinaison « b » (en degrés) est permise à cette hauteur : la
// même règle que ci-dessus, sans rien chercher.
bool inclinaison_admise(const ReglagesMachine& machine, double z_pointe, double b);
// Plus grande inclinaison qu'une couche courbe peut demander. Un peu plus
// que la machine : la tête, à sa limite, n'est alors pas tout à fait d'aplomb.
double inclinaison_des_couches_max(const ReglagesMachine& machine);

// Coordonnées écrites dans le G-code, d'après le réglage « contrat_gcode ».
// Contrat 1 : X, Y, Z sont la position de la POINTE de la buse dans le repère
// de la machine (le plateau a déjà tourné de C) ; c'est le firmware qui en
// déduit la position des axes. Contrat 0 : X, Y, Z sont la position des axes.
// B et C ne changent pas.
PositionMachine vers_gcode(const ReglagesMachine& machine, const PositionMachine& axes);
// L'inverse : la position des axes, à partir de ce qui est écrit dans le fichier.
PositionMachine depuis_gcode(const ReglagesMachine& machine, const PositionMachine& ecrit);

}  // namespace artprint
