// ArtPrint Slicer - licence AGPL v3
//
// Méthode :
//   - Chaque groupe surveillé (tête, bras, module Z) a un champ de distance,
//     calculé une fois dans son propre repère.
//   - Les obstacles sont des nuages de points : le bâti, le chariot Y, le
//     plateau, la matière déposée. Un point d'obstacle est ramené dans le
//     repère du groupe, puis sa distance est lue dans le champ.
//   - Les points sont rangés par paquets cubiques. Un paquet entier est
//     écarté d'un coup quand son centre est assez loin du groupe.
//   - Après un contrôle, on connaît la plus petite « réserve » de distance.
//     Tant que la machine a bougé de moins que cette réserve, rien ne peut
//     se toucher : les contrôles suivants sont sautés.
//   - Les pièces qui se touchent déjà dans le dessin (un palier sur son tube,
//     un pignon sur sa crémaillère) sont des liaisons, pas des collisions :
//     ces couples de pièces sont repérés au départ et ignorés ensuite.
#include "collisions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "champ_distance.hpp"

namespace artprint {

namespace {

// Transformation rigide : rotation puis translation.
struct Transformation {
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec3 t;
    Vec3 appliquer(const Vec3& p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z + t.x,
                m[3] * p.x + m[4] * p.y + m[5] * p.z + t.y,
                m[6] * p.x + m[7] * p.y + m[8] * p.z + t.z};
    }
};

// a puis b : resultat(p) = b(a(p)).
Transformation enchainer(const Transformation& a, const Transformation& b) {
    Transformation r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i * 3 + j] = b.m[i * 3] * a.m[j] + b.m[i * 3 + 1] * a.m[3 + j] +
                             b.m[i * 3 + 2] * a.m[6 + j];
        }
    }
    r.t = b.appliquer(a.t);
    return r;
}

Transformation translation(const Vec3& v) {
    Transformation r;
    r.t = v;
    return r;
}

// Rotation d'un angle (radians) autour d'un axe X, Y ou Z passant par « centre ».
Transformation rotation(char axe, double angle, const Vec3& centre) {
    const double c = std::cos(angle), s = std::sin(angle);
    Transformation r;
    if (axe == 'x') {
        const double m[9] = {1, 0, 0, 0, c, -s, 0, s, c};
        std::copy(m, m + 9, r.m);
    } else if (axe == 'y') {
        const double m[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
        std::copy(m, m + 9, r.m);
    } else {
        const double m[9] = {c, -s, 0, s, c, 0, 0, 0, 1};
        std::copy(m, m + 9, r.m);
    }
    const Vec3 tourne = r.appliquer(centre);
    r.t = centre - tourne;
    return r;
}

// Nuage de points rangés par paquets cubiques.
struct Nuage {
    explicit Nuage(double cote) : cote_paquet(cote) {}

    struct Point {
        Vec3 position;
        std::uint16_t piece;
    };
    double cote_paquet;
    std::unordered_map<std::uint64_t, std::vector<Point>> paquets;
    Vec3 mini{1e30, 1e30, 1e30}, maxi{-1e30, -1e30, -1e30};
    std::size_t nombre = 0;

    static std::uint64_t cle(std::int64_t i, std::int64_t j, std::int64_t k) {
        constexpr std::int64_t d = 1 << 20;
        constexpr std::uint64_t masque = (1u << 21) - 1;
        return ((static_cast<std::uint64_t>(i + d) & masque) << 42) |
               ((static_cast<std::uint64_t>(j + d) & masque) << 21) |
               (static_cast<std::uint64_t>(k + d) & masque);
    }
    std::int64_t case_de(double v) const {
        return static_cast<std::int64_t>(std::floor(v / cote_paquet));
    }
    void ajouter(const Vec3& p, std::uint16_t piece) {
        paquets[cle(case_de(p.x), case_de(p.y), case_de(p.z))].push_back({p, piece});
        mini = {std::min(mini.x, p.x), std::min(mini.y, p.y), std::min(mini.z, p.z)};
        maxi = {std::max(maxi.x, p.x), std::max(maxi.y, p.y), std::max(maxi.z, p.z)};
        ++nombre;
    }
};

// Échantillonne la surface d'un groupe : un point par petit cube de côté « pas ».
Nuage echantillonner(const GroupeMachine& groupe, double pas, double cote_paquet) {
    Nuage nuage(cote_paquet);
    std::unordered_set<std::uint64_t> pris;
    for (std::size_t numero = 0; numero < groupe.pieces.size(); ++numero) {
        const std::vector<float>& t = groupe.pieces[numero].triangles;
        for (std::size_t i = 0; i + 8 < t.size(); i += 9) {
            const Vec3 a{t[i], t[i + 1], t[i + 2]}, b{t[i + 3], t[i + 4], t[i + 5]},
                c{t[i + 6], t[i + 7], t[i + 8]};
            const double plus_long = std::max({norme(b - a), norme(c - a), norme(c - b)});
            const int n = std::max(1, static_cast<int>(std::ceil(plus_long / (pas * 0.7))));
            for (int u = 0; u <= n; ++u) {
                for (int w = 0; w <= n - u; ++w) {
                    const Vec3 p = a + (b - a) * (static_cast<double>(u) / n) +
                                   (c - a) * (static_cast<double>(w) / n);
                    const std::uint64_t cube = Nuage::cle(
                        static_cast<std::int64_t>(std::floor(p.x / pas)),
                        static_cast<std::int64_t>(std::floor(p.y / pas)),
                        static_cast<std::int64_t>(std::floor(p.z / pas)));
                    if (pris.insert(cube).second) {
                        nuage.ajouter(p, static_cast<std::uint16_t>(numero));
                    }
                }
            }
        }
    }
    return nuage;
}

// Repère dans lequel un nuage d'obstacles est fixe.
enum class Repere { machine, chariot, piece, module_z };

struct Obstacle {
    TypeContact type;
    Repere repere;
    Nuage nuage;
    const GroupeMachine* groupe = nullptr;  // pour le nom des pièces ; nul : matière
};

// Un groupe surveillé, avec son champ de distance.
struct Surveille {
    Mouvement mouvement;
    const GroupeMachine* groupe;
    ChampDistance champ;
    double rayon_pivot = 0.0;  // plus grande distance au pivot B, portée comprise
    double marge_propre = -1.0;  // marge particulière à ce groupe ; négatif : la marge générale
    int piece_buse = -1;         // numéro de la pièce « buse » (tête seulement)
};

// Jeu toléré entre le chariot du plateau et le bâti : ces pièces glissent
// l'une près de l'autre par construction.
constexpr double jeu_mecanique = 0.5;

}  // namespace

struct DetecteurCollisions::Interne {
    Interne(const ModeleMachine& m, const ReglagesMachine& r)
        : modele(m), machine(r), marge(r.marge_securite), zone(r.zone_buse) {}

    const ModeleMachine& modele;
    ReglagesMachine machine;
    double marge;
    double zone;
    std::vector<Surveille> surveilles;
    std::vector<Obstacle> obstacles;
    std::size_t indice_matiere = 0;
    std::unordered_set<std::uint64_t> matiere_prise;

    // Couples (surveillé, obstacle) à contrôler, chacun avec sa réserve.
    struct Couple {
        std::size_t surveille, obstacle;
        // Liaisons : vrai à la case (pièce du surveillé, pièce de l'obstacle).
        std::vector<char> liaisons;
        std::size_t pieces_obstacle = 0;
        bool liee(int piece_surveille, int piece_obstacle) const {
            return !liaisons.empty() &&
                   liaisons[static_cast<std::size_t>(piece_surveille) * pieces_obstacle +
                            static_cast<std::size_t>(piece_obstacle)] != 0;
        }
        // Axes dont dépend ce couple : inutile de recontrôler quand seuls
        // les autres axes bougent.
        bool axe_x = false, axe_y = false, axe_z = false, axe_b = false, axe_c = false;
        bool valide = false;                      // faux : contrôle complet obligatoire
        double reserve = 0.0;
        PositionMachine dernier;
    };
    std::vector<Couple> couples;
    std::size_t complets = 0;
    Vec3 pointe_piece;  // dernière position de la pointe, repère de la pièce

    // Majorant du déplacement relatif des deux éléments du couple entre deux
    // positions de la machine, en mm. Seuls comptent les axes du couple.
    double deplacement(const Couple& couple, const PositionMachine& a,
                       const PositionMachine& b) const {
        return (couple.axe_x ? std::abs(a.x - b.x) : 0.0) +
               (couple.axe_y ? std::abs(a.y - b.y) : 0.0) +
               (couple.axe_z ? std::abs(a.z - b.z) : 0.0) +
               (couple.axe_b ? en_radians(std::abs(a.b - b.b)) *
                                   surveilles[couple.surveille].rayon_pivot
                             : 0.0) +
               (couple.axe_c ? en_radians(std::abs(a.c - b.c)) * (machine.rayon_plateau + 20.0)
                             : 0.0);
    }


    // Transformation du repère de l'obstacle vers le repère (du dessin) du
    // groupe surveillé, pour une position de la machine.
    Transformation vers_groupe(Mouvement groupe, Repere repere, const PositionMachine& p) const {
        // Déplacements depuis la position du dessin (buse posée à « pointe_buse »).
        const double dx = p.x - machine.centre_plateau_x - modele.pointe_buse.x;
        const double dy = p.y - machine.centre_plateau_y - modele.pointe_buse.y;
        const double dz = p.z - modele.pointe_buse.z;
        Transformation t;
        // 1. De l'obstacle vers le repère fixe « buse au centre ».
        if (repere == Repere::piece) {
            // Le plateau a tourné de C et reculé de Y.
            t = rotation('z', en_radians(p.c) * machine.sens_c, {});
            t = enchainer(t, translation({0.0, -dy, 0.0}));
        } else if (repere == Repere::chariot) {
            t = translation({0.0, -dy, 0.0});
        } else if (repere == Repere::module_z) {
            t = translation({0.0, 0.0, dz});
        }
        // 2. Du repère fixe vers le groupe, en défaisant son mouvement.
        if (groupe == Mouvement::chariot_y) {
            return enchainer(t, translation({0.0, dy, 0.0}));  // le chariot a reculé de Y
        }
        if (groupe == Mouvement::axe_z) {
            return enchainer(t, translation({0.0, 0.0, -dz}));
        }
        t = enchainer(t, translation({-dx, 0.0, -dz}));
        if (groupe == Mouvement::tete) {
            const double b = en_radians(p.b) * machine.sens_b;
            const bool autour_de_y = machine.axe_pivot_b == AxePivot::y;
            t = enchainer(t, rotation(autour_de_y ? 'y' : 'x', autour_de_y ? -b : b,
                                      modele.pivot_b));
        }
        return t;
    }

    // Contrôle complet d'un couple. Rend le contact trouvé, sinon met à jour
    // la réserve. « chercher_liaisons » : remplit la liste des liaisons au
    // lieu de signaler les contacts.
    std::optional<Contact> controler_couple(Couple& couple, const PositionMachine& p,
                                            bool chercher_liaisons) {
        const Surveille& s = surveilles[couple.surveille];
        const Obstacle& o = obstacles[couple.obstacle];
        const Nuage& nuage = o.nuage;
        couple.dernier = p;
        couple.valide = true;
        couple.reserve = s.champ.portee();
        if (nuage.nombre == 0 || s.champ.vide()) {
            return std::nullopt;
        }
        const Transformation t = vers_groupe(s.mouvement, o.repere, p);
        const bool zone_active = o.repere == Repere::piece && s.mouvement == Mouvement::tete;
        const double rayon_paquet = nuage.cote_paquet * 0.8661;  // demi-diagonale du cube
        const double marge_groupe = s.marge_propre >= 0.0 ? s.marge_propre : marge;
        const double seuil = chercher_liaisons ? marge_groupe + s.champ.pas() : marge_groupe;
        const double seuil_buse = std::min(seuil, machine.marge_buse);

        // Paquets de l'obstacle qui peuvent toucher la boîte du groupe : on
        // ramène la boîte du groupe (portée comprise) dans le repère de l'obstacle.
        Transformation inverse;  // transposée de la rotation
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                inverse.m[i * 3 + j] = t.m[j * 3 + i];
            }
        }
        inverse.t = {};
        const Vec3 tr = inverse.appliquer(t.t);
        inverse.t = {-tr.x, -tr.y, -tr.z};
        const double bord = seuil + s.champ.portee();
        Vec3 bas{1e30, 1e30, 1e30}, haut{-1e30, -1e30, -1e30};
        for (int coin = 0; coin < 8; ++coin) {
            const Vec3 local{(coin & 1) ? s.champ.maxi().x + bord : s.champ.mini().x - bord,
                             (coin & 2) ? s.champ.maxi().y + bord : s.champ.mini().y - bord,
                             (coin & 4) ? s.champ.maxi().z + bord : s.champ.mini().z - bord};
            const Vec3 q = inverse.appliquer(local);
            bas = {std::min(bas.x, q.x), std::min(bas.y, q.y), std::min(bas.z, q.z)};
            haut = {std::max(haut.x, q.x), std::max(haut.y, q.y), std::max(haut.z, q.z)};
        }
        bas = {std::max(bas.x, nuage.mini.x), std::max(bas.y, nuage.mini.y),
               std::max(bas.z, nuage.mini.z)};
        haut = {std::min(haut.x, nuage.maxi.x), std::min(haut.y, nuage.maxi.y),
                std::min(haut.z, nuage.maxi.z)};
        if (bas.x > haut.x || bas.y > haut.y || bas.z > haut.z) {
            return std::nullopt;  // l'obstacle est entièrement hors de portée
        }

        std::optional<Contact> contact;
        double pire = std::numeric_limits<double>::max();
        auto examiner = [&](const std::vector<Nuage::Point>& points) {
            for (const Nuage::Point& point : points) {
                const ChampDistance::Lecture lecture = s.champ.lire(t.appliquer(point.position));
                // Deux pièces liées par construction (guidage) ne comptent pas.
                if (o.groupe && !chercher_liaisons && couple.liee(lecture.piece, point.piece)) {
                    continue;
                }
                // La buse travaille au ras de la matière : marge réduite pour elle.
                const bool buse = zone_active && lecture.piece == s.piece_buse;
                double reserve = lecture.distance - (buse ? seuil_buse : seuil);
                if (zone_active) {
                    // Zone libre autour de la pointe de la buse.
                    const double rho = norme(point.position - pointe_piece);
                    if (rho < zone) {
                        // Ce point est protégé tant qu'il reste dans la zone,
                        // ou tant que sa propre distance suffit.
                        couple.reserve = std::min(couple.reserve, std::max(reserve, zone - rho));
                        continue;
                    }
                }
                if (reserve < 0.0) {
                    if (chercher_liaisons) {
                        couple.liaisons[static_cast<std::size_t>(lecture.piece) *
                                            couple.pieces_obstacle +
                                        point.piece] = 1;
                        continue;
                    }
                    if (lecture.distance < pire) {
                        pire = lecture.distance;
                        contact = Contact{
                            o.type, s.groupe->pieces[static_cast<std::size_t>(lecture.piece)].nom,
                            o.groupe ? o.groupe->pieces[point.piece].nom
                                     : std::string("matière déjà imprimée"),
                            lecture.distance};
                    }
                } else {
                    couple.reserve = std::min(couple.reserve, reserve);
                }
            }
        };

        const std::int64_t i0 = nuage.case_de(bas.x), i1 = nuage.case_de(haut.x);
        const std::int64_t j0 = nuage.case_de(bas.y), j1 = nuage.case_de(haut.y);
        const std::int64_t k0 = nuage.case_de(bas.z), k1 = nuage.case_de(haut.z);
        const double cases = static_cast<double>(i1 - i0 + 1) * static_cast<double>(j1 - j0 + 1) *
                             static_cast<double>(k1 - k0 + 1);
        auto examiner_paquet = [&](std::int64_t i, std::int64_t j, std::int64_t k,
                                   const std::vector<Nuage::Point>& points) {
            // Si le centre du paquet est loin, aucun de ses points ne peut toucher.
            const Vec3 centre{(static_cast<double>(i) + 0.5) * nuage.cote_paquet,
                              (static_cast<double>(j) + 0.5) * nuage.cote_paquet,
                              (static_cast<double>(k) + 0.5) * nuage.cote_paquet};
            const double libre = s.champ.lire(t.appliquer(centre)).distance - rayon_paquet - seuil;
            // Un paquet « à peine » hors de portée est examiné point par point :
            // sinon il réduirait la réserve à presque rien sans raison.
            if (libre > 3.0 && !(zone_active && norme(centre - pointe_piece) < zone + rayon_paquet)) {
                couple.reserve = std::min(couple.reserve, libre);
                return;
            }
            examiner(points);
        };
        if (cases <= static_cast<double>(nuage.paquets.size())) {
            for (std::int64_t k = k0; k <= k1; ++k) {
                for (std::int64_t j = j0; j <= j1; ++j) {
                    for (std::int64_t i = i0; i <= i1; ++i) {
                        const auto trouve = nuage.paquets.find(Nuage::cle(i, j, k));
                        if (trouve != nuage.paquets.end()) {
                            examiner_paquet(i, j, k, trouve->second);
                        }
                    }
                }
            }
        } else {
            for (const auto& [cle, points] : nuage.paquets) {
                const Vec3& p0 = points.front().position;
                const std::int64_t i = nuage.case_de(p0.x), j = nuage.case_de(p0.y),
                                   k = nuage.case_de(p0.z);
                if (i >= i0 && i <= i1 && j >= j0 && j <= j1 && k >= k0 && k <= k1) {
                    examiner_paquet(i, j, k, points);
                }
            }
        }
        if (contact) {
            couple.valide = false;
        }
        couple.reserve = std::max(0.0, couple.reserve) * 0.8;
        return contact;
    }
};

DetecteurCollisions::DetecteurCollisions(const ModeleMachine& modele,
                                         const ReglagesMachine& machine)
    : interne_(std::make_unique<Interne>(modele, machine)) {
    Interne& d = *interne_;
    const double marge = d.marge;
    const double portee = 12.0;

    // Le réglage de distance pivot-buse doit correspondre au modèle.
    const double distance_modele = norme(modele.pivot_b - modele.pointe_buse);
    if (machine.distance_pivot_buse &&
        std::abs(*machine.distance_pivot_buse - distance_modele) > 0.5) {
        throw ErreurMachine(std::format(
            "Le réglage « distance_pivot_buse » ({:.2f} mm) ne correspond pas au modèle de "
            "machine ({:.2f} mm). Il faut corriger l'un ou l'autre.",
            *machine.distance_pivot_buse, distance_modele));
    }

    // Groupes surveillés : la tête finement, le bras et le module Z au millimètre.
    const std::pair<Mouvement, double> finesses[] = {
        {Mouvement::tete, 0.5},
        {Mouvement::chariot_x, 1.0},
        {Mouvement::axe_z, 1.0},
        {Mouvement::chariot_y, 1.0}};
    for (const auto& [mouvement, pas] : finesses) {
        if (const GroupeMachine* groupe = modele.groupe(mouvement)) {
            Surveille s{mouvement, groupe, ChampDistance(*groupe, pas, portee), 0.0};
            for (int coin = 0; coin < 8; ++coin) {
                const Vec3 p{(coin & 1) ? s.champ.maxi().x : s.champ.mini().x,
                             (coin & 2) ? s.champ.maxi().y : s.champ.mini().y,
                             (coin & 4) ? s.champ.maxi().z : s.champ.mini().z};
                s.rayon_pivot = std::max(s.rayon_pivot, norme(p - modele.pivot_b));
            }
            s.rayon_pivot += portee + marge;
            if (mouvement == Mouvement::chariot_y) {
                s.marge_propre = std::min(marge, jeu_mecanique);
            }
            if (mouvement == Mouvement::tete) {
                // La buse : la pièce qui se trouve juste au-dessus de la pointe.
                s.piece_buse = s.champ.lire(modele.pointe_buse + Vec3{0.0, 0.0, 0.3}).piece;
            }
            d.surveilles.push_back(std::move(s));
        }
    }

    // Obstacles.
    auto ajouter = [&](Mouvement mouvement, TypeContact type, Repere repere, double pas,
                       double paquet) {
        if (const GroupeMachine* groupe = modele.groupe(mouvement)) {
            d.obstacles.push_back({type, repere, echantillonner(*groupe, pas, paquet), groupe});
        }
    };
    ajouter(Mouvement::plateau, TypeContact::plateau, Repere::piece, 1.0, 8.0);
    ajouter(Mouvement::chariot_y, TypeContact::plateau, Repere::chariot, 1.5, 12.0);
    ajouter(Mouvement::fixe, TypeContact::machine, Repere::machine, 1.5, 16.0);
    ajouter(Mouvement::axe_z, TypeContact::machine, Repere::module_z, 1.5, 12.0);
    d.indice_matiere = d.obstacles.size();
    d.obstacles.push_back({TypeContact::matiere, Repere::piece, Nuage(4.0), nullptr});

    // Couples à contrôler. Le module Z n'est comparé qu'à la tête (le bras
    // coulisse dedans) ; il ne se compare pas à lui-même.
    for (std::size_t s = 0; s < d.surveilles.size(); ++s) {
        for (std::size_t o = 0; o < d.obstacles.size(); ++o) {
            const Mouvement m = d.surveilles[s].mouvement;
            const Repere repere = d.obstacles[o].repere;
            if (m == Mouvement::chariot_y && repere != Repere::machine) {
                continue;  // le chariot du plateau n'est comparé qu'au bâti
            }
            if (repere == Repere::module_z && m != Mouvement::tete) {
                continue;
            }
            if (m == Mouvement::axe_z && repere == Repere::piece && o != d.indice_matiere) {
                continue;  // le module Z est loin du plateau : couvert par la matière
            }
            Interne::Couple couple;
            couple.surveille = s;
            couple.obstacle = o;
            if (const GroupeMachine* groupe = d.obstacles[o].groupe) {
                couple.pieces_obstacle = groupe->pieces.size();
                couple.liaisons.assign(
                    d.surveilles[s].groupe->pieces.size() * couple.pieces_obstacle, 0);
            }
            // Axes qui déplacent le groupe surveillé, et axes qui déplacent l'obstacle.
            const bool suit_xz = m == Mouvement::tete || m == Mouvement::chariot_x;
            const bool obstacle_suit_y = repere == Repere::piece || repere == Repere::chariot;
            couple.axe_x = suit_xz;
            couple.axe_z = (suit_xz || m == Mouvement::axe_z) != (repere == Repere::module_z);
            couple.axe_b = m == Mouvement::tete;
            couple.axe_y = (m == Mouvement::chariot_y) != obstacle_suit_y;
            couple.axe_c = repere == Repere::piece;
            d.couples.push_back(std::move(couple));
        }
    }

    // Liaisons : les pièces qui se touchent déjà dans le dessin, machine au
    // repos. Seulement entre pièces de la machine (pas avec le plateau : la
    // buse y est posée dans le dessin, ce n'est pas une liaison).
    PositionMachine repos;
    repos.x = machine.centre_plateau_x + modele.pointe_buse.x;
    repos.y = machine.centre_plateau_y + modele.pointe_buse.y;
    repos.z = modele.pointe_buse.z;
    d.pointe_piece = pose_dans_piece(machine, repos).pointe;
    for (Interne::Couple& couple : d.couples) {
        if (d.obstacles[couple.obstacle].type == TypeContact::machine) {
            d.controler_couple(couple, repos, true);
        }
        couple.valide = false;
    }
    d.complets = 0;
}

DetecteurCollisions::~DetecteurCollisions() = default;

DetecteurCollisions::DetecteurCollisions(const DetecteurCollisions& autre)
    : interne_(std::make_unique<Interne>(*autre.interne_)) {}

void DetecteurCollisions::deposer(const Vec3& point) {
    Interne& d = *interne_;
    // Un point par cube de 1 mm suffit.
    const std::uint64_t cube = Nuage::cle(static_cast<std::int64_t>(std::floor(point.x)),
                                          static_cast<std::int64_t>(std::floor(point.y)),
                                          static_cast<std::int64_t>(std::floor(point.z)));
    if (!d.matiere_prise.insert(cube).second) {
        return;
    }
    d.obstacles[d.indice_matiere].nuage.ajouter(point, 0);
    // Cette matière nouvelle n'était pas connue au dernier contrôle : la
    // réserve des couples « matière » est réduite à ce qui reste sûr.
    const double rho = norme(point - d.pointe_piece);
    for (Interne::Couple& couple : d.couples) {
        if (couple.obstacle != d.indice_matiere) {
            continue;
        }
        if (d.surveilles[couple.surveille].mouvement == Mouvement::tete && rho < d.zone) {
            couple.reserve = std::min(couple.reserve, (d.zone - rho) * 0.8);
        } else {
            couple.valide = false;
        }
    }
}

std::optional<Contact> DetecteurCollisions::controler(const PositionMachine& position) {
    Interne& d = *interne_;
    d.pointe_piece = pose_dans_piece(d.machine, position).pointe;
    for (Interne::Couple& couple : d.couples) {
        if (couple.valide) {
            const double bouge = d.deplacement(couple, couple.dernier, position);
            if (bouge < couple.reserve) {
                continue;
            }
        }
        ++d.complets;
        if (auto contact = d.controler_couple(couple, position, false)) {
            return contact;
        }
    }
    return std::nullopt;
}

std::size_t DetecteurCollisions::controles_complets() const { return interne_->complets; }

}  // namespace artprint
