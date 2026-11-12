// ArtPrint Slicer - licence AGPL v3
// Couches courbes : découpe d'une partie du modèle par les surfaces d'égale
// longueur de chemin (voir champ_couches.hpp).
//
// Chaque couche est une surface courbe. Pour y tracer les parois et le
// remplissage avec les outils du slicer (qui travaillent à plat), la couche
// est vue « d'en face » : on la regarde le long de sa direction moyenne et on
// dessine son contour sur un plan, sa « carte ». Les trajectoires sont
// calculées sur la carte, puis chaque point est reposé sur la surface.
#pragma once

#include <cstddef>
#include <vector>

#include "champ_couches.hpp"
#include "geometrie.hpp"
#include "maillage.hpp"
#include "polygones.hpp"

namespace artprint {

struct CoucheCourbe {
    std::size_t niveau = 0;  // numéro de la couche dans tout le modèle
    // La carte : un plan (origine, u, v) regardé le long de w, la direction
    // moyenne de la couche.
    Vec3 origine, u{1, 0, 0}, v{0, 1, 0}, w{0, 0, 1};
    Polygones contour;       // contour de la matière, dessiné sur la carte
    // Plus grand angle, en degrés, entre la surface et le plan de la carte.
    double bombement = 0.0;
};

// Découpe une partie (volume fermé) en couches courbes. « dessus[k] » est la
// longueur de chemin du dessus de la couche k, « epaisseurs[k] » son
// épaisseur ; les couches où la partie n'a pas de matière sont absentes du
// résultat. « numero » sert aux messages d'erreur.
// Lève ErreurTranchage si la partie ne peut pas être découpée ainsi.
std::vector<CoucheCourbe> trancher_selon_champ(const Maillage& partie, const ChampCouches& champ,
                                               const std::vector<double>& dessus,
                                               const std::vector<double>& epaisseurs,
                                               std::size_t numero);

// Point de la surface de niveau « niveau » qui se trouve à l'aplomb du point
// (x, y) de la carte : la carte est quittée le long de w jusqu'à la surface.
// Rend « faux » si la surface n'est pas trouvée. « hauteur » : distance entre
// la carte et la surface ; en entrée, une première idée (celle du point
// précédent), en sortie la valeur trouvée.
bool poser_sur_la_surface(const ChampCouches& champ, const CoucheCourbe& couche, const Point2& p,
                          double niveau, double& hauteur, Vec3& point);
bool poser_sur_la_surface(const ChampCouches& champ, const CoucheCourbe& couche, const Point2& p,
                          double niveau, Vec3& point);

}  // namespace artprint
