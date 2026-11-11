// ArtPrint Slicer - licence AGPL v3
#include "reglages.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

#include "stl.hpp"  // chemin_en_texte

namespace artprint {

namespace {

std::string_view sans_espaces(std::string_view texte) {
    const auto blanc = " \t\r\n";
    const auto debut = texte.find_first_not_of(blanc);
    if (debut == std::string_view::npos) {
        return {};
    }
    const auto fin = texte.find_last_not_of(blanc);
    return texte.substr(debut, fin - debut + 1);
}

// Lit un nombre. La virgule française est acceptée : « 12,5 » vaut 12.5.
double lire_nombre(std::string_view cle, std::string_view valeur, int ligne) {
    std::string texte(valeur);
    for (char& c : texte) {
        if (c == ',') {
            c = '.';
        }
    }
    double nombre = 0.0;
    const char* debut = texte.data();
    const char* fin = texte.data() + texte.size();
    if (debut != fin && *debut == '+') {
        ++debut;
    }
    const auto resultat = std::from_chars(debut, fin, nombre);
    if (resultat.ec != std::errc() || resultat.ptr != fin || !std::isfinite(nombre)) {
        throw ErreurReglages(std::format(
            "Réglages, ligne {} : « {} » n'est pas un nombre valide pour « {} ».", ligne,
            valeur, cle));
    }
    return nombre;
}

double lire_positif(std::string_view cle, std::string_view valeur, int ligne) {
    const double nombre = lire_nombre(cle, valeur, ligne);
    if (nombre <= 0.0) {
        throw ErreurReglages(std::format(
            "Réglages, ligne {} : « {} » doit être plus grand que zéro.", ligne, cle));
    }
    return nombre;
}

int lire_sens(std::string_view cle, std::string_view valeur, int ligne) {
    const double nombre = lire_nombre(cle, valeur, ligne);
    if (nombre != 1.0 && nombre != -1.0) {
        throw ErreurReglages(
            std::format("Réglages, ligne {} : « {} » doit valoir 1 ou -1.", ligne, cle));
    }
    return static_cast<int>(nombre);
}

void appliquer(Reglages& r, std::string_view cle, std::string_view valeur, int ligne) {
    ReglagesMachine& m = r.machine;
    ReglagesVerification& v = r.verification;

    if (cle == "rayon_plateau") {
        m.rayon_plateau = lire_positif(cle, valeur, ligne);
    } else if (cle == "hauteur_impression") {
        m.hauteur_impression = lire_positif(cle, valeur, ligne);
    } else if (cle == "distance_pivot_buse") {
        m.distance_pivot_buse = lire_positif(cle, valeur, ligne);
    } else if (cle == "axe_pivot_b") {
        if (valeur == "x" || valeur == "X") {
            m.axe_pivot_b = AxePivot::x;
        } else if (valeur == "y" || valeur == "Y") {
            m.axe_pivot_b = AxePivot::y;
        } else {
            throw ErreurReglages(std::format(
                "Réglages, ligne {} : « axe_pivot_b » doit valoir X ou Y.", ligne));
        }
    } else if (cle == "sens_b") {
        m.sens_b = lire_sens(cle, valeur, ligne);
    } else if (cle == "sens_c") {
        m.sens_c = lire_sens(cle, valeur, ligne);
    } else if (cle == "centre_plateau_x") {
        m.centre_plateau_x = lire_nombre(cle, valeur, ligne);
    } else if (cle == "centre_plateau_y") {
        m.centre_plateau_y = lire_nombre(cle, valeur, ligne);
    } else if (cle == "b_min") {
        m.b_min = lire_nombre(cle, valeur, ligne);
    } else if (cle == "b_max") {
        m.b_max = lire_nombre(cle, valeur, ligne);
    } else if (cle == "b_limite_pres_plateau") {
        m.b_limite_pres_plateau = lire_positif(cle, valeur, ligne);
    } else if (cle == "hauteur_bascule_b") {
        m.hauteur_bascule_b = lire_nombre(cle, valeur, ligne);
    } else if (cle == "c_min") {
        m.c_min = lire_nombre(cle, valeur, ligne);
    } else if (cle == "c_max") {
        m.c_max = lire_nombre(cle, valeur, ligne);
    } else if (cle == "largeur_extrusion") {
        v.largeur_extrusion = lire_positif(cle, valeur, ligne);
    } else if (cle == "hauteur_couche") {
        v.hauteur_couche = lire_positif(cle, valeur, ligne);
    } else if (cle == "pas_controle") {
        v.pas_controle = lire_positif(cle, valeur, ligne);
    } else if (cle == "tolerance_croisement") {
        v.tolerance_croisement = lire_positif(cle, valeur, ligne);
    } else if (cle == "hauteur_controle_dessus") {
        v.hauteur_controle_dessus = lire_positif(cle, valeur, ligne);
    } else {
        throw ErreurReglages(
            std::format("Réglages, ligne {} : réglage inconnu « {} ».", ligne, cle));
    }
}

}  // namespace

Reglages lire_reglages_texte(std::string_view texte) {
    Reglages reglages;
    int numero = 0;
    while (!texte.empty()) {
        ++numero;
        const auto fin = texte.find('\n');
        std::string_view ligne = texte.substr(0, fin);
        texte = fin == std::string_view::npos ? std::string_view{} : texte.substr(fin + 1);

        ligne = sans_espaces(ligne.substr(0, ligne.find('#')));
        if (ligne.empty()) {
            continue;
        }
        const auto egal = ligne.find('=');
        if (egal == std::string_view::npos) {
            throw ErreurReglages(std::format(
                "Réglages, ligne {} : il manque le signe « = » (forme attendue : nom = valeur).",
                numero));
        }
        appliquer(reglages, sans_espaces(ligne.substr(0, egal)),
                  sans_espaces(ligne.substr(egal + 1)), numero);
    }

    if (reglages.machine.b_min > reglages.machine.b_max) {
        throw ErreurReglages("Réglages : « b_min » est plus grand que « b_max ».");
    }
    if (reglages.machine.c_min && reglages.machine.c_max &&
        *reglages.machine.c_min > *reglages.machine.c_max) {
        throw ErreurReglages("Réglages : « c_min » est plus grand que « c_max ».");
    }
    return reglages;
}

Reglages lire_reglages(const std::filesystem::path& chemin) {
    std::ifstream fichier(chemin, std::ios::binary);
    if (!fichier) {
        throw ErreurReglages(std::format("Impossible d'ouvrir le fichier de réglages « {} ».",
                                         chemin_en_texte(chemin)));
    }
    const std::string texte((std::istreambuf_iterator<char>(fichier)),
                            std::istreambuf_iterator<char>());
    return lire_reglages_texte(texte);
}

}  // namespace artprint
