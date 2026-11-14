// ArtPrint Slicer - licence AGPL v3
// Tests des commandes utilisées par l'interface graphique : un parcours
// complet, de l'ouverture du modèle à l'enregistrement du G-code.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "api.hpp"

using namespace artprint;
using Json = nlohmann::json;

namespace {

int echecs = 0;

#define VERIFIER(condition)                                                     \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "ÉCHEC (ligne " << __LINE__ << ") : " #condition "\n"; \
            ++echecs;                                                           \
        }                                                                       \
    } while (0)

Json appeler(Session& session, const Json& demande) {
    return Json::parse(session.executer(demande.dump()));
}

std::string chemin_utf8(const std::filesystem::path& chemin) {
    const std::u8string u8 = chemin.u8string();
    return std::string(u8.begin(), u8.end());
}

// Écrit un cube de 10 mm en STL texte. Le nom contient un accent, exprès.
std::filesystem::path ecrire_cube() {
    const std::filesystem::path chemin =
        std::filesystem::temp_directory_path() / std::filesystem::path(u8"artprint_cube_é.stl");
    std::ofstream f(chemin, std::ios::binary);
    const int faces[12][3] = {{0, 2, 3}, {0, 3, 1}, {4, 5, 7}, {4, 7, 6}, {0, 1, 5}, {0, 5, 4},
                              {2, 6, 7}, {2, 7, 3}, {0, 4, 6}, {0, 6, 2}, {1, 3, 7}, {1, 7, 5}};
    f << "solid cube\n";
    for (const auto& face : faces) {
        f << "facet normal 0 0 0\nouter loop\n";
        for (const int s : face) {
            f << "vertex " << ((s & 1) ? 10 : 0) << ' ' << ((s & 2) ? 10 : 0) << ' '
              << ((s & 4) ? 10 : 0) << '\n';
        }
        f << "endloop\nendfacet\n";
    }
    f << "endsolid cube\n";
    return chemin;
}

void test_infos_et_reglages() {
    Session session;
    const Json infos = appeler(session, {{"commande", "infos"}});
    VERIFIER(infos["ok"] == true && infos["version"] == "0.1.0");

    const Json reglages = appeler(session, {{"commande", "reglages"}});
    VERIFIER(reglages["ok"] == true);
    VERIFIER(reglages["reglages"].size() > 30);
    bool trouve = false;
    for (const Json& r : reglages["reglages"]) {
        VERIFIER(r.contains("cle") && r.contains("groupe") && r.contains("libelle") &&
                 r.contains("aide") && r.contains("type") && r.contains("valeur"));
        if (r["cle"] == "hauteur_couche") {
            trouve = true;
            VERIFIER(r["valeur"] == "0,2" && r["unite"] == "mm" && r["type"] == "nombre");
        }
        if (r["cle"] == "distance_pivot_buse") {
            VERIFIER(r["facultatif"] == true && r["valeur"] == "");
        }
        if (r["cle"] == "axe_pivot_b") {
            VERIFIER(r["type"] == "choix" && r["choix"].size() == 2 && r["valeur"] == "Y");
        }
    }
    VERIFIER(trouve);
}

void test_changer_les_reglages() {
    Session session;
    const Json reponse = appeler(
        session, {{"commande", "definir_reglages"},
                  {"valeurs",
                   {{"hauteur_couche", "0,3"},
                    {"nombre_parois", 3},
                    {"densite_remplissage", "150"},  // refusé : plus de 100 %
                    {"distance_pivot_buse", "37,5"},
                    {"inconnu", "1"}}}});
    VERIFIER(reponse["ok"] == true);
    VERIFIER(reponse["refus"].size() == 2);
    VERIFIER(reponse["refus"].contains("densite_remplissage") && reponse["refus"].contains("inconnu"));
    for (const Json& r : reponse["reglages"]) {
        if (r["cle"] == "hauteur_couche") VERIFIER(r["valeur"] == "0,3");
        if (r["cle"] == "nombre_parois") VERIFIER(r["valeur"] == "3");
        if (r["cle"] == "densite_remplissage") VERIFIER(r["valeur"] == "15");
        if (r["cle"] == "distance_pivot_buse") VERIFIER(r["valeur"] == "37,5");
    }

    // Enregistrement puis relecture : les valeurs reviennent à l'identique.
    const std::string fichier =
        chemin_utf8(std::filesystem::temp_directory_path() / "artprint_reglages_test.txt");
    VERIFIER(appeler(session, {{"commande", "enregistrer_reglages"}, {"chemin", fichier}})["ok"] == true);
    VERIFIER(appeler(session, {{"commande", "reglages_par_defaut"}})["ok"] == true);
    const Json relu = appeler(session, {{"commande", "charger_reglages"}, {"chemin", fichier}});
    VERIFIER(relu["ok"] == true);
    for (const Json& r : relu["reglages"]) {
        if (r["cle"] == "hauteur_couche") VERIFIER(r["valeur"] == "0,3");
        if (r["cle"] == "distance_pivot_buse") VERIFIER(r["valeur"] == "37,5");
    }
    std::filesystem::remove(std::filesystem::path(fichier));
}

void test_parcours_complet() {
    Session session;
    // Rien n'est ouvert : trancher et enregistrer sont refusés proprement.
    VERIFIER(appeler(session, {{"commande", "trancher"}})["ok"] == false);
    VERIFIER(appeler(session, {{"commande", "strategies"}})["ok"] == false);
    VERIFIER(appeler(session, {{"commande", "enregistrer_gcode"}, {"chemin", "x.gcode"}})["ok"] == false);

    const std::filesystem::path stl = ecrire_cube();
    const Json modele = appeler(session, {{"commande", "charger_modele"}, {"chemin", chemin_utf8(stl)}});
    VERIFIER(modele["ok"] == true);
    VERIFIER(modele["modele"]["triangles"] == 12 && modele["modele"]["ferme"] == true);
    VERIFIER(modele["modele"]["positions"].size() == 12 * 9);
    VERIFIER(modele["modele"]["dimensions"] == Json::array({10.0, 10.0, 10.0}));
    // Le modèle est posé au centre du plateau.
    VERIFIER(modele["modele"]["positions"][0] == -5.0);

    // Stratégies : pour un simple cube, seule la stratégie classique est proposée.
    const Json strategies = appeler(session, {{"commande", "strategies"}});
    VERIFIER(strategies["ok"] == true);
    // Deux stratégies pour tout modèle : le classique, et l'ondulation (qui,
    // pour un simple cube, n'a que le corps de la pièce).
    VERIFIER(strategies["strategies"].size() == 2);
    VERIFIER(strategies["strategies"][1]["type"] == "couches_courbes");
    VERIFIER(strategies["strategies"][1]["parties"].size() == 1);
    VERIFIER(strategies["strategies"][0]["type"] == "classique");
    VERIFIER(strategies["strategies"][0]["surplomb_mm2"] == 0.0);
    VERIFIER(strategies["strategies"][0]["parties"].size() == 1);
    // Demander une stratégie qui n'est pas proposée est refusé proprement.
    const Json refus = appeler(session, {{"commande", "trancher"}, {"strategie", "par_branches"}});
    VERIFIER(refus["ok"] == false && refus["erreur"].get<std::string>().find("par_branches") != std::string::npos);

    // L'ondulation d'un cube : même tranchage que le classique, avec la
    // durée du classique pour comparer et le temps de préparation.
    const Json ondule = appeler(session, {{"commande", "trancher"}, {"strategie", "couches_courbes"}});
    VERIFIER(ondule["ok"] == true && ondule["resultat"]["verification"]["valide"] == true);
    VERIFIER(ondule["resultat"]["duree_classique_s"] == ondule["resultat"]["duree_s"]);
    VERIFIER(ondule["resultat"]["preparation_s"] == 300.0);

    // Mise à l'échelle : le cube de 10 mm devient un cube de 20 mm, puis revient.
    const Json grand = appeler(session, {{"commande", "transformer_modele"}, {"echelle", 2.0}});
    VERIFIER(grand["ok"] == true && grand["modele"]["dimensions"] == Json::array({20.0, 20.0, 20.0}));
    VERIFIER(appeler(session, {{"commande", "transformer_modele"}, {"echelle", 0.0}})["ok"] == false);
    const Json normal = appeler(session, {{"commande", "transformer_modele"}, {"echelle", 1.0}, {"orientation", "auto"}});
    VERIFIER(normal["ok"] == true && normal["modele"]["dimensions"] == Json::array({10.0, 10.0, 10.0}));
    VERIFIER(appeler(session, {{"commande", "transformer_modele"}, {"orientation", "origine"}})["ok"] == true);
    // Déplacer la pièce sur le plateau : elle n'est plus au centre, et le
    // tranchage la laisse là où elle est.
    const Json deplace = appeler(session, {{"commande", "transformer_modele"}, {"decalage", Json::array({30.0, -20.0})}});
    VERIFIER(deplace["ok"] == true && deplace["modele"]["positions"][0] == 25.0 && deplace["modele"]["positions"][1] == -25.0);
    VERIFIER(deplace["modele"]["volume_cm3"] == 1.0);
    const Json a_cote = appeler(session, {{"commande", "trancher"}});
    VERIFIER(a_cote["ok"] == true && a_cote["resultat"]["verification"]["valide"] == true);
    VERIFIER(a_cote["resultat"]["apercu"][1]["m"][0].get<double>() > 20.0);
    // Tourner : trois angles, sinon refus ; puis retour au centre, sans rotation.
    VERIFIER(appeler(session, {{"commande", "transformer_modele"}, {"rotation", Json::array({90.0})}})["ok"] == false);
    const Json remis = appeler(session, {{"commande", "transformer_modele"}, {"rotation", Json::array({90.0, 0.0, 45.0})},
                                         {"decalage", Json::array({0.0, 0.0})}});
    VERIFIER(remis["ok"] == true && remis["modele"]["dimensions"][2] == 10.0);
    VERIFIER(appeler(session, {{"commande", "transformer_modele"}, {"rotation", Json::array({0.0, 0.0, 0.0})}})["ok"] == true);

    const Json tranche = appeler(session, {{"commande", "trancher"}});
    VERIFIER(tranche["ok"] == true);
    const Json& resultat = tranche["resultat"];
    VERIFIER(resultat["couches"] == 50);
    VERIFIER(resultat["verification"]["valide"] == true);
    VERIFIER(resultat["apercu"].size() == 50);
    VERIFIER(resultat["apercu"][0]["m"].size() % 7 == 0 && resultat["apercu"][0]["m"].size() > 70);
    VERIFIER(resultat["types"].size() == 6);
    // L'aperçu contient des extrusions et des déplacements à vide.
    bool extrusion = false, vide = false;
    const Json& m = resultat["apercu"][1]["m"];
    for (std::size_t i = 6; i < m.size(); i += 7) {
        (m[i] == 5 ? vide : extrusion) = true;
    }
    VERIFIER(extrusion && vide);

    const std::filesystem::path gcode = std::filesystem::temp_directory_path() / "artprint_test.gcode";
    VERIFIER(appeler(session, {{"commande", "enregistrer_gcode"}, {"chemin", chemin_utf8(gcode)}})["ok"] == true);
    VERIFIER(std::filesystem::file_size(gcode) == resultat["taille_gcode"].get<std::uintmax_t>());
    std::filesystem::remove(gcode);
    std::filesystem::remove(stl);
}

void test_demandes_invalides() {
    Session session;
    VERIFIER(Json::parse(session.executer("ceci n'est pas du JSON"))["ok"] == false);
    VERIFIER(appeler(session, {{"commande", "danser"}})["ok"] == false);
    const Json absent = appeler(session, {{"commande", "charger_modele"}, {"chemin", "nexiste_pas.stl"}});
    VERIFIER(absent["ok"] == false && absent["erreur"].get<std::string>().size() > 5);
    // Réglages incohérents : le tranchage est refusé avec une explication.
    const std::filesystem::path stl = ecrire_cube();
    appeler(session, {{"commande", "charger_modele"}, {"chemin", chemin_utf8(stl)}});
    appeler(session, {{"commande", "definir_reglages"}, {"valeurs", {{"b_min", "50"}, {"b_max", "10"}}}});
    VERIFIER(appeler(session, {{"commande", "trancher"}})["ok"] == false);
    std::filesystem::remove(stl);
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"infos et liste des réglages", test_infos_et_reglages},
        {"changer, enregistrer et relire les réglages", test_changer_les_reglages},
        {"parcours complet : ouvrir, trancher, enregistrer", test_parcours_complet},
        {"demandes invalides", test_demandes_invalides},
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
        std::cout << "Tous les tests des commandes de l'interface sont passés.\n";
        return 0;
    }
    std::cout << echecs << " vérification(s) en échec.\n";
    return 1;
}
