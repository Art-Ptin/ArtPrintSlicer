// ArtPrint Slicer - licence AGPL v3
// Détection des collisions avec la vraie forme de l'imprimante.
//
// La tête, le bras (chariot X) et le module Z sont surveillés. À chaque
// position de la machine, on vérifie qu'ils restent à plus d'une marge de
// sécurité :
//   1. de la matière déjà imprimée ;
//   2. du plateau et de ce qui se trouve dessous ;
//   3. du bâti et des autres pièces de la machine.
// Le chariot du plateau (axe Y) est lui aussi surveillé contre le bâti.
//
// Près de la pointe de la buse, le contact avec la matière est normal : une
// petite zone autour de la pointe est donc exclue (réglage « zone_buse »).
// Ce qui s'y passe est contrôlé par les règles « dans » et « sous la matière »
// du vérificateur.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "cinematique.hpp"
#include "geometrie.hpp"
#include "machine.hpp"
#include "reglages.hpp"

namespace artprint {

enum class TypeContact { matiere, plateau, machine };

struct Contact {
    TypeContact type{};
    std::string piece;     // pièce de la tête, du bras ou du module Z
    std::string obstacle;  // ce qu'elle touche
    double distance = 0.0; // distance mesurée entre les deux, en mm
};

class DetecteurCollisions {
public:
    // Les marges et la zone libre viennent des réglages de la machine
    // (« marge_securite », « zone_buse », « marge_buse »).
    DetecteurCollisions(const ModeleMachine& modele, const ReglagesMachine& machine);
    ~DetecteurCollisions();
    // Copie complète, matière déposée comprise : pour essayer une suite de
    // mouvements sans toucher à l'original.
    DetecteurCollisions(const DetecteurCollisions& autre);

    // Ajoute de la matière imprimée à cet endroit (repère de la pièce).
    void deposer(const Vec3& point);

    // Contrôle la machine à cette position. Les positions doivent être
    // données dans l'ordre du mouvement, à petits pas : le détecteur saute
    // de lui-même les contrôles inutiles tant que la machine a peu bougé.
    std::optional<Contact> controler(const PositionMachine& position);

    // Nombre de contrôles complets réellement faits (pour les mesures).
    std::size_t controles_complets() const;

private:
    struct Interne;
    std::unique_ptr<Interne> interne_;
};

}  // namespace artprint
