// ArtPrint Slicer - licence AGPL v3
#include "reglages.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <utility>

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
std::optional<double> lire_nombre(std::string_view valeur) {
    std::string texte(valeur);
    std::replace(texte.begin(), texte.end(), ',', '.');
    double nombre = 0.0;
    const char* debut = texte.data();
    const char* fin = texte.data() + texte.size();
    if (debut != fin && *debut == '+') {
        ++debut;
    }
    const auto resultat = std::from_chars(debut, fin, nombre);
    if (resultat.ec != std::errc() || resultat.ptr != fin || !std::isfinite(nombre)) {
        return std::nullopt;
    }
    return nombre;
}

// Écrit un nombre sans zéros inutiles, avec une virgule : 0,45 ; 150 ; -32,6.
std::string ecrire_nombre(double nombre) {
    std::string texte = std::format("{:.6f}", nombre);
    texte.erase(texte.find_last_not_of('0') + 1);
    if (texte.back() == '.') {
        texte.pop_back();
    }
    if (texte == "-0") {
        texte = "0";
    }
    std::replace(texte.begin(), texte.end(), '.', ',');
    return texte;
}

// --- Fabrication de la liste des réglages --------------------------------

struct Texte {
    const char* cle;
    const char* groupe;
    const char* libelle;
    const char* unite;
    const char* aide;
};

DescriptionReglage base(const Texte& t, TypeReglage type, double min, double max) {
    DescriptionReglage d;
    d.cle = t.cle, d.groupe = t.groupe, d.libelle = t.libelle, d.unite = t.unite, d.aide = t.aide;
    d.type = type, d.min = min, d.max = max;
    return d;
}

template <class Section>
DescriptionReglage nombre(Section Reglages::*section, double Section::*champ, const Texte& t,
                          double min, double max) {
    DescriptionReglage d = base(t, TypeReglage::nombre, min, max);
    d.lire = [=](const Reglages& r) { return std::optional<double>((r.*section).*champ); };
    d.ecrire = [=](Reglages& r, std::optional<double> v) { (r.*section).*champ = v.value(); };
    return d;
}

template <class Section>
DescriptionReglage facultatif(Section Reglages::*section, std::optional<double> Section::*champ,
                              const Texte& t, double min, double max) {
    DescriptionReglage d = base(t, TypeReglage::nombre, min, max);
    d.facultatif = true;
    d.lire = [=](const Reglages& r) { return (r.*section).*champ; };
    d.ecrire = [=](Reglages& r, std::optional<double> v) { (r.*section).*champ = v; };
    return d;
}

template <class Section>
DescriptionReglage entier(Section Reglages::*section, int Section::*champ, const Texte& t,
                          int min, int max) {
    DescriptionReglage d = base(t, TypeReglage::entier, min, max);
    d.lire = [=](const Reglages& r) {
        return std::optional<double>(static_cast<double>((r.*section).*champ));
    };
    d.ecrire = [=](Reglages& r, std::optional<double> v) {
        (r.*section).*champ = static_cast<int>(v.value());
    };
    return d;
}

template <class Section>
DescriptionReglage booleen(Section Reglages::*section, bool Section::*champ, const Texte& t) {
    DescriptionReglage d = base(t, TypeReglage::booleen, 0, 1);
    d.lire = [=](const Reglages& r) {
        return std::optional<double>((r.*section).*champ ? 1.0 : 0.0);
    };
    d.ecrire = [=](Reglages& r, std::optional<double> v) { (r.*section).*champ = v.value() != 0.0; };
    return d;
}

// Sens d'un axe : 1 ou -1.
DescriptionReglage sens(int ReglagesMachine::*champ, const Texte& t) {
    DescriptionReglage d = base(t, TypeReglage::choix, -1, 1);
    d.choix = {"1", "-1"};
    d.valeurs_choix = {1.0, -1.0};
    d.lire = [=](const Reglages& r) {
        return std::optional<double>(static_cast<double>(r.machine.*champ));
    };
    d.ecrire = [=](Reglages& r, std::optional<double> v) {
        r.machine.*champ = static_cast<int>(v.value());
    };
    return d;
}

std::vector<DescriptionReglage> construire() {
    const auto M = &Reglages::machine;
    const auto I = &Reglages::impression;
    const auto V = &Reglages::verification;
    using RM = ReglagesMachine;
    using RI = ReglagesImpression;
    using RV = ReglagesVerification;
    const char* qualite = "Qualité";
    const char* solidite = "Solidité";
    const char* vitesse = "Vitesse";
    const char* matiere = "Matière";
    const char* autres = "Autres";
    const char* machine = "Machine";
    const char* controle = "Vérification";

    std::vector<DescriptionReglage> d;

    // --- Qualité ---
    d.push_back(nombre(I, &RI::hauteur_couche, {"hauteur_couche", qualite, "Hauteur de couche", "mm",
        "Épaisseur de chaque couche. Plus fin : plus beau mais plus long."}, 0.02, 2.0));
    d.push_back(nombre(I, &RI::hauteur_premiere_couche, {"hauteur_premiere_couche", qualite,
        "Hauteur de la première couche", "mm",
        "Épaisseur de la couche posée sur le plateau."}, 0.02, 2.0));
    d.push_back(nombre(I, &RI::largeur_extrusion, {"largeur_extrusion", qualite, "Largeur d'extrusion", "mm",
        "Largeur du cordon déposé. En général un peu plus que le diamètre de la buse."}, 0.05, 5.0));

    d.push_back(booleen(I, &RI::peau_courbe, {"peau_courbe", qualite, "Peau courbe (dessus lisse)", "",
        "Une dernière passe suit la forme du dessus de la pièce pour effacer les marches "
        "d'escalier. L'intérieur reste en couches planes."}));
    d.push_back(nombre(I, &RI::peau_courbe_pente_max, {"peau_courbe_pente_max", qualite,
        "Pente maximale de la peau courbe", "°",
        "Pente la plus raide que la buse suit pendant la passe de peau courbe."}, 5.0, 60.0));

    // --- Solidité ---
    d.push_back(entier(I, &RI::nombre_parois, {"nombre_parois", solidite, "Nombre de parois", "",
        "Nombre de contours autour de chaque couche."}, 1, 20));
    d.push_back(entier(I, &RI::couches_dessus, {"couches_dessus", solidite, "Couches pleines du dessus", "",
        "Nombre de couches pleines qui ferment le haut de la pièce."}, 0, 1000));
    d.push_back(entier(I, &RI::couches_dessous, {"couches_dessous", solidite, "Couches pleines du dessous", "",
        "Nombre de couches pleines qui ferment le bas de la pièce."}, 0, 1000));
    d.push_back(nombre(I, &RI::densite_remplissage, {"densite_remplissage", solidite,
        "Densité du remplissage", "%", "0 : pièce creuse. 100 : pièce pleine."}, 0.0, 100.0));
    d.push_back(nombre(I, &RI::recouvrement_remplissage, {"recouvrement_remplissage", solidite,
        "Recouvrement remplissage-parois", "",
        "Part de la largeur du cordon dont le remplissage mord sur les parois."}, 0.0, 0.5));

    // --- Vitesse ---
    d.push_back(nombre(I, &RI::vitesse_paroi_externe, {"vitesse_paroi_externe", vitesse,
        "Paroi externe", "mm/s", "Vitesse de la paroi visible."}, 1.0, 1000.0));
    d.push_back(nombre(I, &RI::vitesse_paroi_interne, {"vitesse_paroi_interne", vitesse,
        "Parois internes", "mm/s", "Vitesse des parois cachées et de la jupe."}, 1.0, 1000.0));
    d.push_back(nombre(I, &RI::vitesse_remplissage, {"vitesse_remplissage", vitesse,
        "Remplissage", "mm/s", "Vitesse du remplissage."}, 1.0, 1000.0));
    d.push_back(nombre(I, &RI::vitesse_premiere_couche, {"vitesse_premiere_couche", vitesse,
        "Première couche", "mm/s", "Vitesse de toute la première couche."}, 1.0, 1000.0));
    d.push_back(nombre(I, &RI::vitesse_a_vide, {"vitesse_a_vide", vitesse,
        "Déplacements à vide", "mm/s", "Vitesse des trajets sans extrusion."}, 1.0, 2000.0));

    // --- Matière ---
    d.push_back(nombre(I, &RI::diametre_filament, {"diametre_filament", matiere, "Diamètre du filament", "mm",
        "1,75 mm pour la plupart des bobines."}, 0.5, 5.0));
    d.push_back(nombre(I, &RI::debit, {"debit", matiere, "Débit", "",
        "Multiplicateur de matière. 1 : normal."}, 0.1, 3.0));
    d.push_back(nombre(I, &RI::temperature_buse, {"temperature_buse", matiere, "Température de la buse", "°C",
        "Température d'impression de la matière."}, 0.0, 500.0));
    d.push_back(nombre(I, &RI::temperature_plateau, {"temperature_plateau", matiere,
        "Température du plateau", "°C", "0 : pas de plateau chauffant."}, 0.0, 200.0));
    d.push_back(booleen(I, &RI::ventilation, {"ventilation", matiere, "Ventilateur de pièce", "",
        "Allume le ventilateur à partir de la deuxième couche."}));
    d.push_back(nombre(I, &RI::duree_preparation, {"duree_preparation", matiere,
        "Préparation avant l'impression", "min",
        "Chauffe et palpage du plateau : ce temps est ajouté au temps d'impression dans les estimations."},
        0.0, 120.0));
    d.push_back(nombre(I, &RI::retraction_longueur, {"retraction_longueur", matiere,
        "Longueur de rétraction", "mm",
        "Filament retiré avant chaque déplacement à vide."}, 0.05, 30.0));
    d.push_back(nombre(I, &RI::retraction_vitesse, {"retraction_vitesse", matiere,
        "Vitesse de rétraction", "mm/s", "Vitesse du retrait et de la reprise du filament."}, 1.0, 300.0));

    // --- Autres ---
    d.push_back(nombre(I, &RI::levee_z, {"levee_z", autres, "Levée de la buse", "mm",
        "La buse monte de cette hauteur pendant les déplacements à vide."}, 0.0, 10.0));
    d.push_back(entier(I, &RI::jupe_boucles, {"jupe_boucles", autres, "Boucles de jupe", "",
        "Boucles tracées autour de la pièce pour amorcer la buse. 0 : aucune."}, 0, 20));
    d.push_back(nombre(I, &RI::jupe_distance, {"jupe_distance", autres, "Distance de la jupe", "mm",
        "Écart entre la jupe et la pièce."}, 0.1, 100.0));

    // --- Machine ---
    d.push_back(nombre(M, &RM::rayon_plateau, {"rayon_plateau", machine, "Rayon du plateau", "mm",
        "Rayon du plateau rond."}, 1.0, 1000.0));
    d.push_back(nombre(M, &RM::hauteur_impression, {"hauteur_impression", machine,
        "Hauteur d'impression", "mm", "Hauteur maximale d'une pièce."}, 1.0, 2000.0));
    d.push_back(facultatif(M, &RM::distance_pivot_buse, {"distance_pivot_buse", machine,
        "Distance pivot B - buse", "mm",
        "Distance entre l'axe du pivot B et la pointe de la buse. À mesurer sur la machine."},
        0.1, 1000.0));
    {
        DescriptionReglage axe = base({"axe_pivot_b", machine, "Axe du pivot B", "",
            "Axe horizontal autour duquel la tête s'incline."}, TypeReglage::choix, 0, 1);
        axe.choix = {"X", "Y"};
        axe.valeurs_choix = {0.0, 1.0};
        axe.lire = [](const Reglages& r) {
            return std::optional<double>(r.machine.axe_pivot_b == AxePivot::x ? 0.0 : 1.0);
        };
        axe.ecrire = [](Reglages& r, std::optional<double> v) {
            r.machine.axe_pivot_b = v.value() == 0.0 ? AxePivot::x : AxePivot::y;
        };
        d.push_back(std::move(axe));
    }
    d.push_back(sens(&RM::sens_b, {"sens_b", machine, "Sens de l'axe B", "",
        "Sens d'inclinaison de la tête pour un B positif."}));
    d.push_back(sens(&RM::sens_c, {"sens_c", machine, "Sens de l'axe C", "",
        "Sens de rotation du plateau pour un C positif."}));
    d.push_back(nombre(M, &RM::centre_plateau_x, {"centre_plateau_x", machine, "Centre du plateau en X", "mm",
        "Position X (G-code) du centre du plateau."}, -10000.0, 10000.0));
    d.push_back(nombre(M, &RM::centre_plateau_y, {"centre_plateau_y", machine, "Centre du plateau en Y", "mm",
        "Position Y (G-code) du centre du plateau."}, -10000.0, 10000.0));
    const struct {
        std::optional<double> RM::*champ;
        const char* cle;
        const char* libelle;
    } courses[] = {
        {&RM::course_x_min, "course_x_min", "Course X minimale"},
        {&RM::course_x_max, "course_x_max", "Course X maximale"},
        {&RM::course_y_min, "course_y_min", "Course Y minimale"},
        {&RM::course_y_max, "course_y_max", "Course Y maximale"},
        {&RM::course_z_min, "course_z_min", "Course Z minimale"},
        {&RM::course_z_max, "course_z_max", "Course Z maximale"},
    };
    for (const auto& course : courses) {
        d.push_back(facultatif(M, course.champ, {course.cle, machine, course.libelle, "mm",
            "Limite de l'axe dans les coordonnées du G-code. Vide : non vérifiée."},
            -10000.0, 10000.0));
    }
    d.push_back(nombre(M, &RM::b_vertical, {"b_vertical", machine,
        "Valeur de B quand la buse est verticale", "°",
        "Angle écrit dans le G-code pour une buse verticale (0 sur la plupart des machines)."},
        -360.0, 360.0));
    d.push_back(nombre(M, &RM::b_min, {"b_min", machine, "Butée basse de B", "°",
        "Inclinaison la plus négative de la tête, comptée depuis la verticale."}, -180.0, 180.0));
    d.push_back(nombre(M, &RM::b_max, {"b_max", machine, "Butée haute de B", "°",
        "Inclinaison la plus positive de la tête, comptée depuis la verticale."}, -180.0, 180.0));
    d.push_back(nombre(M, &RM::b_inclinaison_max, {"b_inclinaison_max", machine,
        "Inclinaison maximale de B", "°",
        "Limite du firmware : le slicer ne la dépasse jamais. Une partie qui demande plus est signalée. "
        "Au-delà de 90°, la buse pointe vers le haut et imprime par en dessous (les butées de B doivent le permettre)."},
        0.0, 180.0));
    d.push_back(nombre(M, &RM::vitesse_b_max, {"vitesse_b_max", machine, "Vitesse maximale de B", "°/s",
        "Limite du firmware, pour estimer la durée."}, 0.1, 1000.0));
    d.push_back(nombre(M, &RM::vitesse_c_max, {"vitesse_c_max", machine, "Vitesse maximale de C", "°/s",
        "Limite du firmware, pour estimer la durée."}, 0.1, 1000.0));
    d.push_back(entier(M, &RM::contrat_gcode, {"contrat_gcode", machine, "Contrat du G-code", "",
        "1 : positions de la pointe de la buse et macros ARTPRINT (firmware ArtPrint). "
        "0 : ancien format, positions des axes."}, 0, 1));
    d.push_back(nombre(M, &RM::b_limite_pres_plateau, {"b_limite_pres_plateau", machine,
        "Inclinaison maximale près du plateau", "°",
        "Limite de B sous la hauteur de bascule."}, 0.0, 180.0));
    d.push_back(facultatif(M, &RM::hauteur_bascule_b, {"hauteur_bascule_b", machine,
        "Hauteur de bascule de B", "mm",
        "Hauteur au-dessus de laquelle B peut dépasser la limite précédente. À mesurer."},
        0.0, 2000.0));
    d.push_back(facultatif(M, &RM::tete_demi_largeur, {"tete_demi_largeur", machine,
        "Demi-largeur du bloc de la tête", "mm",
        "De chaque côté de l'axe de la buse, à partir du pivot. À mesurer. Avec la hauteur du bloc, "
        "remplace la hauteur de bascule : la hauteur où B peut dépasser la limite près du plateau est calculée."},
        0.0, 500.0));
    d.push_back(facultatif(M, &RM::tete_hauteur, {"tete_hauteur", machine,
        "Hauteur du bloc de la tête", "mm",
        "Longueur du bloc au-delà du pivot, du côté opposé à la buse. À mesurer."},
        0.0, 500.0));
    d.push_back(booleen(M, &RM::pivot_continu, {"pivot_continu", machine,
        "Pivot continu (5 axes continu)", "",
        "Sur une branche courbe, la tête pivote pendant qu'elle imprime. "
        "Décoché : elle pivote entre deux couches, sans extruder."}));
    d.push_back(facultatif(M, &RM::c_min, {"c_min", machine, "Butée basse de C", "°",
        "Vide : le plateau tourne sans limite. Avant cette butée, le plateau est déroulé à vide."}, -1.0e6, 1.0e6));
    d.push_back(facultatif(M, &RM::c_max, {"c_max", machine, "Butée haute de C", "°",
        "Vide : le plateau tourne sans limite. Avant cette butée, le plateau est déroulé à vide."}, -1.0e6, 1.0e6));

    {
        DescriptionReglage modele = base({"modele_machine", machine, "Modèle de machine", "",
            "Fichier de la forme réelle de l'imprimante (fabriqué à partir du STEP). "
            "Vide : les collisions ne sont pas vérifiées."}, TypeReglage::texte, 0, 0);
        modele.facultatif = true;
        modele.lire_texte = [](const Reglages& r) { return r.machine.modele_machine; };
        modele.ecrire_texte = [](Reglages& r, std::string v) { r.machine.modele_machine = std::move(v); };
        d.push_back(std::move(modele));
    }
    d.push_back(nombre(M, &RM::marge_securite, {"marge_securite", machine, "Marge de sécurité", "mm",
        "Distance minimale à garder entre la tête, le bras et tout le reste."}, 0.0, 50.0));
    d.push_back(nombre(M, &RM::zone_buse, {"zone_buse", machine, "Zone libre autour de la buse", "mm",
        "Rayon autour de la pointe où le contact avec la matière est normal."}, 0.5, 30.0));
    d.push_back(nombre(M, &RM::marge_buse, {"marge_buse", machine, "Marge de la buse", "mm",
        "Marge propre à la buse, qui travaille au ras de la matière et du plateau."}, 0.0, 10.0));

    // --- Vérification ---
    d.push_back(nombre(V, &RV::pas_controle, {"pas_controle", controle, "Finesse du contrôle", "mm",
        "Taille des petits cubes qui mémorisent la matière déposée."}, 0.01, 5.0));
    d.push_back(nombre(V, &RV::tolerance_croisement, {"tolerance_croisement", controle,
        "Tolérance de croisement", "",
        "Longueur qu'un cordon peut parcourir dans la matière, en largeurs de cordon."},
        0.1, 100.0));
    d.push_back(nombre(V, &RV::hauteur_controle_dessus, {"hauteur_controle_dessus", controle,
        "Hauteur contrôlée au-dessus de la buse", "mm",
        "Distance sur laquelle on cherche de la matière au-dessus de la pointe."}, 0.1, 500.0));
    return d;
}

const DescriptionReglage* trouver(std::string_view cle) {
    for (const DescriptionReglage& d : description_reglages()) {
        if (d.cle == cle) {
            return &d;
        }
    }
    return nullptr;
}

std::string liste_des_choix(const DescriptionReglage& d) {
    std::string texte;
    for (std::size_t i = 0; i < d.choix.size(); ++i) {
        texte += (i == 0 ? "" : " ou ") + d.choix[i];
    }
    return texte;
}

}  // namespace

const std::vector<DescriptionReglage>& description_reglages() {
    static const std::vector<DescriptionReglage> liste = construire();
    return liste;
}

void definir_reglage(Reglages& reglages, std::string_view cle, std::string_view valeur) {
    const DescriptionReglage* d = trouver(cle);
    if (!d) {
        throw ErreurReglages(std::format("réglage inconnu « {} ».", cle));
    }
    valeur = sans_espaces(valeur);
    if (d->type == TypeReglage::texte) {
        d->ecrire_texte(reglages, std::string(valeur));
        return;
    }

    if (valeur.empty()) {
        if (!d->facultatif) {
            throw ErreurReglages(std::format("« {} » ne peut pas rester vide.", cle));
        }
        d->ecrire(reglages, std::nullopt);
        return;
    }
    if (d->type == TypeReglage::choix) {
        for (std::size_t i = 0; i < d->choix.size(); ++i) {
            std::string minuscule(valeur), reference = d->choix[i];
            auto abaisser = [](std::string& t) {
                std::transform(t.begin(), t.end(), t.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            };
            abaisser(minuscule), abaisser(reference);
            if (minuscule == reference) {
                d->ecrire(reglages, d->valeurs_choix[i]);
                return;
            }
        }
        throw ErreurReglages(
            std::format("« {} » doit valoir {}.", cle, liste_des_choix(*d)));
    }

    const std::optional<double> nombre = lire_nombre(valeur);
    if (!nombre) {
        throw ErreurReglages(
            std::format("« {} » n'est pas un nombre valide pour « {} ».", valeur, cle));
    }
    if (d->type == TypeReglage::booleen) {
        if (*nombre != 0.0 && *nombre != 1.0) {
            throw ErreurReglages(std::format("« {} » doit valoir 1 (oui) ou 0 (non).", cle));
        }
    } else {
        if (*nombre < d->min || *nombre > d->max) {
            throw ErreurReglages(std::format("« {} » doit être compris entre {} et {}.", cle,
                                             ecrire_nombre(d->min), ecrire_nombre(d->max)));
        }
        if (d->type == TypeReglage::entier && *nombre != std::floor(*nombre)) {
            throw ErreurReglages(std::format("« {} » doit être un nombre entier.", cle));
        }
    }
    d->ecrire(reglages, *nombre);
}

std::string texte_reglage(const Reglages& reglages, const DescriptionReglage& d) {
    if (d.type == TypeReglage::texte) {
        return d.lire_texte(reglages);
    }
    const std::optional<double> valeur = d.lire(reglages);
    if (!valeur) {
        return "";
    }
    if (d.type == TypeReglage::choix) {
        for (std::size_t i = 0; i < d.choix.size(); ++i) {
            if (d.valeurs_choix[i] == *valeur) {
                return d.choix[i];
            }
        }
    }
    return ecrire_nombre(*valeur);
}

void verifier_coherence(const Reglages& reglages) {
    const ReglagesMachine& m = reglages.machine;
    if (m.b_min > m.b_max) {
        throw ErreurReglages("Réglages : « b_min » est plus grand que « b_max ».");
    }
    if (m.c_min && m.c_max && *m.c_min > *m.c_max) {
        throw ErreurReglages("Réglages : « c_min » est plus grand que « c_max ».");
    }
    const std::pair<const std::optional<double>*, const std::optional<double>*> courses[] = {
        {&m.course_x_min, &m.course_x_max},
        {&m.course_y_min, &m.course_y_max},
        {&m.course_z_min, &m.course_z_max}};
    const char* noms = "xyz";
    for (int i = 0; i < 3; ++i) {
        if (*courses[i].first && *courses[i].second && **courses[i].first > **courses[i].second) {
            throw ErreurReglages(std::format(
                "Réglages : « course_{0}_min » est plus grand que « course_{0}_max ».", noms[i]));
        }
    }
}

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
        try {
            definir_reglage(reglages, sans_espaces(ligne.substr(0, egal)),
                            ligne.substr(egal + 1));
        } catch (const ErreurReglages& erreur) {
            throw ErreurReglages(std::format("Réglages, ligne {} : {}", numero, erreur.what()));
        }
    }
    verifier_coherence(reglages);
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
    Reglages reglages = lire_reglages_texte(texte);
    // Un chemin de modèle de machine écrit sans dossier est cherché à côté
    // du fichier de réglages.
    std::string& modele = reglages.machine.modele_machine;
    if (!modele.empty()) {
        const std::filesystem::path chemin_modele(std::u8string(modele.begin(), modele.end()));
        if (chemin_modele.is_relative()) {
            const std::u8string complet = (chemin.parent_path() / chemin_modele).u8string();
            modele.assign(complet.begin(), complet.end());
        }
    }
    return reglages;
}

std::string ecrire_reglages(const Reglages& reglages) {
    std::string texte =
        "# Réglages d'ArtPrint Slicer.\n"
        "# Une ligne par réglage : nom = valeur. Le signe # commence un commentaire.\n";
    std::string groupe;
    for (const DescriptionReglage& d : description_reglages()) {
        if (d.groupe != groupe) {
            groupe = d.groupe;
            texte += std::format("\n# --- {} ---\n", groupe);
        }
        texte += std::format("\n# {}{} : {}\n", d.libelle,
                             d.unite.empty() ? "" : " (" + d.unite + ")", d.aide);
        const std::string valeur = texte_reglage(reglages, d);
        texte += valeur.empty() ? std::format("# {} =\n", d.cle)
                                : std::format("{} = {}\n", d.cle, valeur);
    }
    return texte;
}

}  // namespace artprint
