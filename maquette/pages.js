// ArtPrint Slicer - licence AGPL v3
// Maquette : pages Imprimante (caméra, progression, contrôle et déplacements
// 5 axes) et Gestionnaire de bobines. Rien n'est envoyé à une vraie machine :
// la page Imprimante ne fait que changer les valeurs affichées.
import { icone } from './icones.js';

const $ = (id) => document.getElementById(id);
const el = (balise, classe, texte) => {
  const e = document.createElement(balise);
  if (classe) e.className = classe;
  if (texte !== undefined) e.textContent = texte;
  return e;
};
const fr = (valeur, decimales = 1) => Number(valeur).toLocaleString('fr-FR',
  { minimumFractionDigits: decimales, maximumFractionDigits: decimales });
const entier = (valeur) => Number(valeur).toLocaleString('fr-FR');
const sansAccent = (t) => t.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();

// « app » : ce que la page principale prête (état, messages, bobines).
export function brancherPages(app) {
  const imprimante = brancherImprimante(app);
  const gestionnaire = brancherGestionnaire(app);
  return { rafraichir() { imprimante.afficher(); gestionnaire.afficher(); } };
}

// --- Imprimante -----------------------------------------------------------------

// Sur l'écran de la machine, B = -90° quand la buse est verticale. Les limites
// de B des réglages du slicer sont comptées depuis la verticale : on les décale.
const B_VERTICAL = -90;
const AXES = [['x', 'mm'], ['y', 'mm'], ['z', 'mm'], ['b', '°'], ['c', '°']];

function brancherImprimante(app) {
  const machine = { x: 0, y: 0, z: 0, b: B_VERTICAL, c: 0, aZero: false, pasLineaire: 10, pasAngle: 10 };

  const reglage = (cle, defaut) => {
    const valeur = app.lireNombre(app.moteur(cle));
    return Number.isFinite(valeur) ? valeur : defaut;
  };
  // Butées de chaque axe, lues dans les réglages de la machine quand ils existent.
  function butees(axe) {
    const rayon = reglage('rayon_plateau', 150);
    if (axe === 'x') return [reglage('course_x_min', -rayon), reglage('course_x_max', rayon)];
    if (axe === 'y') return [reglage('course_y_min', -rayon), reglage('course_y_max', rayon)];
    if (axe === 'z') return [reglage('course_z_min', 0), reglage('course_z_max', reglage('hauteur_impression', 260))];
    if (axe === 'b') return [B_VERTICAL + reglage('b_min', -90), B_VERTICAL + reglage('b_max', 90)];
    return [-Infinity, Infinity];
  }

  function afficher() {
    $('positions').replaceChildren(...AXES.map(([axe, unite]) => {
      const puce = el('span', 'position');
      puce.id = 'position-' + axe;
      puce.append(el('b', '', axe.toUpperCase()), el('span', '', `${fr(machine[axe])}${unite === '°' ? '°' : ' mm'}`));
      if (!machine.aZero) {
        const alerte = icone('attention');
        puce.title = 'Axe pas encore mis à zéro';
        puce.append(alerte);
      }
      return puce;
    }));
    $('dep-alerte').hidden = machine.aZero;
    for (const [id, pas, unite, cle] of [['pas-lineaire', [1, 10, 50], ' mm', 'pasLineaire'], ['pas-angle', [1, 10, 45], '°', 'pasAngle']]) {
      $(id).replaceChildren(...pas.map((valeur) => {
        const bouton = el('button', machine[cle] === valeur ? 'actif' : '', `${valeur}${unite}`);
        bouton.setAttribute('aria-pressed', String(machine[cle] === valeur));
        bouton.addEventListener('click', () => { machine[cle] = valeur; afficher(); });
        return bouton;
      }));
    }
    $('pas-z').textContent = `Pas de ${machine.pasLineaire} mm`;

    const b = app.bobineMontee();
    const mesure = (nom, dessin, valeur, detail) => {
      const bloc = el('div', 'temperature');
      bloc.append(icone(dessin, 20), el('small', '', nom), el('b', '', valeur));
      bloc.title = detail;
      return bloc;
    };
    $('temperatures').replaceChildren(
      mesure('Buse', 'buse', b ? `– / ${b.temp_buse} °C` : '– °C', 'Température mesurée / consigne de la bobine montée'),
      mesure('Plateau', 'plateau', b ? `– / ${b.temp_plateau} °C` : '– °C', 'Température mesurée / consigne de la bobine montée'),
      mesure('Ventilateur', 'ventilateur', '– %', 'Ventilateur de pièce'));
  }

  // Déplace un axe d'un pas, sans dépasser ses butées.
  function bouger(axe, sens) {
    if (!machine.aZero) {
      app.dire('Mets d\'abord les axes à zéro avec « Tout mettre à zéro ».');
      return;
    }
    const pas = axe === 'b' || axe === 'c' ? machine.pasAngle : machine.pasLineaire;
    const [mini, maxi] = butees(axe);
    let cible = machine[axe] + sens * pas;
    if (axe === 'c') cible = ((cible + 180) % 360 + 360) % 360 - 180;   // le plateau tourne sans fin
    const bornee = Math.min(maxi, Math.max(mini, cible));
    if (bornee !== cible) {
      app.dire(`Butée de l'axe ${axe.toUpperCase()} : ${fr(bornee)} ${axe === 'b' ? '°' : 'mm'}.`, { info: true });
    }
    machine[axe] = bornee;
    afficher();
  }
  function aller(valeurs, message) {
    if (!machine.aZero) {
      app.dire('Mets d\'abord les axes à zéro avec « Tout mettre à zéro ».');
      return;
    }
    for (const [axe, valeur] of Object.entries(valeurs)) {
      const [mini, maxi] = butees(axe);
      machine[axe] = Math.min(maxi, Math.max(mini, valeur));
    }
    afficher();
    app.dire(message, { info: true });
  }

  for (const touche of document.querySelectorAll('.touche[data-axe]')) {
    touche.addEventListener('click', () => bouger(touche.dataset.axe, Number(touche.dataset.sens)));
  }
  $('dep-zero').addEventListener('click', () => {
    Object.assign(machine, { x: 0, y: 0, z: 0, b: B_VERTICAL, c: 0, aZero: true });
    afficher();
    app.dire('Simulation : tous les axes sont à zéro.', { info: true });
  });
  $('dep-origine-xy').addEventListener('click', () => aller({ x: 0, y: 0 }, 'Simulation : X et Y à zéro.'));
  $('dep-z150').addEventListener('click', () => aller({ z: 150 }, 'Simulation : Z monté.'));
  $('dep-redresser').addEventListener('click', () => aller({ b: B_VERTICAL }, 'Simulation : tête redressée, buse verticale.'));
  $('dep-plateau0').addEventListener('click', () => aller({ c: 0 }, 'Simulation : plateau à 0°.'));
  $('dep-liberer').addEventListener('click', () => {
    machine.aZero = false;
    afficher();
    app.dire('Simulation : moteurs libérés. Les axes devront être remis à zéro.', { info: true });
  });

  // Caméra, menu de gauche et options : à venir, dit clairement.
  $('btn-camera').append(icone('lecture', 24));
  for (const id of ['btn-camera', 'btn-camera-2', 'btn-camera-plein', 'btn-camera-reglages']) {
    $(id).addEventListener('click', () => app.dire('Caméra : à venir. Aucune caméra n\'est encore reliée au slicer.', { info: true }));
  }
  // Menu de gauche : État, Fichiers, Mise à jour.
  const SOUS_PAGES = { 'menu-etat': null, 'menu-fichiers': 'imprimante-fichiers', 'menu-maj': 'imprimante-maj' };
  function choisirSousPage(menu) {
    for (const [id, section] of Object.entries(SOUS_PAGES)) {
      $(id).classList.toggle('actif', id === menu);
      if (section) $(section).hidden = id !== menu;
    }
    const etat = menu === 'menu-etat';
    document.querySelector('#page-imprimante .camera-zone').hidden = !etat;
    document.querySelector('#page-imprimante .controle').hidden = !etat;
    if (menu === 'menu-fichiers') afficherFichiers();
    if (menu === 'menu-maj') afficherMiseAJour();
  }
  for (const id of Object.keys(SOUS_PAGES)) $(id).addEventListener('click', () => choisirSousPage(id));

  // Fichiers : les G-code exportés depuis ce slicer. (Ceux de la mémoire de
  // l'imprimante s'ajouteront quand elle sera reliée.)
  function afficherFichiers() {
    const exports = app.exports();
    $('fichiers-etat').textContent = 'Imprimante non connectée';
    $('fichiers-aide').textContent = exports.length
      ? 'Les G-code exportés depuis ce slicer, du plus récent au plus ancien. L\'envoi à l\'imprimante et la liste de sa mémoire demandent la liaison avec la machine, pas encore branchée.'
      : 'Aucun G-code exporté pour l\'instant. Tranche une pièce puis clique sur « Exporter le G-code » : le fichier apparaîtra ici. La liste de la mémoire de l\'imprimante demande la liaison avec la machine, pas encore branchée.';
    const table = $('fichiers-table');
    table.replaceChildren();
    if (!exports.length) return;
    const tete = el('div', 'fichier tete');
    tete.append(el('span', '', 'Fichier'), el('span', 'secondaire', 'Impression'), el('span', 'secondaire', 'Exporté le'), el('span', '', ''));
    table.append(tete);
    exports.forEach((f, rang) => {
      const ligne = el('div', 'fichier');
      ligne.id = `fichier-${rang + 1}`;
      const nom = el('span');
      nom.append(el('b', '', f.nom), el('small', '', f.chemin));
      nom.title = f.chemin;
      const actions = el('span', 'actions');
      const envoyer = el('button', 'bouton fin', 'Envoyer');
      envoyer.title = 'Envoyer à l\'imprimante (liaison pas encore branchée)';
      envoyer.addEventListener('click', () => app.dire('Envoi à l\'imprimante : la liaison avec la machine n\'est pas encore branchée. Le fichier est sur ton ordinateur, au chemin indiqué.', { info: true }));
      const retirer = el('button', 'bouton-icone');
      retirer.title = 'Retirer de la liste (le fichier n\'est pas supprimé)';
      retirer.setAttribute('aria-label', `Retirer ${f.nom} de la liste`);
      retirer.append(icone('supprimer'));
      retirer.addEventListener('click', () => { app.retirerExport(rang); afficherFichiers(); });
      actions.append(envoyer, retirer);
      ligne.append(nom, el('span', 'secondaire', `${f.duree} · ${f.grammes} g · ${f.strategie}`), el('span', 'secondaire', f.date), actions);
      table.append(ligne);
    });
  }

  // Mise à jour : la version du slicer, et celle de l'imprimante quand elle sera reliée.
  function afficherMiseAJour() {
    const carte = (titre, valeur, detail, libelle, action) => {
      const c = el('div', 'carte-maj');
      c.append(el('b', '', titre), el('span', 'valeur', valeur), el('span', 'aide', detail));
      if (libelle) {
        const bouton = el('button', 'bouton', libelle);
        bouton.addEventListener('click', action);
        c.append(bouton);
      }
      return c;
    };
    $('cartes-maj').replaceChildren(
      carte('ArtPrint Slicer', `Version ${app.version()}`, 'La version installée sur cet ordinateur.', 'Chercher une mise à jour',
        () => app.dire('La recherche de mise à jour en ligne n\'est pas encore branchée. La dernière version est celle que tu as installée à la main.', { info: true })),
      carte('Firmware de l\'imprimante', 'Inconnu', 'Imprimante non connectée : sa version (Klipper) s\'affichera ici.', 'Mettre à jour',
        () => app.dire('Mise à jour du firmware : il faut d\'abord la liaison avec l\'imprimante, pas encore branchée.', { info: true })),
      carte('Profil de la machine', app.profilMachine(), 'Les valeurs d\'usine posées par le slicer (pivot, hauteur, butées de B).', 'Voir les réglages', () => app.ouvrirMachine()));
    $('maj-aide').textContent = 'Rien n\'est téléchargé ni envoyé depuis cette page pour l\'instant.';
  }
  brancherCalibration(app, { butees, machine });
  for (const bouton of document.querySelectorAll('[data-a-venir]')) {
    bouton.addEventListener('click', () => app.dire(`${bouton.dataset.aVenir} : à venir.`, { info: true }));
  }
  const vignette = new Image();
  vignette.alt = '';
  vignette.addEventListener('error', () => $('progression-vignette').replaceChildren(icone('imprimante', 24)));
  vignette.src = 'prive/imprimante.png';
  $('progression-vignette').replaceChildren(vignette);

  return { afficher };
}

// --- Calibration --------------------------------------------------------------------

// Les mesures à faire sur la machine. « cles » : les réglages du slicer où les reporter.
const CALIBRATIONS = [
  { titre: 'Distance pivot – buse', cles: ['distance_pivot_buse'], etapes: [
    'Mets la tête à la verticale.',
    'Mesure la distance entre l\'axe du pivot de la tête et la pointe de la buse.',
    'Reporte-la dans « Distance pivot – buse ».'] },
  { titre: 'Encombrement de la tête', cles: ['tete_demi_largeur', 'tete_hauteur'], etapes: [
    'Mesure la demi-largeur du bloc de la tête, de chaque côté de l\'axe de la buse.',
    'Mesure sa longueur au-delà du pivot, du côté opposé à la buse.',
    'Reporte les deux valeurs : le slicer calcule alors jusqu\'où la tête peut pencher à chaque hauteur.'] },
  { titre: 'Butées de la tête (B)', cles: ['b_min', 'b_max', 'b_inclinaison_max'], etapes: [
    'Incline doucement la tête d\'un côté jusqu\'à sa limite, puis de l\'autre.',
    'Note les deux angles, comptés depuis la verticale.',
    'Reporte-les dans les butées de B, et la limite du firmware dans « Inclinaison maximale ».'] },
  { titre: 'Sens des axes B et C', cles: ['sens_b', 'sens_c', 'axe_pivot_b'], etapes: [
    'Demande un petit mouvement B+ puis C+ (mode développeur).',
    'Regarde de quel côté la tête penche et dans quel sens le plateau tourne.',
    'Si c\'est l\'inverse de ce que montre le slicer, change le sens (1 ou -1).'] },
  { titre: 'Courses des axes X, Y, Z', cles: ['course_x_min', 'course_x_max', 'course_y_min', 'course_y_max', 'course_z_min', 'course_z_max'], etapes: [
    'Après la mise à zéro, déplace chaque axe jusqu\'à ses deux limites.',
    'Note les positions affichées.',
    'Reporte-les : sans elles, le slicer ne vérifie pas les courses.'] },
  { titre: 'Rétraction', cles: ['retraction_longueur'], etapes: [
    'Imprime deux petites tours espacées.',
    'S\'il y a des fils entre elles, augmente la rétraction par pas de 0,2 mm.',
    'Reporte la valeur dans les paramètres (onglet Autres).'] },
];

function brancherCalibration(app, { butees, machine }) {
  function afficherCartes() {
    $('calibration-cartes').replaceChildren(...CALIBRATIONS.map((c) => {
      const carte = el('div', 'carte-calibration');
      const liste = el('ol');
      for (const etape of c.etapes) liste.append(el('li', '', etape));
      const valeurs = c.cles.map((cle) => [app.libelle(cle), String(app.moteur(cle) ?? '').trim()]);
      const manque = valeurs.filter(([, v]) => v === '').length;
      const etat = el('span', 'etat-reglage' + (manque ? ' manque' : ''),
        manque === valeurs.length ? 'Pas encore renseigné.'
          : valeurs.map(([nom, v]) => `${nom} : ${v === '' ? 'à mesurer' : v}`).join(' · '));
      const bouton = el('button', 'bouton fin', 'Ouvrir les réglages');
      bouton.addEventListener('click', () => (c.cles[0] === 'retraction_longueur' ? app.allerAuxParametres() : app.ouvrirMachine()));
      carte.append(el('b', '', c.titre), liste, etat, bouton);
      return carte;
    }));
  }

  // Mode développeur : déplacements et commandes G-code. Tant que l'imprimante
  // n'est pas reliée, rien n'est envoyé : le journal montre ce qui partirait.
  const pas = { lineaire: 10, angle: 10 };
  const journal = (classe, texte) => {
    const ligne = el('div', classe, texte);
    $('dev-journal').append(ligne);
    $('dev-journal').scrollTop = $('dev-journal').scrollHeight;
  };
  function envoyer(commande) {
    journal('envoi', `> ${commande}`);
    journal('retour', '  (simulation : imprimante non connectée, rien n\'est envoyé)');
  }
  function afficherAxes() {
    $('dev-etat').textContent = 'Simulation · imprimante non connectée';
    $('dev-axes').replaceChildren(...AXES.map(([axe, unite]) => {
      const bloc = el('div', 'dev-axe');
      const lineaire = unite === 'mm';
      const bouger = (sens) => () => {
        const distance = sens * (lineaire ? pas.lineaire : pas.angle);
        const [mini, maxi] = butees(axe);
        const cible = Math.min(maxi, Math.max(mini, machine[axe] + distance));
        if (cible === machine[axe]) { app.dire(`Butée de l'axe ${axe.toUpperCase()} atteinte.`, { info: true }); return; }
        const reel = Math.round((cible - machine[axe]) * 100) / 100;
        machine[axe] = cible;
        envoyer(`G91\n> G1 ${axe.toUpperCase()}${reel} F${lineaire ? 3000 : 600}\n> G90`);
        afficherAxes();
      };
      const plus = el('button', 'touche', `${axe.toUpperCase()}+`), moins = el('button', 'touche', `${axe.toUpperCase()}−`);
      plus.id = `dev-${axe}-plus`; moins.id = `dev-${axe}-moins`;
      plus.addEventListener('click', bouger(1)); moins.addEventListener('click', bouger(-1));
      const valeur = el('output', '', `${fr(machine[axe], 1)} ${unite}`);
      valeur.id = `dev-${axe}-valeur`;
      bloc.append(plus, valeur, moins);
      return bloc;
    }));
    for (const [id, valeurs, unite, cle] of [['dev-pas-lineaire', [0.1, 1, 10, 50], ' mm', 'lineaire'], ['dev-pas-angle', [1, 5, 10, 45], '°', 'angle']]) {
      $(id).replaceChildren(...valeurs.map((v) => {
        const bouton = el('button', pas[cle] === v ? 'actif' : '', `${String(v).replace('.', ',')}${unite}`);
        bouton.addEventListener('click', () => { pas[cle] = v; afficherAxes(); });
        return bouton;
      }));
    }
  }
  const ACTIONS = [
    ['Mise à zéro (G28)', 'dev-g28', () => { Object.assign(machine, { x: 0, y: 0, z: 0, b: B_VERTICAL, c: 0, aZero: true }); envoyer('G28'); afficherAxes(); }],
    ['Libérer les moteurs (M84)', 'dev-m84', () => { machine.aZero = false; envoyer('M84'); }],
    ['Éteindre la chauffe', 'dev-froid', () => envoyer('M104 S0\n> M140 S0')],
    ['Position actuelle (M114)', 'dev-m114', () => { envoyer('M114'); journal('retour', `  X:${machine.x} Y:${machine.y} Z:${machine.z} B:${machine.b} C:${machine.c}`); }],
    ['Vider le journal', 'dev-vider', () => $('dev-journal').replaceChildren()],
  ];
  $('dev-actions').replaceChildren(...ACTIONS.map(([libelle, id, action]) => {
    const bouton = el('button', 'bouton fin', libelle);
    bouton.id = id;
    bouton.addEventListener('click', action);
    return bouton;
  }));
  $('dev-saisie').addEventListener('submit', (e) => {
    e.preventDefault();
    const commande = $('dev-commande').value.trim();
    if (!commande) return;
    envoyer(commande);
    $('dev-commande').value = '';
  });
  $('mode-developpeur').addEventListener('change', (e) => {
    $('developpeur').hidden = !e.target.checked;
    if (e.target.checked) {
      afficherAxes();
      if (!$('dev-journal').childElementCount) journal('retour', 'Mode développeur : les commandes s\'affichent ici. L\'imprimante n\'est pas encore reliée au slicer.');
      $('developpeur').scrollIntoView({ block: 'nearest' });
    }
  });
  $('vue-calibration').addEventListener('click', afficherCartes);
  afficherCartes();
}

// --- Gestionnaire de bobines ------------------------------------------------------

function brancherGestionnaire(app) {
  const filtre = { montee: false, marque: '', matiere: '', recherche: '' };
  const replies = new Set();      // matières dont le groupe est replié
  let aSupprimer = null;          // bobine dont la suppression attend une confirmation

  function listeDeroulante(id, titre, valeurs, courant) {
    const liste = $(id);
    liste.replaceChildren(new Option(titre, ''), ...valeurs.map((v) => new Option(v, v)));
    liste.value = valeurs.includes(courant) ? courant : '';
    return liste.value;
  }

  function afficher() {
    const { etat } = app;
    const unique = (cle) => [...new Set(etat.bobines.map((b) => b[cle]).filter(Boolean))].sort((a, b) => a.localeCompare(b, 'fr'));
    filtre.marque = listeDeroulante('gestion-marque', 'Marque : toutes', unique('marque'), filtre.marque);
    filtre.matiere = listeDeroulante('gestion-matiere', 'Matière : toutes', unique('matiere'), filtre.matiere);
    for (const [id, actif] of [['gestion-toutes', !filtre.montee], ['gestion-montee', filtre.montee]]) {
      $(id).classList.toggle('actif', actif);
      $(id).setAttribute('aria-pressed', String(actif));
    }

    const cherche = sansAccent(filtre.recherche.trim());
    const visibles = etat.bobines.filter((b) => (!filtre.montee || b.id === etat.montee) &&
      (!filtre.marque || b.marque === filtre.marque) && (!filtre.matiere || b.matiere === filtre.matiere) &&
      (!cherche || sansAccent(`${b.nom} ${b.marque} ${b.matiere}`).includes(cherche)));

    const table = $('gestion-table');
    const tete = el('div', 'g-tete');
    tete.append(el('span', '', 'Bobine'), el('span', '', 'Restant'), el('span', '', 'Actions'));
    table.replaceChildren(tete);
    if (!visibles.length) {
      table.append(el('div', 'vide', etat.bobines.length ? 'Aucune bobine ne correspond à ces filtres.'
        : 'Aucune bobine enregistrée. Ajoute-en une avec « Ajouter une bobine ».'));
      return;
    }
    for (const matiere of [...new Set(visibles.map((b) => b.matiere))]) {
      const groupe = visibles.filter((b) => b.matiere === matiere);
      const ouvert = !replies.has(matiere);
      const titre = el('button', 'g-groupe');
      titre.setAttribute('aria-expanded', String(ouvert));
      titre.append(icone('chevron-bas'), el('span', '', matiere), el('span', 'compte', String(groupe.length)),
        el('span', '', `${entier(groupe.reduce((somme, b) => somme + b.poids_restant, 0))} g`));
      titre.addEventListener('click', () => { if (ouvert) replies.add(matiere); else replies.delete(matiere); afficher(); });
      table.append(titre);
      if (ouvert) table.append(...groupe.map(rang));
    }
  }

  function rang(b) {
    const { etat } = app;
    const montee = b.id === etat.montee;
    const ligne = el('div', 'g-rang');
    ligne.id = 'g-' + b.id;
    const couleur = el('span', 'g-couleur');
    couleur.style.background = b.couleur;
    const nom = el('div', 'g-nom');
    const titre = el('b', '', b.nom);
    if (montee) titre.append(el('span', 'g-montee', 'MONTÉE'));
    const detail = el('small');
    const carre = el('span', 'carre');
    carre.style.background = b.couleur;
    detail.append(document.createTextNode(`${String(b.diametre).replace('.', ',')} mm | `), carre,
      document.createTextNode(`${b.couleur.toUpperCase()}${b.marque ? ' · ' + b.marque : ''} · buse ${b.temp_buse} °C · plateau ${b.temp_plateau} °C`));
    nom.append(titre, detail);

    const part = Math.max(0, Math.min(1, b.poids_restant / b.poids_bobine));
    const reste = el('div', 'g-reste');
    const jauge = el('div', 'jauge' + (part < 0.15 ? ' bas' : ''));
    const niveau = el('span');
    niveau.style.width = `${part * 100}%`;
    jauge.append(niveau);
    reste.append(el('span', '', `${entier(b.poids_restant)} g / ${entier(b.poids_bobine)} g`), jauge);

    const actions = el('div', 'g-actions');
    const bouton = (id, dessin, titreBouton, action, inactif = false) => {
      const element = el('button', 'bouton-icone');
      element.id = `${id}-${b.id}`;
      element.title = titreBouton;
      element.setAttribute('aria-label', titreBouton);
      element.disabled = inactif;
      element.append(icone(dessin));
      element.addEventListener('click', action);
      return element;
    };
    if (aSupprimer === b.id) {
      const confirmer = el('button', 'confirmer', 'Supprimer ?');
      confirmer.id = 'g-confirmer-' + b.id;
      confirmer.addEventListener('click', () => {
        etat.bobines = etat.bobines.filter((x) => x.id !== b.id);
        if (etat.montee === b.id) etat.montee = etat.bobines[0]?.id ?? null;
        aSupprimer = null;
        app.enregistrerBobines();
        app.afficherBobine();
        app.dire(`Bobine « ${b.nom} » supprimée.`, { info: true });
      });
      const garder = bouton('g-garder', 'fermer', 'Ne pas supprimer', () => { aSupprimer = null; afficher(); });
      actions.append(confirmer, garder);
    } else {
      actions.append(
        bouton('g-monter', 'imprimante', montee ? 'Bobine déjà montée sur l\'imprimante' : 'Monter sur l\'imprimante',
          () => app.monterBobine(b.id), montee),
        bouton('g-modifier', 'modifier', 'Modifier la bobine', () => app.ouvrirBibliotheque(b.id)),
        bouton('g-supprimer', 'supprimer', 'Supprimer la bobine', () => { aSupprimer = b.id; afficher(); }));
    }
    ligne.append(couleur, nom, reste, actions);
    return ligne;
  }

  $('gestion-toutes').addEventListener('click', () => { filtre.montee = false; afficher(); });
  $('gestion-montee').addEventListener('click', () => { filtre.montee = true; afficher(); });
  $('gestion-marque').addEventListener('change', (e) => { filtre.marque = e.target.value; afficher(); });
  $('gestion-matiere').addEventListener('change', (e) => { filtre.matiere = e.target.value; afficher(); });
  $('gestion-recherche').addEventListener('input', (e) => { filtre.recherche = e.target.value; afficher(); });
  $('gestion-ajouter').addEventListener('click', () => app.ouvrirBibliotheque(app.nouvelleBobine()));

  return { afficher };
}
