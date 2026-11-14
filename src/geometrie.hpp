// ArtPrint Slicer - licence AGPL v3
// Petits outils de géométrie : vecteur 3D en double précision et angles.
#pragma once

#include <cmath>
#include <numbers>

namespace artprint {

// Un vecteur ou un point dans l'espace, en millimètres.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

constexpr Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(const Vec3& a, double k) { return {a.x * k, a.y * k, a.z * k}; }
constexpr Vec3 operator*(double k, const Vec3& a) { return a * k; }

constexpr double produit_scalaire(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 produit_vectoriel(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double norme(const Vec3& a) { return std::sqrt(produit_scalaire(a, a)); }

// Rend le vecteur de longueur 1 (ou le vecteur nul s'il est nul).
inline Vec3 normaliser(const Vec3& a) {
    const double n = norme(a);
    return n > 0.0 ? a * (1.0 / n) : Vec3{};
}

// Tourne le vecteur autour de l'axe Z vertical. Angle positif : sens inverse
// des aiguilles d'une montre, vu de dessus.
inline Vec3 tourner_autour_de_z(const Vec3& a, double angle_radians) {
    const double c = std::cos(angle_radians);
    const double s = std::sin(angle_radians);
    return {c * a.x - s * a.y, s * a.x + c * a.y, a.z};
}

constexpr double en_radians(double degres) { return degres * std::numbers::pi / 180.0; }
constexpr double en_degres(double radians) { return radians * 180.0 / std::numbers::pi; }

}  // namespace artprint
