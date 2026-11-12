// ArtPrint Slicer - licence AGPL v3
// Couches courbes : découpe d'une partie du modèle par les surfaces d'égale
// longueur de chemin (voir champ_couches.hpp).
//
// Chaque couche est une surface courbe. Pour y tracer les parois et le
// remplissage avec les outils du slicer (qui travaillent à plat), la couche
// est vue « d'en face » : on la regarde le long de sa direction moyenne et on
// dessine son contour sur un plan, sa « carte ». Les trajectoires sont
// calculées sur la carte, puis chaque point est reposé sur la surface.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "champ_couches.hpp"
#include "geometrie.hpp"
#include "maillage.hpp"
#include "polygones.hpp"

namespace artprint {

struct CoucheCourbe {
    std::size_t niveau = 0;  // numéro de la couche dans tout le modèle
    // La carte : un plan (origine, u, v) regardé le long de w, la direction
    // moyenne de la couche.
    Vec3 origine, u{1, 0, 0}, v{0, 1, 0}, w{0, 0, 1};
    Polygones contour;       // contour de la matière, dessiné sur la carte
    // Plus grand angle, en degrés, entre la surface et le plan de la carte.
    double bombement = 0.0;
};

// Tout ce qu'il faut pour imprimer les branches d'un modèle en couches courbes.
struct PlanBranches {
    AnalyseBranches analyse;
    // Pour chaque couche courbe : la longueur de chemin de son dessus, et son épaisseur.
    std::vector<double> dessus, epaisseurs;
    // Les couches de chaque tige, dans l'ordre des tiges de l'analyse. Une
    // tige sans couche (trop petite) a une liste vide.
    std::vector<std::vector<CoucheCourbe>> tiges;
    // Nombre de morceaux en surplomb qu'on n'a pas su découper en couches
    // courbes (trop bombés) : ils restent avec le tronc.
    std::size_t morceaux_laisses = 0;
    // Pour chaque branche rendue au tronc : une phrase qui dit pourquoi.
    std::vector<std::string> journal;

    bool vide() const {
        for (const std::vector<CoucheCourbe>& tige : tiges) {
            if (!tige.empty()) {
                return false;
            }
        }
        return true;
    }
};

// Analyse le modèle posé sur le plateau et découpe ses branches en couches
// courbes d'épaisseur « epaisseur ». Un morceau en surplomb qui ne peut pas
// être découpé ainsi (trop bombé, par exemple un chapeau tout autour d'un
// pied) est laissé avec le tronc. « pointe » : les pointes de couche plus
// étroites que deux fois cette valeur sont arrondies.
// « a_plat » : des points du modèle ; la branche qui contient chacun d'eux
// est imprimée à plat, avec le tronc (en couches horizontales), au lieu
// d'être découpée en couches courbes. Les branches qui la portent aussi ;
// celles qu'elle porte restent en couches courbes et se soudent sur elle.
// « inclinaison_max » : au-delà de cet angle depuis la verticale (en degrés),
// une couche regarde trop vers le bas pour la machine ; sa branche est rendue
// au tronc.
PlanBranches preparer_branches(const Maillage& pose, double epaisseur, double pointe,
                               const std::vector<Vec3>& a_plat = {}, double inclinaison_max = 100.0);

// Point de la surface de niveau « niveau » qui se trouve à l'aplomb du point
// (x, y) de la carte : la carte est quittée le long de w jusqu'à la surface.
// Rend « faux » si la surface n'est pas trouvée. « hauteur » : distance entre
// la carte et la surface ; en entrée, une première idée (celle du point
// précédent), en sortie la valeur trouvée.
bool poser_sur_la_surface(const ChampCouches& champ, const CoucheCourbe& couche, const Point2& p,
                          double niveau, double& hauteur, Vec3& point);
bool poser_sur_la_surface(const ChampCouches& champ, const CoucheCourbe& couche, const Point2& p,
                          double niveau, Vec3& point);

}  // namespace artprint
