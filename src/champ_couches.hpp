// ArtPrint Slicer - licence AGPL v3
// Analyse des branches : sépare le modèle en un « tronc » et des « branches »,
// et prépare les couches courbes des branches.
//
// Le tronc est tout ce qui tient debout tout seul : la matière qui monte du
// plateau sans interruption, en colonnes droites (ou un peu penchées, si le
// tronc penche : jusqu'à 27° environ depuis la verticale). Il est imprimé en couches horizontales, sans
// aucune amorce de branche : sa surface reste lisse.
//
// Une branche est un morceau en surplomb, accroché au tronc. Elle est
// imprimée après lui, CONTRE lui : pour chaque point de la branche, on
// calcule la longueur du chemin le plus court, dans la branche, jusqu'à la
// surface du tronc. Les couches de la branche sont les surfaces d'égale
// longueur : la première épouse la surface du tronc, quelle que soit sa
// forme ; les suivantes s'en écartent d'une épaisseur de couche à chaque
// fois et se redressent peu à peu en travers de la branche.
//
// Là où une branche viendra se coller, le tronc est laissé « à nu » : sans
// paroi, son remplissage reste apparent, pour que la branche s'y soude bien.
//
// Une branche qui se divise est partagée en « tiges » : une tige va d'une
// fourche à la suivante.
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "geometrie.hpp"
#include "maillage.hpp"
#include "polygones.hpp"

namespace artprint {

// Une grandeur connue sur une grille de petits cubes autour de la matière.
struct ChampCouches {
    int nx = 0, ny = 0, nz = 0;
    double pas = 1.0;  // côté d'un cube de la grille, en mm
    Vec3 origine;      // coin de la grille
    // Longueur du chemin jusqu'à la surface du tronc, pour chaque cube :
    // positive dans une branche, négative juste sous la surface du tronc,
    // moins l'infini là où elle n'est pas calculée.
    std::vector<float> valeurs;
    double maximum = 0.0;  // plus grande longueur dans la matière

    std::size_t indice(int i, int j, int k) const {
        return (static_cast<std::size_t>(k) * ny + j) * nx + i;
    }
    bool defini(int i, int j, int k) const;
    // Valeur en un point quelconque, interpolée entre les cubes voisins.
    // Rend « faux » si le point est hors de la zone calculée.
    bool valeur(const Vec3& p, double& resultat) const;
    // Direction dans laquelle la longueur augmente le plus vite (la normale
    // à la couche), et vitesse de cette augmentation (1 dans l'idéal).
    bool gradient(const Vec3& p, Vec3& direction, double& pente) const;
};

struct AnalyseBranches {
    ChampCouches champ;
    // Pour chaque cube : le numéro de sa tige, ou -1 (tronc, vide).
    std::vector<std::int32_t> tige_des_cubes;
    // Pour chaque cube : 1 s'il fait partie de la zone des branches (leur
    // matière et une petite marge autour, hors du tronc).
    std::vector<std::uint8_t> zone_branches;
    // Pour chaque tige, dans l'ordre d'impression (une tige après celle qui
    // la porte) : sa tige mère (-1 : elle part du tronc), le numéro du
    // morceau en surplomb dont elle fait partie, son volume en mm³.
    std::vector<int> meres;
    std::vector<int> morceaux;
    std::vector<double> volumes;

    std::size_t nombre_tiges() const { return meres.size(); }
    // Numéro de la tige en ce point (celle du cube le plus proche), ou -1.
    int tige_en(const Vec3& p) const;
    // Zone occupée par les branches à la hauteur z, vue de dessus : c'est ce
    // qu'il faut retirer d'une couche plane pour ne garder que le tronc.
    Polygones zone_a(double z) const;
};

// Analyse un modèle fermé posé sur le plateau (dessous à la hauteur zéro).
// « pas » : côté d'un cube en mm ; 0 = choisi d'après la taille du modèle.
// « exclus » : numéros des morceaux en surplomb à laisser avec le tronc
// (ceux qu'on n'a pas su imprimer en couches courbes).
// « avec_le_tronc » (facultatif) : pour chaque cube de la grille, 1 si sa
// matière doit être imprimée avec le tronc, en couches horizontales, même si
// elle est en surplomb (une tige qu'on n'a pas su découper en couches
// courbes). Les branches qui en partent sont alors imprimées contre elle.
AnalyseBranches analyser_branches(const Maillage& pose, double pas = 0.0, const std::set<int>& exclus = {},
                                  const std::vector<std::uint8_t>* avec_le_tronc = nullptr);

}  // namespace artprint
