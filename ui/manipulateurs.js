// ArtPrint Slicer - licence AGPL v3
// Les manipulateurs de la vue 3D : ce qu'on attrape à la souris autour de la
// pièce pour la tourner (trois anneaux), la déplacer (deux flèches) ou la
// poser sur une de ses faces (les faces planes de son enveloppe, grisées).
// Ils s'ajoutent à la vue (voir vue3d.js) ; l'application choisit lequel
// montrer et reçoit le résultat par des fonctions de rappel :
//   vue.surRotation(axe, degres)      un anneau a été tourné puis lâché ;
//   vue.surRotationEnCours(axe, degres)  pendant qu'on le tourne ;
//   vue.surGlisser(dx, dy)            une flèche a été tirée puis lâchée ;
//   vue.surFace(normale)              une face a été choisie ;
//   la fonction donnée à vue.manipulerPlan : le plan a bougé ou tourné.
import * as THREE from 'three';
import { ConvexHull } from './lib/ConvexHull.js';

// Rouge pour X, vert pour Y, bleu pour Z : les couleurs habituelles des axes.
export const COULEURS_AXES = ['#d9342b', '#1fa77a', '#2f7de1'];
const AXES = [new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 1)];
const RAD = Math.PI / 180;
const GRIS_FACE = '#c3c8cf';   // les faces à poser : un gris clair, lisible sur le plateau noir

// La rotation du slicer : autour de X, puis de Y, puis de Z, depuis la pose du fichier.
const versQuaternion = (degres) => new THREE.Quaternion().setFromEuler(
  new THREE.Euler(degres[0] * RAD, degres[1] * RAD, degres[2] * RAD, 'ZYX'));
function versAngles(quaternion) {
  const e = new THREE.Euler().setFromQuaternion(quaternion, 'ZYX');
  return [e.x, e.y, e.z].map((a) => {
    const d = Math.round((a / RAD) * 100) / 100;
    return Object.is(d, -0) ? 0 : d;
  });
}

// La pose obtenue en tournant encore de « degres » autour d'un axe du plateau.
export function composerRotation(actuelle, axe, degres) {
  const tour = new THREE.Quaternion().setFromAxisAngle(AXES[axe], degres * RAD);
  return versAngles(tour.multiply(versQuaternion(actuelle)));
}

// La pose qui met la face de normale donnée contre le plateau.
export function rotationPourPoser(actuelle, normale) {
  const n = new THREE.Vector3(...normale).normalize();
  const tour = new THREE.Quaternion().setFromUnitVectors(n, new THREE.Vector3(0, 0, -1));
  return versAngles(tour.multiply(versQuaternion(actuelle)));
}

// Les faces planes de l'enveloppe de la pièce (ce qui touche une table quand
// on la pose) : leurs triangles, leur normale, leur aire.
function facesDeLEnveloppe(positions) {
  const total = positions.length / 3;
  // L'enveloppe se calcule sur un choix de sommets : un échantillon régulier,
  // et les sommets les plus avancés dans 200 directions (les coins).
  const choisis = new Set();
  const pas = Math.max(1, Math.floor(total / 40000));
  for (let i = 0; i < total; i += pas) choisis.add(i);
  const directions = 200, or = Math.PI * (3 - Math.sqrt(5));
  const meilleurs = new Float64Array(directions).fill(-Infinity), indices = new Int32Array(directions);
  const dx = new Float64Array(directions), dy = new Float64Array(directions), dz = new Float64Array(directions);
  for (let d = 0; d < directions; d++) {
    const z = 1 - (2 * (d + 0.5)) / directions, r = Math.sqrt(1 - z * z);
    dx[d] = r * Math.cos(or * d); dy[d] = r * Math.sin(or * d); dz[d] = z;
  }
  for (let i = 0; i < total; i++) {
    const x = positions[3 * i], y = positions[3 * i + 1], z = positions[3 * i + 2];
    for (let d = 0; d < directions; d++) {
      const v = x * dx[d] + y * dy[d] + z * dz[d];
      if (v > meilleurs[d]) { meilleurs[d] = v; indices[d] = i; }
    }
  }
  for (const i of indices) choisis.add(i);
  // (Sommets confondus : un seul suffit.)
  const vus = new Set(), points = [];
  for (const i of choisis) {
    const x = positions[3 * i], y = positions[3 * i + 1], z = positions[3 * i + 2];
    const cle = `${Math.round(x * 100)},${Math.round(y * 100)},${Math.round(z * 100)}`;
    if (!vus.has(cle)) { vus.add(cle); points.push(new THREE.Vector3(x, y, z)); }
  }
  if (points.length < 4) return [];
  const enveloppe = new ConvexHull().setFromPoints(points);
  const triangles = enveloppe.faces.map((face) => {
    const a = face.edge.head().point, b = face.edge.next.head().point, c = face.edge.next.next.head().point;
    return { normale: face.normal.clone(), aire: face.area, a, b, c };
  }).filter((t) => t.aire > 1e-6).sort((p, q) => q.aire - p.aire);
  // Les triangles d'un même plan forment une face.
  const taille = new THREE.Box3().setFromPoints(points).getSize(new THREE.Vector3()).length();
  const faces = [];
  for (const t of triangles) {
    const face = faces.find((f) => f.normale.dot(t.normale) > 0.9995 &&
      Math.abs(f.normale.dot(t.a) - f.cote) < 0.004 * taille + 0.02);
    if (face) { face.triangles.push(t); face.aire += t.aire; }
    else faces.push({ normale: t.normale.clone(), cote: t.normale.dot(t.a), aire: t.aire, triangles: [t] });
  }
  const aireTotale = faces.reduce((somme, f) => somme + f.aire, 0);
  return faces.filter((f) => f.aire > 0.012 * aireTotale).sort((p, q) => q.aire - p.aire).slice(0, 18);
}

export function installerManipulateurs(vue) {
  const groupe = new THREE.Group();
  vue.scene.add(groupe);
  let mode = null;          // 'tourner', 'deplacer', 'faces' ou rien
  let prises = [];          // ce qui s'attrape à la souris
  let action = null;        // le geste en cours
  let survol = null;        // la face sous la souris
  let facesCalculees = null, facesDe = null;
  // La poignée de plan : un point et une direction (voir vue.manipulerPlan).
  let plan = null;

  const viser = (e) => {
    const cadre = vue.canvas.getBoundingClientRect();
    const souris = new THREE.Vector2(((e.clientX - cadre.left) / cadre.width) * 2 - 1, -((e.clientY - cadre.top) / cadre.height) * 2 + 1);
    const rayon = new THREE.Raycaster();
    rayon.setFromCamera(souris, vue.camera);
    return rayon;
  };
  const centre = () => vue.boite.getCenter(new THREE.Vector3());
  const taille = () => vue.boite.getSize(new THREE.Vector3()).length();
  const jeton = (nom, repli) => vue.jeton(nom, repli);

  function vider() {
    for (const objet of [...groupe.children]) {
      groupe.remove(objet);
      objet.traverse((o) => { o.geometry?.dispose(); o.material?.dispose(); });
    }
    prises = [];
    survol = null;
  }

  function construire() {
    vider();
    groupe.visible = vue.modele.visible;
    if (!mode || !vue.boite) { vue.demanderRendu(); return; }
    const c = centre(), r = Math.max(10, 0.5 * taille());
    if (mode === 'tourner') {
      // Trois anneaux : rouge autour de X, vert autour de Y, bleu autour de Z.
      AXES.forEach((axe, i) => {
        const orienter = (objet) => {
          objet.position.copy(c);
          objet.quaternion.setFromUnitVectors(AXES[2], axe);
          return objet;
        };
        const anneau = orienter(new THREE.Mesh(new THREE.TorusGeometry(r, Math.max(0.25, 0.008 * r), 8, 128),
          new THREE.MeshBasicMaterial({ color: COULEURS_AXES[i], depthTest: false, transparent: true, opacity: 0.95 })));
        anneau.renderOrder = 8;
        // (Une prise plus épaisse que l'anneau dessiné : il s'attrape facilement.)
        const prise = orienter(new THREE.Mesh(new THREE.TorusGeometry(r, 0.06 * r, 6, 64), new THREE.MeshBasicMaterial({ visible: false })));
        prise.userData = { genre: 'anneau', axe: i, dessin: anneau };
        groupe.add(anneau, prise);
        prises.push(prise);
      });
    } else if (mode === 'deplacer') {
      // Deux flèches posées au pied de la pièce : X et Y.
      const pied = new THREE.Vector3(c.x, c.y, vue.boite.min.z + 0.3);
      [0, 1].forEach((i) => {
        const fleche = new THREE.ArrowHelper(AXES[i], pied, r, COULEURS_AXES[i], 0.2 * r, 0.1 * r);
        for (const morceau of [fleche.line, fleche.cone]) { morceau.material.depthTest = false; morceau.renderOrder = 8; }
        const prise = new THREE.Mesh(new THREE.CylinderGeometry(0.07 * r, 0.07 * r, r, 8), new THREE.MeshBasicMaterial({ visible: false }));
        prise.position.copy(pied).addScaledVector(AXES[i], 0.5 * r);
        prise.quaternion.setFromUnitVectors(AXES[1], AXES[i]);
        prise.userData = { genre: 'fleche', axe: i };
        groupe.add(fleche, prise);
        prises.push(prise);
      });
    } else if (mode === 'plan' && plan) {
      const p0 = plan.point, rp = Math.max(8, 0.32 * taille());
      // Le plan lui-même (pour une coupe) : un grand disque translucide et son bord.
      if (plan.montrer) {
        const etendue = 0.75 * taille();
        const disque = new THREE.Mesh(new THREE.CircleGeometry(etendue, 64), new THREE.MeshBasicMaterial({
          color: jeton('--c-accent', '#e4572e'), transparent: true, opacity: 0.22, side: THREE.DoubleSide, depthWrite: false }));
        disque.position.copy(p0);
        disque.quaternion.setFromUnitVectors(AXES[2], plan.normale);
        disque.renderOrder = 3;
        const normale = new THREE.ArrowHelper(plan.normale, p0, 0.9 * rp, jeton('--c-accent', '#e4572e'), 0.22 * rp, 0.12 * rp);
        for (const morceau of [normale.line, normale.cone]) { morceau.material.depthTest = false; morceau.renderOrder = 7; }
        groupe.add(disque, normale);
      }
      AXES.forEach((axe, i) => {
        // Une flèche par axe, pour glisser le point.
        const fleche = new THREE.ArrowHelper(axe, p0, 1.25 * rp, COULEURS_AXES[i], 0.18 * rp, 0.09 * rp);
        for (const morceau of [fleche.line, fleche.cone]) { morceau.material.depthTest = false; morceau.renderOrder = 8; }
        const tige = new THREE.Mesh(new THREE.CylinderGeometry(0.07 * rp, 0.07 * rp, 0.75 * rp, 8), new THREE.MeshBasicMaterial({ visible: false }));
        tige.position.copy(p0).addScaledVector(axe, 0.9 * rp);
        tige.quaternion.setFromUnitVectors(AXES[1], axe);
        tige.userData = { genre: 'plan-fleche', axe: i };
        // Un anneau par axe, pour tourner la direction.
        const orienter = (objet) => { objet.position.copy(p0); objet.quaternion.setFromUnitVectors(AXES[2], axe); return objet; };
        const anneau = orienter(new THREE.Mesh(new THREE.TorusGeometry(0.72 * rp, Math.max(0.2, 0.01 * rp), 8, 96),
          new THREE.MeshBasicMaterial({ color: COULEURS_AXES[i], depthTest: false, transparent: true, opacity: 0.9 })));
        anneau.renderOrder = 8;
        const prise = orienter(new THREE.Mesh(new THREE.TorusGeometry(0.72 * rp, 0.06 * rp, 6, 48), new THREE.MeshBasicMaterial({ visible: false })));
        prise.userData = { genre: 'plan-anneau', axe: i };
        groupe.add(fleche, tige, anneau, prise);
        prises.push(tige, prise);
      });
    } else if (mode === 'faces') {
      const maille = vue.modele.children[0];
      if (!maille) return;
      const positions = maille.geometry.getAttribute('position').array;
      if (facesDe !== positions) { facesCalculees = facesDeLEnveloppe(positions); facesDe = positions; }
      for (const face of facesCalculees) {
        // La face déjà contre le plateau n'est pas proposée.
        if (face.normale.z < -0.999) continue;
        const sommets = [];
        const decale = face.normale.clone().multiplyScalar(0.12);
        for (const t of face.triangles) for (const p of [t.a, t.b, t.c]) sommets.push(p.x + decale.x, p.y + decale.y, p.z + decale.z);
        const geometrie = new THREE.BufferGeometry();
        geometrie.setAttribute('position', new THREE.Float32BufferAttribute(sommets, 3));
        const plaque = new THREE.Mesh(geometrie, new THREE.MeshBasicMaterial({ color: GRIS_FACE,
          transparent: true, opacity: 0.5, side: THREE.DoubleSide, depthWrite: false }));
        plaque.renderOrder = 4;
        plaque.userData = { genre: 'face', normale: face.normale.toArray() };
        groupe.add(plaque);
        prises.push(plaque);
      }
    }
    vue.demanderRendu();
  }

  function eclairer(plaque) {
    if (survol === plaque) return;
    if (survol) { survol.material.color.set(GRIS_FACE); survol.material.opacity = 0.5; }
    survol = plaque;
    if (survol) { survol.material.color.set(jeton('--c-accent', '#e4572e')); survol.material.opacity = 0.75; }
    vue.canvas.style.cursor = survol ? 'pointer' : '';
    vue.demanderRendu();
  }

  // L'angle (en degrés) du point visé, autour de l'axe du geste.
  function angleVise(e, geste) {
    const point = new THREE.Vector3();
    if (!viser(e).ray.intersectPlane(geste.plan, point)) return null;
    point.sub(geste.centre);
    return Math.atan2(point.dot(geste.v), point.dot(geste.u)) / RAD;
  }

  vue.canvas.addEventListener('pointerdown', (e) => {
    if (!mode || !prises.length || e.button !== 0) return;
    const touche = viser(e).intersectObjects(prises)[0];
    if (!touche) return;
    const donnees = touche.object.userData;
    if (donnees.genre === 'face') { action = { genre: 'face', objet: touche.object, x: e.clientX, y: e.clientY }; return; }
    // Un anneau ou une flèche est attrapé : la vue ne tourne pas, la pièce ne se déplace pas autrement.
    e.stopImmediatePropagation();
    vue.controles.enabled = false;
    vue.canvas.setPointerCapture?.(e.pointerId);
    if (donnees.genre === 'plan-fleche') {
      // Le geste se mesure dans le plan qui contient l'axe et fait face à la caméra.
      const axe = AXES[donnees.axe];
      const regard = vue.camera.getWorldDirection(new THREE.Vector3());
      const face = new THREE.Vector3().crossVectors(axe, new THREE.Vector3().crossVectors(regard, axe)).normalize();
      action = { genre: 'plan-fleche', axe: donnees.axe, plan: new THREE.Plane().setFromNormalAndCoplanarPoint(face, touche.point),
        depart: touche.point.clone(), origine: plan.point.clone() };
      return;
    }
    if (donnees.genre === 'plan-anneau') {
      const axe = AXES[donnees.axe];
      action = { genre: 'plan-anneau', axe: donnees.axe, centre: plan.point.clone(), u: AXES[(donnees.axe + 1) % 3], v: AXES[(donnees.axe + 2) % 3],
        plan: new THREE.Plane().setFromNormalAndCoplanarPoint(axe, plan.point), origine: plan.normale.clone() };
      action.depart = angleVise(e, action);
      if (action.depart === null) { action = null; vue.controles.enabled = true; }
      return;
    }
    const c = centre();
    if (donnees.genre === 'anneau') {
      const axe = AXES[donnees.axe];
      const u = AXES[(donnees.axe + 1) % 3], v = AXES[(donnees.axe + 2) % 3];
      action = { genre: 'anneau', axe: donnees.axe, centre: c, u, v,
        plan: new THREE.Plane().setFromNormalAndCoplanarPoint(axe, c), degres: 0 };
      // Vue presque dans le plan de l'anneau : on mesure le geste à l'écran, c'est plus sûr.
      action.depart = angleVise(e, action);
      if (action.depart === null) { action = null; vue.controles.enabled = true; }
    } else {
      action = { genre: 'fleche', axe: donnees.axe, plan: new THREE.Plane(AXES[2], -touche.point.z), depart: touche.point.clone(), distance: 0 };
    }
  }, { capture: true });

  vue.canvas.addEventListener('pointermove', (e) => {
    if (!action) {
      if (mode === 'faces' && prises.length) eclairer(viser(e).intersectObjects(prises)[0]?.object ?? null);
      return;
    }
    if (action.genre === 'plan-fleche') {
      e.stopImmediatePropagation();
      const point = new THREE.Vector3();
      if (!viser(e).ray.intersectPlane(action.plan, point)) return;
      const distance = Math.round(point.sub(action.depart).dot(AXES[action.axe]) * 10) / 10;
      plan.point.copy(action.origine).addScaledVector(AXES[action.axe], distance);
      construire();
      plan.surChange?.(plan.point.toArray(), plan.normale.toArray(), false);
      return;
    }
    if (action.genre === 'plan-anneau') {
      e.stopImmediatePropagation();
      const angle = angleVise(e, action);
      if (angle === null) return;
      let degres = ((angle - action.depart + 540) % 360) - 180;
      degres = e.shiftKey ? Math.round(degres / 15) * 15 : Math.round(degres);
      plan.normale.copy(action.origine).applyAxisAngle(AXES[action.axe], degres * RAD).normalize();
      construire();
      plan.surChange?.(plan.point.toArray(), plan.normale.toArray(), false);
      return;
    }
    if (action.genre === 'anneau') {
      e.stopImmediatePropagation();
      const angle = angleVise(e, action);
      if (angle === null) return;
      let degres = ((angle - action.depart + 540) % 360) - 180;
      // Par pas de 1° ; avec la touche Maj, par pas de 15°.
      degres = e.shiftKey ? Math.round(degres / 15) * 15 : Math.round(degres);
      action.degres = degres;
      const tour = new THREE.Quaternion().setFromAxisAngle(AXES[action.axe], degres * RAD);
      vue.modele.quaternion.copy(tour);
      vue.modele.position.copy(action.centre).sub(action.centre.clone().applyQuaternion(tour));
      vue.surRotationEnCours?.(action.axe, degres);
      vue.demanderRendu();
    } else if (action.genre === 'fleche') {
      e.stopImmediatePropagation();
      const point = new THREE.Vector3();
      if (!viser(e).ray.intersectPlane(action.plan, point)) return;
      action.distance = Math.round(point.sub(action.depart).dot(AXES[action.axe]) * 10) / 10;
      vue.modele.position.copy(AXES[action.axe]).multiplyScalar(action.distance);
      groupe.position.copy(vue.modele.position);
      vue.demanderRendu();
    }
  }, { capture: true });

  const lacher = (e) => {
    if (!action) return;
    const geste = action;
    action = null;
    if (geste.genre === 'face') {
      // Un clic sans glisser choisit la face.
      if (Math.hypot(e.clientX - geste.x, e.clientY - geste.y) <= 5) vue.surFace?.(geste.objet.userData.normale);
      return;
    }
    e.stopImmediatePropagation();
    vue.controles.enabled = true;
    groupe.position.set(0, 0, 0);
    if (geste.genre === 'plan-fleche' || geste.genre === 'plan-anneau') {
      plan?.surChange?.(plan.point.toArray(), plan.normale.toArray(), true);
      return;
    }
    if (geste.genre === 'anneau') {
      if (geste.degres) vue.surRotation?.(geste.axe, geste.degres);
      else vue.oublierGeste();
    } else if (geste.distance) {
      vue.surGlisser?.(geste.axe === 0 ? geste.distance : 0, geste.axe === 1 ? geste.distance : 0);
    } else vue.oublierGeste();
  };
  vue.canvas.addEventListener('pointerup', lacher, { capture: true });
  vue.canvas.addEventListener('pointercancel', () => { if (action) { action = null; vue.controles.enabled = true; vue.oublierGeste(); } }, { capture: true });

  // Choisit le manipulateur montré : 'tourner', 'deplacer', 'faces' ou rien.
  vue.manipuler = (nouveau) => {
    mode = nouveau || null;
    if (mode !== 'plan') plan = null;
    if (!mode) vue.canvas.style.cursor = '';
    construire();
  };
  // Montre la poignée de plan : trois flèches pour glisser le point, trois
  // anneaux pour tourner la direction. « surChange(point, normale, fini) » est
  // appelée pendant le geste (fini : faux) puis quand il est lâché (fini : vrai).
  // « montrer » : vrai pour dessiner aussi le plan (une coupe).
  vue.manipulerPlan = (point, normale, surChange, montrer = false) => {
    plan = { point: new THREE.Vector3(...point), normale: new THREE.Vector3(...normale).normalize(), surChange, montrer };
    mode = 'plan';
    construire();
  };
  // Déplace la poignée de plan sans la recréer (les champs du panneau ont changé).
  vue.placerPlan = (point, normale) => {
    if (!plan) return;
    plan.point.set(...point);
    plan.normale.set(...normale).normalize();
    if (mode === 'plan') construire();
  };
  vue.manipulateur = () => mode;
  vue.nombreDePrises = () => prises.length;
  // À appeler quand la pièce a changé (ou sa visibilité) : les manipulateurs la suivent.
  vue.rafraichirManipulateurs = construire;
  // Remet la pièce comme avant un geste abandonné.
  vue.oublierGeste = () => {
    vue.modele.quaternion.identity();
    vue.modele.position.set(0, 0, 0);
    groupe.position.set(0, 0, 0);
    vue.demanderRendu();
  };

  // Le repère des axes, vu depuis la caméra : pour chaque point donné dans le
  // repère du plateau, sa place à l'écran (x vers la droite, y vers le haut)
  // et sa profondeur (plus grand : plus près de l'œil).
  vue.versEcran = (x, y, z) => {
    const m = vue.camera.matrixWorld.elements;
    return [x * m[0] + y * m[1] + z * m[2], x * m[4] + y * m[5] + z * m[6], x * m[8] + y * m[9] + z * m[10]];
  };
}
