// ArtPrint Slicer - licence AGPL v3
#include "strategie.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

#include "champ_couches.hpp"
#include "cinematique.hpp"
#include "collisions.hpp"
#include "couches_courbes.hpp"
#include "decoupe.hpp"
#include "machine.hpp"
#include "multiaxe.hpp"
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
    return std::min(90.0, inclinaison_permise(machine, z_bas));
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

// Surface en surplomb d'une partie courbe : chaque face est jugée avec la
// direction d'impression du point de la courbe le plus proche.
double surplomb_courbe(const Maillage& maillage, const std::vector<PointGuide>& guide,
                       double surplomb_max, const GrilleVoxels& matiere) {
    const double seuil = std::sin(en_radians(surplomb_max));
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
        const Vec3 centre = (a + b + c) * (1.0 / 3.0);
        const PointGuide* proche = &guide.front();
        double meilleure = std::numeric_limits<double>::max();
        for (const PointGuide& g : guide) {
            const double d = norme(centre - g.point);
            if (d < meilleure) {
                meilleure = d;
                proche = &g;
            }
        }
        if (-produit_scalaire(normale, proche->direction) <= seuil) {
            continue;
        }
        // Face posée sur la surface de départ, ou cachée dans la matière.
        if (produit_scalaire(centre - guide.front().point, guide.front().direction) < 0.3 ||
            matiere.contient(centre + normale * (1.5 * matiere.pas))) {
            continue;
        }
        total += 0.5 * double_aire;
    }
    return total;
}

double surplomb_de_partie(const PartieImpression& partie, double surplomb_max, const GrilleVoxels& matiere) {
    if (!partie.guide.empty()) {
        return surplomb_courbe(partie.maillage, partie.guide, surplomb_max, matiere);
    }
    const Vec3 base = partie.parent < 0 ? Vec3{} : partie.base;
    return surface_en_surplomb(partie.maillage, partie.direction, base, surplomb_max, 0.3, &matiere);
}

void mesurer(PartieImpression& partie, double surplomb_max, const GrilleVoxels& matiere) {
    partie.inclinaison = angle_entre(partie.direction, {0.0, 0.0, 1.0});
    for (const PointGuide& g : partie.guide) {
        partie.inclinaison = std::max(partie.inclinaison, angle_entre(g.direction, {0.0, 0.0, 1.0}));
    }
    partie.volume = volume_maillage(partie.maillage);
    partie.surplomb = surplomb_de_partie(partie, surplomb_max, matiere);
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
        if (!machine_.distance_pivot_buse) {
            return;
        }
        courses_ = machine_.course_x_min || machine_.course_x_max || machine_.course_y_min ||
                   machine_.course_y_max || machine_.course_z_min || machine_.course_z_max;
        if (machine_.modele_machine.empty()) {
            return;
        }
        // Ce contrôle ne regarde que quelques positions : il garde 1 mm de
        // plus que la marge de sécurité, pour que le G-code complet, relu
        // ensuite position par position, passe sans surprise.
        machine_.marge_securite += 1.0;
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

    // Vrai s'il y a quelque chose à contrôler : la forme de la machine
    // (collisions) ou la course de ses axes.
    bool actif() const { return modele_ != nullptr || courses_; }

    // Plus grande inclinaison, au plus celle de « voulue » et du même côté,
    // qui laisse la tête libre le long de la branche. « tranches » : la
    // branche et celles qu'elle porte. Renvoie « rien » si même une faible
    // inclinaison provoque un contact.
    struct Choix {
        Vec3 direction;
        int cote = 0;
    };

    // Détecteur chargé de la matière déjà imprimée quand on attaque une
    // branche : tout le modèle sauf la branche elle-même.
    std::unique_ptr<DetecteurCollisions> detecteur_pour(const std::vector<TrancheSquelette>& tranches) const {
        if (!modele_) {
            return nullptr;  // pas de forme de machine : seules les courses sont contrôlées
        }
        auto detecteur = std::make_unique<DetecteurCollisions>(*modele_, machine_);
        for (const Vec3& point : peau_) {
            bool dans_la_branche = false;
            for (const TrancheSquelette& tranche : tranches) {
                if (norme(point - tranche.centre) < 1.6 * tranche.rayon + 2.0 * pas_) {
                    dans_la_branche = true;
                    break;
                }
            }
            if (!dans_la_branche) {
                detecteur->deposer(point);
            }
        }
        return detecteur;
    }

    // Pour une partie courbe : le côté (+1 ou -1) qui laisse la tête libre
    // tout le long de la courbe, ou 0 si aucun des deux ne convient.
    // « surface » : des points de la surface de la partie (voir courses_respectees).
    int cote_libre(DetecteurCollisions* detecteur, const std::vector<TrancheSquelette>& tranches,
                   const std::vector<Vec3>& directions, double hauteur_sure,
                   const std::vector<Vec3>& surface) const {
        for (const int cote : {1, -1}) {
            bool libre = true;
            double c = 0.0;
            const auto essayer = [&](const Vec3& pointe, const Vec3& direction) {
                const auto position = position_machine(machine_, {pointe, direction}, c, cote);
                if (!position || !dans_les_courses(*position)) {
                    return false;
                }
                c = position->c;
                return !detecteur || !detecteur->controler(*position).has_value();
            };
            libre = courses_respectees(surface, tranches, directions, cote);
            const Vec3 approche = tranches.front().centre + directions.front() * 8.0;
            for (int etape = 0; etape <= 6 && libre; ++etape) {
                const double z = hauteur_sure + (approche.z - hauteur_sure) * etape / 6.0;
                libre = z < approche.z || essayer({approche.x, approche.y, z}, directions.front());
            }
            const std::size_t saut = std::max<std::size_t>(1, directions.size() / 24);
            for (std::size_t t = 0; t < directions.size() && libre; t += saut) {
                Vec3 u, v;
                repere_autour(directions[t], u, v);
                const double bord = tranches[t].rayon + 1.0;
                for (const Vec3& decalage : {Vec3{}, u * bord, u * -bord, v * bord, v * -bord}) {
                    if (!essayer(tranches[t].centre + decalage, directions[t])) {
                        libre = false;
                        break;
                    }
                }
            }
            if (libre) {
                return cote;
            }
        }
        return 0;
    }
    std::optional<Choix> direction_libre(const std::vector<TrancheSquelette>& tranches,
                                         const Vec3& voulue, double hauteur_sure,
                                         const std::vector<Vec3>& surface) const {
        const std::unique_ptr<DetecteurCollisions> charge = detecteur_pour(tranches);
        DetecteurCollisions* detecteur = charge.get();
        const double azimut = std::atan2(voulue.y, voulue.x);
        const double depart = angle_entre(voulue, {0.0, 0.0, 1.0});
        for (double inclinaison = depart; inclinaison >= 15.0; inclinaison -= 5.0) {
            const double i = en_radians(inclinaison);
            const Vec3 direction{std::sin(i) * std::cos(azimut), std::sin(i) * std::sin(azimut), std::cos(i)};
            // Les deux façons de pencher la tête sont essayées.
            for (const int cote : {1, -1}) {
                if (libre(detecteur, tranches, direction, hauteur_sure, cote, surface)) {
                    return Choix{direction, cote};
                }
            }
        }
        return std::nullopt;
    }

private:
    // Essaie la tête en plusieurs points de la branche, et pendant sa descente d'approche.
    bool libre(DetecteurCollisions* detecteur, const std::vector<TrancheSquelette>& tranches,
               const Vec3& direction, double hauteur_sure, int cote,
               const std::vector<Vec3>& surface) const {
        Vec3 u, v;
        repere_autour(direction, u, v);
        double c = 0.0;
        const auto essayer = [&](const Vec3& pointe) {
            const auto position = position_machine(machine_, {pointe, direction}, c, cote);
            if (!position || !dans_les_courses(*position)) {
                return false;
            }
            c = position->c;
            return !detecteur || !detecteur->controler(*position).has_value();
        };
        // La course des axes est vite calculée : elle est contrôlée sur
        // toute la surface de la branche, avant les collisions.
        if (!courses_respectees(surface, direction, cote)) {
            return false;
        }
        // Approche : descente à la verticale jusqu'au-dessus du départ de la branche.
        const Vec3 approche = tranches.front().centre + direction * 8.0;
        for (int etape = 0; etape <= 6; ++etape) {
            const double z = hauteur_sure + (approche.z - hauteur_sure) * etape / 6.0;
            if (z >= approche.z && !essayer({approche.x, approche.y, z})) {
                return false;
            }
        }
        const std::size_t saut = std::max<std::size_t>(1, tranches.size() / 16);
        for (std::size_t t = 0; t < tranches.size(); t += saut) {
            const TrancheSquelette& tranche = tranches[t];
            // Le centre de la tranche et quatre points de son bord, un peu au-delà.
            const double bord = tranche.rayon + 1.0;
            for (const Vec3& decalage : {Vec3{}, u * bord, u * -bord, v * bord, v * -bord}) {
                if (!essayer(tranche.centre + decalage)) {
                    return false;
                }
            }
        }
        return true;
    }

    // Vrai si les axes X, Y et Z restent dans leur course à cette position,
    // avec 1 mm de réserve (le contrôle ne regarde que quelques points).
    bool dans_les_courses(const PositionMachine& p) const {
        const auto dedans = [](double valeur, const std::optional<double>& min, const std::optional<double>& max) {
            return (!min || valeur >= *min + 1.0) && (!max || valeur <= *max - 1.0);
        };
        return dedans(p.x, machine_.course_x_min, machine_.course_x_max) &&
               dedans(p.y, machine_.course_y_min, machine_.course_y_max) &&
               dedans(p.z, machine_.course_z_min, machine_.course_z_max);
    }

    // Vrai si la machine atteint tous ces points de la pièce, la tête dans
    // cette direction, sans sortir de ses courses. Pour une orientation
    // donnée, la position des axes suit celle de la pointe en ligne droite :
    // il suffit de contrôler les sommets de la surface de la partie.
    bool courses_respectees(const std::vector<Vec3>& points, const Vec3& direction, int cote) const {
        if (!courses_ || points.empty()) {
            return true;
        }
        const auto orientation = position_machine(machine_, {points.front(), direction}, 0.0, cote);
        if (!orientation) {
            return false;
        }
        for (const Vec3& point : points) {
            if (!dans_les_courses(position_avec_angles(machine_, point, orientation->b, orientation->c))) {
                return false;
            }
        }
        return true;
    }

    // La même chose pour une partie courbe : chaque point est atteint dans
    // la direction de la tranche la plus proche de lui.
    bool courses_respectees(const std::vector<Vec3>& points, const std::vector<TrancheSquelette>& tranches,
                            const std::vector<Vec3>& directions, int cote) const {
        if (!courses_ || points.empty()) {
            return true;
        }
        std::vector<PositionMachine> orientations;
        double c = 0.0;
        for (std::size_t t = 0; t < directions.size(); ++t) {
            const auto orientation = position_machine(machine_, {tranches[t].centre, directions[t]}, c, cote);
            if (!orientation) {
                return false;
            }
            c = orientation->c;
            orientations.push_back(*orientation);
        }
        for (const Vec3& point : points) {
            std::size_t proche = 0;
            double meilleure = std::numeric_limits<double>::max();
            for (std::size_t t = 0; t < orientations.size(); ++t) {
                const double d = norme(point - tranches[t].centre);
                if (d < meilleure) {
                    meilleure = d;
                    proche = t;
                }
            }
            if (!dans_les_courses(position_avec_angles(machine_, point, orientations[proche].b,
                                                       orientations[proche].c))) {
                return false;
            }
        }
        return true;
    }

    ReglagesMachine machine_;
    const double pas_;
    bool courses_ = false;  // vrai si au moins une course d'axe est renseignée
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
        : squelette_(squelette), machine_(reglages.machine), impression_(reglages.impression),
          options_(options), controle_(controle), matiere_(matiere), hauteur_sure_(hauteur_sure),
          remarques_(remarques) {
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
            faces_.push_back({normale, centre, 0.5 * double_aire, proche, {a, b, c}});
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
            if (surplomb_tige(enfant, direction_au_bout(partie)) <= seuil_de(enfant) ||
                b.longueur < b.tranches.front().rayon) {
                parcourir(enfant, partie);
                continue;
            }
            // Essais : on détache la branche de deux façons (en suivant sa
            // courbe, ou avec une direction fixe), on traite chaque fois les
            // branches qu'elle porte, et on garde ce qui laisse le moins de
            // surplomb. Si rien ne rapporte, tout est remis comme avant et la
            // branche reste avec sa mère.
            const std::size_t parties_avant = parties_.size();
            const std::size_t remarques_avant = remarques_.size();
            const Maillage mere_avant = parties_[partie].maillage;
            const double surplomb_avant = surplomb_partie(partie);
            const auto remettre = [&]() {
                parties_.resize(parties_avant);
                remarques_.resize(remarques_avant);
                parties_[partie].maillage = mere_avant;
            };
            const auto total = [&]() {
                double somme = surplomb_partie(partie);
                for (std::size_t p = parties_avant; p < parties_.size(); ++p) {
                    somme += surplomb_partie(static_cast<int>(p));
                }
                return somme;
            };
            const double infini = std::numeric_limits<double>::max();

            // Façon 1 : une branche courbe (plusieurs tronçons à la suite)
            // devient une seule partie dont les couches pivotent.
            double total_courbe = infini;
            std::vector<PartieImpression> parties_courbe;
            std::vector<std::string> remarques_courbe;
            Maillage mere_courbe;
            const std::vector<int> tige = tige_de(enfant);
            if (tige.size() > 1 && options_.couches_pivotantes) {
                const int courbe = detacher_courbe(tige, partie);
                if (courbe >= 0) {
                    parcourir(tige.back(), courbe);
                    total_courbe = total();
                    parties_courbe.assign(parties_.begin() + static_cast<std::ptrdiff_t>(parties_avant), parties_.end());
                    remarques_courbe.assign(remarques_.begin() + static_cast<std::ptrdiff_t>(remarques_avant), remarques_.end());
                    mere_courbe = parties_[partie].maillage;
                }
                remettre();
            }
            // Façon 2 : une direction fixe pour la branche.
            double total_fixe = infini;
            const int nouvelle = detacher(enfant, partie);
            if (nouvelle >= 0) {
                parcourir(enfant, nouvelle);
                total_fixe = total();
            }
            if (surplomb_avant - std::min(total_courbe, total_fixe) <= seuil_de(enfant)) {
                remettre();
                parcourir(enfant, partie);
                continue;
            }
            if (total_courbe < total_fixe) {
                remettre();
                parties_.insert(parties_.end(), parties_courbe.begin(), parties_courbe.end());
                remarques_.insert(remarques_.end(), remarques_courbe.begin(), remarques_courbe.end());
                parties_[partie].maillage = std::move(mere_courbe);
            }
        }
    }

    // Surface (mm²) qu'il faudrait soutenir pour imprimer cette partie, telle
    // qu'elle est découpée en ce moment, dans sa direction.
    double surplomb_partie(int partie) const {
        return surplomb_de_partie(parties_[partie], options_.surplomb_max, matiere_);
    }

    // Direction d'impression au bout d'une partie (pour juger ce qu'elle porte).
    Vec3 direction_au_bout(int partie) const {
        const PartieImpression& p = parties_[partie];
        return p.guide.empty() ? p.direction : p.guide.back().direction;
    }

    // La tige qui commence à « branche » : elle-même, puis ses tronçons suivants.
    std::vector<int> tige_de(int branche) const {
        std::vector<int> tige{branche};
        for (;;) {
            const std::vector<int>& enfants = squelette_.branches[tige.back()].enfants;
            if (enfants.size() != 1 || !squelette_.branches[enfants.front()].suite_courbe) {
                break;
            }
            tige.push_back(enfants.front());
        }
        return tige;
    }

    // Détache une branche courbe en une seule partie dont les couches suivent
    // la courbe. Renvoie le numéro de la nouvelle partie, ou -1.
    int detacher_courbe(const std::vector<int>& tige, int partie) {
        // Les tranches de la tige, dans l'ordre, puis de tout ce qu'elle porte.
        std::vector<TrancheSquelette> axe;
        for (const int numero : tige) {
            const BrancheSquelette& b = squelette_.branches[numero];
            axe.insert(axe.end(), b.tranches.begin(), b.tranches.end());
        }
        if (axe.size() < 6) {
            return -1;
        }
        std::vector<TrancheSquelette> tout = axe;
        for (const int enfant : squelette_.branches[tige.back()].enfants) {
            tranches_de(enfant, tout);
        }
        double rayon = 0.0;
        for (const TrancheSquelette& t : axe) {
            rayon = std::max(rayon, t.rayon);
        }
        // Direction de la courbe en chaque tranche, lissée sur quelques voisines.
        std::vector<Vec3> tangentes(axe.size());
        for (std::size_t i = 0; i < axe.size(); ++i) {
            const std::size_t avant = i >= 3 ? i - 3 : 0;
            const std::size_t apres = std::min(i + 3, axe.size() - 1);
            tangentes[i] = normaliser(axe[apres].centre - axe[avant].centre);
        }
        const BrancheSquelette& premiere = squelette_.branches[tige.front()];

        std::unique_ptr<DetecteurCollisions> detecteur;
        std::vector<Vec3> surface;
        if (controle_.actif()) {
            detecteur = controle_.detecteur_pour(tout);
            std::vector<int> numeros(tige.begin(), tige.end());
            for (const int enfant : squelette_.branches[tige.back()].enfants) {
                branches_de(enfant, numeros);
            }
            surface = surface_de(numeros);
        }
        // Directions d'impression : celles de la courbe, limitées à ce que la
        // machine peut faire à chaque hauteur. Avec la forme de la machine,
        // l'inclinaison maximale est abaissée tant qu'il y a un contact.
        std::vector<Vec3> directions(axe.size());
        int cote = 0;
        double plafond = 90.0;
        for (;; plafond -= 10.0) {
            if (plafond < 25.0) {
                return -1;
            }
            for (std::size_t i = 0; i < axe.size(); ++i) {
                const double possible = std::min(plafond, inclinaison_possible(machine_, axe[i].centre.z - axe[i].rayon));
                directions[i] = redresser(tangentes[i], possible);
                if (i > 0) {
                    // La direction ne tourne pas plus vite que l'épaisseur de
                    // la branche ne le permet (sinon les couches se croiseraient).
                    const double permis = en_degres(0.3 / rayon) * norme(axe[i].centre - axe[i - 1].centre);
                    const double ecart = angle_entre(directions[i], directions[i - 1]);
                    if (ecart > permis) {
                        const double t = permis / ecart;
                        directions[i] = normaliser(directions[i - 1] * (1.0 - t) + directions[i] * t);
                    }
                }
            }
            if (!controle_.actif()) {
                break;
            }
            cote = controle_.cote_libre(detecteur.get(), axe, directions, hauteur_sure_, surface);
            if (cote != 0) {
                break;
            }
        }

        // Ce que la courbe rapporte : le surplomb des tronçons avec la partie
        // qui les porte, comparé à celui qu'ils auront en suivant la courbe.
        const Vec3 direction_mere = direction_au_bout(partie);
        const double seuil_face = std::sin(en_radians(options_.surplomb_max));
        double avant = 0.0, apres = 0.0;
        for (const Face& face : faces_) {
            if (std::find(tige.begin(), tige.end(), face.branche) == tige.end()) {
                continue;
            }
            if (-produit_scalaire(face.normale, direction_mere) > seuil_face) {
                avant += face.aire;
            }
            std::size_t proche = 0;
            double meilleure = std::numeric_limits<double>::max();
            for (std::size_t i = 0; i < axe.size(); ++i) {
                const double d = norme(face.centre - axe[i].centre);
                if (d < meilleure) {
                    meilleure = d;
                    proche = i;
                }
            }
            if (-produit_scalaire(face.normale, directions[proche]) > seuil_face) {
                apres += face.aire;
            }
        }
        if (avant - apres <= seuil_de(tige.front())) {
            return -1;
        }

        // Coupe au pied de la tige, perpendiculaire à sa première direction.
        const TrancheSquelette& depart = axe.front();
        const Vec3 direction = directions.front();
        const double ep = squelette_.epaisseur_tranche;
        const double section = std::numbers::pi * depart.rayon * depart.rayon;
        const double biais = std::max(0.3, std::cos(en_radians(angle_entre(direction, tangentes.front()))));
        for (double recul = -2.0 * ep; recul <= 4.0 * ep + 1e-9; recul += 0.5 * ep) {
            const Plan plan{depart.centre + direction * recul, direction};
            std::vector<Vec3> graines;
            for (const TrancheSquelette& tranche : tout) {
                if (produit_scalaire(tranche.centre - plan.point, direction) > 0.5 * ep) {
                    graines.push_back(tranche.centre);
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
            if (!(volume > 0.4 * premiere.volume_total &&
                  volume < 1.3 * premiere.volume_total + section * 3.0 * ep &&
                  coupe->surface_coupe < 2.5 * section / biais)) {
                continue;
            }
            PartieImpression nouvelle;
            nouvelle.maillage = std::move(coupe->partie);
            nouvelle.direction = direction;
            nouvelle.cote = cote;
            nouvelle.base = coupe->centre_coupe;
            nouvelle.parent = partie;
            // La courbe part du centre de la coupe, puis suit les tranches situées au-delà.
            nouvelle.guide.push_back({coupe->centre_coupe, direction});
            for (std::size_t i = 0; i < axe.size(); ++i) {
                if (produit_scalaire(axe[i].centre - plan.point, direction) > ep) {
                    nouvelle.guide.push_back({axe[i].centre, directions[i]});
                }
            }
            if (nouvelle.guide.size() < 3) {
                return -1;
            }
            // Le long du guide tel qu'il est maintenant, la direction ne
            // doit pas tourner plus vite que l'épaisseur ne le permet.
            for (std::size_t i = 1; i < nouvelle.guide.size(); ++i) {
                PointGuide& ici = nouvelle.guide[i];
                const PointGuide& avant_ici = nouvelle.guide[i - 1];
                const double avance = std::max(0.0, produit_scalaire(ici.point - avant_ici.point, avant_ici.direction));
                const double permis = en_degres(0.3 / rayon) * avance;
                const double ecart = angle_entre(ici.direction, avant_ici.direction);
                if (ecart > permis) {
                    const double t = ecart > 1e-9 ? permis / ecart : 0.0;
                    ici.direction = normaliser(avant_ici.direction * (1.0 - t) + ici.direction * t);
                }
            }
            // Dernier contrôle : les couches de cette partie se calculent-elles vraiment ?
            if (!couches_pivotantes_possibles(nouvelle, impression_)) {
                return -1;
            }
            parties_[partie].maillage = std::move(coupe->reste);
            parties_.push_back(std::move(nouvelle));
            if (plafond < 90.0) {
                remarques_.push_back(std::format(
                    "Une branche courbe est limitée à {:.0f}° d'inclinaison : au-delà, la machine "
                    "toucherait la pièce déjà imprimée.",
                    plafond));
            }
            return static_cast<int>(parties_.size()) - 1;
        }
        return -1;
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

    // Même mesure pour une tige entière : la branche et ses tronçons suivants.
    double surplomb_tige(int branche, const Vec3& direction) const {
        double total = 0.0;
        for (const int numero : tige_de(branche)) {
            total += surplomb_de(numero, direction);
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

    // Numéros de la branche et de toutes celles qu'elle porte.
    void branches_de(int branche, std::vector<int>& numeros) const {
        numeros.push_back(branche);
        for (const int enfant : squelette_.branches[branche].enfants) {
            branches_de(enfant, numeros);
        }
    }

    // Sommets des faces du modèle rattachées à ces branches : la surface
    // que la buse devra atteindre pour les imprimer.
    std::vector<Vec3> surface_de(const std::vector<int>& numeros) const {
        std::vector<Vec3> points;
        for (const Face& face : faces_) {
            if (std::find(numeros.begin(), numeros.end(), face.branche) != numeros.end()) {
                points.insert(points.end(), face.sommets.begin(), face.sommets.end());
            }
        }
        return points;
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
        // Avec la vraie forme de la machine et la course de ses axes :
        // l'inclinaison est réduite tant que la tête ou le bras toucherait ce
        // qui est déjà imprimé, ou qu'un axe arriverait en butée.
        bool reduite_pour_collision = false;
        int cote = 0;
        if (controle_.actif()) {
            std::vector<int> numeros;
            branches_de(branche, numeros);
            const auto libre = controle_.direction_libre(tranches, direction, hauteur_sure_, surface_de(numeros));
            if (!libre) {
                remarques_.push_back(std::format(
                    "Une branche (vers {:.0f} mm de haut) ne peut pas être imprimée inclinée sans "
                    "que la machine touche la pièce ou arrive en butée : elle reste imprimée avec "
                    "la partie qui la porte.",
                    depart.centre.z));
                return -1;
            }
            reduite_pour_collision = angle_entre(libre->direction, direction) > 0.5;
            direction = libre->direction;
            cote = libre->cote;
        }
        const double redressement = angle_entre(direction, b.direction);
        // La direction retenue doit vraiment réduire le surplomb de la branche.
        const double avant = surplomb_de(branche, direction_au_bout(partie));
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
                    "pièce déjà imprimée ou arriverait en butée. Elle sera imprimée à {:.0f}°, avec "
                    "un peu de surplomb.",
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
        Vec3 centre;
        double aire;
        int branche;  // branche du squelette la plus proche
        std::array<Vec3, 3> sommets;
    };
    std::vector<Face> faces_;
    const Squelette& squelette_;
    const ReglagesMachine machine_;
    const ReglagesImpression impression_;
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

// Regroupe les remarques identiques : « ... (4 fois) ».
std::vector<std::string> regrouper(std::vector<std::string> remarques) {
    std::vector<std::string> uniques;
    std::vector<int> fois;
    for (std::string& remarque : remarques) {
        const auto deja = std::find(uniques.begin(), uniques.end(), remarque);
        if (deja == uniques.end()) {
            uniques.push_back(std::move(remarque));
            fois.push_back(1);
        } else {
            ++fois[static_cast<std::size_t>(deja - uniques.begin())];
        }
    }
    for (std::size_t i = 0; i < uniques.size(); ++i) {
        if (fois[i] > 1) {
            uniques[i] += std::format(" ({} fois)", fois[i]);
        }
    }
    return uniques;
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
        const Point3 taille_pose = boite.dimensions();
        const double diagonale = std::hypot(static_cast<double>(taille_pose.x), static_cast<double>(taille_pose.y),
                                            static_cast<double>(taille_pose.z));
        for (const Vec3& candidat : candidats) {
            // Une direction de branche peut être nulle ou mal définie (branche
            // réduite à un point) : elle ne peut pas servir de « haut ».
            const double longueur = norme(candidat);
            if (!std::isfinite(longueur) || longueur < 1e-6) {
                continue;
            }
            const Vec3 haut = candidat * (1.0 / longueur);
            const Vec3 base = haut * hauteur_min(pose, haut);
            const double surplomb = surface_en_surplomb(pose, haut, base, options.surplomb_max, 0.3, &matiere);
            if (surplomb >= 0.8 * meilleur) {
                continue;  // pas assez mieux pour valoir le changement
            }
            Vec3 u, v;
            repere_autour(haut, u, v);
            Maillage tourne = poser_au_centre(dans_repere(pose, {}, u, v, haut));
            const BoiteEnglobante b = calculer_boite(tourne);
            // Garde-fou : une rotation ne change pas la taille du modèle.
            const Point3 taille = b.dimensions();
            const double diagonale_tournee = std::hypot(static_cast<double>(taille.x), static_cast<double>(taille.y),
                                                        static_cast<double>(taille.z));
            if (!(diagonale_tournee > 0.55 * diagonale && diagonale_tournee < 1.75 * diagonale)) {
                continue;
            }
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
            if (std::any_of(parties.begin(), parties.end(),
                            [](const PartieImpression& p) { return !p.guide.empty(); })) {
                branches.description += " Une branche courbe est imprimée en couches qui pivotent : "
                                        "elles suivent sa courbe, et la tête s'incline peu à peu pendant "
                                        "qu'elle imprime (5 axes continu).";
            }
            if (squelette.boucles) {
                branches.remarques.push_back("Le modèle contient une anse (deux branches qui se rejoignent) : la découpe automatique peut y être imparfaite.");
            }
            if (!reglages.machine.distance_pivot_buse) {
                branches.remarques.push_back("Pour trancher cette stratégie, le réglage « distance_pivot_buse » doit être mesuré sur la machine.");
            }
            if (!reglages.machine.hauteur_bascule_b && !encombrement_tete_connu(reglages.machine)) {
                branches.remarques.push_back(std::format(
                    "Le réglage « hauteur_bascule_b » n'est pas renseigné : l'inclinaison de la tête est limitée à {:.0f}° partout.",
                    reglages.machine.b_limite_pres_plateau));
            }
            branches.parties = std::move(parties);
            branches.remarques = regrouper(std::move(branches.remarques));
            strategies.push_back(std::move(branches));
        }

        // --- 4. Couches courbes : le tronc d'abord, lisse ; puis chaque
        // branche, imprimée contre lui en couches qui épousent sa surface.
        // Cette stratégie s'appelle « Ondulation » dans le logiciel. Elle est
        // proposée pour TOUT modèle, même quand elle n'apporte rien : c'est à
        // l'utilisateur de choisir, et une remarque le prévient.
        // (Si l'impression classique ne laisse rien dans le vide, il n'y a
        // aucune branche à chercher : inutile de faire ce calcul, qui est long.)
        {
            PlanBranches plan;
            if (surplomb_classique > 1.0) {
                plan = preparer_branches(pose, reglages.impression.hauteur_couche,
                                         0.45 * reglages.impression.largeur_extrusion, options.a_plat,
                                         inclinaison_des_couches_max(reglages.machine));
            }
            Strategie courbes;
            courbes.type = TypeStrategie::couches_courbes;
            courbes.nom = "Ondulation (5 axes continu)";
            courbes.modele = pose;
            courbes.a_plat = options.a_plat;
            {
                PartieImpression tronc;
                tronc.maillage = pose;
                tronc.tronc_lisse = true;
                tronc.volume = volume_maillage(pose);
                courbes.parties.push_back(std::move(tronc));
            }
            if (plan.vide()) {
                courbes.surplomb = surplomb_classique;
                courbes.description =
                    "Ce modèle n'a aucune partie que le slicer sache imprimer la tête inclinée : en "
                    "ondulation, il est imprimé comme en classique, en couches horizontales.";
                for (const std::string& ligne : plan.journal) {
                    courbes.remarques.push_back(ligne);
                }
                courbes.remarques.push_back("L'ondulation ne change rien pour ce modèle : même résultat et même durée qu'en classique.");
            } else {
                std::vector<int> partie_de(plan.tiges.size(), -1);
                for (std::size_t n = 0; n < plan.tiges.size(); ++n) {
                    if (plan.tiges[n].empty()) {
                        continue;
                    }
                    PartieImpression branche;
                    branche.couches_courbes = true;
                    int mere = plan.analyse.meres[n];
                    while (mere >= 0 && partie_de[mere] < 0) {
                        mere = plan.analyse.meres[mere];
                    }
                    branche.parent = mere >= 0 ? partie_de[mere] : 0;
                    branche.base = plan.tiges[n].front().origine;
                    // Un point de repère dans la matière de la branche : sur sa
                    // couche du milieu, un peu en dedans du bord. (Le centre de
                    // la couche ne convient pas : dans un tube, il est dans le vide.)
                    {
                        const CoucheCourbe& milieu = plan.tiges[n][plan.tiges[n].size() / 2];
                        branche.repere = milieu.origine;
                        Polygones dedans = decaler(milieu.contour, -0.6);
                        if (dedans.empty()) {
                            dedans = milieu.contour;
                        }
                        if (!dedans.empty() && !dedans.front().empty()) {
                            Vec3 ici;
                            if (poser_sur_la_surface(plan.analyse.champ, milieu, en_mm(dedans.front().front()),
                                                     plan.dessus[milieu.niveau], ici)) {
                                branche.repere = ici;
                            }
                        }
                    }
                    branche.direction = plan.tiges[n].back().w;
                    for (const CoucheCourbe& couche : plan.tiges[n]) {
                        branche.inclinaison = std::max(branche.inclinaison, angle_entre(couche.w, {0.0, 0.0, 1.0}));
                    }
                    branche.volume = plan.analyse.volumes[n];
                    courbes.parties.front().volume -= branche.volume;
                    partie_de[n] = static_cast<int>(courbes.parties.size());
                    courbes.parties.push_back(std::move(branche));
                }
                // Ce qui resterait dans le vide : dans une branche, les faces
                // tournées contre le sens des couches ; ailleurs, les faces
                // tournées vers le bas.
                const double seuil = std::sin(en_radians(options.surplomb_max));
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
                    if (centre.z < 0.3 || matiere.contient(centre + normale * (1.5 * matiere.pas))) {
                        continue;
                    }
                    Vec3 direction{0.0, 0.0, 1.0};
                    double pente = 1.0, longueur = 0.0;
                    int partie = 0;
                    if (plan.analyse.champ.valeur(centre, longueur) && longueur > 0.5 * plan.analyse.champ.pas) {
                        const int tige = plan.analyse.tige_en(centre);
                        if (tige >= 0 && partie_de[tige] >= 0 && plan.analyse.champ.gradient(centre, direction, pente)) {
                            partie = partie_de[tige];
                        } else {
                            direction = {0.0, 0.0, 1.0};
                        }
                    }
                    if (-produit_scalaire(normale, direction) > seuil) {
                        courbes.parties[partie].surplomb += 0.5 * double_aire;
                        courbes.surplomb += 0.5 * double_aire;
                    }
                }
                courbes.description = std::format(
"Le tronc est imprimé d'abord, en entier, en couches horizontales : sa surface reste lisse, "
                    "sans amorce de branche. Là où une branche viendra, il est laissé à nu (sans paroi, le "
                    "remplissage apparent) pour qu'elle s'y soude bien. Les {} branches sont imprimées "
                    "ensuite, chacune contre la partie qui la porte : ses premières couches épousent cette "
                    "surface, puis se redressent en travers de la branche.",
                    courbes.parties.size() - 1);
                for (const std::string& ligne : plan.journal) {
                    courbes.remarques.push_back(ligne);
                }
                // Limite d'inclinaison de la machine : une partie qui en
                // demande plus est signalée ici, avant l'export. Le fichier
                // ne dépasse jamais la limite.
                for (std::size_t p = 1; p < courbes.parties.size(); ++p) {
                    const PartieImpression& partie = courbes.parties[p];
                    if (partie.couches_courbes && partie.inclinaison > inclinaison_machine_max(reglages.machine) + 0.5) {
                        courbes.remarques.push_back(std::format(
                            "Partie {} : ses couches demandent jusqu'à {:.0f}° d'inclinaison, la machine est "
                            "limitée à {:.0f}° (réglage « b_inclinaison_max »). La tête ne dépassera pas {:.0f}° : "
                            "elle ne sera pas d'aplomb sur ces couches, le rendu peut être moins propre.",
                            p + 1, partie.inclinaison, inclinaison_machine_max(reglages.machine),
                            inclinaison_machine_max(reglages.machine)));
                    }
                }
                courbes.remarques.push_back("Les collisions de cette stratégie sont contrôlées au moment du tranchage.");
                if (!reglages.machine.distance_pivot_buse) {
                    courbes.remarques.push_back("Pour trancher cette stratégie, le réglage « distance_pivot_buse » doit être mesuré sur la machine.");
                }
                if (!reglages.machine.hauteur_bascule_b && !encombrement_tete_connu(reglages.machine)) {
                    courbes.remarques.push_back(std::format(
                        "Le réglage « hauteur_bascule_b » n'est pas renseigné : l'inclinaison de la tête est limitée à {:.0f}° partout.",
                        reglages.machine.b_limite_pres_plateau));
                }
                if (courbes.surplomb >= 0.8 * surplomb_classique) {
                    courbes.remarques.push_back(
                        "L'ondulation ne réduit presque pas la surface à soutenir sur ce modèle ; elle reste "
                        "possible, mais elle peut allonger l'impression.");
                }
            }
            strategies.push_back(std::move(courbes));
        }
    }
    if (!options.couches_orientees) {
        std::erase_if(strategies, [](const Strategie& s) {
            return s.type == TypeStrategie::autre_pose || s.type == TypeStrategie::par_branches;
        });
    }
    return strategies;
}

Vec3 direction_de_zone(const ZoneOrientee& zone) {
    const double pi = std::acos(-1.0);
    const double t = zone.inclinaison * pi / 180.0;
    const double a = zone.azimut * pi / 180.0;
    return {std::sin(t) * std::cos(a), std::sin(t) * std::sin(a), std::cos(t)};
}

Strategie strategie_inclinee(const Maillage& pose, const Reglages& reglages, double inclinaison, double azimut,
                             const OptionsStrategie& options) {
    const double possible = inclinaison_possible(reglages.machine, 0.0);
    if (!(inclinaison >= 1.0) || inclinaison > possible + 0.5) {
        throw std::runtime_error(std::format(
            "Toute la pièce inclinée : l'inclinaison doit être entre 1° et {:.0f}° (au ras du plateau, la tête ne "
            "peut pas pencher davantage : réglage « b_limite_pres_plateau »). Reçu : {:.0f}°.",
            possible, inclinaison));
    }
    Strategie strategie;
    strategie.type = TypeStrategie::par_branches;
    strategie.nom = "Ondulation, toute la pièce inclinée (5 axes)";
    strategie.modele = pose;
    const double pas = options.pas_voxels > 0.0 ? options.pas_voxels : pas_voxels_conseille(pose);
    const GrilleVoxels matiere = remplir_voxels(pose, pas);
    PartieImpression tout;
    tout.maillage = pose;
    tout.direction = direction_de_zone({{}, inclinaison, azimut});
    // Le point de la pièce le plus bas le long de cette direction : la première couche part de là.
    double plus_bas = std::numeric_limits<double>::max();
    for (const Point3& s : pose.sommets) {
        const double h = s.x * tout.direction.x + s.y * tout.direction.y + s.z * tout.direction.z;
        if (h < plus_bas) {
            plus_bas = h;
            tout.base = {s.x, s.y, s.z};
        }
    }
    mesurer(tout, options.surplomb_max, matiere);
    strategie.surplomb = tout.surplomb;
    strategie.description = std::format(
        "Toute la pièce est imprimée en couches penchées de {:.0f}°, posées sur le plateau : la tête garde cette "
        "inclinaison du début à la fin.",
        inclinaison);
    strategie.parties.push_back(std::move(tout));
    return strategie;
}

Strategie strategie_par_zones(const Maillage& pose, const Reglages& reglages,
                              const std::vector<ZoneOrientee>& zones, const OptionsStrategie& options) {
    Strategie strategie;
    strategie.type = TypeStrategie::par_branches;
    strategie.nom = "Ondulation par zones (5 axes)";
    strategie.modele = pose;
    const double pas = options.pas_voxels > 0.0 ? options.pas_voxels : pas_voxels_conseille(pose);
    const GrilleVoxels matiere = remplir_voxels(pose, pas);
    const BoiteEnglobante boite = calculer_boite(pose);
    const double volume_total = volume_maillage(pose);

    std::vector<PartieImpression> parties;
    {
        PartieImpression tronc;
        tronc.maillage = pose;
        tronc.base = {0.0, 0.0, boite.min.z};
        parties.push_back(std::move(tronc));
    }
    // Les zones les plus basses d'abord : une zone plus haute peut alors être
    // prise dans une zone déjà détachée.
    std::vector<std::size_t> ordre(zones.size());
    std::iota(ordre.begin(), ordre.end(), std::size_t{0});
    std::stable_sort(ordre.begin(), ordre.end(),
                     [&](std::size_t a, std::size_t b) { return zones[a].point.z < zones[b].point.z; });
    const double limite = inclinaison_machine_max(reglages.machine);
    for (const std::size_t z : ordre) {
        const ZoneOrientee& zone = zones[z];
        if (zone.inclinaison < 0.0 || zone.inclinaison > 170.0) {
            throw std::runtime_error(std::format(
                "Zone {} : l'inclinaison doit être entre 0 et 170 degrés (reçu : {:.0f}).", z + 1, zone.inclinaison));
        }
        if (zone.inclinaison > limite + 0.5) {
            throw std::runtime_error(std::format(
                "Zone {} : {:.0f}° d'inclinaison demandés, mais la machine ne dépasse pas {:.0f}° (réglages "
                "« butées de B » et « inclinaison maximale de B »). Solution : réduire l'inclinaison de cette zone.",
                z + 1, zone.inclinaison, limite));
        }
        const Vec3 direction = direction_de_zone(zone);
        const Plan plan{zone.point, direction};
        // La matière à détacher : juste au-dessus du plan, autour du point.
        const std::vector<Vec3> graines = {zone.point + direction * 0.6, zone.point + direction * 1.5};
        bool faite = false;
        for (std::size_t p = parties.size(); p-- > 0 && !faite;) {
            auto coupe = couper_par_plan(parties[p].maillage, plan, graines);
            if (!coupe) {
                continue;
            }
            const double volume = volume_maillage(coupe->partie);
            if (volume < 1.0 || volume_maillage(coupe->reste) < 1.0) {
                continue;
            }
            // Près du plateau, la tête ne peut pas s'incliner autant qu'en hauteur.
            const double z_bas = calculer_boite(coupe->partie).min.z;
            const double possible = inclinaison_possible(reglages.machine, z_bas);
            if (zone.inclinaison > possible + 0.5) {
                throw std::runtime_error(std::format(
                    "Zone {} : elle descend jusqu'à {:.1f} mm du plateau, et à cette hauteur la tête ne peut pas "
                    "s'incliner de plus de {:.0f}° sans risquer de toucher le plateau (réglages de la tête inclinable). "
                    "Solution : réduire l'inclinaison de la zone à {:.0f}° ou moins, ou choisir un point plus haut "
                    "sur le modèle.",
                    z + 1, std::max(0.0, z_bas), possible, possible));
            }
            PartieImpression nouvelle;
            nouvelle.maillage = std::move(coupe->partie);
            nouvelle.direction = direction;
            nouvelle.base = coupe->centre_coupe;
            nouvelle.parent = static_cast<int>(p);
            parties[p].maillage = std::move(coupe->reste);
            parties.push_back(std::move(nouvelle));
            faite = true;
        }
        if (!faite) {
            throw std::runtime_error(std::format(
                "Zone {} (X {:.1f}, Y {:.1f}, Z {:.1f}) : le plan de coupe ne détache rien d'imprimable à cet "
                "endroit. Solution : choisir un point plus bas sur le modèle, ou changer l'orientation de la zone.",
                z + 1, zone.point.x, zone.point.y, zone.point.z));
        }
    }
    parties = ordonner(std::move(parties));
    for (PartieImpression& partie : parties) {
        mesurer(partie, options.surplomb_max, matiere);
        strategie.surplomb += partie.surplomb;
    }
    strategie.description = std::format(
        "Le modèle est coupé en {} parties aux endroits choisis : le bas est imprimé d'abord, puis chaque zone "
        "dans la direction demandée, la tête inclinée et le plateau tourné.",
        parties.size());
    if (volume_total > 0.0 && parties.front().volume < 0.02 * volume_total) {
        strategie.remarques.push_back(
            "Presque tout le modèle est dans une zone orientée : il reste très peu de matière imprimée à plat "
            "pour la porter.");
    }
    strategie.parties = std::move(parties);
    return strategie;
}

}  // namespace artprint
