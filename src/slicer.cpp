// ArtPrint Slicer - licence AGPL v3
#include "slicer.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <vector>

#include "ecriture_gcode.hpp"
#include "tranchage.hpp"
#include "trajectoires.hpp"
#include "version.hpp"

namespace artprint {

namespace {

// Écrit les lignes de G-code et se souvient de l'état de la machine :
// position, filament rétracté ou non.
class EcrivainGcode {
public:
    EcrivainGcode(const Reglages& reglages)
        : machine_(reglages.machine), r_(reglages.impression) {}

    void ligne(std::string_view texte) {
        gcode_ += texte;
        gcode_ += '\n';
    }

    // Change de couche : la buse monte à la nouvelle hauteur au prochain
    // déplacement.
    void couche(double z, double hauteur) {
        z_ = z;
        hauteur_ = hauteur;
        apercu_.push_back({z, 0, {}});
        // Section du cordon : un rectangle aux bouts arrondis.
        const double section = (r_.largeur_extrusion - hauteur) * hauteur +
                               std::numbers::pi * hauteur * hauteur / 4.0;
        const double section_filament =
            std::numbers::pi * r_.diametre_filament * r_.diametre_filament / 4.0;
        e_par_mm_ = section / section_filament * r_.debit;
    }

    // Déplacement à vide jusqu'au point (repère de la pièce) : rétraction,
    // petite montée, trajet, redescente.
    void aller_a(const Point2& p) {
        if (place_ && distance(position_, p) < 1e-4 && std::abs(z_machine_ - z_) < 1e-6) {
            return;  // déjà sur place
        }
        // Avant la toute première extrusion, la buse est vide : rien à rétracter.
        if (!retracte_ && filament_ > 0.0) {
            ligne(std::format("G1 E-{:.3f} F{:.0f}", r_.retraction_longueur,
                              r_.retraction_vitesse * 60.0));
            retracte_ = true;
            duree_ += r_.retraction_longueur / r_.retraction_vitesse;
        }
        const double vitesse = r_.vitesse_a_vide * 60.0;
        const double z_trajet = std::max(z_machine_, z_) + r_.levee_z;
        if (place_ && std::abs(z_trajet - z_machine_) > 1e-6) {
            ligne(std::format("G0 {} F{:.0f}", axes(position_, z_trajet), vitesse));
        }
        if (place_) {
            // Aperçu : montée, trajet, descente.
            noter(position_, z_machine_, position_, z_trajet, type_apercu_vide);
            noter(position_, z_trajet, p, z_trajet, type_apercu_vide);
            noter(p, z_trajet, p, z_, type_apercu_vide);
        }
        if (!place_) {
            // Premier déplacement : tous les axes sont écrits.
            // Buse verticale : B prend la valeur « verticale » de la machine.
            ligne(std::format("G0 {} F{:.0f}", axes(p, z_trajet), vitesse));
        } else {
            ligne(std::format("G0 {} F{:.0f}", axes(p, z_trajet), vitesse));
            duree_ += distance(position_, p) / r_.vitesse_a_vide;
        }
        if (std::abs(z_trajet - z_) > 1e-6) {
            ligne(std::format("G0 {} F{:.0f}", axes(p, z_), vitesse));
        }
        z_machine_ = z_;
        position_ = p;
        place_ = true;
    }

    // Largeur du cordon du trait qui commence (0 : celle des réglages) : la
    // quantité de matière suit la section du cordon.
    void largeur_du_trait(double largeur) {
        const auto section = [&](double l) {
            return std::max(l - hauteur_, 0.0) * hauteur_ + std::numbers::pi * hauteur_ * hauteur_ / 4.0;
        };
        debit_du_trait_ = largeur > 0.0 ? section(largeur) / section(r_.largeur_extrusion) : 1.0;
    }

    // Extrusion en ligne droite jusqu'au point, à la vitesse donnée (mm/s).
    void extruder_vers(const Point2& p, double vitesse) {
        const double longueur = distance(position_, p);
        if (longueur < 1e-4) {
            return;
        }
        if (retracte_) {
            ligne(std::format("G1 E{:.3f} F{:.0f}", r_.retraction_longueur,
                              r_.retraction_vitesse * 60.0));
            retracte_ = false;
            duree_ += r_.retraction_longueur / r_.retraction_vitesse;
        }
        const double e = longueur * e_par_mm_ * debit_du_trait_;
        if (e < 5e-6) {
            return;  // segment trop court pour porter de la matière : il est joint au suivant
        }
        ligne(std::format("G1 {} E{:.5f} F{:.0f}", axes(p, z_), e, vitesse * 60.0));
        noter(position_, z_, p, z_, type_);
        filament_ += e;
        duree_ += longueur / vitesse;
        position_ = p;
    }

    // Fin d'impression : rétraction et dégagement vers le haut.
    void degager(double hauteur) {
        if (!retracte_) {
            ligne(std::format("G1 E-{:.3f} F{:.0f}", r_.retraction_longueur,
                              r_.retraction_vitesse * 60.0));
            retracte_ = true;
        }
        const double z = std::min(z_machine_ + hauteur, machine_.hauteur_impression);
        ligne(std::format("G0 {} F{:.0f}", axes(position_, z), r_.vitesse_a_vide * 60.0));
        z_machine_ = z;
    }

    // Les cinq axes d'une ligne de mouvement : chaque ligne les porte tous.
    // En 3 axes la buse reste verticale et le plateau ne tourne pas.
    std::string axes(const Point2& p, double z) const {
        return std::format("X{:.3f} Y{:.3f} Z{:.3f} B{:.3f} C{:.3f}", x_machine(p), y_machine(p), z,
                           machine_.b_vertical, 0.0);
    }

    // Type des prochaines extrusions, pour l'aperçu.
    void type(TypeTrait t) { type_ = static_cast<std::uint8_t>(t); }
    std::vector<CoucheApercu> prendre_apercu() { return std::move(apercu_); }

    const Point2& position() const { return position_; }
    bool place() const { return place_; }
    double filament() const { return filament_; }
    double duree() const { return duree_; }
    std::string prendre_gcode() { return std::move(gcode_); }

private:
    void noter(const Point2& a, double za, const Point2& b, double zb, std::uint8_t type) {
        if (apercu_.empty() || (distance(a, b) < 1e-6 && std::abs(za - zb) < 1e-6)) {
            return;
        }
        apercu_.back().mouvements.push_back(
            {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(za),
             static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(zb), type});
    }

    double x_machine(const Point2& p) const { return p.x + machine_.centre_plateau_x; }
    double y_machine(const Point2& p) const { return p.y + machine_.centre_plateau_y; }

    const ReglagesMachine machine_;
    const ReglagesImpression r_;
    std::string gcode_;
    Point2 position_;        // dans le repère de la pièce
    bool place_ = false;     // faux tant que la buse n'a pas été positionnée
    double z_ = 0.0;         // hauteur de la couche en cours
    double z_machine_ = 0.0; // hauteur réelle de la buse
    double e_par_mm_ = 0.0;
    double hauteur_ = 0.2;          // épaisseur de la couche en cours
    double debit_du_trait_ = 1.0;   // part de matière du trait en cours (cordon plus étroit ou plus large)
    bool retracte_ = false;
    double filament_ = 0.0;
    double duree_ = 0.0;
    std::uint8_t type_ = 0;
    std::vector<CoucheApercu> apercu_;
};

}  // namespace

Maillage poser_au_centre(const Maillage& maillage) {
    const BoiteEnglobante boite = calculer_boite(maillage);
    Maillage pose = maillage;
    for (Point3& sommet : pose.sommets) {
        sommet.x -= (boite.min.x + boite.max.x) / 2.0f;
        sommet.y -= (boite.min.y + boite.max.y) / 2.0f;
        sommet.z -= boite.min.z;
    }
    return pose;
}

ResultatTranchage trancher(const Maillage& maillage, const Reglages& reglages, bool deja_pose, bool verifier) {
    const ReglagesImpression& r = reglages.impression;
    const ReglagesMachine& machine = reglages.machine;

    if (maillage.triangles.empty()) {
        throw ErreurTranchage("Le modèle ne contient aucun triangle.");
    }
    const RapportFermeture fermeture = verifier_fermeture(maillage);
    if (!fermeture.ferme || !fermeture.orientation_coherente) {
        throw ErreurTranchage("Le modèle n'est pas un volume fermé : " + fermeture.message +
                              " Il faut le réparer avant de le trancher.");
    }
    if (r.hauteur_couche >= r.largeur_extrusion ||
        r.hauteur_premiere_couche >= r.largeur_extrusion) {
        throw ErreurTranchage(
            "La hauteur de couche doit être plus petite que la largeur d'extrusion.");
    }

    // Pose le modèle au centre du plateau, le dessous à la hauteur zéro.
    const BoiteEnglobante boite = calculer_boite(maillage);
    const Point3 taille = boite.dimensions();
    const Maillage pose = deja_pose ? maillage : poser_au_centre(maillage);
    double rayon = 0.0;
    for (const Point3& sommet : pose.sommets) {
        rayon = std::max(rayon, std::hypot(static_cast<double>(sommet.x), static_cast<double>(sommet.y)));
    }
    if (rayon > machine.rayon_plateau + 1e-3) {
        throw ErreurTranchage(std::format(
            "Le modèle dépasse du plateau : il s'étend à {:.1f} mm du centre, pour un plateau "
            "de rayon {:.0f} mm. Il faut le réduire ou le tourner.",
            rayon, machine.rayon_plateau));
    }
    if (taille.z > machine.hauteur_impression) {
        throw ErreurTranchage(std::format(
            "Le modèle est trop haut : {:.1f} mm, pour une hauteur d'impression de {:.0f} mm.",
            taille.z, machine.hauteur_impression));
    }

    // Hauteurs de coupe : au milieu de chaque couche.
    std::vector<double> hauteurs;
    for (double bas = 0.0, epaisseur = r.hauteur_premiere_couche;
         bas + epaisseur / 2.0 < taille.z; bas += epaisseur, epaisseur = r.hauteur_couche) {
        hauteurs.push_back(bas + epaisseur / 2.0);
    }
    if (hauteurs.empty()) {
        throw ErreurTranchage("Le modèle est plus fin qu'une couche : rien à imprimer.");
    }

    ResultatCoupe coupe = couper_maillage(pose, hauteurs);
    if (coupe.contours_ouverts > 0) {
        throw ErreurTranchage(std::format(
            "Le modèle est abîmé : {} contour(s) ne se referment pas pendant la coupe. "
            "Il faut le réparer avant de le trancher.",
            coupe.contours_ouverts));
    }
    // Retire les couches vides du haut (pointe trop fine pour être imprimée).
    while (!coupe.couches.empty() && coupe.couches.back().empty()) {
        coupe.couches.pop_back();
    }
    // Une couche vide au milieu : ce qui est au-dessus serait imprimé dans le vide.
    for (std::size_t c = 0; c < coupe.couches.size(); ++c) {
        if (coupe.couches[c].empty()) {
            throw ErreurTranchage(std::format(
                "La couche {} (à {:.2f} mm) est vide : une partie du modèle flotte en l'air "
                "et ne peut pas être imprimée.",
                c + 1, hauteurs[c]));
        }
    }
    std::vector<CoucheTrajectoires> couches = calculer_trajectoires(coupe.couches, r);
    if (couches.empty() || couches.front().traits.empty()) {
        throw ErreurTranchage(
            "La première couche est vide : le modèle ne touche pas assez le plateau.");
    }

    EcrivainGcode ecrivain(reglages);
    ecrire_entete(ecrivain, r, couches.size(), machine.contrat_gcode);

    for (std::size_t c = 0; c < couches.size(); ++c) {
        CoucheTrajectoires& couche = couches[c];
        ecrivain.ligne(std::format(";COUCHE:{}", c + 1));
        ecrivain.ligne(std::format(";Z:{:.3f}", couche.z));
        if (c == 1 && r.ventilation) {
            ecrivain.ligne("M106 S255 ; ventilateur de pièce");
        }
        ecrivain.couche(couche.z, couche.hauteur);

        // Les traits arrivent groupés par type : chaque groupe est imprimé
        // en entier avant le suivant.
        for (std::size_t debut = 0; debut < couche.traits.size();) {
            std::size_t fin = debut;
            while (fin < couche.traits.size() &&
                   couche.traits[fin].type == couche.traits[debut].type) {
                ++fin;
            }
            const TypeTrait type = couche.traits[debut].type;
            ecrivain.ligne(std::format(";TYPE:{}", nom_type(type)));
            ecrivain.type(type);
            ecrire_groupe(ecrivain, couche.traits, debut, fin,
                          vitesse_du_type(r, type, c == 0));
            debut = fin;
        }
    }

    ecrire_fin(ecrivain, r, machine.contrat_gcode);

    ResultatTranchage resultat;
    resultat.couches = couches.size();
    resultat.longueur_filament = ecrivain.filament();
    resultat.duree_estimee = ecrivain.duree();
    resultat.apercu = ecrivain.prendre_apercu();
    resultat.gcode = ecrivain.prendre_gcode();
    if (verifier) {
        resultat.verification = verifier_gcode_texte(resultat.gcode, reglages);
    }
    return resultat;
}

}  // namespace artprint
