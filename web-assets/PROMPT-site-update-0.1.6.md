# Prompt — lalzin.com : Groove Knobs 0.1.6 (Slapper entre dans la suite)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> La section Plugins existe déjà (pages `/plugins`, `/plugins/[slug]`, `/plugins/installation`,
> API `request` / `download`, R2, Resend, `/admin`, `public/plugins/versions.json`, galerie `gallery`
> de Kick Lock). Si la mise à jour 0.1.5 (`PROMPT-site-update-0.1.5.md`) n'a pas été faite, fais-la
> d'abord.

## Ce qui change
1. **Un septième plugin : Slapper** (`slapper`), version **0.1.6**, un **instrument** (comme Pew Pew) :
   la basse funk en un knob. Une basse modélisée (pas de samples) et trois basses synthé (ENGINE :
   STRING, ANALOG, FM, PULSE), 24 presets. Mascotte **Pluck**, la basse sunburst (ambre / vin / brun).
   Fenêtre 420 × 520 comme les autres (visuels au format portrait habituel).
2. **Toute la suite passe en 0.1.6** (les six autres sans changement de son) ; l'installateur de la
   suite contient les sept (`suite.includes`).
3. Visuels : `slapper/` (ui-hero, ui-hero@3x, galerie `ui-analog`, `ui-fm`, `ui-pulse` avec légendes
   FR/EN dans `gallery`, mascot, poses, og, card-square, icon-512) ; `groove-knobs-banner.webp` et
   `groove-knobs-og.webp` montrent maintenant sept fenêtres.
4. Téléchargements Windows : la release GitHub `v0.1.6`.

## Tâches
### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/slapper/og.png --out public/images/plugins/slapper/og-email.jpg
```
(si `og.png` manque : `build/tests/slapper/sl_screenshots_artefacts/Release/sl_screenshots --web web-assets` dans `../groove-knobs`)

### 2. Pages
- `/plugins` : la carte Slapper (badge « instrument » comme Pew Pew), la suite « sept plugins ».
- `/plugins/slapper` : hero, `features`, la liste des presets, la **galerie** (les quatre moteurs, une
  image chacun, légendes `gallery`), et un encadré **« Jouer »** : piste MIDI ; en MONO, une note jouée
  par-dessus une autre glisse vers elle ; vélocité très douce (< 20) = ghost note ; ENGINE à gauche de
  Pluck.
- `/plugins/installation` : Slapper est un instrument (piste MIDI), comme Pew Pew.

### 3. R2 (bucket `groove-knobs`) — je téléverse moi-même, donne-moi les commandes exactes
- macOS (dans `../groove-knobs/dist/`) : `FrenchFilter-0.1.6-macOS.pkg`, `BoogieTrio-0.1.6-macOS.pkg`,
  `Backwash-0.1.6-macOS.pkg`, `PewPew-0.1.6-macOS.pkg`, `SingItBack-0.1.6-macOS.pkg`,
  `KickLock-0.1.6-macOS.pkg`, `Slapper-0.1.6-macOS.pkg` → `<slug>/0.1.6/<fichier>` ;
  `GrooveKnobs-0.1.6-macOS.pkg` → `suite/0.1.6/<fichier>`.
- Windows : les huit `…-0.1.6-Windows-Setup.exe` de https://github.com/lalzin/groove-knobs/releases/tag/v0.1.6
  (à télécharger d'abord), mêmes clés.

### 4. Vérifier
`npm run build` ; `/plugins`, `/plugins/slapper` (galerie, mobile), `/plugins/installation` ; une demande
de téléchargement de Slapper et de la suite de bout en bout (e-mail avec `og-email.jpg`, lien R2) ;
`versions.json` sert 0.1.6 pour les sept.
