// ArtPrint Slicer - licence AGPL v3
#include "squelette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <queue>
#include <utility>

namespace artprint {

namespace {

// Remplit la grille : un voxel est plein si son centre est dans la matière.
// Pour chaque colonne verticale, on note où elle traverse la surface, en
// entrant ou en sortant ; la matière est là où l'on est entré plus de fois
// que sorti. Cela reste juste quand plusieurs volumes se chevauchent.
GrilleVoxels remplir_interne(const Maillage& m, double pas) {
    const BoiteEnglobante boite = calculer_boite(m);
    GrilleVoxels g;
    g.pas = pas;
    g.origine = {boite.min.x - pas, boite.min.y - pas, boite.min.z - pas};
    const Point3 taille = boite.dimensions();
    g.nx = static_cast<int>(std::ceil(taille.x / pas)) + 2;
    g.ny = static_cast<int>(std::ceil(taille.y / pas)) + 2;
    g.nz = static_cast<int>(std::ceil(taille.z / pas)) + 2;
    g.plein.assign(static_cast<std::size_t>(g.nx) * g.ny * g.nz, 0);

    struct Passage {
        float z;
        std::int8_t sens;  // +1 : la colonne entre dans la matière ; -1 : elle en sort
    };
    std::vector<std::vector<Passage>> colonnes(static_cast<std::size_t>(g.nx) * g.ny);
    // Les colonnes sont très légèrement décalées du centre des voxels : elles
    // ne tombent ainsi jamais pile sur une arête ou un sommet du modèle.
    const double dx = pas * 3.713e-4, dy = pas * 6.181e-4;
    for (const Triangle& t : m.triangles) {
        const Point3& a = m.sommets[t.sommets[0]];
        const Point3& b = m.sommets[t.sommets[1]];
        const Point3& c = m.sommets[t.sommets[2]];
        const double aire = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) -
                            (static_cast<double>(c.x) - a.x) * (static_cast<double>(b.y) - a.y);
        if (std::abs(aire) < 1e-12) {
            continue;  // triangle vertical : aucune colonne ne le traverse
        }
        const auto colonne_de = [&](double valeur, double origine) {
            return static_cast<int>(std::floor((valeur - origine) / pas - 0.5));
        };
        const int i0 = std::max(0, colonne_de(std::min({a.x, b.x, c.x}), g.origine.x));
        const int i1 = std::min(g.nx - 1, colonne_de(std::max({a.x, b.x, c.x}), g.origine.x) + 1);
        const int j0 = std::max(0, colonne_de(std::min({a.y, b.y, c.y}), g.origine.y));
        const int j1 = std::min(g.ny - 1, colonne_de(std::max({a.y, b.y, c.y}), g.origine.y) + 1);
        for (int j = j0; j <= j1; ++j) {
            const double y = g.origine.y + (j + 0.5) * pas + dy;
            for (int i = i0; i <= i1; ++i) {
                const double x = g.origine.x + (i + 0.5) * pas + dx;
                // Coordonnées du point dans le triangle (barycentriques).
                const double wa = ((b.x - x) * (c.y - y) - (c.x - x) * (b.y - y)) / aire;
                const double wb = ((c.x - x) * (a.y - y) - (a.x - x) * (c.y - y)) / aire;
                const double wc = 1.0 - wa - wb;
                if (wa < 0.0 || wb < 0.0 || wc < 0.0) {
                    continue;
                }
                const double z = wa * a.z + wb * b.z + wc * c.z;
                // Triangle tourné vers le haut : en montant, la colonne sort de la matière.
                colonnes[static_cast<std::size_t>(j) * g.nx + i].push_back(
                    {static_cast<float>(z), static_cast<std::int8_t>(aire > 0.0 ? -1 : 1)});
            }
        }
    }
    for (int j = 0; j < g.ny; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            auto& passages = colonnes[static_cast<std::size_t>(j) * g.nx + i];
            std::sort(passages.begin(), passages.end(),
                      [](const Passage& p, const Passage& q) { return p.z < q.z; });
            int dedans = 0;
            for (std::size_t p = 0; p + 1 < passages.size(); ++p) {
                dedans += passages[p].sens;
                if (dedans <= 0) {
                    continue;
                }
                const int k0 = std::max(0, static_cast<int>(std::ceil((passages[p].z - g.origine.z) / pas - 0.5)));
                const int k1 = std::min(g.nz - 1, static_cast<int>(std::floor((passages[p + 1].z - g.origine.z) / pas - 0.5)));
                for (int k = k0; k <= k1; ++k) {
                    g.plein[g.indice(i, j, k)] = 1;
                }
            }
        }
    }
    return g;
}

}  // namespace

GrilleVoxels remplir_voxels(const Maillage& maillage, double pas) {
    return remplir_interne(maillage, pas);
}

double pas_voxels_conseille(const Maillage& maillage) {
    const Point3 taille = calculer_boite(maillage).dimensions();
    return std::clamp(static_cast<double>(std::max({taille.x, taille.y, taille.z})) / 140.0, 0.3, 2.5);
}

Squelette calculer_squelette(const Maillage& pose, double pas) {
    Squelette squelette;
    if (pose.triangles.empty()) {
        return squelette;
    }
    if (pas <= 0.0) {
        pas = pas_voxels_conseille(pose);
    }
    const GrilleVoxels g = remplir_interne(pose, pas);
    const std::size_t total = g.plein.size();
    squelette.pas = pas;
    squelette.epaisseur_tranche = 3.0 * pas;

    // Les 26 voisins d'un voxel, avec la distance entre les centres.
    struct Voisin {
        int di, dj, dk;
        float distance;
    };
    std::vector<Voisin> voisins;
    for (int dk = -1; dk <= 1; ++dk) {
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di || dj || dk) {
                    voisins.push_back({di, dj, dk, static_cast<float>(pas * std::sqrt(di * di + dj * dj + dk * dk))});
                }
            }
        }
    }
    const auto dans_grille = [&](int i, int j, int k) {
        return i >= 0 && j >= 0 && k >= 0 && i < g.nx && j < g.ny && k < g.nz;
    };

    // Chemin le plus court, dans la matière, depuis la base (la couche de
    // voxels pleins la plus basse).
    const float infini = std::numeric_limits<float>::max();
    std::vector<float> distance(total, infini);
    using Element = std::pair<float, std::uint32_t>;
    std::priority_queue<Element, std::vector<Element>, std::greater<Element>> file;
    int k_base = -1;
    for (int k = 0; k < g.nz && k_base < 0; ++k) {
        for (int j = 0; j < g.ny && k_base < 0; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                if (g.plein[g.indice(i, j, k)]) {
                    k_base = k;
                    break;
                }
            }
        }
    }
    if (k_base < 0) {
        return squelette;  // modèle trop fin pour la grille
    }
    for (int j = 0; j < g.ny; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            const std::size_t v = g.indice(i, j, k_base);
            if (g.plein[v]) {
                distance[v] = 0.0f;
                file.push({0.0f, static_cast<std::uint32_t>(v)});
            }
        }
    }
    const auto coordonnees = [&](std::size_t v, int& i, int& j, int& k) {
        i = static_cast<int>(v % g.nx);
        j = static_cast<int>((v / g.nx) % g.ny);
        k = static_cast<int>(v / (static_cast<std::size_t>(g.nx) * g.ny));
    };
    while (!file.empty()) {
        const auto [d, v] = file.top();
        file.pop();
        if (d > distance[v]) {
            continue;
        }
        int i, j, k;
        coordonnees(v, i, j, k);
        for (const Voisin& voisin : voisins) {
            const int a = i + voisin.di, b = j + voisin.dj, c = k + voisin.dk;
            if (!dans_grille(a, b, c)) {
                continue;
            }
            const std::size_t w = g.indice(a, b, c);
            if (g.plein[w] && d + voisin.distance < distance[w]) {
                distance[w] = d + voisin.distance;
                file.push({distance[w], static_cast<std::uint32_t>(w)});
            }
        }
    }

    // Tranches : les voxels à peu près à la même distance de la base. Chaque
    // morceau d'un seul tenant d'une tranche devient un « nœud ».
    const auto tranche_de = [&](std::size_t v) {
        return static_cast<int>(distance[v] / squelette.epaisseur_tranche);
    };
    struct Noeud {
        int tranche = 0;
        std::size_t voxels = 0;
        Vec3 somme;                 // somme des centres, pour le centre moyen
        int parent = -1;
        std::vector<int> enfants;
        double volume_total = 0.0;  // en voxels, avec les descendants
        int profondeur = 0;         // nombre de tranches portées au-dessus
    };
    std::vector<Noeud> noeuds;
    std::vector<int> noeud_de(total, -1);
    std::vector<std::uint32_t> pile;
    for (std::size_t depart = 0; depart < total; ++depart) {
        if (!g.plein[depart] || distance[depart] == infini || noeud_de[depart] >= 0) {
            continue;
        }
        const int numero = static_cast<int>(noeuds.size());
        Noeud noeud;
        noeud.tranche = tranche_de(depart);
        noeud_de[depart] = numero;
        pile.assign(1, static_cast<std::uint32_t>(depart));
        while (!pile.empty()) {
            const std::size_t v = pile.back();
            pile.pop_back();
            int i, j, k;
            coordonnees(v, i, j, k);
            ++noeud.voxels;
            noeud.somme = noeud.somme + g.centre(i, j, k);
            for (const Voisin& voisin : voisins) {
                const int a = i + voisin.di, b = j + voisin.dj, c = k + voisin.dk;
                if (!dans_grille(a, b, c)) {
                    continue;
                }
                const std::size_t w = g.indice(a, b, c);
                if (g.plein[w] && distance[w] != infini && noeud_de[w] < 0 &&
                    tranche_de(w) == noeud.tranche) {
                    noeud_de[w] = numero;
                    pile.push_back(static_cast<std::uint32_t>(w));
                }
            }
        }
        squelette.voxels += noeud.voxels;
        noeuds.push_back(std::move(noeud));
    }
    // Liens : un nœud est porté par le nœud de la tranche précédente qu'il touche.
    for (std::size_t v = 0; v < total; ++v) {
        const int numero = noeud_de[v];
        if (numero < 0) {
            continue;
        }
        int i, j, k;
        coordonnees(v, i, j, k);
        for (const Voisin& voisin : voisins) {
            const int a = i + voisin.di, b = j + voisin.dj, c = k + voisin.dk;
            if (!dans_grille(a, b, c)) {
                continue;
            }
            const int autre = noeud_de[g.indice(a, b, c)];
            if (autre < 0 || noeuds[autre].tranche != noeuds[numero].tranche - 1) {
                continue;
            }
            if (noeuds[numero].parent < 0) {
                noeuds[numero].parent = autre;
                noeuds[autre].enfants.push_back(numero);
            } else if (noeuds[numero].parent != autre) {
                squelette.boucles = true;  // deux chemins mènent au même endroit
            }
        }
    }
    // Volume porté et profondeur de chaque nœud, en remontant des extrémités.
    std::vector<int> ordre(noeuds.size());
    for (std::size_t n = 0; n < noeuds.size(); ++n) {
        ordre[n] = static_cast<int>(n);
    }
    std::sort(ordre.begin(), ordre.end(),
              [&](int a, int b) { return noeuds[a].tranche > noeuds[b].tranche; });
    for (const int n : ordre) {
        noeuds[n].volume_total += static_cast<double>(noeuds[n].voxels);
        if (noeuds[n].parent >= 0) {
            Noeud& parent = noeuds[noeuds[n].parent];
            parent.volume_total += noeuds[n].volume_total;
            parent.profondeur = std::max(parent.profondeur, noeuds[n].profondeur + 1);
        }
    }

    // Branches : on suit les nœuds tant qu'il n'y a qu'une suite possible.
    // Les petites excroissances (moins de trois tranches) ne comptent pas
    // comme des branches : leur matière reste dans la branche qui les porte.
    const double volume_voxel = pas * pas * pas;
    const auto compte = [&](int n) { return noeuds[n].profondeur >= 3 && noeuds[n].volume_total >= 20.0; };
    struct Depart {
        int noeud;
        int branche_mere;
    };
    std::vector<Depart> a_faire;
    for (std::size_t n = 0; n < noeuds.size(); ++n) {
        if (noeuds[n].tranche == 0 && noeuds[n].parent < 0) {
            a_faire.push_back({static_cast<int>(n), -1});
        }
    }
    for (std::size_t f = 0; f < a_faire.size(); ++f) {  // la liste grandit pendant le parcours
        const Depart depart = a_faire[f];
        const int numero = static_cast<int>(squelette.branches.size());
        BrancheSquelette branche;
        branche.parent = depart.branche_mere;
        int n = depart.noeud;
        for (;;) {
            double voxels = static_cast<double>(noeuds[n].voxels);
            std::vector<int> suites;
            for (const int enfant : noeuds[n].enfants) {
                if (compte(enfant)) {
                    suites.push_back(enfant);
                } else {
                    voxels += noeuds[enfant].volume_total;  // excroissance absorbée
                }
            }
            TrancheSquelette tranche;
            tranche.centre = noeuds[n].somme * (1.0 / static_cast<double>(noeuds[n].voxels));
            tranche.volume = voxels * volume_voxel;
            tranche.rayon = std::sqrt(static_cast<double>(noeuds[n].voxels) * volume_voxel /
                                      (std::numbers::pi * squelette.epaisseur_tranche));
            branche.tranches.push_back(tranche);
            if (suites.size() == 1) {
                n = suites.front();
                continue;
            }
            for (const int suite : suites) {
                a_faire.push_back({suite, numero});
            }
            break;
        }
        if (branche.parent >= 0) {
            squelette.branches[branche.parent].enfants.push_back(numero);
        }
        squelette.branches.push_back(std::move(branche));
    }

    // Au bout d'une branche, les dernières tranches sont souvent incomplètes
    // (bout arrondi, tube ouvert coupé de biais) : leur centre est décalé et
    // fausserait la direction. Leur matière est rattachée à la tranche d'avant.
    for (BrancheSquelette& branche : squelette.branches) {
        if (!branche.enfants.empty() || branche.tranches.size() < 4) {
            continue;
        }
        // On remonte depuis le bout tant que chaque tranche est nettement
        // plus petite que la précédente : ce sont les tranches incomplètes.
        std::size_t garde = branche.tranches.size();
        while (garde > 3 && branche.tranches[garde - 1].volume < 0.85 * branche.tranches[garde - 2].volume) {
            --garde;
        }
        for (std::size_t t = garde; t < branche.tranches.size(); ++t) {
            branche.tranches[garde - 1].volume += branche.tranches[t].volume;
        }
        branche.tranches.resize(garde);
    }

    // Une branche courbe est partagée en tronçons à peu près droits : dès que
    // sa direction a tourné de plus de « courbure_max », un nouveau tronçon
    // commence. Il devient une branche portée par le tronçon précédent.
    const double courbure_max = 25.0;
    const std::size_t fenetre = 2, longueur_min = 4;
    const auto tangente = [&](const BrancheSquelette& b, std::size_t t) {
        const std::size_t avant = t >= fenetre ? t - fenetre : 0;
        const std::size_t apres = std::min(t + fenetre, b.tranches.size() - 1);
        return normaliser(b.tranches[apres].centre - b.tranches[avant].centre);
    };
    for (std::size_t numero = 0; numero < squelette.branches.size(); ++numero) {
        const std::size_t n = squelette.branches[numero].tranches.size();
        if (n < 2 * longueur_min) {
            continue;
        }
        const Vec3 depart = tangente(squelette.branches[numero], std::min(fenetre, n - 1));
        std::size_t coupure = 0;
        for (std::size_t t = longueur_min; t + longueur_min <= n; ++t) {
            const double cosinus = std::clamp(produit_scalaire(tangente(squelette.branches[numero], t), depart), -1.0, 1.0);
            if (en_degres(std::acos(cosinus)) > courbure_max) {
                coupure = t;
                break;
            }
        }
        if (coupure == 0) {
            continue;
        }
        BrancheSquelette suite;
        suite.parent = static_cast<int>(numero);
        suite.suite_courbe = true;
        const int numero_suite = static_cast<int>(squelette.branches.size());
        {
            BrancheSquelette& b = squelette.branches[numero];
            suite.tranches.assign(b.tranches.begin() + static_cast<std::ptrdiff_t>(coupure), b.tranches.end());
            b.tranches.resize(coupure);
            suite.enfants = std::move(b.enfants);
            b.enfants = {numero_suite};
        }
        for (const int enfant : suite.enfants) {
            squelette.branches[enfant].parent = numero_suite;
        }
        squelette.branches.push_back(std::move(suite));  // sera examinée à son tour
    }

    // Mesures de chaque branche : longueur, direction, volume.
    for (BrancheSquelette& branche : squelette.branches) {
        branche.longueur = 0.0;
        branche.volume = 0.0;
        Vec3 somme{};
        for (std::size_t t = 0; t < branche.tranches.size(); ++t) {
            branche.volume += branche.tranches[t].volume;
            if (t > 0) {
                const Vec3 pas_centre = branche.tranches[t].centre - branche.tranches[t - 1].centre;
                branche.longueur += norme(pas_centre);
                somme = somme + pas_centre;
            }
        }
        branche.direction = normaliser(somme);
        if (norme(branche.direction) == 0.0) {
            // Une seule tranche : la direction de la branche qui la porte, ou la verticale.
            branche.direction = branche.parent >= 0 ? squelette.branches[branche.parent].direction
                                                    : Vec3{0.0, 0.0, 1.0};
        }
    }
    // Volume total de chaque branche, avec celles qu'elle porte.
    const std::function<double(int)> volume_total = [&](int numero) {
        BrancheSquelette& branche = squelette.branches[numero];
        branche.volume_total = branche.volume;
        for (const int enfant : branche.enfants) {
            branche.volume_total += volume_total(enfant);
        }
        return branche.volume_total;
    };
    for (std::size_t b = 0; b < squelette.branches.size(); ++b) {
        if (squelette.branches[b].parent < 0) {
            volume_total(static_cast<int>(b));
        }
    }
    return squelette;
}

}  // namespace artprint
