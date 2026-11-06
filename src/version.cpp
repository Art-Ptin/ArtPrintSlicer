// ArtPrint Slicer - licence AGPL v3
#include "version.hpp"

namespace artprint {

std::string_view version() {
    return ARTPRINT_VERSION;
}

std::string_view presentation() {
    return "ArtPrint Slicer, licence AGPL v3";
}

}  // namespace artprint
