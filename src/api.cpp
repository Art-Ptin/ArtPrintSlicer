// ArtPrint Slicer - licence AGPL v3
#include "api.hpp"
#include "decoupe.hpp"
#include "connecteurs.hpp"

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <numbers>
#include <array>
#include <format>
#include <fstream>
#include <optional>
#include <vector>
#include <cstdint>
#include <mutex>
#include <chrono>

#include <nlohmann/json.hpp>

#include "maillage.hpp"
#include "multiaxe.hpp"
#include "strategie.hpp"
#include "reglages.hpp"
#include "slicer.hpp"
#include "stl.hpp"
#include "trajectoires.hpp"
#include "version.hpp"

namespace artprint {

using Json = nlohmann::json;

struct Session::Etat {
    Reglages reglages;
    std::optional<Maillage> modele;  // posé au centre du plateau
    std::optional<Maillage> origine; // tel qu'il a été lu, avant mise à l'échelle et orientation
    double echelle = 1.0;            // facteur d'échelle demandé (1 : taille d'origine)
    std::array<double, 3> rotation{0.0, 0.0, 0.0};  // rotation demandée autour de X, Y, Z, en degrés
    std::array<double, 2> decalage{0.0, 0.0};       // déplacement sur le plateau, en mm
    bool oriente = false;            // vrai : la pose a été choisie par le slicer
    std::optional<ResultatTranchage> resultat;

    // Une commande à la fois (sauf la recherche des stratégies, voir executer).
    std::mutex verrou;
    // Change à chaque modification du modèle ou des réglages : ce qui a été
    // calculé pour une version n'est plus bon pour la suivante.
    std::uint64_t version = 0;
    // Mémoire des calculs longs, valable pour une version.
    std::optional<std::vector<Strategie>> strategies;
    std::uint64_t version_strategies = 0;
    std::optional<double> duree_classique;
    std::uint64_t version_duree = 0;
    // Les premiers sommets du modèle sont ceux de la matière ; les suivants,
    // s'il y en a, sont ceux des connecteurs « en creux » (trous, rainure),
    // qui dépassent un peu de la pièce sans en faire partie : ils ne comptent
    // ni pour la poser sur le plateau ni pour ses dimensions. 0 : tous comptent.
    std::size_t sommets_solides = 0;
};

namespace {

// Les chemins arrivent en UTF-8 : les accents doivent survivre sous Windows.
std::filesystem::path chemin_depuis_json(const Json& demande) {
    const std::string texte = demande.at("chemin").get<std::string>();
    return std::filesystem::path(std::u8string(texte.begin(), texte.end()));
}

double arrondi(double valeur) { return std::round(valeur * 1000.0) / 1000.0; }

Json liste_reglages(const Reglages& reglages) {
    Json liste = Json::array();
    for (const DescriptionReglage& d : description_reglages()) {
        Json r;
        r["cle"] = d.cle;
        r["groupe"] = d.groupe;
        r["libelle"] = d.libelle;
        r["unite"] = d.unite;
        r["aide"] = d.aide;
        r["facultatif"] = d.facultatif;
        r["valeur"] = texte_reglage(reglages, d);
        switch (d.type) {
            case TypeReglage::nombre:
                r["type"] = "nombre";
                break;
            case TypeReglage::entier:
                r["type"] = "entier";
                break;
            case TypeReglage::booleen:
                r["type"] = "booleen";
                break;
            case TypeReglage::choix:
                r["type"] = "choix";
                r["choix"] = d.choix;
                break;
            case TypeReglage::texte:
                r["type"] = "texte";
                break;
        }
        if (d.type == TypeReglage::nombre || d.type == TypeReglage::entier) {
            r["min"] = d.min;
            r["max"] = d.max;
        }
        liste.push_back(std::move(r));
    }
    return liste;
}

// La boîte de la matière : celle des « solides » premiers sommets (0 : tous).
BoiteEnglobante boite_solide(const Maillage& modele, std::size_t solides) {
    if (solides == 0 || solides >= modele.sommets.size()) {
        return calculer_boite(modele);
    }
    BoiteEnglobante boite{modele.sommets.front(), modele.sommets.front()};
    for (std::size_t i = 1; i < solides; ++i) {
        const Point3& s = modele.sommets[i];
        boite.min = {std::min(boite.min.x, s.x), std::min(boite.min.y, s.y), std::min(boite.min.z, s.z)};
        boite.max = {std::max(boite.max.x, s.x), std::max(boite.max.y, s.y), std::max(boite.max.z, s.z)};
    }
    return boite;
}

// Pose le modèle au centre du plateau, d'après la boîte de sa matière.
Maillage poser_solide(const Maillage& modele, std::size_t solides) {
    const BoiteEnglobante boite = boite_solide(modele, solides);
    Maillage pose = modele;
    for (Point3& sommet : pose.sommets) {
        sommet.x -= (boite.min.x + boite.max.x) / 2.0f;
        sommet.y -= (boite.min.y + boite.max.y) / 2.0f;
        sommet.z -= boite.min.z;
    }
    return pose;
}

Json decrire_modele(const Maillage& modele, std::size_t solides = 0) {
    const BoiteEnglobante boite = boite_solide(modele, solides);
    const Point3 taille = boite.dimensions();
    const RapportFermeture fermeture = verifier_fermeture(modele);

    // Triangles à plat : 9 nombres par triangle (x, y, z de chaque sommet).
    Json positions = Json::array();
    for (const Triangle& triangle : modele.triangles) {
        for (const std::uint32_t indice : triangle.sommets) {
            const Point3& p = modele.sommets[indice];
            positions.push_back(arrondi(p.x));
            positions.push_back(arrondi(p.y));
            positions.push_back(arrondi(p.z));
        }
    }
    Json reponse;
    reponse["triangles"] = modele.triangles.size();
    reponse["volume_cm3"] = arrondi(volume_maillage(modele) / 1000.0);
    reponse["dimensions"] = {arrondi(taille.x), arrondi(taille.y), arrondi(taille.z)};
    reponse["ferme"] = fermeture.ferme && fermeture.orientation_coherente;
    reponse["message_fermeture"] = fermeture.message;
    reponse["positions"] = std::move(positions);
    return reponse;
}

std::string_view nom_type_strategie(TypeStrategie type) {
    switch (type) {
        case TypeStrategie::classique:
            return "classique";
        case TypeStrategie::autre_pose:
            return "autre_pose";
        case TypeStrategie::par_branches:
            return "par_branches";
        case TypeStrategie::couches_courbes:
            return "couches_courbes";
    }
    return "classique";
}

Json decrire_strategie(const Strategie& strategie) {
    Json reponse;
    reponse["type"] = nom_type_strategie(strategie.type);
    reponse["nom"] = strategie.nom;
    reponse["description"] = strategie.description;
    reponse["surplomb_mm2"] = std::round(strategie.surplomb);
    reponse["remarques"] = strategie.remarques;
    reponse["parties"] = Json::array();
    for (const PartieImpression& partie : strategie.parties) {
        reponse["parties"].push_back(
            {{"direction", {partie.direction.x, partie.direction.y, partie.direction.z}},
             {"inclinaison", std::round(partie.inclinaison * 10.0) / 10.0},
             {"cote", partie.cote},
             {"courbe", !partie.guide.empty() || partie.couches_courbes},
             {"volume_cm3", std::round(partie.volume) / 1000.0},
             {"surplomb_mm2", std::round(partie.surplomb)},
             {"parent", partie.parent}});
    }
    return reponse;
}

// Les zones orientées d'une demande de l'interface.
std::vector<ZoneOrientee> lire_zones(const Json& liste) {
    std::vector<ZoneOrientee> zones;
    for (const Json& z : liste) {
        const Json& p = z.at("point");
        if (!p.is_array() || p.size() != 3) {
            throw std::runtime_error("Zone : « point » doit donner trois nombres (X, Y, Z).");
        }
        ZoneOrientee zone;
        zone.point = {p[0].get<double>(), p[1].get<double>(), p[2].get<double>()};
        zone.inclinaison = z.value("inclinaison", 0.0);
        zone.azimut = z.value("azimut", 0.0);
        zones.push_back(zone);
    }
    return zones;
}

// Pour le diagnostic seulement (voir main.cpp) : la variable d'environnement
// ARTPRINT_GCODE_REFUSE donne un fichier où garder un G-code refusé.
void garder_le_refus(const ResultatTranchage& resultat) {
    if (const char* diagnostic = std::getenv("ARTPRINT_GCODE_REFUSE"); diagnostic && !resultat.verification.valide()) {
        std::ofstream(diagnostic, std::ios::binary) << "; G-CODE REFUSE - NE PAS IMPRIMER\n" << resultat.gcode;
    }
}

Json decrire_resultat(const ResultatTranchage& resultat) {
    Json reponse;
    reponse["couches"] = resultat.couches;
    reponse["filament_m"] = arrondi(resultat.longueur_filament / 1000.0);
    reponse["duree_s"] = std::round(resultat.duree_estimee);
    reponse["remarques"] = resultat.remarques;
    reponse["taille_gcode"] = resultat.gcode.size();

    const RapportVerification& v = resultat.verification;
    Json verification;
    verification["valide"] = v.valide();
    verification["nombre_erreurs"] = v.nombre_erreurs;
    verification["extrusions"] = v.extrusions;
    verification["deplacements_a_vide"] = v.deplacements_a_vide;
    verification["extrusions_en_rotation"] = v.extrusions_en_rotation;
    verification["retractions"] = v.retractions;
    verification["dans_le_vide_mm"] = std::round(v.longueur_dans_le_vide);
    verification["erreurs"] = Json::array();
    for (const ErreurVerification& e : v.erreurs) {
        verification["erreurs"].push_back(
            {{"ligne", e.ligne},
             {"texte", e.texte_ligne},
             {"probleme", e.probleme},
             {"solution", e.solution},
             {"position", {e.position.x, e.position.y, e.position.z, e.position.b, e.position.c}}});
    }
    reponse["verification"] = std::move(verification);

    // Aperçu : pour chaque couche, 7 nombres par mouvement
    // (départ x y z, arrivée x y z, type).
    Json apercu = Json::array();
    for (const CoucheApercu& couche : resultat.apercu) {
        Json mouvements = Json::array();
        for (const MouvementApercu& m : couche.mouvements) {
            for (const float v2 : {m.x0, m.y0, m.z0, m.x1, m.y1, m.z1}) {
                mouvements.push_back(arrondi(v2));
            }
            mouvements.push_back(m.type);
        }
        apercu.push_back({{"z", arrondi(couche.z)}, {"partie", couche.partie}, {"m", std::move(mouvements)}});
    }
    reponse["apercu"] = std::move(apercu);
    reponse["types"] = {nom_type(TypeTrait::jupe),
                        nom_type(TypeTrait::paroi_interne),
                        nom_type(TypeTrait::paroi_externe),
                        nom_type(TypeTrait::remplissage_plein),
                        nom_type(TypeTrait::remplissage),
                        "déplacement à vide"};
    return reponse;
}

}  // namespace

Session::Session() : etat_(std::make_unique<Etat>()) {}
Session::~Session() = default;

namespace {
// Secondes écoulées depuis « debut », arrondies au dixième.
double secondes_depuis(std::chrono::steady_clock::time_point debut) {
    return std::round(std::chrono::duration<double>(std::chrono::steady_clock::now() - debut).count() * 10.0) / 10.0;
}
}  // namespace

std::string Session::executer(std::string_view demande_json) {
    Etat& etat = *etat_;
    // La recherche des façons d'imprimer est longue et ne change rien à la
    // session : elle se fait sur une copie du modèle, sans retenir les autres
    // commandes (on peut trancher en classique pendant ce temps).
    if (demande_json.find("\"strategies\"") != std::string_view::npos) {
        try {
            if (Json::parse(demande_json).at("commande") == "strategies") {
                std::unique_lock<std::mutex> garde(etat.verrou);
                if (!etat.modele) {
                    return Json{{"ok", false}, {"erreur", "Aucun modèle n'est ouvert."}}.dump();
                }
                const auto debut = std::chrono::steady_clock::now();
                std::vector<Strategie> trouvees;
                if (etat.strategies && etat.version_strategies == etat.version) {
                    trouvees = *etat.strategies;
                } else {
                    const Maillage modele = *etat.modele;
                    const Reglages reglages = etat.reglages;
                    const std::uint64_t version = etat.version;
                    garde.unlock();
                    trouvees = proposer_strategies(modele, reglages);
                    garde.lock();
                    if (etat.version == version) {
                        etat.strategies = trouvees;
                        etat.version_strategies = version;
                    }
                }
                garde.unlock();
                Json reponse;
                reponse["strategies"] = Json::array();
                for (const Strategie& strategie : trouvees) {
                    reponse["strategies"].push_back(decrire_strategie(strategie));
                }
                reponse["secondes"] = secondes_depuis(debut);
                reponse["ok"] = true;
                return reponse.dump(-1, ' ', false, Json::error_handler_t::replace);
            }
        } catch (const std::exception& erreur) {
            return Json{{"ok", false}, {"erreur", erreur.what()}}.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
    }
    const std::lock_guard<std::mutex> garde(etat.verrou);
    return executer_sous_verrou(demande_json);
}

std::string Session::executer_sous_verrou(std::string_view demande_json) {
    Json reponse;
    try {
        const Json demande = Json::parse(demande_json);
        const std::string commande = demande.at("commande").get<std::string>();
        Etat& etat = *etat_;
        const auto debut_commande = std::chrono::steady_clock::now();
        // Ces commandes changent le modèle ou les réglages : les calculs gardés en mémoire ne valent plus.
        for (const char* nom : {"definir_reglages", "reglages_par_defaut", "charger_reglages", "charger_modele",
                                "transformer_modele", "couper_modele", "fermer_modele"}) {
            if (commande == nom) {
                ++etat.version;
            }
        }
        // Les façons d'imprimer le modèle ouvert, calculées une seule fois par version.
        const auto strategies_du_modele = [&]() -> const std::vector<Strategie>& {
            if (!etat.strategies || etat.version_strategies != etat.version) {
                etat.strategies = proposer_strategies(*etat.modele, etat.reglages);
                etat.version_strategies = etat.version;
            }
            return *etat.strategies;
        };

        if (commande == "infos") {
            reponse["nom"] = "ArtPrint Slicer";
            reponse["version"] = version();
            reponse["presentation"] = presentation();
        } else if (commande == "reglages") {
            reponse["reglages"] = liste_reglages(etat.reglages);
        } else if (commande == "definir_reglages") {
            // Chaque valeur est essayée sur une copie : une valeur refusée
            // est signalée sans empêcher les autres d'être prises en compte.
            Json refus = Json::object();
            for (const auto& [cle, valeur] : demande.at("valeurs").items()) {
                const std::string texte = valeur.is_string() ? valeur.get<std::string>()
                                          : valeur.is_null() ? std::string()
                                                             : valeur.dump();
                try {
                    Reglages essai = etat.reglages;
                    definir_reglage(essai, cle, texte);
                    etat.reglages = essai;
                } catch (const ErreurReglages& erreur) {
                    refus[cle] = erreur.what();
                }
            }
            reponse["refus"] = std::move(refus);
            reponse["reglages"] = liste_reglages(etat.reglages);
        } else if (commande == "reglages_par_defaut") {
            etat.reglages = Reglages{};
            reponse["reglages"] = liste_reglages(etat.reglages);
        } else if (commande == "charger_reglages") {
            etat.reglages = lire_reglages(chemin_depuis_json(demande));
            reponse["reglages"] = liste_reglages(etat.reglages);
        } else if (commande == "enregistrer_reglages") {
            std::ofstream fichier(chemin_depuis_json(demande), std::ios::binary);
            fichier << ecrire_reglages(etat.reglages);
            if (!fichier) {
                throw std::runtime_error("Impossible d'écrire le fichier de réglages.");
            }
        } else if (commande == "charger_modele") {
            const ModeleStl modele = lire_stl(chemin_depuis_json(demande));
            etat.sommets_solides = 0;
            etat.modele = poser_au_centre(modele.maillage);
            etat.origine = etat.modele;
            etat.echelle = 1.0;
            etat.oriente = false;
            etat.rotation = {0.0, 0.0, 0.0};
            etat.decalage = {0.0, 0.0};
            etat.resultat.reset();
            reponse["modele"] = decrire_modele(*etat.modele, etat.sommets_solides);
            reponse["modele"]["format"] = nom_format(modele.format);
        } else if (commande == "transformer_modele") {
            // Met le modèle à l'échelle et choisit sa pose. On repart toujours
            // du modèle tel qu'il a été lu : les demandes ne se cumulent pas.
            //   "echelle" : facteur (1 = taille d'origine) ; absent : inchangé ;
            //   "orientation" : "auto" (la pose qui demande le moins de
            //                   supports) ou "origine" ; absent : inchangée.
            if (!etat.origine) {
                throw std::runtime_error("Aucun modèle n'est ouvert.");
            }
            if (demande.contains("echelle")) {
                const double echelle = demande["echelle"].get<double>();
                if (!(echelle >= 0.01 && echelle <= 100.0)) {
                    throw std::runtime_error("L'échelle doit être comprise entre 1 % et 10 000 %.");
                }
                etat.echelle = echelle;
            }
            if (demande.contains("orientation")) {
                const std::string orientation = demande["orientation"].get<std::string>();
                if (orientation != "auto" && orientation != "origine") {
                    throw std::runtime_error("Orientation inconnue (attendu : auto ou origine).");
                }
                etat.oriente = orientation == "auto";
            }
            //   "rotation" : [x, y, z] en degrés, autour des axes X, Y puis Z,
            //                  depuis la pose d'origine (annule l'orientation « auto ») ;
            //   "decalage" : [x, y] en mm, depuis le centre du plateau.
            if (demande.contains("rotation")) {
                const std::vector<double> angles = demande["rotation"].get<std::vector<double>>();
                if (angles.size() != 3) {
                    throw std::runtime_error("La rotation attend trois angles : autour de X, de Y et de Z.");
                }
                etat.rotation = {angles[0], angles[1], angles[2]};
                etat.oriente = false;
            }
            if (demande.contains("decalage")) {
                const std::vector<double> d = demande["decalage"].get<std::vector<double>>();
                if (d.size() != 2) {
                    throw std::runtime_error("Le déplacement attend deux distances : en X et en Y.");
                }
                etat.decalage = {d[0], d[1]};
            }
            Maillage modele = *etat.origine;
            std::string note;
            if (!etat.oriente) {
                // Rotations successives autour de X, de Y puis de Z.
                for (int axe = 0; axe < 3; ++axe) {
                    const double angle = etat.rotation[static_cast<std::size_t>(axe)] * std::numbers::pi / 180.0;
                    if (std::abs(angle) < 1e-12) {
                        continue;
                    }
                    const double c = std::cos(angle), s = std::sin(angle);
                    for (Point3& p : modele.sommets) {
                        const double x = p.x, y = p.y, z = p.z;
                        const double nx = axe == 0 ? x : axe == 1 ? c * x + s * z : c * x - s * y;
                        const double ny = axe == 0 ? c * y - s * z : axe == 1 ? y : s * x + c * y;
                        const double nz = axe == 0 ? s * y + c * z : axe == 1 ? -s * x + c * z : z;
                        p.x = static_cast<decltype(p.x)>(nx);
                        p.y = static_cast<decltype(p.y)>(ny);
                        p.z = static_cast<decltype(p.z)>(nz);
                    }
                }
            }
            if (etat.oriente) {
                OptionsStrategie options;
                options.couches_orientees = true;
                bool trouvee = false;
                for (const Strategie& strategie : proposer_strategies(modele, etat.reglages, options)) {
                    if (strategie.type == TypeStrategie::autre_pose && !strategie.parties.empty()) {
                        modele = strategie.parties.front().maillage;
                        trouvee = true;
                    }
                }
                note = trouvee ? "Le modèle a été retourné dans la pose qui demande le moins de supports."
                               : "Le modèle est déjà dans sa meilleure pose : rien n'a été tourné.";
            }
            if (std::abs(etat.echelle - 1.0) > 1e-9) {
                for (Point3& sommet : modele.sommets) {
                    sommet.x = static_cast<decltype(sommet.x)>(sommet.x * etat.echelle);
                    sommet.y = static_cast<decltype(sommet.y)>(sommet.y * etat.echelle);
                    sommet.z = static_cast<decltype(sommet.z)>(sommet.z * etat.echelle);
                }
            }
            etat.modele = poser_solide(modele, etat.sommets_solides);
            for (Point3& p : etat.modele->sommets) {
                p.x = static_cast<decltype(p.x)>(p.x + etat.decalage[0]);
                p.y = static_cast<decltype(p.y)>(p.y + etat.decalage[1]);
            }
            etat.resultat.reset();
            reponse["modele"] = decrire_modele(*etat.modele, etat.sommets_solides);
            reponse["rotation"] = etat.rotation;
            reponse["decalage"] = etat.decalage;
            reponse["echelle"] = etat.echelle;
            reponse["orientation"] = etat.oriente ? "auto" : "origine";
            reponse["note"] = note;
        } else if (commande == "trancher") {
            if (!etat.modele) {
                throw std::runtime_error("Aucun modèle n'est ouvert.");
            }
            verifier_coherence(etat.reglages);
            // « strategie » (facultatif) : "classique" ou "couches_courbes".
            const std::string voulue = demande.value("strategie", std::string("classique"));
            if (voulue == "classique") {
                etat.resultat = trancher_classique(*etat.modele, etat.reglages, true);
                etat.duree_classique = etat.resultat->duree_estimee;
                etat.version_duree = etat.version;
            } else if (voulue == "couches_courbes" && demande.contains("tout_incline")) {
                // « tout_incline » (facultatif) : toute la pièce en couches
                // penchées, la tête inclinée du même angle du début à la fin.
                //   "inclinaison" et "azimut", en degrés.
                const Json& choix = demande["tout_incline"];
                // (Pas de jupe : tracée dans le plan des couches penchées, elle passerait sous le plateau.)
                Reglages sans_jupe = etat.reglages;
                sans_jupe.impression.jupe_boucles = 0;
                etat.resultat = trancher_strategie(
                    strategie_inclinee(*etat.modele, sans_jupe, choix.value("inclinaison", 45.0),
                                       choix.value("azimut", 0.0)),
                    sans_jupe);
            } else if (voulue == "couches_courbes" && demande.contains("zones") && !demande["zones"].empty()) {
                // « zones » (facultatif) : les zones orientées choisies par
                // l'utilisateur. Chaque entrée : "point" [x, y, z] sur le
                // modèle posé, "inclinaison" et "azimut" en degrés.
                etat.resultat = trancher_strategie(
                    strategie_par_zones(*etat.modele, etat.reglages, lire_zones(demande["zones"])), etat.reglages);
            } else {
                const Strategie* choisie = nullptr;
                std::vector<Strategie> strategies = strategies_du_modele();
                // « branches » (facultatif) : le choix de l'utilisateur pour
                // certaines branches des couches courbes. Chaque entrée :
                //   "partie" : son numéro dans la liste des parties (2, 3...) ;
                //   "mode" : "courbes" (d'office) ou "a_plat" (imprimée à plat,
                //            en couches horizontales, avec le tronc) ;
                //   "cote" : -1 ou 1 pour imposer le côté où la tête penche ;
                //   "inclinaison_max" : inclinaison maximale de la tête, en degrés.
                if (demande.contains("branches") && voulue == "couches_courbes") {
                    const Strategie* base = nullptr;
                    for (const Strategie& strategie : strategies) {
                        if (strategie.type == TypeStrategie::couches_courbes) {
                            base = &strategie;
                        }
                    }
                    if (!base) {
                        throw std::runtime_error("Les couches courbes ne sont pas proposées pour ce modèle.");
                    }
                    struct Choix {
                        Vec3 repere;
                        int cote = 0;
                        double inclinaison_max = 0.0;
                    };
                    std::vector<Choix> choix;
                    OptionsStrategie options;
                    for (const Json& branche : demande["branches"]) {
                        const int numero = branche.value("partie", 0);
                        if (numero < 2 || static_cast<std::size_t>(numero) > base->parties.size()) {
                            throw std::runtime_error(std::format(
                                "La partie {} n'est pas une branche de ce modèle (les branches vont de 2 à {}).",
                                numero, base->parties.size()));
                        }
                        const PartieImpression& partie = base->parties[static_cast<std::size_t>(numero) - 1];
                        const std::string mode = branche.value("mode", std::string("courbes"));
                        if (mode == "a_plat") {
                            options.a_plat.push_back(partie.repere);
                        } else if (mode != "courbes") {
                            throw std::runtime_error(std::format(
                                "Mode de branche inconnu : « {} » (attendu : courbes ou a_plat).", mode));
                        } else {
                            choix.push_back({partie.repere, branche.value("cote", 0),
                                             branche.value("inclinaison_max", 0.0)});
                        }
                    }
                    if (!options.a_plat.empty()) {
                        strategies = proposer_strategies(*etat.modele, etat.reglages, options);
                    }
                    // Les autres choix sont reportés sur les branches restantes
                    // (reconnues par leur point de repère).
                    for (Strategie& strategie : strategies) {
                        if (strategie.type != TypeStrategie::couches_courbes) {
                            continue;
                        }
                        for (const Choix& c : choix) {
                            PartieImpression* proche = nullptr;
                            double distance_min = 3.0;  // mm
                            for (PartieImpression& partie : strategie.parties) {
                                const double d = norme(partie.repere - c.repere);
                                if (partie.couches_courbes && d < distance_min) {
                                    distance_min = d;
                                    proche = &partie;
                                }
                            }
                            if (proche) {
                                proche->cote = c.cote < 0 ? -1 : c.cote > 0 ? 1 : proche->cote;
                                if (c.inclinaison_max > 0.0) {
                                    proche->inclinaison_outil_max = std::clamp(c.inclinaison_max, 10.0, 180.0);
                                }
                            }
                        }
                    }
                    // Si toutes les branches sont à plat, il ne reste que le classique.
                    if (std::none_of(strategies.begin(), strategies.end(), [](const Strategie& strategie) {
                            return strategie.type == TypeStrategie::couches_courbes;
                        })) {
                        etat.resultat = trancher_classique(*etat.modele, etat.reglages, true);
                        etat.resultat->remarques.push_back(
                            "Toutes les branches sont imprimées à plat : c'est une impression classique.");
                        garder_le_refus(*etat.resultat);
                        reponse["resultat"] = decrire_resultat(*etat.resultat);
                        reponse["resultat"]["preparation_s"] = std::round(etat.reglages.impression.duree_preparation * 60.0);
                        reponse["resultat"]["duree_classique_s"] = std::round(etat.resultat->duree_estimee);
                        reponse["strategie"] = voulue;
                        reponse["ok"] = true;
                        return reponse.dump(-1, ' ', false, Json::error_handler_t::replace);
                    }
                }
                for (const Strategie& strategie : strategies) {
                    if (nom_type_strategie(strategie.type) == voulue) {
                        choisie = &strategie;
                    }
                }
                if (!choisie) {
                    throw std::runtime_error(std::format(
                        "La stratégie « {} » n'est pas proposée pour ce modèle.", voulue));
                }
                etat.resultat = trancher_strategie(*choisie, etat.reglages);
            }
            garder_le_refus(*etat.resultat);
            reponse["resultat"] = decrire_resultat(*etat.resultat);
            reponse["resultat"]["preparation_s"] = std::round(etat.reglages.impression.duree_preparation * 60.0);
            reponse["strategie"] = voulue;
            // Pour l'ondulation : la durée du même modèle en classique, pour
            // dire ce que l'ondulation ajoute ou enlève.
            if (voulue != "classique") {
                if (!etat.duree_classique || etat.version_duree != etat.version) {
                    // (Une simple estimation : ce G-code-là n'est ni vérifié ni gardé.)
                    Reglages sans_peau = etat.reglages;
                    sans_peau.impression.peau_courbe = false;
                    etat.duree_classique = trancher(*etat.modele, sans_peau, true, false).duree_estimee;
                    etat.version_duree = etat.version;
                }
                reponse["resultat"]["duree_classique_s"] = std::round(*etat.duree_classique);
            }
            reponse["secondes"] = secondes_depuis(debut_commande);
        } else if (commande == "exporter_stl") {
            // Le modèle tel qu'il est posé sur le plateau, en STL binaire.
            if (!etat.modele) {
                throw std::runtime_error("Aucun modèle n'est ouvert.");
            }
            ecrire_stl(*etat.modele, chemin_depuis_json(demande));
            reponse["triangles"] = etat.modele->triangles.size();
        } else if (commande == "couper_modele") {
            // Coupe le modèle posé par un plan et garde un des deux morceaux.
            //   "point" [x, y, z] et "normale" [x, y, z] : le plan de coupe
            //       (ou "hauteur" : un plan horizontal à cette hauteur) ;
            //   "garder" : "bas" (le côté opposé à la normale) ou "haut" ;
            //   "poser_sur_coupe" : vrai pour tourner le morceau, sa face de
            //       coupe contre le plateau (ou vers le haut s'il porte des
            //       tenons ou un rail, qui dépassent de cette face) ;
            //   "connecteurs" : {"type": "trous" | "tenons" | "aronde_male" |
            //       "aronde_femelle", "nombre", "diametre", "profondeur", "jeu"}.
            // Le morceau gardé devient le modèle : il est reposé sur le
            // plateau, et l'échelle et la rotation repartent de zéro.
            if (!etat.modele) {
                throw std::runtime_error("Aucun modèle n'est ouvert.");
            }
            const std::string garder = demande.value("garder", std::string("bas"));
            if (garder != "bas" && garder != "haut") {
                throw std::runtime_error("Couper : « garder » attend « bas » ou « haut ».");
            }
            Plan plan{{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
            if (demande.contains("point")) {
                const std::vector<double> p = demande.at("point").get<std::vector<double>>();
                const std::vector<double> n = demande.value("normale", std::vector<double>{0.0, 0.0, 1.0});
                if (p.size() != 3 || n.size() != 3 || norme(Vec3{n[0], n[1], n[2]}) < 1e-9) {
                    throw std::runtime_error("Couper : « point » et « normale » attendent trois nombres chacun.");
                }
                plan = {{p[0], p[1], p[2]}, normaliser(Vec3{n[0], n[1], n[2]})};
            } else {
                plan.point.z = demande.at("hauteur").get<double>();
            }
            // Tous les morceaux du côté de la normale sont détachés : une
            // graine par sommet situé de ce côté (un échantillon suffit).
            std::vector<Vec3> graines;
            const std::size_t pas = std::max<std::size_t>(1, etat.modele->sommets.size() / 300);
            for (std::size_t i = 0; i < etat.modele->sommets.size(); i += pas) {
                const Point3& s = etat.modele->sommets[i];
                if (produit_scalaire(Vec3{s.x, s.y, s.z} - plan.point, plan.normale) > 0.05) {
                    graines.push_back({s.x, s.y, s.z});
                }
            }
            const auto coupe = graines.empty() ? std::nullopt : couper_par_plan(*etat.modele, plan, graines);
            if (!coupe) {
                throw std::runtime_error(
                    "Le plan de coupe ne traverse pas la pièce, ou ne la coupe pas proprement. Solution : déplacer le "
                    "plan à l'intérieur de la pièce, ou l'incliner un peu autrement.");
            }
            Maillage morceau = garder == "bas" ? coupe->reste : coupe->partie;
            if (!verifier_fermeture(morceau).ferme || volume_maillage(morceau) < 1.0) {
                throw std::runtime_error("Le morceau gardé ne serait pas un volume fermé. "
                                         "Solution : déplacer un peu le plan de coupe.");
            }
            // La direction qui sort du morceau gardé, à travers sa face de coupe.
            const Vec3 sortante = garder == "bas" ? plan.normale : plan.normale * -1.0;
            OptionsConnecteurs connecteurs;
            std::string remarque;
            int poses = 0;
            const std::size_t sommets_piece = morceau.sommets.size();
            if (demande.contains("connecteurs") && demande["connecteurs"].is_object()) {
                const Json& c = demande["connecteurs"];
                const std::string type = c.value("type", std::string("aucun"));
                connecteurs.type = type == "trous" ? TypeConnecteur::trous
                                   : type == "tenons" ? TypeConnecteur::tenons
                                   : type == "aronde_male" ? TypeConnecteur::aronde_male
                                   : type == "aronde_femelle" ? TypeConnecteur::aronde_femelle
                                   : TypeConnecteur::aucun;
                if (connecteurs.type == TypeConnecteur::aucun && type != "aucun") {
                    throw std::runtime_error(std::format("Type de connecteur inconnu : « {} ».", type));
                }
                connecteurs.nombre = c.value("nombre", 2);
                connecteurs.diametre = c.value("diametre", 5.0);
                connecteurs.profondeur = c.value("profondeur", 6.0);
                connecteurs.jeu = c.value("jeu", 0.2);
                poses = ajouter_connecteurs(morceau, coupe->centre_coupe, sortante, connecteurs, remarque);
            }
            if (demande.value("poser_sur_coupe", false)) {
                // La face de coupe va contre le plateau ; si des tenons ou un
                // rail en dépassent, elle va vers le haut.
                const bool depasse = poses > 0 && (connecteurs.type == TypeConnecteur::tenons ||
                                                   connecteurs.type == TypeConnecteur::aronde_male);
                const Vec3 cible{0.0, 0.0, depasse ? 1.0 : -1.0};
                // Rotation qui amène « sortante » sur « cible » (formule de Rodrigues).
                Vec3 axe = produit_vectoriel(sortante, cible);
                const double sinus = norme(axe), cosinus = produit_scalaire(sortante, cible);
                if (sinus < 1e-9) {
                    axe = {1.0, 0.0, 0.0};  // déjà dans l'axe, ou à l'opposé : demi-tour autour de X
                } else {
                    axe = axe * (1.0 / sinus);
                }
                const double angle = std::atan2(sinus, cosinus);
                if (std::abs(angle) > 1e-9) {
                    const double c = std::cos(angle), s = std::sin(angle);
                    for (Point3& sommet : morceau.sommets) {
                        const Vec3 p{sommet.x, sommet.y, sommet.z};
                        const Vec3 q = p * c + produit_vectoriel(axe, p) * s + axe * (produit_scalaire(axe, p) * (1.0 - c));
                        sommet = {static_cast<decltype(sommet.x)>(q.x), static_cast<decltype(sommet.y)>(q.y),
                                  static_cast<decltype(sommet.z)>(q.z)};
                    }
                }
            }
            // Un tenon ou un rail fait partie de la matière ; un trou ou une rainure, non.
            etat.sommets_solides = connecteurs.type == TypeConnecteur::trous || connecteurs.type == TypeConnecteur::aronde_femelle
                                       ? sommets_piece : 0;
            etat.origine = poser_solide(morceau, etat.sommets_solides);
            etat.echelle = 1.0;
            etat.rotation = {0.0, 0.0, 0.0};
            etat.oriente = false;
            etat.modele = etat.origine;
            for (Point3& sommet : etat.modele->sommets) {
                sommet.x = static_cast<decltype(sommet.x)>(sommet.x + etat.decalage[0]);
                sommet.y = static_cast<decltype(sommet.y)>(sommet.y + etat.decalage[1]);
            }
            etat.resultat.reset();
            reponse["modele"] = decrire_modele(*etat.modele, etat.sommets_solides);
            reponse["rotation"] = etat.rotation;
            reponse["decalage"] = etat.decalage;
            reponse["echelle"] = etat.echelle;
            reponse["orientation"] = "origine";
            reponse["connecteurs_poses"] = poses;
            reponse["note"] = remarque;
        } else if (commande == "fermer_modele") {
            etat.modele.reset();
            etat.origine.reset();
            etat.resultat.reset();
            etat.echelle = 1.0;
            etat.rotation = {0.0, 0.0, 0.0};
            etat.decalage = {0.0, 0.0};
            etat.oriente = false;
        } else if (commande == "enregistrer_projet") {
            // Le projet est un texte JSON préparé par l'interface (modèle,
            // pose, réglages, zones) : il est écrit tel quel.
            std::ofstream fichier(chemin_depuis_json(demande), std::ios::binary);
            fichier << demande.at("projet").dump(1);
            if (!fichier) {
                throw std::runtime_error("Impossible d'écrire le fichier du projet.");
            }
        } else if (commande == "charger_projet") {
            std::ifstream fichier(chemin_depuis_json(demande), std::ios::binary);
            if (!fichier) {
                throw std::runtime_error("Impossible d'ouvrir le fichier du projet.");
            }
            const Json projet = Json::parse(fichier, nullptr, false);
            if (!projet.is_object() || projet.value("format", std::string()) != "artprint-projet") {
                throw std::runtime_error("Ce fichier n'est pas un projet ArtPrint Slicer.");
            }
            reponse["projet"] = projet;
        } else if (commande == "apercu_zones") {
            // Ce que donneraient les zones orientées, sans trancher : les
            // parties, leur direction, leur volume, ce qui reste à soutenir.
            if (!etat.modele) {
                throw std::runtime_error("Aucun modèle n'est ouvert.");
            }
            reponse["strategie"] = decrire_strategie(
                strategie_par_zones(*etat.modele, etat.reglages, lire_zones(demande.at("zones"))));
        } else if (commande == "strategies") {
            if (!etat.modele) {
                throw std::runtime_error("Aucun modèle n'est ouvert.");
            }
            reponse["strategies"] = Json::array();
            for (const Strategie& strategie : strategies_du_modele()) {
                reponse["strategies"].push_back(decrire_strategie(strategie));
            }
        } else if (commande == "enregistrer_gcode") {
            if (!etat.resultat) {
                throw std::runtime_error("Il faut d'abord trancher le modèle.");
            }
            if (!etat.resultat->verification.valide()) {
                throw std::runtime_error(
                    "Le G-code a été refusé par le vérificateur : il ne peut pas être enregistré.");
            }
            std::ofstream fichier(chemin_depuis_json(demande), std::ios::binary);
            fichier << etat.resultat->gcode;
            if (!fichier) {
                throw std::runtime_error("Impossible d'écrire le fichier G-code.");
            }
        } else {
            throw std::runtime_error(std::format("Commande inconnue : « {} ».", commande));
        }
        reponse["ok"] = true;
    } catch (const Json::exception& erreur) {
        reponse = Json{{"ok", false}, {"erreur", std::format("Demande invalide : {}", erreur.what())}};
    } catch (const std::exception& erreur) {
        reponse = Json{{"ok", false}, {"erreur", erreur.what()}};
    }
    return reponse.dump(-1, ' ', false, Json::error_handler_t::replace);
}

}  // namespace artprint
