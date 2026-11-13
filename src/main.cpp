// ArtPrint Slicer - licence AGPL v3
#include <cstdlib>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "api.hpp"
#include "multiaxe.hpp"
#include "strategie.hpp"
#include "maillage.hpp"
#include "reglages.hpp"
#include "slicer.hpp"
#include "stl.hpp"
#include "verificateur.hpp"
#include "version.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

// Affiche les erreurs d'un rapport de vérification.
void afficher_erreurs(const artprint::RapportVerification& rapport) {
    for (const artprint::ErreurVerification& erreur : rapport.erreurs) {
        std::cout << artprint::decrire(erreur) << '\n';
    }
    if (rapport.nombre_erreurs > rapport.erreurs.size()) {
        std::cout << std::format("... et {} autre(s) erreur(s) du même genre.\n\n",
                                 rapport.nombre_erreurs - rapport.erreurs.size());
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
        afficher_erreurs(rapport);
        std::cout << std::format("G-code REFUSÉ : {} erreur(s).\n", rapport.nombre_erreurs);
        return 1;
    } catch (const std::exception& erreur) {
        std::cerr << "Erreur : " << erreur.what() << '\n';
        return 1;
    }
}

// Tranche un modèle STL et écrit le G-code. Le fichier n'est écrit que si
// le vérificateur accepte le G-code. Renvoie le code de sortie.
// « strategie » : 0 pour le tranchage classique, sinon le numéro donné par
// la commande « strategies ».
int trancher_fichier(const std::filesystem::path& chemin_modele,
                     const std::filesystem::path& chemin_gcode,
                     const std::filesystem::path& chemin_reglages, int strategie = 0,
                     const std::vector<int>& a_plat = {}) {
    using namespace artprint;
    try {
        const Reglages reglages =
            chemin_reglages.empty() ? Reglages{} : lire_reglages(chemin_reglages);
        const ModeleStl modele = lire_stl(chemin_modele);
        const BoiteEnglobante boite = calculer_boite(modele.maillage);
        const Point3 taille = boite.dimensions();
        std::cout << std::format("Modèle : {} ({} triangles, {:.1f} x {:.1f} x {:.1f} mm)\n",
                                 chemin_en_texte(chemin_modele), modele.triangles_lus, taille.x,
                                 taille.y, taille.z);

        ResultatTranchage resultat;
        if (strategie <= 0) {
            resultat = trancher_classique(modele.maillage, reglages);
        } else {
            std::vector<Strategie> strategies = proposer_strategies(poser_au_centre(modele.maillage), reglages);
            // « --a-plat=3,5 » : ces branches (leur numéro de partie) sont
            // imprimées à plat avec le tronc, au lieu des couches courbes.
            if (!a_plat.empty() && static_cast<std::size_t>(strategie) <= strategies.size()) {
                const Strategie& base = strategies[static_cast<std::size_t>(strategie) - 1];
                OptionsStrategie options;
                for (const int numero : a_plat) {
                    if (numero < 2 || static_cast<std::size_t>(numero) > base.parties.size() ||
                        !base.parties[static_cast<std::size_t>(numero) - 1].couches_courbes) {
                        std::cerr << std::format("Erreur : la partie {} n'est pas une branche en couches courbes.\n", numero);
                        return 1;
                    }
                    options.a_plat.push_back(base.parties[static_cast<std::size_t>(numero) - 1].repere);
                }
                const TypeStrategie type = base.type;
                std::vector<Strategie> nouvelles = proposer_strategies(poser_au_centre(modele.maillage), reglages, options);
                strategie = 1;
                for (std::size_t n = 0; n < nouvelles.size(); ++n) {
                    if (nouvelles[n].type == type) {
                        strategie = static_cast<int>(n) + 1;
                    }
                }
                strategies = std::move(nouvelles);
                std::cout << std::format("{} branche(s) imprimée(s) à plat, à la demande.\n", a_plat.size());
            }
            if (static_cast<std::size_t>(strategie) > strategies.size()) {
                std::cerr << std::format(
                    "Erreur : la stratégie {} n'existe pas pour ce modèle ({} proposée(s)).\n",
                    strategie, strategies.size());
                return 1;
            }
            const Strategie& choisie = strategies[static_cast<std::size_t>(strategie) - 1];
            std::cout << std::format("Stratégie : {} ({} partie(s))\n", choisie.nom,
                                     choisie.parties.size());
            // La stratégie classique passe par le même chemin que sans
            // « --strategie » : mêmes contrôles du modèle, mêmes messages.
            resultat = choisie.type == TypeStrategie::classique ? trancher_classique(modele.maillage, reglages)
                                                                : trancher_strategie(choisie, reglages);
        }
        const int minutes = static_cast<int>(resultat.duree_estimee / 60.0);
        std::cout << std::format("Couches : {}\n", resultat.couches);
        std::cout << std::format("Filament : {:.2f} m\n", resultat.longueur_filament / 1000.0);
        std::cout << std::format("Durée estimée : {} h {:02} min (sans les accélérations)\n",
                                 minutes / 60, minutes % 60);
        std::cout << std::format("Extrusions : {}   Déplacements à vide : {}\n",
                                 resultat.verification.extrusions,
                                 resultat.verification.deplacements_a_vide);
        if (resultat.verification.extrusions_en_rotation > 0) {
            std::cout << std::format("5 axes continu : {} extrusions pendant lesquelles la tête ou le plateau tourne\n",
                                     resultat.verification.extrusions_en_rotation);
        }
        std::cout << std::format("Parois imprimées dans le vide (à soutenir) : {:.0f} mm sur {:.0f} mm de cordon\n",
                                 resultat.verification.longueur_dans_le_vide,
                                 resultat.verification.longueur_extrusions);
        for (const std::string& remarque : resultat.remarques) {
            std::cout << "Remarque : " << remarque << "\n";
        }
        std::cout << "\n";

        if (!resultat.verification.valide()) {
            afficher_erreurs(resultat.verification);
            // Pour le diagnostic seulement : la variable d'environnement
            // ARTPRINT_GCODE_REFUSE donne un fichier où garder le G-code
            // refusé, pour l'étudier. Il ne doit jamais être imprimé.
            if (const char* diagnostic = std::getenv("ARTPRINT_GCODE_REFUSE")) {
                std::ofstream(diagnostic, std::ios::binary) << "; G-CODE REFUSE - NE PAS IMPRIMER\n" << resultat.gcode;
            }
            std::cout << std::format(
                "Tranchage REFUSÉ : le G-code viole {} règle(s) physique(s). "
                "Aucun fichier n'a été écrit.\n",
                resultat.verification.nombre_erreurs);
            return 1;
        }

        std::ofstream sortie(chemin_gcode, std::ios::binary);
        sortie << resultat.gcode;
        if (!sortie) {
            std::cerr << std::format("Erreur : impossible d'écrire « {} ».\n",
                                     chemin_en_texte(chemin_gcode));
            return 1;
        }
        std::cout << std::format("G-code vérifié et écrit dans : {}\n",
                                 chemin_en_texte(chemin_gcode));
        return 0;
    } catch (const std::exception& erreur) {
        std::cerr << "Erreur : " << erreur.what() << '\n';
        return 1;
    }
}

// Affiche les stratégies d'impression possibles pour un modèle.
int lister_strategies(const std::filesystem::path& chemin_modele,
                      const std::filesystem::path& chemin_reglages) {
    using namespace artprint;
    try {
        const Reglages reglages =
            chemin_reglages.empty() ? Reglages{} : lire_reglages(chemin_reglages);
        const ModeleStl modele = lire_stl(chemin_modele);
        const std::vector<Strategie> strategies =
            proposer_strategies(poser_au_centre(modele.maillage), reglages);
        std::cout << std::format("Modèle : {}\n\n", chemin_en_texte(chemin_modele));
        for (std::size_t s = 0; s < strategies.size(); ++s) {
            const Strategie& strategie = strategies[s];
            std::cout << std::format("{}. {}\n   {}\n   Surface à soutenir : {:.0f} mm²\n", s + 1,
                                     strategie.nom, strategie.description, strategie.surplomb);
            if (strategie.parties.size() > 1) {
                for (std::size_t p = 0; p < strategie.parties.size(); ++p) {
                    const PartieImpression& partie = strategie.parties[p];
                    std::cout << std::format(
                        "   - partie {} : inclinée de {:.0f}°, volume {:.1f} cm³, à soutenir {:.0f} mm²{}\n",
                        p + 1, partie.inclinaison, partie.volume / 1000.0, partie.surplomb,
                        partie.parent < 0 ? ", posée sur le plateau"
                                          : std::format(", posée sur la partie {}", partie.parent + 1));
                }
            }
            for (const std::string& remarque : strategie.remarques) {
                std::cout << "   Remarque : " << remarque << '\n';
            }
            std::cout << '\n';
        }
        std::cout << "Pour trancher avec l'une d'elles :\n"
                     "  artprint-slicer trancher <modele.stl> <sortie.gcode> [reglages.txt] --strategie=<numéro>\n";
        return 0;
    } catch (const std::exception& erreur) {
        std::cerr << "Erreur : " << erreur.what() << '\n';
        return 1;
    }
}

// Mode « api » : lit une demande JSON par ligne, écrit une réponse JSON par
// ligne. Sert aux essais de l'interface graphique.
int servir_api() {
    artprint::Session session;
    std::string demande;
    while (std::getline(std::cin, demande)) {
        if (!demande.empty()) {
            std::cout << session.executer(demande) << std::endl;
        }
    }
    return 0;
}

int lancer(std::vector<std::filesystem::path> arguments) {
    if (arguments.size() == 1 && arguments[0] == "api") {
        return servir_api();
    }
    // Option « --a-plat=3,5 », tout à la fin de la commande « trancher ».
    std::vector<int> a_plat;
    if (!arguments.empty()) {
        const std::string dernier = arguments.back().string();
        const std::string option = "--a-plat=";
        if (dernier.rfind(option, 0) == 0) {
            std::stringstream liste(dernier.substr(option.size()));
            for (std::string numero; std::getline(liste, numero, ',');) {
                try {
                    a_plat.push_back(std::stoi(numero));
                } catch (const std::exception&) {
                    std::cerr << "Erreur : « --a-plat= » doit être suivi de numéros de partie séparés par des virgules.\n";
                    return 2;
                }
            }
            arguments.pop_back();
        }
    }
    // Option « --strategie=N », à la fin de la commande « trancher ».
    int strategie = 0;
    if (!arguments.empty()) {
        const std::string dernier = arguments.back().string();
        const std::string option = "--strategie=";
        if (dernier.rfind(option, 0) == 0) {
            try {
                strategie = std::stoi(dernier.substr(option.size()));
            } catch (const std::exception&) {
                strategie = -1;
            }
            if (strategie < 1) {
                std::cerr << "Erreur : « --strategie= » doit être suivi d'un numéro (1, 2, 3...).\n";
                return 2;
            }
            arguments.pop_back();
        }
    }
    std::cout << artprint::presentation() << '\n';
    std::cout << "Version " << artprint::version() << "\n\n";

    if (!arguments.empty() && arguments[0] == "verifier") {
        if (arguments.size() == 2) {
            return verifier(arguments[1], {});
        }
        if (arguments.size() == 3) {
            return verifier(arguments[1], arguments[2]);
        }
    } else if (!arguments.empty() && arguments[0] == "trancher") {
        if (arguments.size() == 3) {
            return trancher_fichier(arguments[1], arguments[2], {}, strategie, a_plat);
        }
        if (arguments.size() == 4) {
            return trancher_fichier(arguments[1], arguments[2], arguments[3], strategie, a_plat);
        }
    } else if (!arguments.empty() && arguments[0] == "strategies") {
        if (arguments.size() == 2) {
            return lister_strategies(arguments[1], {});
        }
        if (arguments.size() == 3) {
            return lister_strategies(arguments[1], arguments[2]);
        }
    } else if (arguments.size() == 1) {
        return analyser(arguments[0]);
    }
    std::cerr << "Utilisation :\n"
                 "  artprint-slicer <fichier.stl>\n"
                 "      affiche le résumé d'un modèle 3D\n"
                 "  artprint-slicer trancher <modele.stl> <sortie.gcode> [reglages.txt] [--strategie=N] [--a-plat=3,5]\n"
                 "      tranche un modèle et écrit le G-code vérifié (3 axes, ou la stratégie N) ;\n"
                 "      « --a-plat » : ces branches (numéros de partie) sont imprimées à plat avec le tronc\n"
                 "  artprint-slicer strategies <modele.stl> [reglages.txt]\n"
                 "      propose les façons d'imprimer le modèle (3 axes, couches courbes en 5 axes)\n"
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
