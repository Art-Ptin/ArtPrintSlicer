// ArtPrint Slicer - licence AGPL v3
// Tests du vérificateur de G-code. Chaque G-code est fabriqué ici même.
// D'abord des impressions correctes, qui doivent être acceptées ; puis une
// faute volontaire à la fois, qui doit être attrapée.
// Les valeurs de machine utilisées ici sont inventées pour les tests.
#include <cmath>
#include <format>
#include <iostream>
#include <string>
#include <utility>

#include "cinematique.hpp"
#include "gcode.hpp"
#include "reglages.hpp"
#include "verificateur.hpp"

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

constexpr double hauteur = 0.2;      // hauteur de couche des tests
constexpr double e_par_mm = 0.0374;  // filament poussé par mm de cordon

// Vérifie que le G-code est accepté ; sinon affiche les erreurs trouvées.
#define VERIFIER_ACCEPTE(rapport)                         \
    do {                                                  \
        VERIFIER((rapport).valide());                     \
        for (const auto& erreur_ : (rapport).erreurs) {   \
            std::cerr << decrire(erreur_);                \
        }                                                 \
    } while (0)

// Les essais de ce fichier sont écrits pour l'ancien format du G-code : les
// lignes donnent la position des axes, B va jusqu'à ses butées et le plateau
// tourne sans limite. Le contrat du firmware (positions de la pointe, B limité,
// C entre -360 et +360) a ses propres essais, plus bas.
Reglages ancien_format() {
    Reglages reglages;
    reglages.machine.contrat_gcode = 0;
    reglages.machine.b_inclinaison_max = 90.0;
    reglages.machine.c_min.reset();
    reglages.machine.c_max.reset();
    return reglages;
}

Reglages reglages_inclinables() {
    Reglages reglages = ancien_format();
    reglages.machine.distance_pivot_buse = 50.0;  // valeur de test, inventée
    return reglages;
}

const char* const debut = "G90\nM83\n";

// Un contour carré de 20 mm centré sur le plateau, à la hauteur z.
// « retraction » : faire (ou oublier) la rétraction avant d'y aller.
std::string couche_carree(double z, bool retraction = true) {
    std::string g;
    if (retraction) {
        g += "G1 E-0.8 F2400\n";
    }
    g += std::format("G0 X-10 Y-10 Z{:.3f} F9000\n", z);
    if (retraction) {
        g += "G1 E0.8 F2400\n";
    }
    const double e = 20.0 * e_par_mm;
    g += std::format("G1 X10 Y-10 E{:.4f} F1800\n", e);
    g += std::format("G1 X10 Y10 E{:.4f}\n", e);
    g += std::format("G1 X-10 Y10 E{:.4f}\n", e);
    g += std::format("G1 X-10 Y-10 E{:.4f}\n", e);
    return g;
}

void test_tour_carree_valide() {
    std::string g = debut;
    for (int couche = 1; couche <= 5; ++couche) {
        g += couche_carree(couche * hauteur);
    }
    const RapportVerification rapport = verifier_gcode_texte(g, ancien_format());
    VERIFIER_ACCEPTE(rapport);
    VERIFIER(rapport.extrusions == 20);
    VERIFIER(rapport.deplacements_a_vide == 5);
    VERIFIER(rapport.retractions == 5);
    VERIFIER(std::abs(rapport.longueur_extrusions - 400.0) < 1e-6);
}

void test_retraction_oubliee() {
    std::string g = debut;
    g += couche_carree(hauteur);
    g += couche_carree(2 * hauteur, false);
    const RapportVerification rapport = verifier_gcode_texte(g, ancien_format());
    VERIFIER(!rapport.valide());
    VERIFIER(rapport.nombre_erreurs == 1);
    VERIFIER(rapport.contient(TypeErreur::retraction_manquante));
    // Ligne 10 : le déplacement à vide vers la deuxième couche.
    VERIFIER(!rapport.erreurs.empty() && rapport.erreurs[0].ligne == 10);
}

void test_extrusion_sans_reprise() {
    std::string g = debut;
    g += couche_carree(hauteur);
    g += "G1 E-0.8\nG0 X20 Y20\nG1 X30 Y20 E0.4\n";
    const RapportVerification rapport = verifier_gcode_texte(g, ancien_format());
    VERIFIER(rapport.nombre_erreurs == 1);
    VERIFIER(rapport.contient(TypeErreur::extrusion_sans_reprise));
}

void test_couche_sous_la_matiere() {
    // La deuxième couche est imprimée AVANT la première : interdit.
    std::string g = debut;
    g += couche_carree(2 * hauteur);
    g += couche_carree(hauteur);
    const RapportVerification rapport = verifier_gcode_texte(g, ancien_format());
    VERIFIER(rapport.contient(TypeErreur::sous_matiere));
    VERIFIER(!rapport.contient(TypeErreur::dans_matiere));
}

void test_couche_dans_la_matiere() {
    // La même couche est imprimée deux fois au même endroit : interdit.
    std::string g = debut;
    g += couche_carree(hauteur);
    g += couche_carree(hauteur);
    const RapportVerification rapport = verifier_gcode_texte(g, ancien_format());
    VERIFIER(rapport.contient(TypeErreur::dans_matiere));
    VERIFIER(!rapport.contient(TypeErreur::sous_matiere));
}

void test_croisement_tolere() {
    // Deux lignes qui se croisent à la même hauteur (remplissage en grille),
    // puis des lignes voisines serrées : accepté.
    std::string g = debut;
    g += "G0 X-10 Y0 Z0.2\nG1 X10 Y0 E0.75\n";
    g += "G1 E-0.8\nG0 X0 Y-10\nG1 E0.8\nG1 X0 Y10 E0.75\n";
    g += "G1 E-0.8\nG0 X20 Y0\nG1 E0.8\n";
    for (int i = 0; i < 6; ++i) {  // zigzag, lignes espacées de 0,4 mm
        const double x = 20.0 + i * 0.4;
        g += std::format("G1 X{:.2f} Y{} E0.75\n", x, i % 2 == 0 ? 20 : 0);
        g += std::format("G1 X{:.2f} Y{} E0.015\n", x + 0.4, i % 2 == 0 ? 20 : 0);
    }
    const RapportVerification rapport = verifier_gcode_texte(g, ancien_format());
    VERIFIER_ACCEPTE(rapport);
}

void test_c_chemin_le_plus_court() {
    // De +170° à -170° : 340° de rotation au lieu de 20°. Refusé.
    const RapportVerification long_chemin =
        verifier_gcode_texte("G90\nG0 C170\nG0 C-170\n", ancien_format());
    VERIFIER(long_chemin.nombre_erreurs == 1);
    VERIFIER(long_chemin.contient(TypeErreur::c_chemin_long));
    VERIFIER(!long_chemin.erreurs.empty() && long_chemin.erreurs[0].ligne == 3 &&
             long_chemin.erreurs[0].solution.find("C190.000") != std::string::npos);

    // La même orientation écrite C190 : 20° de rotation. Accepté.
    VERIFIER_ACCEPTE(verifier_gcode_texte("G90\nG0 C170\nG0 C190\n", ancien_format()));
    // Plusieurs tours de suite par petits pas : accepté.
    VERIFIER_ACCEPTE(verifier_gcode_texte("G91\nG0 C170\nG0 C170\nG0 C170\nG0 C170\n", ancien_format()));
}

void test_limites_de_b() {
    // Sans hauteur de bascule : 55° maximum partout.
    Reglages reglages = reglages_inclinables();
    VERIFIER(verifier_gcode_texte("G0 Z100 B60\n", reglages).contient(TypeErreur::limite_b));
    VERIFIER_ACCEPTE(verifier_gcode_texte("G0 Z100 B55\n", reglages));

    // Avec une hauteur de bascule : 60° accepté en haut, refusé en bas.
    // (En s'inclinant, la pointe remonte : à Z1 B60 elle est à 26 mm du plateau.)
    reglages.machine.hauteur_bascule_b = 40.0;
    VERIFIER_ACCEPTE(verifier_gcode_texte("G0 Z100 B60\n", reglages));
    VERIFIER(verifier_gcode_texte("G0 Z1 B60\n", reglages).contient(TypeErreur::limite_b));

    // Butée de l'axe.
    VERIFIER(verifier_gcode_texte("G0 Z100 B95\n", reglages).contient(TypeErreur::limite_b));
}

void test_limites_de_c() {
    Reglages reglages = ancien_format();
    VERIFIER_ACCEPTE(verifier_gcode_texte("G0 C100\nG0 C250\nG0 C400\n", reglages));
    reglages.machine.c_min = -360.0;
    reglages.machine.c_max = 360.0;
    VERIFIER(verifier_gcode_texte("G0 C100\nG0 C250\nG0 C400\n", reglages)
                 .contient(TypeErreur::limite_c));
}

void test_courses_des_axes() {
    // Sans réglage de course : rien n'est vérifié.
    VERIFIER_ACCEPTE(verifier_gcode_texte("G0 X500 Y-500 Z200\n", ancien_format()));

    Reglages reglages = ancien_format();
    reglages.machine.course_x_min = -100.0;
    reglages.machine.course_x_max = 120.0;
    reglages.machine.course_y_max = 80.0;
    reglages.machine.course_z_max = 50.0;
    VERIFIER_ACCEPTE(verifier_gcode_texte("G0 X120 Y80 Z50\nG0 X-100 Y-900\n", reglages));

    const RapportVerification rapport =
        verifier_gcode_texte("G0 X121\nG0 X0 Y81\nG0 Y0 Z51\nG0 X-100.5 Z0\n", reglages);
    VERIFIER(rapport.nombre_erreurs == 4);
    VERIFIER(rapport.contient(TypeErreur::limite_course));
    VERIFIER(!rapport.erreurs.empty() &&
             rapport.erreurs[0].probleme.find("L'axe X") != std::string::npos &&
             rapport.erreurs[0].probleme.find("-100.0 à 120.0") != std::string::npos);
}

void test_reglage_manquant() {
    // Tête inclinée alors que la distance pivot-buse n'est pas renseignée.
    const RapportVerification rapport = verifier_gcode_texte("G0 Z50 B10\n", ancien_format());
    VERIFIER(rapport.contient(TypeErreur::reglage_manquant));
}

void test_volume_d_impression() {
    std::string dehors = debut;
    dehors += "G0 X140 Y0 Z0.2\nG1 X160 Y0 E0.75\n";
    VERIFIER(verifier_gcode_texte(dehors, ancien_format()).contient(TypeErreur::hors_volume));

    std::string trop_haut = debut;
    trop_haut += "G0 X0 Y0 Z290\nG1 X10 Y0 E0.4\n";
    VERIFIER(verifier_gcode_texte(trop_haut, ancien_format()).contient(TypeErreur::hors_volume));

    VERIFIER(verifier_gcode_texte("G0 X0 Y0 Z-1\n", ancien_format()).contient(TypeErreur::sous_plateau));

    // Positions intermédiaires : à l'arrivée (Z-5 B40) la pointe est à 6,7 mm
    // au-dessus du plateau, mais elle passe dessous pendant le mouvement.
    VERIFIER(verifier_gcode_texte("G0 Z-5 B40\n", reglages_inclinables())
                 .contient(TypeErreur::sous_plateau));
    VERIFIER_ACCEPTE(verifier_gcode_texte("G0 Z20\nG0 Z-5 B40\n", reglages_inclinables()));
}

void test_commandes_refusees() {
    VERIFIER(verifier_gcode_texte("G2 X10 Y10 I5 J0 E1\n", ancien_format())
                 .contient(TypeErreur::commande_non_geree));
    VERIFIER(verifier_gcode_texte("G1 X1 @\n", ancien_format()).contient(TypeErreur::syntaxe));
    // Les commandes de Klipper et les commentaires sont ignorés.
    VERIFIER_ACCEPTE(verifier_gcode_texte(
        "; commentaire\nSET_VELOCITY_LIMIT VELOCITY=200\nM104 S210\nG28\n\n", ancien_format()));
}

// Paroi conique imprimée en 5 axes continu : la buse est inclinée de 30° et
// reste alignée avec la paroi, le plateau tourne pendant l'extrusion.
// Les couches sont imprimées de « premiere » à « derniere », par pas de « pas ».
std::string cone_5_axes(const Reglages& reglages, int premiere, int derniere, int pas) {
    const double inclinaison = en_radians(30.0);
    // On monte d'abord : incliner la tête au ras du plateau la ferait passer dessous.
    std::string g = std::string(debut) + "G0 Z20\n";
    double c = 0.0;
    for (int couche = premiere; couche != derniere + pas; couche += pas) {
        for (int i = 0; i <= 120; ++i) {
            const double angle = en_radians(i * 3.0);
            const Vec3 direction{std::sin(inclinaison) * std::cos(angle),
                                 std::sin(inclinaison) * std::sin(angle), std::cos(inclinaison)};
            const Vec3 base{20.0 * std::cos(angle), 20.0 * std::sin(angle), hauteur};
            const PoseOutil pose{base + direction * (couche * hauteur), direction};
            const PositionMachine p = *position_machine(reglages.machine, pose, c);
            c = p.c;
            const std::string axes = std::format("X{:.4f} Y{:.4f} Z{:.4f} B{:.4f} C{:.4f}", p.x,
                                                 p.y, p.z, p.b, p.c);
            if (i == 0) {
                g += "G1 E-0.8\nG0 " + axes + "\nG1 E0.8\n";
            } else {
                g += std::format("G1 {} E{:.4f}\n", axes, 1.1 * e_par_mm);
            }
        }
    }
    return g;
}

void test_cone_5_axes_valide() {
    const Reglages reglages = reglages_inclinables();
    const RapportVerification rapport =
        verifier_gcode_texte(cone_5_axes(reglages, 0, 9, 1), reglages);
    VERIFIER_ACCEPTE(rapport);
    VERIFIER(rapport.extrusions == 1200);
}

void test_cone_5_axes_a_l_envers() {
    // Le même cône imprimé du haut vers le bas : chaque couche passe sous
    // la précédente.
    const Reglages reglages = reglages_inclinables();
    const RapportVerification rapport =
        verifier_gcode_texte(cone_5_axes(reglages, 9, 0, -1), reglages);
    VERIFIER(rapport.contient(TypeErreur::sous_matiere));
}

void test_message_d_erreur() {
    const RapportVerification rapport =
        verifier_gcode_texte("G90\nG0 X1 Y2 Z3 C170\nG0 C-170\n", ancien_format());
    VERIFIER(rapport.erreurs.size() == 1);
    if (rapport.erreurs.empty()) {
        return;
    }
    const std::string texte = decrire(rapport.erreurs[0]);
    VERIFIER(texte.find("Ligne 3 : G0 C-170") != std::string::npos);
    VERIFIER(texte.find("Problème : ") != std::string::npos);
    VERIFIER(texte.find("X=1.000 Y=2.000 Z=3.000 B=0.000° C=-170.000°") != std::string::npos);
    VERIFIER(texte.find("Solution possible : ") != std::string::npos);
}

void test_lecture_des_lignes() {
    const LigneGcode serree = lire_ligne_gcode("g1x10y-2.5 e.4 ; commentaire X99");
    VERIFIER(serree.lettre == 'G' && serree.numero == 1);
    VERIFIER(serree.parametre('X') == 10.0);
    VERIFIER(serree.parametre('Y') == -2.5);
    VERIFIER(serree.parametre('E') == 0.4);
    VERIFIER(!serree.parametre('Z'));

    VERIFIER(lire_ligne_gcode("N12 M83").numero == 83);
    VERIFIER(lire_ligne_gcode("   ; rien").lettre == 0);
    VERIFIER(lire_ligne_gcode("MANUAL_STEPPER STEPPER=b GCODE_AXIS=B").lettre == 0);
}

void test_fichier_de_reglages() {
    const Reglages reglages = lire_reglages_texte(
        "# essai\r\n"
        "distance_pivot_buse = 12,5   # virgule française\r\n"
        "axe_pivot_b = X\n"
        "sens_c = -1\n"
        "hauteur_bascule_b = 30\n"
        "\n"
        "hauteur_couche = 0.3\n");
    VERIFIER(reglages.machine.distance_pivot_buse == 12.5);
    VERIFIER(reglages.machine.axe_pivot_b == AxePivot::x);
    VERIFIER(reglages.machine.sens_c == -1);
    VERIFIER(reglages.machine.hauteur_bascule_b == 30.0);
    VERIFIER(reglages.impression.hauteur_couche == 0.3);
    // Ce qui n'est pas écrit garde sa valeur par défaut.
    VERIFIER(reglages.machine.rayon_plateau == 150.0);
    // (D'office, le plateau a une butée d'un tour dans chaque sens, et le
    // G-code suit le contrat du firmware.)
    VERIFIER(reglages.machine.c_min && *reglages.machine.c_min == -360.0);
    VERIFIER(reglages.machine.contrat_gcode == 1 && reglages.machine.b_inclinaison_max == 50.0);

    auto refuse = [](const char* texte, const char* attendu) {
        try {
            lire_reglages_texte(texte);
        } catch (const ErreurReglages& erreur) {
            return std::string(erreur.what()).find(attendu) != std::string::npos;
        }
        return false;
    };
    VERIFIER(refuse("vitesse_lumiere = 3\n", "réglage inconnu"));
    VERIFIER(refuse("\nrayon_plateau = grand\n", "ligne 2"));
    VERIFIER(refuse("sens_b = 2\n", "1 ou -1"));
    VERIFIER(refuse("rayon_plateau 150\n", "="));
    VERIFIER(refuse("b_min = 10\nb_max = 5\n", "b_min"));
}

}  // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"tour carrée correcte", test_tour_carree_valide},
        {"rétraction oubliée", test_retraction_oubliee},
        {"extrusion sans reprise", test_extrusion_sans_reprise},
        {"couche imprimée sous la matière", test_couche_sous_la_matiere},
        {"couche imprimée dans la matière", test_couche_dans_la_matiere},
        {"croisement de lignes toléré", test_croisement_tolere},
        {"C par le chemin le plus court", test_c_chemin_le_plus_court},
        {"limites de B", test_limites_de_b},
        {"limites de C", test_limites_de_c},
        {"courses des axes X, Y, Z", test_courses_des_axes},
        {"réglage manquant", test_reglage_manquant},
        {"volume d'impression", test_volume_d_impression},
        {"commandes refusées ou ignorées", test_commandes_refusees},
        {"cône 5 axes continu correct", test_cone_5_axes_valide},
        {"cône 5 axes imprimé à l'envers", test_cone_5_axes_a_l_envers},
        {"message d'erreur complet", test_message_d_erreur},
        {"lecture des lignes de G-code", test_lecture_des_lignes},
        {"fichier de réglages", test_fichier_de_reglages},
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
        std::cout << "Tous les tests du vérificateur sont passés.\n";
        return 0;
    }
    std::cout << echecs << " vérification(s) en échec.\n";
    return 1;
}
