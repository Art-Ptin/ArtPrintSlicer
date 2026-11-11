// ArtPrint Slicer - licence AGPL v3
// Polygones 2D d'une couche. Les calculs (décalages, unions, découpes) sont
// faits par la bibliothèque Clipper2, qui travaille en nombres entiers pour
// être exacte : 1 unité = 0,0001 mm.
#pragma once

#include <cmath>
#include <cstdint>

#include "clipper2/clipper.h"

namespace artprint {

// Un ensemble de contours fermés. Contour extérieur : sens inverse des
// aiguilles d'une montre. Trou : sens des aiguilles d'une montre.
using Polygones = Clipper2Lib::Paths64;
using Contour = Clipper2Lib::Path64;
using PointEntier = Clipper2Lib::Point64;

// Un point 2D en millimètres.
struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

inline double distance(const Point2& a, const Point2& b) {
    return std::hypot(b.x - a.x, b.y - a.y);
}

constexpr double unites_par_mm = 10000.0;

inline std::int64_t en_unites(double mm) {
    return static_cast<std::int64_t>(std::llround(mm * unites_par_mm));
}
inline PointEntier en_unites(const Point2& p) { return {en_unites(p.x), en_unites(p.y)}; }
inline Point2 en_mm(const PointEntier& p) {
    return {static_cast<double>(p.x) / unites_par_mm, static_cast<double>(p.y) / unites_par_mm};
}

// Décale les contours : distance positive vers l'extérieur (la forme grossit),
// négative vers l'intérieur (elle rétrécit). Coins gardés vifs.
inline Polygones decaler(const Polygones& polygones, double distance_mm) {
    return Clipper2Lib::InflatePaths(polygones, distance_mm * unites_par_mm,
                                     Clipper2Lib::JoinType::Miter,
                                     Clipper2Lib::EndType::Polygon, 3.0);
}

// Même chose avec des coins arrondis.
inline Polygones decaler_arrondi(const Polygones& polygones, double distance_mm) {
    return Clipper2Lib::InflatePaths(polygones, distance_mm * unites_par_mm,
                                     Clipper2Lib::JoinType::Round,
                                     Clipper2Lib::EndType::Polygon, 2.0,
                                     0.01 * unites_par_mm);
}

inline Polygones unir(const Polygones& a, const Polygones& b) {
    return Clipper2Lib::Union(a, b, Clipper2Lib::FillRule::NonZero);
}
inline Polygones intersecter(const Polygones& a, const Polygones& b) {
    return Clipper2Lib::Intersect(a, b, Clipper2Lib::FillRule::NonZero);
}
inline Polygones soustraire(const Polygones& a, const Polygones& b) {
    return Clipper2Lib::Difference(a, b, Clipper2Lib::FillRule::NonZero);
}

// Surface totale en mm² (les trous sont déduits).
inline double surface(const Polygones& polygones) {
    return Clipper2Lib::Area(polygones) / (unites_par_mm * unites_par_mm);
}

}  // namespace artprint
