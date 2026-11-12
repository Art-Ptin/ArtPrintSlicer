// ArtPrint Slicer - licence AGPL v3
#include "multiaxe.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <numbers>
#include <vector>

#include "champ_couches.hpp"
#include "cinematique.hpp"
#include "couches_courbes.hpp"
#include "decoupe.hpp"
#include "ecriture_gcode.hpp"
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
        champ_ = nullptr;
        limite_partie_ = 90.0;
    }

    // Plus grande inclinaison de la tête pour la partie en cours, en degrés.
    void limiter_inclinaison(double maximum) { limite_partie_ = maximum; }

    // Couches courbes : tronc et branches montent ensemble. La hauteur sûre
    // pour changer d'orientation suit alors ce qui est déjà imprimé.
    void hauteur_sure_suivie() { suivie_ = true; }

    // Couche courbe : la couche en cours est une surface du champ, dessinée
    // sur la carte donnée par partie() ou pivoter(). « hauteur_trajet » :
    // hauteur, au-dessus de la carte, du point le plus haut de la couche ;
    // « z_min » : son point le plus bas au-dessus du plateau.
    void couche_courbe(const ChampCouches& champ, const CoucheCourbe& carte, double niveau,
                       double epaisseur, double hauteur_trajet, double z_min) {
        champ_ = &champ, carte_ = &carte, niveau_ = niveau;
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
        if (champ_) {
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
        if (champ_) {
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
        if (champ_) {
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
        const bool haut = machine_.hauteur_bascule_b && z >= *machine_.hauteur_bascule_b + 0.2;
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
        if (!poser_sur_la_surface(*champ_, *carte_, p, niveau_, hauteur_surface_, resultat)) {
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
        if (moyenne < 1e-6 || !champ_->gradient(ici, normale, pente)) {
            return b_couche_;
        }
        const double horizontal = std::hypot(outil_.x, outil_.y);
        const double vers = (normale.x * outil_.x + normale.y * outil_.y) / horizontal;
        const double limite = limite_inclinaison(ici.z);
        const double angle = std::clamp(en_degres(std::atan2(vers, normale.z)), -limite, limite);
        return std::clamp(b_couche_ * (angle / moyenne), machine_.b_min, machine_.b_max);
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
        b_ = inclinaison_locale(cible);
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
            double b;
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
            if (champ_->gradient((pointe_ + b.point) * 0.5, normale, pente)) {
                epaisseur = std::clamp(h_nominale_ / pente, 0.3 * h_nominale_, 1.6 * h_nominale_);
                const double biais = std::max(0.3, produit_scalaire(normale, w_));
                const double allongement = longueur / std::max(1e-9, distance(a.q, b.q));
                largeur *= std::clamp(1.0 / (biais * allongement), 0.8, 1.5);
            }
            const double section = (largeur - epaisseur) * epaisseur + std::numbers::pi * epaisseur * epaisseur / 4.0;
            const double e = longueur * section / section_filament * r_.debit;
            b_ = b.b;
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
                milieu.b = inclinaison_locale(milieu.point);
                const double fleche = norme(milieu.point - (a.point + b.point) * 0.5);
                if (longueur > 2.0 || fleche > 0.02 || std::abs(b.b - a.b) > options_.pas_pivot_continu) {
                    tracer(a, milieu, profondeur + 1);
                    tracer(milieu, b, profondeur + 1);
                    return;
                }
            }
            emettre(a, b);
        };
        Etape arrivee;
        arrivee.q = p;
        arrivee.point = sur_la_surface(p);
        arrivee.b = inclinaison_locale(arrivee.point);
        tracer(Etape{position_, pointe_, b_}, arrivee, 0);
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
    // Couche courbe en cours (sinon champ_ est nul) : sa surface, sa carte.
    const ChampCouches* champ_ = nullptr;
    const CoucheCourbe* carte_ = nullptr;
    double niveau_ = 0.0;
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
ResultatTranchage trancher_avec(const Strategie& strategie, const Reglages& reglages,
                                const OptionsMultiaxe& options, double reduction) {
    if (strategie.parties.empty()) {
        throw ErreurTranchage("La stratégie ne contient aucune partie à imprimer.");
    }
    // Une seule partie verticale : c'est le slicer 3 axes.
    if (strategie.parties.size() == 1 && strategie.parties.front().direction.z > 1.0 - 1e-9 &&
        strategie.parties.front().guide.empty()) {
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
    };
    // Couches courbes : le chemin de la matière dans tout le modèle, et la
    // longueur de chemin du dessus de chaque couche.
    const bool courbes = std::any_of(strategie.parties.begin(), strategie.parties.end(),
                                     [](const PartieImpression& p) { return p.couches_courbes; });
    ChampCouches champ;
    std::vector<double> dessus, epaisseurs;
    if (courbes) {
        if (strategie.modele.triangles.empty()) {
            throw ErreurTranchage("La stratégie en couches courbes ne contient pas le modèle entier.");
        }
        champ = calculer_champ_couches(strategie.modele);
        for (double haut = r.hauteur_premiere_couche; haut < champ.maximum + r.hauteur_couche;
             haut += r.hauteur_couche) {
            dessus.push_back(haut);
            epaisseurs.push_back(dessus.size() == 1 ? r.hauteur_premiere_couche : r.hauteur_couche);
        }
    }
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
        t.cote = partie.cote;
        t.inclinaison_outil_max = partie.inclinaison_outil_max - reduction;
        if (partie.couches_courbes) {
            t.cartes = trancher_selon_champ(partie.maillage, champ, dessus, epaisseurs, p + 1);
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
        const Maillage local = dans_repere(partie.maillage, t.origine, t.u, t.v, t.w);
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
    if (courbes) {
        ecrivain.hauteur_sure_suivie();
        // D'un niveau au suivant, les parties sont visitées dans l'ordre
        // inverse (aller, puis retour) : le plateau revient sur ses pas au
        // lieu de s'enrouler toujours dans le même sens.
        std::stable_sort(passes.begin(), passes.end(), [](const Passe& a, const Passe& b) {
            if (a.niveau != b.niveau) {
                return a.niveau < b.niveau;
            }
            return a.niveau % 2 == 0 ? a.partie < b.partie : a.partie > b.partie;
        });
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
        if (!t.cartes.empty()) {
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
            ecrivain.couche_courbe(champ, carte, dessus[carte.niveau], epaisseurs[carte.niveau],
                                   hauteur_trajet + 0.5, z_min);
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

ResultatTranchage trancher_strategie(const Strategie& strategie, const Reglages& reglages,
                                     const OptionsMultiaxe& options) {
    ResultatTranchage resultat = trancher_avec(strategie, reglages, options, 0.0);
    // Couches courbes : si le vérificateur trouve que la tête ou le bras
    // touche quelque chose, on recommence en penchant moins la tête. Les
    // couches gardent la même forme ; seule l'orientation de la buse change.
    const bool courbes = std::any_of(strategie.parties.begin(), strategie.parties.end(),
                                     [](const PartieImpression& p) { return p.couches_courbes; });
    const auto touche = [](const RapportVerification& v) {
        return v.contient(TypeErreur::collision_matiere) || v.contient(TypeErreur::collision_plateau) ||
               v.contient(TypeErreur::collision_machine);
    };
    for (double reduction = 10.0; courbes && reduction <= 40.0 && touche(resultat.verification); reduction += 10.0) {
        resultat = trancher_avec(strategie, reglages, options, reduction);
    }
    return resultat;
}

}  // namespace artprint
