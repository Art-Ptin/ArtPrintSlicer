// ArtPrint Slicer - licence AGPL v3
#include "multiaxe.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <vector>

#include "cinematique.hpp"
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
    void partie(const Vec3& origine, const Vec3& u, const Vec3& v, const Vec3& w, int cote) {
        origine_ = origine, u_ = u, v_ = v, w_ = w;
        outil_ = direction_outil(w);
        cote_ = cote;
        oriente_ = false;
        ++partie_;
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
        const double z = std::min(std::max(pointe_.z + hauteur, hauteur_sure_), machine_.hauteur_impression);
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

    // Point de la pièce correspondant à un point (x, y, z) du repère de la partie.
    Vec3 point(const Point2& p, double z) const { return origine_ + u_ * p.x + v_ * p.y + w_ * z; }

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
        const Vec3 aplomb{approche.x, approche.y, std::max(hauteur_sure_, approche.z)};
        if (!place_) {
            // Tout premier déplacement : tous les axes sont écrits.
            b_ = cible->b, c_ = cible->c;
            deplacer_tous_axes(aplomb);
        } else {
            // 1. Recul le long de l'ancienne direction, 2. montée à la verticale.
            deplacer(pointe_ + w_ancien_ * r_.levee_z);
            if (pointe_.z < hauteur_sure_) {
                deplacer({pointe_.x, pointe_.y, hauteur_sure_});
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
        b_ = cible->b, c_ = cible->c;
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

ResultatTranchage trancher_strategie(const Strategie& strategie, const Reglages& reglages,
                                     const OptionsMultiaxe& options) {
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
        std::vector<CoucheTrajectoires> couches;
    };
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
        t.cote = partie.cote;
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
    std::size_t numero = 0;
    for (std::size_t p = 0; p < tranchees.size(); ++p) {
        PartieTranchee& t = tranchees[p];
        ecrivain.ligne(std::format(";PARTIE:{} direction {:.4f} {:.4f} {:.4f}", p + 1, t.w.x, t.w.y, t.w.z));
        ecrivain.partie(t.origine, t.u, t.v, t.w, t.cote);
        ecrivain.epaisseur_fixe();
        ecrivain.pivoter_entre_les_couches();
        for (std::size_t c = 0; c < t.couches.size(); ++c) {
            CoucheTrajectoires& couche = t.couches[c];
            ecrivain.ligne(std::format(";COUCHE:{}", ++numero));
            ecrivain.ligne(std::format(";Z:{:.3f}", couche.z));
            if (numero == 2 && r.ventilation) {
                ecrivain.ligne("M106 S255 ; ventilateur de pièce");
            }
            if (!t.reperes.empty()) {
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
            for (std::size_t debut = 0; debut < couche.traits.size();) {
                std::size_t fin = debut;
                while (fin < couche.traits.size() && couche.traits[fin].type == couche.traits[debut].type) {
                    ++fin;
                }
                const TypeTrait type = couche.traits[debut].type;
                ecrivain.ligne(std::format(";TYPE:{}", nom_type(type)));
                ecrivain.type(type);
                ecrire_groupe(ecrivain, couche.traits, debut, fin, vitesse_du_type(r, type, c == 0));
                debut = fin;
            }
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

}  // namespace artprint
