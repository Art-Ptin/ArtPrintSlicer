// ArtPrint Slicer - licence AGPL v3
// Tests du lecteur STL. Le cube de 20 mm est fabriqué ici même, en mémoire :
// aucun fichier extérieur n'est nécessaire.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "maillage.hpp"
#include "stl.hpp"

using namespace artprint;

namespace {

int echecs = 0;

#define VERIFIER(condition)                                                     \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "ÉCHEC (ligne " << __LINE__ << ") : " #condition "\n"; \
            ++echecs;                                                           \
        }                                                                       \
    } while (0)

using TriangleBrut = std::array<Point3, 3>;

// Cube de 20 mm, de (0,0,0) à (20,20,20) : 6 faces, 12 triangles,
// sommets ordonnés dans le sens inverse des aiguilles d'une montre vu de l'extérieur.
std::vector<TriangleBrut> cube_20mm() {
    auto sommet = [](int i) {
        return Point3{(i & 1) ? 20.0f : 0.0f, (i & 2) ? 20.0f : 0.0f, (i & 4) ? 20.0f : 0.0f};
    };
    const int faces[6][4] = {
        {0, 2, 3, 1},  // dessous (z = 0)
        {4, 5, 7, 6},  // dessus (z = 20)
        {0, 1, 5, 4},  // devant (y = 0)
        {2, 6, 7, 3},  // derrière (y = 20)
        {0, 4, 6, 2},  // gauche (x = 0)
        {1, 3, 7, 5},  // droite (x = 20)
    };
    std::vector<TriangleBrut> triangles;
    for (const auto& f : faces) {
        triangles.push_back({sommet(f[0]), sommet(f[1]), sommet(f[2])});
        triangles.push_back({sommet(f[0]), sommet(f[2]), sommet(f[3])});
    }
    return triangles;
}

void ecrire_octets(std::string& sortie, const void* donnees, std::size_t taille) {
    sortie.append(static_cast<const char*>(donnees), taille);
}

// Fabrique un STL binaire en mémoire.
std::string stl_binaire(const std::vector<TriangleBrut>& triangles,
                        const std::string& entete = "Cube de test ArtPrint") {
    std::string octets(80, '\0');
    std::memcpy(octets.data(), entete.data(), std::min<std::size_t>(entete.size(), 80));
    const auto nombre = static_cast<std::uint32_t>(triangles.size());
    ecrire_octets(octets, &nombre, sizeof nombre);
    for (const TriangleBrut& t : triangles) {
        const float normale[3] = {0.0f, 0.0f, 0.0f};
        ecrire_octets(octets, normale, sizeof normale);
        for (const Point3& p : t) {
            const float xyz[3] = {p.x, p.y, p.z};
            ecrire_octets(octets, xyz, sizeof xyz);
        }
        const std::uint16_t attribut = 0;
        ecrire_octets(octets, &attribut, sizeof attribut);
    }
    return octets;
}

// Fabrique un STL texte (ASCII) en mémoire.
std::string stl_ascii(const std::vector<TriangleBrut>& triangles) {
    std::string texte = "solid cube de test\n";
    for (const TriangleBrut& t : triangles) {
        texte += "  facet normal 0 0 0\n    outer loop\n";
        for (const Point3& p : t) {
            texte += std::format("      vertex {} {} {}\n", p.x, p.y, p.z);
        }
        texte += "    endloop\n  endfacet\n";
    }
    texte += "endsolid cube de test\n";
    return texte;
}

std::string remplacer_tout(std::string texte, const std::string& cherche,
                           const std::string& remplace) {
    std::size_t position = 0;
    while ((position = texte.find(cherche, position)) != std::string::npos) {
        texte.replace(position, cherche.size(), remplace);
        position += remplace.size();
    }
    return texte;
}

// Vrai si la fonction lève une ErreurStl dont le message contient « extrait ».
bool leve_erreur(const std::function<void()>& fonction, const std::string& extrait = "") {
    try {
        fonction();
    } catch (const ErreurStl& erreur) {
        const std::string message = erreur.what();
        if (message.find(extrait) == std::string::npos) {
            std::cerr << "  message reçu : " << message << '\n';
            return false;
        }
        return true;
    }
    return false;
}

// Vérifications communes à un cube de 20 mm bien formé.
void verifier_cube(const ModeleStl& modele, float decalage_x = 0.0f,
                   float decalage_y = 0.0f, float decalage_z = 0.0f) {
    VERIFIER(modele.triangles_lus == 12);
    VERIFIER(modele.triangles_degeneres == 0);
    VERIFIER(modele.maillage.triangles.size() == 12);
    VERIFIER(modele.maillage.sommets.size() == 8);  // 36 sommets lus, 8 après fusion

    const BoiteEnglobante boite = calculer_boite(modele.maillage);
    VERIFIER(boite.min.x == decalage_x && boite.max.x == decalage_x + 20.0f);
    VERIFIER(boite.min.y == decalage_y && boite.max.y == decalage_y + 20.0f);
    VERIFIER(boite.min.z == decalage_z && boite.max.z == decalage_z + 20.0f);
    const Point3 taille = boite.dimensions();
    VERIFIER(taille.x == 20.0f && taille.y == 20.0f && taille.z == 20.0f);

    const RapportFermeture fermeture = verifier_fermeture(modele.maillage);
    VERIFIER(fermeture.ferme);
    VERIFIER(fermeture.orientation_coherente);
    VERIFIER(fermeture.aretes_bord == 0);
    VERIFIER(fermeture.message.find("est fermé") != std::string::npos);
}

// --- Les tests -----------------------------------------------------------------

void test_cube_binaire() {
    const ModeleStl modele = lire_stl_memoire(stl_binaire(cube_20mm()));
    VERIFIER(modele.format == FormatStl::binaire);
    verifier_cube(modele);
}

void test_cube_ascii() {
    const ModeleStl modele = lire_stl_memoire(stl_ascii(cube_20mm()));
    VERIFIER(modele.format == FormatStl::ascii);
    verifier_cube(modele);
}

void test_cube_ascii_fins_de_ligne_windows_et_majuscules() {
    std::string texte = remplacer_tout(stl_ascii(cube_20mm()), "\n", "\r\n");
    texte = remplacer_tout(texte, "vertex", "VERTEX");
    const ModeleStl modele = lire_stl_memoire(texte);
    VERIFIER(modele.format == FormatStl::ascii);
    verifier_cube(modele);
}

void test_binaire_dont_l_entete_commence_par_solid() {
    // Piège classique : un fichier binaire dont l'en-tête commence par « solid ».
    const ModeleStl modele = lire_stl_memoire(stl_binaire(cube_20mm(), "solid piege"));
    VERIFIER(modele.format == FormatStl::binaire);
    verifier_cube(modele);
}

void test_cube_decale() {
    std::vector<TriangleBrut> triangles = cube_20mm();
    for (TriangleBrut& t : triangles) {
        for (Point3& p : t) {
            p.x += 10.0f;
            p.y -= 5.0f;
            p.z += 3.0f;
        }
    }
    verifier_cube(lire_stl_memoire(stl_binaire(triangles)), 10.0f, -5.0f, 3.0f);
}

void test_cube_avec_un_trou() {
    std::vector<TriangleBrut> triangles = cube_20mm();
    triangles.pop_back();  // on retire un triangle : la surface a un trou
    const ModeleStl modele = lire_stl_memoire(stl_binaire(triangles));
    const RapportFermeture fermeture = verifier_fermeture(modele.maillage);
    VERIFIER(!fermeture.ferme);
    VERIFIER(fermeture.aretes_bord == 3);
    VERIFIER(fermeture.message.find("n'est pas fermé") != std::string::npos);
    VERIFIER(fermeture.message.find("3 arêtes de bord") != std::string::npos);
}

void test_triangle_retourne() {
    std::vector<TriangleBrut> triangles = cube_20mm();
    std::swap(triangles[0][1], triangles[0][2]);  // un triangle à l'envers
    const ModeleStl modele = lire_stl_memoire(stl_ascii(triangles));
    const RapportFermeture fermeture = verifier_fermeture(modele.maillage);
    VERIFIER(fermeture.ferme);                   // pas de trou...
    VERIFIER(!fermeture.orientation_coherente);  // ...mais un triangle à l'envers
    VERIFIER(fermeture.aretes_mal_orientees == 3);
}

void test_triangle_degenere_ignore() {
    std::vector<TriangleBrut> triangles = cube_20mm();
    const Point3 p{5.0f, 5.0f, 5.0f};
    triangles.push_back({p, p, Point3{6.0f, 5.0f, 5.0f}});
    const ModeleStl modele = lire_stl_memoire(stl_binaire(triangles));
    VERIFIER(modele.triangles_lus == 13);
    VERIFIER(modele.triangles_degeneres == 1);
    VERIFIER(modele.maillage.triangles.size() == 12);
    VERIFIER(verifier_fermeture(modele.maillage).ferme);
    // Le sommet isolé du triangle ignoré ne compte pas dans la boîte.
    VERIFIER(calculer_boite(modele.maillage).max.x == 20.0f);
}

void test_lecture_depuis_un_fichier() {
    const std::filesystem::path chemin =
        std::filesystem::temp_directory_path() / "artprint_test_cube_20mm.stl";
    {
        std::ofstream fichier(chemin, std::ios::binary);
        const std::string octets = stl_binaire(cube_20mm());
        fichier.write(octets.data(), static_cast<std::streamsize>(octets.size()));
    }
    verifier_cube(lire_stl(chemin));
    std::filesystem::remove(chemin);

    VERIFIER(leve_erreur([&] { lire_stl(chemin); }, "Impossible d'ouvrir"));
}

void test_fichiers_invalides() {
    // Binaire tronqué : la taille ne correspond plus au nombre de triangles.
    std::string tronque = stl_binaire(cube_20mm());
    tronque.resize(tronque.size() - 10);
    VERIFIER(leve_erreur([&] { lire_stl_memoire(tronque); }, "binaire invalide"));

    // Texte sans « endsolid » final.
    std::string sans_fin = stl_ascii(cube_20mm());
    sans_fin = sans_fin.substr(0, sans_fin.find("endsolid"));
    VERIFIER(leve_erreur([&] { lire_stl_memoire(sans_fin); }, "incomplet"));

    // Texte avec un nombre illisible : le message donne la ligne.
    const std::string nombre_faux =
        remplacer_tout(stl_ascii(cube_20mm()), "vertex 0 0 0", "vertex 0 abc 0");
    VERIFIER(leve_erreur([&] { lire_stl_memoire(nombre_faux); }, "ligne 4"));

    // Fichier vide.
    VERIFIER(leve_erreur([&] { lire_stl_memoire(""); }, "trop court"));

    // Aucun triangle.
    VERIFIER(leve_erreur([&] { lire_stl_memoire("solid vide\nendsolid vide\n"); },
                         "aucun triangle"));
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"cube binaire", test_cube_binaire},
        {"cube texte", test_cube_ascii},
        {"cube texte, fins de ligne Windows et majuscules",
         test_cube_ascii_fins_de_ligne_windows_et_majuscules},
        {"binaire dont l'en-tête commence par solid",
         test_binaire_dont_l_entete_commence_par_solid},
        {"cube décalé", test_cube_decale},
        {"cube avec un trou", test_cube_avec_un_trou},
        {"triangle retourné", test_triangle_retourne},
        {"triangle dégénéré ignoré", test_triangle_degenere_ignore},
        {"lecture depuis un fichier", test_lecture_depuis_un_fichier},
        {"fichiers invalides", test_fichiers_invalides},
    };

    for (const auto& [nom, test] : tests) {
        const int avant = echecs;
        try {
            test();
        } catch (const std::exception& erreur) {
            std::cerr << "ÉCHEC : exception inattendue : " << erreur.what() << '\n';
            ++echecs;
        }
        std::cout << (echecs == avant ? "[OK]    " : "[ÉCHEC] ") << nom << '\n';
    }

    if (echecs == 0) {
        std::cout << "Tous les tests STL sont passés.\n";
        return 0;
    }
    std::cout << echecs << " vérification(s) en échec.\n";
    return 1;
}
