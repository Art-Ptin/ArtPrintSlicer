// ArtPrint Slicer - licence AGPL v3
// Stratégies d'impression : plusieurs façons d'imprimer le même modèle.
//
//   - « classique » : tout le modèle en couches horizontales, tête verticale ;
//   - « autre pose » : le modèle entier couché ou retourné sur le plateau,
//     dans la position qui demande le moins de supports ;
//   - « par branches » : le modèle est découpé en morceaux (le tronc, puis
//     chaque branche) et chaque morceau est imprimé dans sa propre
//     direction, la tête inclinée (B) et le plateau tourné (C).
//
// Pour chaque stratégie, on mesure la surface « en surplomb » : celle qui
// serait imprimée dans le vide et demanderait des supports.
#pragma once

#include <string>
#include <vector>

#include "geometrie.hpp"
#include "maillage.hpp"
#include "reglages.hpp"

namespace artprint {

// Un point de la courbe que suivent les couches d'une partie courbe.
struct PointGuide {
    Vec3 point;      // sur l'axe de la branche
    Vec3 direction;  // direction d'impression à cet endroit, longueur 1
};

// Un morceau du modèle et la direction dans laquelle il est imprimé.
struct PartieImpression {
    Maillage maillage;         // volume fermé, dans le repère de la pièce
    Vec3 direction{0.0, 0.0, 1.0};  // direction d'impression : les couches s'empilent le long
    Vec3 base;                 // un point de la surface de départ
    // Pour une partie courbe : la courbe que suivent ses couches, du départ
    // vers le bout. Chaque couche est alors perpendiculaire à la direction
    // de la courbe à cet endroit : la tête et le plateau tournent un peu à
    // chaque couche. Vide : toutes les couches ont la direction ci-dessus.
    std::vector<PointGuide> guide;
    int parent = -1;           // numéro de la partie qui la porte (-1 : le plateau)
    double inclinaison = 0.0;  // angle avec la verticale, en degrés (le plus grand le long d'une partie courbe)
    // Côté vers lequel la tête penche pour cette partie : 0 laisse le slicer
    // choisir ; +1 ou -1 l'impose (choisi pour éviter une collision).
    int cote = 0;
    double volume = 0.0;       // mm³
    double surplomb = 0.0;     // mm² à soutenir quand on l'imprime dans sa direction
    // Vrai : la partie est une branche imprimée en couches courbes, contre la
    // partie qui la porte (voir champ_couches.hpp). Son maillage est vide :
    // ses couches sont recalculées à partir du modèle entier.
    bool couches_courbes = false;
    // Vrai pour le tronc de la stratégie en couches courbes : le maillage est
    // le modèle entier, mais seul ce qui tient debout tout seul est imprimé
    // ici ; les branches sont retirées de ses couches (voir champ_couches.hpp).
    bool tronc_lisse = false;
    // En couches courbes : plus grande inclinaison donnée à la tête, en
    // degrés (celle qui a été trouvée sans collision pour cette partie).
    double inclinaison_outil_max = 180.0;  // (la limite de la machine s'applique toujours)
    // Couches courbes : un point à l'intérieur de la branche, qui sert à la
    // désigner (pour demander qu'elle soit imprimée à plat, par exemple).
    Vec3 repere;
};

enum class TypeStrategie { classique, autre_pose, par_branches, couches_courbes };

struct Strategie {
    TypeStrategie type = TypeStrategie::classique;
    std::string nom;
    std::string description;
    std::vector<PartieImpression> parties;  // dans l'ordre d'impression
    double surplomb = 0.0;                  // mm² à soutenir, toutes parties réunies
    std::vector<std::string> remarques;     // limites et avertissements, en français
    // Pour les couches courbes : le modèle entier, posé sur le plateau. Le
    // chemin de la matière se calcule sur tout le modèle, pas partie par partie.
    Maillage modele;
    // Couches courbes : les branches imprimées à plat avec le tronc, à la
    // demande (chacune désignée par un point à l'intérieur d'elle).
    std::vector<Vec3> a_plat;
};

struct OptionsStrategie {
    // Angle de surplomb imprimable sans support, mesuré depuis la verticale.
    double surplomb_max = 45.0;
    // Côté des voxels d'analyse en mm (0 : choisi d'après la taille du modèle).
    double pas_voxels = 0.0;
    // Vrai : une branche courbe devient une seule partie dont les couches
    // pivotent le long de la courbe. Faux : elle est partagée en tronçons
    // droits, chacun avec sa direction fixe.
    bool couches_pivotantes = true;
    // Faux (d'ordinaire) : seules deux stratégies sont proposées, la
    // classique (3 axes) et les couches courbes (5 axes continu). Vrai : les
    // anciennes stratégies à couches orientées (« autre pose », « par
    // branches ») sont proposées aussi ; elles ne servent plus qu'aux essais.
    bool couches_orientees = false;
    // Couches courbes : des points du modèle ; la branche qui contient
    // chacun d'eux est imprimée à plat, avec le tronc.
    std::vector<Vec3> a_plat;
};

// Surface (mm²) des faces du maillage qui seraient imprimées dans le vide en
// empilant les couches le long de « direction ». Les faces posées sur la
// surface de départ (à moins de « tolerance » mm de « base ») ne comptent pas.
// « matiere » (facultatif) : la matière de tout le modèle ; une face cachée
// à l'intérieur n'est pas comptée.
struct GrilleVoxels;
double surface_en_surplomb(const Maillage& maillage, const Vec3& direction, const Vec3& base,
                           double surplomb_max, double tolerance,
                           const GrilleVoxels* matiere = nullptr);

// Propose les stratégies possibles pour ce modèle posé sur le plateau
// (dessous à la hauteur zéro), de la plus simple à la plus élaborée.
// La première est toujours la stratégie classique.
std::vector<Strategie> proposer_strategies(const Maillage& pose, const Reglages& reglages,
                                           const OptionsStrategie& options = {});

// Une zone désignée par l'utilisateur : à partir du point choisi sur le
// modèle, la matière est imprimée dans une autre direction.
struct ZoneOrientee {
    Vec3 point;                // point choisi sur le modèle posé, en mm
    double inclinaison = 0.0;  // angle de la direction d'impression avec la verticale, en degrés
    double azimut = 0.0;       // côté vers lequel elle penche, en degrés (0 : vers +X, 90 : vers +Y)
};

// Direction d'impression d'une zone, longueur 1.
Vec3 direction_de_zone(const ZoneOrientee& zone);

// Stratégie « tout incliné » : la pièce entière est imprimée en couches
// penchées, posées sur le plateau, la tête inclinée du même angle du début à
// la fin (comme sur une imprimante à tapis roulant). Lève std::runtime_error
// (message en français) si la machine ne peut pas prendre cette inclinaison
// au ras du plateau.
Strategie strategie_inclinee(const Maillage& pose, const Reglages& reglages, double inclinaison, double azimut,
                             const OptionsStrategie& options = {});

// Stratégie « par zones » : le modèle est coupé, à chaque point choisi, par
// un plan perpendiculaire à la direction demandée. Ce qui est sous le plan
// est imprimé d'abord ; ce qui est au-dessus est imprimé ensuite, couche par
// couche le long de cette direction, la tête inclinée et le plateau tourné.
// Lève std::runtime_error (message en français) si une zone ne détache rien.
Strategie strategie_par_zones(const Maillage& pose, const Reglages& reglages,
                              const std::vector<ZoneOrientee>& zones, const OptionsStrategie& options = {});

}  // namespace artprint
