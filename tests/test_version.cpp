// ArtPrint Slicer - licence AGPL v3
// Test simple : le programme renvoie 0 si tout est bon, 1 sinon.
#include <iostream>

#include "version.hpp"

int main() {
    int erreurs = 0;

    if (artprint::version() != "0.1.0") {
        std::cerr << "ÉCHEC : version attendue 0.1.0, obtenue "
                  << artprint::version() << '\n';
        ++erreurs;
    }

    if (artprint::presentation() != "ArtPrint Slicer, licence AGPL v3") {
        std::cerr << "ÉCHEC : texte de présentation inattendu : "
                  << artprint::presentation() << '\n';
        ++erreurs;
    }

    if (erreurs == 0) {
        std::cout << "Tous les tests de version sont passés.\n";
        return 0;
    }
    return 1;
}
