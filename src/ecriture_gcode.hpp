// ArtPrint Slicer - licence AGPL v3
// Morceaux communs au slicer 3 axes et au slicer multi-directions : vitesse
// de chaque type de trait, ordre d'impression des traits d'une couche.
#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <vector>

#include "polygones.hpp"
#include "reglages.hpp"
#include "trajectoires.hpp"
#include "version.hpp"

namespace artprint {

inline double vitesse_du_type(const ReglagesImpression& r, TypeTrait type, bool premiere_couche) {
    if (premiere_couche) {
        return r.vitesse_premiere_couche;
    }
    switch (type) {
        case TypeTrait::paroi_externe:
            return r.vitesse_paroi_externe;
        case TypeTrait::paroi_interne:
        case TypeTrait::jupe:
            return r.vitesse_paroi_interne;
        case TypeTrait::remplissage_plein:
        case TypeTrait::remplissage:
            return r.vitesse_remplissage;
    }
    return r.vitesse_paroi_interne;
}

// Imprime les traits [debut, fin) d'un même type, en choisissant chaque fois
// le plus proche de la buse pour limiter les déplacements à vide.
template <class Ecrivain>
void ecrire_groupe(Ecrivain& ecrivain, std::vector<Trait>& traits, std::size_t debut,
                   std::size_t fin, double vitesse) {
    std::vector<bool> fait(fin - debut, false);
    for (std::size_t reste = fin - debut; reste > 0; --reste) {
        std::size_t choisi = 0;
        bool a_l_envers = false;
        double meilleure = std::numeric_limits<double>::max();
        for (std::size_t i = debut; i < fin; ++i) {
            if (fait[i - debut]) {
                continue;
            }
            const Trait& trait = traits[i];
            const double d_debut =
                ecrivain.place() ? distance(ecrivain.position(), trait.points.front()) : 0.0;
            if (d_debut < meilleure) {
                meilleure = d_debut, choisi = i, a_l_envers = false;
            }
            // Un trait ouvert peut être parcouru dans les deux sens.
            if (!trait.ferme && ecrivain.place()) {
                const double d_fin = distance(ecrivain.position(), trait.points.back());
                if (d_fin < meilleure) {
                    meilleure = d_fin, choisi = i, a_l_envers = true;
                }
            }
        }
        fait[choisi - debut] = true;
        Trait& trait = traits[choisi];
        if (a_l_envers) {
            std::reverse(trait.points.begin(), trait.points.end());
        }
        ecrivain.aller_a(trait.points.front());
        ecrivain.largeur_du_trait(trait.largeur);
        for (std::size_t i = 1; i < trait.points.size(); ++i) {
            ecrivain.extruder_vers(trait.points[i], vitesse);
        }
        if (trait.ferme) {
            ecrivain.extruder_vers(trait.points.front(), vitesse);
        }
    }
}

// Début du fichier. Contrat 1 : la première ligne annonce le contrat, puis
// une seule macro du firmware fait tout (prise d'origine de X Y Z B C,
// chauffe, positions absolues, extrusion relative). Contrat 0 : les commandes
// une à une, comme avant.
template <class Ecrivain>
void ecrire_entete(Ecrivain& ecrivain, const ReglagesImpression& r, std::size_t nombre_couches, int contrat = 0) {
    if (contrat >= 1) {
        ecrivain.ligne(std::format("; CONTRAT_GCODE: {}", contrat));
    }
    ecrivain.ligne(std::format("; Fichier produit par ArtPrint Slicer {}", version()));
    ecrivain.ligne(std::format("; Couches : {}   Hauteur de couche : {:.2f} mm   Largeur : {:.2f} mm",
                               nombre_couches, r.hauteur_couche, r.largeur_extrusion));
    if (contrat >= 1) {
        ecrivain.ligne(std::format("ARTPRINT_PRINT_START BUSE={:.0f} PLATEAU={:.0f}", r.temperature_buse,
                                   r.temperature_plateau));
        return;
    }
    ecrivain.ligne("G90 ; positions absolues");
    ecrivain.ligne("M83 ; extrusion relative");
    if (r.temperature_plateau > 0.0) {
        ecrivain.ligne(std::format("M140 S{:.0f} ; chauffe du plateau", r.temperature_plateau));
    }
    ecrivain.ligne(std::format("M104 S{:.0f} ; chauffe de la buse", r.temperature_buse));
    ecrivain.ligne("G28 ; prise d'origine");
    if (r.temperature_plateau > 0.0) {
        ecrivain.ligne(std::format("M190 S{:.0f} ; attente du plateau", r.temperature_plateau));
    }
    ecrivain.ligne(std::format("M109 S{:.0f} ; attente de la buse", r.temperature_buse));
    ecrivain.ligne("G92 E0");
}

// Fin du fichier : retrait et dégagement, puis l'arrêt. Contrat 1 : la macro
// du firmware relève la buse, redresse la tête, ramène le plateau à C 0 et
// coupe la chauffe.
template <class Ecrivain>
void ecrire_fin(Ecrivain& ecrivain, const ReglagesImpression& r, int contrat = 0) {
    ecrivain.ligne("; Fin de l'impression");
    ecrivain.degager(10.0);
    ecrivain.ligne("M107 ; arrêt du ventilateur");
    if (contrat >= 1) {
        ecrivain.ligne("ARTPRINT_PRINT_END");
        return;
    }
    ecrivain.ligne("M104 S0 ; arrêt de la chauffe de la buse");
    if (r.temperature_plateau > 0.0) {
        ecrivain.ligne("M140 S0 ; arrêt de la chauffe du plateau");
    }
    ecrivain.ligne("M84 ; moteurs libres");
}

}  // namespace artprint
