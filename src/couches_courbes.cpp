// ArtPrint Slicer - licence AGPL v3
#include "couches_courbes.hpp"
#include <string>
#include <map>
#include <mutex>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <set>
#include <unordered_map>

#include "decoupe.hpp"
#include "slicer.hpp"

namespace artprint {

namespace {

using Face = std::array<std::uint32_t, 3>;

std::uint64_t cle_arete(std::uint32_t a, std::uint32_t b) {
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
}

// Recoupe les triangles dont une arête dépasse « longueur_max » : le champ
// n'est pas droit le long d'une grande arête, il faut des petits triangles
// pour le suivre. Une arête est coupée de la même façon dans les deux
// triangles qui la partagent : le maillage reste fermé.
void raffiner(std::vector<Vec3>& sommets, std::vector<Face>& faces, double longueur_max) {
    for (int passe = 0; passe < 24; ++passe) {
        std::unordered_map<std::uint64_t, std::uint32_t> milieux;
        const auto milieu = [&](std::uint32_t a, std::uint32_t b) -> std::int64_t {
            if (norme(sommets[a] - sommets[b]) <= longueur_max) {
                return -1;
            }
            const auto [trouve, nouveau] = milieux.try_emplace(cle_arete(a, b), 0);
            if (nouveau) {
                trouve->second = static_cast<std::uint32_t>(sommets.size());
                sommets.push_back((sommets[a] + sommets[b]) * 0.5);
            }
            return trouve->second;
        };
        std::vector<Face> suite;
        suite.reserve(faces.size() * 2);
        bool change = false;
        for (const Face& f : faces) {
            const std::int64_t m[3] = {milieu(f[0], f[1]), milieu(f[1], f[2]), milieu(f[2], f[0])};
            const int nombre = (m[0] >= 0) + (m[1] >= 0) + (m[2] >= 0);
            if (nombre == 0) {
                suite.push_back(f);
                continue;
            }
            change = true;
            const auto s = [](std::int64_t valeur) { return static_cast<std::uint32_t>(valeur); };
            if (nombre == 3) {
                suite.push_back({f[0], s(m[0]), s(m[2])});
                suite.push_back({s(m[0]), f[1], s(m[1])});
                suite.push_back({s(m[2]), s(m[1]), f[2]});
                suite.push_back({s(m[0]), s(m[1]), s(m[2])});
                continue;
            }
            // Une ou deux arêtes coupées : on tourne le triangle pour que la
            // première arête coupée soit (a, b).
            int depart = 0;
            if (nombre == 1) {
                depart = m[0] >= 0 ? 0 : m[1] >= 0 ? 1 : 2;
            } else {
                depart = m[0] < 0 ? 1 : m[1] < 0 ? 2 : 0;  // l'arête non coupée est juste avant
            }
            const std::uint32_t a = f[depart], b = f[(depart + 1) % 3], c = f[(depart + 2) % 3];
            const std::uint32_t mab = s(m[depart]);
            if (nombre == 1) {
                suite.push_back({a, mab, c});
                suite.push_back({mab, b, c});
            } else {
                const std::uint32_t mbc = s(m[(depart + 1) % 3]);
                suite.push_back({mab, b, mbc});
                suite.push_back({a, mab, mbc});
                suite.push_back({a, mbc, c});
            }
        }
        faces.swap(suite);
        if (!change) {
            break;
        }
    }
}

// Lignes où la surface du maillage a exactement la valeur « niveau » : des
// boucles fermées dans l'espace.
std::vector<std::vector<Vec3>> boucles_de_niveau(const std::vector<Vec3>& sommets,
                                                 const std::vector<Face>& faces,
                                                 const std::vector<double>& valeurs,
                                                 const std::vector<std::array<float, 2>>& bornes,
                                                 double niveau) {
    struct Segment {
        std::uint64_t cles[2];
        bool vu = false;
    };
    std::vector<Segment> segments;
    std::unordered_map<std::uint64_t, std::array<int, 2>> voisins;
    std::unordered_map<std::uint64_t, Vec3> points;
    for (std::size_t n = 0; n < faces.size(); ++n) {
        if (niveau < bornes[n][0] || niveau > bornes[n][1]) {
            continue;
        }
        const Face& f = faces[n];
        const bool dessus[3] = {valeurs[f[0]] >= niveau, valeurs[f[1]] >= niveau, valeurs[f[2]] >= niveau};
        Segment segment{};
        int trouves = 0;
        for (int e = 0; e < 3 && trouves < 2; ++e) {
            const std::uint32_t a = f[e], b = f[(e + 1) % 3];
            if (dessus[e] == dessus[(e + 1) % 3]) {
                continue;
            }
            const std::uint64_t cle = cle_arete(a, b);
            segment.cles[trouves++] = cle;
            if (!points.contains(cle)) {
                const double t = std::clamp((niveau - valeurs[a]) / (valeurs[b] - valeurs[a]), 0.0, 1.0);
                points.emplace(cle, sommets[a] + (sommets[b] - sommets[a]) * t);
            }
        }
        if (trouves != 2) {
            continue;
        }
        const int numero = static_cast<int>(segments.size());
        for (const std::uint64_t cle : segment.cles) {
            auto [trouve, nouveau] = voisins.try_emplace(cle, std::array<int, 2>{numero, -1});
            if (!nouveau) {
                trouve->second[1] = numero;
            }
        }
        segments.push_back(segment);
    }
    std::vector<std::vector<Vec3>> boucles;
    for (std::size_t depart = 0; depart < segments.size(); ++depart) {
        if (segments[depart].vu) {
            continue;
        }
        std::vector<Vec3> boucle;
        int courant = static_cast<int>(depart);
        std::uint64_t cle = segments[depart].cles[0];
        bool fermee = false;
        while (courant >= 0 && !segments[courant].vu) {
            segments[courant].vu = true;
            boucle.push_back(points.at(cle));
            // On sort du segment par son autre arête, puis on passe au triangle voisin.
            cle = segments[courant].cles[0] == cle ? segments[courant].cles[1] : segments[courant].cles[0];
            const std::array<int, 2>& suivants = voisins.at(cle);
            const int suivant = suivants[0] == courant ? suivants[1] : suivants[0];
            if (suivant == static_cast<int>(depart)) {
                fermee = true;
            }
            courant = suivant;
        }
        if (fermee && boucle.size() >= 3) {
            boucles.push_back(std::move(boucle));
        }
    }
    return boucles;
}

}  // namespace

bool poser_sur_la_surface(const ChampCouches& champ, const CoucheCourbe& couche, const Point2& p,
                          double niveau, double& hauteur, Vec3& point) {
    const Vec3 pied = couche.origine + couche.u * p.x + couche.v * p.y;
    double s = hauteur;
    double f = 0.0;
    // Si le départ est hors de la zone calculée, on cherche le long de w un
    // endroit où le champ est connu.
    if (!champ.valeur(pied + couche.w * s, f)) {
        bool trouve = false;
        for (int n = 1; n <= 80 && !trouve; ++n) {
            for (const double essai : {s + n * champ.pas, s - n * champ.pas}) {
                if (champ.valeur(pied + couche.w * essai, f)) {
                    s = essai;
                    trouve = true;
                    break;
                }
            }
        }
        if (!trouve) {
            return false;
        }
    }
    double connu = s;          // dernière hauteur où le champ était connu
    double meilleur = s, plus_petit = 1e9;
    for (int iteration = 0; iteration < 60; ++iteration) {
        const Vec3 ici = pied + couche.w * s;
        if (!champ.valeur(ici, f)) {
            // Sorti de la zone : on revient à mi-chemin du dernier point connu.
            if (std::abs(s - connu) < 1e-5) {
                break;
            }
            s = 0.5 * (s + connu);
            continue;
        }
        connu = s;
        const double ecart = niveau - f;
        if (std::abs(ecart) < plus_petit) {
            plus_petit = std::abs(ecart);
            meilleur = s;
        }
        if (std::abs(ecart) < 2e-4) {
            break;
        }
        Vec3 direction;
        double pente = 1.0;
        double montee = 1.0;
        if (champ.gradient(ici, direction, pente)) {
            montee = pente * produit_scalaire(direction, couche.w);
        }
        s += std::clamp(ecart / std::max(montee, 0.2), -0.6, 0.6);
    }
    // Au bord de la matière, la surface peut passer juste hors de la zone
    // calculée : le point connu le plus proche convient s'il est à quelques
    // centièmes de millimètre.
    if (plus_petit > 0.05) {
        return false;
    }
    hauteur = meilleur;
    point = pied + couche.w * meilleur;
    return true;
}

bool poser_sur_la_surface(const ChampCouches& champ, const CoucheCourbe& couche, const Point2& p,
                          double niveau, Vec3& point) {
    double hauteur = 0.0;
    return poser_sur_la_surface(champ, couche, p, niveau, hauteur, point);
}

namespace {

// Les boucles d'une tige à un niveau, leur centre et leur direction moyenne.
struct Brut {
    std::size_t niveau = 0;
    std::vector<std::vector<Vec3>> boucles;
    Vec3 centre, direction;
    double longueur = 0.0;
};

// Construit les cartes d'une tige à partir de ses boucles. Rend « faux » si
// une couche est trop bombée pour sa carte.
bool cartes_de_la_tige(std::vector<Brut>& bruts, const ChampCouches& champ, double pointe, double inclinaison_max,
                       std::vector<CoucheCourbe>& couches, std::string& raison) {
    for (Brut& brut : bruts) {
        Vec3 somme, direction;
        for (const std::vector<Vec3>& boucle : brut.boucles) {
            for (std::size_t i = 0; i < boucle.size(); ++i) {
                const Vec3& a = boucle[i];
                const Vec3& b = boucle[(i + 1) % boucle.size()];
                const double l = norme(b - a);
                const Vec3 milieu = (a + b) * 0.5;
                somme = somme + milieu * l;
                brut.longueur += l;
                Vec3 d;
                double pente;
                if (champ.gradient(milieu, d, pente)) {
                    direction = direction + d * l;
                }
            }
        }
        brut.centre = brut.longueur > 1e-9 ? somme * (1.0 / brut.longueur) : Vec3{};
        brut.direction = norme(direction) > 1e-9 ? normaliser(direction) : Vec3{0, 0, 1};
    }
    std::erase_if(bruts, [](const Brut& brut) { return brut.longueur < 1e-6; });
    // La direction des cartes est adoucie d'une couche à l'autre.
    for (int passe = 0; passe < 4; ++passe) {
        std::vector<Vec3> adoucies(bruts.size());
        for (std::size_t n = 0; n < bruts.size(); ++n) {
            const Vec3& avant = bruts[n == 0 ? 0 : n - 1].direction;
            const Vec3& apres = bruts[n + 1 < bruts.size() ? n + 1 : n].direction;
            adoucies[n] = normaliser(avant + bruts[n].direction * 2.0 + apres);
        }
        for (std::size_t n = 0; n < bruts.size(); ++n) {
            bruts[n].direction = adoucies[n];
        }
    }
    Vec3 u_avant{1, 0, 0};
    bool premiere = true;
    for (Brut& brut : bruts) {
        CoucheCourbe couche;
        couche.niveau = brut.niveau;
        couche.origine = brut.centre;
        couche.w = brut.direction;
        if (couche.w.z > std::cos(en_radians(0.5))) {
            couche.w = {0, 0, 1};  // pratiquement horizontale : carte vue du dessus
        }
        if (premiere && couche.w.z < 1.0 - 1e-12) {
            repere_autour(couche.w, couche.u, couche.v);
        } else {
            // La carte glisse depuis celle du dessous, sans vriller.
            couche.u = normaliser(u_avant - couche.w * produit_scalaire(u_avant, couche.w));
            couche.v = produit_vectoriel(couche.w, couche.u);
        }
        premiere = false;
        u_avant = couche.u;

        Polygones dessin;
        std::vector<double> cosinus_des_points;
        for (const std::vector<Vec3>& boucle : brut.boucles) {
            Contour contour;
            contour.reserve(boucle.size());
            for (const Vec3& p : boucle) {
                const Vec3 r = p - couche.origine;
                contour.push_back(en_unites(Point2{produit_scalaire(r, couche.u), produit_scalaire(r, couche.v)}));
                Vec3 d;
                double pente;
                if (champ.gradient(p, d, pente)) {
                    cosinus_des_points.push_back(produit_scalaire(d, couche.w));
                }
            }
            dessin.push_back(std::move(contour));
        }
        // Le bombement est celui des points les plus inclinés, sans compter
        // quelques points isolés (3 %) : tout près du tronc, la direction
        // est mal connue en un point ou deux, surtout sur un grand modèle.
        double cosinus = 1.0;
        if (!cosinus_des_points.empty()) {
            const std::size_t rang = cosinus_des_points.size() * 3 / 100;
            std::nth_element(cosinus_des_points.begin(), cosinus_des_points.begin() + static_cast<std::ptrdiff_t>(rang),
                             cosinus_des_points.end());
            cosinus = cosinus_des_points[rang];
        }
        couche.bombement = en_degres(std::acos(std::clamp(cosinus, -1.0, 1.0)));
        // Les toutes premières couches épousent la surface du tronc : sur un
        // tronc arrondi, elles s'enroulent autour de lui et sont très
        // bombées. C'est permis pour elles, et pour les toutes dernières
        // (le bout arrondi de la tige, tout petit) ; entre les deux, une
        // couche trop bombée ne peut plus être dessinée sur une carte plane.
        // Tout près du tronc (les quatre ou cinq premières couches), une
        // couche qui s'enroule trop autour de lui n'est pas abandonnée : on
        // n'en garde que la partie que la tête peut atteindre depuis la
        // direction de la carte (là où la surface penche de moins de 72°).
        // Le reste, les bords très enroulés, n'est qu'une fine pellicule
        // posée sur le tronc : c'est lui qui l'imprime, avec sa propre paroi.
        Polygones atteignable;
        bool decoupee = false;
        if (couche.bombement > 75.0 && couches.size() < 5) {
            double niveau = 0.0, haut = -1e9;
            std::size_t mesures = 0;
            for (const std::vector<Vec3>& boucle : brut.boucles) {
                for (const Vec3& p : boucle) {
                    double v = 0.0;
                    if (champ.valeur(p, v)) {
                        niveau += v;
                        ++mesures;
                    }
                    haut = std::max(haut, produit_scalaire(p - couche.origine, couche.w));
                }
            }
            if (mesures > 0) {
                niveau /= static_cast<double>(mesures);
                // L'ombre de la couche sur la carte (les replis comptent une seule fois).
                Polygones ombre;
                for (Contour contour : dessin) {
                    if (Clipper2Lib::Area(contour) < 0.0) {
                        std::reverse(contour.begin(), contour.end());
                    }
                    ombre.push_back(std::move(contour));
                }
                ombre = Clipper2Lib::Union(ombre, Clipper2Lib::FillRule::NonZero);
                const Clipper2Lib::Rect64 boite = Clipper2Lib::GetBounds(ombre);
                const double x0 = static_cast<double>(boite.left) / unites_par_mm;
                const double y0 = static_cast<double>(boite.top) / unites_par_mm;
                const double x1 = static_cast<double>(boite.right) / unites_par_mm;
                const double y1 = static_cast<double>(boite.bottom) / unites_par_mm;
                // (Sur une très grande couche, la grille de mesure est moins
                // fine : sinon ce calcul prendrait des minutes.)
                const double fin = std::max({0.2, 0.5 * champ.pas, std::abs(x1 - x0) / 140.0, std::abs(y1 - y0) / 140.0});
                const double seuil = std::cos(en_radians(72.0));
                Polygones bandes;
                for (double y = std::min(y0, y1); y < std::max(y0, y1); y += fin) {
                    double debut_bande = 0.0;
                    bool dans_une_bande = false;
                    for (double x = x0;; x += fin) {
                        bool bon = false;
                        if (x < x1) {
                            double hauteur = haut + champ.pas;
                            Vec3 ici, d;
                            double pente = 1.0;
                            bon = poser_sur_la_surface(champ, couche, {x + 0.5 * fin, y + 0.5 * fin}, niveau, hauteur, ici) &&
                                  champ.gradient(ici, d, pente) && produit_scalaire(d, couche.w) >= seuil;
                        }
                        if (bon && !dans_une_bande) {
                            debut_bande = x;
                            dans_une_bande = true;
                        } else if (!bon && dans_une_bande) {
                            bandes.push_back({en_unites(Point2{debut_bande, y}), en_unites(Point2{x, y}),
                                              en_unites(Point2{x, y + fin}), en_unites(Point2{debut_bande, y + fin})});
                            dans_une_bande = false;
                        }
                        if (x >= x1) {
                            break;
                        }
                    }
                }
                const Polygones zone = Clipper2Lib::Union(bandes, Clipper2Lib::FillRule::NonZero);
                atteignable = intersecter(ombre, decaler_arrondi(decaler_arrondi(zone, fin), -fin));
                decoupee = true;
                couche.bombement = 75.0;
            }
        }
        const std::size_t restantes = bruts.size() - static_cast<std::size_t>(&brut - bruts.data());
        if (couche.bombement > (couches.size() < 12 || restantes <= 12 ? 86.0 : 80.0)) {
            raison = std::format("sa couche {} sur {} est trop bombée ({:.0f}°)", couches.size() + 1, bruts.size(),
                                 couche.bombement);
            return false;
        }
        // Une couche qui regarde trop vers le bas (plus que ce que la tête
        // de la machine peut suivre) demanderait d'imprimer la tête en bas :
        // impossible. C'est le cas de la moitié haute d'une anse, attaquée
        // par le haut.
        if (couche.w.z < std::cos(en_radians(inclinaison_max))) {
            raison = std::format("sa couche {} sur {} regarde vers le bas", couches.size() + 1, bruts.size());
            return false;
        }
        couche.contour = Clipper2Lib::SimplifyPaths(
            decoupee ? atteignable : Clipper2Lib::Union(dessin, Clipper2Lib::FillRule::EvenOdd), 0.005 * unites_par_mm);
        // Là où la peau de la branche rejoint le tronc en biseau, la couche
        // finit en pointe très fine : les parois s'y chevaucheraient. Ce qui
        // est plus étroit qu'un cordon environ est retiré (la couche est
        // rétrécie puis regonflée).
        couche.contour = decaler_arrondi(decaler(couche.contour, -pointe), pointe);
        if (!couche.contour.empty()) {
            couches.push_back(std::move(couche));
        }
    }
    return true;
}

// Découpe toutes les tiges. « impossibles » reçoit les numéros des tiges
// qui n'ont pas pu être découpées.
std::vector<std::vector<CoucheCourbe>> trancher_les_tiges(const Maillage& modele, const AnalyseBranches& analyse,
                                                          const std::vector<double>& dessus,
                                                          const std::vector<double>& epaisseurs,
                                                          double pointe, double inclinaison_max,
                                                          std::map<int, std::string>& impossibles) {
    const ChampCouches& champ = analyse.champ;
    // Loin des branches, le champ n'est pas calculé : c'est le tronc.
    const double loin = -1.0e3;
    const auto valeur_en = [&](const Vec3& p) {
        double v = 0.0;
        return champ.valeur(p, v) ? v : loin;
    };
    // Seuls les triangles proches d'une branche servent.
    std::vector<Vec3> sommets;
    std::vector<Face> faces;
    {
        std::vector<std::int64_t> nouveau(modele.sommets.size(), -1);
        std::vector<double> valeurs(modele.sommets.size());
        for (std::size_t n = 0; n < modele.sommets.size(); ++n) {
            const Point3& p = modele.sommets[n];
            valeurs[n] = valeur_en({p.x, p.y, p.z});
        }
        for (const Triangle& t : modele.triangles) {
            if (valeurs[t.sommets[0]] == loin && valeurs[t.sommets[1]] == loin && valeurs[t.sommets[2]] == loin) {
                continue;
            }
            Face face;
            for (int c = 0; c < 3; ++c) {
                const std::uint32_t s = t.sommets[c];
                if (nouveau[s] < 0) {
                    nouveau[s] = static_cast<std::int64_t>(sommets.size());
                    const Point3& p = modele.sommets[s];
                    sommets.push_back({p.x, p.y, p.z});
                }
                face[c] = static_cast<std::uint32_t>(nouveau[s]);
            }
            faces.push_back(face);
        }
    }
    raffiner(sommets, faces, std::max(0.8, 2.0 * champ.pas));
    std::vector<double> valeurs(sommets.size());
    for (std::size_t n = 0; n < sommets.size(); ++n) {
        valeurs[n] = valeur_en(sommets[n]);
    }
    std::vector<std::array<float, 2>> bornes(faces.size());
    for (std::size_t n = 0; n < faces.size(); ++n) {
        const Face& f = faces[n];
        bornes[n] = {static_cast<float>(std::min({valeurs[f[0]], valeurs[f[1]], valeurs[f[2]]}) - 1e-4),
                     static_cast<float>(std::max({valeurs[f[0]], valeurs[f[1]], valeurs[f[2]]}) + 1e-4)};
    }
    // Une couche de branche ne touche jamais le tronc (elle en est à une
    // distance fixe) : son bord est tout entier sur la peau de la branche.
    const std::size_t nombre = analyse.nombre_tiges();
    std::vector<std::vector<Brut>> bruts(nombre);
    for (std::size_t k = 0; k < dessus.size(); ++k) {
        for (std::vector<Vec3>& boucle : boucles_de_niveau(sommets, faces, valeurs, bornes, dessus[k] - epaisseurs[k] / 2.0)) {
            std::vector<int> voix(nombre, 0);
            const std::size_t saut = std::max<std::size_t>(1, boucle.size() / 24);
            for (std::size_t i = 0; i < boucle.size(); i += saut) {
                const int tige = analyse.tige_en(boucle[i]);
                if (tige >= 0) {
                    ++voix[tige];
                }
            }
            const auto gagnante = std::max_element(voix.begin(), voix.end());
            if (gagnante == voix.end() || *gagnante == 0) {
                continue;
            }
            std::vector<Brut>& tige = bruts[gagnante - voix.begin()];
            if (tige.empty() || tige.back().niveau != k) {
                tige.emplace_back();
                tige.back().niveau = k;
            }
            tige.back().boucles.push_back(std::move(boucle));
        }
    }
    std::vector<std::vector<CoucheCourbe>> tiges(nombre);
    for (std::size_t n = 0; n < nombre; ++n) {
        std::string raison;
        if (!cartes_de_la_tige(bruts[n], champ, pointe, inclinaison_max, tiges[n], raison)) {
            impossibles.emplace(static_cast<int>(n), raison);
        }
    }
    return tiges;
}

}  // namespace

namespace {
PlanBranches calculer_le_plan(const Maillage& pose, double epaisseur, double pointe, const std::vector<Vec3>& a_plat,
                              double inclinaison_max);

// Empreinte de la demande : le modèle (tous ses sommets et triangles) et les réglages.
std::uint64_t empreinte_du_plan(const Maillage& pose, double epaisseur, double pointe, const std::vector<Vec3>& a_plat,
                                double inclinaison_max) {
    std::uint64_t h = 1469598103934665603ULL;
    const auto meler = [&](const void* donnees, std::size_t taille) {
        const auto* octets = static_cast<const unsigned char*>(donnees);
        for (std::size_t i = 0; i < taille; ++i) {
            h = (h ^ octets[i]) * 1099511628211ULL;
        }
    };
    for (const Point3& s : pose.sommets) {
        const float c[3] = {s.x, s.y, s.z};
        meler(c, sizeof c);
    }
    for (const Triangle& t : pose.triangles) {
        meler(t.sommets.data(), sizeof(std::uint32_t) * 3);
    }
    for (const Vec3& p : a_plat) {
        const double c[3] = {p.x, p.y, p.z};
        meler(c, sizeof c);
    }
    const double r[3] = {epaisseur, pointe, inclinaison_max};
    meler(r, sizeof r);
    return h;
}
}  // namespace

// Le plan est long à calculer, et le tranchage le redemande plusieurs fois
// pour le même modèle (une fois pour proposer les stratégies, puis à chaque
// nouvel essai) : les derniers plans calculés sont gardés en mémoire.
PlanBranches preparer_branches(const Maillage& pose, double epaisseur, double pointe,
                               const std::vector<Vec3>& a_plat, double inclinaison_max) {
    static std::mutex verrou;
    static std::vector<std::pair<std::uint64_t, PlanBranches>> memoire;
    const std::uint64_t empreinte = empreinte_du_plan(pose, epaisseur, pointe, a_plat, inclinaison_max);
    {
        const std::lock_guard<std::mutex> garde(verrou);
        for (const auto& [cle, plan] : memoire) {
            if (cle == empreinte) {
                return plan;
            }
        }
    }
    PlanBranches plan = calculer_le_plan(pose, epaisseur, pointe, a_plat, inclinaison_max);
    const std::lock_guard<std::mutex> garde(verrou);
    if (memoire.size() >= 6) {
        memoire.erase(memoire.begin());
    }
    memoire.emplace_back(empreinte, plan);
    return plan;
}

namespace {
PlanBranches calculer_le_plan(const Maillage& pose, double epaisseur, double pointe, const std::vector<Vec3>& a_plat,
                              double inclinaison_max) {
    PlanBranches plan;
    // La matière des tiges qu'on n'a pas su découper : elle est imprimée avec le tronc.
    std::vector<std::uint8_t> avec_le_tronc;
    std::vector<std::string> journal;
    for (int essai = 0; essai < 12; ++essai) {
        plan = PlanBranches{};
        plan.analyse = analyser_branches(pose, 0.0, {}, avec_le_tronc.empty() ? nullptr : &avec_le_tronc);
        plan.morceaux_laisses = static_cast<std::size_t>(essai);
        if (plan.analyse.nombre_tiges() == 0) {
            plan.journal = journal;
            return plan;
        }
        for (double haut = epaisseur; haut < plan.analyse.champ.maximum + epaisseur; haut += epaisseur) {
            plan.epaisseurs.push_back(epaisseur);
            plan.dessus.push_back(haut);
        }
        std::map<int, std::string> impossibles;
        plan.tiges = trancher_les_tiges(pose, plan.analyse, plan.dessus, plan.epaisseurs, pointe, inclinaison_max, impossibles);
        plan.journal = journal;
        // Une tige très mince (moins de 1,7 mm d'épaisseur en tout : une
        // pellicule ou un collier autour du tronc, là où des raccords
        // arrondis se rejoignent) n'est pas une branche : découpée en
        // couches courbes, elle ne donnerait que quelques feuilles
        // incomplètes. Elle est rendue au tronc, qui l'imprime avec sa
        // propre paroi, bien lisse ; les branches partent ensuite d'elle.
        std::set<int> pellicules;
        for (std::size_t n = 0; n < plan.tiges.size(); ++n) {
            // (Seulement celles qui tiennent au tronc lui-même.)
            if (plan.analyse.meres[n] < 0 && static_cast<double>(plan.tiges[n].size()) * epaisseur <= 1.7 &&
                !impossibles.contains(static_cast<int>(n))) {
                pellicules.insert(static_cast<int>(n));
            }
        }
        // Les branches qu'on a demandé d'imprimer à plat (celles qui
        // contiennent un des points donnés, si elles sont encore des branches).
        for (const Vec3& point : a_plat) {
            const int tige = plan.analyse.tige_en(point);
            if (tige >= 0) {
                pellicules.insert(tige);
            }
        }
        if (impossibles.empty() && pellicules.empty()) {
            return plan;
        }
        for (const auto& [tige, raison] : impossibles) {
            journal.push_back(std::format("Une branche de {:.1f} cm³ est imprimée avec le tronc, en couches horizontales : {}.",
                                          plan.analyse.volumes[static_cast<std::size_t>(tige)] / 1000.0, raison));
        }
        // Une tige trop bombée pour être découpée en couches courbes est
        // rendue au tronc : elle sera imprimée avec lui, en couches
        // horizontales. Les tiges qui la portent aussi (elle ne peut pas être
        // imprimée avant elles). Celles qu'elle porte restent des branches :
        // elles seront imprimées contre elle. Puis on recommence.
        std::set<int> rendues;
        for (const int tige : pellicules) {
            for (int t = tige; t >= 0 && rendues.insert(t).second; t = plan.analyse.meres[static_cast<std::size_t>(t)]) {
            }
        }
        for (const auto& [tige, raison] : impossibles) {
            for (int t = tige; t >= 0 && rendues.insert(t).second; t = plan.analyse.meres[static_cast<std::size_t>(t)]) {
            }
        }
        avec_le_tronc.resize(plan.analyse.tige_des_cubes.size(), 0);
        for (std::size_t n = 0; n < avec_le_tronc.size(); ++n) {
            if (rendues.contains(plan.analyse.tige_des_cubes[n])) {
                avec_le_tronc[n] = 1;
            }
        }
    }
    plan.tiges.clear();
    return plan;
}
}  // namespace

}  // namespace artprint
