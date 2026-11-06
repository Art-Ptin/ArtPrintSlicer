// ArtPrint Slicer - licence AGPL v3
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>

#include "maillage.hpp"
#include "stl.hpp"
#include "version.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

void afficher_axe(const char* nom, float min, float max) {
    std::cout << std::format("  {} : {:.3f} mm (de {:.3f} à {:.3f})\n", nom, max - min, min, max);
}

// Lit le fichier STL et affiche son résumé. Renvoie le code de sortie.
int analyser(const std::filesystem::path& chemin) {
    using namespace artprint;
    try {
        const ModeleStl modele = lire_stl(chemin);
        const BoiteEnglobante boite = calculer_boite(modele.maillage);
        const RapportFermeture fermeture = verifier_fermeture(modele.maillage);

        std::cout << std::format("Fichier : {}\n", chemin_en_texte(chemin));
        std::cout << std::format("Format : {}\n", nom_format(modele.format));
        std::cout << std::format("Triangles : {}\n", modele.triangles_lus);
        if (modele.triangles_degeneres > 0) {
            std::cout << std::format("Triangles dégénérés ignorés : {}\n",
                                     modele.triangles_degeneres);
        }
        std::cout << std::format("Sommets distincts : {}\n", modele.maillage.sommets.size());
        std::cout << "Dimensions :\n";
        afficher_axe("X", boite.min.x, boite.max.x);
        afficher_axe("Y", boite.min.y, boite.max.y);
        afficher_axe("Z", boite.min.z, boite.max.z);
        std::cout << std::format("Modèle fermé : {}\n", fermeture.ferme ? "oui" : "non");
        if (!fermeture.ferme || !fermeture.orientation_coherente) {
            std::cout << fermeture.message << '\n';
        }
        return 0;
    } catch (const std::exception& erreur) {
        std::cerr << "Erreur : " << erreur.what() << '\n';
        return 1;
    }
}

int lancer(int nombre_arguments, const std::filesystem::path& chemin) {
    std::cout << artprint::presentation() << '\n';
    std::cout << "Version " << artprint::version() << "\n\n";

    if (nombre_arguments < 2) {
        std::cerr << "Utilisation : artprint-slicer <fichier.stl>\n";
        return 2;
    }
    return analyser(chemin);
}

}  // namespace

#ifdef _WIN32
// Sous Windows, wmain reçoit les arguments en Unicode : les chemins
// avec accents fonctionnent.
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);  // affichage correct des accents
    return lancer(argc, argc >= 2 ? std::filesystem::path(argv[1]) : std::filesystem::path());
}
#else
int main(int argc, char* argv[]) {
    return lancer(argc, argc >= 2 ? std::filesystem::path(argv[1]) : std::filesystem::path());
}
#endif
