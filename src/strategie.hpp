// ArtPrint Slicer - licence AGPL v3
// Stratégies d'impression : plusieurs façons d'imprimer le même modèle.
//
//   - « classique » : tout le modèle en couches horizontales, tête verticale ;
//   - « autre pose » : le modèle entier couché ou retourné sur le plateau,
//     dans la position qui demande le moins de supports ;
//   - « par branches » : le modèle est découpé en morceaux (le tronc, puis
//     chaque branche) et chaque morceau est imprimé dans sa propre
//     direction, la tête inclinée (B) et le plateau tourné (C).
//
// Pour chaque stratégie, on mesure la surface « en surplomb » : celle qui
// serait imprimée dans le vide et demanderait des supports.
#pragma once

#include <string>
#include <vector>

#include "geometrie.hpp"
#include "maillage.hpp"
#include "reglages.hpp"

namespace artprint {

// Un morceau du modèle et la direction dans laquelle il est imprimé.
struct PartieImpression {
    Maillage maillage;         // volume fermé, dans le repère de la pièce
    Vec3 direction{0.0, 0.0, 1.0};  // direction d'impression : les couches s'empilent le long
    Vec3 base;                 // un point de la surface de départ
    int parent = -1;           // numéro de la partie qui la porte (-1 : le plateau)
    double inclinaison = 0.0;  // angle avec la verticale, en degrés
    // Côté vers lequel la tête penche pour cette partie : 0 laisse le slicer
    // choisir ; +1 ou -1 l'impose (choisi pour éviter une collision).
    int cote = 0;
    double volume = 0.0;       // mm³
    double surplomb = 0.0;     // mm² à soutenir quand on l'imprime dans sa direction
};

enum class TypeStrategie { classique, autre_pose, par_branches };

struct Strategie {
    TypeStrategie type = TypeStrategie::classique;
    std::string nom;
    std::string description;
    std::vector<PartieImpression> parties;  // dans l'ordre d'impression
    double surplomb = 0.0;                  // mm² à soutenir, toutes parties réunies
    std::vector<std::string> remarques;     // limites et avertissements, en français
};

struct OptionsStrategie {
    // Angle de surplomb imprimable sans support, mesuré depuis la verticale.
    double surplomb_max = 45.0;
    // Côté des voxels d'analyse en mm (0 : choisi d'après la taille du modèle).
    double pas_voxels = 0.0;
};

// Surface (mm²) des faces du maillage qui seraient imprimées dans le vide en
// empilant les couches le long de « direction ». Les faces posées sur la
// surface de départ (à moins de « tolerance » mm de « base ») ne comptent pas.
// « matiere » (facultatif) : la matière de tout le modèle ; une face cachée
// à l'intérieur n'est pas comptée.
struct GrilleVoxels;
double surface_en_surplomb(const Maillage& maillage, const Vec3& direction, const Vec3& base,
                           double surplomb_max, double tolerance,
                           const GrilleVoxels* matiere = nullptr);

// Propose les stratégies possibles pour ce modèle posé sur le plateau
// (dessous à la hauteur zéro), de la plus simple à la plus élaborée.
// La première est toujours la stratégie classique.
std::vector<Strategie> proposer_strategies(const Maillage& pose, const Reglages& reglages,
                                           const OptionsStrategie& options = {});

}  // namespace artprint
