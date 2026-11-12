// ArtPrint Slicer - licence AGPL v3
// Trajectoires : transforme les contours de chaque couche en chemins que la
// buse va suivre (jupe, parois, remplissage plein, remplissage intérieur).
#pragma once

#include <string_view>
#include <vector>

#include "polygones.hpp"
#include "reglages.hpp"

namespace artprint {

enum class TypeTrait {
    jupe,
    paroi_interne,
    paroi_externe,
    remplissage_plein,
    remplissage,
};

// Nom du type en français, écrit dans les commentaires du G-code.
std::string_view nom_type(TypeTrait type);

// Un chemin d'extrusion continu, en millimètres, dans le repère de la pièce.
struct Trait {
    TypeTrait type{};
    std::vector<Point2> points;
    bool ferme = false;  // vrai : boucle, le dernier point rejoint le premier
    // Largeur du cordon de ce trait, en mm. 0 : la largeur d'extrusion des
    // réglages. Autre valeur : un cordon unique, plus étroit ou plus large,
    // posé au milieu d'un passage où deux cordons ne tiennent pas.
    double largeur = 0.0;
};

struct CoucheTrajectoires {
    double z = 0.0;        // hauteur de la buse pendant cette couche
    double hauteur = 0.0;  // épaisseur de la couche
    std::vector<Trait> traits;  // dans l'ordre d'impression
};

// « contours » : les contours de la matière pour chaque couche, du bas vers
// le haut. La couche 0 a l'épaisseur « hauteur_premiere_couche ».
// « pleins » (facultatif) : pour chaque couche, une zone à remplir en plein
// même si elle est recouverte (par exemple sous le pied d'une branche).
std::vector<CoucheTrajectoires> calculer_trajectoires(const std::vector<Polygones>& contours,
                                                      const ReglagesImpression& reglages,
                                                      const std::vector<Polygones>* pleins = nullptr);

// Lignes parallèles qui remplissent une zone, reliées en zigzag quand c'est
// possible. « angle » en degrés, « espacement » en mm entre deux lignes.
std::vector<std::vector<Point2>> lignes_de_remplissage(const Polygones& zone, double espacement,
                                                       double angle);

}  // namespace artprint
