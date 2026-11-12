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
  for (const [id, nom] of [['menu-fichiers', 'Fichiers de l\'imprimante'], ['menu-maj', 'Mise à jour de l\'imprimante']]) {
    $(id).addEventListener('click', () => app.dire(`${nom} : à venir.`, { info: true }));
  }
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
