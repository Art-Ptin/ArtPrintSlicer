// ArtPrint Slicer - licence AGPL v3
// Pont de SIMULATION de la maquette : remplace le cœur du slicer par des
// réponses préenregistrées, pour essayer l'interface dans un simple navigateur.

const PIECES = [
  { fichier: 'vase.stl', donnees: 'vase', nom: 'Vase bosselé', detail: '50 × 51 × 60 mm, 300 couches' },
  { fichier: 'tube.stl', donnees: 'tube', nom: 'Tube', detail: '30 × 30 × 12 mm, 60 couches' },
  { fichier: 'marches.stl', donnees: 'marches', nom: 'Pièce à marches', detail: '30 × 30 × 16 mm, 80 couches' },
  { fichier: 'arbre.stl', donnees: 'arbre', nom: 'Arbre (un tronc, deux branches)', detail: '55 × 27 × 60 mm, à essayer avec la stratégie « Ondulation »' },
  { fichier: 'arbre_plat.stl', donnees: 'arbre_plat', nom: 'Arbre plat (une seule pièce)', detail: '60 × 10 × 50 mm, deux stratégies proposées' },
  { fichier: 'arbre_organique.stl', donnees: 'arbre_organique', nom: 'Arbre organique', detail: '51 × 34 × 74 mm, trois branches et une brindille' },
  { fichier: 'chassis_drone.stl', donnees: 'chassis_drone', nom: 'Châssis de drone', detail: '88 × 88 × 32 mm, quatre bras qui montent' },
  { fichier: 'chandelier.stl', donnees: 'chandelier', nom: 'Chandelier', detail: '51 × 46 × 52 mm, trois bras courbes' },
  { fichier: 'tuyau_en_y.stl', donnees: 'tuyau_en_y', nom: 'Tuyau en Y (creux)', detail: '54 × 16 × 51 mm, deux sorties inclinées' },
  { fichier: '3DBenchy.stl', donnees: '3DBenchy', nom: '3DBenchy (le test d\'imprimante le plus connu)', detail: '60 × 31 × 48 mm · modèle de Creative Tools, dans le domaine public (CC0)' },
  { fichier: 'w40_banc_d_essai.stl', donnees: 'w40_banc_d_essai', nom: 'Banc d\'essai ArtPrint (pièce maison)', detail: '68 × 44 × 67 mm : éventail de surplombs (30° à 90°), pont, colonne fine et boule, pointe, bras courbe' },
  { fichier: 'v05_etoile.stl', donnees: 'v05_etoile', nom: 'Étoile (six branches au même niveau)', detail: '51 × 45 × 50 mm, modèle d\'essai n° 5' },
  { fichier: 'v08_fourches_en_cascade.stl', donnees: 'v08_fourches_en_cascade', nom: 'Fourches en cascade', detail: '53 × 30 × 63 mm, quatorze branches sur trois niveaux' },
  { fichier: 'v20_corail.stl', donnees: 'v20_corail', nom: 'Corail', detail: '35 × 30 × 77 mm, seize branches et rameaux' },
];

const base = fetch('./donnees/base.json').then((r) => r.json());
const pieces = {};
let reglages = null;   // liste courante
let origine = null;    // valeurs d'origine
let piece = null;      // pièce ouverte

async function charger() {
  if (!reglages) {
    const b = await base;
    origine = JSON.stringify(b.reglages);
    reglages = JSON.parse(origine);
  }
}

const pause = (ms) => new Promise((suite) => setTimeout(suite, ms));

function ecrireNombre(n) {
  return String(Number(n.toFixed(6))).replace('.', ',');
}

// Mêmes contrôles que le vrai slicer : nombre valide, bornes, entier, choix.
function definir(r, valeur) {
  const texte = String(valeur ?? '').trim();
  if (r.type === 'texte') { r.valeur = texte; return null; }
  if (texte === '') {
    if (!r.facultatif) return `« ${r.cle} » ne peut pas rester vide.`;
    r.valeur = '';
    return null;
  }
  if (r.type === 'choix') {
    const choix = r.choix.find((c) => c.toLowerCase() === texte.toLowerCase());
    if (!choix) return `« ${r.cle} » doit valoir ${r.choix.join(' ou ')}.`;
    r.valeur = choix;
    return null;
  }
  const n = Number(texte.replace(',', '.'));
  if (!Number.isFinite(n) || !/^[+-]?(\d+([.,]\d*)?|[.,]\d+)$/.test(texte)) {
    return `« ${texte} » n'est pas un nombre valide pour « ${r.cle} ».`;
  }
  if (r.type === 'booleen') {
    if (n !== 0 && n !== 1) return `« ${r.cle} » doit valoir 1 (oui) ou 0 (non).`;
  } else {
    if (n < r.min || n > r.max) {
      return `« ${r.cle} » doit être compris entre ${ecrireNombre(r.min)} et ${ecrireNombre(r.max)}.`;
    }
    if (r.type === 'entier' && !Number.isInteger(n)) return `« ${r.cle} » doit être un nombre entier.`;
  }
  r.valeur = ecrireNombre(n);
  return null;
}

// Feuille de choix d'une pièce d'exemple (à la place de la fenêtre « Ouvrir » de Windows).
function choisirPiece() {
  return new Promise((resoudre) => {
    const voile = document.createElement('div');
    voile.className = 'voile';
    const feuille = document.createElement('div');
    feuille.className = 'feuille etroite';
    feuille.setAttribute('role', 'dialog');
    feuille.setAttribute('aria-label', 'Ouvrir une pièce d\'exemple');
    const tete = document.createElement('div');
    tete.className = 'bloc-tete';
    const titre = document.createElement('h2');
    titre.textContent = 'Ouvrir une pièce d\'exemple';
    const annuler = document.createElement('button');
    annuler.className = 'bouton fin';
    annuler.id = 'piece-annuler';
    annuler.textContent = 'Annuler';
    tete.append(titre, annuler);
    const liste = document.createElement('div');
    liste.className = 'choix-piece';
    const fermer = (chemin) => { voile.remove(); resoudre(chemin); };
    PIECES.forEach((p, i) => {
      const bouton = document.createElement('button');
      bouton.id = 'piece-' + p.donnees;
      const nom = document.createElement('strong');
      nom.textContent = p.nom;
      const detail = document.createElement('small');
      detail.textContent = p.detail;
      bouton.append(nom, detail);
      bouton.addEventListener('click', () => fermer(p.fichier));
      liste.append(bouton);
      if (i === 0) setTimeout(() => bouton.focus(), 0);
    });
    const pied = document.createElement('div');
    pied.className = 'pied';
    pied.textContent = 'Dans le vrai logiciel, c\'est la fenêtre « Ouvrir » de Windows qui apparaît ici.';
    annuler.addEventListener('click', () => fermer(null));
    voile.addEventListener('click', (e) => { if (e.target === voile) fermer(null); });
    feuille.append(tete, liste, pied);
    voile.append(feuille);
    document.body.append(voile);
  });
}

// Vrai : les réponses viennent d'enregistrements, pas du vrai slicer.
export const simulation = true;

export async function appeler(commande, parametres = {}) {
  await charger();
  switch (commande) {
    case 'infos':
      return { ok: true, ...(await base).infos };
    case 'reglages':
      return { ok: true, reglages };
    case 'definir_reglages': {
      const refus = {};
      for (const [cle, valeur] of Object.entries(parametres.valeurs)) {
        const r = reglages.find((x) => x.cle === cle);
        const erreur = r ? definir(r, valeur) : `réglage inconnu « ${cle} ».`;
        if (erreur) refus[cle] = erreur;
      }
      return { ok: true, refus, reglages };
    }
    case 'reglages_par_defaut':
      reglages = JSON.parse(origine);
      return { ok: true, reglages };
    case 'charger_reglages':
      return { ok: true, reglages };
    case 'enregistrer_reglages':
    case 'enregistrer_gcode':
      return { ok: true };
    case 'dialogue_ouvrir': {
      const estReglages = parametres.filtres?.[0]?.[1] === '*.txt';
      return { ok: true, chemin: estReglages ? 'reglages.txt' : await choisirPiece() };
    }
    case 'dialogue_enregistrer':
      return { ok: true, chemin: parametres.nom_propose || 'fichier' };
    case 'charger_modele': {
      const trouvee = PIECES.find((p) => p.fichier === parametres.chemin);
      if (!trouvee) return { ok: false, erreur: `Impossible d'ouvrir « ${parametres.chemin} ».` };
      if (!pieces[trouvee.donnees]) {
        pieces[trouvee.donnees] = await fetch(`./donnees/${trouvee.donnees}.json`).then((r) => r.json());
      }
      piece = pieces[trouvee.donnees];
      piece.nom_donnees = trouvee.donnees;
      return { ok: true, modele: structuredClone(piece.modele) };
    }
    case 'transformer_modele': {
      // La simulation agrandit ou réduit l'image du modèle ; le tranchage, lui, reste celui enregistré.
      if (!piece) return { ok: false, erreur: 'Aucun modèle n\'est ouvert.' };
      const echelle = parametres.echelle ?? piece.echelle ?? 1;
      piece.echelle = echelle;
      const modele = structuredClone(piece.modele);
      modele.positions = modele.positions.map((v) => Math.round(v * echelle * 1000) / 1000);
      modele.dimensions = modele.dimensions.map((v) => Math.round(v * echelle * 100) / 100);
      const orientation = parametres.orientation ?? 'origine';
      return { ok: true, modele, echelle, orientation: 'origine',
        note: orientation === 'auto'
          ? 'Simulation : l\'orientation automatique est calculée par le vrai slicer, dans l\'application.'
          : (echelle !== 1 ? 'Simulation : le modèle est affiché à la nouvelle échelle, mais le tranchage reste celui enregistré.' : '') };
    }
    case 'strategies':
      if (!piece) return { ok: false, erreur: 'Aucun modèle n\'est ouvert.' };
      return { ok: true, strategies: structuredClone(piece.strategies ?? []) };
    case 'trancher': {
      if (!piece) return { ok: false, erreur: 'Aucun modèle n\'est ouvert.' };
      await pause(700);
      const strategie = parametres.strategie || 'classique';
      if (strategie === 'classique') return { ok: true, resultat: { ...piece.resultat, preparation_s: 300 }, strategie };
      if (!(piece.strategies ?? []).some((s) => s.type === strategie)) {
        return { ok: false, erreur: `La stratégie « ${strategie} » n'est pas proposée pour ce modèle.` };
      }
      // Le résultat de chaque autre stratégie est dans son propre fichier, chargé à la demande.
      piece.resultats ??= {};
      piece.resultats[strategie] ??= await fetch(`./donnees/${piece.nom_donnees}-${strategie}.json`).then((r) => r.json());
      return { ok: true, strategie,
        resultat: { ...piece.resultats[strategie], preparation_s: 300, duree_classique_s: piece.resultat.duree_s } };
    }
    default:
      return { ok: false, erreur: `Commande inconnue : « ${commande} ».` };
  }
}
