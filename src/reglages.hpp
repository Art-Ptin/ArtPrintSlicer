// ArtPrint Slicer - licence AGPL v3
// Réglages de la machine et du vérificateur. Rien de ce qui dépend de
// l'imprimante n'est écrit en dur ailleurs dans le code : tout passe par ici.
//
// Le fichier de réglages est un simple texte, une ligne par réglage :
//   # commentaire
//   distance_pivot_buse = 12,5
#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace artprint {

// Axe horizontal autour duquel la tête s'incline (axe B).
enum class AxePivot { x, y };

struct ReglagesMachine {
    // Plateau rond et hauteur d'impression, en millimètres.
    double rayon_plateau = 150.0;
    double hauteur_impression = 281.0;

    // Distance entre le pivot B et la pointe de la buse. À MESURER sur la
    // machine : il n'y a volontairement aucune valeur par défaut.
    std::optional<double> distance_pivot_buse;

    // Géométrie des axes. Valeurs par défaut à confirmer sur la machine.
    AxePivot axe_pivot_b = AxePivot::y;
    int sens_b = 1;  // 1 ou -1 : sens d'inclinaison pour un B positif
    int sens_c = 1;  // 1 ou -1 : sens de rotation du plateau pour un C positif
    double centre_plateau_x = 0.0;  // position X, Y machine du centre du plateau
    double centre_plateau_y = 0.0;

    // Butées de l'axe B, en degrés.
    double b_min = -90.0;
    double b_max = 90.0;
    // Inclinaison maximale près du plateau, en degrés.
    double b_limite_pres_plateau = 55.0;
    // Hauteur au-dessus de laquelle B peut dépasser la limite précédente.
    // À MESURER : tant qu'elle manque, la limite basse s'applique partout.
    std::optional<double> hauteur_bascule_b;

    // Butées de l'axe C, en degrés. Absentes : le plateau tourne sans limite.
    std::optional<double> c_min;
    std::optional<double> c_max;
};

struct ReglagesVerification {
    // Taille nominale du cordon déposé, en millimètres.
    double largeur_extrusion = 0.45;
    double hauteur_couche = 0.2;
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
    ReglagesVerification verification;
};

// Erreur dans un fichier de réglages, avec un message en français.
class ErreurReglages : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Lit un fichier de réglages. Les réglages absents gardent leur valeur par défaut.
Reglages lire_reglages(const std::filesystem::path& chemin);

// Lit des réglages déjà chargés en mémoire (utile pour les tests).
Reglages lire_reglages_texte(std::string_view texte);

}  // namespace artprint
