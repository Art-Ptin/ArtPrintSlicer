// ArtPrint Slicer - licence AGPL v3
#include "gcode.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace artprint {

namespace {

bool est_blanc(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool est_lettre(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

char en_majuscule(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

// Lit un nombre au début de « texte » et avance « texte » juste après.
std::optional<double> lire_nombre(std::string_view& texte) {
    const char* debut = texte.data();
    const char* fin = texte.data() + texte.size();
    if (debut != fin && *debut == '+') {
        ++debut;
    }
    double valeur = 0.0;
    const auto resultat = std::from_chars(debut, fin, valeur);
    if (resultat.ec != std::errc() || !std::isfinite(valeur)) {
        return std::nullopt;
    }
    texte.remove_prefix(static_cast<std::size_t>(resultat.ptr - texte.data()));
    return valeur;
}

void sauter_blancs(std::string_view& texte) {
    while (!texte.empty() && est_blanc(texte.front())) {
        texte.remove_prefix(1);
    }
}

}  // namespace

LigneGcode lire_ligne_gcode(std::string_view ligne) {
    LigneGcode resultat;
    std::string_view texte = ligne.substr(0, ligne.find(';'));
    sauter_blancs(texte);
    if (texte.empty()) {
        return resultat;
    }

    // Numéro de ligne facultatif (« N12 G1 ... ») : ignoré.
    if (en_majuscule(texte.front()) == 'N' && texte.size() > 1 &&
        std::isdigit(static_cast<unsigned char>(texte[1]))) {
        texte.remove_prefix(1);
        lire_nombre(texte);
        sauter_blancs(texte);
        if (texte.empty()) {
            return resultat;
        }
    }

    // Commande longue de Klipper (un mot entier) : ignorée.
    const char lettre = en_majuscule(texte.front());
    const bool commande_classique =
        (lettre == 'G' || lettre == 'M' || lettre == 'T') && texte.size() > 1 &&
        std::isdigit(static_cast<unsigned char>(texte[1]));
    if (!commande_classique) {
        if (est_lettre(texte.front()) && texte.size() > 1 &&
            (est_lettre(texte[1]) || texte[1] == '_')) {
            return resultat;
        }
        throw ErreurGcode(std::format("commande illisible : « {} »", texte));
    }

    texte.remove_prefix(1);
    const auto numero = lire_nombre(texte);
    if (!numero) {
        throw ErreurGcode(std::format("numéro de commande illisible après « {} »", lettre));
    }
    resultat.lettre = lettre;
    resultat.numero = static_cast<int>(*numero);

    // Paramètres : une lettre suivie d'un nombre, avec ou sans espaces.
    for (;;) {
        sauter_blancs(texte);
        if (texte.empty()) {
            break;
        }
        const char nom = en_majuscule(texte.front());
        if (nom < 'A' || nom > 'Z') {
            throw ErreurGcode(std::format("caractère inattendu « {} »", texte.front()));
        }
        texte.remove_prefix(1);
        sauter_blancs(texte);
        const auto valeur = lire_nombre(texte);
        if (!valeur) {
            // Paramètre sans valeur (« G28 X ») : accepté seulement en fin de mot.
            if (texte.empty() || est_blanc(texte.front())) {
                resultat.parametres_[nom - 'A'] = 0.0;
                continue;
            }
            throw ErreurGcode(std::format("valeur illisible pour le paramètre « {} »", nom));
        }
        resultat.parametres_[nom - 'A'] = *valeur;
    }
    return resultat;
}

}  // namespace artprint
