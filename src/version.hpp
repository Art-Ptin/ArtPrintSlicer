// ArtPrint Slicer - licence AGPL v3
#pragma once

#include <string_view>

namespace artprint {

// Numéro de version du slicer, par exemple "0.0.1".
std::string_view version();

// Nom du logiciel et licence, affichés au démarrage.
std::string_view presentation();

}  // namespace artprint
