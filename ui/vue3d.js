// ArtPrint Slicer - licence AGPL v3
// La vue 3D : le plateau, le volume d'impression, le modèle, et l'aperçu des
// trajectoires (cordons dessinés à leur vraie largeur).
import * as THREE from 'three';
import { OrbitControls } from './lib/OrbitControls.js';
import { LineSegments2 } from './lib/lines/LineSegments2.js';
import { LineSegmentsGeometry } from './lib/lines/LineSegmentsGeometry.js';
import { LineMaterial } from './lib/lines/LineMaterial.js';

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
    this.controles.addEventListener('change', () => this.demanderRendu());

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
    this.hauteur = 281;
    this.sombre = window.matchMedia('(prefers-color-scheme: dark)');
    this.sombre.addEventListener('change', () => this.rafraichirTheme());
    // Un thème choisi explicitement (attribut data-theme) l'emporte sur celui du système.
    new MutationObserver(() => this.rafraichirTheme())
      .observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });

    this.definirVolume(this.rayon, this.hauteur);
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

  // « positions » : 9 nombres par triangle, modèle déjà posé sur le plateau.
  afficherModele(positions) {
    this.vider(this.modele);
    this.viderApercu();
    const geometrie = new THREE.BufferGeometry();
    geometrie.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    geometrie.computeVertexNormals();
    geometrie.computeBoundingBox();
    this.boite = geometrie.boundingBox.clone();
    this.modele.add(new THREE.Mesh(geometrie, new THREE.MeshStandardMaterial({
      color: this.jeton('--c-piece', '#2aa57b'), roughness: 0.6, metalness: 0.05, flatShading: true })));
    this.montrer('preparer');
    this.pointDeVue('iso');
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
      iso: [0.55, -0.72, 0.5],
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
    this.apercu.visible = vue === 'apercu';
    this.demanderRendu();
  }
}
