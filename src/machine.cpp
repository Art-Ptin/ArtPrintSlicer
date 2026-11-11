// ArtPrint Slicer - licence AGPL v3
//
// Format du fichier (nombres en petit-boutiste) :
//   « APM1 », nombre de groupes (entier 32 bits), puis pour chaque groupe :
//     nom, mouvement (1 octet), nombre de pièces, puis pour chaque pièce :
//       nom, nombre de triangles, 9 flottants par triangle ;
//   enfin 9 flottants : pivot B (3), direction du pivot (3), pointe de la buse (3).
//   Un nom : longueur sur 16 bits, puis le texte en UTF-8.
#include "machine.hpp"

#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>

#include "stl.hpp"  // chemin_en_texte

namespace artprint {

namespace {

template <class T>
T lire(std::istream& flux) {
    T valeur{};
    flux.read(reinterpret_cast<char*>(&valeur), sizeof valeur);
    if (!flux) {
        throw ErreurMachine("Le fichier du modèle de machine est incomplet.");
    }
    return valeur;
}

std::string lire_texte(std::istream& flux) {
    std::string texte(lire<std::uint16_t>(flux), '\0');
    flux.read(texte.data(), static_cast<std::streamsize>(texte.size()));
    if (!flux) {
        throw ErreurMachine("Le fichier du modèle de machine est incomplet.");
    }
    return texte;
}

template <class T>
void ecrire(std::ostream& flux, T valeur) {
    flux.write(reinterpret_cast<const char*>(&valeur), sizeof valeur);
}

void ecrire_texte(std::ostream& flux, const std::string& texte) {
    ecrire(flux, static_cast<std::uint16_t>(texte.size()));
    flux.write(texte.data(), static_cast<std::streamsize>(texte.size()));
}

Vec3 lire_vecteur(std::istream& flux) {
    const float x = lire<float>(flux), y = lire<float>(flux), z = lire<float>(flux);
    return {x, y, z};
}

}  // namespace

const GroupeMachine* ModeleMachine::groupe(Mouvement mouvement) const {
    for (const GroupeMachine& g : groupes) {
        if (g.mouvement == mouvement) {
            return &g;
        }
    }
    return nullptr;
}

ModeleMachine lire_modele_machine(const std::filesystem::path& chemin) {
    std::ifstream flux(chemin, std::ios::binary);
    if (!flux) {
        throw ErreurMachine(std::format("Impossible d'ouvrir le modèle de machine « {} ».",
                                        chemin_en_texte(chemin)));
    }
    char magie[4] = {};
    flux.read(magie, 4);
    if (!flux || std::memcmp(magie, "APM1", 4) != 0) {
        throw ErreurMachine(std::format(
            "« {} » n'est pas un modèle de machine ArtPrint (à fabriquer avec "
            "outils/convertir_machine.py).",
            chemin_en_texte(chemin)));
    }
    ModeleMachine modele;
    const auto nombre_groupes = lire<std::uint32_t>(flux);
    if (nombre_groupes > 64) {
        throw ErreurMachine("Le fichier du modèle de machine est abîmé.");
    }
    for (std::uint32_t g = 0; g < nombre_groupes; ++g) {
        GroupeMachine groupe;
        groupe.nom = lire_texte(flux);
        const auto code = lire<std::uint8_t>(flux);
        if (code > 5) {
            throw ErreurMachine("Le fichier du modèle de machine est abîmé.");
        }
        groupe.mouvement = static_cast<Mouvement>(code);
        const auto nombre_pieces = lire<std::uint32_t>(flux);
        for (std::uint32_t p = 0; p < nombre_pieces; ++p) {
            PieceMachine piece;
            piece.nom = lire_texte(flux);
            const auto triangles = lire<std::uint32_t>(flux);
            if (triangles > 50'000'000) {
                throw ErreurMachine("Le fichier du modèle de machine est abîmé.");
            }
            piece.triangles.resize(static_cast<std::size_t>(triangles) * 9);
            flux.read(reinterpret_cast<char*>(piece.triangles.data()),
                      static_cast<std::streamsize>(piece.triangles.size() * sizeof(float)));
            if (!flux) {
                throw ErreurMachine("Le fichier du modèle de machine est incomplet.");
            }
            groupe.pieces.push_back(std::move(piece));
        }
        modele.groupes.push_back(std::move(groupe));
    }
    modele.pivot_b = lire_vecteur(flux);
    modele.direction_pivot = lire_vecteur(flux);
    modele.pointe_buse = lire_vecteur(flux);
    return modele;
}

void ecrire_modele_machine(const std::filesystem::path& chemin, const ModeleMachine& modele) {
    std::ofstream flux(chemin, std::ios::binary);
    flux.write("APM1", 4);
    ecrire(flux, static_cast<std::uint32_t>(modele.groupes.size()));
    for (const GroupeMachine& groupe : modele.groupes) {
        ecrire_texte(flux, groupe.nom);
        ecrire(flux, static_cast<std::uint8_t>(groupe.mouvement));
        ecrire(flux, static_cast<std::uint32_t>(groupe.pieces.size()));
        for (const PieceMachine& piece : groupe.pieces) {
            ecrire_texte(flux, piece.nom);
            ecrire(flux, static_cast<std::uint32_t>(piece.triangles.size() / 9));
            flux.write(reinterpret_cast<const char*>(piece.triangles.data()),
                       static_cast<std::streamsize>(piece.triangles.size() * sizeof(float)));
        }
    }
    for (const Vec3& v : {modele.pivot_b, modele.direction_pivot, modele.pointe_buse}) {
        ecrire(flux, static_cast<float>(v.x));
        ecrire(flux, static_cast<float>(v.y));
        ecrire(flux, static_cast<float>(v.z));
    }
    if (!flux) {
        throw ErreurMachine(std::format("Impossible d'écrire le modèle de machine « {} ».",
                                        chemin_en_texte(chemin)));
    }
}

}  // namespace artprint
