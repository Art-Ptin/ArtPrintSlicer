// ArtPrint Slicer - licence AGPL v3
#include "api.hpp"
#include "decoupe.hpp"

#include <cmath>
#include <filesystem>
#include <algorithm>
#include <numbers>
#include <array>
#include <format>
#include <fstream>
#include <optional>

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

Json decrire_modele(const Maillage& modele) {
    const BoiteEnglobante boite = calculer_boite(modele);
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

std::string Session::executer(std::string_view demande_json) {
    Json reponse;
    try {
        const Json demande = Json::parse(demande_json);
        const std::string commande = demande.at("commande").get<std::string>();
        Etat& etat = *etat_;

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
            etat.modele = poser_au_centre(modele.maillage);
            etat.origine = etat.modele;
            etat.echelle = 1.0;
            etat.oriente = false;
            etat.rotation = {0.0, 0.0, 0.0};
            etat.decalage = {0.0, 0.0};
            etat.resultat.reset();
            reponse["modele"] = decrire_modele(*etat.modele);
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
            etat.modele = poser_au_centre(modele);
            for (Point3& p : etat.modele->sommets) {
                p.x = static_cast<decltype(p.x)>(p.x + etat.decalage[0]);
                p.y = static_cast<decltype(p.y)>(p.y + etat.decalage[1]);
            }
            etat.resultat.reset();
            reponse["modele"] = decrire_modele(*etat.modele);
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
            } else {
                const Strategie* choisie = nullptr;
                std::vector<Strategie> strategies = proposer_strategies(*etat.modele, etat.reglages);
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
            reponse["resultat"] = decrire_resultat(*etat.resultat);
            reponse["resultat"]["preparation_s"] = std::round(etat.reglages.impression.duree_preparation * 60.0);
            reponse["strategie"] = voulue;
            // Pour l'ondulation : la durée du même modèle en classique, pour
            // dire ce que l'ondulation ajoute ou enlève.
            if (voulue != "classique") {
                reponse["resultat"]["duree_classique_s"] =
                    std::round(trancher_classique(*etat.modele, etat.reglages, true).duree_estimee);
            }
        } else if (commande == "strategies") {
            if (!etat.modele) {
                throw std::runtime_error("Aucun modèle n'est ouvert.");
            }
            reponse["strategies"] = Json::array();
            for (const Strategie& strategie : proposer_strategies(*etat.modele, etat.reglages)) {
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
