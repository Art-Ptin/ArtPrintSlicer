// ArtPrint Slicer - licence AGPL v3
#include "strategie.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

#include "decoupe.hpp"
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

// Découpe le modèle en suivant son squelette : une branche qui s'écarte trop
// de la direction de la partie qui la porte devient une partie à elle seule.
class DecoupeParBranches {
public:
    DecoupeParBranches(const Maillage& pose, const Squelette& squelette, const Reglages& reglages,
                       const OptionsStrategie& options, std::vector<std::string>& remarques)
        : squelette_(squelette), machine_(reglages.machine), options_(options),
          remarques_(remarques) {
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
            const double ecart = angle_entre(b.direction, parties_[partie].direction);
            const double rayon = b.tranches.front().rayon;
            // Une branche courte ou peu écartée s'imprime bien avec sa mère.
            if (ecart <= options_.surplomb_max || b.longueur < 2.0 * rayon) {
                parcourir(enfant, partie);
                continue;
            }
            const int nouvelle = detacher(enfant, partie);
            parcourir(enfant, nouvelle >= 0 ? nouvelle : partie);
        }
    }

    // Centres des tranches de la branche et de toutes celles qu'elle porte.
    void graines_de(int branche, std::vector<Vec3>& graines) const {
        const BrancheSquelette& b = squelette_.branches[branche];
        for (const TrancheSquelette& tranche : b.tranches) {
            graines.push_back(tranche.centre);
        }
        for (const int enfant : b.enfants) {
            graines_de(enfant, graines);
        }
    }

    // Sépare la branche de la partie qui la porte. Renvoie le numéro de la
    // nouvelle partie, ou -1 si aucune coupe propre n'a été trouvée.
    int detacher(int branche, int partie) {
        const BrancheSquelette& b = squelette_.branches[branche];
        const TrancheSquelette& depart = b.tranches.front();
        const double possible = inclinaison_possible(machine_, depart.centre.z - depart.rayon);
        const Vec3 direction = redresser(b.direction, possible);
        const double redressement = angle_entre(direction, b.direction);

        std::vector<Vec3> toutes;
        graines_de(branche, toutes);
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
            nouvelle.base = coupe->centre_coupe;
            nouvelle.parent = partie;
            parties_[partie].maillage = std::move(coupe->reste);
            parties_.push_back(std::move(nouvelle));
            if (redressement > 1.0) {
                remarques_.push_back(std::format(
                    "Une branche penche de {:.0f}° : la machine ne peut incliner la tête que de "
                    "{:.0f}° à cette hauteur. Elle sera imprimée à {:.0f}°, avec un peu de surplomb.",
                    angle_entre(b.direction, {0.0, 0.0, 1.0}), possible, possible));
            }
            return static_cast<int>(parties_.size()) - 1;
        }
        remarques_.push_back(std::format(
            "Une branche (vers {:.0f} mm de haut) n'a pas pu être séparée proprement : elle reste "
            "imprimée avec la partie qui la porte.",
            depart.centre.z));
        return -1;
    }

    const Squelette& squelette_;
    const ReglagesMachine machine_;
    const OptionsStrategie options_;
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
        DecoupeParBranches decoupe(pose, squelette, reglages, options, branches.remarques);
        std::vector<PartieImpression> parties = ordonner(decoupe.prendre());
        if (parties.size() > 1) {
            for (PartieImpression& partie : parties) {
                mesurer(partie, options.surplomb_max, matiere);
                branches.surplomb += partie.surplomb;
            }
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
