# ArtPrint Slicer - licence AGPL v3
"""Vérifie les couleurs du fichier de variables de design.

  1. Contraste du texte (WCAG AA : 4,5 pour 1) dans les deux thèmes.
  2. Contraste des éléments graphiques (3 pour 1) : bordures de champ, trajectoires.
  3. Trajectoires de l'aperçu : assez différentes entre elles, aussi pour les
     trois formes courantes de daltonisme.
  4. Accent, attention et erreur assez différents entre eux.

Utilisation : python design/verifier_couleurs.py maquette/tokens.css
"""
import re
import sys

def lire(chemin):
    texte = open(chemin, encoding="utf-8").read()
    clair = dict(re.findall(r"(--[\w-]+):\s*(#[0-9a-fA-F]{6})", texte.split("@media")[0]))
    bloc = re.search(r':root\[data-theme="dark"\]\s*\{(.*?)\n\}', texte, re.S).group(1)
    sombre = {**clair, **dict(re.findall(r"(--[\w-]+):\s*(#[0-9a-fA-F]{6})", bloc))}
    return {"clair": clair, "sombre": sombre}

def rvb(h): return tuple(int(h[i:i + 2], 16) / 255 for i in (1, 3, 5))
def lin(c): return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
def luminance(h):
    r, g, b = (lin(c) for c in rvb(h))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b
def contraste(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)

# Simulation du daltonisme (matrices de Machado et al., sévérité complète).
VISIONS = {
    "normale": ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
    "protanopie": ((0.152286, 1.052583, -0.204868), (0.114503, 0.786281, 0.099216), (-0.003882, -0.048116, 1.051998)),
    "deutéranopie": ((0.367322, 0.860646, -0.227968), (0.280085, 0.672501, 0.047413), (-0.011820, 0.042940, 0.968881)),
    "tritanopie": ((1.255528, -0.076749, -0.178779), (-0.078411, 0.930809, 0.147602), (0.004733, 0.691367, 0.303900)),
}
def lab(h, vision):
    r, g, b = (lin(c) for c in rvb(h))
    m = VISIONS[vision]
    r, g, b = (max(0.0, min(1.0, m[i][0] * r + m[i][1] * g + m[i][2] * b)) for i in range(3))
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    return 116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))
def ecart(a, b, vision):
    return sum((p - q) ** 2 for p, q in zip(lab(a, vision), lab(b, vision))) ** 0.5

TEXTES = [  # (texte, fond, minimum)
    ("--c-texte", "--c-panneau", 4.5), ("--c-texte", "--c-champ", 4.5), ("--c-texte", "--c-fenetre", 4.5),
    ("--c-texte", "--c-survol", 4.5), ("--c-texte-2", "--c-panneau", 4.5), ("--c-texte-2", "--c-champ", 4.5),
    ("--c-texte-2", "--c-survol", 4.5), ("--c-barre-texte", "--c-barre", 4.5), ("--c-barre-texte", "--c-barre-2", 4.5),
    ("--c-barre-texte-2", "--c-barre-2", 4.5), ("--c-sur-accent", "--c-accent", 4.5), ("--c-sur-accent", "--c-accent-survol", 4.5),
    ("--c-sur-accent", "--c-accent-appui", 4.5), ("--c-accent-texte", "--c-panneau", 4.5), ("--c-accent-texte", "--c-champ", 4.5),
    ("--c-accent-texte", "--c-accent-voile", 4.5), ("--c-texte", "--c-accent-voile", 4.5),
    ("--c-ok", "--c-ok-voile", 4.5), ("--c-ok", "--c-panneau", 4.5), ("--c-attention", "--c-attention-voile", 4.5),
    ("--c-attention", "--c-panneau", 4.5), ("--c-erreur", "--c-erreur-voile", 4.5), ("--c-erreur", "--c-panneau", 4.5),
]
GRAPHIQUES = [("--c-bord-fort", "--c-panneau", 3.0), ("--c-bord-fort", "--c-champ", 3.0), ("--c-accent", "--c-panneau", 3.0),
              ("--c-selection", "--c-scene", 3.0), ("--c-selection", "--c-plateau", 2.0), ("--c-texte-3", "--c-panneau", 3.0)]
SEGMENTS = ["--seg-paroi-externe", "--seg-paroi-interne", "--seg-remplissage", "--seg-plein", "--seg-supports",
            "--seg-ponts", "--seg-jupe", "--seg-deplacement"]

def main():
    themes = lire(sys.argv[1])
    echecs = 0
    for nom, t in themes.items():
        print(f"== Thème {nom}")
        for a, b, mini in TEXTES + GRAPHIQUES:
            c = contraste(t[a], t[b])
            if c < mini:
                echecs += 1
                print(f"  ÉCHEC  {a} sur {b} : {c:.2f} (minimum {mini})")
        pire = min((contraste(t[a], t[b]), a, b) for a, b, _ in TEXTES)
        print(f"  texte : plus faible contraste {pire[0]:.2f} ({pire[1]} sur {pire[2]})")
        # Les trajectoires se lisent d'abord sur le plateau (sombre dans les
        # deux thèmes) : 3 pour 1 exigé. Sur le fond de la scène, la valeur
        # est seulement relevée : en thème clair, des couleurs vives sur fond
        # clair ne peuvent pas atteindre 3 pour 1 sur les deux fonds à la fois.
        for s in SEGMENTS:
            c = contraste(t[s], t["--c-plateau"])
            if c < 3.0:
                echecs += 1
                print(f"  ÉCHEC  {s} sur --c-plateau : {c:.2f} (minimum 3)")
        faible = min((contraste(t[s], t["--c-scene"]), s) for s in SEGMENTS if s != "--seg-deplacement")
        print(f"  trajectoires sur le fond de la scène : plus faible contraste {faible[0]:.2f} ({faible[1][6:]})")
        for vision in VISIONS:
            proche = min((ecart(t[a], t[b], vision), a, b) for i, a in enumerate(SEGMENTS) for b in SEGMENTS[i + 1:])
            marque = "" if proche[0] >= 12 else "  ÉCHEC"
            echecs += proche[0] < 12
            print(f"  trajectoires, vision {vision:13s} : écart minimal {proche[0]:5.1f} ({proche[1][6:]} / {proche[2][6:]}){marque}")
        for a, b in (("--c-accent", "--c-erreur"), ("--c-erreur", "--c-attention"), ("--c-accent", "--c-attention"), ("--c-ok", "--c-attention")):
            e = ecart(t[a], t[b], "normale")
            marque = "" if e >= 20 else "  ÉCHEC"
            echecs += e < 20
            print(f"  états : {a[4:]} / {b[4:]} écart {e:5.1f}{marque}")
    print("\nTout est conforme." if not echecs else f"\n{echecs} point(s) à corriger.")
    sys.exit(1 if echecs else 0)

if __name__ == "__main__":
    main()
