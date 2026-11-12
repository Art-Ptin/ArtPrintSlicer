// ArtPrint Slicer - licence AGPL v3
// Slicer multi-directions : imprime chaque partie d'une stratégie le long de
// sa propre direction. Pour une partie penchée, la tête s'incline (B) et le
// plateau tourne (C) ; les couches sont des plans perpendiculaires à la
// direction de la partie.
//
// Entre deux parties, la buse se rétracte, monte au-dessus de tout ce qui
// est déjà imprimé, puis la tête et le plateau tournent par petits pas avant
// de redescendre : c'est le « changement d'orientation ».
#pragma once

#include "reglages.hpp"
#include "slicer.hpp"
#include "strategie.hpp"

namespace artprint {

struct OptionsMultiaxe {
    // Hauteur libre au-dessus du modèle pour changer d'orientation, en mm.
    double degagement = 20.0;
    // Distance d'approche le long de la buse avant de reprendre l'impression, en mm.
    double approche = 8.0;
    // Angle maximal de B ou de C entre deux lignes de G-code d'une rotation, en degrés.
    double pas_rotation = 5.0;
};

// Tranche une stratégie. Une stratégie à une seule partie verticale passe par
// le slicer 3 axes. Le G-code est toujours relu par le vérificateur :
// regarder « verification.valide() » avant de l'utiliser.
// Lève ErreurTranchage si une partie ne peut pas être tranchée.
ResultatTranchage trancher_strategie(const Strategie& strategie, const Reglages& reglages,
                                     const OptionsMultiaxe& options = {});

}  // namespace artprint
