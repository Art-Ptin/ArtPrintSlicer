// ArtPrint Slicer - licence AGPL v3
// Lecture des fichiers STL, en binaire et en texte (ASCII).
#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "maillage.hpp"

namespace artprint {

enum class FormatStl { binaire, ascii };

// Nom du format en français : « binaire » ou « texte (ASCII) ».
std::string_view nom_format(FormatStl format);

// Modèle chargé depuis un fichier STL.
struct ModeleStl {
    FormatStl format = FormatStl::binaire;
    Maillage maillage;                    // sommets fusionnés + triangles gardés
    std::size_t triangles_lus = 0;        // triangles présents dans le fichier
    std::size_t triangles_degeneres = 0;  // ignorés : deux sommets confondus
};

// Erreur de lecture, avec un message en français.
class ErreurStl : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Lit un fichier STL sur le disque. Le format est détecté tout seul.
// Lève ErreurStl si le fichier est absent ou invalide.
ModeleStl lire_stl(const std::filesystem::path& chemin);

// Écrit le maillage dans un fichier STL binaire. Lève ErreurStl si le
// fichier ne peut pas être écrit.
void ecrire_stl(const Maillage& maillage, const std::filesystem::path& chemin);

// Lit un STL déjà chargé en mémoire (utile pour les tests).
ModeleStl lire_stl_memoire(std::string_view donnees);

// Chemin converti en texte UTF-8, pour l'afficher.
std::string chemin_en_texte(const std::filesystem::path& chemin);

}  // namespace artprint
