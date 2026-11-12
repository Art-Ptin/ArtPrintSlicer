// ArtPrint Slicer - licence AGPL v3
// Peau courbe : une dernière passe qui suit la forme du dessus de la pièce.
//
// Avec des couches planes, un dessus bombé est imprimé en marches d'escalier.
// La peau courbe les efface : l'intérieur reste en couches planes, mais elles
// s'arrêtent un peu sous la surface ; la place laissée est remplie par une
// passe dont la buse monte et descend pour suivre la vraie forme du dessus.
//
// Seul le dessus visible d'en haut est concerné, là où sa pente ne dépasse
// pas le réglage « peau_courbe_pente_max ». Pendant cette passe, la tête
// s'incline pour rester d'aplomb sur la surface (voir multiaxe.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "geometrie.hpp"
#include "maillage.hpp"
#include "polygones.hpp"

namespace artprint {

struct RangementPeau;

class PeauCourbe {
public:
    // Vrai si ce point de l'espace est à l'intérieur du modèle.
    bool dans_la_matiere(const Vec3& point) const;
    // Vrai s'il n'y a pas de dessus bombé à lisser sur cette partie.
    bool vide() const { return faces_.empty(); }
    // La partie, dont le dessus a été abaissé pour laisser la place de la peau.
    const Maillage& abaisse() const { return abaisse_; }
    // La zone couverte par la peau, vue de dessus.
    const Polygones& zone() const { return zone_; }
    // Point de la surface d'origine à l'aplomb de (x, y), et sa normale.
    // Rend « faux » hors de la zone.
    bool poser(const Point2& p, Vec3& point, Vec3& normale) const;

private:
    friend PeauCourbe preparer_peau_courbe(const Maillage&, const std::vector<const Maillage*>&, double, double,
                                           double);
    struct Face {
        Vec3 a, b, c, normale;
    };
    std::vector<Face> faces_;
    // Rangement des faces par cases, vues de dessus, pour les retrouver vite.
    double case_ = 2.0;
    double x0_ = 0.0, y0_ = 0.0;
    int nx_ = 0, ny_ = 0;
    std::vector<std::vector<std::uint32_t>> cases_;
    Maillage abaisse_;
    Polygones zone_;
    std::shared_ptr<const RangementPeau> rangement_;  // tous les triangles du modèle
};

// Cherche le dessus bombé d'une partie posée dans le repère de la pièce.
// « tout » : toutes les parties du modèle (une face cachée sous une autre
// partie n'est pas un dessus). « epaisseur » : épaisseur de la peau en mm.
// « pente_max » : pente la plus raide suivie, en degrés depuis l'horizontale.
// « premiere_couche » : épaisseur de la première couche (les suivantes ont
// l'épaisseur de la peau), pour savoir où tombent les couches planes.
PeauCourbe preparer_peau_courbe(const Maillage& partie, const std::vector<const Maillage*>& tout,
                                double epaisseur, double pente_max, double premiere_couche);

}  // namespace artprint
