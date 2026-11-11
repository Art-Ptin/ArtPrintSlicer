// ArtPrint Slicer - licence AGPL v3
// Modèle de la machine : la vraie forme de l'imprimante, pièce par pièce,
// rangée par groupes qui bougent ensemble. Il est fabriqué à partir du
// fichier STEP par « outils/convertir_machine.py » et reste privé.
//
// Repère du modèle : origine au centre du plateau, sur sa surface, Z vers
// le haut ; la machine y est dessinée buse verticale, posée au centre.
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "geometrie.hpp"

namespace artprint {

// Comment bouge un groupe de pièces.
enum class Mouvement {
    fixe = 0,       // bâti
    chariot_y = 1,  // suit l'axe Y, sous le plateau
    plateau = 2,    // suit l'axe Y et tourne en C
    axe_z = 3,      // suit l'axe Z
    chariot_x = 4,  // suit X et Z (le bras)
    tete = 5,       // suit X et Z, s'incline en B
};

struct PieceMachine {
    std::string nom;
    std::vector<float> triangles;  // 9 nombres par triangle
};

struct GroupeMachine {
    std::string nom;
    Mouvement mouvement = Mouvement::fixe;
    std::vector<PieceMachine> pieces;
};

struct ModeleMachine {
    std::vector<GroupeMachine> groupes;
    Vec3 pivot_b;         // point de l'axe du pivot B, à la verticale de la buse
    Vec3 direction_pivot; // direction de cet axe
    Vec3 pointe_buse;     // pointe de la buse

    // Premier groupe ayant ce mouvement, ou nullptr.
    const GroupeMachine* groupe(Mouvement mouvement) const;
};

class ErreurMachine : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

ModeleMachine lire_modele_machine(const std::filesystem::path& chemin);
void ecrire_modele_machine(const std::filesystem::path& chemin, const ModeleMachine& modele);

}  // namespace artprint
