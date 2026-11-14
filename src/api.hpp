// ArtPrint Slicer - licence AGPL v3
// Commandes appelées par l'interface graphique. Chaque demande et chaque
// réponse est un texte JSON : l'interface (HTML/JavaScript) et le cœur du
// slicer (C++) ne se parlent que par là.
//
// Demande :  {"commande": "trancher", ...}
// Réponse :  {"ok": true, ...}   ou   {"ok": false, "erreur": "message"}
#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace artprint {

// Une session de travail : le modèle ouvert, les réglages, le dernier G-code.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Exécute une commande. Ne lève jamais d'exception : une erreur est
    // rendue dans la réponse, avec un message en français.
    //
    // Commandes :
    //   infos                    nom et version du logiciel
    //   reglages                 liste des réglages avec leur valeur
    //   definir_reglages         {"valeurs": {"hauteur_couche": "0,2", ...}}
    //   reglages_par_defaut      remet tous les réglages d'origine
    //   charger_reglages         {"chemin": "..."}
    //   enregistrer_reglages     {"chemin": "..."}
    //   charger_modele           {"chemin": "..."} : rend aussi les triangles
    //   trancher                 tranche le modèle ouvert, rend l'aperçu
    //   enregistrer_gcode        {"chemin": "..."}
    std::string executer(std::string_view demande_json);

private:
    struct Etat;
    std::unique_ptr<Etat> etat_;
    std::string executer_sous_verrou(std::string_view demande_json);
};

}  // namespace artprint
