// ArtPrint Slicer - licence AGPL v3
#include "strategie.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

#include "cinematique.hpp"
#include "collisions.hpp"
#include "decoupe.hpp"
#include "machine.hpp"
#include "slicer.hpp"
#include "squelette.hpp"

namespace artprint {

namespace {

Vec3 en_vec(const Point3& p) { return {p.x, p.y, p.z}; }

// Angle entre deux directions de longueur 1, en degrés.
double angle_entre(const Vec3& a, const Vec3& b) {
    return en_degres(std::acos(std::clamp(produit_scalaire(a, b), -1.0, 1.0)));
}

// Inclinaison maximale que la machine peut donner à la tête pour une matière
// dont le point le plus bas est à la hauteur « z_bas ». Près du plateau, B
// est limité ; plus haut, il peut aller jusqu'à sa butée.
double inclinaison_possible(const ReglagesMachine& machine, double z_bas) {
    const double butee = std::min(90.0, std::max(machine.b_max, -machine.b_min));
    const bool haut = machine.hauteur_bascule_b && z_bas >= *machine.hauteur_bascule_b;
    return haut ? butee : std::min(butee, machine.b_limite_pres_plateau);
}

// Redresse une direction trop penchée pour la machine, sans changer le côté
// vers lequel elle penche.
Vec3 redresser(const Vec3& direction, double inclinaison_max) {
    if (angle_entre(direction, {0.0, 0.0, 1.0}) <= inclinaison_max) {
        return direction;
    }
    const Vec3 a_plat = normaliser({direction.x, direction.y, 0.0});
    const double i = en_radians(inclinaison_max);
    return a_plat * std::sin(i) + Vec3{0.0, 0.0, std::cos(i)};
}

// Point le plus bas du maillage le long d'une direction.
double hauteur_min(const Maillage& maillage, const Vec3& direction) {
    double mini = std::numeric_limits<double>::max();
    for (const Point3& sommet : maillage.sommets) {
        mini = std::min(mini, produit_scalaire(en_vec(sommet), direction));
    }
    return mini;
}

void mesurer(PartieImpression& partie, double surplomb_max, const GrilleVoxels& matiere) {
    partie.inclinaison = angle_entre(partie.direction, {0.0, 0.0, 1.0});
    partie.volume = volume_maillage(partie.maillage);
    partie.surplomb = surface_en_surplomb(partie.maillage, partie.direction, partie.base,
                                          surplomb_max, 0.3, &matiere);
}

std::string decrire_direction(const Vec3& d) {
    const double inclinaison = angle_entre(d, {0.0, 0.0, 1.0});
    if (inclinaison < 0.5) {
        return "à la verticale";
    }
    return std::format("penchée de {:.0f}° vers l'angle {:.0f}° (vu de dessus)", inclinaison,
                       en_degres(std::atan2(d.y, d.x)));
}

// Contrôle, avec la vraie forme de l'imprimante, qu'une branche peut être
// imprimée dans une direction donnée sans que la tête ou le bras touche la
// matière déjà imprimée, le plateau ou la machine. Sans modèle de machine
// (réglage « modele_machine » vide), ce contrôle est simplement absent : le
// vérificateur reste de toute façon le juge final du G-code.
class ControleMachine {
public:
    ControleMachine(const Reglages& reglages, const GrilleVoxels& matiere,
                    std::vector<std::string>& remarques)
        : machine_(reglages.machine), pas_(matiere.pas) {
        if (machine_.modele_machine.empty() || !machine_.distance_pivot_buse) {
            return;
        }
        try {
            const std::string& chemin = machine_.modele_machine;
            modele_ = std::make_unique<ModeleMachine>(lire_modele_machine(
                std::filesystem::path(std::u8string(chemin.begin(), chemin.end()))));
        } catch (const std::exception& erreur) {
            remarques.push_back(std::string("Le modèle de la machine n'a pas pu être lu : les "
                                            "collisions ne sont pas prises en compte pour choisir "
                                            "les inclinaisons. ") + erreur.what());
            return;
        }
        // La peau du modèle : les voxels pleins qui touchent le vide.
        for (int k = 0; k < matiere.nz; ++k) {
            for (int j = 0; j < matiere.ny; ++j) {
                for (int i = 0; i < matiere.nx; ++i) {
                    if (!matiere.plein[matiere.indice(i, j, k)]) {
                        continue;
                    }
                    const bool bord = i == 0 || j == 0 || k == 0 || i == matiere.nx - 1 ||
                                      j == matiere.ny - 1 || k == matiere.nz - 1 ||
                                      !matiere.plein[matiere.indice(i - 1, j, k)] ||
                                      !matiere.plein[matiere.indice(i + 1, j, k)] ||
                                      !matiere.plein[matiere.indice(i, j - 1, k)] ||
                                      !matiere.plein[matiere.indice(i, j + 1, k)] ||
                                      !matiere.plein[matiere.indice(i, j, k - 1)] ||
                                      !matiere.plein[matiere.indice(i, j, k + 1)];
                    if (bord) {
                        peau_.push_back(matiere.centre(i, j, k));
                    }
                }
            }
        }
    }

    bool actif() const { return modele_ != nullptr; }

    // Plus grande inclinaison, au plus celle de « voulue » et du même côté,
    // qui laisse la tête libre le long de la branche. « tranches » : la
    // branche et celles qu'elle porte. Renvoie « rien » si même une faible
    // inclinaison provoque un contact.
    struct Choix {
        Vec3 direction;
        int cote = 0;
    };
    std::optional<Choix> direction_libre(const std::vector<TrancheSquelette>& tranches,
                                         const Vec3& voulue, double hauteur_sure) const {
        // Matière déjà imprimée au moment d'attaquer la branche : tout le
        // modèle sauf la branche elle-même.
        DetecteurCollisions detecteur(*modele_, machine_);
        for (const Vec3& point : peau_) {
            bool dans_la_branche = false;
            for (const TrancheSquelette& tranche : tranches) {
                if (norme(point - tranche.centre) < 1.6 * tranche.rayon + 2.0 * pas_) {
                    dans_la_branche = true;
                    break;
                }
            }
            if (!dans_la_branche) {
                detecteur.deposer(point);
            }
        }
        const double azimut = std::atan2(voulue.y, voulue.x);
        const double depart = angle_entre(voulue, {0.0, 0.0, 1.0});
        for (double inclinaison = depart; inclinaison >= 15.0; inclinaison -= 5.0) {
            const double i = en_radians(inclinaison);
            const Vec3 direction{std::sin(i) * std::cos(azimut), std::sin(i) * std::sin(azimut), std::cos(i)};
            // Les deux façons de pencher la tête sont essayées.
            for (const int cote : {1, -1}) {
                if (libre(detecteur, tranches, direction, hauteur_sure, cote)) {
                    return Choix{direction, cote};
                }
            }
        }
        return std::nullopt;
    }

private:
    // Essaie la tête en plusieurs points de la branche, et pendant sa descente d'approche.
    bool libre(DetecteurCollisions& detecteur, const std::vector<TrancheSquelette>& tranches,
               const Vec3& direction, double hauteur_sure, int cote) const {
        Vec3 u, v;
        repere_autour(direction, u, v);
        double c = 0.0;
        const auto essayer = [&](const Vec3& pointe) {
            const auto position = position_machine(machine_, {pointe, direction}, c, cote);
            if (!position) {
                return false;
            }
            c = position->c;
            return !detecteur.controler(*position).has_value();
        };
        // Approche : descente à la verticale jusqu'au-dessus du départ de la branche.
        const Vec3 approche = tranches.front().centre + direction * 8.0;
        for (int etape = 0; etape <= 6; ++etape) {
            const double z = hauteur_sure + (approche.z - hauteur_sure) * etape / 6.0;
            if (z >= approche.z && !essayer({approche.x, approche.y, z})) {
                return false;
            }
        }
        const std::size_t saut = std::max<std::size_t>(1, tranches.size() / 8);
        for (std::size_t t = 0; t < tranches.size(); t += saut) {
            const TrancheSquelette& tranche = tranches[t];
            for (const Vec3& decalage : {Vec3{}, u * tranche.rayon, u * -tranche.rayon,
                                         v * tranche.rayon, v * -tranche.rayon}) {
                if (!essayer(tranche.centre + decalage)) {
                    return false;
                }
            }
        }
        return true;
    }

    const ReglagesMachine machine_;
    const double pas_;
    std::unique_ptr<ModeleMachine> modele_;
    std::vector<Vec3> peau_;
};

// Découpe le modèle en suivant son squelette : une branche qui s'écarte trop
// de la direction de la partie qui la porte devient une partie à elle seule.
class DecoupeParBranches {
public:
    DecoupeParBranches(const Maillage& pose, const Squelette& squelette, const Reglages& reglages,
                       const OptionsStrategie& options, const ControleMachine& controle,
                       const GrilleVoxels& matiere, double hauteur_sure,
                       std::vector<std::string>& remarques)
        : squelette_(squelette), machine_(reglages.machine), options_(options),
          controle_(controle), matiere_(matiere), hauteur_sure_(hauteur_sure), remarques_(remarques) {
        // Chaque face du modèle est rattachée à la branche dont une tranche
        // est la plus proche : on saura ainsi ce que chaque branche coûte en
        // surplomb selon la direction dans laquelle elle est imprimée.
        faces_.reserve(pose.triangles.size());
        for (const Triangle& t : pose.triangles) {
            const Vec3 a = en_vec(pose.sommets[t.sommets[0]]);
            const Vec3 b = en_vec(pose.sommets[t.sommets[1]]);
            const Vec3 c = en_vec(pose.sommets[t.sommets[2]]);
            const Vec3 croix = produit_vectoriel(b - a, c - a);
            const double double_aire = norme(croix);
            if (double_aire < 1e-12) {
                continue;
            }
            const Vec3 centre = (a + b + c) * (1.0 / 3.0);
            const Vec3 normale = croix * (1.0 / double_aire);
            // Face cachée dans la matière, ou posée sur le plateau : elle ne coûte rien.
            if (centre.z < 0.3 || matiere.contient(centre + normale * (1.5 * matiere.pas))) {
                continue;
            }
            int proche = -1;
            double meilleure = std::numeric_limits<double>::max();
            for (std::size_t n = 0; n < squelette_.branches.size(); ++n) {
                for (const TrancheSquelette& tranche : squelette_.branches[n].tranches) {
                    const double d = norme(centre - tranche.centre);
                    if (d < meilleure) {
                        meilleure = d;
                        proche = static_cast<int>(n);
                    }
                }
            }
            faces_.push_back({normale, 0.5 * double_aire, proche});
        }
        PartieImpression tronc;
        tronc.maillage = pose;
        parties_.push_back(std::move(tronc));
        for (std::size_t b = 0; b < squelette_.branches.size(); ++b) {
            if (squelette_.branches[b].parent < 0) {
                parcourir(static_cast<int>(b), 0);
            }
        }
    }

    std::vector<PartieImpression> prendre() { return std::move(parties_); }

private:
    // Regarde les branches portées par « branche », elle-même dans la partie « partie ».
    void parcourir(int branche, int partie) {
        std::vector<int> enfants = squelette_.branches[branche].enfants;
        std::sort(enfants.begin(), enfants.end(), [&](int a, int b) {
            return squelette_.branches[a].tranches.front().centre.z <
                   squelette_.branches[b].tranches.front().centre.z;
        });
        for (const int enfant : enfants) {
            const BrancheSquelette& b = squelette_.branches[enfant];
            // Une branche qui s'imprime sans surplomb avec la partie qui la
            // porte y reste : moins il y a de parties, mieux c'est. Une excroissance
            // plus courte que son rayon n'est pas une branche.
            if (surplomb_de(enfant, parties_[partie].direction) <= seuil_de(enfant) ||
                b.longueur < b.tranches.front().rayon) {
                parcourir(enfant, partie);
                continue;
            }
            // Essai : on détache la branche, on traite celles qu'elle porte,
            // puis on regarde si le surplomb total a vraiment baissé. Sinon
            // tout est remis comme avant et la branche reste avec sa mère.
            const std::size_t parties_avant = parties_.size();
            const std::size_t remarques_avant = remarques_.size();
            const Maillage mere_avant = parties_[partie].maillage;
            const double surplomb_avant = surplomb_partie(partie);
            const int nouvelle = detacher(enfant, partie);
            if (nouvelle < 0) {
                parcourir(enfant, partie);
                continue;
            }
            parcourir(enfant, nouvelle);
            double surplomb_apres = surplomb_partie(partie);
            for (std::size_t p = parties_avant; p < parties_.size(); ++p) {
                surplomb_apres += surplomb_partie(static_cast<int>(p));
            }
            if (surplomb_avant - surplomb_apres <= seuil_de(enfant)) {
                parties_.resize(parties_avant);
                remarques_.resize(remarques_avant);
                parties_[partie].maillage = mere_avant;
                parcourir(enfant, partie);
            }
        }
    }

    // Surface (mm²) qu'il faudrait soutenir pour imprimer cette partie, telle
    // qu'elle est découpée en ce moment, dans sa direction.
    double surplomb_partie(int partie) const {
        const PartieImpression& p = parties_[partie];
        const Vec3 base = p.parent < 0 ? Vec3{} : p.base;
        return surface_en_surplomb(p.maillage, p.direction, base, options_.surplomb_max, 0.3, &matiere_);
    }

    // Surface (mm²) de la branche qui serait imprimée dans le vide en
    // empilant les couches le long de « direction ».
    double surplomb_de(int branche, const Vec3& direction) const {
        const double seuil = std::sin(en_radians(options_.surplomb_max));
        double total = 0.0;
        for (const Face& face : faces_) {
            if (face.branche == branche && -produit_scalaire(face.normale, direction) > seuil) {
                total += face.aire;
            }
        }
        return total;
    }

    // En dessous de cette surface, le surplomb d'une branche est négligé :
    // quelques pour cent de sa surface, et jamais moins de quelques mm².
    double seuil_de(int branche) const {
        const BrancheSquelette& b = squelette_.branches[branche];
        const double surface = 2.0 * std::numbers::pi * b.tranches.front().rayon * b.longueur;
        return std::max(6.0, 0.04 * surface);
    }

    // Tranches de la branche et de toutes celles qu'elle porte.
    void tranches_de(int branche, std::vector<TrancheSquelette>& tranches) const {
        const BrancheSquelette& b = squelette_.branches[branche];
        tranches.insert(tranches.end(), b.tranches.begin(), b.tranches.end());
        for (const int enfant : b.enfants) {
            tranches_de(enfant, tranches);
        }
    }

    // Sépare la branche de la partie qui la porte. Renvoie le numéro de la
    // nouvelle partie, ou -1 si aucune coupe propre n'a été trouvée.
    int detacher(int branche, int partie) {
        const BrancheSquelette& b = squelette_.branches[branche];
        const TrancheSquelette& depart = b.tranches.front();
        const double possible = inclinaison_possible(machine_, depart.centre.z - depart.rayon);
        Vec3 direction = redresser(b.direction, possible);

        std::vector<TrancheSquelette> tranches;
        tranches_de(branche, tranches);
        std::vector<Vec3> toutes;
        for (const TrancheSquelette& tranche : tranches) {
            toutes.push_back(tranche.centre);
        }
        // Avec la vraie forme de la machine : l'inclinaison est réduite tant
        // que la tête ou le bras toucherait ce qui est déjà imprimé.
        bool reduite_pour_collision = false;
        int cote = 0;
        if (controle_.actif()) {
            const auto libre = controle_.direction_libre(tranches, direction, hauteur_sure_);
            if (!libre) {
                remarques_.push_back(std::format(
                    "Une branche (vers {:.0f} mm de haut) ne peut pas être imprimée inclinée sans "
                    "que la machine touche la pièce : elle reste imprimée avec la partie qui la porte.",
                    depart.centre.z));
                return -1;
            }
            reduite_pour_collision = angle_entre(libre->direction, direction) > 0.5;
            direction = libre->direction;
            cote = libre->cote;
        }
        const double redressement = angle_entre(direction, b.direction);
        // La direction retenue doit vraiment réduire le surplomb de la branche.
        const double avant = surplomb_de(branche, parties_[partie].direction);
        if (avant - surplomb_de(branche, direction) <= seuil_de(branche)) {
            if (angle_entre(b.direction, {0.0, 0.0, 1.0}) > 90.0) {
                remarques_.push_back(std::format(
                    "Une branche (vers {:.0f} mm de haut) redescend : la buse ne peut pas imprimer "
                    "vers le haut. Elle reste avec la partie qui la porte et demandera des supports.",
                    depart.centre.z));
            }
            return -1;
        }
        const double ep = squelette_.epaisseur_tranche;
        const double section = std::numbers::pi * depart.rayon * depart.rayon;
        // La section d'une branche coupée de biais est plus grande.
        const double biais = std::max(0.3, std::cos(en_radians(redressement)));

        // On cherche le plan le plus près du tronc qui ne détache que la branche.
        for (double recul = -2.0 * ep; recul <= 4.0 * ep + 1e-9; recul += 0.5 * ep) {
            const Plan plan{depart.centre + direction * recul, direction};
            std::vector<Vec3> graines;
            for (const Vec3& graine : toutes) {
                if (produit_scalaire(graine - plan.point, direction) > 0.5 * ep) {
                    graines.push_back(graine);
                }
            }
            if (graines.empty()) {
                break;
            }
            auto coupe = couper_par_plan(parties_[partie].maillage, plan, graines);
            if (!coupe) {
                continue;
            }
            const double volume = volume_maillage(coupe->partie);
            const bool propre = volume > 0.4 * b.volume_total &&
                                volume < 1.3 * b.volume_total + section * 3.0 * ep &&
                                coupe->surface_coupe < 2.5 * section / biais;
            if (!propre) {
                continue;
            }
            PartieImpression nouvelle;
            nouvelle.maillage = std::move(coupe->partie);
            nouvelle.direction = direction;
            nouvelle.cote = cote;
            nouvelle.base = coupe->centre_coupe;
            nouvelle.parent = partie;
            parties_[partie].maillage = std::move(coupe->reste);
            parties_.push_back(std::move(nouvelle));
            const double imprimee = angle_entre(direction, {0.0, 0.0, 1.0});
            if (reduite_pour_collision) {
                remarques_.push_back(std::format(
                    "Une branche penche de {:.0f}° : à cette inclinaison, la machine toucherait la "
                    "pièce déjà imprimée. Elle sera imprimée à {:.0f}°, avec un peu de surplomb.",
                    angle_entre(b.direction, {0.0, 0.0, 1.0}), imprimee));
            } else if (redressement > 1.0) {
                remarques_.push_back(std::format(
                    "Une branche penche de {:.0f}° : la machine ne peut incliner la tête que de "
                    "{:.0f}° à cette hauteur. Elle sera imprimée à {:.0f}°, avec un peu de surplomb.",
                    angle_entre(b.direction, {0.0, 0.0, 1.0}), possible, imprimee));
            }
            return static_cast<int>(parties_.size()) - 1;
        }
        remarques_.push_back(std::format(
            "Une branche (vers {:.0f} mm de haut) n'a pas pu être séparée proprement : elle reste "
            "imprimée avec la partie qui la porte.",
            depart.centre.z));
        return -1;
    }

    struct Face {
        Vec3 normale;
        double aire;
        int branche;  // branche du squelette la plus proche
    };
    std::vector<Face> faces_;
    const Squelette& squelette_;
    const ReglagesMachine machine_;
    const OptionsStrategie options_;
    const ControleMachine& controle_;
    const GrilleVoxels& matiere_;
    const double hauteur_sure_;
    std::vector<std::string>& remarques_;
    std::vector<PartieImpression> parties_;
};

// Range les parties dans l'ordre d'impression : une partie après celle qui
// la porte, et la plus basse d'abord.
std::vector<PartieImpression> ordonner(std::vector<PartieImpression> parties) {
    std::vector<int> nouveau_numero(parties.size(), -1);
    std::vector<PartieImpression> ordre;
    while (ordre.size() < parties.size()) {
        int choisie = -1;
        for (std::size_t p = 0; p < parties.size(); ++p) {
            const bool prete = nouveau_numero[p] < 0 &&
                               (parties[p].parent < 0 || nouveau_numero[parties[p].parent] >= 0);
            if (prete && (choisie < 0 || parties[p].base.z < parties[choisie].base.z)) {
                choisie = static_cast<int>(p);
            }
        }
        nouveau_numero[choisie] = static_cast<int>(ordre.size());
        ordre.push_back(std::move(parties[choisie]));
    }
    for (PartieImpression& partie : ordre) {
        if (partie.parent >= 0) {
            partie.parent = nouveau_numero[partie.parent];
        }
    }
    return ordre;
}

}  // namespace

double surface_en_surplomb(const Maillage& maillage, const Vec3& direction, const Vec3& base,
                           double surplomb_max, double tolerance, const GrilleVoxels* matiere) {
    // Une face est en surplomb quand elle regarde vers le bas plus que la
    // limite : l'angle entre sa normale et « le bas » est petit.
    const double seuil = std::sin(en_radians(surplomb_max));
    const double h_base = produit_scalaire(base, direction);
    double total = 0.0;
    for (const Triangle& t : maillage.triangles) {
        const Vec3 a = en_vec(maillage.sommets[t.sommets[0]]);
        const Vec3 b = en_vec(maillage.sommets[t.sommets[1]]);
        const Vec3 c = en_vec(maillage.sommets[t.sommets[2]]);
        const Vec3 croix = produit_vectoriel(b - a, c - a);
        const double double_aire = norme(croix);
        if (double_aire < 1e-12) {
            continue;
        }
        const Vec3 normale = croix * (1.0 / double_aire);
        if (-produit_scalaire(normale, direction) <= seuil) {
            continue;
        }
        // Face posée sur la surface de départ : elle est soutenue.
        const double plus_haut = std::max({produit_scalaire(a, direction), produit_scalaire(b, direction),
                                           produit_scalaire(c, direction)});
        if (plus_haut - h_base < tolerance) {
            continue;
        }
        // Face cachée dans la matière (deux volumes qui se chevauchent).
        if (matiere) {
            const Vec3 centre = (a + b + c) * (1.0 / 3.0);
            if (matiere->contient(centre + normale * (1.5 * matiere->pas))) {
                continue;
            }
        }
        total += 0.5 * double_aire;
    }
    return total;
}

std::vector<Strategie> proposer_strategies(const Maillage& pose, const Reglages& reglages,
                                           const OptionsStrategie& options) {
    std::vector<Strategie> strategies;
    if (pose.triangles.empty()) {
        return strategies;
    }
    const double pas = options.pas_voxels > 0.0 ? options.pas_voxels : pas_voxels_conseille(pose);
    const GrilleVoxels matiere = remplir_voxels(pose, pas);
    const BoiteEnglobante boite = calculer_boite(pose);

    // --- 1. Classique : couches horizontales, tête verticale.
    Strategie classique;
    classique.type = TypeStrategie::classique;
    classique.nom = "Classique (3 axes)";
    classique.description = "Tout le modèle est imprimé en couches horizontales, la tête à la verticale.";
    {
        PartieImpression tout;
        tout.maillage = pose;
        tout.base = {0.0, 0.0, boite.min.z};
        mesurer(tout, options.surplomb_max, matiere);
        classique.surplomb = tout.surplomb;
        classique.parties.push_back(std::move(tout));
    }
    if (classique.surplomb > 1.0) {
        classique.remarques.push_back(std::format(
            "{:.0f} mm² de surface seraient imprimés dans le vide : il faudrait des supports "
            "(pas encore disponibles dans le slicer).",
            classique.surplomb));
    }
    const double surplomb_classique = classique.surplomb;
    strategies.push_back(std::move(classique));

    const Squelette squelette = calculer_squelette(pose, pas);

    // --- 2. Autre pose : le modèle entier couché ou retourné.
    {
        std::vector<Vec3> candidats = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, -1}};
        for (const BrancheSquelette& b : squelette.branches) {
            if (candidats.size() < 16 && angle_entre(b.direction, {0.0, 0.0, 1.0}) > 20.0) {
                candidats.push_back(b.direction);
                candidats.push_back(b.direction * -1.0);
            }
        }
        double meilleur = surplomb_classique;
        Maillage meilleure_pose;
        Vec3 meilleur_haut{};
        for (const Vec3& haut : candidats) {
            const Vec3 base = haut * hauteur_min(pose, haut);
            const double surplomb = surface_en_surplomb(pose, haut, base, options.surplomb_max, 0.3, &matiere);
            if (surplomb >= 0.8 * meilleur) {
                continue;  // pas assez mieux pour valoir le changement
            }
            Vec3 u, v;
            repere_autour(haut, u, v);
            Maillage tourne = poser_au_centre(dans_repere(pose, {}, u, v, haut));
            const BoiteEnglobante b = calculer_boite(tourne);
            double rayon = 0.0;
            for (const Point3& s : tourne.sommets) {
                rayon = std::max(rayon, std::hypot(static_cast<double>(s.x), static_cast<double>(s.y)));
            }
            if (rayon > reglages.machine.rayon_plateau || b.max.z > reglages.machine.hauteur_impression) {
                continue;  // ne tient pas sur la machine dans cette position
            }
            meilleur = surplomb;
            meilleure_pose = std::move(tourne);
            meilleur_haut = haut;
        }
        if (!meilleure_pose.triangles.empty()) {
            Strategie autre;
            autre.type = TypeStrategie::autre_pose;
            autre.nom = "Autre pose (3 axes)";
            autre.description = std::format(
                "Le modèle entier est basculé : la direction {} devient le haut. Il est ensuite "
                "imprimé en couches horizontales, la tête à la verticale.",
                decrire_direction(meilleur_haut));
            PartieImpression tout;
            tout.maillage = std::move(meilleure_pose);
            tout.volume = volume_maillage(tout.maillage);
            tout.surplomb = meilleur;
            autre.surplomb = meilleur;
            autre.parties.push_back(std::move(tout));
            autre.remarques.push_back("La surface de pose sur le plateau change : vérifier qu'elle est assez grande pour que la pièce tienne.");
            strategies.push_back(std::move(autre));
        }
    }

    // --- 3. Par branches : chaque branche dans sa direction.
    {
        Strategie branches;
        branches.type = TypeStrategie::par_branches;
        branches.nom = "Par branches (5 axes)";
        const ControleMachine controle(reglages, matiere, branches.remarques);
        // Hauteur où la tête change d'orientation (voir multiaxe.hpp : 20 mm au-dessus du modèle).
        const double hauteur_sure = boite.max.z + 20.0;
        DecoupeParBranches decoupe(pose, squelette, reglages, options, controle, matiere,
                                   hauteur_sure, branches.remarques);
        if (!controle.actif() && !reglages.machine.modele_machine.empty() &&
            !reglages.machine.distance_pivot_buse) {
            branches.remarques.push_back("Les collisions ne sont pas prises en compte pour choisir les inclinaisons tant que « distance_pivot_buse » n'est pas renseigné.");
        }
        std::vector<PartieImpression> parties = ordonner(decoupe.prendre());
        for (PartieImpression& partie : parties) {
            mesurer(partie, options.surplomb_max, matiere);
            branches.surplomb += partie.surplomb;
        }
        if (parties.size() > 1) {
            branches.description = std::format(
                "Le modèle est découpé en {} parties : le tronc, imprimé à la verticale, puis "
                "chaque branche, imprimée le long de sa propre direction avec la tête inclinée "
                "et le plateau tourné.",
                parties.size());
            if (squelette.boucles) {
                branches.remarques.push_back("Le modèle contient une anse (deux branches qui se rejoignent) : la découpe automatique peut y être imparfaite.");
            }
            if (!reglages.machine.distance_pivot_buse) {
                branches.remarques.push_back("Pour trancher cette stratégie, le réglage « distance_pivot_buse » doit être mesuré sur la machine.");
            }
            if (!reglages.machine.hauteur_bascule_b) {
                branches.remarques.push_back(std::format(
                    "Le réglage « hauteur_bascule_b » n'est pas renseigné : l'inclinaison de la tête est limitée à {:.0f}° partout.",
                    reglages.machine.b_limite_pres_plateau));
            }
            branches.parties = std::move(parties);
            strategies.push_back(std::move(branches));
        }
    }
    return strategies;
}

}  // namespace artprint
