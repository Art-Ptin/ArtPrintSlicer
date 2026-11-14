// ArtPrint Slicer - licence AGPL v3
// Dessine l'icône du logiciel (design/icone.svg) en images PNG de plusieurs
// tailles. « fabriquer_icone.py » les assemble ensuite en fichier .ico.
//
//   node outils/fabriquer_icone.js <dossier_de_sortie>
//
// Demande Playwright (un navigateur sans fenêtre fait le dessin).
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const TAILLES = [16, 20, 24, 32, 40, 48, 64, 128, 256];
(async () => {
  const sortie = process.argv[2];
  if (!sortie) { console.error('Utilisation : node outils/fabriquer_icone.js <dossier_de_sortie>'); process.exit(2); }
  fs.mkdirSync(sortie, { recursive: true });
  const svg = fs.readFileSync(path.join(__dirname, '..', 'design', 'icone.svg'), 'utf8');
  const navigateur = await chromium.launch();
  for (const taille of TAILLES) {
    const page = await navigateur.newPage({ viewport: { width: taille, height: taille }, deviceScaleFactor: 1 });
    await page.setContent(`<style>html,body{margin:0;background:transparent}svg{display:block;width:${taille}px;height:${taille}px}</style>${svg}`);
    await page.screenshot({ path: path.join(sortie, `icone-${taille}.png`), omitBackground: true });
    await page.close();
  }
  await navigateur.close();
  console.log(`${TAILLES.length} images écrites dans ${sortie}`);
})();
