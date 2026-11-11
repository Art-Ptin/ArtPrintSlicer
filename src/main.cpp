// ArtPrint Slicer - licence AGPL v3
#include <algorithm>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "maillage.hpp"
#include "reglages.hpp"
#include "stl.hpp"
#include "verificateur.hpp"
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

// Vérifie un fichier G-code et affiche le rapport. Renvoie le code de sortie :
// 0 si le G-code est accepté, 1 s'il est refusé.
int verifier(const std::filesystem::path& chemin_gcode,
             const std::filesystem::path& chemin_reglages) {
    using namespace artprint;
    try {
        const Reglages reglages =
            chemin_reglages.empty() ? Reglages{} : lire_reglages(chemin_reglages);

        std::ifstream fichier(chemin_gcode, std::ios::binary);
        if (!fichier) {
            std::cerr << std::format("Erreur : impossible d'ouvrir « {} ».\n",
                                     chemin_en_texte(chemin_gcode));
            return 1;
        }
        const RapportVerification rapport = verifier_gcode(fichier, reglages);

        std::cout << std::format("Fichier : {}\n", chemin_en_texte(chemin_gcode));
        std::cout << std::format("Lignes lues : {}\n", rapport.lignes);
        std::cout << std::format("Extrusions : {} ({:.1f} mm)\n", rapport.extrusions,
                                 rapport.longueur_extrusions);
        std::cout << std::format("Déplacements à vide : {} ({:.1f} mm)\n",
                                 rapport.deplacements_a_vide, rapport.longueur_a_vide);
        std::cout << std::format("Rétractions : {}\n\n", rapport.retractions);

        if (rapport.valide()) {
            std::cout << "G-code ACCEPTÉ : aucune règle physique n'est violée.\n";
            return 0;
        }
        for (const ErreurVerification& erreur : rapport.erreurs) {
            std::cout << decrire(erreur) << '\n';
        }
        if (rapport.nombre_erreurs > rapport.erreurs.size()) {
            std::cout << std::format("... et {} autre(s) erreur(s) du même genre.\n\n",
                                     rapport.nombre_erreurs - rapport.erreurs.size());
        }
        std::cout << std::format("G-code REFUSÉ : {} erreur(s).\n", rapport.nombre_erreurs);
        return 1;
    } catch (const std::exception& erreur) {
        std::cerr << "Erreur : " << erreur.what() << '\n';
        return 1;
    }
}

int lancer(const std::vector<std::filesystem::path>& arguments) {
    std::cout << artprint::presentation() << '\n';
    std::cout << "Version " << artprint::version() << "\n\n";

    if (!arguments.empty() && arguments[0] == "verifier") {
        if (arguments.size() == 2) {
            return verifier(arguments[1], {});
        }
        if (arguments.size() == 3) {
            return verifier(arguments[1], arguments[2]);
        }
    } else if (arguments.size() == 1) {
        return analyser(arguments[0]);
    }
    std::cerr << "Utilisation :\n"
                 "  artprint-slicer <fichier.stl>\n"
                 "      affiche le résumé d'un modèle 3D\n"
                 "  artprint-slicer verifier <fichier.gcode> [reglages.txt]\n"
                 "      vérifie qu'un G-code respecte les règles physiques\n";
    return 2;
}

}  // namespace

#ifdef _WIN32
// Sous Windows, wmain reçoit les arguments en Unicode : les chemins
// avec accents fonctionnent.
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);  // affichage correct des accents
    return lancer(std::vector<std::filesystem::path>(argv + std::min(argc, 1), argv + argc));
}
#else
int main(int argc, char* argv[]) {
    return lancer(std::vector<std::filesystem::path>(argv + std::min(argc, 1), argv + argc));
}
#endif
