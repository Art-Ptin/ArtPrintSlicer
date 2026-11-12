// ArtPrint Slicer - licence AGPL v3
#include "multiaxe.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#include "champ_couches.hpp"
#include "cinematique.hpp"
#include "couches_courbes.hpp"
#include "decoupe.hpp"
#include "ecriture_gcode.hpp"
#include "peau_courbe.hpp"
#include "tranchage.hpp"
#include "trajectoires.hpp"

namespace artprint {

namespace {

// Le plan d'une couche d'une partie courbe, et celui de la couche du dessous.
struct RepereCouche {
    Vec3 origine, u, v, w;
    Vec3 origine_dessous, normale_dessous;
};

// Dans une section, ne garde que la matière qui entoure l'axe de la partie
// (le point 0, 0) : le plan peut aussi couper un autre endroit du modèle.
Polygones autour_de_l_axe(const Polygones& section) {
    const PointEntier centre{0, 0};
    const Contour* exterieur = nullptr;
    double plus_proche = std::numeric_limits<double>::max();
    for (const Contour& contour : section) {
        if (!Clipper2Lib::IsPositive(contour)) {
            continue;
        }
        if (Clipper2Lib::PointInPolygon(centre, contour) != Clipper2Lib::PointInPolygonResult::IsOutside) {
            exterieur = &contour;
            plus_proche = 0.0;
            break;
        }
        // Sinon : le contour dont un sommet est le plus près de l'axe (à 3 mm au plus).
        for (const PointEntier& sommet : contour) {
            const double d = std::hypot(static_cast<double>(sommet.x), static_cast<double>(sommet.y)) / unites_par_mm;
            if (d < plus_proche && d < 3.0) {
                plus_proche = d;
                exterieur = &contour;
            }
        }
    }
    Polygones garde;
    if (!exterieur) {
        return garde;
    }
    garde.push_back(*exterieur);
    // Ses trous : les contours négatifs situés dedans.
    for (const Contour& contour : section) {
        if (!Clipper2Lib::IsPositive(contour) && !contour.empty() &&
            Clipper2Lib::PointInPolygon(contour.front(), *exterieur) != Clipper2Lib::PointInPolygonResult::IsOutside) {
            garde.push_back(contour);
        }
    }
    return garde;
}

// Surface sur laquelle la buse se pose pendant une couche qui n'est pas plane.
struct Surface {
    virtual ~Surface() = default;
    // Point de la surface à l'aplomb du point de la carte. « hauteur » :
    // distance entre la carte et la surface (une première idée en entrée).
    virtual bool poser(const Point2& p, double& hauteur, Vec3& point) const = 0;
    // Normale à la surface, et vitesse à laquelle les couches s'empilent le
    // long de cette normale (1 : épaisseur nominale).
    virtual bool normale(const Vec3& ici, Vec3& direction, double& pente) const = 0;
    // Vrai si ce point de l'espace est libre (hors de la pièce finie).
    virtual bool libre(const Vec3&) const { return true; }
};

// Une couche courbe : une surface du champ de couches.
struct SurfaceChamp final : Surface {
    SurfaceChamp(const ChampCouches& champ, const CoucheCourbe& carte, double niveau)
        : champ_(champ), carte_(carte), niveau_(niveau) {}
    bool poser(const Point2& p, double& hauteur, Vec3& point) const override {
        return poser_sur_la_surface(champ_, carte_, p, niveau_, hauteur, point);
    }
    bool normale(const Vec3& ici, Vec3& direction, double& pente) const override {
        return champ_.gradient(ici, direction, pente);
    }
    const ChampCouches& champ_;
    const CoucheCourbe& carte_;
    const double niveau_;
};

// La peau courbe : la surface du dessus de la pièce, vue de dessus.
struct SurfacePeau final : Surface {
    explicit SurfacePeau(const PeauCourbe& peau) : peau_(peau) {}
    bool poser(const Point2& p, double& hauteur, Vec3& point) const override {
        Vec3 normale;
        if (!peau_.poser(p, point, normale)) {
            return false;
        }
        hauteur = point.z;
        return true;
    }
    bool normale(const Vec3& ici, Vec3& direction, double& pente) const override {
        Vec3 point;
        pente = 1.0;
        return peau_.poser({ici.x, ici.y}, point, direction);
    }
    bool libre(const Vec3& point) const override { return !peau_.dans_la_matiere(point); }
    const PeauCourbe& peau_;
};

// Écrit le G-code 5 axes. Les trajectoires d'une partie sont données dans
// son repère (x, y dans le plan des couches, z le long de sa direction) ;
// l'écrivain calcule la position des cinq axes de la machine.
class EcrivainCinqAxes {
public:
    EcrivainCinqAxes(const Reglages& reglages, double hauteur_sure, const OptionsMultiaxe& options)
        : machine_(reglages.machine), r_(reglages.impression), options_(options),
          hauteur_sure_(hauteur_sure) {}

    void ligne(std::string_view texte) {
        gcode_ += texte;
        gcode_ += '\n';
    }

    // Commence une partie : son repère est (origine, u, v, w), w étant la
    // direction d'impression. La tête ne bouge qu'au prochain déplacement.
    // « cote » : côté vers lequel la tête penche (0 : le plus proche).
    // « numero » : numéro de la partie pour l'aperçu (-1 : la suivante).
    void partie(const Vec3& origine, const Vec3& u, const Vec3& v, const Vec3& w, int cote, int numero = -1) {
        origine_ = origine, u_ = u, v_ = v, w_ = w;
        outil_ = direction_outil(w);
        cote_ = cote;
        oriente_ = false;
        partie_ = numero >= 0 ? numero : partie_ + 1;
        surface_ = nullptr;
        limite_partie_ = 90.0;
    }

    // Plus grande inclinaison de la tête pour la partie en cours, en degrés.
    void limiter_inclinaison(double maximum) { limite_partie_ = maximum; }

    // Couches courbes : tronc et branches montent ensemble. La hauteur sûre
    // pour changer d'orientation suit alors ce qui est déjà imprimé.
    void hauteur_sure_suivie() { suivie_ = true; }

    // La surface n'est plus disponible : retour aux couches planes.
    void quitter_la_surface() { surface_ = nullptr; }

    // Couche courbe : la couche en cours est une surface, dessinée sur la
    // carte donnée par partie() ou pivoter(). « hauteur_trajet » :
    // hauteur, au-dessus de la carte, du point le plus haut de la couche ;
    // « z_min » : son point le plus bas au-dessus du plateau.
    // « libre » : en chaque point, la tête se met d'aplomb sur la surface
    // quelle que soit la direction de la pente (B et C changent tous les
    // deux). Sinon, elle penche toujours dans le sens de la carte : seul B change.
    void couche_courbe(const Surface& surface, double epaisseur, double hauteur_trajet, double z_min,
                       bool libre = false) {
        surface_ = &surface;
        libre_ = libre;
        h_nominale_ = epaisseur;
        hauteur_trajet_ = hauteur_trajet + r_.levee_z;
        hauteur_surface_ = 0.0;
        variable_ = false;
        continu_ = a_preparer_ = false;
        outil_ = direction_outil(incliner_au_plus(w_, limite_inclinaison(z_min)));
    }

    // Fait pivoter le repère pour la couche suivante d'une partie courbe.
    // Le changement est petit : la tête se réoriente pendant le trajet vers
    // la nouvelle couche, sans remonter au-dessus de la pièce.
    void pivoter(const Vec3& origine, const Vec3& u, const Vec3& v, const Vec3& w) {
        origine_ = origine, u_ = u, v_ = v, w_ = w;
        outil_ = direction_outil(w);
        pivot_ = oriente_;  // au tout début d'une partie, c'est le grand changement qui sert
    }

    // Entre deux couches qui pivotent, l'épaisseur n'est pas la même partout :
    // elle est mesurée, en chaque point, depuis le plan de la couche du dessous.
    void epaisseur_depuis(const Vec3& origine_dessous, const Vec3& normale_dessous, double nominale) {
        variable_ = true;
        o_dessous_ = origine_dessous, n_dessous_ = normale_dessous, h_nominale_ = nominale;
    }
    void epaisseur_fixe() { variable_ = false; }

    // Partie courbe : pendant la couche qui commence, la tête glisse peu à
    // peu vers l'orientation de la couche suivante (de normale « w_suivante »),
    // tout en imprimant. « longueur » : longueur de cordon de la couche ;
    // « z_min » : son point le plus bas au-dessus du plateau.
    void pivoter_en_imprimant(double longueur, const Vec3& w_suivante, double z_min) {
        continu_ = false;
        a_preparer_ = options_.pivot_continu && machine_.pivot_continu && longueur > 1e-6;
        longueur_couche_ = longueur;
        parcouru_ = 0.0;
        outil_suivant_ = direction_outil(w_suivante);
        z_min_couche_ = z_min;
    }
    void pivoter_entre_les_couches() { continu_ = a_preparer_ = false; }

    void couche(double z, double hauteur) {
        z_ = z;
        apercu_.push_back({(origine_ + w_ * z).z, partie_, {}});
        const double section = (r_.largeur_extrusion - hauteur) * hauteur +
                               std::numbers::pi * hauteur * hauteur / 4.0;
        const double section_filament =
            std::numbers::pi * r_.diametre_filament * r_.diametre_filament / 4.0;
        e_par_mm_ = section / section_filament * r_.debit;
    }

    // Déplacement à vide jusqu'au point de la couche en cours.
    void aller_a(const Point2& p) {
        if (surface_) {
            aller_sur_la_surface(p);
            return;
        }
        if (place_ && oriente_ && distance(position_, p) < 1e-4 && std::abs(z_local_ - z_) < 1e-6) {
            return;  // déjà sur place
        }
        retracter();
        double z_trajet;
        if (!oriente_) {
            z_trajet = changer_orientation(p);
        } else if (pivot_) {
            z_trajet = pivoter_vers(p);
        } else {
            z_trajet = std::max(z_local_, z_) + r_.levee_z;
            deplacer(point(position_, z_trajet));
            deplacer(point(p, z_trajet));
        }
        deplacer(point(p, z_));
        position_ = p;
        z_local_ = z_;
    }

    void extruder_vers(const Point2& p, double vitesse) {
        const double longueur = distance(position_, p);
        if (longueur < 1e-4) {
            return;
        }
        if (surface_) {
            extruder_sur_la_surface(p, vitesse);
            return;
        }
        if (retracte_) {
            ligne(std::format("G1 E{:.3f} F{:.0f}", r_.retraction_longueur, r_.retraction_vitesse * 60.0));
            retracte_ = false;
            duree_ += r_.retraction_longueur / r_.retraction_vitesse;
        }
        const Vec3 cible = point(p, z_);
        if (a_preparer_) {
            preparer_pivot_continu();
        }
        // Le segment est découpé pour que B et C ne tournent que très peu
        // d'une ligne à l'autre : la pointe reste ainsi sur sa ligne droite.
        int morceaux = 1;
        double avant = 0.0, apres = 0.0;
        if (continu_) {
            avant = std::min(1.0, parcouru_ / longueur_couche_);
            apres = std::min(1.0, (parcouru_ + longueur) / longueur_couche_);
            const double ecart = std::max(std::abs(b_fin_ - b_debut_), std::abs(c_fin_ - c_debut_)) * (apres - avant);
            morceaux = std::max(1, static_cast<int>(std::ceil(ecart / options_.pas_pivot_continu)));
        }
        const Vec3 depart = pointe_;
        for (int k = 1; k <= morceaux; ++k) {
            const double t = static_cast<double>(k) / morceaux;
            const Vec3 etape = k == morceaux ? cible : depart + (cible - depart) * t;
            double e_par_mm = e_par_mm_;
            if (variable_) {
                // Épaisseur locale de la couche, au milieu du morceau.
                const Vec3 milieu = (pointe_ + etape) * 0.5;
                const double epaisseur = std::clamp(produit_scalaire(milieu - o_dessous_, n_dessous_),
                                                    0.3 * h_nominale_, 1.6 * h_nominale_);
                const double section = (r_.largeur_extrusion - epaisseur) * epaisseur +
                                       std::numbers::pi * epaisseur * epaisseur / 4.0;
                e_par_mm = section / (std::numbers::pi * r_.diametre_filament * r_.diametre_filament / 4.0) * r_.debit;
            }
            const double e = longueur / morceaux * e_par_mm;
            if (continu_) {
                const double f = avant + (apres - avant) * t;
                b_ = b_debut_ + (b_fin_ - b_debut_) * f;
                c_ = c_debut_ + (c_fin_ - c_debut_) * f;
                const PositionMachine m = position_avec_angles(machine_, etape, b_, c_);
                ligne(std::format("G1 X{:.3f} Y{:.3f} Z{:.3f} B{:.3f} C{:.3f} E{:.5f} F{:.0f}", m.x, m.y, m.z,
                                  m.b + machine_.b_vertical, m.c, e, vitesse * 60.0));
            } else {
                const PositionMachine m = position_avec_angles(machine_, etape, b_, c_);
                ligne(std::format("G1 X{:.3f} Y{:.3f} Z{:.3f} E{:.5f} F{:.0f}", m.x, m.y, m.z, e, vitesse * 60.0));
            }
            filament_ += e;
            pointe_ = etape;
            z_haut_ = std::max(z_haut_, pointe_.z);
        }
        noter(depart, cible, type_);
        parcouru_ += longueur;
        duree_ += longueur / vitesse;
        position_ = p;
    }

    // Fin d'impression : rétraction, recul le long de la buse, puis montée.
    void degager(double hauteur) {
        retracter();
        deplacer(pointe_ + outil_ * r_.levee_z);
        const double z = std::min(std::max(pointe_.z + hauteur, hauteur_sure()), machine_.hauteur_impression);
        if (z > pointe_.z) {
            deplacer({pointe_.x, pointe_.y, z});
        }
    }

    void type(TypeTrait t) { type_ = static_cast<std::uint8_t>(t); }
    std::vector<CoucheApercu> prendre_apercu() { return std::move(apercu_); }
    const Point2& position() const { return position_; }
    bool place() const { return place_ && oriente_; }
    double filament() const { return filament_; }
    double duree() const { return duree_; }
    std::size_t changements() const { return changements_; }
    std::string prendre_gcode() { return std::move(gcode_); }

private:
    // Direction donnée à la buse pour des couches de normale « w ». Pour une
    // inclinaison insignifiante, la buse reste verticale : le plateau n'a
    // pas à tourner pour si peu.
    static Vec3 direction_outil(const Vec3& w) {
        return w.z > std::cos(en_radians(1.5)) ? Vec3{0.0, 0.0, 1.0} : w;
    }

    // Point de la pièce correspondant à un point (x, y, z) du repère de la
    // partie. Sur une couche courbe, z est compté au-dessus de la surface.
    Vec3 point(const Point2& p, double z) {
        if (surface_) {
            return sur_la_surface(p) + w_ * z;
        }
        return origine_ + u_ * p.x + v_ * p.y + w_ * z;
    }

    // --- Couches courbes ---------------------------------------------------

    double hauteur_sure() const {
        return suivie_ ? std::min(z_haut_ + options_.degagement, machine_.hauteur_impression) : hauteur_sure_;
    }

    // Plus grande inclinaison permise à cette hauteur, en degrés.
    double limite_inclinaison(double z) const {
        const bool haut = machine_.hauteur_bascule_b && z >= *machine_.hauteur_bascule_b + 0.5;
        const double butee = std::max(std::abs(machine_.b_min), std::abs(machine_.b_max));
        return std::min(limite_partie_, haut ? butee : std::min(butee, machine_.b_limite_pres_plateau));
    }

    // La même direction, redressée si elle penche de plus de « maximum » degrés.
    static Vec3 incliner_au_plus(const Vec3& direction, double maximum) {
        const double inclinaison = en_degres(std::acos(std::clamp(direction.z, -1.0, 1.0)));
        if (inclinaison <= maximum) {
            return direction;
        }
        const double horizontal = std::hypot(direction.x, direction.y);
        const double i = en_radians(maximum);
        return {direction.x / horizontal * std::sin(i), direction.y / horizontal * std::sin(i), std::cos(i)};
    }

    Vec3 sur_la_surface(const Point2& p) {
        Vec3 resultat;
        if (!surface_->poser(p, hauteur_surface_, resultat)) {
            throw ErreurTranchage(
                "Une trajectoire sort de la surface d'une couche courbe : cette partie ne peut "
                "pas être imprimée en couches courbes.");
        }
        return resultat;
    }

    // Valeur de B en ce point de la surface : la tête penche dans le même
    // sens que pour toute la couche (C ne bouge pas), mais de l'angle que
    // fait la surface à cet endroit.
    double inclinaison_locale(const Vec3& ici) const {
        const double moyenne = en_degres(std::acos(std::clamp(outil_.z, -1.0, 1.0)));
        Vec3 normale;
        double pente = 1.0;
        if (moyenne < 1e-6 || !surface_->normale(ici, normale, pente)) {
            return b_couche_;
        }
        const double horizontal = std::hypot(outil_.x, outil_.y);
        const double vers = (normale.x * outil_.x + normale.y * outil_.y) / horizontal;
        const double limite = limite_inclinaison(ici.z);
        const double angle = std::clamp(en_degres(std::atan2(vers, normale.z)), -limite, limite);
        return std::clamp(b_couche_ * (angle / moyenne), machine_.b_min, machine_.b_max);
    }

    // Orientation de la tête (B, C) en ce point de la surface.
    struct Orientation {
        double b, c;
    };
    Orientation orientation_locale(const Vec3& ici) const {
        if (!libre_) {
            return {inclinaison_locale(ici), c_};
        }
        Vec3 normale;
        double pente = 1.0;
        if (!surface_->normale(ici, normale, pente)) {
            return {b_, c_};
        }
        normale = incliner_au_plus(normale, limite_inclinaison(ici.z));
        // Presque à plat : la tête reste verticale, le plateau ne bouge pas.
        if (normale.z > std::cos(en_radians(3.0))) {
            return {0.0, c_};
        }
        // Des deux façons de pencher, celle qui fait le moins tourner le plateau.
        const auto position = position_machine(machine_, {ici, normale}, c_, 0);
        return position ? degager(ici, {position->b, position->c}) : Orientation{b_, c_};
    }

    // Orientation libre, en suivant le cordon : la tête se met d'aplomb sur
    // la surface, mais le plateau ne tourne pas de plus de quelques degrés
    // par millimètre parcouru. Quand il ne peut pas suivre (près d'un sommet,
    // la pente change de direction très vite), la tête penche au mieux dans
    // le sens que permet la position du plateau.
    Orientation orientation_suivie(const Vec3& ici, double b_avant, double c_avant, double parcouru) const {
        Vec3 normale;
        double pente = 1.0;
        if (!surface_->normale(ici, normale, pente)) {
            return {b_avant, c_avant};
        }
        const double limite = limite_inclinaison(ici.z);
        normale = incliner_au_plus(normale, limite);
        if (normale.z > std::cos(en_radians(3.0))) {
            return {0.0, c_avant};
        }
        double c = c_avant;
        if (const auto position = position_machine(machine_, {ici, normale}, c_avant, 0)) {
            const double permis = options_.rotation_plateau_par_mm * parcouru;
            c = c_avant + std::clamp(position->c - c_avant, -permis, permis);
            if (std::abs(position->c - c) < 1e-9) {
                return degager(ici, {position->b, c});
            }
        }
        // Sens dans lequel la tête penche, pour B positif, avec le plateau à cet angle.
        const Vec3 sens = pose_dans_piece(machine_, PositionMachine{0.0, 0.0, 0.0, 45.0, c}).direction;
        const double horizontal = std::hypot(sens.x, sens.y);
        const double vers = (normale.x * sens.x + normale.y * sens.y) / horizontal;
        const double b = std::clamp(en_degres(std::atan2(vers, normale.z)), -limite, limite);
        return degager(ici, {std::clamp(b, machine_.b_min, machine_.b_max), c});
    }

    // Si l'axe de la buse, ainsi orientée, rencontre la pièce (un mur tout
    // près), la tête est redressée par paliers jusqu'à être dégagée.
    Orientation degager(const Vec3& ici, Orientation o) const {
        for (const double part : {1.0, 0.66, 0.33}) {
            const double b = o.b * part;
            const Vec3 axe = pose_dans_piece(machine_, PositionMachine{0.0, 0.0, 0.0, b, o.c}).direction;
            bool degage = true;
            for (double t = 0.6; t <= 8.0 && degage; t += 0.3) {
                degage = surface_->libre(ici + axe * t);
            }
            if (degage) {
                return {b, o.c};
            }
        }
        return {0.0, o.c};
    }

    // Tourne la tête et le plateau par petits pas, la pointe restant sur place.
    void tourner_sur_place(const Orientation& cible) {
        const double b0 = b_, c0 = c_;
        const double ecart = std::max(std::abs(cible.b - b0), std::abs(cible.c - c0));
        const int pas = static_cast<int>(std::ceil(ecart / options_.pas_rotation));
        const Vec3 ici = pointe_;
        for (int k = 1; k <= pas; ++k) {
            const double t = static_cast<double>(k) / pas;
            b_ = b0 + (cible.b - b0) * t;
            c_ = c0 + (cible.c - c0) * t;
            deplacer_tous_axes(ici);
        }
        b_ = cible.b, c_ = cible.c;
    }

    void aller_sur_la_surface(const Point2& p) {
        const Vec3 cible = sur_la_surface(p);
        if (place_ && oriente_ && !pivot_ && norme(cible - pointe_) < 1e-4) {
            return;  // déjà sur place
        }
        retracter();
        const Vec3 pied = origine_ + u_ * p.x + v_ * p.y;
        bool grand_changement = !oriente_;
        if (!grand_changement && pivot_) {
            // Couche suivante de la même partie : la carte a un peu tourné.
            const auto orientation = position_machine(machine_, {cible, outil_}, c_, cote_);
            if (!orientation) {
                throw ErreurTranchage(
                    "La machine ne peut pas suivre cette couche courbe : l'orientation demandée "
                    "dépasse les butées des axes B ou C.");
            }
            if (std::abs(orientation->b - b_couche_) > 4.0 || std::abs(orientation->c - c_) > 4.0) {
                grand_changement = true;
            } else {
                monter_au_plan_de_trajet();
                b_ = b_couche_ = orientation->b;
                c_ = orientation->c;
                deplacer_tous_axes(pied + w_ * hauteur_trajet_);
                w_ancien_ = outil_;
            }
        } else if (!grand_changement) {
            monter_au_plan_de_trajet();
            deplacer(pied + w_ * hauteur_trajet_);
        }
        pivot_ = false;
        if (grand_changement) {
            changer_orientation(p);
        }
        // Arrivée : la tête prend l'inclinaison de la surface à cet endroit.
        const Orientation arrivee = orientation_locale(cible);
        if (libre_) {
            tourner_sur_place(arrivee);  // au-dessus de la couche, avant de descendre
        }
        b_ = arrivee.b, c_ = arrivee.c;
        deplacer_tous_axes(cible);
        position_ = p;
    }

    // Monte le long de la direction de la carte jusqu'au-dessus de toute la couche.
    void monter_au_plan_de_trajet() {
        const double manque = hauteur_trajet_ - produit_scalaire(pointe_ - origine_, w_);
        if (manque > 1e-6) {
            deplacer(pointe_ + w_ * manque);
        }
    }

    void extruder_sur_la_surface(const Point2& p, double vitesse) {
        if (retracte_) {
            ligne(std::format("G1 E{:.3f} F{:.0f}", r_.retraction_longueur, r_.retraction_vitesse * 60.0));
            retracte_ = false;
            duree_ += r_.retraction_longueur / r_.retraction_vitesse;
        }
        struct Etape {
            Point2 q;
            Vec3 point;
            double b, c;
        };
        const double section_filament = std::numbers::pi * r_.diametre_filament * r_.diametre_filament / 4.0;
        const auto emettre = [&](const Etape& a, const Etape& b) {
            const double longueur = norme(b.point - pointe_);
            if (longueur < 1e-6) {
                return;
            }
            // Épaisseur de la couche ici, et écartement réel des cordons : la
            // carte est vue de biais là où la surface est inclinée.
            double epaisseur = h_nominale_, largeur = r_.largeur_extrusion;
            Vec3 normale;
            double pente = 1.0;
            if (surface_->normale((pointe_ + b.point) * 0.5, normale, pente)) {
                epaisseur = std::clamp(h_nominale_ / pente, 0.3 * h_nominale_, 1.6 * h_nominale_);
                const double biais = std::max(0.3, produit_scalaire(normale, w_));
                const double allongement = longueur / std::max(1e-9, distance(a.q, b.q));
                largeur *= std::clamp(1.0 / (biais * allongement), 0.8, 1.5);
            }
            const double section = (largeur - epaisseur) * epaisseur + std::numbers::pi * epaisseur * epaisseur / 4.0;
            const double e = longueur * section / section_filament * r_.debit;
            if (e < 1e-5) {
                return;  // morceau trop court pour être écrit : il est joint au suivant
            }
            b_ = b.b, c_ = b.c;
            const PositionMachine m = position_avec_angles(machine_, b.point, b_, c_);
            ligne(std::format("G1 X{:.3f} Y{:.3f} Z{:.3f} B{:.3f} C{:.3f} E{:.5f} F{:.0f}", m.x, m.y, m.z,
                              m.b + machine_.b_vertical, m.c, e, vitesse * 60.0));
            noter(pointe_, b.point, type_);
            filament_ += e;
            duree_ += longueur / vitesse;
            pointe_ = b.point;
            z_haut_ = std::max(z_haut_, pointe_.z);
        };
        // Le segment de la carte est recoupé tant que la surface s'écarte de
        // la corde, ou que la tête tourne trop d'un point à l'autre.
        const std::function<void(const Etape&, const Etape&, int)> tracer = [&](const Etape& a, const Etape& b,
                                                                             int profondeur) {
            const double longueur = distance(a.q, b.q);
            if (profondeur < 12 && longueur > 0.3) {
                Etape milieu;
                milieu.q = {(a.q.x + b.q.x) / 2.0, (a.q.y + b.q.y) / 2.0};
                milieu.point = sur_la_surface(milieu.q);
                const Orientation au_milieu = orientation_locale(milieu.point);
                milieu.b = au_milieu.b, milieu.c = au_milieu.c;
                const double fleche = norme(milieu.point - (a.point + b.point) * 0.5);
                // En orientation libre, le plateau peut tourner beaucoup sur
                // une courte distance : des pas de 2° suffisent à garder la
                // pointe sur son chemin.
                const double pas_angle = libre_ ? 2.0 : options_.pas_pivot_continu;
                if (longueur > 2.0 || fleche > 0.02 || std::abs(b.b - a.b) > pas_angle ||
                    std::abs(b.c - a.c) > pas_angle) {
                    tracer(a, milieu, profondeur + 1);
                    tracer(milieu, b, profondeur + 1);
                    return;
                }
            }
            emettre(a, b);
        };
        if (libre_) {
            // Orientation libre : on avance par petits pas le long du segment,
            // l'orientation de chaque pas partant de celle du pas d'avant.
            Etape avant{position_, pointe_, b_, c_};
            const int pas = std::max(1, static_cast<int>(std::ceil(distance(position_, p) / 0.5)));
            for (int k = 1; k <= pas; ++k) {
                const double t = static_cast<double>(k) / pas;
                Etape ici;
                ici.q = {position_.x + (p.x - position_.x) * t, position_.y + (p.y - position_.y) * t};
                // Un pas intermédiaire qui frôle le bord de la zone peut ne
                // pas trouver la surface : il est sauté (le bout du segment,
                // lui, a été contrôlé d'avance).
                if (k < pas && !surface_->poser(ici.q, hauteur_surface_, ici.point)) {
                    continue;
                }
                if (k == pas) {
                    ici.point = sur_la_surface(ici.q);
                }
                const Orientation o = orientation_suivie(ici.point, avant.b, avant.c, norme(ici.point - avant.point));
                ici.b = o.b, ici.c = o.c;
                // Une grande rotation est répartie sur plusieurs lignes.
                const double ecart = std::max(std::abs(ici.b - avant.b), std::abs(ici.c - avant.c));
                const int morceaux = std::max(1, static_cast<int>(std::ceil(ecart / 2.0)));
                Etape depart = avant;
                for (int j = 1; j <= morceaux; ++j) {
                    const double u = static_cast<double>(j) / morceaux;
                    Etape etape = ici;
                    if (j < morceaux) {
                        etape.q = {avant.q.x + (ici.q.x - avant.q.x) * u, avant.q.y + (ici.q.y - avant.q.y) * u};
                        if (!surface_->poser(etape.q, hauteur_surface_, etape.point)) {
                            continue;
                        }
                        etape.b = avant.b + (ici.b - avant.b) * u;
                        etape.c = avant.c + (ici.c - avant.c) * u;
                    }
                    emettre(depart, etape);
                    depart = etape;
                }
                avant = ici;
            }
            position_ = p;
            return;
        }
        Etape arrivee;
        arrivee.q = p;
        arrivee.point = sur_la_surface(p);
        const Orientation a_l_arrivee = orientation_locale(arrivee.point);
        arrivee.b = a_l_arrivee.b, arrivee.c = a_l_arrivee.c;
        tracer(Etape{position_, pointe_, b_, c_}, arrivee, 0);
        position_ = p;
    }

    void retracter() {
        // Avant la toute première extrusion, la buse est vide : rien à rétracter.
        if (!retracte_ && filament_ > 0.0) {
            ligne(std::format("G1 E-{:.3f} F{:.0f}", r_.retraction_longueur, r_.retraction_vitesse * 60.0));
            retracte_ = true;
            duree_ += r_.retraction_longueur / r_.retraction_vitesse;
        }
    }

    // Déplacement à vide en ligne droite, sans changer B ni C.
    void deplacer(const Vec3& cible) {
        if (norme(cible - pointe_) < 1e-6) {
            return;
        }
        const PositionMachine m = position_avec_angles(machine_, cible, b_, c_);
        ligne(std::format("G0 X{:.3f} Y{:.3f} Z{:.3f} F{:.0f}", m.x, m.y, m.z, r_.vitesse_a_vide * 60.0));
        noter(pointe_, cible, type_apercu_vide);
        duree_ += norme(cible - pointe_) / r_.vitesse_a_vide;
        pointe_ = cible;
    }

    void deplacer_tous_axes(const Vec3& cible) {
        const PositionMachine m = position_avec_angles(machine_, cible, b_, c_);
        // Dans le G-code, B est compté depuis la valeur « buse verticale » de la machine.
        ligne(std::format("G0 X{:.3f} Y{:.3f} Z{:.3f} B{:.3f} C{:.3f} F{:.0f}", m.x, m.y, m.z,
                          m.b + machine_.b_vertical, m.c, r_.vitesse_a_vide * 60.0));
        if (place_) {
            noter(pointe_, cible, type_apercu_vide);
            duree_ += norme(cible - pointe_) / r_.vitesse_a_vide;
        }
        pointe_ = cible;
        place_ = true;
    }

    // Amène la tête dans l'orientation de la partie, au-dessus du point « p »
    // de la couche en cours. Renvoie la hauteur (dans le repère de la partie)
    // où la buse s'arrête, prête à descendre le long de son axe.
    double changer_orientation(const Point2& p) {
        const auto cible = position_machine(machine_, {point(p, z_), outil_}, c_, cote_);
        if (!cible) {
            throw ErreurTranchage(std::format(
                "La machine ne peut pas orienter la tête pour une partie penchée de {:.0f}° : "
                "c'est au-delà des butées des axes B ou C.",
                en_degres(std::acos(std::clamp(w_.z, -1.0, 1.0)))));
        }
        // Point d'approche : en retrait le long de la buse ; et son aplomb à la hauteur sûre.
        const Vec3 approche = point(p, z_ + options_.approche);
        const Vec3 aplomb{approche.x, approche.y, std::max(hauteur_sure(), approche.z)};
        if (!place_) {
            // Tout premier déplacement : tous les axes sont écrits.
            b_ = cible->b, c_ = cible->c;
            deplacer_tous_axes(aplomb);
        } else {
            // 1. Recul le long de l'ancienne direction, 2. montée à la verticale.
            deplacer(pointe_ + w_ancien_ * r_.levee_z);
            if (pointe_.z < hauteur_sure()) {
                deplacer({pointe_.x, pointe_.y, hauteur_sure()});
            }
            // 3. Rotation de B et de C par petits pas, la pointe allant en
            // ligne droite vers l'aplomb du point d'approche.
            const double b0 = b_, c0 = c_;
            const Vec3 depart = pointe_;
            const double ecart = std::max(std::abs(cible->b - b0), std::abs(cible->c - c0));
            const int pas = std::max(1, static_cast<int>(std::ceil(ecart / options_.pas_rotation)));
            for (int k = 1; k <= pas; ++k) {
                const double t = static_cast<double>(k) / pas;
                b_ = b0 + (cible->b - b0) * t;
                c_ = c0 + (cible->c - c0) * t;
                deplacer_tous_axes(depart + (aplomb - depart) * t);
            }
            b_ = cible->b, c_ = cible->c;
            ++changements_;
        }
        b_couche_ = b_;
        // 4. Descente jusqu'au point d'approche.
        deplacer(approche);
        oriente_ = true;
        w_ancien_ = outil_;
        return z_ + options_.approche;
    }

    // Petit changement d'orientation entre deux couches d'une partie courbe.
    // Renvoie la hauteur (dans le nouveau repère) où la buse s'arrête.
    double pivoter_vers(const Point2& p) {
        pivot_ = false;
        const auto cible = position_machine(machine_, {point(p, z_), outil_}, c_, cote_);
        if (!cible) {
            throw ErreurTranchage(
                "La machine ne peut pas suivre la courbe de cette partie : l'orientation "
                "demandée dépasse les butées des axes B ou C.");
        }
        // Une grande rotation (par exemple le plateau qui doit faire un quart
        // de tour) se fait au-dessus de la pièce, comme entre deux parties.
        if (std::abs(cible->b - b_) > 4.0 || std::abs(cible->c - c_) > 4.0) {
            return changer_orientation(p);
        }
        deplacer(pointe_ + w_ancien_ * r_.levee_z);
        b_ = b_couche_ = cible->b, c_ = cible->c;
        deplacer_tous_axes(point(p, z_ + r_.levee_z));
        w_ancien_ = outil_;
        return z_ + r_.levee_z;
    }

    // Au premier cordon d'une couche courbe : décide si la tête peut glisser
    // vers l'orientation de la couche suivante pendant l'impression.
    void preparer_pivot_continu() {
        a_preparer_ = false;
        const auto fin = position_machine(machine_, {pointe_, outil_suivant_}, c_, cote_);
        if (!fin) {
            return;  // hors des butées : le changement d'orientation suivant le dira
        }
        const double ecart_b = std::abs(fin->b - b_), ecart_c = std::abs(fin->c - c_);
        // Une grande rotation se fait à vide, au-dessus de la pièce.
        if (ecart_b > 4.0 || ecart_c > 4.0 || (ecart_b < 1e-9 && ecart_c < 1e-9)) {
            return;
        }
        // Près du plateau, l'inclinaison est limitée : la tête ne prend pas
        // d'avance sur une inclinaison qui n'est permise que plus haut.
        const bool haut = machine_.hauteur_bascule_b && z_min_couche_ >= *machine_.hauteur_bascule_b;
        if (!haut && std::abs(fin->b) > machine_.b_limite_pres_plateau) {
            return;
        }
        b_debut_ = b_, c_debut_ = c_;
        b_fin_ = fin->b, c_fin_ = fin->c;
        continu_ = true;
    }

    void noter(const Vec3& a, const Vec3& b, std::uint8_t type) {
        if (apercu_.empty() || norme(b - a) < 1e-6) {
            return;
        }
        apercu_.back().mouvements.push_back(
            {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z),
             static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z), type});
    }

    const ReglagesMachine machine_;
    const ReglagesImpression r_;
    const OptionsMultiaxe options_;
    const double hauteur_sure_;
    std::string gcode_;
    Vec3 origine_, u_{1, 0, 0}, v_{0, 1, 0}, w_{0, 0, 1};  // repère de la partie en cours
    Vec3 outil_{0, 0, 1};     // direction de la buse pour la couche en cours
    Vec3 w_ancien_{0, 0, 1};  // direction de la buse avant le changement d'orientation
    Vec3 pointe_;             // pointe de la buse, dans le repère de la pièce
    Point2 position_;         // la même, dans le plan des couches de la partie
    double z_local_ = 0.0;    // et sa hauteur dans le repère de la partie
    double z_ = 0.0;          // hauteur de la couche en cours
    double b_ = 0.0, c_ = 0.0;
    bool place_ = false;      // faux tant que la buse n'a pas été positionnée
    bool oriente_ = false;    // faux tant que la tête n'est pas dans l'orientation de la partie
    bool pivot_ = false;      // vrai quand le repère vient de pivoter un peu (partie courbe)
    // Couche non plane en cours (sinon surface_ est nul).
    const Surface* surface_ = nullptr;
    bool libre_ = false;  // vrai : la tête se met d'aplomb sur la surface en chaque point
    double hauteur_trajet_ = 0.0;   // hauteur des déplacements à vide au-dessus de la carte
    double hauteur_surface_ = 0.0;  // hauteur de la surface au dernier point calculé
    double b_couche_ = 0.0;         // valeur de B pour la direction moyenne de la couche
    double limite_partie_ = 90.0;   // inclinaison maximale de la tête pour la partie en cours
    bool suivie_ = false;           // vrai : la hauteur sûre suit ce qui est imprimé
    double z_haut_ = 0.0;           // point le plus haut déjà imprimé
    bool variable_ = false;   // vrai quand l'épaisseur de la couche varie d'un point à l'autre
    // Pivot continu (partie courbe) : pendant la couche, B et C vont de
    // (b_debut_, c_debut_) à (b_fin_, c_fin_) au fil de la longueur imprimée.
    bool a_preparer_ = false;
    bool continu_ = false;
    double longueur_couche_ = 0.0, parcouru_ = 0.0, z_min_couche_ = 0.0;
    double b_debut_ = 0.0, c_debut_ = 0.0, b_fin_ = 0.0, c_fin_ = 0.0;
    Vec3 outil_suivant_{0, 0, 1};
    Vec3 o_dessous_, n_dessous_{0, 0, 1};  // plan de la couche du dessous
    double h_nominale_ = 0.2;
    double e_par_mm_ = 0.0;
    bool retracte_ = false;
    double filament_ = 0.0;
    double duree_ = 0.0;
    std::size_t changements_ = 0;
    int partie_ = -1;  // numéro de la partie en cours
    int cote_ = 0;     // côté imposé pour pencher la tête (0 : libre)
    std::uint8_t type_ = 0;
    std::vector<CoucheApercu> apercu_;
};

// Sections d'une partie courbe : des plans qui pivotent le long de sa courbe.
// Chaque plan est avancé juste assez pour que la couche ait, à son endroit
// le plus épais, la hauteur de couche demandée ; elle est plus fine du côté
// intérieur de la courbe. Rend les contours, un par couche, et remplit
// « reperes » (le plan de chaque couche et celui de la couche du dessous).
std::vector<Polygones> sections_pivotantes(const PartieImpression& partie, const ReglagesImpression& r,
                                           bool sur_plateau, std::size_t numero,
                                           std::vector<RepereCouche>& reperes) {
    const std::vector<PointGuide>& guide = partie.guide;
    // Longueur parcourue le long de la courbe à chaque point.
    std::vector<double> abscisses(guide.size(), 0.0);
    for (std::size_t i = 1; i < guide.size(); ++i) {
        abscisses[i] = abscisses[i - 1] + norme(guide[i].point - guide[i - 1].point);
    }
    const auto redressee = [](const Vec3& d) { return normaliser(d); };
    // Point et direction de la courbe à l'abscisse « s » (prolongée tout droit aux deux bouts).
    const auto evaluer = [&](double s, Vec3& point, Vec3& direction) {
        if (s <= 0.0 || guide.size() == 1) {
            direction = redressee(guide.front().direction);
            point = guide.front().point + direction * s;
            return;
        }
        if (s >= abscisses.back()) {
            direction = redressee(guide.back().direction);
            point = guide.back().point + direction * (s - abscisses.back());
            return;
        }
        std::size_t i = 1;
        while (abscisses[i] < s) {
            ++i;
        }
        const double t = (s - abscisses[i - 1]) / std::max(1e-9, abscisses[i] - abscisses[i - 1]);
        point = guide[i - 1].point + (guide[i].point - guide[i - 1].point) * t;
        direction = redressee(guide[i - 1].direction + (guide[i].direction - guide[i - 1].direction) * t);
    };

    Vec3 o_dessous, n_dessous;
    evaluer(0.0, o_dessous, n_dessous);
    if (sur_plateau && n_dessous.z < 1.0 - 1e-9) {
        throw ErreurTranchage(std::format(
            "La partie {} est posée sur le plateau : sa courbe doit partir à la verticale.", numero));
    }
    Vec3 u_dessous, v_dessous;
    if (n_dessous.z > 1.0 - 1e-9) {
        u_dessous = {1, 0, 0}, v_dessous = {0, 1, 0};
    } else {
        repere_autour(n_dessous, u_dessous, v_dessous);
    }

    const BoiteEnglobante boite = calculer_boite(partie.maillage);
    const Point3 taille = boite.dimensions();
    const double fin = abscisses.back() + std::hypot(taille.x, taille.y, taille.z);
    std::vector<Polygones> contours;
    double s = 0.0;
    for (double h = sur_plateau ? r.hauteur_premiere_couche : r.hauteur_couche; s < fin; h = r.hauteur_couche) {
        double avance = h;
        Polygones section;
        RepereCouche repere;
        double fine = 0.0;
        for (int essai = 0; essai < 8; ++essai) {
            evaluer(s + avance, repere.origine, repere.w);
            // Le repère de la couche glisse depuis celui du dessous, sans vriller.
            repere.u = normaliser(u_dessous - repere.w * produit_scalaire(u_dessous, repere.w));
            repere.v = produit_vectoriel(repere.w, repere.u);
            const Maillage local = dans_repere(partie.maillage, repere.origine, repere.u, repere.v, repere.w);
            ResultatCoupe coupe = couper_maillage(local, {0.0});
            if (coupe.contours_ouverts > 0) {
                throw ErreurTranchage(std::format(
                    "La partie {} est abîmée : un contour ne se referme pas pendant la coupe.", numero));
            }
            section = autour_de_l_axe(coupe.couches.front());
            if (section.empty()) {
                break;
            }
            // Épaisseur de la couche : distance de chaque point au plan du dessous.
            double epaisse = -1e9;
            fine = 1e9;
            for (const Contour& contour : section) {
                for (const PointEntier& sommet : contour) {
                    const Point2 q = en_mm(sommet);
                    const Vec3 point = repere.origine + repere.u * q.x + repere.v * q.y;
                    const double epaisseur = produit_scalaire(point - o_dessous, n_dessous);
                    epaisse = std::max(epaisse, epaisseur);
                    fine = std::min(fine, epaisseur);
                }
            }
            if (epaisse <= 1e-6 || std::abs(epaisse - h) < 0.02 * h) {
                break;
            }
            avance *= h / epaisse;
        }
        if (section.empty()) {
            break;  // le plan a dépassé le bout de la partie
        }
        if (fine < 0.35 * h) {
            throw ErreurTranchage(std::format(
                "La partie {} tourne trop serré pour son épaisseur : du côté intérieur de la "
                "courbe, les couches seraient trop fines ({:.2f} mm).",
                numero, fine));
        }
        repere.origine_dessous = o_dessous;
        repere.normale_dessous = n_dessous;
        reperes.push_back(repere);
        contours.push_back(std::move(section));
        s += avance;
        o_dessous = repere.origine, n_dessous = repere.w, u_dessous = repere.u, v_dessous = repere.v;
    }
    if (contours.empty()) {
        throw ErreurTranchage(std::format("La partie {} est plus fine qu'une couche : rien à imprimer.", numero));
    }
    return contours;
}

}  // namespace

bool couches_pivotantes_possibles(const PartieImpression& partie, const ReglagesImpression& reglages) {
    if (partie.guide.empty()) {
        return false;
    }
    try {
        std::vector<RepereCouche> reperes;
        sections_pivotantes(partie, reglages, partie.parent < 0, 0, reperes);
        return true;
    } catch (const ErreurTranchage&) {
        return false;
    }
}

namespace {

// « reduction » : degrés retirés à l'inclinaison maximale de la tête des
// parties en couches courbes (voir trancher_strategie).
// « avec_peau » : faux pour ne pas faire la passe de peau courbe.
ResultatTranchage trancher_avec(const Strategie& strategie, const Reglages& reglages_demandes,
                                const OptionsMultiaxe& options, const std::vector<double>& reductions,
                                const std::vector<int>& cotes, bool avec_peau = true) {
    Reglages reglages = reglages_demandes;
    reglages.impression.peau_courbe = reglages.impression.peau_courbe && avec_peau;
    if (strategie.parties.empty()) {
        throw ErreurTranchage("La stratégie ne contient aucune partie à imprimer.");
    }
    // Une seule partie verticale : c'est le slicer 3 axes.
    if (strategie.parties.size() == 1 && strategie.parties.front().direction.z > 1.0 - 1e-9 &&
        strategie.parties.front().guide.empty() && !reglages.impression.peau_courbe) {
        return trancher(strategie.parties.front().maillage, reglages);
    }

    const ReglagesImpression& r = reglages.impression;
    const ReglagesMachine& machine = reglages.machine;
    if (r.hauteur_couche >= r.largeur_extrusion || r.hauteur_premiere_couche >= r.largeur_extrusion) {
        throw ErreurTranchage("La hauteur de couche doit être plus petite que la largeur d'extrusion.");
    }
    double rayon = 0.0, z_max = 0.0;
    for (std::size_t p = 0; p < strategie.parties.size(); ++p) {
        const Maillage& maillage = strategie.parties[p].maillage;
        if (strategie.parties[p].couches_courbes) {
            continue;  // une branche en couches courbes : découpée dans le modèle entier
        }
        const RapportFermeture fermeture = verifier_fermeture(maillage);
        if (maillage.triangles.empty() || !fermeture.ferme || !fermeture.orientation_coherente) {
            throw ErreurTranchage(std::format(
                "La partie {} n'est pas un volume fermé : {} La découpe du modèle a échoué.",
                p + 1, fermeture.message));
        }
        for (const Point3& sommet : maillage.sommets) {
            rayon = std::max(rayon, std::hypot(static_cast<double>(sommet.x), static_cast<double>(sommet.y)));
            z_max = std::max(z_max, static_cast<double>(sommet.z));
        }
    }
    if (rayon > machine.rayon_plateau + 1e-3) {
        throw ErreurTranchage(std::format(
            "Le modèle dépasse du plateau : il s'étend à {:.1f} mm du centre, pour un plateau "
            "de rayon {:.0f} mm. Il faut le réduire ou le tourner.",
            rayon, machine.rayon_plateau));
    }
    const double hauteur_sure = z_max + options.degagement;
    if (hauteur_sure > machine.hauteur_impression) {
        throw ErreurTranchage(std::format(
            "Il n'y a pas assez de hauteur pour changer d'orientation au-dessus de la pièce : "
            "il faut {:.0f} mm (modèle de {:.0f} mm + {:.0f} mm de dégagement), pour une hauteur "
            "d'impression de {:.0f} mm.",
            hauteur_sure, z_max, options.degagement, machine.hauteur_impression));
    }

    // Trajectoires de chaque partie, dans son propre repère.
    struct PartieTranchee {
        Vec3 origine, u, v, w;
        int cote = 0;
        // Partie courbe : un repère par couche (sinon vide).
        std::vector<RepereCouche> reperes;
        // Couches courbes : la carte de chaque couche (sinon vide).
        std::vector<CoucheCourbe> cartes;
        std::vector<CoucheTrajectoires> couches;
        bool sur_plateau = true;
        double inclinaison_outil_max = 90.0;
        // Peau courbe : la surface du dessus, et le numéro de sa passe
        // (la dernière « couche » de la partie).
        std::shared_ptr<PeauCourbe> peau;
        std::size_t couche_peau = 0;
    };
    // Couches courbes : le chemin de la matière dans tout le modèle, et la
    // longueur de chemin du dessus de chaque couche.
    const bool courbes = std::any_of(strategie.parties.begin(), strategie.parties.end(),
                                     [](const PartieImpression& p) { return p.couches_courbes; });
    PlanBranches plan;
    if (courbes) {
        if (strategie.modele.triangles.empty()) {
            throw ErreurTranchage("La stratégie en couches courbes ne contient pas le modèle entier.");
        }
        plan = preparer_branches(strategie.modele, r.hauteur_couche, 0.45 * r.largeur_extrusion);
    }
    const ChampCouches& champ = plan.analyse.champ;
    const std::vector<double>& dessus = plan.dessus;
    const std::vector<double>& epaisseurs = plan.epaisseurs;
    std::size_t tige_suivante = 0;
    std::vector<PartieTranchee> tranchees;
    std::size_t nombre_couches = 0;
    for (std::size_t p = 0; p < strategie.parties.size(); ++p) {
        const PartieImpression& partie = strategie.parties[p];
        PartieTranchee t;
        t.w = normaliser(partie.direction);
        const bool verticale = t.w.z > 1.0 - 1e-9;
        if (verticale) {
            t.u = {1, 0, 0}, t.v = {0, 1, 0}, t.w = {0, 0, 1};
        } else {
            repere_autour(t.w, t.u, t.v);
        }
        const bool sur_plateau = partie.parent < 0;
        t.sur_plateau = sur_plateau;
        t.cote = p < cotes.size() && cotes[p] != 0 ? cotes[p] : partie.cote;
        t.inclinaison_outil_max = partie.inclinaison_outil_max - (p < reductions.size() ? reductions[p] : 0.0);
        if (partie.couches_courbes) {
            // La tige suivante du plan (celles qui n'ont pas de couche ne sont pas des parties).
            while (tige_suivante < plan.tiges.size() && plan.tiges[tige_suivante].empty()) {
                ++tige_suivante;
            }
            if (tige_suivante >= plan.tiges.size()) {
                throw ErreurTranchage("La stratégie en couches courbes ne correspond plus au modèle.");
            }
            t.cartes = plan.tiges[tige_suivante++];
            std::vector<Polygones> contours;
            for (const CoucheCourbe& carte : t.cartes) {
                contours.push_back(carte.contour);
            }
            ReglagesImpression reglages_courbe = r;
            reglages_courbe.jupe_boucles = 0;
            reglages_courbe.hauteur_premiere_couche = r.hauteur_couche;
            t.couches = calculer_trajectoires(contours, reglages_courbe);
            t.origine = t.cartes.front().origine, t.u = t.cartes.front().u;
            t.v = t.cartes.front().v, t.w = t.cartes.front().w;
            nombre_couches += t.couches.size();
            tranchees.push_back(std::move(t));
            continue;
        }
        if (!partie.guide.empty()) {
            // Partie courbe : ses couches pivotent le long de sa courbe.
            std::vector<Polygones> contours = sections_pivotantes(partie, r, sur_plateau, p + 1, t.reperes);
            ReglagesImpression reglages_courbe = r;
            if (!sur_plateau) {
                reglages_courbe.jupe_boucles = 0;
                reglages_courbe.hauteur_premiere_couche = r.hauteur_couche;
            }
            t.couches = calculer_trajectoires(contours, reglages_courbe);
            t.origine = t.reperes.front().origine, t.u = t.reperes.front().u;
            t.v = t.reperes.front().v, t.w = t.reperes.front().w;
            if (t.couches.empty() || t.couches.front().traits.empty()) {
                throw ErreurTranchage(std::format(
                    "La première couche de la partie {} est vide : elle ne touche pas assez {}.",
                    p + 1, sur_plateau ? "le plateau" : "la partie qui la porte"));
            }
            nombre_couches += t.couches.size();
            tranchees.push_back(std::move(t));
            continue;
        }
        t.origine = sur_plateau ? Vec3{} : partie.base;
        // Peau courbe : le dessus de la partie est abaissé, les couches
        // planes s'arrêtent dessous et une dernière passe suit la surface.
        std::shared_ptr<PeauCourbe> peau;
        if (r.peau_courbe && sur_plateau && verticale && !courbes) {
            std::vector<const Maillage*> tout;
            for (const PartieImpression& autre : strategie.parties) {
                tout.push_back(&autre.maillage);
            }
            auto trouvee = std::make_shared<PeauCourbe>(
                preparer_peau_courbe(partie.maillage, tout, r.hauteur_couche, r.peau_courbe_pente_max,
                                     r.hauteur_premiere_couche));
            if (!trouvee->vide()) {
                peau = std::move(trouvee);
            }
        }
        const double haut_origine = calculer_boite(partie.maillage).max.z;
        const Maillage local = dans_repere(peau ? peau->abaisse() : partie.maillage, t.origine, t.u, t.v, t.w);
        const double haut = calculer_boite(local).max.z;

        // Une partie posée sur une autre n'a ni jupe ni première couche spéciale.
        ReglagesImpression reglages_partie = r;
        if (!sur_plateau) {
            reglages_partie.jupe_boucles = 0;
            reglages_partie.hauteur_premiere_couche = r.hauteur_couche;
        }
        std::vector<double> hauteurs;
        for (double bas = 0.0, epaisseur = reglages_partie.hauteur_premiere_couche;
             bas + epaisseur / 2.0 < haut; bas += epaisseur, epaisseur = r.hauteur_couche) {
            hauteurs.push_back(bas + epaisseur / 2.0);
        }
        if (hauteurs.empty()) {
            throw ErreurTranchage(std::format("La partie {} est plus fine qu'une couche : rien à imprimer.", p + 1));
        }
        ResultatCoupe coupe = couper_maillage(local, hauteurs);
        if (coupe.contours_ouverts > 0) {
            throw ErreurTranchage(std::format(
                "La partie {} est abîmée : {} contour(s) ne se referment pas pendant la coupe.",
                p + 1, coupe.contours_ouverts));
        }
        // Pour chaque couche du tronc : la zone des branches à cette hauteur.
        std::vector<Polygones> zones_branches;
        if (partie.tronc_lisse && courbes) {
            // Seul le tronc est imprimé ici : la zone des branches est retirée
            // de chaque couche (et les éclats plus fins qu'un demi-cordon).
            // Puis le contour est avancé dans cette zone de l'épaisseur des
            // parois : les parois qui s'y trouveront seront supprimées plus
            // bas, et le remplissage arrivera ainsi jusqu'à la surface du
            // tronc, laissée à nu là où une branche viendra se souder.
            const double parois = reglages_partie.nombre_parois * r.largeur_extrusion;
            zones_branches.resize(coupe.couches.size());
            for (std::size_t c = 0; c < coupe.couches.size(); ++c) {
                zones_branches[c] = plan.analyse.zone_a(hauteurs[c]);
                if (zones_branches[c].empty()) {
                    continue;
                }
                const Polygones reste = soustraire(coupe.couches[c], zones_branches[c]);
                const Polygones tronc =
                    decaler(decaler(reste, -0.25 * r.largeur_extrusion), 0.25 * r.largeur_extrusion);
                coupe.couches[c] = intersecter(coupe.couches[c], decaler_arrondi(tronc, parois));
            }
        }
        while (!coupe.couches.empty() && coupe.couches.back().empty()) {
            coupe.couches.pop_back();
        }
        for (std::size_t c = 0; c < coupe.couches.size(); ++c) {
            if (coupe.couches[c].empty()) {
                throw ErreurTranchage(std::format(
                    "La couche {} de la partie {} est vide : un morceau flotte en l'air et ne "
                    "peut pas être imprimé.",
                    c + 1, p + 1));
            }
        }
        t.couches = calculer_trajectoires(coupe.couches, reglages_partie);
        // Surface à nu : les morceaux de paroi qui tombent dans la zone d'une
        // branche sont retirés. Il reste le remplissage, apparent.
        for (std::size_t c = 0; c < t.couches.size() && c < zones_branches.size(); ++c) {
            const Polygones& zone = zones_branches[c];
            if (zone.empty()) {
                continue;
            }
            const auto dans_la_zone = [&](const Point2& q) {
                int dedans = 0;
                for (const Contour& contour : zone) {
                    dedans += Clipper2Lib::PointInPolygon(en_unites(q), contour) !=
                              Clipper2Lib::PointInPolygonResult::IsOutside;
                }
                return dedans % 2 == 1;
            };
            std::vector<Trait> gardes;
            for (Trait& trait : t.couches[c].traits) {
                if (trait.type != TypeTrait::paroi_externe && trait.type != TypeTrait::paroi_interne) {
                    gardes.push_back(std::move(trait));
                    continue;
                }
                // Le trait est recoupé finement, puis on garde les suites de points hors de la zone.
                std::vector<Point2> points;
                const std::size_t nombre = trait.points.size() + (trait.ferme ? 1 : 0);
                for (std::size_t i = 0; i < nombre; ++i) {
                    const Point2& q = trait.points[i % trait.points.size()];
                    if (!points.empty()) {
                        const Point2 avant = points.back();
                        const int morceaux = std::max(1, static_cast<int>(std::ceil(distance(avant, q) / 0.4)));
                        for (int k = 1; k < morceaux; ++k) {
                            const double u = static_cast<double>(k) / morceaux;
                            points.push_back({avant.x + (q.x - avant.x) * u, avant.y + (q.y - avant.y) * u});
                        }
                    }
                    points.push_back(q);
                }
                std::vector<std::uint8_t> retire(points.size());
                bool touche = false;
                for (std::size_t i = 0; i < points.size(); ++i) {
                    retire[i] = dans_la_zone(points[i]);
                    touche = touche || retire[i];
                }
                if (!touche) {
                    gardes.push_back(std::move(trait));
                    continue;
                }
                // Pour une boucle, on commence juste après un point retiré : aucune suite n'est coupée en deux.
                std::size_t debut = 0;
                if (trait.ferme) {
                    while (debut < points.size() && !retire[debut]) {
                        ++debut;
                    }
                    points.pop_back();  // le point de fermeture, en double
                    retire.pop_back();
                    debut %= std::max<std::size_t>(1, points.size());
                }
                Trait morceau;
                morceau.type = trait.type;
                const auto finir = [&]() {
                    double longueur = 0.0;
                    for (std::size_t i = 1; i < morceau.points.size(); ++i) {
                        longueur += distance(morceau.points[i - 1], morceau.points[i]);
                    }
                    if (longueur > r.largeur_extrusion) {
                        gardes.push_back(morceau);
                    }
                    morceau.points.clear();
                };
                for (std::size_t n = 0; n < points.size(); ++n) {
                    const std::size_t i = (debut + n) % points.size();
                    if (retire[i]) {
                        finir();
                    } else {
                        morceau.points.push_back(points[i]);
                    }
                }
                finir();
            }
            // Les traits restent groupés par type, dans l'ordre d'origine.
            std::stable_sort(gardes.begin(), gardes.end(), [](const Trait& a, const Trait& b) {
                return static_cast<int>(a.type) < static_cast<int>(b.type);
            });
            t.couches[c].traits = std::move(gardes);
        }
        if (peau && !t.couches.empty()) {
            // La passe de peau : un tour le long du bord de la zone, puis des
            // allers-retours serrés, tous reposés ensuite sur la surface.
            CoucheTrajectoires passe;
            passe.z = haut_origine;
            passe.hauteur = r.hauteur_couche;
            // Les endroits trop étroits pour deux cordons côte à côte sont
            // laissés aux couches planes : la zone est rétrécie puis regonflée.
            const Polygones zone = decaler_arrondi(decaler(peau->zone(), -r.largeur_extrusion), r.largeur_extrusion);
            for (const Contour& contour : decaler(zone, -0.5 * r.largeur_extrusion)) {
                Trait trait;
                trait.type = TypeTrait::paroi_externe;
                trait.ferme = true;
                for (const PointEntier& sommet : contour) {
                    trait.points.push_back(en_mm(sommet));
                }
                if (trait.points.size() >= 3) {
                    passe.traits.push_back(std::move(trait));
                }
            }
            for (std::vector<Point2>& ligne :
                 lignes_de_remplissage(decaler(zone, -r.largeur_extrusion), r.largeur_extrusion, 45.0)) {
                // Un tout petit zigzag, dans un coin, se replierait sur lui-même : il est laissé.
                double longueur = 0.0;
                for (std::size_t i = 1; i < ligne.size(); ++i) {
                    longueur += distance(ligne[i - 1], ligne[i]);
                }
                if (ligne.size() >= 2 && longueur > 6.0 * r.largeur_extrusion) {
                    passe.traits.push_back({TypeTrait::remplissage_plein, std::move(ligne), false});
                }
            }
            if (!passe.traits.empty()) {
                t.peau = peau;
                t.couche_peau = t.couches.size();
                t.couches.push_back(std::move(passe));
            }
        }
        if (t.couches.empty() || t.couches.front().traits.empty()) {
            throw ErreurTranchage(std::format(
                "La première couche de la partie {} est vide : elle ne touche pas assez {}.",
                p + 1, sur_plateau ? "le plateau" : "la partie qui la porte"));
        }
        nombre_couches += t.couches.size();
        tranchees.push_back(std::move(t));
    }

    EcrivainCinqAxes ecrivain(reglages, hauteur_sure, options);
    ecrire_entete(ecrivain, r, nombre_couches);
    // Ordre d'impression. D'ordinaire, une partie après l'autre. En couches
    // courbes, tout monte ensemble : la couche k de chaque partie, puis la
    // couche k + 1, comme dans une impression classique.
    struct Passe {
        std::size_t niveau, partie, couche;
    };
    std::vector<Passe> passes;
    for (std::size_t p = 0; p < tranchees.size(); ++p) {
        for (std::size_t c = 0; c < tranchees[p].couches.size(); ++c) {
            if (courbes && tranchees[p].couches[c].traits.empty()) {
                continue;
            }
            const std::size_t niveau = !courbes ? 0 : tranchees[p].cartes.empty() ? c : tranchees[p].cartes[c].niveau;
            passes.push_back({niveau, p, c});
        }
    }
    std::size_t numero = 0;
    std::size_t en_cours = tranchees.size();  // partie en cours d'impression (aucune)
    for (const Passe& passe : passes) {
        const std::size_t p = passe.partie, c = passe.couche;
        PartieTranchee& t = tranchees[p];
        CoucheTrajectoires& couche = t.couches[c];
        const bool changement = en_cours != p;
        if (changement) {
            en_cours = p;
            // La partie est reprise là où elle en est : avec la carte de sa couche.
            const Vec3 origine = t.cartes.empty() ? t.origine : t.cartes[c].origine;
            const Vec3 u = t.cartes.empty() ? t.u : t.cartes[c].u;
            const Vec3 v = t.cartes.empty() ? t.v : t.cartes[c].v;
            const Vec3 w = t.cartes.empty() ? t.w : t.cartes[c].w;
            ecrivain.ligne(std::format(";PARTIE:{} direction {:.4f} {:.4f} {:.4f}", p + 1, w.x, w.y, w.z));
            ecrivain.partie(origine, u, v, w, t.cote, static_cast<int>(p));
            if (!t.cartes.empty()) {
                ecrivain.limiter_inclinaison(std::max(10.0, t.inclinaison_outil_max));
            }
            ecrivain.epaisseur_fixe();
            ecrivain.pivoter_entre_les_couches();
        }
        ecrivain.ligne(std::format(";COUCHE:{}", ++numero));
        ecrivain.ligne(std::format(";Z:{:.3f}", couche.z));
        if (numero == 2 && r.ventilation) {
            ecrivain.ligne("M106 S255 ; ventilateur de pièce");
        }
        std::unique_ptr<Surface> surface;  // doit vivre jusqu'à la fin de la couche
        if (t.peau && c == t.couche_peau) {
            // Passe de peau courbe : les trajectoires sont reposées sur le dessus de la pièce.
            surface = std::make_unique<SurfacePeau>(*t.peau);
            double hauteur_trajet = 0.0, z_min = std::numeric_limits<double>::max(), hauteur = 0.0;
            std::erase_if(couche.traits, [&](const Trait& trait) {
                for (const Point2& q : trait.points) {
                    Vec3 ici;
                    if (!surface->poser(q, hauteur, ici)) {
                        return true;
                    }
                    hauteur_trajet = std::max(hauteur_trajet, hauteur);
                    z_min = std::min(z_min, ici.z);
                }
                return false;
            });
            if (couche.traits.empty()) {
                continue;
            }
            ecrivain.ligne(";PEAU COURBE");
            // Un peu plus de matière que l'épaisseur nominale : la peau comble
            // aussi le creux des marches laissées par les couches planes.
            // La tête se met d'aplomb sur la surface, si la machine sait où est son pivot.
            ecrivain.couche_courbe(*surface, 1.25 * r.hauteur_couche, hauteur_trajet + 0.5, z_min,
                                   machine.distance_pivot_buse.has_value());
            ecrivain.couche(0.0, r.hauteur_couche);
        } else if (!t.cartes.empty()) {
            // Couche courbe : sa carte, et la surface sur laquelle la buse se pose.
            const CoucheCourbe& carte = t.cartes[c];
            if (!changement) {
                ecrivain.pivoter(carte.origine, carte.u, carte.v, carte.w);
            }
            // Point le plus haut au-dessus de la carte (pour les déplacements
            // à vide) et point le plus bas au-dessus du plateau.
            double hauteur_trajet = -std::numeric_limits<double>::max();
            double z_min = std::numeric_limits<double>::max(), hauteur = 0.0;
            // Un trait dont un point ne trouve pas la surface (un éclat au
            // tout début ou au bout de la partie) est retiré ; s'il y en a
            // beaucoup, la partie ne peut pas être imprimée ainsi.
            double garde = 0.0, retire = 0.0;
            std::erase_if(couche.traits, [&](const Trait& trait) {
                double longueur = 0.0;
                bool pose = true;
                for (std::size_t i = 0; i < trait.points.size() && pose; ++i) {
                    Vec3 ici;
                    pose = poser_sur_la_surface(champ, carte, trait.points[i], dessus[carte.niveau], hauteur, ici);
                    if (pose) {
                        hauteur_trajet = std::max(hauteur_trajet, hauteur);
                        z_min = std::min(z_min, ici.z);
                        longueur += i > 0 ? distance(trait.points[i - 1], trait.points[i]) : 0.0;
                    }
                }
                (pose ? garde : retire) += longueur;
                return !pose;
            });
            if (retire > 2.0 + 0.2 * garde) {
                throw ErreurTranchage(std::format(
                    "Des trajectoires de la partie {} sortent de la surface de leur couche courbe.", p + 1));
            }
            if (couche.traits.empty()) {
                continue;
            }
            surface = std::make_unique<SurfaceChamp>(champ, carte, dessus[carte.niveau]);
            ecrivain.couche_courbe(*surface, epaisseurs[carte.niveau], hauteur_trajet + 0.5, z_min);
            ecrivain.couche(0.0, epaisseurs[carte.niveau]);
        } else if (!t.reperes.empty()) {
            // Couche d'une partie courbe : son propre repère, la buse sur son plan.
            const RepereCouche& repere = t.reperes[c];
            if (c > 0) {
                ecrivain.pivoter(repere.origine, repere.u, repere.v, repere.w);
            }
            ecrivain.epaisseur_depuis(repere.origine_dessous, repere.normale_dessous, couche.hauteur);
            ecrivain.couche(0.0, couche.hauteur);
            if (c + 1 < t.reperes.size()) {
                // Longueur de cordon de la couche, et son point le plus bas.
                double longueur = 0.0, z_min = std::numeric_limits<double>::max();
                for (const Trait& trait : couche.traits) {
                    for (std::size_t i = 0; i < trait.points.size(); ++i) {
                        const Point2& q = trait.points[i];
                        z_min = std::min(z_min, (repere.origine + repere.u * q.x + repere.v * q.y).z);
                        if (i > 0) {
                            longueur += distance(trait.points[i - 1], q);
                        }
                    }
                    if (trait.ferme && trait.points.size() > 1) {
                        longueur += distance(trait.points.back(), trait.points.front());
                    }
                }
                ecrivain.pivoter_en_imprimant(longueur, t.reperes[c + 1].w, z_min);
            } else {
                ecrivain.pivoter_entre_les_couches();
            }
        } else {
            ecrivain.couche(couche.z, couche.hauteur);
        }
        const bool premiere = c == 0 && (t.cartes.empty() || t.sur_plateau);
        for (std::size_t debut_groupe = 0; debut_groupe < couche.traits.size();) {
            std::size_t fin_groupe = debut_groupe;
            while (fin_groupe < couche.traits.size() &&
                   couche.traits[fin_groupe].type == couche.traits[debut_groupe].type) {
                ++fin_groupe;
            }
            const TypeTrait type = couche.traits[debut_groupe].type;
            ecrivain.ligne(std::format(";TYPE:{}", nom_type(type)));
            ecrivain.type(type);
            ecrire_groupe(ecrivain, couche.traits, debut_groupe, fin_groupe, vitesse_du_type(r, type, premiere));
            debut_groupe = fin_groupe;
        }
        ecrivain.quitter_la_surface();
    }
    ecrire_fin(ecrivain, r);

    ResultatTranchage resultat;
    resultat.couches = nombre_couches;
    resultat.longueur_filament = ecrivain.filament();
    resultat.duree_estimee = ecrivain.duree();
    resultat.apercu = ecrivain.prendre_apercu();
    resultat.gcode = ecrivain.prendre_gcode();
    resultat.verification = verifier_gcode_texte(resultat.gcode, reglages);
    return resultat;
}

}  // namespace

ResultatTranchage trancher_classique(const Maillage& modele, const Reglages& reglages) {
    if (!reglages.impression.peau_courbe) {
        return trancher(modele, reglages);
    }
    const RapportFermeture fermeture = verifier_fermeture(modele);
    if (modele.triangles.empty() || !fermeture.ferme || !fermeture.orientation_coherente) {
        return trancher(modele, reglages);  // le tranchage classique explique ce qui ne va pas
    }
    Strategie classique;
    classique.type = TypeStrategie::classique;
    classique.parties.emplace_back();
    classique.parties.back().maillage = poser_au_centre(modele);
    return trancher_strategie(classique, reglages);
}

ResultatTranchage trancher_strategie(const Strategie& strategie, const Reglages& reglages,
                                     const OptionsMultiaxe& options) {
    std::vector<double> reductions(strategie.parties.size(), 0.0);
    std::vector<int> cotes(strategie.parties.size(), 0), etapes(strategie.parties.size(), 0);
    ResultatTranchage resultat = trancher_avec(strategie, reglages, options, reductions, cotes);
    const auto contact = [](TypeErreur type) {
        return type == TypeErreur::collision_matiere || type == TypeErreur::collision_plateau ||
               type == TypeErreur::collision_machine || type == TypeErreur::limite_b;
    };
    const auto touche = [&](const RapportVerification& v) {
        return std::any_of(v.erreurs.begin(), v.erreurs.end(),
                           [&](const ErreurVerification& e) { return contact(e.type); });
    };
    // Peau courbe : si la tête touche quelque chose pendant cette passe (en
    // s'inclinant près d'une autre partie de la pièce, par exemple), le
    // slicer y renonce : le dessus est imprimé en couches planes, et il le dit.
    const bool sans_peau = reglages.impression.peau_courbe && touche(resultat.verification);
    if (sans_peau) {
        resultat = trancher_avec(strategie, reglages, options, reductions, cotes, false);
        resultat.remarques.push_back(
            "La peau courbe a été abandonnée pour cette pièce : pendant cette passe, la tête aurait "
            "touché la pièce ou la machine. Le dessus est imprimé en couches planes.");
    }
    // Couches courbes : si le vérificateur trouve que la tête ou le bras
    // touche quelque chose pendant une branche, cette branche est refaite
    // autrement : d'abord en penchant la tête de l'autre côté (le plateau
    // retourné), puis en la penchant moins, de 10° en 10°. Ses couches
    // gardent la même forme ; seule l'orientation de la buse change.
    const bool courbes = std::any_of(strategie.parties.begin(), strategie.parties.end(),
                                     [](const PartieImpression& p) { return p.couches_courbes; });
    for (int essai = 1; courbes && essai <= options.redressements && touche(resultat.verification); ++essai) {
        // À quelle partie appartient chaque ligne du G-code.
        std::vector<std::size_t> debuts;  // ligne où commence chaque passage sur une partie
        std::vector<std::size_t> parties;
        {
            std::size_t numero = 0, debut = 0;
            const std::string& g = resultat.gcode;
            while (debut < g.size()) {
                ++numero;
                std::size_t fin = g.find('\n', debut);
                if (fin == std::string::npos) {
                    fin = g.size();
                }
                if (g.compare(debut, 8, ";PARTIE:") == 0) {
                    debuts.push_back(numero);
                    parties.push_back(static_cast<std::size_t>(std::stoul(g.substr(debut + 8, 6))) - 1);
                }
                debut = fin + 1;
            }
        }
        bool change = false;
        std::vector<std::uint8_t> fautive(strategie.parties.size(), 0);
        for (const ErreurVerification& e : resultat.verification.erreurs) {
            if (!contact(e.type)) {
                continue;
            }
            const auto apres = std::upper_bound(debuts.begin(), debuts.end(), static_cast<std::size_t>(e.ligne));
            if (apres == debuts.begin()) {
                continue;
            }
            const std::size_t partie = parties[apres - debuts.begin() - 1];
            if (partie < fautive.size() && strategie.parties[partie].couches_courbes) {
                fautive[partie] = 1;
            }
        }
        // Essais successifs pour une branche : un côté, l'autre, puis 10° de moins des deux côtés...
        const std::vector<double> reductions_avant = reductions;
        const std::vector<int> cotes_avant = cotes;
        for (std::size_t p = 0; p < fautive.size(); ++p) {
            if (fautive[p] && etapes[p] < 9) {
                ++etapes[p];
                cotes[p] = etapes[p] % 2 == 1 ? 1 : -1;
                reductions[p] = 10.0 * ((etapes[p] - 1) / 2);
                change = true;
            }
        }
        if (!change) {
            break;
        }
        ResultatTranchage essaye = trancher_avec(strategie, reglages, options, reductions, cotes, !sans_peau);
        essaye.remarques = resultat.remarques;
        // On garde l'essai qui a le moins de fautes : trop relever la tête
        // finit par faire plus de mal que de bien.
        if (essaye.verification.nombre_erreurs <= resultat.verification.nombre_erreurs) {
            resultat = std::move(essaye);
        } else {
            // Cet essai est moins bon : on garde le précédent, mais on
            // continuera avec l'essai suivant de la liste.
            reductions = reductions_avant;
            cotes = cotes_avant;
        }
    }
    if (resultat.verification.valide()) {
        for (std::size_t p = 0; p < reductions.size(); ++p) {
            if (reductions[p] > 0.0) {
                resultat.remarques.push_back(std::format(
                    "Partie {} : pour ne rien toucher, la tête a été limitée à {:.0f}° d'inclinaison ; elle "
                    "n'est pas toujours d'aplomb sur les couches de cette branche.",
                    p + 1, strategie.parties[p].inclinaison_outil_max - reductions[p]));
            }
        }
    }
    return resultat;
}

}  // namespace artprint
