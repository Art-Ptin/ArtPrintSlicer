// ArtPrint Slicer - licence AGPL v3
// Banc d'essai des stratégies : fabrique une série d'objets (arbres, châssis
// de drone, chandelier, crochet, tuyau, champignon, tasse, main, équerre,
// arbre à deux niveaux, buisson, potence),
// demande les stratégies proposées pour chacun, les tranche toutes et fait
// relire chaque G-code par le vérificateur.
//
// Utilisation : banc_strategies [reglages.txt ou -] [dossier pour écrire les STL] [filtre de nom]
// Sans fichier de réglages, une machine d'essai inventée est utilisée.
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "decoupe.hpp"
#include "formes_organiques.hpp"
#include "modeles_quarante.hpp"
#include "modeles_vingt.hpp"
#include "modeles_essai.hpp"
#include "multiaxe.hpp"
#include "slicer.hpp"
#include "strategie.hpp"

// Les essais portent aussi sur les anciennes stratégies à couches orientées,
// que le slicer ne propose plus d'ordinaire.
static std::vector<artprint::Strategie> toutes_les_strategies(const artprint::Maillage& pose,
                                                              const artprint::Reglages& reglages) {
    artprint::OptionsStrategie options;
    options.couches_orientees = true;
    return artprint::proposer_strategies(pose, reglages, options);
}

using namespace artprint;

namespace {

void ecrire_stl(const Maillage& m, const std::filesystem::path& chemin) {
    std::ofstream f(chemin, std::ios::binary);
    char entete[80] = "objet d'essai ArtPrint";
    f.write(entete, 80);
    const auto n = static_cast<std::uint32_t>(m.triangles.size());
    f.write(reinterpret_cast<const char*>(&n), 4);
    for (const Triangle& t : m.triangles) {
        const float normale[3] = {0, 0, 0};
        f.write(reinterpret_cast<const char*>(normale), 12);
        for (int i = 0; i < 3; ++i) {
            f.write(reinterpret_cast<const char*>(&m.sommets[t.sommets[i]]), 12);
        }
        const std::uint16_t attribut = 0;
        f.write(reinterpret_cast<const char*>(&attribut), 2);
    }
}

double secondes_depuis(std::chrono::steady_clock::time_point debut) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - debut).count();
}

}  // namespace

int main(int argc, char** argv) {
    Reglages reglages;
    if (argc > 1 && std::string(argv[1]) != "-") {
        reglages = lire_reglages(argv[1]);
    } else {
        reglages.machine.distance_pivot_buse = 37.5;  // machine d'essai inventée
        reglages.machine.hauteur_bascule_b = 10.0;
    }
    const std::filesystem::path dossier = argc > 2 ? argv[2] : "";

    const std::vector<std::pair<std::string, std::function<Maillage()>>> objets = {
        {"arbre", essai::arbre},
        {"arbre_plat", essai::arbre_plat},
        {"arbre_organique", essai::arbre_organique},
        {"chassis_drone", essai::chassis_drone},
        {"chandelier", essai::chandelier},
        {"crochet", essai::crochet},
        {"tuyau_en_y", essai::tuyau_en_y},
        {"champignon", essai::champignon},
        {"tasse", essai::tasse},
        {"main_ouverte", essai::main_ouverte},
        {"equerre", essai::equerre},
        {"arbre_double", essai::arbre_double},
        {"buisson", essai::buisson},
        {"potence", essai::potence},
    };
    // Et les vingt modèles d'essai des couches courbes (noms en « v01_ »... « v20_ »).
    std::vector<std::pair<std::string, std::function<Maillage()>>> tous(objets.begin(), objets.end());
    for (auto& modele : essai::les_vingt()) {
        tous.push_back(std::move(modele));
    }
    // Et les quarante suivants (« w01_ »... « w40_ »), dont le banc d'essai.
    for (auto& modele : essai::les_quarante()) {
        tous.push_back(std::move(modele));
    }
    // Quatrième argument « stl » : écrire seulement les fichiers STL, sans trancher.
    const bool stl_seulement = argc > 4 && std::string(argv[4]) == "stl";

    // Troisième argument (facultatif) : ne traiter que les objets dont le nom contient ce texte.
    const std::string filtre = argc > 3 ? argv[3] : "";
    int refus = 0;
    for (const auto& [nom, fabriquer] : tous) {
        if (!filtre.empty() && nom.find(filtre) == std::string::npos) {
            continue;
        }
        const Maillage modele = poser_au_centre(fabriquer());
        const RapportFermeture fermeture = verifier_fermeture(modele);
        const Point3 taille = calculer_boite(modele).dimensions();
        std::cout << std::format("\n=== {} : {} triangles, {:.0f} x {:.0f} x {:.0f} mm, volume {:.1f} cm3{}\n", nom,
                                 modele.triangles.size(), taille.x, taille.y, taille.z,
                                 volume_maillage(modele) / 1000.0,
                                 fermeture.ferme && fermeture.orientation_coherente ? "" : "  !! MODELE OUVERT");
        if (!dossier.empty()) {
            ecrire_stl(modele, dossier / (nom + ".stl"));
        }
        if (stl_seulement) {
            continue;
        }
        auto debut = std::chrono::steady_clock::now();
        std::vector<Strategie> strategies;
        try {
            strategies = toutes_les_strategies(modele, reglages);
        } catch (const std::exception& erreur) {
            std::cout << "   !! stratégies impossibles : " << erreur.what() << '\n';
            ++refus;
            continue;
        }
        std::cout << std::format("   stratégies en {:.1f} s\n", secondes_depuis(debut));
        for (const Strategie& strategie : strategies) {
            std::string inclinaisons;
            for (const PartieImpression& partie : strategie.parties) {
                // « ~ » : partie courbe, dont les couches pivotent jusqu'à cette inclinaison.
                inclinaisons += std::format("{}{}{:.0f}", inclinaisons.empty() ? "" : " ",
                                            partie.guide.empty() ? "" : "~", partie.inclinaison);
            }
            std::cout << std::format("   - {} : à soutenir {:.0f} mm2, {} partie(s) [{}]", strategie.nom,
                                     strategie.surplomb, strategie.parties.size(), inclinaisons);
            debut = std::chrono::steady_clock::now();
            try {
                const ResultatTranchage r = trancher_strategie(strategie, reglages);
                std::cout << std::format(" -> {} couches, {} erreur(s), dans le vide {:.0f} mm ({:.1f} %), "
                                         "en continu {:.0f} %, {:.1f} s\n",
                                         r.couches, r.verification.nombre_erreurs,
                                         r.verification.longueur_dans_le_vide,
                                         100.0 * r.verification.longueur_dans_le_vide /
                                             std::max(1.0, r.verification.longueur_extrusions),
                                         100.0 * static_cast<double>(r.verification.extrusions_en_rotation) /
                                             std::max<double>(1.0, static_cast<double>(r.verification.extrusions)),
                                         secondes_depuis(debut));
                for (const std::string& remarque : r.remarques) {
                    std::cout << "        tranchage : " << remarque << "\n";
                }
                if (!r.verification.valide()) {
                    ++refus;
                    int montrees = 0;
                    for (const ErreurVerification& e : r.verification.erreurs) {
                        if (montrees++ < 2) {
                            std::cout << "        " << e.probleme << " (ligne " << e.ligne << ")\n";
                        }
                    }
                }
            } catch (const std::exception& erreur) {
                ++refus;
                std::cout << " -> REFUS : " << erreur.what() << '\n';
            }
            for (const std::string& remarque : strategie.remarques) {
                std::cout << "        note : " << remarque << '\n';
            }
            std::cout << std::flush;  // pour suivre l'avancement d'un banc long
        }
    }
    std::cout << std::format("\n{} tranchage(s) refusé(s).\n", refus);
    return refus == 0 ? 0 : 1;
}
