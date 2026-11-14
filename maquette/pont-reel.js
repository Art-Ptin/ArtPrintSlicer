// ArtPrint Slicer - licence AGPL v3
// Le pont entre la page et le cœur du slicer (C++).
// Dans le logiciel installé, les demandes passent par WebView2.
// Pendant les essais dans un navigateur, elles passent par une adresse /api.

// Faux : les réponses viennent du vrai slicer.
export const simulation = false;

const natif = Boolean(window.chrome && window.chrome.webview);
const enAttente = new Map();
let prochainNumero = 1;

if (natif) {
  window.chrome.webview.addEventListener('message', (evenement) => {
    const { id, reponse } = evenement.data;
    const resoudre = enAttente.get(id);
    if (resoudre) {
      enAttente.delete(id);
      resoudre(reponse);
    }
  });
}

// Envoie une commande et rend sa réponse : { ok: true, ... } ou
// { ok: false, erreur: "message" }.
export function appeler(commande, parametres = {}) {
  const demande = { commande, ...parametres };
  if (natif) {
    return new Promise((resoudre) => {
      const id = prochainNumero++;
      enAttente.set(id, resoudre);
      window.chrome.webview.postMessage({ id, demande });
    });
  }
  return fetch('/api', { method: 'POST', body: JSON.stringify(demande) })
    .then((reponse) => reponse.json())
    .catch((erreur) => ({ ok: false, erreur: `Le slicer ne répond pas : ${erreur.message}` }));
}
