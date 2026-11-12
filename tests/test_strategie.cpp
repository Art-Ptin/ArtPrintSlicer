// ArtPrint Slicer - licence AGPL v3
// Tests du 5 axes : découpe d'un modèle par un plan, squelette (tronc et
// branches), stratégies proposées, et G-code multi-directions relu par le
// vérificateur. Les modèles et les réglages de machine sont inventés ici.
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "cinematique.hpp"
#include "decoupe.hpp"
#include "formes_organiques.hpp"
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
    // Une même orientation s'obtient en penchant la tête d'un côté ou de
    // l'autre : les deux solutions amènent la buse au même endroit, avec B
    // de signe opposé et le plateau tourné d'un demi-tour.
    const PoseOutil voulue{pointe, essai::direction(40.0, 25.0)};
    const auto d_un_cote = position_machine(machine, voulue, 0.0, +1);
    const auto de_l_autre = position_machine(machine, voulue, 0.0, -1);
    VERIFIER(d_un_cote.has_value() && de_l_autre.has_value());
    if (d_un_cote && de_l_autre) {
        VERIFIER(proche(d_un_cote->b, 40.0, 1e-9) && proche(de_l_autre->b, -40.0, 1e-9));
        VERIFIER(proche(std::abs(angle_le_plus_proche(d_un_cote->c - de_l_autre->c, 180.0)), 180.0, 1e-9));
        for (const auto& solution : {*d_un_cote, *de_l_autre}) {
            const PoseOutil pose = pose_dans_piece(machine, solution);
            VERIFIER(norme(pose.pointe - pointe) < 1e-9);
            VERIFIER(norme(pose.direction - voulue.direction) < 1e-9);
        }
    }
}

// Couches qui pivotent : un tube courbe posé sur le plateau, qui part à la
// verticale et se couche jusqu'à 70°. Ses couches suivent sa courbe.
void test_couches_pivotantes() {
    const Reglages r = reglages_essai();
    const auto partie_pour = [](const essai::TubeCourbe& tube) {
        const Maillage brut = tube.maillage();
        const BoiteEnglobante boite = calculer_boite(brut);
        const Vec3 decalage{(boite.min.x + boite.max.x) / 2.0, (boite.min.y + boite.max.y) / 2.0, boite.min.z};
        PartieImpression partie;
        partie.maillage = poser_au_centre(brut);
        partie.guide.push_back({Vec3{-decalage.x, -decalage.y, 0.0}, {0, 0, 1}});
        for (double angle = 0.0; angle <= tube.inclinaison_finale + 1e-9; angle += 2.5) {
            partie.guide.push_back({tube.point(angle) - decalage, tube.direction(angle)});
        }
        return partie;
    };
    const essai::TubeCourbe tube;
    Strategie courbe;
    courbe.type = TypeStrategie::par_branches;
    courbe.parties.push_back(partie_pour(tube));
    VERIFIER(ferme(courbe.parties.front().maillage));
    VERIFIER(couches_pivotantes_possibles(courbe.parties.front(), r.impression));

    const ResultatTranchage resultat = trancher_strategie(courbe, r);
    VERIFIER_ACCEPTE(resultat.verification);
    // La tête s'incline un peu à chaque couche : beaucoup de valeurs de B, de 0° à 70°.
    std::set<std::string> valeurs_b;
    std::istringstream lignes(resultat.gcode);
    for (std::string ligne; std::getline(lignes, ligne);) {
        const auto ou = ligne.find(" B");
        if (ou != std::string::npos) {
            valeurs_b.insert(ligne.substr(ou + 2, ligne.find(' ', ou + 2) - ou - 2));
        }
    }
    VERIFIER(valeurs_b.size() > 150);
    VERIFIER(valeurs_b.count("0.000") == 1 && valeurs_b.count("70.000") == 1);
    // Aucune paroi n'est imprimée dans le vide ; en 3 axes, il y en a.
    VERIFIER(resultat.verification.longueur_dans_le_vide < 1.0);
    VERIFIER(trancher(courbe.parties.front().maillage, r).verification.longueur_dans_le_vide > 3.0);

    // Une courbe trop serrée pour l'épaisseur du tube est reconnue et refusée.
    essai::TubeCourbe serre;
    serre.rayon_courbure = 8.0;
    VERIFIER(!couches_pivotantes_possibles(partie_pour(serre), r.impression));
}

// Objets organiques d'une seule pièce : la stratégie par branches est
// proposée, son G-code est accepté, et il laisse moins de parois dans le vide.
void test_objets_organiques() {
    const Reglages r = reglages_essai();
    for (const auto& fabriquer : {essai::arbre_organique, essai::chassis_drone, essai::tuyau_en_y,
                                  essai::arbre_double, essai::buisson}) {
        const Maillage pose = poser_au_centre(fabriquer());
        VERIFIER(ferme(pose));
        const std::vector<Strategie> strategies = proposer_strategies(pose, r);
        const Strategie* branches = trouver(strategies, TypeStrategie::par_branches);
        VERIFIER(branches != nullptr);
        if (!branches) {
            continue;
        }
        VERIFIER(branches->parties.size() >= 3);
        VERIFIER(branches->surplomb < 0.5 * strategies.front().surplomb);
        const ResultatTranchage par_branches = trancher_strategie(*branches, r);
        VERIFIER_ACCEPTE(par_branches.verification);
        const ResultatTranchage classique = trancher_strategie(strategies.front(), r);
        VERIFIER_ACCEPTE(classique.verification);
        VERIFIER(par_branches.verification.longueur_dans_le_vide <=
                 classique.verification.longueur_dans_le_vide + 1.0);
    }
    // Une branche qui porte elle-même une branche : la petite est rattachée à la
    // grosse (pas au tronc) et elle est imprimée après elle.
    const std::vector<Strategie> double_niveau = proposer_strategies(poser_au_centre(essai::arbre_double()), r);
    if (const Strategie* branches = trouver(double_niveau, TypeStrategie::par_branches)) {
        bool portee_par_une_branche = false;
        for (std::size_t i = 0; i < branches->parties.size(); ++i) {
            const int parent = branches->parties[i].parent;
            VERIFIER(parent < static_cast<int>(i));
            portee_par_une_branche = portee_par_une_branche || parent > 0;
        }
        VERIFIER(portee_par_une_branche);
    }
    // Un buisson : six branches autour du tronc, chacune a sa propre partie.
    const std::vector<Strategie> buisson = proposer_strategies(poser_au_centre(essai::buisson()), r);
    if (const Strategie* branches = trouver(buisson, TypeStrategie::par_branches)) {
        VERIFIER(branches->parties.size() == 7);
    }
    // Un champignon n'a pas de branche : pas de stratégie par branches, mais
    // une autre pose (retourné) qui demande moins de supports.
    const std::vector<Strategie> champignon = proposer_strategies(poser_au_centre(essai::champignon()), r);
    VERIFIER(trouver(champignon, TypeStrategie::par_branches) == nullptr);
    VERIFIER(trouver(champignon, TypeStrategie::autre_pose) != nullptr);
}

}  // namespace

int main() {
    test_decoupe();
    test_squelette();
    test_cinematique();
    test_strategies(essai::arbre(), "arbre en plusieurs volumes");
    test_strategies(essai::arbre_plat(), "arbre d'une seule pièce");
    test_b_vertical();
    test_couches_pivotantes();
    test_objets_organiques();
    if (echecs == 0) {
        std::cout << "Tests du 5 axes : tout est correct.\n";
        return 0;
    }
    std::cerr << echecs << " vérification(s) en échec.\n";
    return 1;
}
