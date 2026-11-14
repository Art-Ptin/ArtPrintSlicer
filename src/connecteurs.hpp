// ArtPrint Slicer - licence AGPL v3
// Connecteurs posés sur la face de coupe d'une pièce, pour réassembler les
// deux moitiés après l'impression :
//   - des trous, où glisser de petites tiges (goupilles) ;
//   - des tenons, qui entrent dans les trous de l'autre moitié ;
//   - une queue d'aronde : un rail (mâle) qui coulisse dans une rainure (femelle).
//
// Les connecteurs ne retaillent pas le maillage de la pièce. Chacun est un
// petit volume fermé ajouté au modèle : un volume « en plein » pour un tenon
// ou un rail, un volume « en creux » (ses faces tournées vers l'intérieur)
// pour un trou ou une rainure. Le tranchage additionne ces volumes avec leur
// signe : un creux retire de la matière, un plein en ajoute.
//
// Les deux moitiés d'une même coupe reçoivent leurs connecteurs aux mêmes
// endroits : les positions ne dépendent que de la forme de la section.
#pragma once

#include <string>

#include "geometrie.hpp"
#include "maillage.hpp"

namespace artprint {

enum class TypeConnecteur { aucun, trous, tenons, aronde_male, aronde_femelle };

struct OptionsConnecteurs {
    TypeConnecteur type = TypeConnecteur::aucun;
    int nombre = 2;            // trous ou tenons : combien (1 à 6)
    double diametre = 5.0;     // diamètre d'un trou ; largeur de la queue d'aronde à sa base, en mm
    double profondeur = 6.0;   // profondeur d'un trou, hauteur d'un tenon ou de la queue d'aronde, en mm
    double jeu = 0.2;          // jeu d'assemblage, en mm : les tenons et le rail sont plus petits d'autant
};

// Ajoute les connecteurs au morceau coupé. « centre » : le centre de la face
// de coupe ; « sortante » : la direction qui sort du morceau à travers cette
// face (longueur 1). Rend le nombre de connecteurs posés ; « remarque » dit
// pourquoi il y en a moins que demandé (face trop petite, par exemple).
int ajouter_connecteurs(Maillage& morceau, const Vec3& centre, const Vec3& sortante,
                        const OptionsConnecteurs& options, std::string& remarque);

}  // namespace artprint
