// ArtPrint Slicer - licence AGPL v3
// Maillage triangulaire : sommets partagés, boîte englobante, contrôle de fermeture.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace artprint {

// Un point dans l'espace, en millimètres.
struct Point3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Un triangle : les numéros (indices) de ses trois sommets dans Maillage::sommets.
// L'ordre des sommets donne l'orientation : vu de l'extérieur, ils tournent
// dans le sens inverse des aiguilles d'une montre.
struct Triangle {
    std::array<std::uint32_t, 3> sommets{};
};

// Un maillage : chaque sommet n'est stocké qu'une fois, les triangles y font référence.
struct Maillage {
    std::vector<Point3> sommets;
    std::vector<Triangle> triangles;
};

// La plus petite boîte alignée sur les axes qui contient tout le modèle.
struct BoiteEnglobante {
    Point3 min;
    Point3 max;

    // Taille de la boîte en X, Y et Z.
    Point3 dimensions() const {
        return {max.x - min.x, max.y - min.y, max.z - min.z};
    }
};

// Calcule la boîte englobante des sommets utilisés par les triangles.
// Lève std::invalid_argument si le maillage n'a aucun triangle.
BoiteEnglobante calculer_boite(const Maillage& maillage);

// Construit un maillage triangle par triangle en fusionnant les sommets
// identiques (mêmes coordonnées exactes).
class ConstructeurMaillage {
public:
    // Ajoute un triangle. Renvoie false s'il est dégénéré (deux sommets
    // confondus) : il est alors ignoré.
    bool ajouter_triangle(const Point3& a, const Point3& b, const Point3& c);

    // Rend le maillage construit. Le constructeur est vide ensuite.
    Maillage terminer();

private:
    using Cle = std::array<float, 3>;

    struct HachageCle {
        std::size_t operator()(const Cle& cle) const noexcept;
    };

    std::uint32_t indice_sommet(const Point3& point);

    Maillage maillage_;
    std::unordered_map<Cle, std::uint32_t, HachageCle> index_;
};

// Résultat du contrôle de fermeture (étanchéité) d'un maillage.
struct RapportFermeture {
    // Vrai si chaque arête est partagée par exactement deux triangles :
    // la surface n'a ni trou ni arête « en trop ».
    bool ferme = false;
    // Vrai si deux triangles voisins sont toujours orientés dans le même sens.
    bool orientation_coherente = false;

    std::size_t aretes_bord = 0;           // arêtes d'un seul triangle : bord d'un trou
    std::size_t aretes_non_manifold = 0;   // arêtes de plus de deux triangles
    std::size_t aretes_mal_orientees = 0;  // deux triangles voisins en sens contraire

    // Explication en français, prête à afficher.
    std::string message;
};

RapportFermeture verifier_fermeture(const Maillage& maillage);

}  // namespace artprint
