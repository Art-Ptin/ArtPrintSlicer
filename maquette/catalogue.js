// ArtPrint Slicer - licence AGPL v3
// Maquette de l'interface : catalogue des paramètres du procédé, profils,
// matières et bobines d'exemple.
//
// « moteur » : nom du réglage dans le cœur du slicer. Un paramètre sans
// « moteur » est PRÉVU : il est affiché pour valider l'interface, mais le
// slicer ne l'applique pas encore.

export const ONGLETS = ['Qualité', 'Solidité', 'Vitesse', 'Supports', 'Autres'];

const n = (cle, libelle, unite, defaut, min, max, plus = {}) =>
  ({ cle, libelle, unite, defaut, min, max, type: 'nombre', ...plus });
const e = (cle, libelle, defaut, min, max, plus = {}) =>
  ({ cle, libelle, unite: '', defaut, min, max, type: 'entier', ...plus });
const b = (cle, libelle, defaut, plus = {}) => ({ cle, libelle, unite: '', defaut, type: 'bool', ...plus });
const c = (cle, libelle, defaut, choix, plus = {}) =>
  ({ cle, libelle, unite: '', defaut, choix, type: 'choix', ...plus });

const groupe = (onglet, section, parametres) => parametres.map((p) => ({ onglet, section, ...p }));

export const PARAMETRES = [
  // ---------------------------------------------------------------- Qualité
  ...groupe('Qualité', 'Hauteur de couche', [
    n('hauteur_couche', 'Hauteur de couche', 'mm', 0.2, 0.02, 2, { moteur: 'hauteur_couche',
      aide: 'Épaisseur de chaque couche. Plus fin : plus beau, mais plus long.' }),
    n('hauteur_premiere_couche', 'Première couche', 'mm', 0.2, 0.02, 2, { moteur: 'hauteur_premiere_couche',
      aide: 'Épaisseur de la couche posée sur le plateau.' }),
    b('couche_adaptative', 'Hauteur adaptative', false, { avance: true,
      aide: 'Couches plus fines dans les zones courbes, plus épaisses ailleurs.' }),
  ]),
  ...groupe('Qualité', 'Largeur de ligne', [
    n('largeur_extrusion', 'Par défaut', 'mm', 0.45, 0.05, 5, { moteur: 'largeur_extrusion',
      aide: 'Largeur du cordon déposé. En général un peu plus que le diamètre de la buse.' }),
    n('largeur_premiere_couche', 'Première couche', 'mm', 0.5, 0.05, 5),
    n('largeur_paroi_externe', 'Paroi externe', 'mm', 0.42, 0.05, 5),
    n('largeur_paroi_interne', 'Parois internes', 'mm', 0.45, 0.05, 5),
    n('largeur_dessus', 'Surface du dessus', 'mm', 0.42, 0.05, 5),
    n('largeur_remplissage', 'Remplissage', 'mm', 0.45, 0.05, 5, { avance: true }),
    n('largeur_plein', 'Remplissage plein', 'mm', 0.42, 0.05, 5, { avance: true }),
    n('largeur_supports', 'Supports', 'mm', 0.42, 0.05, 5, { avance: true }),
  ]),
  ...groupe('Qualité', 'Surface du dessus', [
    b('peau_courbe', 'Peau courbe (dessus lisse)', false, { moteur: 'peau_courbe',
      aide: 'Une dernière passe suit la forme du dessus de la pièce pour effacer les marches d\'escalier. ' +
        'L\'intérieur reste en couches planes. Dans cette simulation, les aperçus sont préenregistrés sans cette option.' }),
    n('peau_courbe_pente_max', 'Pente maximale suivie', '°', 30, 5, 60, { moteur: 'peau_courbe_pente_max',
      aide: 'Pente la plus raide que la buse suit pendant la passe de peau courbe.' }),
  ]),
  ...groupe('Qualité', 'Couture', [
    c('couture_position', 'Position', 'Arrière', ['Arrière', 'Plus proche', 'Alignée', 'Aléatoire'],
      { aide: 'Endroit où chaque contour commence et finit. Seul « Arrière » est appliqué aujourd\'hui.' }),
    n('couture_ecart', 'Écart de fermeture', '%', 10, 0, 100, { avance: true,
      aide: 'Le contour s\'arrête un peu avant de se refermer, pour ne pas faire de bosse.' }),
    b('couture_essuyage', 'Essuyer en fin de contour', false, { avance: true }),
  ]),
  ...groupe('Qualité', 'Précision', [
    n('resolution', 'Résolution des contours', 'mm', 0.012, 0.001, 1, { avance: true,
      aide: 'Les détails plus petits que cette valeur sont simplifiés.' }),
    n('compensation_trous', 'Compensation des trous', 'mm', 0, -1, 1),
    n('compensation_contours', 'Compensation des contours', 'mm', 0, -1, 1),
    n('pied_elephant', 'Compensation du pied d\'éléphant', 'mm', 0.15, 0, 2,
      { aide: 'Rétrécit la première couche, qui s\'écrase et déborde toujours un peu.' }),
  ]),
  ...groupe('Qualité', 'Lissage du dessus', [
    c('lissage_type', 'Surfaces lissées', 'Aucune', ['Aucune', 'Dessus seulement', 'Toutes les surfaces pleines'],
      { aide: 'La buse repasse à chaud sur la surface pour la rendre lisse.' }),
    n('lissage_debit', 'Débit', '%', 10, 0, 100, { avance: true }),
    n('lissage_espacement', 'Espacement des passes', 'mm', 0.15, 0.01, 2, { avance: true }),
    n('lissage_vitesse', 'Vitesse', 'mm/s', 30, 1, 500, { avance: true }),
  ]),
  ...groupe('Qualité', 'Parois', [
    c('generateur_parois', 'Générateur de parois', 'Classique', ['Classique', 'Largeur variable'],
      { aide: '« Largeur variable » adapte la largeur du cordon dans les pièces fines.' }),
    c('ordre_parois', 'Ordre des parois', 'Intérieur puis extérieur', ['Intérieur puis extérieur', 'Extérieur puis intérieur'],
      { aide: 'Seul « Intérieur puis extérieur » est appliqué aujourd\'hui.' }),
    b('une_paroi_dessus', 'Une seule paroi sur le dessus', false, { avance: true }),
    b('eviter_parois', 'Éviter de traverser les parois', false, { avance: true,
      aide: 'Les trajets à vide contournent les parois pour ne pas laisser de traces.' }),
  ]),

  // --------------------------------------------------------------- Solidité
  ...groupe('Solidité', 'Parois', [
    e('nombre_parois', 'Nombre de parois', 2, 1, 20, { moteur: 'nombre_parois',
      aide: 'Nombre de contours autour de chaque couche.' }),
    b('parois_fines', 'Détecter les parois fines', false,
      { aide: 'Imprime en un seul passage les parois plus fines que deux cordons.' }),
  ]),
  ...groupe('Solidité', 'Coques du dessus et du dessous', [
    e('couches_dessus', 'Couches du dessus', 4, 0, 1000, { moteur: 'couches_dessus',
      aide: 'Nombre de couches pleines qui ferment le haut de la pièce.' }),
    n('epaisseur_dessus', 'Épaisseur minimale du dessus', 'mm', 0.8, 0, 50, { avance: true }),
    c('motif_dessus', 'Motif du dessus', 'Lignes', ['Lignes', 'Lignes sans croisement', 'Concentrique'],
      { aide: 'Seul « Lignes » est appliqué aujourd\'hui.' }),
    e('couches_dessous', 'Couches du dessous', 3, 0, 1000, { moteur: 'couches_dessous',
      aide: 'Nombre de couches pleines qui ferment le bas de la pièce.' }),
    n('epaisseur_dessous', 'Épaisseur minimale du dessous', 'mm', 0.6, 0, 50, { avance: true }),
    c('motif_dessous', 'Motif du dessous', 'Lignes', ['Lignes', 'Lignes sans croisement', 'Concentrique']),
  ]),
  ...groupe('Solidité', 'Remplissage', [
    n('densite_remplissage', 'Densité', '%', 15, 0, 100, { moteur: 'densite_remplissage',
      aide: '0 : pièce creuse. 100 : pièce pleine.' }),
    c('motif_remplissage', 'Motif', 'Lignes croisées',
      ['Lignes croisées', 'Grille', 'Triangles', 'Cubique', 'Gyroïde', 'Nid d\'abeille'],
      { aide: 'Seul « Lignes croisées » est appliqué aujourd\'hui.' }),
    n('direction_remplissage', 'Direction', '°', 45, 0, 360, { avance: true }),
    n('recouvrement_remplissage', 'Recouvrement avec les parois', '%', 25, 0, 50,
      { moteur: 'recouvrement_remplissage', echelle: 100,
        aide: 'Part de la largeur du cordon dont le remplissage mord sur les parois.' }),
    e('combinaison_remplissage', 'Regrouper les couches', 1, 1, 10, { avance: true,
      aide: 'Imprime le remplissage une couche sur N, plus épais, pour gagner du temps.' }),
  ]),
  ...groupe('Solidité', 'Ponts', [
    n('debit_ponts', 'Débit des ponts', '', 1, 0.5, 2, { avance: true }),
    b('ponts_epais', 'Ponts épais', true, { avance: true,
      aide: 'Cordons plus épais dans le vide : plus solides, moins beaux.' }),
  ]),

  // ---------------------------------------------------------------- Vitesse
  ...groupe('Vitesse', 'Première couche', [
    n('vitesse_premiere_couche', 'Première couche', 'mm/s', 20, 1, 1000, { moteur: 'vitesse_premiere_couche',
      aide: 'Vitesse de toute la première couche : lente, pour bien accrocher.' }),
    n('vitesse_plein_premiere', 'Remplissage de la première couche', 'mm/s', 30, 1, 1000, { avance: true }),
  ]),
  ...groupe('Vitesse', 'Autres couches', [
    n('vitesse_paroi_externe', 'Paroi externe', 'mm/s', 25, 1, 1000, { moteur: 'vitesse_paroi_externe',
      aide: 'La paroi visible : plus lente pour un bel aspect.' }),
    n('vitesse_paroi_interne', 'Parois internes', 'mm/s', 40, 1, 1000, { moteur: 'vitesse_paroi_interne' }),
    n('vitesse_petits_contours', 'Petits contours', 'mm/s', 15, 1, 1000, { avance: true }),
    n('vitesse_remplissage', 'Remplissage', 'mm/s', 60, 1, 1000, { moteur: 'vitesse_remplissage' }),
    n('vitesse_plein', 'Remplissage plein', 'mm/s', 60, 1, 1000),
    n('vitesse_dessus', 'Surface du dessus', 'mm/s', 30, 1, 1000),
    n('vitesse_comblement', 'Comblement des interstices', 'mm/s', 30, 1, 1000, { avance: true }),
    n('vitesse_supports', 'Supports', 'mm/s', 40, 1, 1000, { avance: true }),
  ]),
  ...groupe('Vitesse', 'Surplombs et ponts', [
    b('ralentir_surplombs', 'Ralentir dans les surplombs', true),
    n('vitesse_surplombs', 'Vitesse dans les surplombs', 'mm/s', 20, 1, 1000, { avance: true }),
    n('vitesse_ponts', 'Ponts', 'mm/s', 30, 1, 1000),
  ]),
  ...groupe('Vitesse', 'Déplacements', [
    n('vitesse_a_vide', 'Déplacements à vide', 'mm/s', 120, 1, 2000, { moteur: 'vitesse_a_vide',
      aide: 'Vitesse des trajets sans extrusion.' }),
  ]),
  ...groupe('Vitesse', 'Accélérations', [
    n('acceleration', 'Impression', 'mm/s²', 3000, 50, 50000),
    n('acceleration_premiere', 'Première couche', 'mm/s²', 500, 50, 50000, { avance: true }),
    n('acceleration_externe', 'Paroi externe', 'mm/s²', 2000, 50, 50000, { avance: true }),
    n('acceleration_vide', 'Déplacements à vide', 'mm/s²', 5000, 50, 50000, { avance: true }),
  ]),

  // --------------------------------------------------------------- Supports
  ...groupe('Supports', 'Supports', [
    b('supports', 'Générer des supports', false,
      { aide: 'En 5 axes, incliner la tête évite souvent d\'en avoir besoin.' }),
    c('supports_type', 'Type', 'Arborescent', ['Classique', 'Arborescent', 'Peints à la main']),
    n('supports_angle', 'Angle de surplomb', '°', 45, 0, 90,
      { aide: 'Les surfaces plus inclinées que cet angle reçoivent des supports.' }),
    b('supports_plateau', 'Depuis le plateau seulement', false),
    n('supports_distance_z', 'Écart vertical avec la pièce', 'mm', 0.2, 0, 5, { avance: true }),
    n('supports_distance_xy', 'Écart horizontal avec la pièce', 'mm', 0.35, 0, 10, { avance: true }),
    n('supports_espacement', 'Espacement des lignes', 'mm', 2.5, 0.2, 20, { avance: true }),
    e('supports_interface', 'Couches d\'interface', 2, 0, 10, { avance: true }),
  ]),
  ...groupe('Supports', 'Adhérence au plateau', [
    e('jupe_boucles', 'Boucles de jupe', 1, 0, 20, { moteur: 'jupe_boucles',
      aide: 'Boucles tracées autour de la pièce pour amorcer la buse. 0 : aucune.' }),
    n('jupe_distance', 'Distance de la jupe', 'mm', 5, 0.1, 100, { moteur: 'jupe_distance' }),
    c('bordure_type', 'Bordure', 'Aucune', ['Aucune', 'Extérieure', 'Intérieure', 'Les deux'],
      { aide: 'Collerette collée à la pièce, pour mieux tenir sur le plateau.' }),
    n('bordure_largeur', 'Largeur de la bordure', 'mm', 5, 0, 100),
    e('radeau_couches', 'Couches de radeau', 0, 0, 20, { avance: true }),
  ]),

  // ----------------------------------------------------------------- Autres
  ...groupe('Autres', 'Déplacements à vide', [
    n('levee_z', 'Levée de la buse', 'mm', 0.2, 0, 10, { moteur: 'levee_z',
      aide: 'La buse monte de cette hauteur pendant les déplacements à vide.' }),
    c('levee_type', 'Forme de la levée', 'Verticale', ['Verticale', 'En pente', 'En spirale'], { avance: true }),
    b('retraction_couche', 'Rétracter au changement de couche', true, { avance: true }),
  ]),
  ...groupe('Autres', '5 axes', [
    n('inclinaison_max', 'Inclinaison maximale de la tête', '°', 55, 0, 90,
      { aide: 'Limite de l\'axe B utilisée par le tranchage 5 axes.' }),
    n('hauteur_securite', 'Hauteur de dégagement', 'mm', 10, 0, 200,
      { aide: 'Hauteur à laquelle la buse remonte avant de tourner le plateau ou d\'incliner la tête.' }),
    b('reculer_bras', 'Reculer le bras avant de tourner le plateau', true,
      { aide: 'Évite que le bras touche une pièce haute pendant la rotation.' }),
    n('lissage_orientation', 'Lissage de l\'orientation', '°/mm', 2, 0.1, 45, { avance: true,
      aide: 'Variation maximale de l\'inclinaison le long d\'un cordon.' }),
  ]),
  ...groupe('Autres', 'Modes spéciaux', [
    b('mode_vase', 'Mode vase (spirale continue)', false),
    b('peau_floue', 'Peau floue', false, { avance: true,
      aide: 'Fait vibrer la paroi externe pour un aspect granuleux.' }),
  ]),
];

// Profils de procédé : seules les valeurs qui diffèrent du catalogue sont écrites.
export const PROFILS = [
  { id: 'standard', nom: '0,20 mm Standard', valeurs: {} },
  { id: 'fin', nom: '0,12 mm Fin', valeurs: { hauteur_couche: 0.12, couches_dessus: 6, couches_dessous: 5,
      vitesse_paroi_externe: 20, vitesse_paroi_interne: 35, vitesse_remplissage: 50 } },
  { id: 'rapide', nom: '0,28 mm Rapide', valeurs: { hauteur_couche: 0.28, hauteur_premiere_couche: 0.28,
      couches_dessus: 3, couches_dessous: 3, densite_remplissage: 10,
      vitesse_paroi_externe: 40, vitesse_paroi_interne: 60, vitesse_remplissage: 80 } },
  { id: 'solide', nom: '0,20 mm Solide', valeurs: { nombre_parois: 4, couches_dessus: 6, couches_dessous: 5,
      densite_remplissage: 35 } },
];

// Valeurs de départ proposées pour chaque matière.
export const MATIERES = {
  PLA:  { temp_buse: 210, temp_plateau: 55, densite: 1.24, ventilation: true, retraction: 2 },
  PETG: { temp_buse: 240, temp_plateau: 75, densite: 1.27, ventilation: true, retraction: 2.5 },
  ABS:  { temp_buse: 250, temp_plateau: 100, densite: 1.04, ventilation: false, retraction: 2 },
  ASA:  { temp_buse: 255, temp_plateau: 100, densite: 1.07, ventilation: false, retraction: 2 },
  TPU:  { temp_buse: 225, temp_plateau: 45, densite: 1.21, ventilation: true, retraction: 1 },
  PA:   { temp_buse: 270, temp_plateau: 90, densite: 1.14, ventilation: false, retraction: 2 },
  PC:   { temp_buse: 275, temp_plateau: 110, densite: 1.2, ventilation: false, retraction: 2 },
};

// Bobines d'EXEMPLE, pour que la bibliothèque ne soit pas vide au premier essai.
export const BOBINES_EXEMPLE = [
  { id: 'ex1', nom: 'PLA blanc', marque: 'Exemple', matiere: 'PLA', couleur: '#e9e9e4', diametre: 1.75,
    densite: 1.24, temp_buse: 210, temp_plateau: 55, debit: 1, retraction: 2, retraction_vitesse: 40,
    ventilation: true, prix_kg: 22, poids_bobine: 1000, poids_restant: 820 },
  { id: 'ex2', nom: 'PETG noir', marque: 'Exemple', matiere: 'PETG', couleur: '#22252a', diametre: 1.75,
    densite: 1.27, temp_buse: 240, temp_plateau: 75, debit: 0.98, retraction: 2.5, retraction_vitesse: 35,
    ventilation: true, prix_kg: 25, poids_bobine: 1000, poids_restant: 410 },
  { id: 'ex3', nom: 'PLA orange', marque: 'Exemple', matiere: 'PLA', couleur: '#e8710a', diametre: 1.75,
    densite: 1.24, temp_buse: 215, temp_plateau: 55, debit: 1, retraction: 2, retraction_vitesse: 40,
    ventilation: true, prix_kg: 24, poids_bobine: 750, poids_restant: 96 },
];

// Profil de la machine « ArtPrint 5 axes » : les mesures faites sur le
// prototype par son constructeur. Ce sont des réglages, pas des constantes :
// ils se changent dans la fiche de l'imprimante. « version » : changer ce
// texte fait reposer les valeurs une fois chez tout le monde.
export const PROFIL_MACHINE = {
  version: 'prototype-3',
  reglages: {
    distance_pivot_buse: '55',   // du pivot de la tête à la pointe de la buse, en mm
    hauteur_impression: '260',   // hauteur imprimable, en mm
    b_min: '-110',               // la tête bascule jusqu'à 110° de chaque côté :
    b_max: '110',                // au-delà de 90°, la buse imprime par en dessous
    b_inclinaison_max: '110',
  },
};
