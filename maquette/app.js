// ArtPrint Slicer - licence AGPL v3
// Maquette de l'interface : sections Imprimante, Filament et Procédé, barre
// d'outils, aperçu avec trajectoires repliables, ligne d'état.
import { appeler } from './pont.js';
import { Vue3D, couleursTrajectoires } from './vue3d.js';
import { icone } from './icones.js';
import { ONGLETS, PARAMETRES, PROFILS, MATIERES, BOBINES_EXEMPLE } from './catalogue.js';

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
  const hauteur = lireNombre(moteur('hauteur_impression')) || 281;
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
  ['Tête inclinable (axe B)', ['distance_pivot_buse', 'axe_pivot_b', 'sens_b', 'b_min', 'b_max', 'b_limite_pres_plateau', 'hauteur_bascule_b']],
  ['Plateau rotatif (axe C)', ['sens_c', 'c_min', 'c_max']],
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

// --- Relevés, modèle, tranchage --------------------------------------------------

const ICONES_ETAT = { bon: 'ok', mauvais: 'erreur', perime: 'attention' };
function definirEtat(classe, titre, detail) {
  $('releve-etat').className = 'etat-verification ' + classe;
  $('etat-icone').replaceChildren(icone(ICONES_ETAT[classe] || 'info'));
  $('r-etat').textContent = titre;
  $('r-etat-detail').textContent = detail;
  $('r-etat-detail').title = detail;
}

function viderReleves() {
  $('etat-resultat').hidden = true;
  $('console').hidden = true;
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
  else definirEtat('', 'Prêt', 'Clique sur « Trancher ».');
  choisirVue('preparer');
}

async function ouvrirModele() {
  const choix = await demander('dialogue_ouvrir', { titre: 'Ouvrir un modèle',
    filtres: [['Modèles STL', '*.stl'], ['Tous les fichiers', '*.*']] });
  if (!choix || !choix.chemin) return;
  occuper('Lecture du modèle…');
  const reponse = await demander('charger_modele', { chemin: choix.chemin });
  liberer();
  if (!reponse) return;
  etat.modele = reponse.modele;
  etat.resultat = null;
  etat.modele.nom = choix.chemin.split(/[\\/]/).pop();
  vue3d.afficherModele(reponse.modele.positions);
  vue3d.rafraichirTheme();
  $('accueil').hidden = true;
  const [x, y, z] = etat.modele.dimensions;
  $('r-modele').textContent = etat.modele.nom;
  $('r-dimensions').textContent = `${fr(x)} × ${fr(y)} × ${fr(z)} mm · ${entier(etat.modele.triangles)} triangles`;
  if (!etat.modele.ferme) dire(etat.modele.message_fermeture);
  $('btn-trancher').disabled = false;
  perimer();
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
  occuper('Tranchage et vérification en cours…');
  await new Promise((suite) => setTimeout(suite, 30));
  const reponse = await demander('trancher');
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
  $('etat-resultat').hidden = false;
  $('trajectoires-note').textContent = `Couches de ${ecrire(etat.valeurs.hauteur_couche)} mm · ` +
    `${entier(verification.deplacements_a_vide)} déplacements à vide · ${entier(verification.retractions)} rétractions`;

  if (verification.valide) definirEtat('bon', 'G-code vérifié', 'Aucune règle physique violée. Prêt à exporter.');
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

function choisirVue(vue) {
  const apercu = vue === 'apercu' && Boolean(etat.resultat);
  $('vue-preparer').classList.toggle('actif', !apercu);
  $('vue-apercu').classList.toggle('actif', apercu);
  $('vue-preparer').setAttribute('aria-selected', String(!apercu));
  $('vue-apercu').setAttribute('aria-selected', String(apercu));
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

function dessinerOutils() {
  const barre = $('outils');
  barre.replaceChildren();
  OUTILS.forEach((groupe, rang) => {
    if (rang) barre.append(el('span', 'separateur'));
    for (const [nom, libelle] of groupe) {
      const bouton = el('button');
      bouton.id = 'outil-' + nom;
      bouton.title = `${libelle} (à venir)`;
      bouton.setAttribute('aria-label', `${libelle} (à venir)`);
      bouton.setAttribute('aria-disabled', 'true');
      bouton.append(icone(nom, 20));
      bouton.addEventListener('click', () => dire(`« ${libelle} » : cet outil est à venir.`, { info: true }));
      barre.append(bouton);
    }
  });
}

// Pose les icônes demandées par l'attribut data-icone de la page.
function poserIcones() {
  for (const element of document.querySelectorAll('[data-icone]')) {
    element.prepend(icone(element.dataset.icone));
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

$('btn-ouvrir').addEventListener('click', ouvrirModele);
$('btn-ouvrir-2').addEventListener('click', ouvrirModele);
$('btn-trancher').addEventListener('click', trancher);
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
  const reponse = await demander('reglages');
  if (reponse) etat.moteur = reponse.reglages;
  await synchroniser(PARAMETRES);
  await monterBobine(etat.montee, true);
  afficherImprimante();
  afficherProcede();
  perimer();
})();
