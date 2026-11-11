// ArtPrint Slicer - licence AGPL v3
//
// Comment la matière est contrôlée :
//   Chaque extrusion est rejouée par petits pas. À chaque pas, on regarde
//   deux choses avant de déposer le cordon dans la grille de matière :
//     - « dans » : le cœur du nouveau cordon est-il déjà rempli ? Un court
//       passage est normal (deux lignes qui se croisent, un contour qui se
//       referme) ; au-delà de la tolérance, c'est une erreur.
//     - « sous » : y a-t-il de la matière juste au-dessus de la pointe, le
//       long de la buse ? Si oui, la buse devrait traverser la pièce.
//   Le cordon n'est ajouté à la grille qu'avec un peu de retard : sinon il
//   se « verrait » lui-même juste derrière la buse.
#include "verificateur.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <istream>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>

#include "collisions.hpp"
#include "gcode.hpp"
#include "machine.hpp"
#include "matiere.hpp"

namespace artprint {

namespace {

constexpr double epsilon = 1e-9;
// Pas de contrôle des déplacements à vide, en mm.
constexpr double pas_a_vide = 1.0;
// Nombre maximal de pas pour un seul mouvement (garde-fou).
constexpr double pas_max_par_mouvement = 2'000'000.0;

PositionMachine interpoler(const PositionMachine& a, const PositionMachine& b, double t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.b + (b.b - a.b) * t, a.c + (b.c - a.c) * t};
}

class Verificateur {
public:
    explicit Verificateur(const Reglages& reglages)
        : machine_(reglages.machine),
          verif_(reglages.verification),
          largeur_(reglages.impression.largeur_extrusion),
          hauteur_(std::min(reglages.impression.hauteur_couche,
                            reglages.impression.hauteur_premiere_couche)),
          grille_(std::min(verif_.pas_controle, hauteur_ / 2.0)) {
        if (machine_.modele_machine.empty()) {
            return;
        }
        // Forme réelle de l'imprimante : chargée une fois, puis gardée.
        try {
            modele_ = charger_modele(machine_.modele_machine);
            detecteur_ = std::make_unique<DetecteurCollisions>(*modele_, machine_);
        } catch (const std::exception& erreur) {
            signaler(TypeErreur::modele_machine, position_, erreur.what(),
                     "Vérifier le réglage « modele_machine », ou le vider pour trancher sans "
                     "contrôle des collisions.");
        }
    }

    void traiter_ligne(int numero, std::string_view texte) {
        ligne_ = numero;
        texte_ligne_ = texte;
        while (!texte_ligne_.empty() &&
               (texte_ligne_.back() == '\r' || texte_ligne_.back() == '\n')) {
            texte_ligne_.pop_back();
        }
        ++rapport_.lignes;

        LigneGcode ligne;
        try {
            ligne = lire_ligne_gcode(texte);
        } catch (const ErreurGcode& erreur) {
            signaler(TypeErreur::syntaxe, position_,
                     std::format("Ligne de G-code illisible : {}.", erreur.what()),
                     "Corriger ou supprimer cette ligne.");
            return;
        }

        if (ligne.lettre == 'G') {
            traiter_g(ligne);
        } else if (ligne.lettre == 'M') {
            if (ligne.numero == 82) {
                e_absolu_ = true;
            } else if (ligne.numero == 83) {
                e_absolu_ = false;
            }
        }
    }

    RapportVerification terminer() {
        vider_attente();
        return std::move(rapport_);
    }

private:
    // Un morceau de cordon qui attend d'être ajouté à la grille.
    struct Depot {
        Vec3 pointe;
        Vec3 direction;
        double distance;  // distance parcourue en extrusion à ce moment-là
    };

    void signaler(TypeErreur type, const PositionMachine& position, std::string probleme,
                  std::string solution) {
        ++rapport_.nombre_erreurs;
        if (++compte_par_type_[type] > verif_.erreurs_max_par_type) {
            return;
        }
        rapport_.erreurs.push_back(
            {type, ligne_, texte_ligne_, position, std::move(probleme), std::move(solution)});
    }

    void traiter_g(const LigneGcode& ligne) {
        switch (ligne.numero) {
            case 0:
            case 1:
                traiter_mouvement(ligne);
                break;
            case 2:
            case 3:
                signaler(TypeErreur::commande_non_geree, position_,
                         "Les arcs de cercle (G2 et G3) ne sont pas vérifiables.",
                         "Découper l'arc en petits segments droits (G1).");
                break;
            case 10:  // rétraction faite par le firmware
                retracte_firmware_ = true;
                ++rapport_.retractions;
                fin_de_trait();
                break;
            case 11:  // reprise faite par le firmware
                retracte_firmware_ = false;
                break;
            case 20:
                signaler(TypeErreur::commande_non_geree, position_,
                         "Les pouces (G20) ne sont pas pris en charge.",
                         "Écrire le G-code en millimètres (G21).");
                break;
            case 90:
                absolu_ = true;
                break;
            case 91:
                absolu_ = false;
                break;
            case 92:
                redefinir_position(ligne);
                break;
            default:
                break;  // G4, G21, G28... : sans effet sur les contrôles
        }
    }

    void redefinir_position(const LigneGcode& ligne) {
        if (const auto v = ligne.parametre('X')) position_.x = *v;
        if (const auto v = ligne.parametre('Y')) position_.y = *v;
        if (const auto v = ligne.parametre('Z')) position_.z = *v;
        if (const auto v = ligne.parametre('B')) position_.b = *v;
        if (const auto v = ligne.parametre('C')) position_.c = *v;
        if (const auto v = ligne.parametre('E')) e_ = *v;
    }

    bool est_retracte() const { return retracte_firmware_ || retrait_ > epsilon; }

    void traiter_mouvement(const LigneGcode& ligne) {
        PositionMachine cible = position_;
        auto lire_axe = [&](char nom, double& axe) {
            if (const auto v = ligne.parametre(nom)) {
                axe = absolu_ ? *v : axe + *v;
            }
        };
        lire_axe('X', cible.x);
        lire_axe('Y', cible.y);
        lire_axe('Z', cible.z);
        lire_axe('B', cible.b);
        lire_axe('C', cible.c);

        double delta_e = 0.0;
        if (const auto v = ligne.parametre('E')) {
            delta_e = e_absolu_ ? *v - e_ : *v;
            e_ += delta_e;
        }

        const bool bouge = std::abs(cible.x - position_.x) > epsilon ||
                           std::abs(cible.y - position_.y) > epsilon ||
                           std::abs(cible.z - position_.z) > epsilon ||
                           std::abs(cible.b - position_.b) > epsilon ||
                           std::abs(cible.c - position_.c) > epsilon;

        if (!bouge) {
            if (delta_e < -epsilon) {  // rétraction sur place
                retrait_ += -delta_e;
                ++rapport_.retractions;
                fin_de_trait();
            } else if (delta_e > epsilon) {  // reprise sur place
                retrait_ = std::max(0.0, retrait_ - delta_e);
            }
            return;
        }

        const bool extrusion = delta_e > epsilon;
        if (extrusion) {
            if (est_retracte()) {
                signaler(TypeErreur::extrusion_sans_reprise, position_,
                         "Extrusion alors que le filament est encore rétracté : le début "
                         "du cordon manquera.",
                         "Ajouter une reprise (G1 E positif, sans déplacement) avant cette "
                         "extrusion.");
                retrait_ = 0.0;
                retracte_firmware_ = false;
            }
            a_extrude_ = true;
            ++rapport_.extrusions;
        } else {
            // Avant la toute première extrusion, la buse est encore vide :
            // la rétraction n'est pas exigée.
            if (a_extrude_ && !est_retracte()) {
                signaler(TypeErreur::retraction_manquante, position_,
                         "Déplacement à vide sans rétraction : la buse va couler et "
                         "laisser des fils.",
                         "Ajouter une rétraction (G1 E négatif, sans déplacement) juste "
                         "avant ce déplacement.");
            }
            if (delta_e < -epsilon) {  // rétraction pendant le déplacement
                retrait_ += -delta_e;
                ++rapport_.retractions;
            }
            ++rapport_.deplacements_a_vide;
            fin_de_trait();
        }

        verifier_axes(cible);
        try {
            parcourir(cible, extrusion);
        } catch (const ErreurReglageManquant& erreur) {
            signaler(TypeErreur::reglage_manquant, cible, erreur.what(),
                     "Ajouter ce réglage dans le fichier de réglages.");
        }
        position_ = cible;
    }

    // Contrôles qui ne dépendent que des valeurs des axes B et C.
    void verifier_axes(const PositionMachine& cible) {
        const double tour = std::abs(cible.c - position_.c);
        if (tour > 180.0 + 1e-6) {
            const double court = angle_le_plus_proche(cible.c, position_.c);
            signaler(TypeErreur::c_chemin_long, cible,
                     std::format("L'axe C tourne de {:.1f}° d'un seul coup, alors que le "
                                 "chemin le plus court fait {:.1f}°.",
                                 tour, std::abs(court - position_.c)),
                     std::format("Écrire C{:.3f} à la place de C{:.3f} : c'est la même "
                                 "orientation du plateau.",
                                 court, cible.c));
        }
        verifier_course('X', cible.x, machine_.course_x_min, machine_.course_x_max, cible);
        verifier_course('Y', cible.y, machine_.course_y_min, machine_.course_y_max, cible);
        verifier_course('Z', cible.z, machine_.course_z_min, machine_.course_z_max, cible);
        if (cible.b < machine_.b_min - 1e-6 || cible.b > machine_.b_max + 1e-6) {
            signaler(TypeErreur::limite_b, cible,
                     std::format("L'axe B est à {:.2f}°, hors de ses butées ({:.1f}° à "
                                 "{:.1f}°).",
                                 cible.b, machine_.b_min, machine_.b_max),
                     "Réduire l'inclinaison de la tête à cet endroit.");
        }
        if ((machine_.c_min && cible.c < *machine_.c_min - 1e-6) ||
            (machine_.c_max && cible.c > *machine_.c_max + 1e-6)) {
            signaler(TypeErreur::limite_c, cible,
                     std::format("L'axe C est à {:.2f}°, hors de ses butées.", cible.c),
                     "Détordre le plateau : faire un déplacement à vide qui ramène C dans "
                     "ses limites.");
        }
    }

    // Vérifie qu'un axe linéaire reste dans sa course. Le mouvement est
    // droit : si le départ et l'arrivée sont dans la course, tout le trajet l'est.
    void verifier_course(char axe, double valeur, const std::optional<double>& min,
                         const std::optional<double>& max, const PositionMachine& cible) {
        if ((min && valeur < *min - 1e-6) || (max && valeur > *max + 1e-6)) {
            const std::string bas = min ? std::format("{:.1f}", *min) : "sans limite";
            const std::string haut = max ? std::format("{:.1f}", *max) : "sans limite";
            signaler(TypeErreur::limite_course, cible,
                     std::format("L'axe {} est demandé à {:.2f} mm, hors de sa course "
                                 "({} à {} mm) : la machine irait en butée.",
                                 axe, valeur, bas, haut),
                     "Déplacer, tourner ou réduire la pièce pour rester dans la course.");
        }
    }

    // Le modèle de machine est gros : il est gardé en mémoire d'un tranchage
    // à l'autre tant que le fichier ne change pas.
    static std::shared_ptr<const ModeleMachine> charger_modele(const std::string& chemin_utf8) {
        static std::mutex verrou;
        static std::string chemin_garde;
        static std::filesystem::file_time_type date_gardee;
        static std::shared_ptr<const ModeleMachine> garde;
        const std::filesystem::path chemin(std::u8string(chemin_utf8.begin(), chemin_utf8.end()));
        std::error_code erreur;
        const auto date = std::filesystem::last_write_time(chemin, erreur);
        const std::lock_guard<std::mutex> garde_verrou(verrou);
        if (!garde || chemin_garde != chemin_utf8 || erreur || date != date_gardee) {
            garde = std::make_shared<const ModeleMachine>(lire_modele_machine(chemin));
            chemin_garde = chemin_utf8;
            date_gardee = date;
        }
        return garde;
    }

    // Contrôle les collisions à cette position. Renvoie vrai si une erreur
    // a été signalée.
    bool controler_collision(const PositionMachine& ici) {
        if (!detecteur_) {
            return false;
        }
        std::optional<Contact> contact;
        try {
            contact = detecteur_->controler(ici);
        } catch (const ErreurReglageManquant&) {
            return false;  // déjà signalé par le contrôle de la pose
        }
        if (!contact) {
            return false;
        }
        const char* solution = "";
        TypeErreur type = TypeErreur::collision_machine;
        switch (contact->type) {
            case TypeContact::matiere:
                type = TypeErreur::collision_matiere;
                solution = "Imprimer cette zone plus tôt, réduire l'inclinaison de la tête, ou "
                           "faire tourner le plateau avant d'approcher.";
                break;
            case TypeContact::plateau:
                type = TypeErreur::collision_plateau;
                solution = "Remonter la trajectoire, ou réduire l'inclinaison de la tête près "
                           "du plateau.";
                break;
            case TypeContact::machine:
                solution = "Rapprocher la pièce du centre du plateau, la tourner ou la réduire.";
                break;
        }
        signaler(type, ici,
                 std::format("Collision : « {} » touche « {} » (distance {:.1f} mm, pour une "
                             "marge de sécurité de {:.1f} mm).",
                             contact->piece, contact->obstacle, contact->distance,
                             machine_.marge_securite),
                 solution);
        // Après trop de collisions, le contrôle s'arrête : le G-code est
        // déjà refusé, inutile de ralentir la suite.
        if (++collisions_ >= verif_.erreurs_max_par_type) {
            detecteur_.reset();
        }
        return true;
    }

    // Rejoue le mouvement par petits pas, de la position actuelle à la cible.
    void parcourir(const PositionMachine& cible, bool extrusion) {
        const PoseOutil depart = pose_dans_piece(machine_, position_);
        const PoseOutil arrivee = pose_dans_piece(machine_, cible);

        // Majorant du chemin parcouru par la pointe sur la pièce.
        const double droit = norme(Vec3{cible.x - position_.x, cible.y - position_.y,
                                        cible.z - position_.z});
        const double rayon = std::max(std::hypot(depart.pointe.x, depart.pointe.y),
                                      std::hypot(arrivee.pointe.x, arrivee.pointe.y));
        const double borne = droit + en_radians(std::abs(cible.c - position_.c)) * rayon +
                             en_radians(std::abs(cible.b - position_.b)) *
                                 machine_.distance_pivot_buse.value_or(0.0);
        const double pas = extrusion ? grille_.pas() : pas_a_vide;
        const int nombre =
            static_cast<int>(std::clamp(std::ceil(borne / pas), 1.0, pas_max_par_mouvement));

        bool b_signale = false;
        bool plateau_signale = false;
        bool volume_signale = false;
        bool dessus_signale = false;
        bool collision_signalee = false;
        Vec3 precedent = depart.pointe;

        for (int k = 1; k <= nombre; ++k) {
            const PositionMachine ici =
                interpoler(position_, cible, static_cast<double>(k) / nombre);
            const PoseOutil pose = pose_dans_piece(machine_, ici);
            const double avance = norme(pose.pointe - precedent);
            precedent = pose.pointe;
            (extrusion ? rapport_.longueur_extrusions : rapport_.longueur_a_vide) += avance;

            if (!plateau_signale && pose.pointe.z < -1e-6) {
                plateau_signale = true;
                signaler(TypeErreur::sous_plateau, ici,
                         std::format("La pointe de la buse descend à {:.3f} mm sous la "
                                     "surface du plateau.",
                                     -pose.pointe.z),
                         "Remonter la trajectoire, ou vérifier le réglage "
                         "« distance_pivot_buse ».");
            }
            if (!b_signale) {
                b_signale = verifier_inclinaison(ici, pose);
            }
            if (!collision_signalee) {
                collision_signalee = controler_collision(ici);
            }
            if (!extrusion) {
                continue;
            }

            if (!volume_signale &&
                (std::hypot(pose.pointe.x, pose.pointe.y) > machine_.rayon_plateau + 1e-6 ||
                 pose.pointe.z > machine_.hauteur_impression + 1e-6)) {
                volume_signale = true;
                signaler(TypeErreur::hors_volume, ici,
                         std::format("Extrusion en dehors du volume d'impression (plateau "
                                     "de rayon {:.0f} mm, hauteur {:.0f} mm).",
                                     machine_.rayon_plateau, machine_.hauteur_impression),
                         "Recentrer ou réduire la pièce.");
            }

            controler_matiere(ici, pose, avance, dessus_signale);

            distance_extrusion_ += avance;
            attente_.push_back({pose.pointe, pose.direction, distance_extrusion_});
            const double retard = 3.0 * largeur_;
            while (!attente_.empty() && attente_.front().distance < distance_extrusion_ - retard) {
                deposer(attente_.front());
                attente_.pop_front();
            }
        }
    }

    // Vérifie l'inclinaison de B selon la hauteur. Renvoie vrai si une erreur
    // a été signalée.
    bool verifier_inclinaison(const PositionMachine& ici, const PoseOutil& pose) {
        const bool haut = machine_.hauteur_bascule_b && pose.pointe.z >= *machine_.hauteur_bascule_b;
        if (haut || std::abs(ici.b) <= machine_.b_limite_pres_plateau + 1e-6) {
            return false;
        }
        if (machine_.hauteur_bascule_b) {
            signaler(TypeErreur::limite_b, ici,
                     std::format("La tête est inclinée de {:.2f}° à {:.2f} mm du plateau. "
                                 "Sous {:.2f} mm, la limite est de {:.1f}°.",
                                 ici.b, pose.pointe.z, *machine_.hauteur_bascule_b,
                                 machine_.b_limite_pres_plateau),
                     "Réduire l'inclinaison près du plateau, ou imprimer cette zone plus haut.");
        } else {
            signaler(TypeErreur::limite_b, ici,
                     std::format("La tête est inclinée de {:.2f}°. La limite est de {:.1f}° "
                                 "partout tant que le réglage « hauteur_bascule_b » n'est pas "
                                 "renseigné.",
                                 ici.b, machine_.b_limite_pres_plateau),
                     "Mesurer la hauteur de bascule sur la machine et l'ajouter aux "
                     "réglages, ou réduire l'inclinaison.");
        }
        return true;
    }

    void controler_matiere(const PositionMachine& ici, const PoseOutil& pose, double avance,
                           bool& dessus_signale) {
        const double hauteur = hauteur_;

        // « Dans » : le cœur du cordon, un quart de couche sous la pointe.
        if (grille_.occupe(pose.pointe - pose.direction * (hauteur / 4.0))) {
            longueur_dans_ += avance;
            if (!dans_signale_ &&
                longueur_dans_ > verif_.tolerance_croisement * largeur_) {
                dans_signale_ = true;
                signaler(TypeErreur::dans_matiere, ici,
                         std::format("Le cordon est imprimé dans de la matière déjà déposée "
                                     "sur {:.2f} mm au moins.",
                                     longueur_dans_),
                         "Supprimer ce passage en double, ou décaler cette trajectoire "
                         "d'une couche.");
            }
        } else {
            longueur_dans_ = 0.0;
            dans_signale_ = false;
        }

        // « Sous » : de la matière au-dessus de la pointe, le long de la buse.
        if (dessus_signale) {
            return;
        }
        const double pas = grille_.pas();
        for (double t = pas; t <= verif_.hauteur_controle_dessus; t += pas) {
            if (grille_.occupe(pose.pointe + pose.direction * t)) {
                dessus_signale = true;
                signaler(TypeErreur::sous_matiere, ici,
                         std::format("Le cordon est imprimé sous de la matière déjà déposée "
                                     "({:.2f} mm au-dessus de la pointe) : la buse devrait "
                                     "traverser la pièce.",
                                     t),
                         "Imprimer cette zone avant celle du dessus, ou changer "
                         "l'orientation de la buse.");
                return;
            }
        }
    }

    void deposer(const Depot& depot) {
        grille_.deposer(depot.pointe, depot.direction, largeur_,
                        hauteur_);
        if (detecteur_) {
            detecteur_->deposer(depot.pointe - depot.direction * (hauteur_ / 2.0));
        }
    }

    void vider_attente() {
        for (const Depot& depot : attente_) {
            deposer(depot);
        }
        attente_.clear();
    }

    // Fin d'un trait continu d'extrusion (déplacement à vide ou rétraction).
    void fin_de_trait() {
        vider_attente();
        longueur_dans_ = 0.0;
        dans_signale_ = false;
    }

    const ReglagesMachine machine_;
    const ReglagesVerification verif_;
    const double largeur_;  // largeur nominale du cordon
    const double hauteur_;  // hauteur nominale du cordon
    GrilleMatiere grille_;
    RapportVerification rapport_;
    std::map<TypeErreur, int> compte_par_type_;

    int ligne_ = 0;
    std::string texte_ligne_;

    PositionMachine position_;
    double e_ = 0.0;
    bool absolu_ = true;    // G90 / G91
    bool e_absolu_ = true;  // M82 / M83
    double retrait_ = 0.0;  // longueur de filament actuellement rétractée
    bool retracte_firmware_ = false;
    bool a_extrude_ = false;

    std::shared_ptr<const ModeleMachine> modele_;
    std::unique_ptr<DetecteurCollisions> detecteur_;
    int collisions_ = 0;

    std::deque<Depot> attente_;
    double distance_extrusion_ = 0.0;
    double longueur_dans_ = 0.0;
    bool dans_signale_ = false;
};

}  // namespace

bool RapportVerification::contient(TypeErreur type) const {
    return std::any_of(erreurs.begin(), erreurs.end(),
                       [type](const ErreurVerification& e) { return e.type == type; });
}

RapportVerification verifier_gcode(std::istream& gcode, const Reglages& reglages) {
    Verificateur verificateur(reglages);
    std::string ligne;
    int numero = 0;
    while (std::getline(gcode, ligne)) {
        verificateur.traiter_ligne(++numero, ligne);
    }
    return verificateur.terminer();
}

RapportVerification verifier_gcode_texte(std::string_view gcode, const Reglages& reglages) {
    std::istringstream flux{std::string(gcode)};
    return verifier_gcode(flux, reglages);
}

std::string decrire(const ErreurVerification& erreur) {
    const PositionMachine& p = erreur.position;
    return std::format(
        "Ligne {} : {}\n"
        "  Problème : {}\n"
        "  Position de la machine : X={:.3f} Y={:.3f} Z={:.3f} B={:.3f}° C={:.3f}°\n"
        "  Solution possible : {}\n",
        erreur.ligne, erreur.texte_ligne, erreur.probleme, p.x, p.y, p.z, p.b, p.c,
        erreur.solution);
}

}  // namespace artprint
