// ArtPrint Slicer - licence AGPL v3
// Réglages de la machine, de l'impression et du vérificateur. Rien de ce qui dépend de
// l'imprimante n'est écrit en dur ailleurs dans le code : tout passe par ici.
//
// Le fichier de réglages est un simple texte, une ligne par réglage :
//   # commentaire
//   distance_pivot_buse = 12,5
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace artprint {

// Axe horizontal autour duquel la tête s'incline (axe B).
enum class AxePivot { x, y };

struct ReglagesMachine {
    // Plateau rond et hauteur d'impression, en millimètres.
    double rayon_plateau = 150.0;
    double hauteur_impression = 260.0;

    // Distance entre le pivot B et la pointe de la buse. À MESURER sur la
    // machine : il n'y a volontairement aucune valeur par défaut.
    std::optional<double> distance_pivot_buse;

    // Géométrie des axes. Valeurs par défaut à confirmer sur la machine.
    AxePivot axe_pivot_b = AxePivot::y;
    int sens_b = 1;  // 1 ou -1 : sens d'inclinaison pour un B positif
    int sens_c = 1;  // 1 ou -1 : sens de rotation du plateau pour un C positif
    double centre_plateau_x = 0.0;  // position X, Y machine du centre du plateau
    double centre_plateau_y = 0.0;

    // Courses des axes X, Y et Z, en mm, dans les coordonnées du G-code.
    // À MESURER sur la machine. Absentes : la course n'est pas vérifiée.
    std::optional<double> course_x_min, course_x_max;
    std::optional<double> course_y_min, course_y_max;
    std::optional<double> course_z_min, course_z_max;

    // Butées de l'axe B, en degrés.
    // Valeur de B, dans le G-code, quand la buse est verticale. Les butées et
    // les limites de B ci-dessous sont des inclinaisons comptées depuis cette
    // position verticale.
    double b_vertical = 0.0;
    // Vrai : sur une partie courbe, B et C tournent pendant l'extrusion
    // (5 axes continu). Faux : ils ne tournent qu'à vide, entre deux couches.
    bool pivot_continu = true;
    double b_min = -90.0;
    double b_max = 90.0;
    // Inclinaison maximale de B que le firmware accepte, en degrés. Le slicer
    // ne la dépasse jamais ; une partie qui demanderait plus est signalée.
    double b_inclinaison_max = 50.0;
    // Vitesses maximales des axes de rotation, en degrés par seconde : le
    // firmware les impose, les durées estimées en tiennent compte.
    double vitesse_b_max = 5.0;
    double vitesse_c_max = 10.0;
    // Contrat du G-code. 1 : les lignes donnent la position de la POINTE de
    // la buse (le firmware calcule lui-même celle des axes), le fichier
    // commence et finit par les macros ARTPRINT_PRINT_START et
    // ARTPRINT_PRINT_END. 0 : ancien format, positions des axes.
    int contrat_gcode = 1;
    // Inclinaison maximale près du plateau, en degrés.
    double b_limite_pres_plateau = 55.0;
    // Hauteur au-dessus de laquelle B peut dépasser la limite précédente.
    // À MESURER : tant qu'elle manque, la limite basse s'applique partout.
    std::optional<double> hauteur_bascule_b;
    // Encombrement simplifié de la tête, pour qu'elle ne touche pas le
    // plateau quand elle s'incline : un bloc rectangulaire qui part du pivot
    // et s'étend du côté opposé à la buse. À MESURER sur la machine. Quand
    // les deux valeurs (et la distance pivot-buse) sont connues, la hauteur
    // où la tête peut dépasser la limite près du plateau est calculée
    // d'après ce bloc : la hauteur de bascule ne sert plus.
    std::optional<double> tete_demi_largeur;  // de chaque côté de l'axe de la buse, en mm
    std::optional<double> tete_hauteur;       // au-delà du pivot, en mm

    // Butées de l'axe C, en degrés. Absentes : le plateau tourne sans limite.
    // Avant d'en atteindre une, le slicer déroule le plateau d'un tour, à
    // vide, la buse relevée.
    std::optional<double> c_min = -360.0;
    std::optional<double> c_max = 360.0;

    // Collisions avec la vraie forme de l'imprimante.
    // Chemin du modèle de machine, fabriqué à partir du fichier STEP par
    // « outils/convertir_machine.py ». Vide : les collisions ne sont pas vérifiées.
    std::string modele_machine;
    // Distance minimale à garder entre les pièces, en mm.
    double marge_securite = 2.0;
    // Rayon de la zone libre autour de la pointe de la buse, en mm : le
    // contact avec la matière y est normal.
    double zone_buse = 5.0;
    // Marge propre à la buse, en mm : elle travaille au ras de la matière
    // et du plateau, la marge de sécurité générale ne peut pas s'y appliquer.
    double marge_buse = 0.2;
};

// Réglages d'impression : la façon de fabriquer la pièce.
struct ReglagesImpression {
    // Cordon déposé, en millimètres.
    double largeur_extrusion = 0.45;
    // Peau courbe : une dernière passe suit la forme du dessus de la pièce
    // pour effacer les marches d'escalier (voir peau_courbe.hpp).
    bool peau_courbe = false;
    // Pente la plus raide suivie par la peau courbe, en degrés depuis l'horizontale.
    double peau_courbe_pente_max = 30.0;
    double hauteur_couche = 0.2;
    double hauteur_premiere_couche = 0.2;
    double diametre_filament = 1.75;
    double debit = 1.0;  // multiplicateur de matière (1 = normal)

    // Coque : parois et couches pleines du dessus et du dessous.
    int nombre_parois = 2;
    int couches_dessus = 4;
    int couches_dessous = 3;
    // Remplissage intérieur, en pourcentage (0 = creux, 100 = plein).
    double densite_remplissage = 15.0;
    // Part de la largeur du cordon dont le remplissage mord sur les parois.
    double recouvrement_remplissage = 0.25;

    // Jupe : boucles tracées autour de la pièce pour amorcer la buse.
    int jupe_boucles = 1;
    double jupe_distance = 5.0;

    // Vitesses, en mm/s.
    double vitesse_paroi_externe = 25.0;
    double vitesse_paroi_interne = 40.0;
    double vitesse_remplissage = 60.0;
    double vitesse_premiere_couche = 20.0;
    double vitesse_a_vide = 120.0;

    // Rétraction avant chaque déplacement à vide.
    double retraction_longueur = 2.0;  // mm de filament
    double retraction_vitesse = 40.0;  // mm/s
    double levee_z = 0.2;              // la buse monte de cette hauteur pendant le trajet

    // Températures, en °C. Plateau à 0 : pas de plateau chauffant.
    double temperature_buse = 210.0;
    double temperature_plateau = 0.0;
    bool ventilation = true;  // ventilateur de pièce à partir de la 2e couche
    // Préparation avant l'impression (chauffe, palpage du plateau), en
    // minutes : ajoutée au temps d'impression dans les estimations.
    double duree_preparation = 5.0;
};

struct ReglagesVerification {
    // Finesse du contrôle de la matière (taille des petits cubes), en mm.
    // Elle est réduite toute seule à la moitié de la hauteur de couche.
    double pas_controle = 0.1;
    // Longueur qu'un cordon peut parcourir dans de la matière déjà posée
    // (croisement de deux lignes, fermeture d'un contour), en largeurs de cordon.
    double tolerance_croisement = 2.0;
    // Hauteur contrôlée au-dessus de la pointe de la buse, en mm.
    double hauteur_controle_dessus = 5.0;
    // Nombre maximal d'erreurs détaillées gardées pour chaque type.
    int erreurs_max_par_type = 10;
};

struct Reglages {
    ReglagesMachine machine;
    ReglagesImpression impression;
    ReglagesVerification verification;
};

// Erreur dans un fichier de réglages, avec un message en français.
class ErreurReglages : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Description d'un réglage : c'est la seule liste des réglages du logiciel.
// Le fichier de réglages, l'interface et l'aide s'appuient tous dessus.
enum class TypeReglage { nombre, entier, booleen, choix, texte };

struct DescriptionReglage {
    std::string cle;      // nom dans le fichier de réglages
    std::string groupe;   // onglet de l'interface
    std::string libelle;  // nom affiché
    std::string unite;    // « mm », « mm/s », « °C »... ou vide
    std::string aide;     // explication en une phrase
    TypeReglage type = TypeReglage::nombre;
    double min = 0.0;         // bornes incluses, pour les nombres et les entiers
    double max = 0.0;
    bool facultatif = false;  // vrai : peut rester vide (valeur à mesurer)
    std::vector<std::string> choix;     // textes possibles, pour le type « choix »
    std::vector<double> valeurs_choix;  // valeur numérique de chaque choix

    // Valeur actuelle, sous forme de nombre (vide : non renseigné).
    std::function<std::optional<double>(const Reglages&)> lire;
    std::function<void(Reglages&, std::optional<double>)> ecrire;
    // Pour le type « texte » seulement (un chemin de fichier, par exemple).
    std::function<std::string(const Reglages&)> lire_texte;
    std::function<void(Reglages&, std::string)> ecrire_texte;
};

// Tous les réglages, dans l'ordre d'affichage.
const std::vector<DescriptionReglage>& description_reglages();

// Change un réglage à partir d'un texte (« 0,2 », « Y », vide pour effacer un
// réglage facultatif). Lève ErreurReglages si le nom ou la valeur est invalide.
void definir_reglage(Reglages& reglages, std::string_view cle, std::string_view valeur);

// Valeur d'un réglage sous forme de texte, comme dans le fichier.
std::string texte_reglage(const Reglages& reglages, const DescriptionReglage& description);

// Contrôle la cohérence d'ensemble (par exemple b_min plus petit que b_max).
void verifier_coherence(const Reglages& reglages);

// Lit un fichier de réglages. Les réglages absents gardent leur valeur par défaut.
Reglages lire_reglages(const std::filesystem::path& chemin);

// Lit des réglages déjà chargés en mémoire (utile pour les tests).
Reglages lire_reglages_texte(std::string_view texte);

// Texte complet d'un fichier de réglages, avec un commentaire par réglage.
std::string ecrire_reglages(const Reglages& reglages);

}  // namespace artprint
