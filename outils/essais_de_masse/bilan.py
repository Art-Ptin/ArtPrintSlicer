# ArtPrint Slicer - licence AGPL v3
"""Résume un fichier de résultats d'essai de masse en tableaux (Markdown).

Utilisation :
    python outils/essais_de_masse/bilan.py <resultats.jsonl> [<resultats_avant.jsonl>]

Avec un second fichier (une version précédente du slicer sur les mêmes
modèles et le même tirage), ajoute la comparaison avant / après.
"""
import collections
import json
import sys

NOMS = {"verifie": "vérifié", "refuse": "refusé", "erreur": "erreur", "delai": "délai dépassé", "plantage": "plantage",
        "panne_du_script": "panne du banc"}
ORDRE = ["verifie", "refuse", "erreur", "delai", "plantage", "panne_du_script"]


def lire(chemin):
    return [json.loads(l) for l in open(chemin, encoding="utf-8") if l.strip()]


def pour_cent(n, total):
    return f"{100.0 * n / total:.1f} %" if total else "-"


def tableau(titre, lignes, cle, tri=None):
    groupes = collections.defaultdict(collections.Counter)
    for x in lignes:
        groupes[cle(x)][x["resultat"]] += 1
    presents = [r for r in ORDRE if any(g[r] for g in groupes.values())]
    print(f"\n### {titre}\n")
    print("| | modèles | " + " | ".join(NOMS[r] for r in presents) + " | part vérifiée |")
    print("|---|---:|" + "---:|" * (len(presents) + 1))
    for nom in sorted(groupes, key=tri or (lambda n: (-sum(groupes[n].values()), str(n)))):
        g = groupes[nom]
        total = sum(g.values())
        print(f"| {nom} | {total} | " + " | ".join(str(g[r]) for r in presents) + f" | {pour_cent(g['verifie'], total)} |")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    lignes = lire(sys.argv[1])
    total = len(lignes)
    compte = collections.Counter(x["resultat"] for x in lignes)
    print(f"**{total} modèles tranchés.** " + ", ".join(f"{NOMS[r]} : {compte[r]} ({pour_cent(compte[r], total)})"
                                                         for r in ORDRE if compte[r]) + ".")

    tableau("Par origine", lignes, lambda x: "modèles fabriqués" if x["source"] == "fabrique" else "pièces réelles")
    tableau("Par stratégie demandée", lignes,
            lambda x: "ondulation" if x["choix"]["strategie"] == "couches_courbes" else "classique")
    tableau("Par chemin d'entrée", lignes,
            lambda x: "ligne de commande" if x["choix"]["entree"] == "commande" else "commandes de l'interface")
    tableau("Par inclinaison maximale de la tête (ondulation seulement)",
            [x for x in lignes if x["choix"]["strategie"] == "couches_courbes"],
            lambda x: f"{x['choix']['reglages']['b_max']}°", tri=lambda n: int(n[:-1]))
    tableau("Par famille de modèles fabriqués", [x for x in lignes if x["source"] == "fabrique"], lambda x: x["famille"])
    tableau("Par dépôt de pièces réelles", [x for x in lignes if x["source"] != "fabrique"], lambda x: x["famille"])

    # Ce que l'ondulation a vraiment fait.
    courbes = [x for x in lignes if x["choix"]["strategie"] == "couches_courbes" and x["resultat"] == "verifie"]
    inclinees = [x for x in courbes if x.get("gcode", {}).get("b_max", 0) > 1.0]
    au_dela = [x for x in courbes if x.get("gcode", {}).get("b_max", 0) > 90.5]
    print("\n### Ce que l'ondulation a fait\n")
    print(f"- {len(courbes)} modèles vérifiés en ondulation ;")
    print(f"- sur {len(inclinees)} d'entre eux, la tête s'incline vraiment (les autres n'ont pas de branche : "
          "c'est le même tranchage qu'en classique) ;")
    print(f"- sur {len(au_dela)}, la tête dépasse 90° (impression par en dessous).")
    remarques = collections.Counter()
    for x in courbes:
        for r in x.get("remarques", []):
            if "à plat" in r:
                remarques["au moins une partie repassée à plat"] += 1
                break
    for x in courbes:
        if any("b_inclinaison_max" in r for r in x.get("remarques", [])):
            remarques["au moins une partie bridée par la limite d'inclinaison"] += 1
        if any("bout(s) de cordon" in r for r in x.get("remarques", [])):
            remarques["des petits bouts de cordon retirés au ras d'une branche"] += 1
    for nom, n in remarques.most_common():
        print(f"- {n} : {nom} ;")

    print("\n### Les refus, par type de faute\n")
    fautes = collections.Counter()
    for x in lignes:
        if x["resultat"] == "refuse":
            fautes[" + ".join(sorted(x.get("problemes", {}))) or "?"] += 1
    for nom, n in fautes.most_common():
        print(f"- {n} : {nom}")
    if not fautes:
        print("Aucun.")

    print("\n### Les erreurs, par message\n")
    erreurs = collections.Counter()
    for x in lignes:
        if x["resultat"] == "erreur":
            texte = (x.get("erreur") or "").replace("Erreur : ", "")
            erreurs[texte.split(" : ")[0][:80]] += 1
    for nom, n in erreurs.most_common(12):
        print(f"- {n} : {nom}")
    if not erreurs:
        print("Aucune.")

    print("\n### Contrôle indépendant des G-code vérifiés\n")
    relus = [x for x in lignes if "gcode" in x]
    fautifs = collections.Counter(f for x in relus for f in x["gcode"]["fautes"])
    print(f"{len(relus)} fichiers relus par le banc, sans passer par le slicer : première ligne du contrat, cinq axes "
          "sur chaque déplacement, pointe jamais sous le plateau, inclinaison et rotation dans les limites, "
          "rétraction avant chaque déplacement à vide.")
    if fautifs:
        for nom, n in fautifs.most_common():
            print(f"- {n} fichier(s) : {nom}")
    else:
        print("Aucune faute trouvée.")

    temps = sorted(x["secondes"] for x in lignes)
    print("\n### Temps de calcul\n")
    print(f"Médiane : {temps[len(temps) // 2]:.0f} s par modèle ; 9 sur 10 en moins de {temps[int(len(temps) * 0.9)]:.0f} s ; "
          f"le plus long : {temps[-1]:.0f} s ; total : {sum(temps) / 3600:.1f} heures de calcul.")

    if len(sys.argv) > 2:
        avant = {x["id"]: x for x in lire(sys.argv[2])}
        paires = collections.Counter((avant[x["id"]]["resultat"], x["resultat"]) for x in lignes if x["id"] in avant)
        print(f"\n### Avant / après, sur les {sum(paires.values())} mêmes modèles\n")
        for (a, b), n in paires.most_common():
            print(f"- {n} : {NOMS[a]} → {NOMS[b]}")


if __name__ == "__main__":
    main()
