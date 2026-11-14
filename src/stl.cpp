// ArtPrint Slicer - licence AGPL v3
//
// Format STL binaire :
//   80 octets d'en-tête libre, puis un entier 32 bits (petit-boutiste) donnant
//   le nombre de triangles, puis 50 octets par triangle : normale (3 flottants),
//   trois sommets (3 x 3 flottants), 2 octets d'attribut.
//
// Format STL texte :
//   solid nom
//     facet normal nx ny nz
//       outer loop
//         vertex x y z   (trois fois)
//       endloop
//     endfacet
//   endsolid nom
#include "stl.hpp"

#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>

namespace artprint {

namespace {

constexpr std::size_t taille_entete = 80;
constexpr std::size_t debut_triangles = 84;  // en-tête + nombre de triangles
constexpr std::size_t taille_triangle_binaire = 50;

static_assert(std::endian::native == std::endian::little,
              "La lecture binaire suppose une machine petit-boutiste (x86-64).");

float lire_float(const char* octets) {
    float valeur = 0.0f;
    std::memcpy(&valeur, octets, sizeof valeur);
    return valeur;
}

std::uint32_t lire_entier32(const char* octets) {
    std::uint32_t valeur = 0;
    std::memcpy(&valeur, octets, sizeof valeur);
    return valeur;
}

bool est_fini(const Point3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// Ajoute un triangle lu au modèle en cours de construction.
void ajouter_triangle(ConstructeurMaillage& constructeur, ModeleStl& modele,
                      const std::array<Point3, 3>& sommets) {
    ++modele.triangles_lus;
    for (const Point3& p : sommets) {
        if (!est_fini(p)) {
            throw ErreurStl(std::format(
                "Le triangle n°{} contient une coordonnée invalide (infinie ou non numérique).",
                modele.triangles_lus));
        }
    }
    if (!constructeur.ajouter_triangle(sommets[0], sommets[1], sommets[2])) {
        ++modele.triangles_degeneres;
    }
}

// Contrôles communs aux deux formats, une fois la lecture terminée.
ModeleStl terminer(ConstructeurMaillage& constructeur, ModeleStl modele) {
    modele.maillage = constructeur.terminer();
    if (modele.triangles_lus == 0) {
        throw ErreurStl("Le fichier STL ne contient aucun triangle.");
    }
    if (modele.maillage.triangles.empty()) {
        throw ErreurStl("Tous les triangles du fichier STL sont dégénérés (surface nulle).");
    }
    return modele;
}

// --- Binaire -----------------------------------------------------------------

ModeleStl lire_binaire(std::string_view donnees, std::uint32_t nombre_triangles) {
    ModeleStl modele;
    modele.format = FormatStl::binaire;
    ConstructeurMaillage constructeur;

    for (std::uint32_t i = 0; i < nombre_triangles; ++i) {
        const char* triangle = donnees.data() + debut_triangles + i * taille_triangle_binaire;
        const char* sommet = triangle + 12;  // on saute la normale (3 flottants)
        std::array<Point3, 3> sommets;
        for (Point3& p : sommets) {
            p = {lire_float(sommet), lire_float(sommet + 4), lire_float(sommet + 8)};
            sommet += 12;
        }
        ajouter_triangle(constructeur, modele, sommets);
    }
    return terminer(constructeur, std::move(modele));
}

// --- Texte (ASCII) -----------------------------------------------------------

bool egal_sans_casse(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

bool est_espace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Découpe le texte en mots séparés par des espaces, en comptant les lignes.
class Jetons {
public:
    explicit Jetons(std::string_view texte) : texte_(texte) {}

    std::optional<std::string_view> suivant() {
        while (position_ < texte_.size() && est_espace(texte_[position_])) {
            if (texte_[position_] == '\n') {
                ++ligne_;
            }
            ++position_;
        }
        if (position_ >= texte_.size()) {
            return std::nullopt;
        }
        ligne_jeton_ = ligne_;
        const std::size_t debut = position_;
        while (position_ < texte_.size() && !est_espace(texte_[position_])) {
            ++position_;
        }
        return texte_.substr(debut, position_ - debut);
    }

    // Saute la fin de la ligne en cours (le nom après « solid »).
    void sauter_fin_de_ligne() {
        while (position_ < texte_.size() && texte_[position_] != '\n') {
            ++position_;
        }
    }

    // Numéro de ligne du dernier mot lu.
    std::size_t ligne() const { return ligne_jeton_; }

private:
    std::string_view texte_;
    std::size_t position_ = 0;
    std::size_t ligne_ = 1;
    std::size_t ligne_jeton_ = 1;
};

[[noreturn]] void erreur_texte(const Jetons& jetons, std::string_view attendu,
                               std::optional<std::string_view> trouve) {
    if (!trouve) {
        throw ErreurStl(std::format(
            "Fichier STL texte incomplet : « {} » attendu, mais le fichier s'arrête.", attendu));
    }
    throw ErreurStl(std::format(
        "Fichier STL texte invalide, ligne {} : « {} » attendu, « {} » trouvé.",
        jetons.ligne(), attendu, *trouve));
}

void attendre(Jetons& jetons, std::string_view mot) {
    const auto jeton = jetons.suivant();
    if (!jeton || !egal_sans_casse(*jeton, mot)) {
        erreur_texte(jetons, mot, jeton);
    }
}

float lire_nombre(Jetons& jetons) {
    const auto jeton = jetons.suivant();
    if (!jeton) {
        erreur_texte(jetons, "un nombre", jeton);
    }
    std::string_view texte = *jeton;
    if (!texte.empty() && texte.front() == '+') {
        texte.remove_prefix(1);  // from_chars n'accepte pas le « + »
    }
    double valeur = 0.0;
    const char* fin = texte.data() + texte.size();
    const auto [arret, code] = std::from_chars(texte.data(), fin, valeur);
    if (code != std::errc{} || arret != fin) {
        erreur_texte(jetons, "un nombre", jeton);
    }
    return static_cast<float>(valeur);
}

ModeleStl lire_ascii(std::string_view donnees) {
    ModeleStl modele;
    modele.format = FormatStl::ascii;
    ConstructeurMaillage constructeur;
    Jetons jetons(donnees);

    attendre(jetons, "solid");
    jetons.sauter_fin_de_ligne();

    while (true) {
        const auto jeton = jetons.suivant();
        if (jeton && egal_sans_casse(*jeton, "endsolid")) {
            break;
        }
        if (!jeton || !egal_sans_casse(*jeton, "facet")) {
            erreur_texte(jetons, "facet ou endsolid", jeton);
        }

        attendre(jetons, "normal");
        for (int i = 0; i < 3; ++i) {
            lire_nombre(jetons);  // la normale est recalculée plus tard, on l'ignore
        }
        attendre(jetons, "outer");
        attendre(jetons, "loop");

        std::array<Point3, 3> sommets;
        for (Point3& p : sommets) {
            attendre(jetons, "vertex");
            p.x = lire_nombre(jetons);
            p.y = lire_nombre(jetons);
            p.z = lire_nombre(jetons);
        }

        attendre(jetons, "endloop");
        attendre(jetons, "endfacet");
        ajouter_triangle(constructeur, modele, sommets);
    }
    return terminer(constructeur, std::move(modele));
}

// Vrai si le texte commence par le mot « solid » (espaces et BOM UTF-8 ignorés).
bool commence_par_solid(std::string_view donnees) {
    if (donnees.starts_with("\xEF\xBB\xBF")) {
        donnees.remove_prefix(3);
    }
    std::size_t i = 0;
    while (i < donnees.size() && est_espace(donnees[i])) {
        ++i;
    }
    donnees.remove_prefix(i);
    if (donnees.size() < 5 || !egal_sans_casse(donnees.substr(0, 5), "solid")) {
        return false;
    }
    return donnees.size() == 5 || est_espace(donnees[5]);
}

}  // namespace

std::string_view nom_format(FormatStl format) {
    return format == FormatStl::binaire ? "binaire" : "texte (ASCII)";
}

std::string chemin_en_texte(const std::filesystem::path& chemin) {
    const std::u8string texte = chemin.u8string();
    return std::string(texte.begin(), texte.end());
}

ModeleStl lire_stl_memoire(std::string_view donnees) {
    // Un STL binaire a une taille exacte : 84 + 50 x nombre de triangles.
    // On teste cela en premier, car beaucoup de fichiers binaires commencent
    // aussi par le mot « solid » dans leur en-tête.
    std::uint32_t nombre_triangles = 0;
    std::uint64_t taille_attendue = 0;
    if (donnees.size() >= debut_triangles) {
        nombre_triangles = lire_entier32(donnees.data() + taille_entete);
        taille_attendue =
            debut_triangles + static_cast<std::uint64_t>(nombre_triangles) * taille_triangle_binaire;
        if (taille_attendue == donnees.size()) {
            return lire_binaire(donnees, nombre_triangles);
        }
    }

    if (commence_par_solid(donnees)) {
        return lire_ascii(donnees);
    }

    if (donnees.size() < debut_triangles) {
        throw ErreurStl(std::format(
            "Fichier trop court ({} octets) : ce n'est ni un STL binaire ni un STL texte.",
            donnees.size()));
    }
    throw ErreurStl(std::format(
        "Fichier STL binaire invalide : l'en-tête annonce {} triangles, soit {} octets, "
        "mais le fichier fait {} octets.",
        nombre_triangles, taille_attendue, donnees.size()));
}

ModeleStl lire_stl(const std::filesystem::path& chemin) {
    std::ifstream fichier(chemin, std::ios::binary);
    if (!fichier) {
        throw ErreurStl(std::format("Impossible d'ouvrir le fichier « {} ».",
                                    chemin_en_texte(chemin)));
    }
    const std::string donnees{std::istreambuf_iterator<char>(fichier),
                              std::istreambuf_iterator<char>()};
    if (fichier.bad()) {
        throw ErreurStl(std::format("Erreur pendant la lecture du fichier « {} ».",
                                    chemin_en_texte(chemin)));
    }
    return lire_stl_memoire(donnees);
}

void ecrire_stl(const Maillage& maillage, const std::filesystem::path& chemin) {
    std::ofstream fichier(chemin, std::ios::binary);
    char entete[80] = "ArtPrint Slicer";
    fichier.write(entete, sizeof entete);
    const auto nombre = static_cast<std::uint32_t>(maillage.triangles.size());
    fichier.write(reinterpret_cast<const char*>(&nombre), 4);
    for (const Triangle& triangle : maillage.triangles) {
        float valeurs[12];
        const Point3& a = maillage.sommets[triangle.sommets[0]];
        const Point3& b = maillage.sommets[triangle.sommets[1]];
        const Point3& c = maillage.sommets[triangle.sommets[2]];
        // Normale de la face (longueur 1, ou nulle pour un triangle plat).
        const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const double longueur = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (longueur > 0.0) {
            nx /= longueur, ny /= longueur, nz /= longueur;
        }
        valeurs[0] = static_cast<float>(nx), valeurs[1] = static_cast<float>(ny), valeurs[2] = static_cast<float>(nz);
        const Point3* sommets[3] = {&a, &b, &c};
        for (int i = 0; i < 3; ++i) {
            valeurs[3 + 3 * i] = static_cast<float>(sommets[i]->x);
            valeurs[4 + 3 * i] = static_cast<float>(sommets[i]->y);
            valeurs[5 + 3 * i] = static_cast<float>(sommets[i]->z);
        }
        fichier.write(reinterpret_cast<const char*>(valeurs), sizeof valeurs);
        const std::uint16_t attribut = 0;
        fichier.write(reinterpret_cast<const char*>(&attribut), 2);
    }
    if (!fichier) {
        throw ErreurStl(std::format("Impossible d'écrire le fichier « {} ».", chemin_en_texte(chemin)));
    }
}

}  // namespace artprint
