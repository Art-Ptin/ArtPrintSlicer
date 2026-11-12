// ArtPrint Slicer - licence AGPL v3
// La page principale : réglages, boutons, et lien avec la vue 3D.
import { appeler } from './pont.js';
import { Vue3D, COULEURS } from './vue3d.js';

const $ = (id) => document.getElementById(id);
const vue3d = new Vue3D($('canvas3d'));
// Accès pour les essais automatiques et le dépannage.
window.artprint = { vue3d };

const etat = {
  reglages: [],        // liste rendue par le slicer
  groupe: null,        // onglet de réglages ouvert
  refus: {},           // messages d'erreur par réglage
  saisies: {},         // texte tapé pour les réglages refusés
  modele: null,
  resultat: null,
  strategies: [],      // façons d'imprimer proposées pour le modèle ouvert
  strategie: 'classique',
  strategiesPerimees: false,  // vrai si un réglage a changé depuis qu'elles ont été proposées
};

// --- Messages ----------------------------------------------------------------

let minuterieMessage = 0;
function dire(texte, info = false) {
  const zone = $('message');
  zone.textContent = texte;
  zone.classList.toggle('info', info);
  zone.hidden = false;
  clearTimeout(minuterieMessage);
  minuterieMessage = setTimeout(() => { zone.hidden = true; }, info ? 4000 : 9000);
}

function occuper(texte) {
  $('attente-texte').textContent = texte;
  $('attente').hidden = false;
}
function liberer() { $('attente').hidden = true; }

// Appelle le slicer et affiche l'erreur éventuelle. Rend la réponse, ou null.
async function demander(commande, parametres) {
  const reponse = await appeler(commande, parametres);
  if (!reponse.ok) {
    dire(reponse.erreur || 'Erreur inconnue.');
    return null;
  }
  return reponse;
}

// --- Réglages ----------------------------------------------------------------

function afficherOnglets() {
  const groupes = [...new Set(etat.reglages.map((r) => r.groupe))];
  if (!groupes.includes(etat.groupe)) etat.groupe = groupes[0];
  const zone = $('onglets');
  zone.replaceChildren(...groupes.map((groupe) => {
    const bouton = document.createElement('button');
    bouton.className = 'onglet' + (groupe === etat.groupe ? ' actif' : '');
    bouton.textContent = groupe;
    bouton.setAttribute('role', 'tab');
    bouton.setAttribute('aria-selected', String(groupe === etat.groupe));
    bouton.addEventListener('click', () => { etat.groupe = groupe; afficherReglages(); });
    return bouton;
  }));
}

function afficherReglages() {
  afficherOnglets();
  const zone = $('reglages');
  zone.replaceChildren(...etat.reglages.filter((r) => r.groupe === etat.groupe).map((r) => {
    const champ = document.createElement('div');
    champ.className = 'champ' + (etat.refus[r.cle] ? ' invalide' : '') + (r.type === 'texte' ? ' long' : '');

    const libelle = document.createElement('label');
    libelle.textContent = r.libelle;
    libelle.htmlFor = 'reglage-' + r.cle;

    const saisie = document.createElement('div');
    saisie.className = 'saisie';
    let entree;
    if (r.type === 'choix') {
      entree = document.createElement('select');
      for (const choix of r.choix) entree.add(new Option(choix, choix, false, choix === r.valeur));
    } else {
      entree = document.createElement('input');
      if (r.type === 'booleen') {
        entree.type = 'checkbox';
        entree.checked = r.valeur === '1';
      } else {
        entree.type = 'text';
        if (r.type !== 'texte') entree.inputMode = 'decimal';
        // Une valeur refusée reste affichée telle qu'elle a été tapée, pour la corriger.
        entree.value = etat.saisies[r.cle] ?? r.valeur;
        entree.autocomplete = 'off';
        if (r.facultatif) entree.placeholder = r.type === 'texte' ? 'aucun' : 'à mesurer';
      }
    }
    entree.id = 'reglage-' + r.cle;
    entree.addEventListener('change', () => {
      const valeur = r.type === 'booleen' ? (entree.checked ? '1' : '0') : entree.value;
      changerReglage(r.cle, valeur);
    });
    saisie.append(entree);
    if (r.unite) {
      const unite = document.createElement('span');
      unite.className = 'unite';
      unite.textContent = r.unite;
      saisie.append(unite);
    }

    const aide = document.createElement('div');
    aide.className = 'aide';
    aide.textContent = r.aide;
    champ.append(libelle, saisie, aide);
    if (etat.refus[r.cle]) {
      const refus = document.createElement('div');
      refus.className = 'refus';
      refus.textContent = etat.refus[r.cle];
      champ.append(refus);
    }
    return champ;
  }));
}

function recevoirReglages(reglages) {
  etat.reglages = reglages;
  const rayon = Number(valeurDe('rayon_plateau').replace(',', '.'));
  const hauteur = Number(valeurDe('hauteur_impression').replace(',', '.'));
  if (rayon > 0 && hauteur > 0 && (rayon !== vue3d.rayon || hauteur !== vue3d.hauteur)) {
    vue3d.definirVolume(rayon, hauteur);
  }
  afficherReglages();
}

function valeurDe(cle) {
  return etat.reglages.find((r) => r.cle === cle)?.valeur ?? '';
}

async function changerReglage(cle, valeur) {
  const reponse = await demander('definir_reglages', { valeurs: { [cle]: valeur } });
  if (!reponse) return;
  if (reponse.refus[cle]) {
    etat.refus[cle] = reponse.refus[cle];
    etat.saisies[cle] = valeur;
  } else {
    delete etat.refus[cle];
    delete etat.saisies[cle];
    // Les stratégies proposées dépendent des réglages (limites de la machine...).
    if (etat.strategies.length > 1) {
      etat.strategiesPerimees = true;
      afficherStrategies();
    }
  }
  recevoirReglages(reponse.reglages);
  perimerResultat();
}

// --- Tableau de bord -----------------------------------------------------------

const nombre = (valeur, decimales = 1) =>
  Number(valeur).toLocaleString('fr-FR', { minimumFractionDigits: decimales, maximumFractionDigits: decimales });
const entier = (valeur) => Number(valeur).toLocaleString('fr-FR');
const reglageNombre = (cle) => Number(valeurDe(cle).replace(',', '.'));

function duree(secondes) {
  const minutes = Math.max(1, Math.round(secondes / 60));
  return minutes >= 60 ? `${Math.floor(minutes / 60)} h ${String(minutes % 60).padStart(2, '0')} min`
                       : `${minutes} min`;
}

// État de la tuile « Vérification » : neutre, bon, mauvais ou perime.
function definirEtat(classe, titre, detail) {
  $('tuile-etat').className = 'tuile etat ' + classe;
  $('t-etat').textContent = titre;
  $('t-etat-detail').textContent = detail;
}

function viderResultat() {
  for (const id of ['t-couches', 't-filament', 't-duree', 't-vide']) $(id).textContent = '–';
  for (const id of ['t-hauteur', 't-poids', 't-retractions']) $(id).textContent = ' ';
  $('btn-voir-erreurs').hidden = true;
  $('carte-erreurs').hidden = true;
}

// --- Modèle et tranchage -------------------------------------------------------

async function ouvrirModele() {
  const choix = await demander('dialogue_ouvrir', {
    titre: 'Ouvrir un modèle', filtres: [['Modèles STL', '*.stl'], ['Tous les fichiers', '*.*']] });
  if (!choix || !choix.chemin) return;
  occuper('Lecture du modèle…');
  const reponse = await demander('charger_modele', { chemin: choix.chemin });
  liberer();
  if (!reponse) return;

  etat.modele = reponse.modele;
  etat.resultat = null;  // nouveau modèle : l'ancien résultat ne compte plus
  etat.modele.nom = choix.chemin.split(/[\\/]/).pop();
  vue3d.afficherModele(reponse.modele.positions);
  $('accueil').hidden = true;

  const [x, y, z] = etat.modele.dimensions;
  $('t-modele').textContent = etat.modele.nom;
  $('t-modele').title = choix.chemin;
  $('t-dimensions').textContent =
    `${nombre(x)} × ${nombre(y)} × ${nombre(z)} mm · ${entier(etat.modele.triangles)} triangles`;
  if (!etat.modele.ferme) dire(etat.modele.message_fermeture);

  $('btn-trancher').disabled = false;
  perimerResultat();
  await proposerStrategies();
}

// --- Stratégies d'impression ---------------------------------------------------

// Demande au slicer les façons d'imprimer le modèle ouvert.
async function proposerStrategies() {
  etat.strategies = [];
  etat.strategie = 'classique';
  etat.strategiesPerimees = false;
  if (etat.modele && etat.modele.ferme) {
    occuper('Recherche des façons d\'imprimer ce modèle…');
    await new Promise((suite) => setTimeout(suite, 30));
    const reponse = await demander('strategies');
    liberer();
    if (reponse) etat.strategies = reponse.strategies;
  }
  afficherStrategies();
}

function afficherStrategies() {
  const liste = $('strategie');
  const detail = $('strategie-detail');
  if (!etat.strategies.length) {
    liste.replaceChildren(new Option('Classique (3 axes)', 'classique'));
    detail.textContent = etat.modele
      ? 'Tout le modèle est imprimé en couches horizontales, la tête à la verticale.'
      : 'Ouvre un modèle : le slicer proposera les façons de l\'imprimer (3 axes, ou 5 axes par branches).';
    return;
  }
  liste.replaceChildren(...etat.strategies.map((s) => new Option(
    `${s.nom} · ${entier(s.surplomb_mm2)} mm² à soutenir`, s.type, false, s.type === etat.strategie)));
  const choisie = etat.strategies.find((s) => s.type === etat.strategie) || etat.strategies[0];
  detail.replaceChildren(document.createTextNode(choisie.description));
  if (choisie.parties.length > 1) {
    const inclinaisons = document.createElement('b');
    inclinaisons.textContent = choisie.parties.map((p) =>
      (p.courbe ? `jusqu'à ${Math.round(p.inclinaison)}° (courbe)` : `${Math.round(p.inclinaison)}°`)).join(', ');
    detail.append(document.createTextNode(' Inclinaisons : '), inclinaisons, document.createTextNode('.'));
  }
  const remarques = [...choisie.remarques];
  if (etat.strategiesPerimees) {
    remarques.push('Un réglage a changé : les stratégies seront recalculées au moment de trancher.');
  }
  for (const texte of remarques) {
    const remarque = document.createElement('span');
    remarque.className = 'remarque';
    remarque.textContent = texte;
    detail.append(remarque);
  }
}

// Le modèle ou un réglage a changé : l'ancien résultat n'est plus valable.
function perimerResultat() {
  const avaitResultat = Boolean(etat.resultat);
  etat.resultat = null;
  vue3d.viderApercu();
  $('btn-exporter').disabled = true;
  $('vue-apercu').disabled = true;
  viderResultat();
  if (!etat.modele) {
    definirEtat('neutre', 'En attente', 'Ouvre un modèle pour commencer.');
  } else if (!etat.modele.ferme) {
    definirEtat('mauvais', 'Modèle ouvert', 'Le modèle n\'est pas un volume fermé : il faut le réparer.');
  } else if (avaitResultat) {
    definirEtat('perime', 'À retrancher', 'Un réglage a changé depuis le dernier tranchage.');
  } else {
    definirEtat('neutre', 'Prêt', 'Clique sur « Trancher ».');
  }
  choisirVue('preparer');
}

async function trancher() {
  if (!etat.modele) return;
  occuper('Tranchage et vérification en cours…');
  // Laisse l'écran afficher le message avant le calcul.
  await new Promise((suite) => setTimeout(suite, 30));
  const reponse = await demander('trancher', { strategie: etat.strategie });
  liberer();
  if (!reponse) {
    definirEtat('mauvais', 'Tranchage impossible', 'Voir le message en haut de la vue.');
    return;
  }

  const resultat = reponse.resultat;
  etat.resultat = resultat;
  const verification = resultat.verification;
  $('t-couches').textContent = entier(resultat.couches);
  $('t-hauteur').textContent = `de ${valeurDe('hauteur_couche')} mm`;
  $('t-filament').textContent = `${nombre(resultat.filament_m, 2)} m`;
  // Poids indicatif : densité du PLA, 1,24 g/cm³.
  const diametre = reglageNombre('diametre_filament');
  const grammes = resultat.filament_m * 1000 * Math.PI * (diametre / 2) ** 2 * 1.24 / 1000;
  $('t-poids').textContent = `environ ${nombre(grammes, grammes < 10 ? 1 : 0)} g de PLA`;
  $('t-duree').textContent = duree(resultat.duree_s);
  $('t-vide').textContent = entier(verification.deplacements_a_vide);
  $('t-retractions').textContent = `${entier(verification.retractions)} rétractions`;

  if (verification.valide) {
    definirEtat('bon', 'G-code vérifié', 'Aucune règle physique violée. Prêt à exporter.');
  } else {
    definirEtat('mauvais', 'Tranchage refusé',
      `${entier(verification.nombre_erreurs)} erreur(s) : le G-code ne peut pas être exporté.`);
  }
  $('erreurs').replaceChildren(...verification.erreurs.map((erreur) => {
    const element = document.createElement('li');
    const titre = document.createElement('div');
    titre.className = 'ligne';
    titre.textContent = `Ligne ${erreur.ligne} : ${erreur.texte}`;
    const probleme = document.createElement('div');
    probleme.textContent = erreur.probleme;
    const position = document.createElement('div');
    const [x, y, z, b, c] = erreur.position.map((v) => nombre(v, 2));
    position.textContent = `Position de la machine : X ${x}  Y ${y}  Z ${z}  B ${b}°  C ${c}°`;
    const solution = document.createElement('div');
    solution.className = 'solution';
    solution.textContent = `Solution possible : ${erreur.solution}`;
    element.append(titre, probleme, position, solution);
    return element;
  }));
  $('btn-voir-erreurs').hidden = verification.valide;
  $('carte-erreurs').hidden = verification.valide;
  $('btn-exporter').disabled = !verification.valide;

  // Aperçu : légende, curseur de couches.
  vue3d.afficherApercu(resultat.apercu, reglageNombre('largeur_extrusion'));
  $('legende-types').replaceChildren(...resultat.types.map((nom, type) => {
    const element = document.createElement('li');
    const libelle = document.createElement('label');
    const case_ = document.createElement('input');
    case_.type = 'checkbox';
    case_.id = 'type-' + type;
    case_.checked = vue3d.typesVisibles.has(type);
    case_.addEventListener('change', () => vue3d.montrerType(type, case_.checked));
    const pastille = document.createElement('span');
    pastille.className = 'pastille';
    pastille.style.background = COULEURS[type];
    const texte = document.createElement('span');
    texte.textContent = nom.charAt(0).toUpperCase() + nom.slice(1);
    libelle.append(case_, pastille, texte);
    element.append(libelle);
    return element;
  }));
  const curseur = $('curseur-couche');
  curseur.max = resultat.couches;
  curseur.value = resultat.couches;
  $('couche-seule').checked = false;
  majCouche();
  $('vue-apercu').disabled = false;
  choisirVue('apercu');
}

function majCouche() {
  const numero = Number($('curseur-couche').value);
  const couche = etat.resultat?.apercu[numero - 1];
  $('couche-texte').textContent = couche ? `${numero}\n${nombre(couche.z, 2)}` : '';
  vue3d.montrerCouches(numero, $('couche-seule').checked);
}

function choisirVue(vue) {
  const apercu = vue === 'apercu' && Boolean(etat.resultat);
  $('vue-preparer').classList.toggle('actif', !apercu);
  $('vue-apercu').classList.toggle('actif', apercu);
  $('vue-preparer').setAttribute('aria-selected', String(!apercu));
  $('vue-apercu').setAttribute('aria-selected', String(apercu));
  $('legende').hidden = !apercu;
  $('couches').hidden = !apercu;
  vue3d.montrer(apercu ? 'apercu' : 'preparer');
}

async function exporter() {
  if (!etat.resultat) return;
  const nom = (etat.modele?.nom || 'piece').replace(/\.[^.]+$/, '') + '.gcode';
  const choix = await demander('dialogue_enregistrer', {
    titre: 'Exporter le G-code', nom_propose: nom, filtres: [['G-code', '*.gcode']] });
  if (!choix || !choix.chemin) return;
  if (await demander('enregistrer_gcode', { chemin: choix.chemin })) {
    dire(`G-code enregistré : ${choix.chemin}`, true);
  }
}

async function chargerReglages() {
  const choix = await demander('dialogue_ouvrir', {
    titre: 'Charger des réglages', filtres: [['Réglages', '*.txt'], ['Tous les fichiers', '*.*']] });
  if (!choix || !choix.chemin) return;
  const reponse = await demander('charger_reglages', { chemin: choix.chemin });
  if (!reponse) return;
  etat.refus = {};
  etat.saisies = {};
  recevoirReglages(reponse.reglages);
  perimerResultat();
  dire('Réglages chargés.', true);
}

async function enregistrerReglages() {
  const choix = await demander('dialogue_enregistrer', {
    titre: 'Enregistrer les réglages', nom_propose: 'reglages.txt', filtres: [['Réglages', '*.txt']] });
  if (!choix || !choix.chemin) return;
  if (await demander('enregistrer_reglages', { chemin: choix.chemin })) {
    dire(`Réglages enregistrés : ${choix.chemin}`, true);
  }
}

async function remettreDefaut() {
  const reponse = await demander('reglages_par_defaut');
  if (!reponse) return;
  etat.refus = {};
  etat.saisies = {};
  recevoirReglages(reponse.reglages);
  perimerResultat();
  dire('Tous les réglages sont revenus à leur valeur d\'origine.', true);
}

// --- Démarrage ---------------------------------------------------------------

$('btn-ouvrir').addEventListener('click', ouvrirModele);
$('btn-ouvrir-2').addEventListener('click', ouvrirModele);
$('btn-trancher').addEventListener('click', trancher);
$('strategie').addEventListener('change', (e) => {
  etat.strategie = e.target.value;
  afficherStrategies();
  perimerResultat();
});
$('btn-exporter').addEventListener('click', exporter);
$('btn-charger-reglages').addEventListener('click', chargerReglages);
$('btn-enregistrer-reglages').addEventListener('click', enregistrerReglages);
$('btn-defaut').addEventListener('click', remettreDefaut);
$('vue-preparer').addEventListener('click', () => choisirVue('preparer'));
$('vue-apercu').addEventListener('click', () => choisirVue('apercu'));
$('curseur-couche').addEventListener('input', majCouche);
$('couche-seule').addEventListener('change', majCouche);
$('cam-iso').addEventListener('click', () => vue3d.pointDeVue('iso'));
$('cam-dessus').addEventListener('click', () => vue3d.pointDeVue('dessus'));
$('cam-face').addEventListener('click', () => vue3d.pointDeVue('face'));
$('cam-droite').addEventListener('click', () => vue3d.pointDeVue('droite'));
$('cam-cadrer').addEventListener('click', () => vue3d.pointDeVue('cadrer'));
$('btn-voir-erreurs').addEventListener('click', () => { $('carte-erreurs').hidden = false; });
$('btn-fermer-erreurs').addEventListener('click', () => { $('carte-erreurs').hidden = true; });

(async () => {
  const infos = await demander('infos');
  if (infos) $('version').textContent = infos.version;
  const reponse = await demander('reglages');
  if (reponse) recevoirReglages(reponse.reglages);
})();
