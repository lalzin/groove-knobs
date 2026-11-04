# Prompt — lalzin.com : Groove Knobs 0.1.8 (Head Spin entre dans la suite)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> La section Plugins existe déjà (pages `/plugins`, `/plugins/[slug]`, `/plugins/installation`,
> API `request` / `download`, R2, Resend, `/admin`, `public/plugins/versions.json`, galeries `gallery`).
> Si la mise à jour 0.1.7 (`PROMPT-site-update-0.1.7.md`, Crackle) n'a pas été faite, fais-la d'abord.

## Ce qui change
1. **Un neuvième plugin : Head Spin** (`head-spin`), version **0.1.8**, un **effet** : un panoramique
   circulaire — le son tourne autour de la tête, vue de dessus, sur une forme (rond, ovale, triangle, carré,
   huit, étoile, ou un dessin libre au crayon), calé sur le tempo ou en Hz ; MIX en knob principal ; rendu
   enceintes ou casque (binaural), 3D, GROOVE, RELANCE. Mascotte **Kiko**, le b-boy (rose casquette, marine,
   bleu casque, blanc). **Fenêtre plus large : 760 × 480** (comme Kick Lock) — les visuels `ui-*.webp`
   sont à ce format.
2. **Toute la suite passe en 0.1.8** ; l'installateur de la suite contient les neuf (`suite.includes`).
   Les autres plugins ne changent pas de son.
3. Visuels : `head-spin/` (ui-hero, ui-hero@3x, galerie `ui-eight`, `ui-custom`, `ui-behind` avec légendes
   FR/EN dans `gallery`, mascot, poses, og, card-square, icon-512) ;
   `groove-knobs-banner.webp` / `groove-knobs-og.webp` : neuf fenêtres.
4. Téléchargements Windows : la release GitHub `v0.1.8`.

## Tâches
### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/head-spin/og.png --out public/images/plugins/head-spin/og-email.jpg
```

### 2. Pages
- `/plugins` : la carte Head Spin (effet), la suite « neuf plugins ».
- `/plugins/head-spin` : hero (fenêtre large 760 × 480 : vérifier que la mise en page de la fiche
  l'accepte, comme pour Kick Lock), `features`, presets, la **galerie** (légendes `gallery`), un encadré
  **« Dessine ta forme »** : clic sur le crayon, trace le chemin autour de Kiko d'un seul geste, il devient
  la forme CUSTOM ; et un conseil **« Enceintes ou casque »** : RENDER Casque pour le binaural, 3D pour
  pousser l'avant / l'arrière.

### 3. R2 (bucket `groove-knobs`) — je téléverse moi-même, donne-moi les commandes exactes
- macOS (dans `../groove-knobs/dist/`) : les neuf `…-0.1.8-macOS.pkg` (FrenchFilter, BoogieTrio, Backwash,
  PewPew, SingItBack, KickLock, Slapper, Crackle, HeadSpin) → `<slug>/0.1.8/<fichier>` ;
  `GrooveKnobs-0.1.8-macOS.pkg` → `suite/0.1.8/<fichier>`.
- Windows : les dix `…-0.1.8-Windows-Setup.exe` de https://github.com/lalzin/groove-knobs/releases/tag/v0.1.8
  (à télécharger d'abord), mêmes clés.

### 4. Vérifier
`npm run build` ; `/plugins`, `/plugins/head-spin` (galerie, mobile) ; une demande de téléchargement de
Head Spin et de la suite de bout en bout ; `versions.json` sert 0.1.8 pour les neuf.
