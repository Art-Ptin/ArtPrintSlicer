// ArtPrint Slicer - licence AGPL v3
// Maquette de l'interface : sections Imprimante, Filament et Procédé, barre
// d'outils, aperçu avec trajectoires repliables, ligne d'état.
import { appeler, simulation } from './pont.js';
import { Vue3D, couleursTrajectoires } from './vue3d.js';
import { composerRotation, rotationPourPoser, COULEURS_AXES } from './manipulateurs.js';
import { icone } from './icones.js';
import { ONGLETS, PARAMETRES, PROFILS, MATIERES, BOBINES_EXEMPLE, PROFIL_MACHINE } from './catalogue.js';
import { ouvrirMenu } from './menu.js';
import { brancherPages } from './pages.js';

const $ = (id) => document.getElementById(id);
const el = (balise, classe, texte) => {
  const e = document.createElement(balise);
  if (classe) e.className = classe;
  if (texte !== undefined) e.textContent = texte;
  return e;
};

// Mémoire du navigateur : pratique, mais jamais indispensable.
const memoire = {
  lire(cle, defaut) {
    try { const v = localStorage.getItem('artprint.' + cle); return v === null ? defaut : JSON.parse(v); } catch { return defaut; }
  },
  ecrire(cle, valeur) {
    try { localStorage.setItem('artprint.' + cle, JSON.stringify(valeur)); } catch { /* sans mémoire, tant pis */ }
  },
};

const vue3d = new Vue3D($('canvas3d'));
window.artprint = { vue3d };
let pages = null;        // pages Imprimante et Gestionnaire, branchées au démarrage

const etat = {
  valeurs: {},          // valeur de chaque paramètre du procédé
  profil: 'standard',
  onglet: ONGLETS[0],
  avance: false,
  recherche: '',
  refus: {},            // message d'erreur par paramètre
  saisies: {},          // texte tapé pour les paramètres refusés
  moteur: [],           // réglages du cœur du slicer (machine, vérification...)
  bobines: memoire.lire('bobines', BOBINES_EXEMPLE),
  montee: memoire.lire('montee', BOBINES_EXEMPLE[0].id),
  modele: null,
  resultat: null,
  page: 'preparer',     // page affichée (onglet de la barre du haut)
  strategies: [],       // façons d'imprimer proposées pour le modèle ouvert
  strategie: 'classique',
  branches: {},         // ondulation : choix de l'utilisateur par branche (numéro de partie -> choix)
  coupes: [],           // coupes faites sur le modèle ouvert ({ hauteur, garder, pose })
  zones: [],            // ondulation : zones orientées choisies sur le modèle ({ point, inclinaison, azimut })
  zoneChoisie: -1,
  pointage: false,      // vrai : le prochain clic sur le modèle ajoute une zone
  apercuZones: null,    // ce que le slicer dit des zones ({ texte, alerte })
  chemin: null,         // fichier du modèle ouvert
  echelle: 1,           // facteur d'échelle du modèle (1 : taille d'origine)
  orientation: 'origine',
  rotation: [0, 0, 0],  // rotation du modèle autour de X, Y, Z, en degrés
  decalage: [0, 0],     // déplacement du modèle sur le plateau, en mm
  lecture: 0,           // minuterie de la simulation d'impression (0 : arrêtée)
  plateaux: [{}],       // un modèle par plateau : { chemin, echelle, orientation }
  plateau: 0,
  remarques: [],        // remarques du dernier tranchage
};

// --- Nombres -------------------------------------------------------------------

const fr = (valeur, decimales = 1) => Number(valeur).toLocaleString('fr-FR',
  { minimumFractionDigits: decimales, maximumFractionDigits: decimales });
const entier = (valeur) => Number(valeur).toLocaleString('fr-FR');
const ecrire = (valeur) => String(Number(Number(valeur).toFixed(6))).replace('.', ',');
function lireNombre(texte) {
  const propre = String(texte).trim();
  if (!/^[+-]?(\d+([.,]\d*)?|[.,]\d+)$/.test(propre)) return NaN;
  return Number(propre.replace(',', '.'));
}
function duree(secondes) {
  const minutes = Math.max(1, Math.round(secondes / 60));
  return minutes >= 60 ? `${Math.floor(minutes / 60)} h ${String(minutes % 60).padStart(2, '0')} min` : `${minutes} min`;
}
const sansAccent = (t) => t.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();

// --- Messages ------------------------------------------------------------------

let minuterie = 0;
function dire(texte, { info = false, actions = [], tenir = false } = {}) {
  $('message-texte').textContent = texte;
  $('message').classList.toggle('info', info);
  $('message-actions').replaceChildren(...actions.map(({ libelle, action }) => {
    const bouton = el('button', 'lien', libelle);
    bouton.addEventListener('click', () => { $('message').hidden = true; action?.(); });
    return bouton;
  }));
  $('message').hidden = false;
  clearTimeout(minuterie);
  if (!tenir) minuterie = setTimeout(() => { $('message').hidden = true; }, info ? 4500 : 9000);
}
function occuper(texte) { $('attente-texte').textContent = texte; $('attente').hidden = false; }
function liberer() { $('attente').hidden = true; }
async function demander(commande, parametres) {
  const reponse = await appeler(commande, parametres);
  if (!reponse.ok) { dire(reponse.erreur || 'Erreur inconnue.'); return null; }
  return reponse;
}

// --- Imprimante ----------------------------------------------------------------

const moteur = (cle) => etat.moteur.find((r) => r.cle === cle)?.valeur ?? '';

function afficherImprimante() {
  const liste = $('profil-imprimante');
  if (!liste.options.length) liste.add(new Option('ArtPrint 5 axes', 'artprint'));
  const rayon = lireNombre(moteur('rayon_plateau')) || 150;
  const hauteur = lireNombre(moteur('hauteur_impression')) || 260;
  if (rayon !== vue3d.rayon || hauteur !== vue3d.hauteur) vue3d.definirVolume(rayon, hauteur);
}

// Vignettes de la section Imprimante. L'image de la machine est un fichier
// privé : s'il n'est pas là, une icône la remplace.
function afficherVignettes() {
  const image = new Image();
  image.alt = '';
  image.addEventListener('error', () => $('vignette-imprimante').replaceChildren(icone('imprimante', 24)));
  image.src = 'prive/imprimante.png';
  $('vignette-imprimante').replaceChildren(image);
  $('vignette-synchro').replaceChildren(icone('synchroniser', 24));
  dessinerPlateau();
}

// Petit dessin du plateau rond, différent selon le type choisi.
function dessinerPlateau() {
  const type = { 'Texturé': 'texture', Verre: 'verre' }[$('type-plateau').value] || 'lisse';
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 48 48');
  svg.setAttribute('aria-hidden', 'true');
  svg.setAttribute('class', 'dessin-plateau ' + type);
  let detail = '';
  if (type === 'texture') {
    for (let ligne = 0; ligne < 5; ligne++) {
      for (let x = 6 + (ligne % 2) * 1.5; x <= 42; x += 3) {
        const y = 17 + ligne * 3;
        if (((x - 24) / 17.5) ** 2 + ((y - 23) / 6.5) ** 2 < 1) detail += `<circle class="grain" cx="${x}" cy="${y}" r="0.600"/>`;
      }
    }
  } else if (type === 'verre') {
    detail = '<path class="reflet" d="M11 22c3-3 8-4.500 13-4.500M31 28c3-.500 5-1.500 6.500-3"/>';
  }
  svg.innerHTML = '<path class="tranche" d="M4 23v3.500a20 8.500 0 0 0 40 0V23z"/>' +
    `<ellipse class="dessus" cx="24" cy="23" rx="20" ry="8.500"/>${detail}`;
  $('vignette-plateau').replaceChildren(svg);
}

// Choix gardés en mémoire mais pas encore appliqués par le slicer.
function brancherChoixPrevus() {
  for (const [id, nom] of [['type-plateau', 'Le type de plateau'], ['buse-diametre', 'Le diamètre de la buse'], ['buse-debit', 'Le débit de la buse']]) {
    const liste = $(id);
    const garde = memoire.lire(id, null);
    if (garde !== null && [...liste.options].some((o) => o.value === garde)) liste.value = garde;
    liste.addEventListener('change', () => {
      memoire.ecrire(id, liste.value);
      if (id === 'type-plateau') dessinerPlateau();
      dire(`${nom} est noté, mais il ne change pas encore le tranchage (prévu).`, { info: true });
    });
  }
}

// Sous-groupes de la feuille « Réglages de la machine ».
const FAMILLES_MACHINE = [
  ['Volume d\'impression', ['rayon_plateau', 'hauteur_impression', 'centre_plateau_x', 'centre_plateau_y']],
  ['Tête inclinable (axe B)', ['distance_pivot_buse', 'axe_pivot_b', 'sens_b', 'b_vertical', 'b_min', 'b_max', 'b_inclinaison_max', 'tete_demi_largeur', 'tete_hauteur', 'vitesse_b_max', 'b_limite_pres_plateau', 'hauteur_bascule_b', 'pivot_continu']],
  ['Plateau rotatif (axe C)', ['sens_c', 'c_min', 'c_max', 'vitesse_c_max']],
  ['Fichier G-code', ['contrat_gcode']],
  ['Courses des axes', ['course_x_min', 'course_x_max', 'course_y_min', 'course_y_max', 'course_z_min', 'course_z_max']],
  ['Collisions', ['modele_machine', 'marge_securite', 'zone_buse', 'marge_buse']],
  ['Vérificateur', ['pas_controle', 'tolerance_croisement', 'hauteur_controle_dessus']],
];

function ouvrirMachine() {
  const refus = {};
  const corps = el('div', 'colonne');
  const dessiner = () => {
    corps.replaceChildren();
    for (const [titre, cles] of FAMILLES_MACHINE) {
      corps.append(el('div', 'section-titre etiquette', titre));
      for (const cle of cles) {
        const r = etat.moteur.find((x) => x.cle === cle);
        if (!r) continue;
        const large = r.type === 'choix' || r.type === 'texte';
        const rang = el('div', 'rang' + (large ? ' large' : '') + (refus[cle] ? ' invalide' : ''));
        rang.title = r.aide;
        const libelle = el('label', '', r.libelle);
        libelle.htmlFor = 'machine-' + cle;
        let champ;
        if (r.type === 'choix') {
          champ = el('select', 'champ');
          for (const choix of r.choix) champ.add(new Option(choix, choix, false, choix === r.valeur));
        } else {
          champ = el('input', 'champ');
          champ.type = 'text';
          champ.value = r.valeur;
          if (r.type === 'texte') champ.classList.add('texte');
          if (r.facultatif) champ.placeholder = r.type === 'texte' ? 'aucun' : 'à mesurer';
        }
        champ.id = 'machine-' + cle;
        champ.addEventListener('change', async () => {
          const reponse = await demander('definir_reglages', { valeurs: { [cle]: champ.value } });
          if (!reponse) return;
          if (reponse.refus[cle]) refus[cle] = reponse.refus[cle]; else delete refus[cle];
          etat.moteur = reponse.reglages;
          afficherImprimante();
          perimer();
          dessiner();
        });
        rang.append(el('span', 'marqueur'), libelle, champ);
        if (!large) rang.append(el('span', 'unite', r.unite));
        rang.append(el('span'));
        if (refus[cle]) rang.append(el('div', 'refus', refus[cle]));
        corps.append(rang);
      }
    }
  };
  dessiner();
  ouvrirFeuille('Réglages de la machine', corps, { simple: true,
    pied: 'Ces valeurs décrivent ton imprimante. Les champs vides sont à mesurer sur la machine.' });
}

// --- Feuilles (panneaux qui glissent par-dessus la page) ------------------------

function ouvrirFeuille(titre, contenu, { simple = false, pied = '' } = {}) {
  const voile = el('div', 'voile');
  const feuille = el('div', 'feuille');
  feuille.setAttribute('role', 'dialog');
  feuille.setAttribute('aria-label', titre);
  const tete = el('div', 'bloc-tete');
  const fermer = el('button', 'bouton fin', 'Fermer');
  fermer.id = 'feuille-fermer';
  tete.append(el('h2', '', titre), fermer);
  const corps = el('div', 'corps' + (simple ? ' simple' : ''));
  corps.append(...(Array.isArray(contenu) ? contenu : [contenu]));
  feuille.append(tete, corps);
  if (pied) feuille.append(el('div', 'pied', pied));
  voile.append(feuille);
  const clore = () => { voile.remove(); document.removeEventListener('keydown', touche); };
  const touche = (e) => { if (e.key === 'Escape') clore(); };
  fermer.addEventListener('click', clore);
  voile.addEventListener('click', (e) => { if (e.target === voile) clore(); });
  document.addEventListener('keydown', touche);
  $('feuilles').append(voile);
  fermer.focus();
  return clore;
}

// --- Filament ------------------------------------------------------------------

const bobineMontee = () => etat.bobines.find((b) => b.id === etat.montee) || etat.bobines[0] || null;

function enregistrerBobines() {
  memoire.ecrire('bobines', etat.bobines);
  memoire.ecrire('montee', etat.montee);
}

// Vrai si une couleur « #rrggbb » est claire : le chiffre posé dessus doit être sombre.
function estClaire(hex) {
  const [rouge, vert, bleu] = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
  return 0.2126 * rouge + 0.7152 * vert + 0.0722 * bleu > 140;
}

function afficherBobine() {
  pages?.rafraichir();
  $('bobines-grille').replaceChildren(...etat.bobines.map((b, rang) => {
    const montee = b.id === etat.montee;
    const emplacement = el('div', 'emplacement' + (montee ? ' montee' : ''));
    const numero = el('span', 'numero', String(rang + 1));
    numero.style.background = b.couleur;
    numero.style.color = estClaire(b.couleur) ? 'var(--c-barre)' : 'var(--c-barre-texte)';
    const choix = el('button', 'choix');
    choix.id = 'monter-' + b.id;
    choix.setAttribute('role', 'radio');
    choix.setAttribute('aria-checked', String(montee));
    choix.title = montee ? `« ${b.nom} » est la bobine montée sur l'imprimante.` : `Monter « ${b.nom} » sur l'imprimante`;
    choix.append(el('span', 'point'), el('span', 'nom', b.nom));
    choix.addEventListener('click', () => { if (b.id !== etat.montee) monterBobine(b.id); });
    const modifier = el('button', 'bouton-icone petit');
    modifier.id = 'modifier-' + b.id;
    modifier.title = `Modifier « ${b.nom} »`;
    modifier.setAttribute('aria-label', modifier.title);
    modifier.append(icone('modifier'));
    modifier.addEventListener('click', () => ouvrirBibliotheque(b.id));
    emplacement.append(numero, choix, modifier);
    return emplacement;
  }));
  const zone = $('bobine-montee');
  const b = bobineMontee();
  if (!b) {
    zone.replaceChildren(el('span', 'texte', 'Aucune bobine. Ajoute-en une avec le bouton « + ».'));
    return;
  }
  const reste = Math.max(0, Math.min(1, b.poids_restant / b.poids_bobine));
  const jauge = el('div', 'jauge' + (reste < 0.15 ? ' bas' : ''));
  const niveau = el('span');
  niveau.style.width = `${reste * 100}%`;
  jauge.append(niveau);
  jauge.title = `Reste ${entier(b.poids_restant)} g sur ${entier(b.poids_bobine)} g`;
  const texte = el('span', 'texte');
  texte.append(el('b', '', b.nom),
    document.createTextNode(` · reste ${entier(b.poids_restant)} g · buse ${b.temp_buse} °C · plateau ${b.temp_plateau} °C`));
  texte.title = `Bobine montée : ${texte.textContent}`;
  zone.replaceChildren(texte, jauge);
  document.documentElement.style.setProperty('--c-piece', b.couleur);
  vue3d.rafraichirTheme();
}

function nouvelleBobine() {
  const id = 'b' + Date.now().toString(36);
  etat.bobines.push({ id, nom: 'Nouvelle bobine', marque: '', matiere: 'PLA', couleur: '#9aa4b2', diametre: 1.75,
    densite: MATIERES.PLA.densite, temp_buse: MATIERES.PLA.temp_buse, temp_plateau: MATIERES.PLA.temp_plateau,
    debit: 1, retraction: MATIERES.PLA.retraction, retraction_vitesse: 40, ventilation: true,
    prix_kg: 20, poids_bobine: 1000, poids_restant: 1000 });
  enregistrerBobines();
  afficherBobine();
  return id;
}

// La bobine montée fixe les réglages de matière du slicer.
async function monterBobine(id, silencieux = false) {
  etat.montee = id;
  enregistrerBobines();
  afficherBobine();
  const b = bobineMontee();
  if (!b) return;
  const reponse = await demander('definir_reglages', { valeurs: {
    temperature_buse: ecrire(b.temp_buse), temperature_plateau: ecrire(b.temp_plateau),
    diametre_filament: ecrire(b.diametre), debit: ecrire(b.debit), ventilation: b.ventilation ? '1' : '0',
    retraction_longueur: ecrire(b.retraction), retraction_vitesse: ecrire(b.retraction_vitesse) } });
  if (reponse) etat.moteur = reponse.reglages;
  perimer();
  if (!silencieux) dire(`« ${b.nom} » est maintenant le filament monté.`, { info: true });
}

const CHAMPS_BOBINE = [
  ['nom', 'Nom', 'texte'], ['marque', 'Marque', 'texte'], ['matiere', 'Matière', 'matiere'], ['couleur', 'Couleur', 'couleur'],
  [null],
  ['diametre', 'Diamètre', 'nombre', 'mm', 0.5, 5], ['densite', 'Densité', 'nombre', 'g/cm³', 0.5, 5],
  ['temp_buse', 'Température de la buse', 'nombre', '°C', 150, 500], ['temp_plateau', 'Température du plateau', 'nombre', '°C', 0, 200],
  ['debit', 'Débit', 'nombre', '', 0.5, 2], ['retraction', 'Longueur de rétraction', 'nombre', 'mm', 0.05, 30],
  ['retraction_vitesse', 'Vitesse de rétraction', 'nombre', 'mm/s', 1, 300], ['ventilation', 'Ventilateur de pièce', 'bool'],
  [null],
  ['prix_kg', 'Prix', 'nombre', '€/kg', 0, 10000], ['poids_bobine', 'Poids d\'une bobine pleine', 'nombre', 'g', 1, 100000],
  ['poids_restant', 'Poids restant', 'nombre', 'g', 0, 100000],
];

function ouvrirBibliotheque(depart = etat.montee) {
  let choisie = depart;
  let brouillon = null;       // bobine en cours de modification
  let aSupprimer = false;
  const gauche = el('div', 'colonne');
  const droite = el('div', 'colonne');

  const dessiner = () => {
    // Liste des bobines.
    const table = el('table', 'bobines');
    const tete = el('thead');
    const ligneTete = el('tr');
    for (const t of ['Bobine', 'Matière', 'Buse', 'Reste']) ligneTete.append(el('th', 'etiquette', t));
    tete.append(ligneTete);
    const corps = el('tbody');
    for (const b of etat.bobines) {
      const ligne = el('tr', b.id === choisie ? 'choisie' : '');
      ligne.id = 'bobine-' + b.id;
      const nom = el('td');
      const carre = el('span', 'carre');
      carre.style.background = b.couleur;
      nom.append(carre, document.createTextNode(b.nom));
      if (b.id === etat.montee) nom.append(el('span', 'montee', 'MONTÉE'));
      ligne.append(nom, el('td', '', b.matiere), el('td', 'nombre', `${b.temp_buse} °C`),
        el('td', 'nombre', `${entier(b.poids_restant)} g`));
      ligne.addEventListener('click', () => { choisie = b.id; brouillon = null; aSupprimer = false; dessiner(); });
      corps.append(ligne);
    }
    table.append(tete, corps);
    const ajouter = el('div', 'formulaire');
    const nouveau = el('button', 'bouton', '+ Nouvelle bobine');
    nouveau.id = 'bobine-nouvelle';
    nouveau.addEventListener('click', () => { choisie = nouvelleBobine(); brouillon = null; dessiner(); });
    ajouter.append(nouveau);
    gauche.replaceChildren(table, ajouter);

    // Fiche de la bobine choisie.
    const b = etat.bobines.find((x) => x.id === choisie);
    if (!b) { droite.replaceChildren(el('div', 'vide', 'Choisis une bobine dans la liste.')); return; }
    brouillon = brouillon || { ...b };
    const formulaire = el('div', 'formulaire');
    const erreurs = el('div', 'refus');
    for (const [cle, libelle, type, unite] of CHAMPS_BOBINE) {
      if (!cle) { formulaire.append(el('hr')); continue; }
      const paire = el('div', 'paire' + (type === 'nombre' ? ' unite' : ''));
      const nom = el('label', '', libelle);
      nom.htmlFor = 'fiche-' + cle;
      let champ;
      if (type === 'matiere') {
        champ = el('select', 'champ');
        for (const m of Object.keys(MATIERES)) champ.add(new Option(m, m, false, m === brouillon.matiere));
        champ.addEventListener('change', () => {
          // Changer de matière propose ses valeurs habituelles.
          const m = MATIERES[champ.value];
          Object.assign(brouillon, { matiere: champ.value, temp_buse: m.temp_buse, temp_plateau: m.temp_plateau,
            densite: m.densite, ventilation: m.ventilation, retraction: m.retraction });
          dessiner();
        });
      } else if (type === 'bool') {
        champ = el('input');
        champ.type = 'checkbox';
        champ.checked = brouillon[cle];
        champ.className = 'case';
        champ.addEventListener('change', () => { brouillon[cle] = champ.checked; });
      } else {
        champ = el('input', 'champ' + (type === 'texte' ? ' texte' : ''));
        champ.type = type === 'couleur' ? 'color' : 'text';
        champ.value = type === 'nombre' ? ecrire(brouillon[cle]) : brouillon[cle];
        champ.addEventListener('input', () => { brouillon[cle] = champ.value; });
      }
      champ.id = 'fiche-' + cle;
      paire.append(nom, champ);
      if (type === 'nombre') paire.append(el('span', 'unite', unite));
      formulaire.append(paire);
    }
    const boutons = el('div', 'boutons');
    const enregistrer = el('button', 'bouton plein', 'Enregistrer');
    enregistrer.id = 'fiche-enregistrer';
    enregistrer.addEventListener('click', () => {
      const propre = { ...brouillon };
      for (const [cle, libelle, type, , min, max] of CHAMPS_BOBINE) {
        if (type !== 'nombre') continue;
        const n = typeof propre[cle] === 'number' ? propre[cle] : lireNombre(propre[cle]);
        if (!Number.isFinite(n) || n < min || n > max) {
          erreurs.textContent = `« ${libelle} » doit être un nombre entre ${ecrire(min)} et ${ecrire(max)}.`;
          return;
        }
        propre[cle] = n;
      }
      if (!String(propre.nom).trim()) { erreurs.textContent = 'Donne un nom à la bobine.'; return; }
      Object.assign(b, propre);
      brouillon = null;
      enregistrerBobines();
      if (b.id === etat.montee) monterBobine(b.id, true); else afficherBobine();
      dessiner();
      dire(`Bobine « ${b.nom} » enregistrée.`, { info: true });
    });
    const monter = el('button', 'bouton', b.id === etat.montee ? 'Déjà montée' : 'Monter sur l\'imprimante');
    monter.id = 'fiche-monter';
    monter.disabled = b.id === etat.montee;
    monter.addEventListener('click', async () => { await monterBobine(b.id); dessiner(); });
    const supprimer = el('button', 'bouton', aSupprimer ? 'Confirmer la suppression' : 'Supprimer');
    supprimer.id = 'fiche-supprimer';
    supprimer.addEventListener('click', () => {
      if (!aSupprimer) { aSupprimer = true; dessiner(); return; }
      etat.bobines = etat.bobines.filter((x) => x.id !== b.id);
      if (etat.montee === b.id) etat.montee = etat.bobines[0]?.id ?? null;
      choisie = etat.montee; brouillon = null; aSupprimer = false;
      enregistrerBobines(); afficherBobine(); dessiner();
    });
    boutons.append(enregistrer, monter, supprimer);
    formulaire.append(el('hr'), erreurs, boutons);
    droite.replaceChildren(formulaire);
  };
  dessiner();
  ouvrirFeuille('Mes bobines de filament', [gauche, droite],
    { pied: 'La bobine « montée » fixe les températures, le débit et la rétraction du tranchage. Les bobines sont gardées sur cet ordinateur.' });
}

// --- Procédé -------------------------------------------------------------------

// Icône de chaque groupe de paramètres (et de chaque onglet, pour la recherche).
const ICONES_GROUPE = {
  'Hauteur de couche': 'g-hauteur', 'Largeur de ligne': 'g-largeur', 'Couture': 'g-couture', 'Précision': 'g-precision',
  'Lissage du dessus': 'g-lissage', 'Parois': 'g-parois', 'Coques du dessus et du dessous': 'g-coques',
  'Remplissage': 'g-remplissage', 'Ponts': 'g-ponts', 'Première couche': 'g-hauteur', 'Autres couches': 'g-vitesse',
  'Surplombs et ponts': 'g-ponts', 'Déplacements': 'g-deplacement', 'Accélérations': 'g-acceleration',
  'Supports': 'g-supports', 'Adhérence au plateau': 'g-adherence', 'Déplacements à vide': 'g-deplacement',
  '5 axes': 'g-cinq-axes', 'Modes spéciaux': 'g-speciaux',
  'Qualité': 'g-precision', 'Solidité': 'g-remplissage', 'Vitesse': 'g-vitesse', 'Autres': 'g-speciaux',
};

const profilCourant = () => PROFILS.find((p) => p.id === etat.profil) || PROFILS[0];
const valeurProfil = (p) => (p.cle in profilCourant().valeurs ? profilCourant().valeurs[p.cle] : p.defaut);
const estModifie = (p) => etat.valeurs[p.cle] !== valeurProfil(p);
const texteValeur = (p, v) => (p.type === 'nombre' || p.type === 'entier' ? ecrire(v) : v);

// Envoie au slicer les paramètres qu'il sait appliquer.
async function synchroniser(parametres) {
  const valeurs = {};
  for (const p of parametres) {
    if (p.moteur) valeurs[p.moteur] = ecrire(etat.valeurs[p.cle] / (p.echelle || 1));
  }
  if (!Object.keys(valeurs).length) return {};
  const reponse = await demander('definir_reglages', { valeurs });
  if (!reponse) return {};
  etat.moteur = reponse.reglages;
  return reponse.refus;
}

async function changer(p, brut) {
  let valeur = brut;
  delete etat.refus[p.cle];
  delete etat.saisies[p.cle];
  if (p.type === 'nombre' || p.type === 'entier') {
    valeur = lireNombre(brut);
    let erreur = '';
    if (!Number.isFinite(valeur)) erreur = `« ${brut} » n'est pas un nombre.`;
    else if (valeur < p.min || valeur > p.max) erreur = `Valeur attendue entre ${ecrire(p.min)} et ${ecrire(p.max)}${p.unite ? ' ' + p.unite : ''}.`;
    else if (p.type === 'entier' && !Number.isInteger(valeur)) erreur = 'Un nombre entier est attendu.';
    if (erreur) {
      etat.refus[p.cle] = erreur;
      etat.saisies[p.cle] = brut;
      afficherProcede();
      return;
    }
  }
  const ancienne = etat.valeurs[p.cle];
  etat.valeurs[p.cle] = valeur;
  const refus = await synchroniser([p]);
  if (p.moteur && refus[p.moteur]) {
    etat.valeurs[p.cle] = ancienne;
    etat.refus[p.cle] = refus[p.moteur];
    etat.saisies[p.cle] = brut;
  }
  perimer();
  afficherProcede();
}

async function choisirProfil(id) {
  etat.profil = id;
  etat.refus = {};
  etat.saisies = {};
  for (const p of PARAMETRES) etat.valeurs[p.cle] = valeurProfil(p);
  await synchroniser(PARAMETRES);
  perimer();
  afficherProcede();
}

function rangParametre(p, avecChemin) {
  const modifie = estModifie(p);
  const large = p.type === 'choix' && p.choix.some((choix) => choix.length > 12);
  const rang = el('div', 'rang' + (large ? ' large' : '') + (modifie ? ' modifie' : '') +
    (p.moteur ? '' : ' prevu') + (etat.refus[p.cle] ? ' invalide' : ''));
  rang.title = (p.aide || p.libelle) + (p.moteur ? '' : ' (Prévu : pas encore appliqué par le slicer.)');
  const libelle = el('label', '', avecChemin ? `${p.section} · ${p.libelle}` : p.libelle);
  libelle.htmlFor = 'p-' + p.cle;
  if (!p.moteur) libelle.append(el('span', 'tag-prevu', 'prévu'));
  let champ;
  if (p.type === 'bool') {
    champ = el('input', 'case');
    champ.type = 'checkbox';
    champ.checked = etat.valeurs[p.cle];
    champ.addEventListener('change', () => changer(p, champ.checked));
  } else if (p.type === 'choix') {
    champ = el('select');
    for (const choix of p.choix) champ.add(new Option(choix, choix, false, choix === etat.valeurs[p.cle]));
    champ.addEventListener('change', () => changer(p, champ.value));
  } else {
    champ = el('input');
    champ.type = 'text';
    champ.inputMode = 'decimal';
    champ.autocomplete = 'off';
    champ.value = etat.saisies[p.cle] ?? texteValeur(p, etat.valeurs[p.cle]);
    champ.addEventListener('change', () => changer(p, champ.value));
  }
  champ.id = 'p-' + p.cle;
  // Un nombre s'écrit dans une boîte qui porte aussi son unité ; une liste
  // déroulante a son chevron à gauche.
  let boite = champ;
  if (p.type === 'nombre' || p.type === 'entier') {
    boite = el('span', 'champ-boite');
    boite.append(champ);
    if (p.unite) boite.append(el('span', 'unite', p.unite));
  } else if (p.type === 'choix') {
    boite = el('span', 'deroulant');
    boite.append(icone('chevron-bas'), champ);
  }
  const retablir = el('button', 'retablir');
  retablir.append(icone('retablir'));
  retablir.title = `Revenir à la valeur du profil : ${texteValeur(p, valeurProfil(p))}`;
  retablir.setAttribute('aria-label', `Rétablir ${p.libelle}`);
  retablir.addEventListener('click', () => changer(p, p.type === 'bool' || p.type === 'choix'
    ? valeurProfil(p) : ecrire(valeurProfil(p))));
  rang.append(el('span', 'marqueur'), libelle, boite, retablir);
  if (etat.refus[p.cle]) rang.append(el('div', 'refus', etat.refus[p.cle]));
  return rang;
}

function afficherProcede() {
  // Profils.
  const liste = $('profil-procede');
  const modifies = PARAMETRES.filter(estModifie);
  liste.replaceChildren(...PROFILS.map((p) => new Option(
    p.nom + (p.id === etat.profil && modifies.length ? ` (modifié : ${modifies.length})` : ''), p.id, false, p.id === etat.profil)));
  $('btn-retablir-tout').disabled = !modifies.length;

  // Onglets, avec le nombre de paramètres modifiés.
  const recherche = sansAccent(etat.recherche.trim());
  $('onglets').replaceChildren(...ONGLETS.map((nom) => {
    const bouton = el('button', nom === etat.onglet && !recherche ? 'actif' : '', nom);
    bouton.setAttribute('role', 'tab');
    bouton.setAttribute('aria-selected', String(nom === etat.onglet && !recherche));
    const compte = modifies.filter((p) => p.onglet === nom).length;
    if (compte) bouton.append(el('span', 'compte', String(compte)));
    bouton.addEventListener('click', () => { etat.onglet = nom; etat.recherche = ''; $('recherche').value = ''; afficherProcede(); });
    return bouton;
  }));

  // Paramètres : ceux de l'onglet, ou ceux qui correspondent à la recherche.
  const visibles = PARAMETRES.filter((p) => {
    if (recherche) return sansAccent(`${p.libelle} ${p.section} ${p.onglet}`).includes(recherche);
    return p.onglet === etat.onglet && (etat.avance || !p.avance || estModifie(p));
  });
  const zone = $('liste');
  const defilement = zone.scrollTop;
  zone.replaceChildren();
  if (!visibles.length) {
    zone.append(el('div', 'vide', `Aucun paramètre ne correspond à « ${etat.recherche} ».`));
    return;
  }
  let section = null;
  for (const p of visibles) {
    const titre = recherche ? p.onglet : p.section;
    if (titre !== section) {
      section = titre;
      const ligne = el('div', 'groupe-titre');
      ligne.append(icone(ICONES_GROUPE[titre] || 'reglages'), el('span', '', titre));
      zone.append(ligne);
    }
    zone.append(rangParametre(p, Boolean(recherche)));
  }
  const caches = recherche ? 0 : PARAMETRES.filter((p) => p.onglet === etat.onglet && p.avance && !etat.avance && !estModifie(p)).length;
  if (caches) zone.append(el('div', 'legende-prevu', `${caches} paramètre(s) avancé(s) masqué(s) : active « Avancé ».`));
  zone.scrollTop = defilement;
}

// --- Stratégies d'impression ------------------------------------------------------

// Les stratégies qui inclinent la tête et font tourner le plateau.
const STRATEGIES_5_AXES = ['couches_courbes'];

// Liste des façons d'imprimer le modèle ouvert, avec ce qu'il resterait à soutenir.
function afficherStrategies() {
  const cinqAxes = STRATEGIES_5_AXES.includes(etat.strategie);
  dessinerRepere();
  for (const [id, actif] of [['mode-3axes', !cinqAxes], ['mode-5axes', cinqAxes]]) {
    $(id).classList.toggle('actif', actif);
    $(id).setAttribute('aria-pressed', String(actif));
  }
  const bloc = $('bloc-strategie');
  bloc.hidden = false;
  // Les deux cartes : ce que chaque stratégie laisserait à soutenir.
  for (const [id, type] of [['mode-3axes', 'classique'], ['mode-5axes', 'couches_courbes']]) {
    const proposee = etat.strategies.find((st) => st.type === type);
    $(id).querySelector('.carte-note').textContent = etat.calculStrategies ? 'calcul…'
      : proposee ? `${entier(proposee.surplomb_mm2)} mm² à soutenir`
        : (type === 'classique' ? 'couches horizontales' : 'tête et plateau inclinés');
  }
  $('strategie').parentElement.hidden = !etat.strategies.length;
  if (!etat.modele || !etat.strategies.length) {
    $('strategie-detail').textContent = etat.modele && etat.calculStrategies ? 'Recherche des façons d\'imprimer cette pièce…'
      : 'Ouvre un modèle : le slicer dira ce que chaque stratégie laisse à soutenir.';
    afficherOndulation();
    return;
  }
  $('strategie').replaceChildren(...etat.strategies.map((s) => new Option(
    `${s.nom} · ${entier(s.surplomb_mm2)} mm² à soutenir`, s.type, false, s.type === etat.strategie)));
  const choisie = etat.strategies.find((s) => s.type === etat.strategie) || etat.strategies[0];
  const detail = $('strategie-detail');
  detail.replaceChildren(document.createTextNode(choisie.description));
  if (choisie.type === 'couches_courbes' && etat.zones.length) {
    // Des zones choisies à la main : ce sont elles qui décident, pas les parties trouvées par le slicer.
    detail.replaceChildren(document.createTextNode(`Ondulation par zones : ${etat.zones.length} zone(s) choisie(s) sur la pièce. ` +
      'Le bas est imprimé à plat, puis chaque zone dans sa direction.'));
    afficherOndulation();
    return;
  }
  if (choisie.parties.length > 1) {
    detail.append(document.createTextNode(' Inclinaisons : '),
      el('b', '', choisie.parties.map((p) => (p.courbe ? `jusqu'à ${fr(p.inclinaison, 0)}° (courbe)` : `${fr(p.inclinaison, 0)}°`)).join(', ')),
      document.createTextNode('.'));
  }
  if (choisie.type === 'couches_courbes') {
  }
  for (const remarque of choisie.remarques) detail.append(el('span', 'remarque', remarque));
  afficherOndulation();
}

// --- Relevés, modèle, tranchage --------------------------------------------------

const ICONES_ETAT = { bon: 'ok', mauvais: 'erreur', perime: 'attention' };
function definirEtat(classe, titre, detail) {
  $('releve-etat').className = 'etat-verification ' + classe;
  $('etat-icone').replaceChildren(icone(ICONES_ETAT[classe] || 'info'));
  $('r-etat').textContent = titre;
  $('r-etat-detail').textContent = detail;
  $('r-etat-detail').title = detail;
  // La petite barre : pleine quand le tranchage est fait, vide sinon.
  $('barre-progression').value = classe === 'bon' || classe === 'mauvais' ? 100 : 0;
}

function viderReleves() {
  $('etat-resultat').hidden = true;
  $('console').hidden = true;
  $('btn-messages').hidden = true;
  etat.remarques = [];
}

function perimer() {
  const avait = Boolean(etat.resultat);
  etat.resultat = null;
  vue3d.viderApercu();
  $('btn-exporter').disabled = true;
  $('vue-apercu').disabled = true;
  viderReleves();
  if (!etat.modele) definirEtat('', 'En attente', 'Ouvre un modèle pour commencer.');
  else if (!etat.modele.ferme) definirEtat('mauvais', 'Modèle ouvert', 'Le modèle n\'est pas un volume fermé : il faut le réparer.');
  else if (avait) definirEtat('perime', 'À retrancher', 'Un réglage a changé depuis le dernier tranchage.');
  else definirEtat('', 'Prêt', 'Clique sur « Trancher le plateau ».');
  if (etat.page === 'apercu') choisirVue('preparer');
}

async function ouvrirModele() {
  const choix = await demander('dialogue_ouvrir', { titre: 'Ouvrir un modèle',
    filtres: [['Modèles STL', '*.stl'], ['Tous les fichiers', '*.*']] });
  if (!choix || !choix.chemin) return;
  await chargerModele(choix.chemin);
}

// Charge un fichier dans le plateau courant.
async function chargerModele(chemin) {
  occuper('Lecture du modèle…');
  const reponse = await demander('charger_modele', { chemin });
  liberer();
  if (!reponse) return false;
  etat.chemin = chemin;
  etat.echelle = 1;
  etat.orientation = 'origine';
  etat.rotation = [0, 0, 0];
  etat.decalage = [0, 0];
  etat.coupes = [];
  etat.plateaux[etat.plateau] = { chemin, echelle: 1, orientation: 'origine', rotation: [0, 0, 0], decalage: [0, 0] };
  await poserModele(reponse.modele);
  noter();
  return true;
}

// --- Calculs en arrière-plan ---------------------------------------------------------
// Les façons d'imprimer la pièce sont cherchées sans bloquer l'écran. Tant que
// ce n'est pas fini, les anciennes restent affichées avec la mention « calcul… ».

let generationStrategies = 0, minuterieStrategies = 0;
function signalerCalcul(enCours) {
  etat.calculStrategies = enCours;
  document.body.dataset.calcul = enCours ? '1' : '0';
  $('calcul').hidden = !enCours;
}
function chercherStrategies(delai = 0) {
  const generation = ++generationStrategies;
  signalerCalcul(true);
  clearTimeout(minuterieStrategies);
  minuterieStrategies = setTimeout(async () => {
    const proposees = await appeler('strategies');
    if (generation !== generationStrategies) return;   // la pièce a encore changé entre-temps
    etat.strategies = proposees.ok ? proposees.strategies : [];
    if (!etat.strategies.some((st) => st.type === etat.strategie)) etat.strategie = 'classique';
    signalerCalcul(false);
    afficherStrategies();
  }, delai);
}
// Les demandes au slicer partent l'une après l'autre, dans l'ordre des gestes.
let fileMoteur = Promise.resolve();
function enFile(action) {
  const suite = fileMoteur.then(action, action);
  fileMoteur = suite.catch(() => {});
  return suite;
}

// Met en place le modèle rendu par le slicer (après ouverture, mise à l'échelle ou orientation).
// « glissement » [dx, dy] : la pièce a seulement glissé sur le plateau ; rien n'est à recalculer.
async function poserModele(modele, garderVue = false, glissement = null) {
  etat.modele = modele;
  etat.plateaux[etat.plateau].positions = modele.positions;   // pour le dessiner quand un autre plateau est choisi
  etat.resultat = null;
  let zonesPerdues = 0;
  if (glissement) {
    // Les zones suivent la pièce.
    for (const zone of etat.zones) { zone.point[0] += glissement[0]; zone.point[1] += glissement[1]; }
  } else {
    etat.branches = {};
    // (Le modèle a changé de forme ou de pose : les points choisis ne sont plus les bons.)
    zonesPerdues = etat.zones.length;
    oublierZones();
  }
  etat.modele.nom = etat.chemin.split(/[\\/]/).pop();
  if (glissement) vue3d.glisserModele(glissement[0], glissement[1]); else vue3d.afficherModele(modele.positions, garderVue);
  if (glissement) vue3d.afficherZones(etat.zones, etat.zoneChoisie);
  vue3d.rafraichirTheme();
  $('accueil').hidden = true;
  const [x, y, z] = etat.modele.dimensions;
  $('r-modele').textContent = etat.modele.nom;
  $('r-dimensions').textContent = `${fr(x)} × ${fr(y)} × ${fr(z)} mm · ${entier(etat.modele.triangles)} triangles` +
    (etat.echelle !== 1 ? ` · échelle ${fr(etat.echelle * 100, 0)} %` : '');
  $('titre-document').textContent = etat.modele.nom.replace(/\.[^.]+$/, '');
  if (!glissement) chercherStrategies(garderVue ? 500 : 0);
  afficherStrategies();
  if (!etat.modele.ferme) dire(etat.modele.message_fermeture);
  else if (zonesPerdues) dire('Le modèle a changé : les zones orientées ont été retirées. Remets-les sur la pièce.', { info: true });
  $('btn-trancher').disabled = false;
  perimer();
  afficherPlateaux();
  choisirVue('preparer');
}

// --- Outils : échelle, orientation, agencement, plateaux -----------------------------

// Applique une échelle ou une orientation au modèle ouvert.
async function transformer(parametres, message) {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
  // Un simple déplacement : la pièce glisse tout de suite à l'écran, le slicer suit en arrière-plan.
  const cles = Object.keys(parametres);
  const depart = [...etat.decalage];
  const glisse = cles.length === 1 && cles[0] === 'decalage';
  if (glisse) vue3d.glisserProvisoire(parametres.decalage[0] - depart[0], parametres.decalage[1] - depart[1]);
  $('calcul').hidden = false;
  const reponse = await enFile(() => demander('transformer_modele', parametres));
  if (!etat.calculStrategies) $('calcul').hidden = true;
  if (!reponse) { if (glisse) vue3d.glisserProvisoire(0, 0); return; }
  const glissement = glisse ? [reponse.decalage[0] - depart[0], reponse.decalage[1] - depart[1]] : null;
  etat.echelle = reponse.echelle ?? etat.echelle;
  etat.orientation = reponse.orientation ?? etat.orientation;
  etat.rotation = reponse.rotation ?? etat.rotation;
  etat.decalage = reponse.decalage ?? etat.decalage;
  etat.plateaux[etat.plateau] = { chemin: etat.chemin, echelle: etat.echelle, orientation: etat.orientation,
    rotation: etat.rotation, decalage: etat.decalage, positions: etat.plateaux[etat.plateau]?.positions };
  await poserModele(reponse.modele, true, glissement);
  dire(((parametres.orientation || parametres.rotation) && reponse.note) || message, { info: true });
  noter();
}

// --- Annuler, refaire --------------------------------------------------------------
// L'historique garde la pièce et ce qu'on lui a fait : pose, échelle, coupes,
// stratégie, zones. (Les paramètres du procédé n'en font pas partie.)

const historique = { passe: [], futur: [], courant: null, gele: false };
const instantane = () => structuredClone({ chemin: etat.chemin, echelle: etat.echelle, orientation: etat.orientation,
  rotation: etat.rotation, decalage: etat.decalage, coupes: etat.coupes, strategie: etat.strategie,
  branches: etat.branches, zones: etat.zones });
function boutonsHistorique() {
  $('btn-annuler').disabled = !historique.passe.length;
  $('btn-refaire').disabled = !historique.futur.length;
}
// À appeler après chaque action aboutie.
function noter() {
  if (historique.gele) return;
  const ici = instantane();
  if (historique.courant && JSON.stringify(ici) === JSON.stringify(historique.courant)) return;
  if (historique.courant) historique.passe.push(historique.courant);
  if (historique.passe.length > 50) historique.passe.shift();
  historique.courant = ici;
  historique.futur = [];
  boutonsHistorique();
}
let minuterieNote = 0;
function noterBientot() { clearTimeout(minuterieNote); minuterieNote = setTimeout(noter, 700); }
function oublierHistorique() {
  historique.passe = [];
  historique.futur = [];
  historique.courant = etat.chemin ? instantane() : null;
  boutonsHistorique();
}

const poseDe = (i) => {
  const parametres = { echelle: i.echelle, decalage: i.decalage ?? [0, 0] };
  if (i.orientation === 'auto') parametres.orientation = 'auto'; else parametres.rotation = i.rotation ?? [0, 0, 0];
  return parametres;
};

// Remet la pièce dans l'état d'un instantané (historique ou projet). Rend faux en cas d'échec.
async function restaurer(cible) {
  historique.gele = true;
  try {
    if (!cible.chemin) { await fermerModele(); return true; }
    const memeBase = cible.chemin === etat.chemin && JSON.stringify(cible.coupes ?? []) === JSON.stringify(etat.coupes);
    if (!memeBase) {
      if (!(await chargerModele(cible.chemin))) return false;
      // Les coupes sont refaites dans l'ordre, chacune dans la pose où elle avait été faite.
      for (const coupe of cible.coupes ?? []) {
        await transformer(coupe.pose, 'Pièce remise en place.');
        if (!(await couper(coupe.hauteur, coupe.garder))) return false;
      }
    }
    await transformer(poseDe(cible), 'Pièce remise en place.');
    // (Les stratégies arrivent en arrière-plan : si celle-ci n'est finalement pas proposée, le classique reprend la main.)
    etat.strategie = cible.strategie === 'couches_courbes' ? 'couches_courbes' : 'classique';
    etat.branches = structuredClone(cible.branches ?? {});
    etat.zones = structuredClone(cible.zones ?? []);
    etat.zoneChoisie = -1;
    zonesChangees();
    return true;
  } finally {
    historique.gele = false;
  }
}

async function annuler() {
  if (!historique.passe.length) { dire('Il n\'y a rien à annuler.', { info: true }); return; }
  clearTimeout(minuterieNote);
  const cible = historique.passe.pop(), depart = historique.courant;
  if (!(await restaurer(cible))) { historique.passe.push(cible); return; }
  historique.futur.push(depart);
  historique.courant = cible;
  boutonsHistorique();
  dire('Dernière action annulée.', { info: true });
}
async function refaire() {
  if (!historique.futur.length) { dire('Il n\'y a rien à refaire.', { info: true }); return; }
  const cible = historique.futur.pop(), depart = historique.courant;
  if (!(await restaurer(cible))) { historique.futur.push(cible); return; }
  historique.passe.push(depart);
  historique.courant = cible;
  boutonsHistorique();
  dire('Action refaite.', { info: true });
}

// --- Couper la pièce ---------------------------------------------------------------

// Coupe la pièce posée par un plan horizontal et garde le bas ou le haut. Rend faux en cas d'échec.
async function couper(hauteur, garder) {
  const pose = poseDe(etat);
  occuper('Coupe de la pièce…');
  const reponse = await demander('couper_modele', { hauteur, garder });
  liberer();
  if (!reponse) return false;
  etat.coupes = [...etat.coupes, { hauteur, garder, pose }];
  etat.echelle = reponse.echelle;
  etat.orientation = reponse.orientation;
  etat.rotation = reponse.rotation;
  etat.decalage = reponse.decalage;
  etat.plateaux[etat.plateau] = { ...etat.plateaux[etat.plateau], echelle: etat.echelle, orientation: etat.orientation,
    rotation: etat.rotation, decalage: etat.decalage };
  await poserModele(reponse.modele, true);
  return true;
}

async function outilCouper() {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
  const haut = etat.modele.dimensions[2];
  const champ = el('input');
  champ.type = 'text';
  champ.id = 'coupe-hauteur';
  champ.inputMode = 'decimal';
  champ.value = ecrire(Math.round(haut * 5) / 10);
  const etiquette = el('label', '', 'Hauteur de la coupe, depuis le plateau (mm)');
  etiquette.append(champ);
  const choix = el('select');
  choix.id = 'coupe-garder';
  choix.append(new Option('Garder le bas', 'bas'), new Option('Garder le haut', 'haut'));
  const etiquetteChoix = el('label', '', 'Morceau gardé');
  etiquetteChoix.append(choix);
  const aide = el('p', 'aide', `La pièce fait ${fr(haut)} mm de haut. Elle est coupée à plat à cette hauteur ; le morceau gardé ` +
    'est reposé sur le plateau, l\'autre est retiré. « Annuler » (en haut à gauche) revient à la pièce entière.' +
    (simulation ? ' (Simulation : la coupe demande le vrai slicer, dans l\'application.)' : ''));
  if (!(await fenetreOptions('Couper la pièce', [etiquette, etiquetteChoix, aide], 'Couper', 'coupe-appliquer', { panneau: true }))) return;
  const hauteur = lireNombre(champ.value);
  if (!(hauteur > 0 && hauteur < haut)) { dire(`La hauteur de coupe doit être entre 0 et ${fr(haut)} mm.`); return; }
  if (await couper(hauteur, choix.value)) {
    dire(`Pièce coupée à ${fr(hauteur)} mm : le ${choix.value === 'bas' ? 'bas' : 'haut'} est gardé.`, { info: true });
    noter();
  }
}

// --- Projet : nouveau, enregistrer, ouvrir ------------------------------------------

const FILTRE_PROJET = [['Projet ArtPrint Slicer', '*.artprint']];

async function fermerModele() {
  await demander('fermer_modele');
  oublierZones();
  etat.modele = null;
  etat.chemin = null;
  etat.coupes = [];
  etat.strategies = [];
  etat.strategie = 'classique';
  etat.branches = {};
  etat.echelle = 1;
  etat.orientation = 'origine';
  etat.rotation = [0, 0, 0];
  etat.decalage = [0, 0];
  etat.plateaux = [{}];
  etat.plateau = 0;
  vue3d.afficherModele([]);
  $('r-modele').textContent = 'Aucun modèle';
  $('r-dimensions').textContent = '';
  $('titre-document').textContent = 'Sans titre';
  $('btn-trancher').disabled = true;
  $('accueil').hidden = false;
  afficherStrategies();
  perimer();
  afficherPlateaux();
}

async function nouveauProjet() {
  await fermerModele();
  etat.cheminProjet = null;
  oublierHistorique();
  dire('Nouveau projet : le plateau est vide. Les réglages sont gardés.', { info: true });
}

async function enregistrerProjet() {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle : il n\'y a rien à enregistrer.', { info: true }); return; }
  const nom = (etat.modele.nom || 'piece').replace(/\.[^.]+$/, '') + '.artprint';
  const choix = await demander('dialogue_enregistrer', { titre: 'Enregistrer le projet', nom_propose: etat.cheminProjet || nom, filtres: FILTRE_PROJET });
  if (!choix || !choix.chemin) return;
  const projet = { format: 'artprint-projet', version: 1, ...instantane(), profil: etat.profil, valeurs: etat.valeurs,
    reglages: Object.fromEntries(etat.moteur.map((r) => [r.cle, r.valeur])) };
  if (!(await demander('enregistrer_projet', { chemin: choix.chemin, projet }))) return;
  etat.cheminProjet = choix.chemin;
  dire(`Projet enregistré : ${choix.chemin}. Il garde le chemin du modèle, sa pose, les coupes, les zones et les réglages.`, { info: true });
}

async function ouvrirProjet() {
  const choix = await demander('dialogue_ouvrir', { titre: 'Ouvrir un projet', filtres: [...FILTRE_PROJET, ['Tous les fichiers', '*.*']] });
  if (!choix || !choix.chemin) return;
  const reponse = await demander('charger_projet', { chemin: choix.chemin });
  if (!reponse) return;
  const projet = reponse.projet;
  // Les réglages d'abord (la pose automatique et les stratégies en dépendent).
  if (projet.reglages) {
    const retour = await demander('definir_reglages', { valeurs: projet.reglages });
    if (retour) etat.moteur = retour.reglages;
  }
  if (projet.valeurs) {
    if (projet.profil) etat.profil = projet.profil;
    for (const p of PARAMETRES) if (p.cle in projet.valeurs) etat.valeurs[p.cle] = projet.valeurs[p.cle];
    etat.saisies = {};
    await synchroniser(PARAMETRES);
    afficherProcede();
  }
  afficherImprimante();
  if (!(await restaurer(projet))) {
    dire(`Le modèle du projet est introuvable : ${projet.chemin}. Le projet garde le chemin du fichier, pas le fichier lui-même.`);
    return;
  }
  etat.cheminProjet = choix.chemin;
  oublierHistorique();
  dire(`Projet ouvert : ${choix.chemin}.`, { info: true });
}

async function exporterStl() {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
  const nom = (etat.modele.nom || 'piece').replace(/\.[^.]+$/, '') + '-pose.stl';
  const choix = await demander('dialogue_enregistrer', { titre: 'Exporter en STL', nom_propose: nom, filtres: [['Modèles STL', '*.stl']] });
  if (!choix || !choix.chemin) return;
  if (!(await demander('exporter_stl', { chemin: choix.chemin }))) return;
  dire(`Modèle exporté tel qu'il est posé (échelle, rotation, coupes) : ${choix.chemin}.`, { info: true });
}

// Demande des paramètres. D'ordinaire dans une fenêtre ; avec « panneau », dans
// le petit rectangle qui s'ouvre sous la barre d'outils. « raccourcis » : des
// boutons qui agissent tout de suite ([libellé, identifiant, action]).
// Rend vrai si l'utilisateur valide, faux s'il ferme.
function fenetreOptions(titre, contenu, libelleAction, idAction, { panneau = false, raccourcis = [] } = {}) {
  return new Promise((resoudre) => {
    const corps = el('div', 'options-liste');
    corps.append(...contenu);
    if (raccourcis.length) {
      const rangee = el('div', 'raccourcis');
      for (const [libelle, id, action] of raccourcis) {
        const bouton = el('button', 'bouton', libelle);
        bouton.id = id;
        bouton.addEventListener('click', () => { finir(false); action(); });
        rangee.append(bouton);
      }
      corps.append(rangee);
    }
    const actions = el('div', 'options-actions');
    const annuler = el('button', 'bouton', panneau ? 'Fermer' : 'Annuler');
    annuler.id = 'options-annuler';
    actions.append(annuler);
    let valider = null;
    if (libelleAction) {
      valider = el('button', 'bouton plein', libelleAction);
      valider.id = idAction;
      actions.append(valider);
    }
    corps.append(actions);
    let fini = false;
    let clore = () => {};
    function finir(valeur) {
      if (fini) return;
      fini = true;
      clore();
      resoudre(valeur);
    }
    if (panneau) {
      const cadre = $('panneau-outil');
      etat.fermerPanneau?.();
      const tete = el('div', 'panneau-tete');
      tete.append(el('b', '', titre));
      cadre.replaceChildren(tete, corps);
      cadre.hidden = false;
      clore = () => { cadre.hidden = true; cadre.replaceChildren(); etat.fermerPanneau = null; };
      etat.fermerPanneau = () => finir(false);
      cadre.onkeydown = (e) => {
        if (e.key === 'Escape') finir(false);
        if (e.key === 'Enter' && valider && e.target.tagName === 'INPUT') finir(true);
      };
    } else {
      clore = ouvrirFeuille(titre, corps, { simple: true });
      document.getElementById('feuille-fermer').addEventListener('click', () => finir(false));
      document.querySelector('#feuilles .voile').addEventListener('click', (e) => { if (e.target.classList.contains('voile')) finir(false); });
    }
    annuler.addEventListener('click', () => finir(false));
    valider?.addEventListener('click', () => finir(true));
    (corps.querySelector('input, select') ?? valider ?? annuler).focus();
  });
}

async function outilEchelle() {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
  const champ = el('input');
  champ.type = 'text';
  champ.id = 'echelle-valeur';
  champ.inputMode = 'decimal';
  champ.value = ecrire(etat.echelle * 100);
  const [x, y, z] = etat.modele.dimensions;
  const etiquette = el('label', '', 'Échelle, en pour cent de la taille d\'origine');
  etiquette.append(champ);
  const aide = el('p', 'aide', `Taille actuelle : ${fr(x)} × ${fr(y)} × ${fr(z)} mm. 100 % redonne la taille d'origine.`);
  const tailles = [50, 100, 150, 200].map((pc) => [`${pc} %`, `echelle-${pc}`,
    async () => { await transformer({ echelle: pc / 100 }, `Modèle mis à l'échelle : ${pc} %.`); outilEchelle(); }]);
  if (!(await fenetreOptions('Mettre à l\'échelle', [etiquette, aide], 'Appliquer', 'echelle-appliquer',
    { panneau: true, raccourcis: tailles }))) return;
  const pourcent = lireNombre(champ.value);
  if (!(pourcent >= 1 && pourcent <= 10000)) { dire('L\'échelle doit être un nombre entre 1 et 10 000 %.'); return; }
  await transformer({ echelle: pourcent / 100 }, `Modèle mis à l'échelle : ${fr(pourcent, 0)} %.`);
  outilEchelle();
}

async function outilOrienter() {
  await transformer({ orientation: 'auto' }, 'Orientation choisie par le slicer.');
  if (etat.orientation === 'auto') {
    dire($('message-texte').textContent, { info: true, tenir: true,
      actions: [{ libelle: 'Revenir à la pose d\'origine', action: () => transformer({ orientation: 'origine' }, 'Pose d\'origine rétablie.') }] });
  }
}

// Un champ de nombre avec son étiquette, pour les fenêtres d'outils.
function champNombre(id, libelle, valeur) {
  const etiquette = el('label', '', libelle);
  const champ = el('input');
  champ.type = 'text';
  champ.id = id;
  champ.inputMode = 'decimal';
  champ.value = ecrire(valeur);
  etiquette.append(champ);
  return [etiquette, champ];
}

// Une rangée de champs, un par axe, avec la lettre de l'axe dans sa couleur.
function rangeeAxes(libelle, champs, unite) {
  const rangee = el('div', 'rangee-axes');
  rangee.append(el('span', 'rangee-libelle', libelle));
  for (const [axe, id, valeur, titre] of champs) {
    const etiquette = el('label', 'champ-axe');
    etiquette.title = titre;
    const lettre = el('b', '', 'XYZ'[axe]);
    lettre.style.color = COULEURS_AXES[axe];
    const champ = el('input');
    champ.type = 'text';
    champ.id = id;
    champ.inputMode = 'decimal';
    champ.value = ecrire(valeur);
    champ.setAttribute('aria-label', titre);
    etiquette.append(lettre, champ);
    rangee.append(etiquette);
  }
  rangee.append(el('span', 'rangee-unite', unite));
  return rangee;
}

// Les poignées 3D suivent l'outil ouvert : elles disparaissent quand son panneau se ferme.
new MutationObserver(() => { if ($('panneau-outil').hidden) vue3d.manipuler(null); })
  .observe($('panneau-outil'), { attributes: true, attributeFilter: ['hidden'] });

// La rotation à la main part de la pose du fichier : la pose automatique est d'abord quittée.
async function quitterPoseAutomatique() {
  if (etat.orientation !== 'auto') return;
  await transformer({ orientation: 'origine' }, 'Pose du fichier rétablie.');
  dire('La pose automatique a été quittée : la pièce repart de la pose du fichier.', { info: true });
}

async function outilDeplacer() {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
  const rangee = rangeeAxes('Position', [
    [0, 'deplacer-x', etat.decalage[0], 'Vers la droite (X), en mm depuis le centre du plateau'],
    [1, 'deplacer-y', etat.decalage[1], 'Vers l\'arrière (Y), en mm depuis le centre du plateau']], 'mm');
  const aide = el('p', 'aide', 'Tire une flèche pour glisser le long d\'un axe, ou attrape la pièce pour la glisser librement. ' +
    '0 et 0 la remettent au centre.');
  const pousser = (dx, dy) => async () => {
    await transformer({ decalage: [etat.decalage[0] + dx, etat.decalage[1] + dy] }, 'Pièce déplacée.');
    outilDeplacer();
  };
  const fleches = [['← 10', 'deplacer-gauche', pousser(-10, 0)], ['→ 10', 'deplacer-droite', pousser(10, 0)],
    ['↑ 10', 'deplacer-arriere', pousser(0, 10)], ['↓ 10', 'deplacer-avant', pousser(0, -10)],
    ['Au centre', 'deplacer-centre', async () => { await transformer({ decalage: [0, 0] }, 'Pièce remise au centre.'); outilDeplacer(); }]];
  const ouverture = fenetreOptions('Déplacer', [rangee, aide], 'Appliquer', 'deplacer-appliquer', { panneau: true, raccourcis: fleches });
  vue3d.manipuler('deplacer');
  if (!(await ouverture)) return;
  const x = lireNombre($('deplacer-x')?.value ?? rangee.querySelector('#deplacer-x').value), y = lireNombre(rangee.querySelector('#deplacer-y').value);
  if (!Number.isFinite(x) || !Number.isFinite(y)) { dire('Les deux distances doivent être des nombres.'); return; }
  await transformer({ decalage: [x, y] }, `Pièce placée à ${fr(x)} mm en X et ${fr(y)} mm en Y du centre.`);
  outilDeplacer();
}

async function outilTourner() {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
  await quitterPoseAutomatique();
  const rangee = rangeeAxes('Rotation', [
    [0, 'tourner-x', etat.rotation[0], 'Autour de X (bascule vers l\'avant ou l\'arrière), en degrés'],
    [1, 'tourner-y', etat.rotation[1], 'Autour de Y (bascule à gauche ou à droite), en degrés'],
    [2, 'tourner-z', etat.rotation[2], 'Autour de Z (tourne à plat sur le plateau), en degrés']], '°');
  const geste = el('p', 'aide', '');
  geste.id = 'tourner-geste';
  const aide = el('p', 'aide', 'Tire un anneau pour tourner la pièce : rouge autour de X, vert autour de Y, bleu autour de Z ' +
    '(touche Maj : par pas de 15°). Les angles sont comptés depuis la pose du fichier ; la pièce est ensuite reposée sur le plateau.');
  const pivoter = (axe, angle) => async () => {
    await transformer({ rotation: composerRotation(etat.rotation, axe, angle) }, 'Pièce tournée.');
    outilTourner();
  };
  const quarts = [['X +90°', 'tourner-x-plus', pivoter(0, 90)], ['X −90°', 'tourner-x-moins', pivoter(0, -90)],
    ['Y +90°', 'tourner-y-plus', pivoter(1, 90)], ['Y −90°', 'tourner-y-moins', pivoter(1, -90)],
    ['Z +45°', 'tourner-z-plus', pivoter(2, 45)], ['Z −45°', 'tourner-z-moins', pivoter(2, -45)],
    ['Pose du fichier', 'tourner-origine', async () => { await transformer({ rotation: [0, 0, 0] }, 'Pose du fichier rétablie.'); outilTourner(); }]];
  const ouverture = fenetreOptions('Tourner', [rangee, geste, aide], 'Appliquer', 'tourner-appliquer', { panneau: true, raccourcis: quarts });
  vue3d.manipuler('tourner');
  if (!(await ouverture)) return;
  const angles = ['tourner-x', 'tourner-y', 'tourner-z'].map((id) => lireNombre(rangee.querySelector('#' + id).value));
  if (angles.some((a) => !Number.isFinite(a))) { dire('Les trois angles doivent être des nombres.'); return; }
  await transformer({ rotation: angles }, `Pièce tournée de ${angles.map((a) => fr(a, 0) + '°').join(', ')}.`);
  outilTourner();
}
// Un anneau est tourné à la souris : l'angle s'affiche, puis la pièce est tournée pour de bon.
vue3d.surRotationEnCours = (axe, degres) => {
  const geste = $('tourner-geste');
  if (geste) geste.textContent = `Autour de ${'XYZ'[axe]} : ${degres > 0 ? '+' : ''}${degres}°`;
};
vue3d.surRotation = async (axe, degres) => {
  await transformer({ rotation: composerRotation(etat.rotation, axe, degres) }, `Pièce tournée de ${degres}° autour de ${'XYZ'[axe]}.`);
  if (!$('panneau-outil').hidden && $('tourner-x')) outilTourner();
};

// Poser sur une face : les faces planes de l'enveloppe de la pièce sont grisées
// dans la vue ; un clic sur l'une d'elles la pose contre le plateau.
const FACES = [
  ['origine', 'Le dessous (comme dans le fichier)', [0, 0, 0]],
  ['dessus', 'Le dessus (pièce retournée)', [180, 0, 0]],
  ['avant', 'L\'avant', [-90, 0, 0]],
  ['arriere', 'L\'arrière', [90, 0, 0]],
  ['gauche', 'Le côté gauche', [0, 90, 0]],
  ['droite', 'Le côté droit', [0, -90, 0]],
  ['auto', 'Automatique : la pose qui demande le moins de supports', null],
];

async function outilPoserFace() {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
  await quitterPoseAutomatique();
  const aide = el('p', 'aide', 'Clique dans la vue sur une des faces grisées : la pièce est tournée pour la poser contre le plateau. ' +
    'Ce sont les faces planes de son contour, celles sur lesquelles elle tiendrait posée sur une table.');
  const choix = el('select');
  choix.id = 'face-choix';
  for (const [valeur, texte] of FACES) choix.append(new Option(texte, valeur));
  const etiquette = el('label', '', 'Ou choisir un côté de la pièce');
  etiquette.append(choix);
  const poser = (face) => async () => {
    if (face[2]) await transformer({ rotation: face[2] }, `Pièce posée sur : ${face[1].toLowerCase()}.`);
    else await outilOrienter();
  };
  const boutons = [['Automatique', 'face-auto', poser(FACES.find((f) => f[0] === 'auto'))],
    ['Pose du fichier', 'face-origine', poser(FACES[0])]];
  const ouverture = fenetreOptions('Poser sur une face', [aide, etiquette], 'Poser', 'face-appliquer', { panneau: true, raccourcis: boutons });
  vue3d.manipuler('faces');
  if (!(await ouverture)) return;
  await poser(FACES.find((f) => f[0] === choix.value))();
}
vue3d.surFace = async (normale) => {
  await transformer({ rotation: rotationPourPoser(etat.rotation, normale) }, 'Pièce posée sur la face choisie.');
  if (!$('panneau-outil').hidden && $('face-choix')) vue3d.manipuler('faces');
};

// --- Le repère des axes, dans le coin de la vue -----------------------------------
// X, Y, Z comme sur la machine ; en ondulation, les deux rotations s'ajoutent :
// B (la tête s'incline) et C (le plateau tourne).

function dessinerRepere() {
  const svg = $('repere');
  if (!svg || !vue3d.versEcran) return;
  const NS = 'http://www.w3.org/2000/svg';
  const L = 30;   // longueur d'un axe, en unités du dessin
  const trait = (points, couleur, pointille) => {
    const ligne = document.createElementNS(NS, 'polyline');
    ligne.setAttribute('points', points.map(([x, y]) => `${(x * L).toFixed(1)},${(-y * L).toFixed(1)}`).join(' '));
    ligne.setAttribute('stroke', couleur);
    if (pointille) ligne.setAttribute('stroke-dasharray', '2 2');
    return ligne;
  };
  const lettre = (texte, [x, y], couleur) => {
    const t = document.createElementNS(NS, 'text');
    t.setAttribute('x', (x * L).toFixed(1));
    t.setAttribute('y', (-y * L).toFixed(1));
    t.setAttribute('fill', couleur);
    t.textContent = texte;
    return t;
  };
  const morceaux = [];   // [profondeur, élément] : le plus loin est dessiné d'abord
  const axes = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
  axes.forEach((axe, i) => {
    const bout = vue3d.versEcran(...axe);
    morceaux.push([bout[2] / 2, trait([[0, 0], bout], COULEURS_AXES[i])]);
    morceaux.push([bout[2], lettre('XYZ'[i], [bout[0] * 1.28, bout[1] * 1.28], COULEURS_AXES[i])]);
  });
  if (etat.strategie === 'couches_courbes' && etat.modele) {
    // C : le plateau tourne autour de Z. B : la tête s'incline autour de son pivot (X ou Y, d'après les réglages).
    const pivotX = String(moteur('axe_pivot_b')).toUpperCase() === 'X';
    const arc = (point, de, vers) => Array.from({ length: 25 }, (_, k) => vue3d.versEcran(...point((de + (vers - de) * k / 24) * Math.PI / 180)));
    const arcs = [
      ['C', arc((t) => [0.62 * Math.cos(t), 0.62 * Math.sin(t), 0.72], 20, 300), COULEURS_AXES[2]],
      ['B', arc((t) => (pivotX ? [0.72, 0.62 * Math.sin(t), 0.62 * Math.cos(t)] : [0.62 * Math.sin(t), 0.72, 0.62 * Math.cos(t)]), -70, 70),
        COULEURS_AXES[pivotX ? 0 : 1]]];
    for (const [nom, points, couleur] of arcs) {
      const profondeur = points.reduce((somme, q) => somme + q[2], 0) / points.length;
      morceaux.push([profondeur, trait(points, couleur, true)]);
      const fin = points[points.length - 1], avant = points[points.length - 4];
      const dx = fin[0] - avant[0], dy = fin[1] - avant[1], n = Math.hypot(dx, dy) || 1;
      // (La pointe de la flèche, au bout de l'arc.)
      morceaux.push([profondeur, trait([[fin[0] - 0.16 * (dx / n) - 0.09 * (dy / n), fin[1] - 0.16 * (dy / n) + 0.09 * (dx / n)], fin,
        [fin[0] - 0.16 * (dx / n) + 0.09 * (dy / n), fin[1] - 0.16 * (dy / n) - 0.09 * (dx / n)]], couleur)]);
      morceaux.push([profondeur + 0.01, lettre(nom, [fin[0] + 0.2 * (dx / n), fin[1] + 0.2 * (dy / n)], couleur)]);
    }
  }
  morceaux.sort((p, q) => p[0] - q[0]);
  svg.replaceChildren(...morceaux.map((m) => m[1]));
  svg.setAttribute('aria-label', etat.strategie === 'couches_courbes' && etat.modele
    ? 'Repère des axes X, Y, Z et des rotations B (tête) et C (plateau)' : 'Repère des axes X, Y, Z');
}
vue3d.surVue = dessinerRepere;

function outilMesurer() {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
  const [x, y, z] = etat.modele.dimensions;
  const b = bobineMontee();
  const lignes = [
    ['Largeur (X)', `${fr(x, 2)} mm`], ['Profondeur (Y)', `${fr(y, 2)} mm`], ['Hauteur (Z)', `${fr(z, 2)} mm`],
    ['Volume', etat.modele.volume_cm3 === undefined ? '' : `${fr(etat.modele.volume_cm3, 2)} cm³`],
    ['Poids si la pièce était pleine', etat.modele.volume_cm3 === undefined ? '' : `${fr(etat.modele.volume_cm3 * (b?.densite ?? 1.24), 1)} g`],
    ['Triangles', entier(etat.modele.triangles)],
    ['Échelle', `${fr(etat.echelle * 100, 0)} %`],
    ['Position sur le plateau', `X ${fr(etat.decalage[0])} mm · Y ${fr(etat.decalage[1])} mm`],
    ['Surface à soutenir en classique', etat.strategies[0] ? `${entier(etat.strategies[0].surplomb_mm2)} mm²` : ''],
  ].filter(([, valeur]) => valeur !== '');
  const table = el('table', 'mesures');
  table.id = 'mesures';
  for (const [nom, valeur] of lignes) {
    const ligne = el('tr');
    ligne.append(el('td', '', nom), el('td', 'nombre', valeur));
    table.append(ligne);
  }
  fenetreOptions('Mesures de la pièce', [table], '', '', { panneau: true });
}

// Simulation de l'impression : les couches défilent toutes seules, de la première à la dernière.
function outilSimulation() {
  if (!etat.resultat) { dire('Tranche d\'abord le modèle : la simulation rejoue les couches du G-code.', { info: true }); return; }
  if (etat.lecture) { clearInterval(etat.lecture); etat.lecture = 0; dire('Simulation arrêtée.', { info: true }); return; }
  choisirVue('apercu');
  const curseur = $('curseur-couche');
  const total = Number(curseur.max);
  const pas = Math.max(1, Math.round(total / 240));
  curseur.value = 1;
  majCouche();
  dire('Simulation de l\'impression : les couches défilent. Reclique sur l\'outil pour arrêter.', { info: true });
  etat.lecture = setInterval(() => {
    const suivant = Math.min(total, Number(curseur.value) + pas);
    curseur.value = suivant;
    majCouche();
    if (suivant >= total) { clearInterval(etat.lecture); etat.lecture = 0; }
  }, 50);
}

function afficherPlateaux() {
  vue3d.definirPlateaux(etat.plateaux.length, etat.plateau, (numero) => choisirPlateau(numero),
    etat.plateaux.map((plateau) => plateau.positions ?? null));
  const barre = $('plateaux');
  barre.hidden = etat.plateaux.length < 2;
  barre.replaceChildren(...etat.plateaux.map((plateau, i) => {
    const bouton = el('button', '', `Plateau ${i + 1}`);
    bouton.id = `plateau-${i + 1}`;
    bouton.title = plateau.chemin ? plateau.chemin.split(/[\\/]/).pop() : 'Plateau vide';
    bouton.setAttribute('aria-pressed', String(i === etat.plateau));
    bouton.addEventListener('click', () => choisirPlateau(i));
    return bouton;
  }));
}

async function choisirPlateau(numero) {
  if (numero === etat.plateau) return;
  setTimeout(oublierHistorique, 0);   // (l'historique ne suit qu'un plateau à la fois)
  etat.plateau = numero;
  const plateau = etat.plateaux[numero];
  if (!plateau.chemin) {
    // Plateau vide : on attend qu'un modèle y soit ouvert.
    etat.modele = null;
    etat.chemin = null;
    etat.strategies = [];
    etat.strategie = 'classique';
    vue3d.afficherModele([]);
    $('r-modele').textContent = 'Aucun modèle';
    $('r-dimensions').textContent = `Plateau ${numero + 1} vide`;
    $('btn-trancher').disabled = true;
    afficherStrategies();
    perimer();
    afficherPlateaux();
    dire(`Plateau ${numero + 1} : ouvre un modèle pour le remplir.`, { info: true });
    return;
  }
  if (!(await chargerModele(plateau.chemin))) return;
  const tourne = (plateau.rotation ?? [0, 0, 0]).some((v) => v !== 0) || (plateau.decalage ?? [0, 0]).some((v) => v !== 0);
  if (plateau.echelle !== 1 || plateau.orientation !== 'origine' || tourne) {
    const parametres = { echelle: plateau.echelle, decalage: plateau.decalage ?? [0, 0] };
    if (plateau.orientation === 'auto') parametres.orientation = 'auto'; else parametres.rotation = plateau.rotation ?? [0, 0, 0];
    await transformer(parametres, `Plateau ${numero + 1}.`);
  }
}

function outilAjouterPlateau() {
  if (!etat.plateaux[etat.plateaux.length - 1].chemin) { dire('Le dernier plateau est encore vide : ouvre d\'abord un modèle dedans.', { info: true }); return; }
  etat.plateaux.push({});
  afficherPlateaux();
  vue3d.voirPlateaux();
  dire(`Plateau ${etat.plateaux.length} ajouté, à côté. Clique dessus (ou sur son bouton en bas) pour travailler dessus.`, { info: true });
}

// --- Ondulation : le panneau des réglages 5 axes, dans la colonne ---------------------

const CHOIX_BRANCHE = [
  ['courbes', 'Ondulation, tête d\'aplomb sur les couches'],
  ['incline60', 'Ondulation, tête moins inclinée (60° au plus)'],
  ['incline45', 'Ondulation, tête peu inclinée (45° au plus)'],
  ['a_plat', 'À plat, avec le corps de la pièce (couches horizontales)'],
];

// Plus grande inclinaison qu'une zone peut demander, d'après les réglages de la machine.
function inclinaisonMaxZone() {
  const nombre = (cle, repli) => { const v = lireNombre(moteur(cle)); return Number.isFinite(v) ? v : repli; };
  const butee = Math.max(Math.abs(nombre('b_min', -90)), Math.abs(nombre('b_max', 90)));
  return Math.max(5, Math.min(90, butee, nombre('b_inclinaison_max', 90)));
}

function pointer(actif) {
  etat.pointage = actif;
  $('canvas3d').style.cursor = actif ? 'crosshair' : '';
  vue3d.surPointage = actif ? (point) => {
    if (!point) { dire('Clique sur la pièce elle-même : c\'est là que la zone commencera.', { info: true }); return; }
    pointer(false);
    // D'office, la zone penche vers l'extérieur de la pièce : du centre vers le point cliqué
    // (c'est le sens d'une branche). Tout près du centre : vers la droite.
    const boite = vue3d.boite;
    const dx = point[0] - (boite ? (boite.min.x + boite.max.x) / 2 : 0), dy = point[1] - (boite ? (boite.min.y + boite.max.y) / 2 : 0);
    const azimut = Math.hypot(dx, dy) > 1 ? Math.round((Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360) : 0;
    ajouterZone(point, 30, azimut);
  } : null;
}

function ajouterZone(point, inclinaison = 30, azimut = 0) {
  etat.zones.push({ point: point.map((v) => Math.round(v * 10) / 10), inclinaison: Math.min(inclinaison, inclinaisonMaxZone()), azimut });
  etat.zoneChoisie = etat.zones.length - 1;
  zonesChangees();
}

// Tout le haut du modèle en diagonale : une zone au milieu de la pièce, à 45° (ou moins si la machine ne peut pas).
function zoneDiagonale() {
  const boite = vue3d.boite;
  if (!boite) return;
  ajouterZone([(boite.min.x + boite.max.x) / 2, (boite.min.y + boite.max.y) / 2, boite.min.z + 0.5 * (boite.max.z - boite.min.z)], 45, 0);
}

let minuterieZones = 0;
function zonesChangees() {
  vue3d.afficherZones(etat.zones, etat.zoneChoisie);
  perimer();
  afficherStrategies();
  noterBientot();
  clearTimeout(minuterieZones);
  if (!etat.zones.length) { etat.apercuZones = null; return; }
  // Le slicer dit ce que donnent ces zones (sans trancher), un instant après le dernier réglage.
  minuterieZones = setTimeout(async () => {
    const zones = structuredClone(etat.zones);
    const reponse = await appeler('apercu_zones', { zones });
    if (JSON.stringify(zones) !== JSON.stringify(etat.zones)) return;   // réglé de nouveau entre-temps
    if (!reponse.ok) etat.apercuZones = { texte: reponse.erreur || 'Ces zones ne peuvent pas être imprimées.', alerte: true };
    else {
      const parties = reponse.strategie.parties;
      etat.apercuZones = { alerte: false, texte: `${parties.length} parties : le bas à plat (${fr(parties[0].volume_cm3, 1)} cm³), puis ` +
        parties.slice(1).map((p) => `${fr(p.volume_cm3, 1)} cm³ à ${fr(p.inclinaison, 0)}°`).join(', ') +
        `. Reste à soutenir : ${entier(reponse.strategie.surplomb_mm2)} mm².` +
        (reponse.strategie.remarques.length ? ' ' + reponse.strategie.remarques.join(' ') : '') };
    }
    afficherOndulation();
  }, 350);
}

function oublierZones() {
  if (etat.pointage) pointer(false);
  etat.zones = [];
  etat.zoneChoisie = -1;
  etat.apercuZones = null;
  vue3d.afficherZones([]);
}

// Une ligne de réglage d'une zone : un curseur et un champ, qui disent la même valeur.
function reglageZone(libelle, id, valeur, min, max, unite, surChange) {
  const ligne = el('div', 'reglage');
  const curseur = el('input');
  curseur.type = 'range'; curseur.min = min; curseur.max = max; curseur.step = 1; curseur.value = valeur;
  curseur.id = id; curseur.setAttribute('aria-label', `${libelle} (${unite})`);
  const champ = el('input', 'champ');
  champ.type = 'number'; champ.min = min; champ.max = max; champ.step = 1; champ.value = valeur;
  champ.id = `${id}-champ`; champ.setAttribute('aria-label', `${libelle} (${unite})`);
  const poser = (texte, final) => {
    const v = Math.min(max, Math.max(min, Number(texte)));
    if (!Number.isFinite(v)) return;
    curseur.value = v;
    if (final || document.activeElement !== champ) champ.value = v;
    surChange(v);
  };
  curseur.addEventListener('input', () => { champ.value = curseur.value; poser(curseur.value, false); });
  champ.addEventListener('change', () => poser(champ.value, true));
  ligne.append(el('span', '', libelle), curseur, champ);
  return ligne;
}

function afficherOndulation() {
  const panneau = $('panneau-ondulation');
  const actif = Boolean(etat.modele) && etat.strategie === 'couches_courbes';
  panneau.hidden = !actif;
  if (!actif) {
    if (etat.pointage) pointer(false);
    vue3d.afficherZones([]);
    return;
  }
  // Pendant qu'on fait glisser un curseur, le panneau n'est pas reconstruit (le curseur serait lâché).
  if (panneau.contains(document.activeElement) && document.activeElement.type === 'range') {
    const note = $('zones-apercu');
    if (note && etat.apercuZones) { note.textContent = etat.apercuZones.texte; note.classList.toggle('alerte', etat.apercuZones.alerte); }
    return;
  }
  const contenu = [el('h3', '', 'Ondulation · 5 axes')];
  const strategie = etat.strategies.find((st) => st.type === 'couches_courbes');
  const max = inclinaisonMaxZone();

  // 1. Les zones orientées, choisies à la main.
  contenu.push(el('p', 'aide', 'Zones orientées : clique sur la pièce pour dire « à partir d\'ici, imprime dans une autre direction ». ' +
    'Le bas est imprimé à plat, puis chaque zone la tête inclinée et le plateau tourné.'));
  const rangee = el('div', 'rangee');
  const ajouter = el('button', 'bouton fin', etat.pointage ? 'Clique sur la pièce… (annuler)' : 'Ajouter une zone en cliquant');
  ajouter.id = 'zone-ajouter';
  ajouter.setAttribute('aria-pressed', String(etat.pointage));
  ajouter.addEventListener('click', () => { pointer(!etat.pointage); afficherOndulation(); });
  const diagonale = el('button', 'bouton fin', 'Le haut en diagonale');
  diagonale.id = 'zone-diagonale';
  diagonale.title = 'Ajoute une zone à mi-hauteur de la pièce, inclinée de 45° : à régler ensuite.';
  diagonale.addEventListener('click', zoneDiagonale);
  rangee.append(ajouter, diagonale);
  contenu.push(rangee);

  etat.zones.forEach((zone, i) => {
    const carte = el('div', 'zone-orientee' + (i === etat.zoneChoisie ? ' choisie' : ''));
    carte.id = `zone-${i + 1}`;
    const tete = el('header');
    const retirer = el('button', 'bouton-icone');
    retirer.id = `zone-${i + 1}-retirer`;
    retirer.title = 'Retirer cette zone';
    retirer.setAttribute('aria-label', `Retirer la zone ${i + 1}`);
    retirer.append(icone('supprimer'));
    retirer.addEventListener('click', () => { etat.zones.splice(i, 1); etat.zoneChoisie = -1; zonesChangees(); });
    tete.append(el('span', '', `Zone ${i + 1}`), retirer);
    const choisir = () => { if (etat.zoneChoisie !== i) { etat.zoneChoisie = i; vue3d.afficherZones(etat.zones, i); } };
    carte.addEventListener('pointerdown', choisir);
    carte.addEventListener('focusin', choisir);
    const hauteurMax = Math.max(1, Math.floor(vue3d.boite ? vue3d.boite.max.z : 260));
    carte.append(tete,
      reglageZone('Inclinaison', `zone-${i + 1}-inclinaison`, zone.inclinaison, 0, max, 'degrés', (v) => { zone.inclinaison = v; zonesChangees(); }),
      reglageZone('Direction', `zone-${i + 1}-azimut`, zone.azimut, 0, 359, 'degrés', (v) => { zone.azimut = v; zonesChangees(); }),
      reglageZone('Hauteur', `zone-${i + 1}-hauteur`, Math.round(zone.point[2]), 0, hauteurMax, 'mm', (v) => { zone.point[2] = v; zonesChangees(); }));
    contenu.push(carte);
  });
  if (etat.zones.length) {
    const note = el('p', 'aide' + (etat.apercuZones?.alerte ? ' alerte' : ''), etat.apercuZones?.texte ?? 'Calcul des parties…');
    note.id = 'zones-apercu';
    contenu.push(note, el('p', 'aide', `Inclinaison : 0° = tête verticale, ${fr(max, 0)}° au plus sur cette machine. Direction : le côté vers lequel ` +
      'les couches penchent (0° vers la droite du plateau, 90° vers le fond). Le disque dans la vue montre le plan de coupe, la flèche la direction d\'impression.' +
      (simulation ? ' (Simulation : le tranchage affiché est enregistré, il ne tient pas compte des zones.)' : '')));
  } else if (etat.calculStrategies) {
    contenu.push(el('p', 'aide', 'Recherche des parties que le slicer sait incliner tout seul…'));
  } else if (!strategie || strategie.parties.length < 2) {
    // 2. Pas de zone, et rien que le slicer sache incliner tout seul.
    contenu.push(el('p', 'aide', 'Sans zone, ce modèle sera imprimé comme en classique : le slicer n\'y trouve aucune partie à imprimer la tête inclinée.'));
  } else {
    // 3. Pas de zone : les parties trouvées par le slicer, à régler une par une.
    contenu.push(el('p', 'aide', `Sans zone, le slicer imprime tout seul ${strategie.parties.length - 1} partie(s) dans une autre direction. ` +
      'Choisis pour chacune la façon de l\'imprimer :'));
    strategie.parties.forEach((partie, i) => {
      if (i === 0) return;
      const etiquette = el('label', '', `Partie ${i + 1} · ${fr(partie.volume_cm3, 1)} cm³ · inclinée de ${fr(partie.inclinaison, 0)}°` +
        (partie.parent > 0 ? ` · portée par la partie ${partie.parent + 1}` : ''));
      const choix = el('select', 'champ');
      choix.id = `branche-${i + 1}`;
      for (const [valeur, texte] of CHOIX_BRANCHE) {
        choix.append(new Option(texte, valeur, false, (etat.branches[i + 1] || 'courbes') === valeur));
      }
      choix.addEventListener('change', () => {
        if (choix.value === 'courbes') delete etat.branches[i + 1]; else etat.branches[i + 1] = choix.value;
        perimer();
        noter();
      });
      etiquette.append(choix);
      contenu.push(etiquette);
    });
    if (simulation) contenu.push(el('p', 'aide', '(Simulation : le tranchage affiché est enregistré, il ne tient pas compte de ces choix.)'));
  }
  if (etat.zones.length && strategie && strategie.parties.length > 1) {
    contenu.push(el('p', 'aide', 'Avec des zones, les parties que le slicer trouve tout seul ne sont pas utilisées : seules tes zones comptent.'));
  }
  panneau.replaceChildren(...contenu);
}

function montrerRemarques() {
  const liste = el('div', 'options-liste');
  liste.append(...etat.remarques.map((texte) => el('p', 'aide', texte)));
  ouvrirFeuille('Messages du tranchage', liste, { simple: true });
}

// Vitesse d'un type de trajectoire, en mm/s, pour estimer la part du temps.
function vitesseDuType(type, premiereCouche) {
  if (type === 5) return etat.valeurs.vitesse_a_vide;
  if (premiereCouche) return etat.valeurs.vitesse_premiere_couche;
  if (type === 2) return etat.valeurs.vitesse_paroi_externe;
  if (type === 3 || type === 4) return etat.valeurs.vitesse_remplissage;
  return etat.valeurs.vitesse_paroi_interne;
}

function afficherTrajectoires(resultat) {
  const longueurs = [0, 0, 0, 0, 0, 0];
  const temps = [0, 0, 0, 0, 0, 0];
  resultat.apercu.forEach((couche, indice) => {
    const m = couche.m;
    for (let i = 0; i < m.length; i += 7) {
      const longueur = Math.hypot(m[i + 3] - m[i], m[i + 4] - m[i + 1], m[i + 5] - m[i + 2]);
      longueurs[m[i + 6]] += longueur;
      temps[m[i + 6]] += longueur / vitesseDuType(m[i + 6], indice === 0);
    }
  });
  const total = temps.reduce((a, b) => a + b, 0) || 1;
  $('tableau-types').replaceChildren(...resultat.types.map((nom, type) => {
    const ligne = el('tr');
    const coche = el('td');
    const boite = el('input');
    boite.type = 'checkbox';
    boite.id = 'type-' + type;
    boite.checked = vue3d.typesVisibles.has(type);
    boite.setAttribute('aria-label', `Afficher : ${nom}`);
    boite.addEventListener('change', () => vue3d.montrerType(type, boite.checked));
    coche.append(boite);
    const libelle = el('td');
    const trait = el('span', 'trait-couleur');
    trait.style.background = couleursTrajectoires()[type];
    const texte = el('label', '', nom.charAt(0).toUpperCase() + nom.slice(1));
    texte.htmlFor = boite.id;
    libelle.append(trait, texte);
    const part = el('td', 'nombre', `${fr(100 * temps[type] / total, 1)} %`);
    const barre = el('span', 'part');
    const niveau = el('span');
    niveau.style.width = `${100 * temps[type] / total}%`;
    barre.append(niveau);
    part.append(barre);
    ligne.append(coche, libelle, el('td', 'nombre', `${fr(longueurs[type] / 1000, 2)} m`), part);
    return ligne;
  }));
  const pied = el('tr');
  pied.append(el('td'), el('td', '', 'Total'), el('td', 'nombre', `${fr(longueurs.reduce((a, b) => a + b, 0) / 1000, 2)} m`),
    el('td', 'nombre', duree(total)));
  $('total-types').replaceChildren(pied);
  $('trajectoires-resume').textContent = `${fr(longueurs.reduce((a, b) => a + b, 0) / 1000, 2)} m · ${duree(total)}`;
}

function deplierTrajectoires(ouvert) {
  $('trajectoires').classList.toggle('replie', !ouvert);
  $('trajectoires-corps').hidden = !ouvert;
  $('btn-trajectoires').setAttribute('aria-expanded', String(ouvert));
}

async function trancher() {
  if (!etat.modele) return;
  if (etat.page !== 'preparer' && etat.page !== 'apercu') choisirVue('preparer');   // pour voir l'attente
  const demande = { strategie: etat.strategie };
  if (etat.strategie === 'couches_courbes') {
    if (etat.pointage) pointer(false);
    if (etat.zones.length) demande.zones = etat.zones;
    const branches = etat.zones.length ? [] : Object.entries(etat.branches).map(([partie, choix]) => (
      choix === 'a_plat' ? { partie: Number(partie), mode: 'a_plat' }
        : { partie: Number(partie), mode: 'courbes', inclinaison_max: choix === 'incline60' ? 60 : 45 }));
    if (branches.length) demande.branches = branches;
  }
  await fileMoteur;
  occuper('Tranchage et vérification en cours…');
  definirEtat('', 'Tranchage en cours', 'Calcul des trajectoires et vérification.');
  $('barre-progression').removeAttribute('value');   // barre animée : durée inconnue d'avance
  await new Promise((suite) => setTimeout(suite, 30));
  const reponse = await demander('trancher', demande);
  liberer();
  if (!reponse) { definirEtat('mauvais', 'Tranchage impossible', 'Voir le message en haut de la vue.'); return; }

  const resultat = reponse.resultat;
  etat.resultat = resultat;
  const verification = resultat.verification;
  const b = bobineMontee();
  const diametre = b?.diametre ?? 1.75;
  const grammes = resultat.filament_m * 1000 * Math.PI * (diametre / 2) ** 2 * (b?.densite ?? 1.24) / 1000;
  etat.grammes = grammes;
  $('r-couches').textContent = entier(resultat.couches);
  $('r-filament').textContent = `${fr(resultat.filament_m, 2)} m`;
  $('r-poids').textContent = `${fr(grammes, grammes < 10 ? 1 : 0)} g`;
  $('r-cout').textContent = b ? `${fr(grammes * b.prix_kg / 1000, 2)} €` : '';
  $('r-duree').textContent = duree(resultat.duree_s);
  // La préparation (chauffe, palpage du plateau) s'ajoute au temps d'impression.
  const preparation = resultat.preparation_s ?? 300;
  $('r-preparation').textContent = duree(preparation);
  $('r-total').textContent = duree(preparation + resultat.duree_s);
  const comparaison = $('r-comparaison');
  comparaison.hidden = resultat.duree_classique_s === undefined;
  if (resultat.duree_classique_s !== undefined) {
    const ecart = Math.round((resultat.duree_s - resultat.duree_classique_s) / 60);
    comparaison.textContent = ecart === 0
      ? `Ondulation : même durée qu'en classique (${duree(resultat.duree_classique_s)}).`
      : `Ondulation : ${Math.abs(ecart)} min de ${ecart > 0 ? 'plus' : 'moins'} qu'en classique (${duree(resultat.duree_classique_s)}).`;
  }
  etat.remarques = [...(resultat.remarques ?? [])];
  if (!comparaison.hidden) etat.remarques.unshift(comparaison.textContent);
  $('btn-messages').hidden = etat.remarques.length === 0;
  $('messages-compte').textContent = `${etat.remarques.length} message${etat.remarques.length > 1 ? 's' : ''}`;
  $('etat-resultat').hidden = false;
  const parties = new Set(resultat.apercu.map((couche) => couche.partie ?? 0)).size;
  $('trajectoires-note').textContent = `Couches de ${ecrire(etat.valeurs.hauteur_couche)} mm · ` +
    (parties > 1 ? `${parties} parties, chacune dans sa direction · ` : '') +
    `${entier(verification.deplacements_a_vide)} déplacements à vide · ${entier(verification.retractions)} rétractions` +
    (verification.dans_le_vide_mm === undefined ? '' : ` · parois imprimées dans le vide : ${entier(verification.dans_le_vide_mm)} mm`) +
    (verification.extrusions_en_rotation ? ` · 5 axes continu : la tête pivote pendant ${entier(verification.extrusions_en_rotation)} extrusions` : '');

  if (verification.valide) definirEtat('bon', 'G-code vérifié', 'Tranché. Aucune règle physique violée : prêt à exporter.');
  else definirEtat('mauvais', 'Tranchage refusé', `${entier(verification.nombre_erreurs)} erreur(s) : le G-code ne peut pas être exporté.`);
  $('erreurs').replaceChildren(...verification.erreurs.map((erreur) => {
    const element = el('li');
    const [x, y, z, bb, c] = erreur.position.map((v) => fr(v, 2));
    element.append(el('span', 'code', `Ligne ${erreur.ligne} : ${erreur.texte}`), el('span', '', erreur.probleme),
      el('span', 'chiffres', `X ${x}  Y ${y}  Z ${z}  B ${bb}°  C ${c}°`), el('span', 'solution', `Solution possible : ${erreur.solution}`));
    return element;
  }));
  $('console').hidden = verification.valide;
  $('btn-exporter').disabled = !verification.valide;

  vue3d.afficherApercu(resultat.apercu, etat.valeurs.largeur_extrusion);
  afficherTrajectoires(resultat);
  const curseur = $('curseur-couche');
  curseur.max = resultat.couches;
  curseur.value = resultat.couches;
  $('couche-seule').checked = false;
  $('progression').value = 1000;
  majCouche();
  $('vue-apercu').disabled = false;
  choisirVue('apercu');
  if (b && verification.valide && grammes > b.poids_restant) {
    dire(`La bobine « ${b.nom} » ne suffira pas : il faut ${fr(grammes, 0)} g et il en reste ${entier(b.poids_restant)} g.`);
  }
}

function majCouche() {
  const numero = Number($('curseur-couche').value);
  const couche = etat.resultat?.apercu[numero - 1];
  $('couche-texte').textContent = couche ? `${numero}\n${fr(couche.z, 2)}` : '';
  const fraction = Number($('progression').value) / 1000;
  $('progression-texte').textContent = `${fr(fraction * 100, 0)} %`;
  vue3d.coucheMax = numero;
  vue3d.coucheSeule = $('couche-seule').checked;
  vue3d.montrerProgression(fraction);
}

const PAGES = ['accueil', 'preparer', 'apercu', 'imprimante', 'projet', 'calibration', 'gestionnaire'];

function choisirVue(vue) {
  if (vue === 'apercu' && !etat.resultat) vue = 'preparer';
  etat.page = vue;
  for (const nom of PAGES) {
    $('vue-' + nom).classList.toggle('actif', nom === vue);
    $('vue-' + nom).setAttribute('aria-selected', String(nom === vue));
  }
  // Préparation et Aperçu partagent la même page (colonne des réglages + vue 3D).
  const dansLePlan = vue === 'preparer' || vue === 'apercu';
  $('plan').hidden = !dansLePlan;
  for (const nom of PAGES) {
    if (nom !== 'preparer' && nom !== 'apercu') $('page-' + nom).hidden = nom !== vue;
  }
  if (!dansLePlan) return;
  const apercu = vue === 'apercu';
  for (const id of ['trajectoires', 'curseur-couches', 'curseur-trajet']) $(id).hidden = !apercu;
  $('outils').hidden = apercu;
  $('vue').classList.toggle('apercu', apercu);
  vue3d.montrer(apercu ? 'apercu' : 'preparer');
}

async function exporter() {
  if (!etat.resultat) return;
  const nom = (etat.modele?.nom || 'piece').replace(/\.[^.]+$/, '') + '.gcode';
  const choix = await demander('dialogue_enregistrer', { titre: 'Exporter le G-code', nom_propose: nom, filtres: [['G-code', '*.gcode']] });
  if (!choix || !choix.chemin) return;
  if (!(await demander('enregistrer_gcode', { chemin: choix.chemin }))) return;
  const b = bobineMontee();
  const grammes = Math.round(etat.grammes || 0);
  // Le fichier est noté pour la page Imprimante > Fichiers.
  etat.exports = [{ nom: choix.chemin.split(/[\\/]/).pop(), chemin: choix.chemin, date: new Date().toLocaleString('fr-FR', { dateStyle: 'short', timeStyle: 'short' }),
    duree: duree(etat.resultat.duree_s), grammes, strategie: etat.strategie === 'couches_courbes' ? 'ondulation' : 'classique' },
  ...etat.exports.filter((f) => f.chemin !== choix.chemin)].slice(0, 40);
  memoire.ecrire('exports', etat.exports);
  if (!b || !grammes) { dire(`G-code enregistré : ${choix.chemin}`, { info: true }); return; }
  dire(`G-code enregistré : ${choix.chemin}.`, { info: true, tenir: true, actions: [
    { libelle: `Déduire ${grammes} g de « ${b.nom} »`, action: () => {
      b.poids_restant = Math.max(0, b.poids_restant - grammes);
      enregistrerBobines();
      afficherBobine();
      dire(`Il reste ${entier(b.poids_restant)} g sur « ${b.nom} ».`, { info: true });
    } },
    { libelle: 'Plus tard' }] });
}

// --- Barre d'outils, icônes, thème ------------------------------------------------

// Outils de manipulation, par groupes. Aucun n'est encore branché : un clic
// le dit clairement au lieu de ne rien faire.
const OUTILS = [
  [['ajouter-objet', 'Ajouter un objet'], ['ajouter-plateau', 'Ajouter un plateau']],
  [['orienter', 'Orientation automatique'], ['agencer', 'Agencer les objets'], ['couches-variables', 'Hauteur de couche variable']],
  [['deplacer', 'Déplacer'], ['tourner', 'Tourner'], ['echelle', 'Mettre à l\'échelle'], ['poser-face', 'Poser sur une face']],
  [['couper', 'Couper'], ['booleen', 'Opérations booléennes'], ['assembler', 'Assembler'], ['texte', 'Texte en relief']],
  [['peindre-supports', 'Peindre les supports'], ['peindre-couture', 'Peindre la couture'], ['mesurer', 'Mesurer']],
  [['strategie-5-axes', 'Stratégie de tranchage 5 axes'], ['direction-zone', 'Direction d\'impression par zone'], ['simulation-machine', 'Simulation de la machine']],
];

// Les outils qui font vraiment quelque chose.
const OUTILS_BRANCHES = {
  'ajouter-plateau': outilAjouterPlateau,
  orienter: outilOrienter,
  'poser-face': outilPoserFace,
  deplacer: outilDeplacer,
  tourner: outilTourner,
  mesurer: outilMesurer,
  'simulation-machine': outilSimulation,
  echelle: outilEchelle,
  couper: outilCouper,
  agencer: () => transformer({}, 'L\'objet est recentré sur le plateau. (Un seul objet par plateau pour l\'instant.)'),
  'direction-zone': () => {
    if (!etat.modele) { dire('Ouvre d\'abord un modèle.', { info: true }); return; }
    etat.strategie = etat.strategies.some((st) => st.type === 'couches_courbes') ? 'couches_courbes' : etat.strategie;
    perimer();
    // L'outil met la souris en attente d'un clic sur la pièce : la zone commencera là.
    if (etat.strategie === 'couches_courbes') pointer(true);
    afficherStrategies();
    if (etat.pointage) dire('Clique sur la pièce : à partir de ce point, elle sera imprimée dans une autre direction.', { info: true });
  },
};

function dessinerOutils() {
  const barre = $('outils');
  barre.replaceChildren();
  OUTILS.forEach((groupe, rang) => {
    if (rang) barre.append(el('span', 'separateur'));
    for (const [nom, libelle] of groupe) {
      const bouton = el('button');
      bouton.id = 'outil-' + nom;
      bouton.append(icone(nom, 20));
      if (nom === 'ajouter-objet') {
        bouton.title = 'Ouvrir un modèle…';
        bouton.setAttribute('aria-label', bouton.title);
        bouton.addEventListener('click', ouvrirModele);
      } else if (nom === 'strategie-5-axes') {
        // Mène au choix de la stratégie, dans la section Procédé.
        bouton.title = 'Stratégie d\'impression';
        bouton.setAttribute('aria-label', bouton.title);
        bouton.addEventListener('click', () => {
          if (!etat.modele) { dire('Ouvre d\'abord un modèle : les stratégies dépendent de sa forme.', { info: true }); return; }
          const replier = document.querySelector('.replier[aria-controls="corps-procede"]');
          if (replier.getAttribute('aria-expanded') !== 'true') replier.click();
          if ($('btn-cote').getAttribute('aria-expanded') === 'false') $('btn-cote').click();
          $('bloc-strategie').scrollIntoView({ block: 'start' });
          $('strategie').focus();
          dire('La stratégie se choisit ici, à gauche : « Classique » (3 axes) ou « Ondulation » (5 axes).', { info: true });
        });
      } else if (OUTILS_BRANCHES[nom]) {
        bouton.title = libelle;
        bouton.setAttribute('aria-label', libelle);
        bouton.addEventListener('click', OUTILS_BRANCHES[nom]);
      } else {
        bouton.title = `${libelle} (à venir)`;
        bouton.setAttribute('aria-label', `${libelle} (à venir)`);
        bouton.setAttribute('aria-disabled', 'true');
        bouton.addEventListener('click', () => dire(`« ${libelle} » : cet outil est à venir.`, { info: true }));
      }
      barre.append(bouton);
    }
  });
}

// Pose les icônes demandées par l'attribut data-icone de la page.
function poserIcones() {
  for (const element of document.querySelectorAll('[data-icone]')) {
    element.prepend(icone(element.dataset.icone));
  }
  for (const element of document.querySelectorAll('[data-icone-24]')) {
    element.prepend(icone(element.dataset.icone24, 24));
  }
}

// Thème : celui du système au départ, puis celui choisi avec le bouton.
function themeSombre() {
  const choisi = document.documentElement.dataset.theme;
  return choisi ? choisi === 'dark' : window.matchMedia('(prefers-color-scheme: dark)').matches;
}
function afficherTheme() {
  const sombre = themeSombre();
  $('btn-theme').replaceChildren(icone(sombre ? 'soleil' : 'lune'));
  $('btn-theme').title = sombre ? 'Passer au thème clair' : 'Passer au thème sombre';
  $('btn-theme').setAttribute('aria-label', $('btn-theme').title);
  vue3d.rafraichirTheme();
  if (etat.resultat) {
    const ouvert = !$('trajectoires').classList.contains('replie');
    afficherTrajectoires(etat.resultat);
    deplierTrajectoires(ouvert);
  }
}
function changerTheme() {
  const theme = themeSombre() ? 'light' : 'dark';
  document.documentElement.dataset.theme = theme;
  memoire.ecrire('theme', theme);
  afficherTheme();
}

// --- Menus de la barre du haut ----------------------------------------------------

const aVenir = (nom) => () => dire(`${nom} : à venir.`, { info: true });
function trancherOuDire() {
  if (etat.modele) trancher(); else dire('Ouvre d\'abord un modèle.');
}
function exporterOuDire() {
  if (etat.resultat?.verification.valide) exporter();
  else dire('Tranche d\'abord le modèle : il n\'y a pas encore de G-code vérifié à exporter.');
}
const SORTIES_A_VENIR = [
  { id: 'menu-exporter-stl', libelle: 'Exporter en STL…', action: () => exporterStl() },
  { id: 'menu-exporter-3mf', libelle: 'Exporter en 3MF…', note: 'à venir', action: aVenir('Export en 3MF') },
  { id: 'menu-enregistrer', libelle: 'Enregistrer le projet…', action: () => enregistrerProjet() },
];
function menuFichier() {
  ouvrirMenu($('btn-fichier'), [
    { id: 'menu-nouveau', libelle: 'Nouveau projet', action: nouveauProjet },
    { id: 'menu-ouvrir', libelle: 'Ouvrir un modèle…', action: ouvrirModele },
    { id: 'menu-ouvrir-projet', libelle: 'Ouvrir un projet…', action: ouvrirProjet },
    null,
    { id: 'menu-exporter-gcode', libelle: 'Exporter le G-code…', action: exporterOuDire },
    ...SORTIES_A_VENIR,
    null,
    { id: 'menu-machine', libelle: 'Réglages de la machine…', action: ouvrirMachine },
    { id: 'menu-bobines', libelle: 'Gestionnaire de bobines', action: () => choisirVue('gestionnaire') },
    { id: 'menu-theme', libelle: themeSombre() ? 'Passer au thème clair' : 'Passer au thème sombre', action: changerTheme },
  ]);
}
function menuTrancher() {
  ouvrirMenu($('btn-trancher-menu'), [
    { id: 'menu-trancher-plateau', libelle: 'Trancher le plateau', choisi: true, action: trancherOuDire },
    { id: 'menu-trancher-tous', libelle: 'Trancher tous les plateaux', note: 'à venir', action: aVenir('Plusieurs plateaux') },
  ]);
}
function menuSortie() {
  ouvrirMenu($('btn-sortie-menu'), [
    { id: 'menu-imprimer', libelle: 'Imprimer le plateau', note: 'à venir', action: aVenir('Impression directe sur l\'imprimante') },
    { id: 'menu-imprimer-tous', libelle: 'Imprimer tous les plateaux', note: 'à venir', action: aVenir('Plusieurs plateaux') },
    { id: 'menu-sortie-gcode', libelle: 'Exporter le G-code…', choisi: true, action: exporterOuDire },
    null,
    ...SORTIES_A_VENIR,
  ]);
}

// --- Démarrage -----------------------------------------------------------------

const themeGarde = memoire.lire('theme', null);
if (themeGarde === 'light' || themeGarde === 'dark') document.documentElement.dataset.theme = themeGarde;
poserIcones();
dessinerOutils();
brancherChoixPrevus();
afficherVignettes();
afficherTheme();
window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', afficherTheme);
$('btn-theme').addEventListener('click', changerTheme);
$('btn-trajectoires').addEventListener('click', () => deplierTrajectoires($('trajectoires').classList.contains('replie')));
$('btn-recherche').addEventListener('click', () => {
  const ouvrir = $('recherche').hidden;
  $('recherche').hidden = !ouvrir;
  $('btn-recherche').setAttribute('aria-expanded', String(ouvrir));
  if (ouvrir) { $('recherche').focus(); return; }
  $('recherche').value = '';
  etat.recherche = '';
  afficherProcede();
});

etat.exports = memoire.lire('exports', []);
pages = brancherPages({ etat, dire, moteur, lireNombre, bobineMontee, monterBobine, ouvrirBibliotheque, nouvelleBobine,
  enregistrerBobines, afficherBobine, ouvrirMachine,
  exports: () => etat.exports,
  retirerExport: (rang) => { etat.exports.splice(rang, 1); memoire.ecrire('exports', etat.exports); },
  version: () => $('version').textContent || '?',
  profilMachine: () => PROFIL_MACHINE.version,
  libelle: (cle) => etat.moteur.find((r) => r.cle === cle)?.libelle ?? cle,
  allerAuxParametres: () => { choisirVue('preparer'); dire('La rétraction est dans les paramètres du procédé, onglet « Autres ».', { info: true }); } });
$('btn-fichier').addEventListener('click', menuFichier);
$('btn-trancher-menu').addEventListener('click', menuTrancher);
$('btn-sortie-menu').addEventListener('click', menuSortie);
$('accueil-ouvrir').addEventListener('click', ouvrirModele);
$('btn-enregistrer').addEventListener('click', enregistrerProjet);
$('btn-annuler').addEventListener('click', annuler);
$('btn-refaire').addEventListener('click', refaire);
boutonsHistorique();
// Ctrl+Z annule, Ctrl+Y (ou Ctrl+Maj+Z) refait, Ctrl+S enregistre le projet — sauf pendant qu'on tape dans un champ.
document.addEventListener('keydown', (e) => {
  if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
  const touche = e.key.toLowerCase();
  if (touche === 's') { e.preventDefault(); enregistrerProjet(); return; }
  if (/^(input|textarea|select)$/i.test(document.activeElement?.tagName ?? '')) return;
  if (touche === 'z' && !e.shiftKey) { e.preventDefault(); annuler(); }
  else if (touche === 'y' || (touche === 'z' && e.shiftKey)) { e.preventDefault(); refaire(); }
});
for (const nom of ['accueil', 'imprimante', 'projet', 'calibration', 'gestionnaire']) {
  $('vue-' + nom).addEventListener('click', () => choisirVue(nom));
}
$('btn-ouvrir-2').addEventListener('click', ouvrirModele);
$('btn-trancher').addEventListener('click', trancher);
$('btn-messages').addEventListener('click', montrerRemarques);
// La pièce s'attrape à la souris : un clic la sélectionne, on la fait glisser sur le plateau.
vue3d.surSelection = (choisie) => {
  if (choisie) dire('Pièce sélectionnée : fais-la glisser pour la déplacer, ou choisis un outil dans la barre.', { info: true });
};
vue3d.surGlisser = (dx, dy) => {
  transformer({ decalage: [Math.round((etat.decalage[0] + dx) * 10) / 10, Math.round((etat.decalage[1] + dy) * 10) / 10] },
    'Pièce déplacée.').then(() => { if (!$('panneau-outil').hidden && $('deplacer-x')) outilDeplacer(); });
};
// Dans le logiciel installé, ce n'est pas une simulation : le bandeau disparaît.
if (!simulation) {
  document.querySelector('.note-simulation')?.remove();
  document.title = 'ArtPrint Slicer';
}
$('btn-exporter').addEventListener('click', exporter);
$('btn-machine').addEventListener('click', ouvrirMachine);
$('btn-bibliotheque').addEventListener('click', () => ouvrirBibliotheque());
$('btn-nouvelle-bobine').addEventListener('click', () => ouvrirBibliotheque(nouvelleBobine()));
$('btn-synchro').addEventListener('click', () => dire('Synchronisation avec l\'imprimante (Klipper) : à venir.', { info: true }));
$('btn-cote').addEventListener('click', () => {
  const montrer = $('cote').hidden;
  $('cote').hidden = !montrer;
  $('btn-cote').setAttribute('aria-expanded', String(montrer));
  $('btn-cote').title = montrer ? 'Masquer la colonne des réglages' : 'Afficher la colonne des réglages';
  $('btn-cote').setAttribute('aria-label', $('btn-cote').title);
});
// La flèche d'un bandeau replie ou déplie sa section. Tout est ouvert au démarrage.
for (const bouton of document.querySelectorAll('.replier')) {
  bouton.addEventListener('click', () => {
    const ouvrir = bouton.getAttribute('aria-expanded') !== 'true';
    bouton.setAttribute('aria-expanded', String(ouvrir));
    bouton.title = ouvrir ? 'Replier' : 'Déplier';
    $(bouton.getAttribute('aria-controls')).hidden = !ouvrir;
    bouton.closest('.section').classList.toggle('repliee', !ouvrir);
  });
}
$('btn-retablir-tout').addEventListener('click', () => choisirProfil(etat.profil));
$('profil-procede').addEventListener('change', (e) => choisirProfil(e.target.value));
$('strategie').addEventListener('change', (e) => { etat.strategie = e.target.value; afficherStrategies(); perimer(); noter(); });
// La pastille « 3 axes | 5 axes » choisit la stratégie correspondante.
function choisirMode(cinqAxes) {
  if (!etat.modele) { dire('Ouvre d\'abord un modèle : le 5 axes dépend de sa forme.', { info: true }); return; }
  if (cinqAxes && !etat.calculStrategies && !etat.strategies.some((s) => s.type === 'couches_courbes')) {
    dire('L\'ondulation n\'est pas proposée pour ce modèle : il reste en 3 axes.', { info: true });
    return;
  }
  const voulue = cinqAxes ? 'couches_courbes' : 'classique';
  if (cinqAxes === STRATEGIES_5_AXES.includes(etat.strategie)) return;
  etat.strategie = voulue;
  afficherStrategies();
  perimer();
  noter();
}
$('mode-3axes').addEventListener('click', () => choisirMode(false));
$('mode-5axes').addEventListener('click', () => choisirMode(true));
$('mode-avance').addEventListener('change', (e) => { etat.avance = e.target.checked; afficherProcede(); });
$('recherche').addEventListener('input', (e) => { etat.recherche = e.target.value; afficherProcede(); });
$('vue-preparer').addEventListener('click', () => choisirVue('preparer'));
$('vue-apercu').addEventListener('click', () => choisirVue('apercu'));
$('curseur-couche').addEventListener('input', () => { $('progression').value = 1000; majCouche(); });
$('couche-seule').addEventListener('change', majCouche);
$('progression').addEventListener('input', majCouche);
$('btn-fermer-erreurs').addEventListener('click', () => { $('console').hidden = true; });
for (const nom of ['iso', 'dessus', 'face', 'droite', 'cadrer']) {
  $('cam-' + nom).addEventListener('click', () => vue3d.pointDeVue(nom));
}

(async () => {
  for (const p of PARAMETRES) etat.valeurs[p.cle] = valeurProfil(p);
  const infos = await demander('infos');
  if (infos) $('version').textContent = infos.version;
  let reponse = await demander('reglages');
  if (reponse) etat.moteur = reponse.reglages;
  // Les mesures de la machine (profil « ArtPrint 5 axes ») sont posées une
  // fois, si elles ne sont pas déjà renseignées ; elles restent modifiables.
  const manquants = Object.fromEntries(Object.entries(PROFIL_MACHINE.reglages)
    .filter(([cle]) => memoire.lire('profil-machine', '') !== PROFIL_MACHINE.version && etat.moteur.some((r) => r.cle === cle)));
  if (Object.keys(manquants).length) {
    reponse = await demander('definir_reglages', { valeurs: manquants });
    if (reponse) etat.moteur = reponse.reglages;
    memoire.ecrire('profil-machine', PROFIL_MACHINE.version);
  }
  await synchroniser(PARAMETRES);
  await monterBobine(etat.montee, true);
  afficherImprimante();
  afficherProcede();
  perimer();
})();
