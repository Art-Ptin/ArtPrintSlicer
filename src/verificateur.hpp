// ArtPrint Slicer - licence AGPL v3
// Vérificateur de G-code : relit un fichier ligne par ligne, rejoue les
// mouvements de la machine et refuse tout ce qui viole les règles physiques
// du projet :
//   - jamais de cordon imprimé dans ou sous une matière déjà imprimée ;
//   - une rétraction avant chaque déplacement à vide ;
//   - C passe toujours par le chemin le plus court ;
//   - les courses de X, Y, Z, les limites de B et C et le volume d'impression
//     sont respectés.
//
// Si le réglage « modele_machine » est renseigné, chaque mouvement est aussi
// contrôlé contre la vraie forme de l'imprimante (voir collisions.hpp).
#pragma once

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cinematique.hpp"
#include "reglages.hpp"

namespace artprint {

enum class TypeErreur {
    syntaxe,                 // ligne de G-code illisible
    commande_non_geree,      // mouvement que le vérificateur ne sait pas rejouer
    reglage_manquant,        // réglage machine indispensable absent
    retraction_manquante,    // déplacement à vide sans rétraction avant
    extrusion_sans_reprise,  // extrusion alors que le filament est encore rétracté
    c_chemin_long,           // C tourne de plus d'un demi-tour en un mouvement
    limite_course,           // X, Y ou Z sort de la course de la machine
    limite_b,                // B dépasse sa butée ou sa limite près du plateau
    limite_c,                // C dépasse sa butée
    sous_plateau,            // la pointe de la buse passe sous le plateau
    hors_volume,             // extrusion en dehors du volume d'impression
    dans_matiere,            // cordon imprimé dans de la matière déjà posée
    sous_matiere,            // cordon imprimé sous de la matière déjà posée
    collision_matiere,       // la tête ou le bras touche la matière déjà imprimée
    collision_plateau,       // la tête ou le bras touche le plateau
    collision_machine,       // la tête, le bras ou le module Z touche la machine
    modele_machine,          // le modèle de machine est illisible ou incohérent
};

struct ErreurVerification {
    TypeErreur type{};
    int ligne = 0;             // numéro de la ligne de G-code (la première vaut 1)
    std::string texte_ligne;   // la ligne telle qu'elle est écrite dans le fichier
    PositionMachine position;  // position de la machine au moment du problème
    std::string probleme;      // explication en français
    std::string solution;      // une solution possible
};

struct RapportVerification {
    // Erreurs détaillées (limitées en nombre pour chaque type).
    std::vector<ErreurVerification> erreurs;
    // Nombre total d'erreurs trouvées, y compris celles non détaillées.
    std::size_t nombre_erreurs = 0;

    std::size_t lignes = 0;
    std::size_t extrusions = 0;           // mouvements qui déposent de la matière
    std::size_t deplacements_a_vide = 0;  // mouvements sans dépôt
    // Extrusions pendant lesquelles B ou C tourne (5 axes continu).
    std::size_t extrusions_en_rotation = 0;
    std::size_t retractions = 0;
    double longueur_extrusions = 0.0;     // en mm, mesurée sur la pièce
    // Longueur de paroi déposée sans rien dessous (ni plateau, ni matière),
    // en mm. Ce n'est pas une erreur : c'est la mesure de ce qui demanderait
    // des supports. Le remplissage n'est pas compté ; un pont l'est.
    double longueur_dans_le_vide = 0.0;
    double longueur_a_vide = 0.0;

    bool valide() const { return nombre_erreurs == 0; }
    bool contient(TypeErreur type) const;
};

RapportVerification verifier_gcode(std::istream& gcode, const Reglages& reglages);

// Même chose pour un G-code déjà chargé en mémoire (utile pour les tests).
RapportVerification verifier_gcode_texte(std::string_view gcode, const Reglages& reglages);

// Vérification « au fur et à mesure » : le G-code est donné morceau par
// morceau, dans l'ordre. Une copie de l'objet garde tout ce qui a été lu
// (la matière déposée, la position de la machine) : elle permet d'essayer
// plusieurs suites possibles depuis le même point et de garder la meilleure.
class VerificationSuivie {
public:
    explicit VerificationSuivie(const Reglages& reglages);
    VerificationSuivie(const VerificationSuivie& autre);
    VerificationSuivie(VerificationSuivie&&) noexcept;
    VerificationSuivie& operator=(VerificationSuivie&&) noexcept;
    ~VerificationSuivie();

    // Lit des lignes entières de G-code. S'arrête dès que le nombre total
    // d'erreurs dépasse « erreurs_max ». Rend le nombre de lignes lues.
    std::size_t lire(std::string_view texte, std::size_t erreurs_max = static_cast<std::size_t>(-1));
    // Nombre d'erreurs trouvées jusqu'ici.
    std::size_t erreurs() const;
    // Le rapport complet. À n'appeler qu'une fois, à la fin.
    RapportVerification terminer();

private:
    struct Interne;
    std::unique_ptr<Interne> interne_;
};

// Texte complet d'une erreur, prêt à afficher : ligne, problème, position
// de la machine et solution possible.
std::string decrire(const ErreurVerification& erreur);

}  // namespace artprint
