// ArtPrint Slicer - licence AGPL v3
// Lecture d'une ligne de G-code : la commande (G1, M83...) et ses paramètres.
#pragma once

#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace artprint {

// Une ligne de G-code découpée.
struct LigneGcode {
    // Lettre de la commande en majuscule ('G', 'M', 'T'), ou 0 s'il n'y en a
    // pas (ligne vide, commentaire seul, ou commande longue de Klipper comme
    // « SET_VELOCITY_LIMIT », que le vérificateur ignore).
    char lettre = 0;
    int numero = 0;  // 1 pour « G1 », 83 pour « M83 »

    // Renvoie la valeur du paramètre (lettre majuscule), s'il est présent.
    std::optional<double> parametre(char nom) const {
        return (nom >= 'A' && nom <= 'Z') ? parametres_[nom - 'A'] : std::nullopt;
    }

    std::array<std::optional<double>, 26> parametres_{};
};

// Erreur d'écriture dans une ligne de G-code, avec un message en français.
class ErreurGcode : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Découpe une ligne de G-code. Tout ce qui suit un « ; » est un commentaire.
LigneGcode lire_ligne_gcode(std::string_view ligne);

}  // namespace artprint
