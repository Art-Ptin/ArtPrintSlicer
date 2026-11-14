// ArtPrint Slicer - licence AGPL v3
// Le slicer 3 axes : d'un modèle 3D au G-code, couche par couche. La tête
// reste verticale (B = 0) et le plateau ne tourne pas (C = 0).
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "maillage.hpp"
#include "reglages.hpp"
#include "verificateur.hpp"

namespace artprint {

// Refus de trancher, avec un message en français.
class ErreurTranchage : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Un mouvement de la buse pour l'aperçu, dans le repère de la pièce (mm).
struct MouvementApercu {
    float x0 = 0, y0 = 0, z0 = 0;  // départ
    float x1 = 0, y1 = 0, z1 = 0;  // arrivée
    // Type : 0 jupe, 1 paroi interne, 2 paroi externe, 3 remplissage plein,
    // 4 remplissage, 5 déplacement à vide.
    std::uint8_t type = 0;
};
constexpr std::uint8_t type_apercu_vide = 5;

struct CoucheApercu {
    double z = 0.0;
    int partie = 0;  // numéro de la partie imprimée (0 : la première, ou la seule)
    std::vector<MouvementApercu> mouvements;
};

struct ResultatTranchage {
    std::string gcode;
    std::size_t couches = 0;
    double longueur_filament = 0.0;  // mm de filament consommé
    double duree_estimee = 0.0;      // secondes, sans les accélérations
    // Tous les mouvements, couche par couche, pour l'aperçu à l'écran.
    std::vector<CoucheApercu> apercu;
    // Contrôle du G-code produit par le vérificateur.
    RapportVerification verification;
    // Ce que le slicer a dû faire autrement que demandé, en français
    // (par exemple : la peau courbe abandonnée à cause d'une collision).
    std::vector<std::string> remarques;
};

// Rend une copie du modèle posée au centre du plateau, le dessous à la
// hauteur zéro. C'est la position utilisée pour trancher et pour l'affichage.
Maillage poser_au_centre(const Maillage& maillage);

// Tranche le modèle, posé au centre du plateau. Le G-code produit est
// toujours relu par le vérificateur : regarder « verification.valide() »
// avant de l'utiliser.
// Lève ErreurTranchage si le modèle ne peut pas être tranché (modèle ouvert,
// trop grand pour la machine...).
// « deja_pose » : vrai si le modèle est déjà à sa place sur le plateau (le
// dessous à la hauteur zéro) ; il n'est alors pas recentré.
// « verifier » : faux pour une simple estimation (durée, filament) ; le
// G-code n'est alors pas contrôlé et ne doit pas être utilisé.
ResultatTranchage trancher(const Maillage& maillage, const Reglages& reglages, bool deja_pose = false,
                           bool verifier = true);

}  // namespace artprint
