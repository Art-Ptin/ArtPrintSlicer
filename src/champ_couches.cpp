// ArtPrint Slicer - licence AGPL v3
#include "champ_couches.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

#include "squelette.hpp"

namespace artprint {

namespace {

constexpr float hors_zone = -std::numeric_limits<float>::infinity();
// Marge de cubes vides autour du modèle, et épaisseur de la bande calculée
// autour de la matière (pour interpoler jusqu'à la surface et un peu au-delà).
constexpr int marge = 4;
constexpr int bande = 2;

}  // namespace

bool ChampCouches::defini(int i, int j, int k) const {
    return i >= 0 && j >= 0 && k >= 0 && i < nx && j < ny && k < nz &&
           valeurs[indice(i, j, k)] != hors_zone;
}

bool ChampCouches::valeur(const Vec3& p, double& resultat) const {
    const double gx = (p.x - origine.x) / pas - 0.5;
    const double gy = (p.y - origine.y) / pas - 0.5;
    const double gz = (p.z - origine.z) / pas - 0.5;
    const int i0 = static_cast<int>(std::floor(gx));
    const int j0 = static_cast<int>(std::floor(gy));
    const int k0 = static_cast<int>(std::floor(gz));
    const double fx = gx - i0, fy = gy - j0, fz = gz - k0;
    double somme = 0.0, poids = 0.0;
    for (int dk = 0; dk <= 1; ++dk) {
        for (int dj = 0; dj <= 1; ++dj) {
            for (int di = 0; di <= 1; ++di) {
                if (!defini(i0 + di, j0 + dj, k0 + dk)) {
                    continue;
                }
                const double w = (di ? fx : 1.0 - fx) * (dj ? fy : 1.0 - fy) * (dk ? fz : 1.0 - fz);
                somme += w * valeurs[indice(i0 + di, j0 + dj, k0 + dk)];
                poids += w;
            }
        }
    }
    // Il faut la plupart des huit cubes voisins : sinon l'interpolation
    // serait faussée par ceux qui manquent.
    if (poids < 0.85) {
        return false;
    }
    resultat = somme / poids;
    return true;
}

bool ChampCouches::gradient(const Vec3& p, Vec3& direction, double& pente) const {
    const double h = 0.75 * pas;
    Vec3 g;
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    double ici = 0.0;
    const bool centre = valeur(p, ici);
    for (int a = 0; a < 3; ++a) {
        double plus = 0.0, moins = 0.0;
        const bool a_plus = valeur(p + axes[a] * h, plus);
        const bool a_moins = valeur(p - axes[a] * h, moins);
        double d = 0.0;
        if (a_plus && a_moins) {
            d = (plus - moins) / (2.0 * h);
        } else if (a_plus && centre) {
            d = (plus - ici) / h;
        } else if (a_moins && centre) {
            d = (ici - moins) / h;
        } else {
            return false;
        }
        (a == 0 ? g.x : a == 1 ? g.y : g.z) = d;
    }
    pente = norme(g);
    if (pente < 1e-6) {
        return false;
    }
    direction = g * (1.0 / pente);
    return true;
}

int AnalyseBranches::tige_en(const Vec3& p) const {
    const int i = static_cast<int>(std::floor((p.x - champ.origine.x) / champ.pas));
    const int j = static_cast<int>(std::floor((p.y - champ.origine.y) / champ.pas));
    const int k = static_cast<int>(std::floor((p.z - champ.origine.z) / champ.pas));
    // Le cube du point, sinon le cube de tige le plus proche autour.
    int meilleure = -1;
    int plus_pres = 1 << 30;
    for (int dk = -2; dk <= 2; ++dk) {
        for (int dj = -2; dj <= 2; ++dj) {
            for (int di = -2; di <= 2; ++di) {
                const int a = i + di, b = j + dj, c = k + dk;
                if (a < 0 || b < 0 || c < 0 || a >= champ.nx || b >= champ.ny || c >= champ.nz) {
                    continue;
                }
                const std::int32_t tige = tige_des_cubes[champ.indice(a, b, c)];
                const int d = di * di + dj * dj + dk * dk;
                if (tige >= 0 && d < plus_pres) {
                    plus_pres = d;
                    meilleure = tige;
                }
            }
        }
    }
    return meilleure;
}

Polygones AnalyseBranches::zone_a(double z) const {
    Polygones bandes;
    const int k = static_cast<int>(std::floor((z - champ.origine.z) / champ.pas));
    if (k < 0 || k >= champ.nz) {
        return bandes;
    }
    const double pas = champ.pas;
    // Les cubes concernés à cette hauteur : ceux de la zone, et leurs voisins.
    int i_min = champ.nx, i_max = -1, j_min = champ.ny, j_max = -1;
    for (int dk = -1; dk <= 1; ++dk) {
        if (k + dk < 0 || k + dk >= champ.nz) {
            continue;
        }
        for (int j = 0; j < champ.ny; ++j) {
            for (int i = 0; i < champ.nx; ++i) {
                if (zone_branches[champ.indice(i, j, k + dk)]) {
                    i_min = std::min(i_min, i), i_max = std::max(i_max, i);
                    j_min = std::min(j_min, j), j_max = std::max(j_max, j);
                }
            }
        }
    }
    if (i_max < 0) {
        return bandes;
    }
    i_min = std::max(0, i_min - 1), j_min = std::max(0, j_min - 1);
    i_max = std::min(champ.nx - 1, i_max + 1), j_max = std::min(champ.ny - 1, j_max + 1);
    // La limite entre le tronc et une branche est la surface où la longueur
    // de chemin vaut zéro : c'est sur elle que se pose la première couche de
    // la branche. Elle est cherchée plus finement que les cubes, pour que le
    // tronc et la branche s'emboîtent exactement.
    const int n = std::clamp(static_cast<int>(std::ceil(pas / 0.12)), 1, 8);
    const double fin = pas / n;
    const auto dans_la_zone = [&](int a, int b) {
        const Vec3 p{champ.origine.x + (a + 0.5) * fin, champ.origine.y + (b + 0.5) * fin, z};
        double v = 0.0;
        if (champ.valeur(p, v)) {
            return v > 0.0;
        }
        return zone_branches[champ.indice(a / n, b / n, k)] != 0;
    };
    for (int b = j_min * n; b < (j_max + 1) * n; ++b) {
        for (int a = i_min * n; a < (i_max + 1) * n;) {
            if (!dans_la_zone(a, b)) {
                ++a;
                continue;
            }
            int dernier = a;
            while (dernier + 1 < (i_max + 1) * n && dans_la_zone(dernier + 1, b)) {
                ++dernier;
            }
            const double xa = champ.origine.x + a * fin, xb = champ.origine.x + (dernier + 1) * fin;
            const double ya = champ.origine.y + b * fin, yb = ya + fin;
            bandes.push_back({en_unites(Point2{xa, ya}), en_unites(Point2{xb, ya}), en_unites(Point2{xb, yb}),
                              en_unites(Point2{xa, yb})});
            a = dernier + 1;
        }
    }
    if (bandes.empty()) {
        return bandes;
    }
    // Le petit escalier qui reste est arrondi (gonflé puis dégonflé). La
    // zone garde un peu plus d'un dixième de millimètre d'avance sur le tronc : mieux vaut
    // ce petit jeu entre le tronc et la branche qu'un recouvrement.
    const double jeu = 0.15;
    const Polygones zone = Clipper2Lib::Union(bandes, Clipper2Lib::FillRule::NonZero);
    return decaler_arrondi(decaler_arrondi(zone, fin + jeu), -fin);
}

AnalyseBranches analyser_branches(const Maillage& pose, double pas, const std::set<int>& exclus,
                                  const std::vector<std::uint8_t>* avec_le_tronc) {
    AnalyseBranches analyse;
    ChampCouches& champ = analyse.champ;
    if (pose.triangles.empty()) {
        return analyse;
    }
    if (pas <= 0.0) {
        const Point3 taille = calculer_boite(pose).dimensions();
        pas = std::clamp(static_cast<double>(std::max({taille.x, taille.y, taille.z})) / 200.0, 0.3, 1.2);
    }
    const GrilleVoxels g = remplir_voxels(pose, pas);
    champ.pas = pas;
    champ.nx = g.nx + 2 * marge, champ.ny = g.ny + 2 * marge, champ.nz = g.nz + 2 * marge;
    champ.origine = g.origine - Vec3{marge * pas, marge * pas, marge * pas};
    const int nx = champ.nx, ny = champ.ny, nz = champ.nz;
    const std::size_t total = static_cast<std::size_t>(nx) * ny * nz;
    const auto indice = [&](int i, int j, int k) { return champ.indice(i, j, k); };
    const auto dedans = [&](int i, int j, int k) { return i >= 0 && j >= 0 && k >= 0 && i < nx && j < ny && k < nz; };
    const auto coordonnees = [&](std::size_t n, int& i, int& j, int& k) {
        i = static_cast<int>(n % nx);
        j = static_cast<int>((n / nx) % ny);
        k = static_cast<int>(n / (static_cast<std::size_t>(nx) * ny));
    };
    struct Voisin {
        int di, dj, dk;
    };
    std::vector<Voisin> voisins;
    for (int dk = -1; dk <= 1; ++dk) {
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di || dj || dk) {
                    voisins.push_back({di, dj, dk});
                }
            }
        }
    }

    std::vector<std::uint8_t> matiere(total, 0);
    for (int k = 0; k < g.nz; ++k) {
        for (int j = 0; j < g.ny; ++j) {
            for (int i = 0; i < g.nx; ++i) {
                if (g.plein[g.indice(i, j, k)]) {
                    matiere[indice(i + marge, j + marge, k + marge)] = 1;
                }
            }
        }
    }
    const auto hauteur = [&](int k) { return champ.origine.z + (k + 0.5) * pas; };

    // 1. Le tronc : la matière qui monte du plateau sans interruption, en
    // colonnes. D'ordinaire les colonnes sont verticales. Mais un tronc peut
    // pencher un peu : on essaie donc aussi des colonnes penchées (jusqu'à
    // 27° environ, une pente qu'une impression en couches horizontales tient
    // sans peine), et on garde la direction qui donne le plus gros tronc,
    // si elle fait nettement mieux que la verticale. Une seule direction
    // pour tout le modèle : une branche, plus inclinée, n'en fait pas partie.
    // Une colonne penchée se décale d'un cube toutes les « periode » couches.
    const auto colonnes = [&](int di, int dj, int periode, std::vector<std::uint8_t>& resultat) {
        resultat.assign(total, 0);
        std::size_t volume = 0;
        for (int k = 0; k < nz; ++k) {
            // Le cube du dessous dans la même colonne penchée.
            const bool decale = periode > 0 && k % periode == 0;
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    if (!matiere[indice(i, j, k)]) {
                        continue;
                    }
                    bool porte = hauteur(k) <= 1.5 * pas;
                    if (!porte && k > 0) {
                        const int a = decale ? i - di : i, b = decale ? j - dj : j;
                        porte = dedans(a, b, k - 1) && resultat[indice(a, b, k - 1)];
                    }
                    resultat[indice(i, j, k)] = porte;
                    volume += porte;
                }
            }
        }
        return volume;
    };
    std::vector<std::uint8_t> tronc;
    std::size_t volume_tronc = colonnes(0, 0, 0, tronc);
    {
        std::vector<std::uint8_t> essai;
        const std::size_t volume_droit = volume_tronc;
        for (const int periode : {2, 3, 4, 6}) {
            for (int dj = -1; dj <= 1; ++dj) {
                for (int di = -1; di <= 1; ++di) {
                    if (di == 0 && dj == 0) {
                        continue;
                    }
                    const std::size_t volume = colonnes(di, dj, periode, essai);
                    // (Il faut un gain net : au moins 15 % de plus que la verticale.)
                    if (volume > volume_tronc && static_cast<double>(volume) > 1.15 * static_cast<double>(volume_droit)) {
                        volume_tronc = volume;
                        tronc.swap(essai);
                    }
                }
            }
        }
    }

    // Et la matière qu'on a demandé d'imprimer avec lui.
    if (avec_le_tronc && avec_le_tronc->size() == total) {
        for (std::size_t n = 0; n < total; ++n) {
            if ((*avec_le_tronc)[n] && matiere[n]) {
                tronc[n] = 1;
            }
        }
    }

    // 2. Les morceaux en surplomb : la matière hors du tronc, groupée par
    // morceaux d'un seul tenant. Pour chacun : son volume, et sa profondeur
    // (le nombre de cubes qu'il faut traverser depuis le tronc).
    std::vector<std::uint8_t> surplomb(total, 0);
    for (std::size_t n = 0; n < total; ++n) {
        surplomb[n] = matiere[n] && !tronc[n];
    }
    std::vector<std::int32_t> morceau(total, -1);
    std::vector<std::size_t> taille_morceau;
    std::vector<int> profondeur_morceau;
    {
        std::vector<std::uint32_t> pile, front, suite;
        for (std::size_t depart = 0; depart < total; ++depart) {
            if (!surplomb[depart] || morceau[depart] >= 0) {
                continue;
            }
            const auto numero = static_cast<std::int32_t>(taille_morceau.size());
            std::vector<std::uint32_t> cubes;
            pile.assign(1, static_cast<std::uint32_t>(depart));
            morceau[depart] = numero;
            while (!pile.empty()) {
                const std::uint32_t n = pile.back();
                pile.pop_back();
                cubes.push_back(n);
                int i, j, k;
                coordonnees(n, i, j, k);
                for (const Voisin& v : voisins) {
                    const int a = i + v.di, b = j + v.dj, c = k + v.dk;
                    if (!dedans(a, b, c)) {
                        continue;
                    }
                    const std::size_t m = indice(a, b, c);
                    if (surplomb[m] && morceau[m] < 0) {
                        morceau[m] = numero;
                        pile.push_back(static_cast<std::uint32_t>(m));
                    }
                }
            }
            // Profondeur : on avance par vagues depuis les cubes qui touchent le tronc.
            std::unordered_map<std::uint32_t, int> vague;
            front.clear();
            for (const std::uint32_t n : cubes) {
                int i, j, k;
                coordonnees(n, i, j, k);
                for (const Voisin& v : voisins) {
                    const int a = i + v.di, b = j + v.dj, c = k + v.dk;
                    if (dedans(a, b, c) && matiere[indice(a, b, c)] && !surplomb[indice(a, b, c)]) {
                        vague.emplace(n, 1);
                        front.push_back(n);
                        break;
                    }
                }
            }
            int profondeur = front.empty() ? 0 : 1;
            while (!front.empty()) {
                suite.clear();
                for (const std::uint32_t n : front) {
                    int i, j, k;
                    coordonnees(n, i, j, k);
                    for (const Voisin& v : voisins) {
                        const int a = i + v.di, b = j + v.dj, c = k + v.dk;
                        if (!dedans(a, b, c)) {
                            continue;
                        }
                        const auto m = static_cast<std::uint32_t>(indice(a, b, c));
                        if (morceau[m] == numero && vague.emplace(m, profondeur + 1).second) {
                            suite.push_back(m);
                        }
                    }
                }
                if (!suite.empty()) {
                    ++profondeur;
                }
                front.swap(suite);
            }
            taille_morceau.push_back(cubes.size());
            profondeur_morceau.push_back(profondeur);
        }
    }
    // Un morceau tout petit, ou une simple pellicule sur un tronc un peu
    // bombé, ne mérite pas d'être une branche : il reste avec le tronc.
    // Un morceau qui ne touche pas le tronc non plus (il flotte).
    const double volume_cube = pas * pas * pas;
    std::vector<std::uint8_t> garde(taille_morceau.size(), 0);
    for (std::size_t n = 0; n < garde.size(); ++n) {
        garde[n] = taille_morceau[n] * volume_cube >= 20.0 && profondeur_morceau[n] * pas >= 2.0 &&
                   !exclus.contains(static_cast<int>(n));
    }
    std::vector<std::uint8_t> branche(total, 0);
    bool une_branche = false;
    for (std::size_t n = 0; n < total; ++n) {
        if (morceau[n] >= 0 && garde[morceau[n]]) {
            branche[n] = 1;
            une_branche = true;
        }
    }
    analyse.tige_des_cubes.assign(total, -1);
    analyse.zone_branches.assign(total, 0);
    champ.valeurs.assign(total, hors_zone);
    if (!une_branche) {
        return analyse;
    }

    // 3. La zone des branches : leur matière, élargie de quelques cubes (pour
    // interpoler jusqu'à leur surface et un peu au-delà), hors du tronc.
    std::vector<std::uint8_t>& zone = analyse.zone_branches;
    zone = branche;
    for (int axe = 0; axe < 3; ++axe) {
        std::vector<std::uint8_t> suite(total, 0);
        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    bool plein = false;
                    for (int d = -bande; d <= bande && !plein; ++d) {
                        const int a = i + (axe == 0 ? d : 0), b = j + (axe == 1 ? d : 0), c = k + (axe == 2 ? d : 0);
                        plein = dedans(a, b, c) && zone[indice(a, b, c)];
                    }
                    suite[indice(i, j, k)] = plein;
                }
            }
        }
        zone.swap(suite);
    }
    for (std::size_t n = 0; n < total; ++n) {
        // Le reste de la matière (le tronc, les morceaux laissés avec lui) n'en fait pas partie.
        if (matiere[n] && !branche[n]) {
            zone[n] = 0;
        }
    }

    // 4. Longueur du chemin jusqu'au tronc. On part des cubes du tronc qui
    // touchent la zone ; la longueur y vaut moins un demi-cube, pour valoir
    // zéro sur la surface du tronc. Chaque cube retient sa « source » : le
    // point d'où part sa dernière ligne droite. La longueur est ainsi celle
    // d'un vrai chemin en lignes droites, pas celle d'un escalier de cubes.
    const auto visible = [&](int i0, int j0, int k0, int i1, int j1, int k1) {
        const int di = i1 - i0, dj = j1 - j0, dk = k1 - k0;
        const int pas_ligne = 2 * std::max({std::abs(di), std::abs(dj), std::abs(dk)});
        for (int s = 1; s < pas_ligne; ++s) {
            const double t = static_cast<double>(s) / pas_ligne;
            const int i = static_cast<int>(std::lround(i0 + di * t));
            const int j = static_cast<int>(std::lround(j0 + dj * t));
            const int k = static_cast<int>(std::lround(k0 + dk * t));
            if (!zone[indice(i, j, k)] && !(i == i1 && j == j1 && k == k1)) {
                return false;
            }
        }
        return true;
    };
    std::vector<std::uint32_t> source(total, 0);
    std::vector<std::uint8_t> fini(total, 0), depart(total, 0);
    using Entree = std::pair<float, std::uint32_t>;
    std::priority_queue<Entree, std::vector<Entree>, std::greater<Entree>> file;
    const float infini = std::numeric_limits<float>::max();
    std::vector<float> longueur(total, infini);
    for (std::size_t n = 0; n < total; ++n) {
        if (!matiere[n] || branche[n]) {
            continue;
        }
        int i, j, k;
        coordonnees(n, i, j, k);
        for (const Voisin& v : voisins) {
            const int a = i + v.di, b = j + v.dj, c = k + v.dk;
            if (dedans(a, b, c) && zone[indice(a, b, c)]) {
                longueur[n] = static_cast<float>(-0.5 * pas);
                source[n] = static_cast<std::uint32_t>(n);
                depart[n] = 1;
                file.push({longueur[n], static_cast<std::uint32_t>(n)});
                break;
            }
        }
    }
    while (!file.empty()) {
        const auto [d, n] = file.top();
        file.pop();
        if (d > longueur[n] || fini[n]) {
            continue;
        }
        fini[n] = 1;
        int i, j, k;
        coordonnees(n, i, j, k);
        const std::uint32_t s = source[n];
        int si, sj, sk;
        coordonnees(s, si, sj, sk);
        for (const Voisin& v : voisins) {
            const int a = i + v.di, b = j + v.dj, c = k + v.dk;
            if (!dedans(a, b, c)) {
                continue;
            }
            const std::size_t m = indice(a, b, c);
            if (!zone[m] || fini[m] || longueur[m] <= d) {
                continue;
            }
            float candidat;
            std::uint32_t origine_candidat;
            if (s != n && visible(a, b, c, si, sj, sk)) {
                candidat = longueur[s] + static_cast<float>(pas * std::sqrt(static_cast<double>(
                    (a - si) * (a - si) + (b - sj) * (b - sj) + (c - sk) * (c - sk))));
                origine_candidat = s;
            } else {
                candidat = d + static_cast<float>(pas * std::sqrt(static_cast<double>(
                    v.di * v.di + v.dj * v.dj + v.dk * v.dk)));
                origine_candidat = n;
            }
            if (candidat < longueur[m]) {
                longueur[m] = candidat;
                source[m] = origine_candidat;
                file.push({candidat, static_cast<std::uint32_t>(m)});
            }
        }
    }
    for (std::size_t n = 0; n < total; ++n) {
        if (zone[n] && longueur[n] != infini) {
            champ.valeurs[n] = longueur[n];
            if (branche[n]) {
                champ.maximum = std::max(champ.maximum, static_cast<double>(longueur[n]));
            }
        }
    }
    // Sous la surface du tronc, sur quelques cubes : l'opposé de la distance
    // à la zone des branches. Le champ traverse ainsi la surface du tronc
    // sans cassure, et s'interpole proprement de part et d'autre.
    const int fond = 3;
    for (std::size_t n = 0; n < total; ++n) {
        if (!depart[n]) {
            continue;
        }
        int i, j, k;
        coordonnees(n, i, j, k);
        for (int dk = -fond; dk <= fond; ++dk) {
            for (int dj = -fond; dj <= fond; ++dj) {
                for (int di = -fond; di <= fond; ++di) {
                    const int a = i + di, b = j + dj, c = k + dk;
                    if (!dedans(a, b, c)) {
                        continue;
                    }
                    const std::size_t m = indice(a, b, c);
                    if (zone[m] || !matiere[m]) {
                        continue;
                    }
                    const auto valeur = static_cast<float>(-0.5 * pas - pas * std::sqrt(static_cast<double>(di * di + dj * dj + dk * dk)));
                    if (champ.valeurs[m] == hors_zone || valeur > champ.valeurs[m]) {
                        champ.valeurs[m] = valeur;
                    }
                }
            }
        }
    }

    // 5. Les tiges. La matière des branches est rangée par bandes d'égale
    // longueur de chemin ; dans chaque bande, les groupes d'un seul tenant
    // sont les « nœuds ». Quand un nœud en porte deux, la branche se divise.
    const double epaisseur_bande = 3.0 * pas;
    const auto bande_de = [&](std::size_t n) {
        return static_cast<int>(std::max(0.0f, champ.valeurs[n]) / epaisseur_bande);
    };
    struct Noeud {
        int bande = 0;
        std::int32_t morceau = 0;
        std::size_t cubes = 0;
        std::vector<int> dessus, dessous;  // nœuds de la bande suivante / précédente
    };
    std::vector<Noeud> noeuds;
    std::vector<std::int32_t> noeud_de(total, -1);
    {
        std::vector<std::uint32_t> pile;
        for (std::size_t debut = 0; debut < total; ++debut) {
            if (!branche[debut] || noeud_de[debut] >= 0 || champ.valeurs[debut] == hors_zone) {
                continue;
            }
            const auto numero = static_cast<std::int32_t>(noeuds.size());
            Noeud noeud;
            noeud.bande = bande_de(debut);
            noeud.morceau = morceau[debut];
            pile.assign(1, static_cast<std::uint32_t>(debut));
            noeud_de[debut] = numero;
            while (!pile.empty()) {
                const std::uint32_t n = pile.back();
                pile.pop_back();
                ++noeud.cubes;
                int i, j, k;
                coordonnees(n, i, j, k);
                for (const Voisin& v : voisins) {
                    const int a = i + v.di, b = j + v.dj, c = k + v.dk;
                    if (!dedans(a, b, c)) {
                        continue;
                    }
                    const std::size_t m = indice(a, b, c);
                    if (branche[m] && noeud_de[m] < 0 && champ.valeurs[m] != hors_zone && bande_de(m) == noeud.bande) {
                        noeud_de[m] = numero;
                        pile.push_back(static_cast<std::uint32_t>(m));
                    }
                }
            }
            noeuds.push_back(std::move(noeud));
        }
        for (std::size_t n = 0; n < total; ++n) {
            if (noeud_de[n] < 0) {
                continue;
            }
            int i, j, k;
            coordonnees(n, i, j, k);
            for (const Voisin& v : voisins) {
                const int a = i + v.di, b = j + v.dj, c = k + v.dk;
                if (!dedans(a, b, c)) {
                    continue;
                }
                const std::int32_t autre = noeud_de[indice(a, b, c)];
                if (autre >= 0 && noeuds[autre].bande == noeuds[noeud_de[n]].bande + 1) {
                    std::vector<int>& dessus = noeuds[noeud_de[n]].dessus;
                    if (std::find(dessus.begin(), dessus.end(), autre) == dessus.end()) {
                        dessus.push_back(autre);
                        noeuds[autre].dessous.push_back(noeud_de[n]);
                    }
                }
            }
        }
    }
    // Une pousse minuscule (une aspérité) ne fait pas une fourche.
    const std::size_t cubes_min = static_cast<std::size_t>(std::max(8.0, 1.5 / volume_cube));
    const auto volume_au_dessus = [&](int depart_noeud) {
        std::size_t somme = 0;
        std::vector<int> a_voir{depart_noeud};
        std::vector<std::uint8_t> vu(noeuds.size(), 0);
        while (!a_voir.empty() && somme < 4 * cubes_min) {
            const int n = a_voir.back();
            a_voir.pop_back();
            if (vu[n]) {
                continue;
            }
            vu[n] = 1;
            somme += noeuds[n].cubes;
            a_voir.insert(a_voir.end(), noeuds[n].dessus.begin(), noeuds[n].dessus.end());
        }
        return somme;
    };
    // Tiges : une tige est une suite de nœuds qui se suivent un par un. Une
    // fourche ne compte que si chacune de ses pousses a un vrai volume.
    std::vector<std::int32_t> tige_du_noeud(noeuds.size(), -1);
    std::vector<int> mere_brute, morceau_brut;
    std::vector<std::size_t> cubes_bruts;
    const auto vraies_pousses = [&](int n) {
        std::vector<int> pousses;
        for (const int d : noeuds[n].dessus) {
            if (volume_au_dessus(d) >= cubes_min) {
                pousses.push_back(d);
            }
        }
        return pousses;
    };
    // Parcours des nœuds par bandes croissantes.
    std::vector<int> ordre(noeuds.size());
    for (std::size_t n = 0; n < ordre.size(); ++n) {
        ordre[n] = static_cast<int>(n);
    }
    std::stable_sort(ordre.begin(), ordre.end(), [&](int a, int b) { return noeuds[a].bande < noeuds[b].bande; });
    for (const int n : ordre) {
        if (tige_du_noeud[n] >= 0) {
            continue;
        }
        // Un nœud sans vraie tige en dessous commence une tige (posée sur le tronc).
        int mere = -1;
        for (const int d : noeuds[n].dessous) {
            if (tige_du_noeud[d] >= 0) {
                mere = tige_du_noeud[d];
            }
        }
        int tige = -1;
        if (mere >= 0) {
            // Suite de la tige du dessous, sauf si celle-ci se divise ici.
            int porteur = noeuds[n].dessous.front();
            for (const int d : noeuds[n].dessous) {
                if (tige_du_noeud[d] == mere) {
                    porteur = d;
                }
            }
            const std::vector<int> pousses = vraies_pousses(porteur);
            const bool vraie = std::find(pousses.begin(), pousses.end(), n) != pousses.end();
            if (pousses.size() <= 1 || !vraie || noeuds[n].dessous.size() > 1) {
                tige = mere;  // même tige (ou aspérité rattachée, ou deux pousses qui se rejoignent)
            }
        }
        if (tige < 0) {
            tige = static_cast<int>(mere_brute.size());
            mere_brute.push_back(mere);
            morceau_brut.push_back(noeuds[n].morceau);
            cubes_bruts.push_back(0);
        }
        tige_du_noeud[n] = tige;
        cubes_bruts[tige] += noeuds[n].cubes;
    }
    // Les miettes : au pied d'une branche, de tout petits groupes de cubes
    // forment des « tiges » de quelques millimètres cubes. Chacune est
    // rattachée à sa tige mère, ou, si elle part du tronc, à la plus grosse
    // tige de son morceau.
    const std::size_t nombre_brut = mere_brute.size();
    std::vector<int> vers(nombre_brut);
    for (std::size_t t = 0; t < nombre_brut; ++t) {
        vers[t] = static_cast<int>(t);
    }
    const auto miette = [&](std::size_t t) { return cubes_bruts[t] * volume_cube < 15.0; };
    for (std::size_t t = 0; t < nombre_brut; ++t) {
        if (!miette(t)) {
            continue;
        }
        int cible = mere_brute[t];
        if (cible < 0) {
            std::size_t plus_grosse = 0;
            for (std::size_t autre = 0; autre < nombre_brut; ++autre) {
                if (morceau_brut[autre] == morceau_brut[t] && mere_brute[autre] < 0 && !miette(autre) &&
                    cubes_bruts[autre] > plus_grosse) {
                    plus_grosse = cubes_bruts[autre];
                    cible = static_cast<int>(autre);
                }
            }
        }
        vers[t] = cible;  // -1 : aucune tige où la rattacher, elle disparaît
    }
    const auto finale = [&](int t) {
        for (int tours = 0; t >= 0 && vers[t] != t && tours < 64; ++tours) {
            t = vers[t];
        }
        return t;
    };
    // Renumérotation : les tiges gardées, dans le même ordre (une tige vient
    // toujours après celle qui la porte).
    std::vector<int> nouveau(nombre_brut, -1);
    for (std::size_t t = 0; t < nombre_brut; ++t) {
        if (vers[t] == static_cast<int>(t)) {
            nouveau[t] = static_cast<int>(analyse.meres.size());
            const int mere = finale(mere_brute[t]);
            analyse.meres.push_back(mere >= 0 ? nouveau[mere] : -1);
            analyse.morceaux.push_back(morceau_brut[t]);
            analyse.volumes.push_back(0.0);
        }
    }
    for (std::size_t t = 0; t < nombre_brut; ++t) {
        const int cible = finale(static_cast<int>(t));
        if (cible >= 0 && nouveau[cible] >= 0) {
            analyse.volumes[nouveau[cible]] += cubes_bruts[t] * volume_cube;
        }
    }
    for (std::size_t n = 0; n < total; ++n) {
        if (noeud_de[n] >= 0) {
            const int cible = finale(tige_du_noeud[noeud_de[n]]);
            analyse.tige_des_cubes[n] = cible >= 0 ? nouveau[cible] : -1;
        }
    }
    return analyse;
}

}  // namespace artprint
