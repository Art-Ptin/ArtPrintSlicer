// ArtPrint Slicer - licence AGPL v3
// La vue 3D : le plateau, le volume d'impression, le modèle, et l'aperçu des
// trajectoires (cordons dessinés à leur vraie largeur).
import * as THREE from 'three';
import { OrbitControls } from './lib/OrbitControls.js';
import { LineSegments2 } from './lib/lines/LineSegments2.js';
import { LineSegmentsGeometry } from './lib/lines/LineSegmentsGeometry.js';
import { LineMaterial } from './lib/lines/LineMaterial.js';
import { installerManipulateurs } from './manipulateurs.js';

// Couleur de chaque type de trajectoire (même ordre que dans le slicer) :
// jupe, paroi interne, paroi externe, remplissage plein, remplissage, déplacement.
// Ce sont les valeurs de repli, utilisées quand la feuille de style ne définit
// pas les variables « --seg-... ».
export const COULEURS = ['#8a94a3', '#e8890c', '#d9342b', '#2f7de1', '#1fa77a', '#a3acb9'];
const VARIABLES = ['--seg-jupe', '--seg-paroi-interne', '--seg-paroi-externe', '--seg-plein',
  '--seg-remplissage', '--seg-deplacement'];

// Couleurs en vigueur : celles des variables de design si elles existent.
export function couleursTrajectoires() {
  const style = getComputedStyle(document.documentElement);
  return COULEURS.map((repli, type) => style.getPropertyValue(VARIABLES[type]).trim() || repli);
}
const TYPE_VIDE = 5;

// Normales « lissées » d'un maillage (9 nombres par triangle). En chaque
// sommet, on fait la moyenne des faces qui s'y rejoignent, mais seulement de
// celles qui tournent de moins de 40° : une surface courbe paraît lisse, une
// vraie arête (le bord d'un cube) reste vive.
function normalesLissees(positions) {
  const normales = new Float32Array(positions.length);
  const faces = new Float32Array(positions.length / 3);   // une normale (pondérée par l'aire) par triangle
  const sommes = new Map();
  const cle = (i) => `${Math.round(positions[i] * 500)},${Math.round(positions[i + 1] * 500)},${Math.round(positions[i + 2] * 500)}`;
  const cles = new Array(positions.length / 3);
  for (let t = 0; t < positions.length; t += 9) {
    const ux = positions[t + 3] - positions[t], uy = positions[t + 4] - positions[t + 1], uz = positions[t + 5] - positions[t + 2];
    const vx = positions[t + 6] - positions[t], vy = positions[t + 7] - positions[t + 1], vz = positions[t + 8] - positions[t + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    faces[t / 3] = nx; faces[t / 3 + 1] = ny; faces[t / 3 + 2] = nz;
    for (let k = 0; k < 3; k++) {
      const c = cle(t + 3 * k);
      cles[t / 3 + k] = c;
      let liste = sommes.get(c);
      if (!liste) { liste = []; sommes.set(c, liste); }
      liste.push(t / 3);
    }
  }
  const seuil = Math.cos(40 * Math.PI / 180);
  for (let t = 0; t < positions.length; t += 9) {
    const f = t / 3;
    const fl = Math.hypot(faces[f], faces[f + 1], faces[f + 2]) || 1;
    for (let k = 0; k < 3; k++) {
      let x = 0, y = 0, z = 0;
      for (const g of sommes.get(cles[f + k])) {
        const gl = Math.hypot(faces[g], faces[g + 1], faces[g + 2]) || 1;
        const accord = (faces[f] * faces[g] + faces[f + 1] * faces[g + 1] + faces[f + 2] * faces[g + 2]) / (fl * gl);
        if (accord >= seuil) { x += faces[g]; y += faces[g + 1]; z += faces[g + 2]; }
      }
      const l = Math.hypot(x, y, z) || 1;
      normales[t + 3 * k] = x / l; normales[t + 3 * k + 1] = y / l; normales[t + 3 * k + 2] = z / l;
    }
  }
  return normales;
}

// Une étiquette (texte clair, fond transparent) à poser à plat ou face à la caméra.
function textureTexte(texte, taille = 96, graisse = 700) {
  const toile = document.createElement('canvas');
  const dessin = toile.getContext('2d');
  const police = `${graisse} ${taille}px Archivo, "Segoe UI", sans-serif`;
  dessin.font = police;
  toile.width = Math.ceil(dessin.measureText(texte).width) + 24;
  toile.height = Math.ceil(taille * 1.3);
  dessin.font = police;
  dessin.fillStyle = '#ffffff';
  dessin.textBaseline = 'middle';
  dessin.fillText(texte, 12, toile.height / 2);
  const texture = new THREE.CanvasTexture(toile);
  texture.anisotropy = 4;
  return { texture, rapport: toile.width / toile.height };
}

export class Vue3D {
  constructor(canvas) {
    this.canvas = canvas;
    this.rendu = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
    this.rendu.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.scene = new THREE.Scene();

    // Z vers le haut, comme sur la machine.
    this.camera = new THREE.PerspectiveCamera(35, 1, 1, 6000);
    this.camera.up.set(0, 0, 1);
    this.camera.position.set(300, -420, 300);
    this.controles = new OrbitControls(this.camera, canvas);
    this.controles.target.set(0, 0, 40);
    this.controles.addEventListener('change', () => { this.demanderRendu(); this.surVue?.(); });

    this.scene.add(new THREE.HemisphereLight(0xffffff, 0x777788, 1.6));
    const soleil = new THREE.DirectionalLight(0xffffff, 1.4);
    soleil.position.set(150, -200, 400);
    this.scene.add(soleil);

    this.plateau = new THREE.Group();
    this.modele = new THREE.Group();
    this.apercu = new THREE.Group();
    this.scene.add(this.plateau, this.modele, this.apercu);
    this.apercu.visible = false;

    // Un matériau par type : les cordons ont leur vraie largeur, en millimètres.
    this.largeur = 0.45;
    this.materiaux = couleursTrajectoires().map((couleur, type) => type === TYPE_VIDE
      ? new THREE.LineBasicMaterial({ color: couleur, transparent: true, opacity: 0.6 })
      : new LineMaterial({ color: 0xffffff, vertexColors: true, linewidth: this.largeur, worldUnits: true }));

    this.couches = [];          // pour chaque couche : un objet par type (ou null)
    this.typesVisibles = new Set([0, 1, 2, 3, 4]);
    this.coucheMax = 0;
    this.coucheSeule = false;
    this.boite = null;          // boîte englobante de la pièce
    this.rayon = 150;
    this.hauteur = 260;
    this.sombre = window.matchMedia('(prefers-color-scheme: dark)');
    this.sombre.addEventListener('change', () => this.rafraichirTheme());
    // Un thème choisi explicitement (attribut data-theme) l'emporte sur celui du système.
    new MutationObserver(() => this.rafraichirTheme())
      .observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });

    this.nombrePlateaux = 1;
    this.plateauActif = 0;
    this.surChoixPlateau = null;
    this.fantomes = [];
    this.definirVolume(this.rayon, this.hauteur);
    this.brancherClics();
    installerManipulateurs(this);
    new ResizeObserver(() => this.redimensionner()).observe(canvas.parentElement);
    this.redimensionner();
  }

  // Le thème a changé : le plateau et la pièce reprennent leurs couleurs.
  rafraichirTheme() {
    this.definirVolume(this.rayon, this.hauteur);
    this.modele.children.forEach((objet) => objet.material.color.copy(this.jeton('--c-piece', '#2aa57b')));
    this.materiaux[TYPE_VIDE].color.set(couleursTrajectoires()[TYPE_VIDE]);
    if (this.donnees) {
      const progression = this.progression;
      const max = this.coucheMax;
      this.afficherApercu(this.donnees, this.largeur);
      this.coucheMax = max;
      this.montrerProgression(progression);
    }
    this.demanderRendu();
  }

  // Couleur d'un jeton de la feuille de style (« --plateau »...), ou la valeur de repli.
  jeton(nom, repli) {
    const valeur = getComputedStyle(document.documentElement).getPropertyValue(nom).trim();
    return new THREE.Color(valeur || repli);
  }

  estSombre() {
    const choisi = document.documentElement.dataset.theme;
    return choisi ? choisi === 'dark' : this.sombre.matches;
  }

  demanderRendu() {
    if (this.renduDemande) return;
    this.renduDemande = true;
    requestAnimationFrame(() => {
      this.renduDemande = false;
      this.rendu.render(this.scene, this.camera);
    });
  }

  redimensionner() {
    const { clientWidth: largeur, clientHeight: hauteur } = this.canvas.parentElement;
    if (!largeur || !hauteur) return;
    this.rendu.setSize(largeur, hauteur, false);
    this.camera.aspect = largeur / hauteur;
    this.camera.updateProjectionMatrix();
    for (const materiau of this.materiaux) materiau.resolution?.set(largeur, hauteur);
    this.demanderRendu();
  }

  vider(groupe, avecMateriaux = true) {
    for (const objet of [...groupe.children]) {
      groupe.remove(objet);
      objet.geometry?.dispose();
      if (avecMateriaux) objet.material?.dispose();
    }
  }

  // Dessine le plateau rond (cercles tous les 50 mm, axes, repère C = 0) et
  // le contour du volume d'impression.
  definirVolume(rayon, hauteur) {
    this.rayon = rayon;
    this.hauteur = hauteur;
    this.vider(this.plateau);
    const sombre = this.estSombre();
    const disque = new THREE.Mesh(
      new THREE.CircleGeometry(rayon, 128),
      new THREE.MeshBasicMaterial({ color: this.jeton('--c-plateau', sombre ? '#262c36' : '#fbfcfd'), side: THREE.DoubleSide }));
    disque.position.z = -0.05;
    this.plateau.add(disque);

    const traits = (points, couleur, opacite = 1) => {
      const geometrie = new THREE.BufferGeometry();
      geometrie.setAttribute('position', new THREE.Float32BufferAttribute(points, 3));
      this.plateau.add(new THREE.LineSegments(geometrie, new THREE.LineBasicMaterial({
        color: couleur, transparent: opacite < 1, opacity: opacite })));
    };
    const cercle = (points, r, z) => {
      for (let i = 0; i < 128; i++) {
        const a = (i / 128) * Math.PI * 2, b = ((i + 1) / 128) * Math.PI * 2;
        points.push(r * Math.cos(a), r * Math.sin(a), z, r * Math.cos(b), r * Math.sin(b), z);
      }
    };
    const grille = [];
    for (let r = 50; r < rayon; r += 50) cercle(grille, r, 0);
    cercle(grille, rayon, 0);
    grille.push(-rayon, 0, 0, rayon, 0, 0, 0, -rayon, 0, 0, rayon, 0);
    traits(grille, this.jeton('--c-grille', sombre ? '#4a5462' : '#c3c9d2'));
    // Graduations : un cercle plus léger entre deux cercles principaux.
    const graduations = [];
    for (let r = 25; r < rayon; r += 50) if (r > 50) cercle(graduations, r, 0);
    traits(graduations, this.jeton('--c-grille', sombre ? '#4a5462' : '#c3c9d2'), 0.5);

    // Le nom du logiciel, à plat sur l'avant du plateau : il montre où est l'avant.
    const nom = textureTexte('ArtPrint Slicer');
    const hauteurNom = rayon * 0.13;
    const etiquette = new THREE.Mesh(
      new THREE.PlaneGeometry(hauteurNom * nom.rapport, hauteurNom),
      new THREE.MeshBasicMaterial({ map: nom.texture, transparent: true, opacity: 0.32, depthWrite: false }));
    etiquette.position.set(0, -rayon * 0.74, 0.03);
    this.plateau.add(etiquette);
    this.dessinerPlateaux();

    // Axes de la machine au centre du plateau : X rouge, Y vert, Z bleu.
    traits([0, 0, 0.05, 30, 0, 0.05], this.jeton('--axe-x', '#d9342b'));
    traits([0, 0, 0.05, 0, 30, 0.05], this.jeton('--axe-y', '#2f9e5b'));
    traits([0, 0, 0.05, 0, 0, 30], this.jeton('--axe-z', '#2f6fe1'));

    // Volume d'impression : un cylindre en traits légers.
    const volume = [];
    cercle(volume, rayon, hauteur);
    for (let i = 0; i < 8; i++) {
      const a = (i / 8) * Math.PI * 2;
      volume.push(rayon * Math.cos(a), rayon * Math.sin(a), 0, rayon * Math.cos(a), rayon * Math.sin(a), hauteur);
    }
    traits(volume, this.jeton('--c-grille', sombre ? '#6b7686' : '#9aa4b2'), 0.55);

    // Repère C = 0 : un trait rouge au bord, côté arrière.
    traits([0, rayon - 14, 0.02, 0, rayon, 0.02], this.jeton('--axe-x', '#d9342b'));
    this.demanderRendu();
  }

  // Plusieurs plateaux : celui sur lequel on travaille est au centre ; les
  // autres sont dessinés à côté, presque transparents, avec leur numéro. Un
  // clic sur l'un d'eux appelle « surChoix » avec son numéro (à partir de 0).
  definirPlateaux(nombre, actif, surChoix, pieces = []) {
    this.nombrePlateaux = nombre;
    this.plateauActif = actif;
    this.surChoixPlateau = surChoix;
    this.piecesPlateaux = pieces;   // pour chaque plateau : les triangles de sa pièce, ou rien
    this.definirVolume(this.rayon, this.hauteur);
  }

  dessinerPlateaux() {
    this.fantomes = [];
    const nombre = this.nombrePlateaux || 1;
    if (nombre < 2) return;
    const sombre = this.estSombre();
    const pas = 2 * this.rayon + 46;
    for (let i = 0; i < nombre; i++) {
      const x = (i - this.plateauActif) * pas;
      const numero = textureTexte(String(i + 1), 120);
      const pastille = new THREE.Sprite(new THREE.SpriteMaterial({ map: numero.texture, transparent: true,
        opacity: i === this.plateauActif ? 0.95 : 0.6, depthTest: false, color: this.jeton('--c-accent', '#e4572e') }));
      pastille.scale.set(26 * numero.rapport, 26, 1);
      pastille.position.set(x - this.rayon * 0.78, -this.rayon * 0.78, 14);
      this.plateau.add(pastille);
      if (i === this.plateauActif) continue;
      // Le plateau voisin : noir lui aussi, un peu transparent, avec ses cercles.
      const disque = new THREE.Mesh(new THREE.CircleGeometry(this.rayon, 96), new THREE.MeshBasicMaterial({
        color: this.jeton('--c-plateau', sombre ? '#0b0c0e' : '#0f1113'), transparent: true, opacity: 0.82,
        side: THREE.DoubleSide, depthWrite: false }));
      disque.position.set(x, 0, -0.06);
      disque.userData.plateau = i;
      this.plateau.add(disque);
      this.fantomes.push(disque);
      const cercles = [];
      const rayons = [this.rayon];
      for (let r = 50; r < this.rayon; r += 25) rayons.push(r);
      for (const r of rayons) {
        for (let k = 0; k < 96; k++) {
          const a = (k / 96) * Math.PI * 2, b = ((k + 1) / 96) * Math.PI * 2;
          cercles.push(x + r * Math.cos(a), r * Math.sin(a), 0, x + r * Math.cos(b), r * Math.sin(b), 0);
        }
      }
      const geometrie = new THREE.BufferGeometry();
      geometrie.setAttribute('position', new THREE.Float32BufferAttribute(cercles, 3));
      this.plateau.add(new THREE.LineSegments(geometrie, new THREE.LineBasicMaterial({
        color: this.jeton('--c-grille', '#4f565f'), transparent: true, opacity: 0.5 })));
      // Sa pièce, un peu transparente elle aussi.
      const positions = (this.piecesPlateaux || [])[i];
      if (positions && positions.length) {
        if (!this.cachePieces) this.cachePieces = new Map();
        let forme = this.cachePieces.get(positions);
        if (!forme) {
          forme = new THREE.BufferGeometry();
          forme.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
          forme.setAttribute('normal', new THREE.Float32BufferAttribute(normalesLissees(positions), 3));
          this.cachePieces = new Map([[positions, forme], ...[...this.cachePieces].slice(0, 7)]);
        }
        const piece = new THREE.Mesh(forme, new THREE.MeshStandardMaterial({
          color: this.jeton('--c-piece', '#a2a7ad'), roughness: 0.55, metalness: 0.05, transparent: true, opacity: 0.75 }));
        piece.position.x = x;
        piece.userData.plateau = i;
        this.plateau.add(piece);
        this.fantomes.push(piece);
      }
    }
  }

  // Recule la caméra pour voir tous les plateaux d'un coup.
  voirPlateaux() {
    const nombre = this.nombrePlateaux || 1;
    const pas = 2 * this.rayon + 46;
    const centre = new THREE.Vector3(((nombre - 1) / 2 - this.plateauActif) * pas, 0, 20);
    const d = (this.rayon * 2.3 + (nombre - 1) * pas * 0.62) * 1.5;
    this.controles.target.copy(centre);
    this.camera.position.set(centre.x, centre.y - 0.84 * d, centre.z + 0.54 * d);
    this.controles.update();
    this.demanderRendu();
  }

  // La souris dans la vue : un clic sur un plateau voisin le choisit ; un clic
  // sur la pièce la sélectionne ; la pièce se fait glisser sur le plateau.
  brancherClics() {
    const viser = (e) => {
      const cadre = this.canvas.getBoundingClientRect();
      const souris = new THREE.Vector2(((e.clientX - cadre.left) / cadre.width) * 2 - 1, -((e.clientY - cadre.top) / cadre.height) * 2 + 1);
      const rayon = new THREE.Raycaster();
      rayon.setFromCamera(souris, this.camera);
      return rayon;
    };
    const surLePlateau = (rayon) => {
      const point = new THREE.Vector3();
      return rayon.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0, 0, 1), 0), point) ? point : null;
    };
    let depart = null, prise = null;
    this.canvas.addEventListener('pointerdown', (e) => {
      depart = [e.clientX, e.clientY];
      prise = null;
      if (!this.modele.visible || !this.modele.children.length) return;
      if (this.surPointage) return;   // on désigne un point : la pièce ne se déplace pas
      const rayon = viser(e);
      if (rayon.intersectObjects(this.modele.children).length) {
        const point = surLePlateau(rayon);
        if (point) { prise = point; this.controles.enabled = false; }
      }
    });
    this.canvas.addEventListener('pointermove', (e) => {
      if (!prise) return;
      const point = surLePlateau(viser(e));
      if (!point) return;
      this.modele.position.set(point.x - prise.x, point.y - prise.y, 0);
      this.demanderRendu();
    });
    const lacher = (e) => {
      this.controles.enabled = true;
      const bouge = depart && Math.hypot(e.clientX - depart[0], e.clientY - depart[1]) > 5;
      if (prise) {
        const dx = this.modele.position.x, dy = this.modele.position.y;
        prise = null;
        if (bouge && Math.hypot(dx, dy) > 0.5) { this.surGlisser?.(dx, dy); return; }
        this.modele.position.set(0, 0, 0);
        this.selectionner(!this.selection);
        return;
      }
      if (bouge || !depart) return;
      if (this.surPointage) {
        // Le point de la pièce sous la souris, dans le repère du plateau (mm).
        const vise = this.modele.visible ? viser(e).intersectObjects(this.modele.children)[0] : null;
        this.surPointage(vise ? [vise.point.x, vise.point.y, vise.point.z] : null);
        return;
      }
      const touche = this.fantomes && this.fantomes.length ? viser(e).intersectObjects(this.fantomes)[0] : null;
      if (touche && this.surChoixPlateau) this.surChoixPlateau(touche.object.userData.plateau);
      else if (this.selection) this.selectionner(false);
    };
    this.canvas.addEventListener('pointerup', lacher);
    this.canvas.addEventListener('pointercancel', () => { prise = null; this.controles.enabled = true; this.modele.position.set(0, 0, 0); this.demanderRendu(); });
  }

  // Zones orientées : pour chacune, le plan de coupe (un disque) et la
  // direction d'impression (une flèche). « zones » : [{ point: [x, y, z],
  // inclinaison, azimut }], en mm et en degrés ; « choisie » : numéro de la
  // zone mise en avant (ou -1).
  afficherZones(zones, choisie = -1) {
    if (!this.zones) {
      this.zones = new THREE.Group();
      this.modele.parent.add(this.zones);
    }
    this.vider(this.zones);
    // (Un petit cercle par zone : plusieurs zones restent lisibles côte à côte.)
    const taille = this.boite ? Math.max(5, 0.16 * this.boite.getSize(new THREE.Vector3()).length()) : 12;
    zones.forEach((zone, i) => {
      const t = zone.inclinaison * Math.PI / 180, a = zone.azimut * Math.PI / 180;
      const direction = new THREE.Vector3(Math.sin(t) * Math.cos(a), Math.sin(t) * Math.sin(a), Math.cos(t));
      const point = new THREE.Vector3(...zone.point);
      const couleur = this.jeton(i === choisie ? '--c-selection' : '--c-accent', '#cf4a23');
      const disque = new THREE.Mesh(new THREE.CircleGeometry(taille, 48), new THREE.MeshBasicMaterial({
        color: couleur, transparent: true, opacity: i === choisie ? 0.34 : 0.18, side: THREE.DoubleSide, depthWrite: false, depthTest: false }));
      disque.renderOrder = 5;
      disque.position.copy(point);
      disque.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), direction);
      const fleche = new THREE.ArrowHelper(direction, point, 1.3 * taille, couleur, 0.32 * taille, 0.18 * taille);
      // Le plan et la flèche restent visibles à travers la pièce.
      for (const morceau of [fleche.line, fleche.cone]) { morceau.material.depthTest = false; morceau.renderOrder = 6; }
      const bord = new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints(
        Array.from({ length: 64 }, (_, k) => new THREE.Vector3(taille * Math.cos(k * Math.PI / 32), taille * Math.sin(k * Math.PI / 32), 0))),
        new THREE.LineBasicMaterial({ color: couleur, depthTest: false }));
      bord.position.copy(point); bord.quaternion.copy(disque.quaternion); bord.renderOrder = 6;
      const centre = new THREE.Mesh(new THREE.SphereGeometry(0.07 * taille, 12, 8), new THREE.MeshBasicMaterial({ color: couleur, depthTest: false }));
      centre.position.copy(point); centre.renderOrder = 7;
      this.zones.add(disque, bord, fleche, centre);
    });
    this.zones.visible = this.modele.visible;
    this.demanderRendu();
  }

  // La pièce glisse à l'écran en attendant la réponse du slicer.
  glisserProvisoire(dx, dy) {
    this.modele.position.set(dx, dy, 0);
    if (this.zones) this.zones.position.set(dx, dy, 0);
    this.demanderRendu();
  }

  // La pièce a glissé de (dx, dy) sur le plateau : sa géométrie est déplacée, sans être reconstruite.
  glisserModele(dx, dy) {
    for (const objet of this.modele.children) {
      objet.geometry.translate(dx, dy, 0);
      objet.geometry.computeBoundingBox();
      this.boite = objet.geometry.boundingBox.clone();
    }
    this.modele.position.set(0, 0, 0);
    if (this.zones) this.zones.position.set(0, 0, 0);
    this.viderApercu();
    this.rafraichirManipulateurs?.();
    this.demanderRendu();
  }

  // Montre que la pièce est sélectionnée (elle prend la couleur de sélection).
  selectionner(choisie) {
    this.selection = choisie;
    for (const objet of this.modele.children) {
      objet.material.emissive?.set(choisie ? this.jeton('--c-selection', '#cf4a23') : '#000000');
      if (objet.material.emissiveIntensity !== undefined) objet.material.emissiveIntensity = choisie ? 0.35 : 0;
    }
    this.demanderRendu();
    this.surSelection?.(choisie);
  }

  // « positions » : 9 nombres par triangle, modèle déjà posé sur le plateau.
  afficherModele(positions, garderVue = false) {
    this.vider(this.modele);
    this.modele.position.set(0, 0, 0);
    this.modele.quaternion.identity();
    if (this.zones) this.zones.position.set(0, 0, 0);
    this.selection = false;
    this.viderApercu();
    const geometrie = new THREE.BufferGeometry();
    geometrie.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    // La pièce est montrée lisse : on ne voit pas les triangles du fichier,
    // seulement ses vraies arêtes (voir « normalesLissees »).
    geometrie.setAttribute('normal', new THREE.Float32BufferAttribute(normalesLissees(positions), 3));
    geometrie.computeBoundingBox();
    this.boite = positions.length ? geometrie.boundingBox.clone() : null;
    this.modele.add(new THREE.Mesh(geometrie, new THREE.MeshStandardMaterial({
      color: this.jeton('--c-piece', '#2aa57b'), roughness: 0.55, metalness: 0.05 })));
    this.montrer('preparer');
    if (!garderVue) this.pointDeVue('iso');
    this.rafraichirManipulateurs?.();
    this.demanderRendu();
  }

  // Place la caméra : « iso », « dessus », « face », « droite ». La pièce
  // (ou le plateau s'il n'y a pas de pièce) est cadrée.
  pointDeVue(nom) {
    const centre = new THREE.Vector3(0, 0, 30);
    let portee = this.rayon * 1.1;
    if (this.boite) {
      const taille = new THREE.Vector3();
      this.boite.getSize(taille);
      this.boite.getCenter(centre);
      portee = Math.max(taille.x, taille.y, taille.z, 30);
    }
    const d = portee * 2.6;
    const directions = {
      iso: [0.0, -0.84, 0.54],   // de face, un peu au-dessus : le nom du logiciel est devant nous
      dessus: [0, -0.0005, 1],
      face: [0, -1, 0.0005],
      droite: [1, 0, 0.0005],
    };
    // « cadrer » : garde la direction actuelle du regard, recentre seulement.
    const actuelle = this.camera.position.clone().sub(this.controles.target);
    const [x, y, z] = nom === 'cadrer' ? [actuelle.x, actuelle.y, actuelle.z]
                                       : (directions[nom] || directions.iso);
    const norme = Math.hypot(x, y, z);
    this.controles.target.copy(centre);
    this.camera.position.set(centre.x + (x / norme) * d, centre.y + (y / norme) * d, centre.z + (z / norme) * d);
    this.controles.update();
    this.demanderRendu();
  }

  viderApercu() {
    this.vider(this.apercu, false);
    this.couches = [];
    this.donnees = null;
    this.partielle = null;
    this.progression = 1;
  }

  // « apercu » : pour chaque couche, { z, m: [x0,y0,z0,x1,y1,z1,type, ...] }.
  // « largeur » : largeur du cordon en mm.
  afficherApercu(apercu, largeur) {
    this.viderApercu();
    this.largeur = largeur > 0 ? largeur : 0.45;
    this.materiaux.forEach((materiau, type) => {
      if (type !== TYPE_VIDE) materiau.linewidth = this.largeur;
    });
    this.donnees = apercu;
    for (const couche of apercu) {
      this.couches.push(this.construireCouche(couche.m, couche.m.length));
    }
    this.coucheMax = this.couches.length;
    this.progression = 1;
    this.appliquerVisibilite();
  }

  // Fabrique les objets d'une couche à partir de ses « limite » premiers nombres
  // (7 par mouvement). Rend un objet par type, ou null.
  construireCouche(m, limite) {
    const parType = [[], [], [], [], [], []];
    for (let i = 0; i + 6 < limite; i += 7) {
      parType[m[i + 6]].push(m[i], m[i + 1], m[i + 2], m[i + 3], m[i + 4], m[i + 5]);
    }
    return parType.map((positions, type) => {
      if (!positions.length) return null;
      let objet;
      if (type === TYPE_VIDE) {
        const geometrie = new THREE.BufferGeometry();
        geometrie.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
        objet = new THREE.LineSegments(geometrie, this.materiaux[type]);
      } else {
        const geometrie = new LineSegmentsGeometry();
        geometrie.setPositions(positions);
        geometrie.setColors(this.ombrer(positions, type));
        objet = new LineSegments2(geometrie, this.materiaux[type]);
      }
      this.apercu.add(objet);
      return objet;
    });
  }

  // Montre seulement le début de la couche du haut : « fraction » de 0 à 1.
  // Sert à suivre le trajet de la buse dans la couche.
  montrerProgression(fraction) {
    this.progression = Math.min(1, Math.max(0, fraction));
    if (this.partielle) {
      for (const objet of this.partielle) {
        if (objet) { this.apercu.remove(objet); objet.geometry.dispose(); }
      }
      this.partielle = null;
    }
    const couche = this.donnees?.[this.coucheMax - 1];
    if (couche && this.progression < 1) {
      const mouvements = Math.round((couche.m.length / 7) * this.progression);
      this.partielle = this.construireCouche(couche.m, mouvements * 7);
    }
    this.appliquerVisibilite();
  }

  // Donne du relief à l'aperçu : chaque cordon est éclairci ou assombri
  // selon son orientation, comme s'il était éclairé de côté.
  ombrer(positions, type) {
    const base = new THREE.Color(couleursTrajectoires()[type]);
    const couleurs = new Float32Array(positions.length);
    for (let i = 0; i < positions.length; i += 6) {
      const dx = positions[i + 3] - positions[i], dy = positions[i + 4] - positions[i + 1];
      const longueur = Math.hypot(dx, dy) || 1;
      // Éclairage : part de la lumière reçue par le flanc du cordon.
      const lumiere = Math.abs((dy / longueur) * 0.62 - (dx / longueur) * 0.78);
      const facteur = 0.62 + 0.46 * lumiere;
      for (let k = 0; k < 2; k++) {
        couleurs[i + k * 3] = Math.min(1, base.r * facteur);
        couleurs[i + k * 3 + 1] = Math.min(1, base.g * facteur);
        couleurs[i + k * 3 + 2] = Math.min(1, base.b * facteur);
      }
    }
    return couleurs;
  }

  montrerCouches(coucheMax, seule) {
    this.coucheMax = coucheMax;
    this.coucheSeule = seule;
    this.montrerProgression(this.progression);
  }

  montrerType(type, visible) {
    if (visible) this.typesVisibles.add(type); else this.typesVisibles.delete(type);
    this.appliquerVisibilite();
  }

  appliquerVisibilite() {
    this.couches.forEach((objets, indice) => {
      const numero = indice + 1;
      const coucheVisible = this.coucheSeule ? numero === this.coucheMax : numero <= this.coucheMax;
      // La couche du haut est remplacée par sa version partielle, s'il y en a une.
      const remplacee = this.partielle && numero === this.coucheMax;
      objets.forEach((objet, type) => {
        if (objet) objet.visible = coucheVisible && !remplacee && this.typesVisibles.has(type);
      });
    });
    this.partielle?.forEach((objet, type) => {
      if (objet) objet.visible = this.typesVisibles.has(type);
    });
    this.demanderRendu();
  }

  // « preparer » : le modèle plein. « apercu » : les trajectoires.
  montrer(vue) {
    this.modele.visible = vue === 'preparer';
    if (this.zones) this.zones.visible = vue === 'preparer';
    this.apercu.visible = vue === 'apercu';
    this.rafraichirManipulateurs?.();
    this.demanderRendu();
  }
}
