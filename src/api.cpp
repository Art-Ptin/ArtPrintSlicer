// ArtPrint Slicer - licence AGPL v3
#include "api.hpp"

#include <cmath>
#include <filesystem>
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
            etat.resultat.reset();
            reponse["modele"] = decrire_modele(*etat.modele);
            reponse["modele"]["format"] = nom_format(modele.format);
        } else if (commande == "trancher") {
            if (!etat.modele) {
                throw std::runtime_error("Aucun modèle n'est ouvert.");
            }
            verifier_coherence(etat.reglages);
            // « strategie » (facultatif) : "classique", "autre_pose", "par_branches" ou "couches_courbes".
            const std::string voulue = demande.value("strategie", std::string("classique"));
            if (voulue == "classique") {
                etat.resultat = trancher_classique(*etat.modele, etat.reglages);
            } else {
                const Strategie* choisie = nullptr;
                const std::vector<Strategie> strategies =
                    proposer_strategies(*etat.modele, etat.reglages);
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
            reponse["strategie"] = voulue;
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
