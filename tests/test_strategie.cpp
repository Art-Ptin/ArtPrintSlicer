// ArtPrint Slicer - licence AGPL v3
// Tests du 5 axes : découpe d'un modèle par un plan, squelette (tronc et
// branches), stratégies proposées, et G-code multi-directions relu par le
// vérificateur. Les modèles et les réglages de machine sont inventés ici.
#include <cmath>
#include <iostream>
#include <string>

#include "cinematique.hpp"
#include "decoupe.hpp"
#include "modeles_essai.hpp"
#include "multiaxe.hpp"
#include "slicer.hpp"
#include "squelette.hpp"
#include "strategie.hpp"

using namespace artprint;

namespace {

int echecs = 0;

#define VERIFIER(condition)                                                     \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "ÉCHEC (ligne " << __LINE__ << ") : " #condition "\n"; \
            ++echecs;                                                           \
        }                                                                       \
    } while (0)

#define VERIFIER_ACCEPTE(rapport)                       \
    do {                                                \
        VERIFIER((rapport).valide());                   \
        for (const auto& erreur_ : (rapport).erreurs) { \
            std::cerr << decrire(erreur_);              \
        }                                               \
    } while (0)

bool proche(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

bool ferme(const Maillage& m) {
    const RapportFermeture f = verifier_fermeture(m);
    return f.ferme && f.orientation_coherente;
}

// Réglages d'une machine d'essai (valeurs inventées).
Reglages reglages_essai() {
    Reglages r;
    r.machine.distance_pivot_buse = 37.5;
    r.machine.hauteur_bascule_b = 10.0;
    return r;
}

const Strategie* trouver(const std::vector<Strategie>& strategies, TypeStrategie type) {
    for (const Strategie& s : strategies) {
        if (s.type == type) {
            return &s;
        }
    }
    return nullptr;
}

void test_decoupe() {
    // Un cylindre vertical de 40 mm coupé à 15 mm de haut.
    ConstructeurMaillage c;
    essai::ajouter_cylindre(c, {0, 0, 0}, {0, 0, 40}, 10.0, 64);
    const Maillage cylindre = c.terminer();
    VERIFIER(ferme(cylindre));
    const double volume = volume_maillage(cylindre);
    VERIFIER(proche(volume, std::numbers::pi * 100.0 * 40.0, 60.0));

    const auto coupe = couper_par_plan(cylindre, {{0, 0, 15}, {0, 0, 1}}, {{0, 0, 30}});
    VERIFIER(coupe.has_value());
    if (coupe) {
        VERIFIER(ferme(coupe->partie) && ferme(coupe->reste));
        VERIFIER(proche(volume_maillage(coupe->partie), volume * 25.0 / 40.0, 1.0));
        VERIFIER(proche(volume_maillage(coupe->reste), volume * 15.0 / 40.0, 1.0));
        VERIFIER(proche(coupe->surface_coupe, std::numbers::pi * 100.0, 2.0));
        VERIFIER(coupe->boucles == 1 && proche(coupe->centre_coupe.z, 15.0, 1e-6));
    }
    // Coupe de biais : les deux morceaux restent fermés et leur volume s'ajoute.
    const auto biais = couper_par_plan(cylindre, {{0, 0, 20}, essai::direction(40.0, 30.0)}, {{0, 0, 38}});
    VERIFIER(biais.has_value());
    if (biais) {
        VERIFIER(ferme(biais->partie) && ferme(biais->reste));
        VERIFIER(proche(volume_maillage(biais->partie) + volume_maillage(biais->reste), volume, 1.0));
    }
    // Un plan qui ne touche pas le modèle : rien à séparer.
    VERIFIER(!couper_par_plan(cylindre, {{0, 0, 50}, {0, 0, 1}}, {{0, 0, 60}}).has_value());

    VERIFIER(proche(distance_point_triangle({0.2, 0.2, 3}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}), 3.0, 1e-12));
    VERIFIER(proche(distance_point_triangle({2, 0, 0}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}), 1.0, 1e-12));
}

void test_squelette() {
    const Squelette s = calculer_squelette(poser_au_centre(essai::arbre()));
    VERIFIER(!s.boucles && s.voxels > 10000);
    // Un tronc en trois morceaux et deux branches de côté.
    VERIFIER(s.branches.size() == 5);
    int penchees = 0;
    for (const BrancheSquelette& b : s.branches) {
        const double inclinaison = en_degres(std::acos(b.direction.z));
        if (inclinaison > 45.0) {
            ++penchees;
            VERIFIER(proche(inclinaison, 60.0, 4.0) || proche(inclinaison, 75.0, 4.0));
            VERIFIER(b.parent >= 0 && b.enfants.empty());
            VERIFIER(proche(b.tranches[b.tranches.size() / 2].rayon, 3.25, 0.6));
        } else {
            VERIFIER(inclinaison < 12.0);
        }
    }
    VERIFIER(penchees == 2);
    VERIFIER(s.branches.front().parent == -1);
    VERIFIER(proche(s.branches.front().volume_total, static_cast<double>(s.voxels) * s.pas * s.pas * s.pas, 1.0));

    // Un simple cylindre : une seule branche, verticale.
    ConstructeurMaillage c;
    essai::ajouter_cylindre(c, {0, 0, 0}, {0, 0, 30}, 5.0);
    const Squelette simple = calculer_squelette(c.terminer());
    VERIFIER(simple.branches.size() == 1 && simple.branches.front().direction.z > 0.99);
}

void test_strategies(const Maillage& modele, const char* nom) {
    const Reglages r = reglages_essai();
    const Maillage pose = poser_au_centre(modele);
    const std::vector<Strategie> strategies = proposer_strategies(pose, r);
    VERIFIER(!strategies.empty() && strategies.front().type == TypeStrategie::classique);
    const Strategie& classique = strategies.front();
    const Strategie* branches = trouver(strategies, TypeStrategie::par_branches);
    VERIFIER(branches != nullptr);
    if (!branches) {
        std::cerr << "(" << nom << ")\n";
        return;
    }
    // Le tronc puis les deux branches ; presque plus rien à soutenir.
    VERIFIER(branches->parties.size() == 3);
    VERIFIER(classique.surplomb > 100.0);
    VERIFIER(branches->surplomb < 0.35 * classique.surplomb);
    double volume = 0.0;
    for (std::size_t p = 0; p < branches->parties.size(); ++p) {
        const PartieImpression& partie = branches->parties[p];
        VERIFIER(ferme(partie.maillage));
        VERIFIER((p == 0) == (partie.parent < 0));
        VERIFIER(p == 0 ? partie.inclinaison < 0.01 : partie.inclinaison > 50.0);
        volume += partie.volume;
    }
    // La découpe ne crée ni ne perd de matière.
    VERIFIER(proche(volume, volume_maillage(pose), 0.01 * volume));
    // Les branches sont imprimées de la plus basse à la plus haute.
    if (branches->parties.size() == 3) {
        VERIFIER(branches->parties[1].base.z < branches->parties[2].base.z);
    }

    // Le G-code multi-directions respecte toutes les règles du vérificateur.
    const ResultatTranchage resultat = trancher_strategie(*branches, r);
    VERIFIER_ACCEPTE(resultat.verification);
    VERIFIER(resultat.couches > 100 && resultat.longueur_filament > 100.0);
    VERIFIER(resultat.gcode.find(";PARTIE:3") != std::string::npos);
    VERIFIER(resultat.apercu.size() == resultat.couches);
    VERIFIER(resultat.verification.retractions > 3);

    // La stratégie classique passe par le slicer 3 axes.
    const ResultatTranchage simple = trancher_strategie(classique, r);
    VERIFIER_ACCEPTE(simple.verification);
    VERIFIER(simple.gcode.find(";PARTIE") == std::string::npos);

    // Faute provoquée : une branche imprimée deux fois est refusée.
    Strategie deux_fois = *branches;
    deux_fois.parties.push_back(deux_fois.parties[1]);
    const ResultatTranchage faute = trancher_strategie(deux_fois, r);
    VERIFIER(!faute.verification.valide());
    VERIFIER(faute.verification.contient(TypeErreur::dans_matiere) ||
             faute.verification.contient(TypeErreur::sous_matiere));

    // Sans hauteur de bascule, B est limité à 55° partout : la découpe faite
    // pour 60° et 75° est refusée, mais la stratégie recalculée s'y adapte.
    Reglages sans_bascule = r;
    sans_bascule.machine.hauteur_bascule_b.reset();
    VERIFIER(trancher_strategie(*branches, sans_bascule).verification.contient(TypeErreur::limite_b));
    const std::vector<Strategie> prudentes = proposer_strategies(pose, sans_bascule);
    const Strategie* prudente = trouver(prudentes, TypeStrategie::par_branches);
    VERIFIER(prudente != nullptr);
    if (prudente) {
        for (const PartieImpression& partie : prudente->parties) {
            VERIFIER(partie.inclinaison <= 55.0 + 1e-6);
        }
        VERIFIER_ACCEPTE(trancher_strategie(*prudente, sans_bascule).verification);
    }

    // Sans la distance pivot-buse, une partie penchée ne peut pas être tranchée.
    Reglages sans_pivot = r;
    sans_pivot.machine.distance_pivot_buse.reset();
    bool refuse = false;
    try {
        trancher_strategie(*branches, sans_pivot);
    } catch (const std::exception&) {
        refuse = true;
    }
    VERIFIER(refuse);
}

void test_b_vertical() {
    // Sur certaines machines, B ne vaut pas 0 quand la buse est verticale.
    Reglages r = reglages_essai();
    r.machine.b_vertical = -90.0;
    const Maillage pose = poser_au_centre(essai::arbre());
    const std::vector<Strategie> strategies = proposer_strategies(pose, r);
    const Strategie* branches = trouver(strategies, TypeStrategie::par_branches);
    VERIFIER(branches != nullptr);
    if (branches) {
        const ResultatTranchage resultat = trancher_strategie(*branches, r);
        VERIFIER_ACCEPTE(resultat.verification);
        VERIFIER(resultat.gcode.find(" B-90.000 ") != std::string::npos);  // buse verticale
        VERIFIER(resultat.gcode.find(" B0.000 ") == std::string::npos);
    }
    const ResultatTranchage simple = trancher(pose, r);
    VERIFIER_ACCEPTE(simple.verification);
    VERIFIER(simple.gcode.find(" B-90 C0 ") != std::string::npos);
}

void test_cinematique() {
    // La position calculée pour une pointe et des angles donnés ramène bien
    // la buse à cet endroit de la pièce.
    const ReglagesMachine machine = reglages_essai().machine;
    const Vec3 pointe{12.0, -7.0, 33.0};
    for (const double b : {0.0, 25.0, -60.0}) {
        for (const double c : {0.0, 75.0, -200.0}) {
            const PositionMachine m = position_avec_angles(machine, pointe, b, c);
            const PoseOutil pose = pose_dans_piece(machine, m);
            VERIFIER(norme(pose.pointe - pointe) < 1e-9);
            VERIFIER(proche(en_degres(std::acos(pose.direction.z)), std::abs(b), 1e-9));
        }
    }
}

}  // namespace

int main() {
    test_decoupe();
    test_squelette();
    test_cinematique();
    test_strategies(essai::arbre(), "arbre en plusieurs volumes");
    test_strategies(essai::arbre_plat(), "arbre d'une seule pièce");
    test_b_vertical();
    if (echecs == 0) {
        std::cout << "Tests du 5 axes : tout est correct.\n";
        return 0;
    }
    std::cerr << echecs << " vérification(s) en échec.\n";
    return 1;
}
