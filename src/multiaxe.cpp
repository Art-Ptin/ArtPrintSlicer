// ArtPrint Slicer - licence AGPL v3
#include "multiaxe.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <vector>

#include "cinematique.hpp"
#include "decoupe.hpp"
#include "ecriture_gcode.hpp"
#include "tranchage.hpp"
#include "trajectoires.hpp"

namespace artprint {

namespace {

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
        cote_ = cote;
        oriente_ = false;
        ++partie_;
    }

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
        const PositionMachine m = position_avec_angles(machine_, cible, b_, c_);
        const double e = longueur * e_par_mm_;
        ligne(std::format("G1 X{:.3f} Y{:.3f} Z{:.3f} E{:.5f} F{:.0f}", m.x, m.y, m.z, e, vitesse * 60.0));
        noter(pointe_, cible, type_);
        filament_ += e;
        duree_ += longueur / vitesse;
        pointe_ = cible;
        position_ = p;
    }

    // Fin d'impression : rétraction, recul le long de la buse, puis montée.
    void degager(double hauteur) {
        retracter();
        deplacer(pointe_ + w_ * r_.levee_z);
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
        const auto cible = position_machine(machine_, {point(p, z_), w_}, c_, cote_);
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
        w_ancien_ = w_;
        return z_ + options_.approche;
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
    Vec3 w_ancien_{0, 0, 1};  // direction de la buse avant le changement de partie
    Vec3 pointe_;             // pointe de la buse, dans le repère de la pièce
    Point2 position_;         // la même, dans le plan des couches de la partie
    double z_local_ = 0.0;    // et sa hauteur dans le repère de la partie
    double z_ = 0.0;          // hauteur de la couche en cours
    double b_ = 0.0, c_ = 0.0;
    bool place_ = false;      // faux tant que la buse n'a pas été positionnée
    bool oriente_ = false;    // faux tant que la tête n'est pas dans l'orientation de la partie
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

}  // namespace

ResultatTranchage trancher_strategie(const Strategie& strategie, const Reglages& reglages,
                                     const OptionsMultiaxe& options) {
    if (strategie.parties.empty()) {
        throw ErreurTranchage("La stratégie ne contient aucune partie à imprimer.");
    }
    // Une seule partie verticale : c'est le slicer 3 axes.
    if (strategie.parties.size() == 1 && strategie.parties.front().direction.z > 1.0 - 1e-9) {
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
        for (std::size_t c = 0; c < t.couches.size(); ++c) {
            CoucheTrajectoires& couche = t.couches[c];
            ecrivain.ligne(std::format(";COUCHE:{}", ++numero));
            ecrivain.ligne(std::format(";Z:{:.3f}", couche.z));
            if (numero == 2 && r.ventilation) {
                ecrivain.ligne("M106 S255 ; ventilateur de pièce");
            }
            ecrivain.couche(couche.z, couche.hauteur);
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
