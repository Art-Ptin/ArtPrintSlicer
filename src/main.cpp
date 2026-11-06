// ArtPrint Slicer - licence AGPL v3
#include <iostream>

#include "version.hpp"

int main() {
    std::cout << artprint::presentation() << '\n';
    std::cout << "Version " << artprint::version() << '\n';
    return 0;
}
