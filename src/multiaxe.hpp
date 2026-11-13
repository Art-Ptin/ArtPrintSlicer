// ArtPrint Slicer - licence AGPL v3
// Slicer multi-directions : imprime chaque partie d'une stratégie le long de
// sa propre direction. Pour une partie penchée, la tête s'incline (B) et le
// plateau tourne (C) ; les couches sont des plans perpendiculaires à la
// direction de la partie.
//
// Entre deux parties, la buse se rétracte, monte au-dessus de tout ce qui
// est déjà imprimé, puis la tête et le plateau tournent par petits pas avant
// de redescendre : c'est le « changement d'orientation ».
//
// Le long d'une partie courbe, les couches pivotent. La tête passe de
// l'orientation d'une couche à celle de la suivante pendant qu'elle imprime :
// B et C tournent en même temps que X, Y, Z et l'extrudeur (5 axes continu).
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
    // Partie courbe : la tête pivote pendant l'extrusion (5 axes continu).
    // Faux : elle pivote entre deux couches, pendant un déplacement à vide.
    // Le réglage « pivot_continu » de la machine doit aussi le permettre.
    bool pivot_continu = true;
    // Angle maximal de B ou de C entre deux lignes de G-code pendant l'extrusion, en degrés.
    double pas_pivot_continu = 0.25;
    // Peau courbe : rotation maximale du plateau par millimètre de cordon, en degrés.
    double rotation_plateau_par_mm = 4.0;
    // Couches courbes : nombre de fois où le tranchage est refait autrement
    // (tête penchée de l'autre côté, puis moins penchée) quand le
    // vérificateur trouve un contact.
    int redressements = 9;
    // Couches courbes : petits bouts de cordon à ne pas imprimer, parce
    // qu'ils chevaucheraient de la matière déjà posée (au ras du pied d'une
    // branche, par exemple). Chacun : le numéro de sa couche (celui du
    // commentaire « ;COUCHE: » du G-code) et le centre d'une petite boule,
    // dans le repère de la pièce. C'est le slicer qui remplit cette liste,
    // d'après ce que trouve le vérificateur.
    struct Retrait {
        std::size_t couche = 0;
        Vec3 centre;
    };
    // Couches courbes : nombre de fois où, faute de mieux, les branches
    // fautives sont imprimées à plat avec le tronc et le tranchage refait.
    int replis = 6;
    // Temps accordé aux reprises, en secondes. Une fois dépassé, le slicer
    // n'essaie plus branche par branche : toutes les branches qui restent en
    // ondulation sont imprimées à plat d'un coup.
    double budget_reprises = 75.0;
    std::vector<Retrait> retraits;
    double rayon_retrait = 1.3;  // rayon de la boule, en mm
};

// Tranche une stratégie. Une stratégie à une seule partie verticale passe par
// le slicer 3 axes. Le G-code est toujours relu par le vérificateur :
// regarder « verification.valide() » avant de l'utiliser.
// Lève ErreurTranchage si une partie ne peut pas être tranchée.
ResultatTranchage trancher_strategie(const Strategie& strategie, const Reglages& reglages,
                                     const OptionsMultiaxe& options = {});

// Tranchage classique d'un modèle entier (3 axes). Si le réglage
// « peau_courbe » est coché, une dernière passe suit la forme du dessus.
ResultatTranchage trancher_classique(const Maillage& modele, const Reglages& reglages, bool deja_pose = false);

// Vrai si les couches d'une partie courbe (qui a un « guide ») peuvent être
// calculées : la courbe ne tourne pas trop serré pour l'épaisseur de la partie.
bool couches_pivotantes_possibles(const PartieImpression& partie, const ReglagesImpression& reglages);

}  // namespace artprint
