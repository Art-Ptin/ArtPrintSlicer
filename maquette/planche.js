// ArtPrint Slicer - licence AGPL v3
// Planche de design : affiche les variables de tokens.css telles que le
// navigateur les applique, dans le thème choisi.
import { icone, NOMS_ICONES } from './icones.js';

const $ = (id) => document.getElementById(id);
const el = (balise, classe, texte) => {
  const e = document.createElement(balise);
  if (classe) e.className = classe;
  if (texte !== undefined) e.textContent = texte;
  return e;
};
const variable = (nom) => getComputedStyle(document.documentElement).getPropertyValue(nom).trim();

// --- Contraste (formule WCAG) ---
function luminance(hex) {
  const c = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255)
    .map((v) => (v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4));
  return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}
function contraste(a, b) {
  const [haut, bas] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (haut + 0.05) / (bas + 0.05);
}

// Groupes de couleurs. Troisième élément : la couleur de fond avec laquelle
// le contraste est mesuré (pour les couleurs qui servent de texte).
const COULEURS = [
  ['Neutres', [['--c-fenetre', 'Fenêtre'], ['--c-panneau', 'Panneau'], ['--c-champ', 'Champ'], ['--c-survol', 'Survol'],
    ['--c-bord', 'Bordure'], ['--c-bord-fort', 'Bordure forte', '--c-panneau']]],
  ['Barres du haut', [['--c-barre', 'Barre'], ['--c-barre-2', 'Barre, niveau 2'], ['--c-barre-texte', 'Texte', '--c-barre'],
    ['--c-barre-texte-2', 'Texte secondaire', '--c-barre-2']]],
  ['Texte', [['--c-texte', 'Principal', '--c-panneau'], ['--c-texte-2', 'Secondaire', '--c-panneau'], ['--c-texte-3', 'Désactivé', '--c-panneau']]],
  ['Accent', [['--c-accent', 'Accent'], ['--c-accent-survol', 'Survol'], ['--c-accent-appui', 'Appui'],
    ['--c-sur-accent', 'Texte sur accent', '--c-accent'], ['--c-accent-texte', 'Accent en texte', '--c-panneau'], ['--c-accent-voile', 'Voile']]],
  ['États', [['--c-ok', 'OK', '--c-ok-voile'], ['--c-ok-voile', 'OK, voile'], ['--c-attention', 'Attention', '--c-attention-voile'],
    ['--c-attention-voile', 'Attention, voile'], ['--c-erreur', 'Erreur', '--c-erreur-voile'], ['--c-erreur-voile', 'Erreur, voile']]],
  ['Vue 3D', [['--c-scene', 'Fond'], ['--c-plateau', 'Plateau'], ['--c-grille', 'Grille'], ['--c-plateau-bord', 'Bord du plateau'],
    ['--c-piece', 'Pièce'], ['--c-selection', 'Sélection', '--c-scene'], ['--axe-x', 'Axe X'], ['--axe-y', 'Axe Y'], ['--axe-z', 'Axe Z']]],
  ['Trajectoires de l\'aperçu (sur le plateau)', [['--seg-paroi-externe', 'Paroi externe', '--c-plateau'], ['--seg-paroi-interne', 'Paroi interne', '--c-plateau'],
    ['--seg-remplissage', 'Remplissage', '--c-plateau'], ['--seg-plein', 'Remplissage plein', '--c-plateau'], ['--seg-supports', 'Supports', '--c-plateau'],
    ['--seg-ponts', 'Ponts', '--c-plateau'], ['--seg-jupe', 'Jupe', '--c-plateau'], ['--seg-deplacement', 'Déplacement', '--c-plateau']]],
];

function dessinerCouleurs() {
  const zone = $('couleurs');
  zone.replaceChildren();
  for (const [titre, liste] of COULEURS) {
    zone.append(el('h3', '', titre));
    const nuancier = el('div', 'nuancier');
    for (const [nom, libelle, fond] of liste) {
      const valeur = variable(nom);
      const nuance = el('div', 'nuance');
      const couleur = el('div', 'couleur');
      couleur.style.background = fond ? variable(fond) : valeur;
      if (fond) {
        couleur.style.color = valeur;
        couleur.textContent = `Aa ${contraste(valeur, variable(fond)).toFixed(1).replace('.', ',')} : 1`;
      }
      nuance.append(couleur, el('span', 'nom', libelle), el('span', 'hex', `${nom} ${valeur}`));
      nuancier.append(nuance);
    }
    zone.append(nuancier);
  }
}

function dessinerTypo() {
  const zone = $('typo');
  zone.replaceChildren();
  for (const taille of ['--t-11', '--t-12', '--t-13', '--t-16']) {
    for (const [graisse, nom] of [['--g-normal', 'normale'], ['--g-fort', 'semi-grasse']]) {
      const ligne = el('div');
      const texte = el('span', '', 'Hauteur de couche 0,20 mm · 1 234 567,89 · Trancher le plateau');
      texte.style.fontSize = `var(${taille})`;
      texte.style.fontWeight = `var(${graisse})`;
      ligne.append(el('span', 'mesure', `${variable(taille)} · ${nom}`), texte);
      zone.append(ligne);
    }
  }
}

function dessinerFormes() {
  const zone = $('formes');
  zone.replaceChildren();
  zone.append(el('h3', '', 'Espacements (grille de 4 px)'));
  for (const nom of ['--e-1', '--e-2', '--e-3', '--e-4', '--e-5', '--e-6', '--e-8']) {
    const ligne = el('div', 'ligne');
    const barre = el('span', 'barre');
    barre.style.width = `var(${nom})`;
    ligne.append(el('span', 'mesure', `${nom} = ${variable(nom)}`), barre);
    zone.append(ligne);
  }
  zone.append(el('h3', '', 'Hauteurs'));
  for (const [nom, libelle] of [['--h-champ', 'Champs et boutons'], ['--h-ligne', 'Ligne de paramètre'], ['--h-barre-onglets', 'Barre d\'onglets'], ['--h-etat', 'Ligne d\'état']]) {
    const ligne = el('div', 'ligne');
    const boite = el('span', 'boite', libelle);
    boite.style.height = `var(${nom})`;
    boite.style.width = 'auto';
    boite.style.padding = '0 var(--e-3)';
    ligne.append(el('span', 'mesure', `${nom} = ${variable(nom)}`), boite);
    zone.append(ligne);
  }
  zone.append(el('h3', '', 'Rayons et ombres'));
  const ligne = el('div', 'ligne');
  for (const [style, libelle] of [['border-radius: var(--r-1)', 'rayon ' + variable('--r-1')], ['border-radius: var(--r-2)', 'rayon ' + variable('--r-2')],
    ['border-radius: var(--r-2); box-shadow: var(--o-1)', 'ombre 1'], ['border-radius: var(--r-2); box-shadow: var(--o-2)', 'ombre 2']]) {
    const boite = el('span', 'boite', libelle);
    boite.style.cssText = style;
    ligne.append(boite);
  }
  zone.append(ligne);
}

function dessinerIcones() {
  $('icones').replaceChildren(...NOMS_ICONES.map((nom) => {
    const boite = el('div', 'case-icone');
    const tailles = el('div', 'tailles');
    tailles.append(icone(nom, 16), icone(nom, 20), icone(nom, 24));
    boite.append(tailles, el('span', 'nom', nom));
    return boite;
  }));
}

// Le logo, tel qu'il est dessiné dans la barre du slicer. Les masques font
// passer l'anneau derrière le A en haut et devant lui en bas.
function logo(surClair) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '10 14 262 210');
  svg.setAttribute('class', 'logo' + (surClair ? ' sur-clair' : ''));
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', 'Symbole ArtPrint');
  const id = surClair ? 'clair' : 'sombre' + (logo.compte = (logo.compte || 0) + 1);
  svg.innerHTML = `<defs>
        <mask id="logo-a-${id}" maskUnits="userSpaceOnUse" x="10" y="14" width="262" height="210">
          <rect x="10" y="14" width="262" height="210" fill="#fff"/>
          <clipPath id="logo-devant-${id}"><rect x="10" y="156" width="262" height="68"/></clipPath>
          <ellipse cx="141" cy="156" rx="119.250" ry="30.250" fill="none" stroke="#000" stroke-width="31.500" clip-path="url(#logo-devant-${id})"/>
        </mask>
        <mask id="logo-anneau-${id}" maskUnits="userSpaceOnUse" x="10" y="14" width="262" height="210">
          <rect x="10" y="14" width="262" height="210" fill="#fff"/>
          <clipPath id="logo-derriere-${id}"><rect x="10" y="14" width="262" height="142"/></clipPath>
          <path d="M100 22 125 18 246 220H195L124.600 85.500 105.700 220H61.600z" fill="#000" stroke="#000" stroke-width="16" clip-path="url(#logo-derriere-${id})"/>
        </mask>
      </defs>
      <path class="logo-a" d="M100 22 125 18 246 220H195L124.600 85.500 105.700 220H61.600z" mask="url(#logo-a-${id})"/>
      <ellipse class="logo-anneau" cx="141" cy="156" rx="119.250" ry="30.250" stroke-width="15.500" mask="url(#logo-anneau-${id})"/>`;
  return svg;
}

function toutDessiner() {
  dessinerCouleurs();
  dessinerTypo();
  dessinerFormes();
}

for (const [id, theme] of [['theme-systeme', null], ['theme-clair', 'light'], ['theme-sombre', 'dark']]) {
  $(id).addEventListener('click', () => {
    if (theme) document.documentElement.dataset.theme = theme; else delete document.documentElement.dataset.theme;
    for (const autre of ['theme-systeme', 'theme-clair', 'theme-sombre']) $(autre).classList.toggle('actif', autre === id);
    toutDessiner();
  });
}
window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', toutDessiner);

$('logo-barre').append(logo(false), el('span', 'nom', 'ArtPrint'));
$('logo-sombre').append(logo(false), el('span', 'nom', 'ArtPrint'));
$('logo-clair').append(logo(true), el('span', 'nom', 'ArtPrint'));
dessinerIcones();
document.fonts.ready.then(toutDessiner);
toutDessiner();
