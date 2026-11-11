// ArtPrint Slicer - licence AGPL v3
// Menu déroulant : s'ouvre sous un bouton, se ferme au clic ailleurs ou avec Échap.

let fermerCourant = null;

// « elements » : liste de { id, libelle, action, note, choisi }, ou null pour un trait.
export function ouvrirMenu(ancre, elements) {
  const dejaOuvert = ancre.getAttribute('aria-expanded') === 'true';
  fermerCourant?.();
  if (dejaOuvert) return;

  const menu = document.createElement('div');
  menu.className = 'menu';
  menu.setAttribute('role', 'menu');
  for (const element of elements) {
    if (!element) { menu.append(document.createElement('hr')); continue; }
    const bouton = document.createElement('button');
    bouton.setAttribute('role', 'menuitem');
    if (element.id) bouton.id = element.id;
    if (element.choisi) bouton.className = 'choisi';
    const libelle = document.createElement('span');
    libelle.textContent = element.libelle;
    bouton.append(libelle);
    if (element.note) {
      const note = document.createElement('span');
      note.className = 'note';
      note.textContent = element.note;
      bouton.append(note);
    }
    bouton.addEventListener('click', () => { fermer(); element.action?.(); });
    menu.append(bouton);
  }
  document.body.append(menu);

  // Sous le bouton, aligné à gauche ; à droite s'il déborderait de la fenêtre.
  const cadre = ancre.getBoundingClientRect();
  const gauche = Math.min(cadre.left, window.innerWidth - menu.offsetWidth - 8);
  menu.style.left = `${Math.max(8, gauche)}px`;
  menu.style.top = `${cadre.bottom}px`;
  ancre.setAttribute('aria-expanded', 'true');

  const dehors = (e) => { if (!menu.contains(e.target) && !ancre.contains(e.target)) fermer(); };
  const touche = (e) => { if (e.key === 'Escape') { fermer(); ancre.focus(); } };
  function fermer() {
    menu.remove();
    ancre.setAttribute('aria-expanded', 'false');
    document.removeEventListener('pointerdown', dehors, true);
    document.removeEventListener('keydown', touche);
    window.removeEventListener('resize', fermer);
    if (fermerCourant === fermer) fermerCourant = null;
  }
  document.addEventListener('pointerdown', dehors, true);
  document.addEventListener('keydown', touche);
  window.addEventListener('resize', fermer);
  fermerCourant = fermer;
  menu.querySelector('button')?.focus();
}
