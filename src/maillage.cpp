// ArtPrint Slicer - licence AGPL v3
#include "maillage.hpp"

#include <algorithm>
#include <bit>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

namespace artprint {

BoiteEnglobante calculer_boite(const Maillage& maillage) {
    if (maillage.triangles.empty()) {
        throw std::invalid_argument(
            "Le maillage est vide : impossible de calculer la boîte englobante.");
    }

    const Point3& premier = maillage.sommets[maillage.triangles.front().sommets[0]];
    BoiteEnglobante boite{premier, premier};

    for (const Triangle& triangle : maillage.triangles) {
        for (std::uint32_t indice : triangle.sommets) {
            const Point3& p = maillage.sommets[indice];
            boite.min.x = std::min(boite.min.x, p.x);
            boite.min.y = std::min(boite.min.y, p.y);
            boite.min.z = std::min(boite.min.z, p.z);
            boite.max.x = std::max(boite.max.x, p.x);
            boite.max.y = std::max(boite.max.y, p.y);
            boite.max.z = std::max(boite.max.z, p.z);
        }
    }
    return boite;
}

// --- ConstructeurMaillage ---------------------------------------------------

std::size_t ConstructeurMaillage::HachageCle::operator()(const Cle& cle) const noexcept {
    // Hachage FNV-1a sur les bits des trois coordonnées.
    std::uint64_t h = 1469598103934665603ULL;
    for (float valeur : cle) {
        h ^= std::bit_cast<std::uint32_t>(valeur);
        h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h);
}

std::uint32_t ConstructeurMaillage::indice_sommet(const Point3& point) {
    // -0.0 et +0.0 désignent le même point : on les ramène à +0.0.
    auto normaliser = [](float v) { return v == 0.0f ? 0.0f : v; };
    const Cle cle{normaliser(point.x), normaliser(point.y), normaliser(point.z)};

    if (maillage_.sommets.size() >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Le modèle contient trop de sommets.");
    }

    const auto nouvel_indice = static_cast<std::uint32_t>(maillage_.sommets.size());
    const auto [position, insere] = index_.try_emplace(cle, nouvel_indice);
    if (insere) {
        maillage_.sommets.push_back({cle[0], cle[1], cle[2]});
    }
    return position->second;
}

bool ConstructeurMaillage::ajouter_triangle(const Point3& a, const Point3& b, const Point3& c) {
    const std::uint32_t ia = indice_sommet(a);
    const std::uint32_t ib = indice_sommet(b);
    const std::uint32_t ic = indice_sommet(c);

    if (ia == ib || ib == ic || ia == ic) {
        return false;  // triangle dégénéré : deux sommets confondus
    }
    maillage_.triangles.push_back(Triangle{{ia, ib, ic}});
    return true;
}

Maillage ConstructeurMaillage::terminer() {
    index_.clear();
    Maillage resultat = std::move(maillage_);
    maillage_ = Maillage{};
    return resultat;
}

// --- Contrôle de fermeture ----------------------------------------------------

namespace {

// Pour une arête {a, b} avec a < b : combien de triangles la parcourent
// de a vers b, et combien de b vers a.
struct ParcoursArete {
    std::uint32_t direct = 0;
    std::uint32_t inverse = 0;
};

std::string pluriel(std::size_t nombre, const char* singulier, const char* pluriel_texte) {
    return std::format("{} {}", nombre, nombre > 1 ? pluriel_texte : singulier);
}

}  // namespace

RapportFermeture verifier_fermeture(const Maillage& maillage) {
    RapportFermeture rapport;

    if (maillage.triangles.empty()) {
        rapport.message = "Le modèle ne contient aucun triangle.";
        return rapport;
    }

    std::unordered_map<std::uint64_t, ParcoursArete> aretes;
    aretes.reserve(maillage.triangles.size() * 3 / 2 + 1);

    for (const Triangle& triangle : maillage.triangles) {
        for (std::size_t i = 0; i < 3; ++i) {
            const std::uint32_t de = triangle.sommets[i];
            const std::uint32_t vers = triangle.sommets[(i + 1) % 3];
            const std::uint32_t petit = std::min(de, vers);
            const std::uint32_t grand = std::max(de, vers);
            const std::uint64_t cle = (static_cast<std::uint64_t>(petit) << 32) | grand;

            ParcoursArete& parcours = aretes[cle];
            if (de < vers) {
                ++parcours.direct;
            } else {
                ++parcours.inverse;
            }
        }
    }

    for (const auto& [cle, parcours] : aretes) {
        const std::uint32_t total = parcours.direct + parcours.inverse;
        if (total == 1) {
            ++rapport.aretes_bord;
        } else if (total > 2) {
            ++rapport.aretes_non_manifold;
        } else if (parcours.direct != 1) {
            // Deux triangles, mais qui parcourent l'arête dans le même sens.
            ++rapport.aretes_mal_orientees;
        }
    }

    rapport.ferme = rapport.aretes_bord == 0 && rapport.aretes_non_manifold == 0;
    rapport.orientation_coherente = rapport.aretes_mal_orientees == 0;

    if (rapport.ferme) {
        rapport.message = "Le modèle est fermé : aucun trou détecté.";
    } else {
        rapport.message = "Le modèle n'est pas fermé :";
        bool premier = true;
        auto ajouter = [&](const std::string& texte) {
            rapport.message += premier ? " " : " ; ";
            rapport.message += texte;
            premier = false;
        };
        if (rapport.aretes_bord > 0) {
            ajouter(pluriel(rapport.aretes_bord,
                            "arête de bord (trou dans la surface)",
                            "arêtes de bord (trous dans la surface)"));
        }
        if (rapport.aretes_non_manifold > 0) {
            ajouter(pluriel(rapport.aretes_non_manifold,
                            "arête partagée par plus de deux triangles",
                            "arêtes partagées par plus de deux triangles"));
        }
        rapport.message += '.';
    }

    if (!rapport.orientation_coherente) {
        rapport.message += std::format(
            " Attention : {} entre triangles voisins orientés en sens contraire.",
            pluriel(rapport.aretes_mal_orientees, "arête", "arêtes"));
    }

    return rapport;
}

}  // namespace artprint
