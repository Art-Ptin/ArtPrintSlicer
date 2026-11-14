// ArtPrint Slicer - licence AGPL v3
// Jeu d'icônes d'ArtPrint Slicer, dessiné pour le projet.
// Grille de 24 × 24, trait seul (épaisseur fixée par la feuille de style),
// bouts et angles arrondis, couleur héritée du texte (currentColor).
// Trois tailles d'affichage : 16, 20 et 24 px.

const DESSINS = {
  // --- Général ---
  menu: '<path d="M4 7h16M4 12h16M4 17h16"/>',
  annuler: '<path d="M9 7 5 11l4 4"/><path d="M5 11h9a5 5 0 0 1 0 10h-3"/>',
  refaire: '<path d="m15 7 4 4-4 4"/><path d="M19 11h-9a5 5 0 0 0 0 10h3"/>',
  enregistrer: '<path d="M5 4h11l3 3v13H5z"/><path d="M8 4v5h7V4M8 20v-6h8v6"/>',
  dossier: '<path d="M3 7a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v9a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z"/>',
  exporter: '<path d="M12 4v11M8 8l4-4 4 4"/><path d="M5 14v5h14v-5"/>',
  reglages: '<path d="M4 7h9M17 7h3M4 12h3M11 12h9M4 17h11M19 17h1"/><circle cx="15" cy="7" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="17" r="2"/>',
  recherche: '<circle cx="11" cy="11" r="6"/><path d="m16 16 4 4"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  moins: '<path d="M5 12h14"/>',
  fermer: '<path d="m6 6 12 12M18 6 6 18"/>',
  coche: '<path d="m5 12 5 5 9-10"/>',
  synchroniser: '<path d="M4.800 9.500A7.500 7.500 0 0 1 18.600 8.400"/><path d="M19 4.500v4h-4"/><path d="M19.200 14.500A7.500 7.500 0 0 1 5.400 15.600"/><path d="M5 19.500v-4h4"/>',
  modifier: '<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="m13 7 4 4"/>',
  panneau: '<rect x="3" y="5" width="18" height="14" rx="1"/><path d="M9 5v14"/>',
  'chevron-haut': '<path d="m6 15 6-6 6 6"/>',
  'chevron-gauche': '<path d="m15 6-6 6 6 6"/>',
  projet: '<rect x="4" y="4" width="16" height="16" rx="1"/><path d="M8 9h8M8 12h8M8 15h5"/>',
  calibration: '<circle cx="12" cy="12" r="8.500"/><circle cx="12" cy="12" r="3"/><path d="M12 2v4M12 18v4M2 12h4M18 12h4"/>',
  supprimer: '<path d="M5 7h14M10 7V4h4v3M7 7l1 13h8l1-13M10 11v5M14 11v5"/>',
  'plein-ecran': '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>',
  camera: '<rect x="3" y="7" width="13" height="10" rx="1"/><path d="m16 11 5-3v8l-5-3"/>',
  ventilateur: '<circle cx="12" cy="12" r="8.500"/><circle cx="12" cy="12" r="1.500"/><path d="M12 10.500c-1-3 0-5 2-5.500M13.500 12c3-1 5 0 5.500 2M12 13.500c1 3 0 5-2 5.500M10.500 12c-3 1-5 0-5.500-2"/>',
  origine: '<path d="m4 11 8-7 8 7"/><path d="M6 10v10h12V10"/><path d="M10 20v-6h4v6"/>',
  arret: '<rect x="6" y="6" width="12" height="12" rx="1"/>',
  'moteurs-libres': '<circle cx="12" cy="12" r="8.500"/><path d="m6 6 12 12"/>',
  temperature: '<path d="M10 4a2 2 0 0 1 4 0v9.500a4 4 0 1 1-4 0z"/><path d="M12 9v7"/>',
  'chevron-bas': '<path d="m6 9 6 6 6-6"/>',
  'chevron-droite': '<path d="m9 6 6 6-6 6"/>',
  retablir: '<path d="M5 5v5h5"/><path d="M5.5 10A7.5 7.5 0 1 1 5 14"/>',
  info: '<circle cx="12" cy="12" r="8.5"/><path d="M12 11v6M12 7.5v.5"/>',
  ok: '<circle cx="12" cy="12" r="8.5"/><path d="m8 12 3 3 5-6"/>',
  attention: '<path d="M12 4 3 19h18z"/><path d="M12 10v4M12 16.5v.5"/>',
  erreur: '<path d="M8.5 3.5h7l5 5v7l-5 5h-7l-5-5v-7z"/><path d="m9 9 6 6M15 9l-6 6"/>',
  oeil: '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="2.5"/>',
  'oeil-barre': '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><path d="m4 4 16 16"/>',
  verrou: '<rect x="5" y="11" width="14" height="9" rx="1"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
  soleil: '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"/>',
  lune: '<path d="M19.5 14A8 8 0 0 1 10 4.5a8 8 0 1 0 9.5 9.5z"/>',
  lecture: '<path d="M7 5v14l12-7z"/>',
  pause: '<path d="M8 5v14M16 5v14"/>',

  // --- Pages et objets de l'atelier ---
  preparer: '<path d="m12 3 8 4.5v9L12 21l-8-4.5v-9z"/><path d="m4 7.5 8 4.5 8-4.5M12 12v9"/>',
  apercu: '<path d="m12 4 9 4.5-9 4.5-9-4.5z"/><path d="m3 12.5 9 4.5 9-4.5M3 16.5 12 21l9-4.5"/>',
  imprimante: '<path d="M4 20V4h16v16M2.5 20h19"/><path d="M4 8h16M10 8v3h4V8M12 11v2"/><path d="M7 17h10"/>',
  bobine: '<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="2.5"/><path d="M12 3.5v3M12 17.5v3M3.5 12h3M17.5 12h3"/>',
  buse: '<path d="M8 3h8v6l-2 4h-4L8 9z"/><path d="M10.5 13 12 17l1.5-4M9 20.5h6"/>',
  plateau: '<ellipse cx="12" cy="13" rx="9" ry="4.5"/><path d="M12 13v.5"/><path d="M3 13v2c0 2.5 4 4.5 9 4.5s9-2 9-4.5v-2"/>',

  // --- Barre d'outils : plateau et objets ---
  'ajouter-objet': '<path d="m10 4 7 3.5v7L10 18l-7-3.5v-7z"/><path d="m3 7.5 7 3.5 7-3.5M10 11v7"/><path d="M19 15v6M16 18h6"/>',
  'ajouter-plateau': '<ellipse cx="10.5" cy="10" rx="8" ry="4"/><path d="M2.5 10v2.5c0 2.2 3.6 4 8 4 .8 0 1.600-.1 2.300-.2"/><path d="M19 14v6M16 17h6"/>',
  orienter: '<path d="m7 12 6-3 6 3v6l-6 3-6-3z"/><path d="m7 12 6 3 6-3M13 15v6"/><path d="M4 9a8 8 0 0 1 11-5M15 2v2.500h-2.500"/>',
  agencer: '<rect x="4" y="4" width="7" height="7" rx="1"/><rect x="13" y="4" width="7" height="7" rx="1"/><rect x="4" y="13" width="7" height="7" rx="1"/><rect x="13" y="13" width="7" height="7" rx="1"/>',
  'couches-variables': '<path d="M9 20h11M9 17.500h11M9 14h11M9 9.500h11M9 4h11"/><path d="M4.500 6v12M3 7.500 4.500 6 6 7.500M3 16.500 4.500 18 6 16.500"/>',

  // --- Barre d'outils : placement ---
  deplacer: '<path d="M12 3v18M3 12h18"/><path d="m9 6 3-3 3 3M9 18l3 3 3-3M6 9l-3 3 3 3M18 9l3 3-3 3"/>',
  tourner: '<path d="M19.500 12A7.500 7.500 0 1 1 12 4.500h4"/><path d="m13.500 2 2.500 2.500-2.500 2.500"/><circle cx="12" cy="12" r="1.500"/>',
  echelle: '<rect x="4" y="11" width="9" height="9" rx="1"/><path d="M4 7V4h3M10 4h3M17 4h3v3M20 11v3M20 17v3h-3"/><path d="m13 11 5-5M14.500 6H18v3.500"/>',
  'poser-face': '<path d="M3 20h18"/><path d="M7 20v-9l10-3v12"/><path d="M12 2.500v4.500M10 5l2 2 2-2"/>',

  // --- Barre d'outils : modifier ---
  couper: '<path d="m12 4 7 3.500v9L12 20l-7-3.500v-9z"/><path d="M2 14.500 22 9.500" stroke-dasharray="2.500 2.500"/>',
  booleen: '<rect x="4" y="4" width="11" height="11" rx="1"/><rect x="9" y="9" width="11" height="11" rx="1"/>',
  assembler: '<rect x="3" y="7" width="7" height="10" rx="1"/><rect x="14" y="7" width="7" height="10" rx="1"/><path d="M10 12h4M12 10v4"/>',

  // --- Barre d'outils : peindre et annoter ---
  'peindre-supports': '<path d="M4 5h16M7 5v9M17 5v9"/><path d="m13 21 7-7-2.500-2.500-7 7L10 21z"/>',
  'peindre-couture': '<circle cx="9" cy="11" r="6"/><path d="M9 5v12" stroke-dasharray="2 2"/><path d="m14 21 6.500-6.500L18 12l-6.500 6.500L11 21z"/>',
  texte: '<path d="M5 7V5h14v2M12 5v14M9 19h6"/>',
  mesurer: '<path d="m3 16 13-13 5 5L8 21z"/><path d="m7 12 2 2M10 9l2 2M13 6l2 2"/>',

  // --- Barre d'outils : 5 axes ---
  'strategie-5-axes': '<path d="M12 21v-8"/><path d="M12 13 6 7M12 13l6-6M12 13V4"/><path d="M4 7h2v2M18 9V7h2M10.500 5.500 12 4l1.500 1.500"/>',
  'direction-zone': '<path d="m4 17 8 3 8-3-8-3z"/><path d="M12 14V4M9 7l3-3 3 3"/>',
  'simulation-machine': '<ellipse cx="12" cy="18" rx="8" ry="2.500"/><path d="m8 4 5 2-1.500 4-2 .500L8 9z"/><path d="m10.500 10.500-.5 3"/><path d="M16 7a6 6 0 0 1 1.500 5M19.500 17a8 8 0 0 0 1-3"/>',

  // --- Groupes de paramètres ---
  'g-hauteur': '<path d="M9 5h11M9 12h11M9 19h11"/><path d="M4.500 6v12M3 7.500 4.500 6 6 7.500M3 16.500 4.500 18 6 16.500"/>',
  'g-largeur': '<path d="M5 4v16M19 4v16"/><path d="M8 12h8M10 10l-2 2 2 2M14 10l2 2-2 2"/>',
  'g-couture': '<circle cx="12" cy="12" r="7.500"/><path d="M12 2.500v19" stroke-dasharray="2.500 2.500"/>',
  'g-precision': '<circle cx="12" cy="12" r="6"/><path d="M12 2.500v5M12 16.500v5M2.500 12h5M16.500 12h5"/>',
  'g-lissage': '<path d="M3 18h18"/><path d="M5 14c2-2.500 4-2.500 6 0s4 2.500 6 0"/><path d="M8 9h8l2-4H9z"/>',
  'g-parois': '<rect x="3.500" y="3.500" width="17" height="17" rx="1"/><rect x="7.500" y="7.500" width="9" height="9" rx="1"/>',
  'g-coques': '<path d="M4 5h16M4 8h16M4 16h16M4 19h16"/><path d="M4 12h2M9 12h2M14 12h2M19 12h1"/>',
  'g-remplissage': '<rect x="4" y="4" width="16" height="16" rx="1"/><path d="m4 12 8-8M4 20 20 4M12 20l8-8"/>',
  'g-ponts': '<path d="M3 8h18M5 8v11M19 8v11"/><path d="M5 13c4-4 10-4 14 0"/>',
  'g-vitesse': '<path d="M4 17a8.500 8.500 0 1 1 16 0"/><path d="m12 15 4-5"/><circle cx="12" cy="15" r="1"/>',
  'g-acceleration': '<path d="m4 7 5 5-5 5M10 7l5 5-5 5M16 7l5 5-5 5"/>',
  'g-supports': '<path d="M3 6h18M6 6v14M12 6v14M18 6v14M3 20h18"/>',
  'g-adherence': '<path d="M8 5h8v10H8z"/><path d="M3 19h18M5 15h14"/>',
  'g-deplacement': '<path d="M4 18h4M10 18h4" /><path d="M4 18 12 6l8 12" stroke-dasharray="3 2.500"/><path d="m17 17 3 1 1-3"/>',
  'g-cinq-axes': '<path d="M12 21V5M9 8l3-3 3 3"/><ellipse cx="12" cy="17" rx="8" ry="2.500"/><path d="M5 9a8 8 0 0 1 3-4.500"/>',
  'g-speciaux': '<path d="M12 3v5M12 16v5M3 12h5M16 12h5M6 6l3 3M15 15l3 3M6 18l3-3M15 9l3-3"/>',
};

export const NOMS_ICONES = Object.keys(DESSINS);

// Fabrique une icône. « taille » : 16 (par défaut), 20 ou 24.
export function icone(nom, taille = 16) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  svg.setAttribute('class', 'icone' + (taille === 16 ? '' : ` i-${taille}`));
  svg.innerHTML = DESSINS[nom] || DESSINS.info;
  return svg;
}
