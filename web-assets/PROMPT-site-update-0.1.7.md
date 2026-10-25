# Prompt — lalzin.com : Groove Knobs 0.1.7 (Crackle entre dans la suite)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> La section Plugins existe déjà (pages `/plugins`, `/plugins/[slug]`, `/plugins/installation`,
> API `request` / `download`, R2, Resend, `/admin`, `public/plugins/versions.json`, galeries `gallery`).
> Si la mise à jour 0.1.6 (`PROMPT-site-update-0.1.6.md`, Slapper) n'a pas été faite, fais-la d'abord.

## Ce qui change
1. **Un huitième plugin : Crackle** (`crackle`), version **0.1.7**, un **effet** : le grain lo-fi en un
   knob (GRAIN) — vinyle, cassette, saturation (six types), radio AM, bitcrush ; chaque effet réglable
   dans « ⋯ » (AUTO : il suit GRAIN). Mascotte **Hiss**, la cassette (lilas / encre / papier).
   Fenêtre 420 × 520 comme les autres.
2. **Toute la suite passe en 0.1.7** ; l'installateur de la suite contient les huit (`suite.includes`).
   Slapper : FUNK instantané, plus de notes oubliées en MIDI, nouveaux réglages « ⋯ ». Sing It Back :
   notation en lettres au choix. Boogie Trio : « ⋯ » avec 12 réglages, 15 presets (`features`,
   `presets` mis à jour dans `plugins.json`). French Filter : 16 presets. Pew Pew : « ⋯ » (WAVE + 16 réglages),
   pitch bend et molette, 18 presets.
3. Visuels : `crackle/` (ui-hero, ui-hero@3x, galerie `ui-effects`, `ui-radio`, `ui-crushed` avec
   légendes FR/EN dans `gallery`, mascot, poses, og, card-square, icon-512) ;
   `groove-knobs-banner.webp` / `groove-knobs-og.webp` : huit fenêtres.
4. Téléchargements Windows : la release GitHub `v0.1.7`.

## Tâches
### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/crackle/og.png --out public/images/plugins/crackle/og-email.jpg
```

### 2. Pages
- `/plugins` : la carte Crackle (effet), la suite « huit plugins ».
- `/plugins/crackle` : hero, `features`, presets, la **galerie** (légendes `gallery`), un encadré
  **« GRAIN et AUTO »** : GRAIN règle tout ; dans « ⋯ » chaque effet suit GRAIN tant que son voyant AUTO
  est allumé, le tourner le rend indépendant, un clic sur AUTO le rend à GRAIN ; NOISE = le niveau des bruits.

### 3. R2 (bucket `groove-knobs`) — je téléverse moi-même, donne-moi les commandes exactes
- macOS (dans `../groove-knobs/dist/`) : les huit `…-0.1.7-macOS.pkg` (FrenchFilter, BoogieTrio, Backwash,
  PewPew, SingItBack, KickLock, Slapper, Crackle) → `<slug>/0.1.7/<fichier>` ;
  `GrooveKnobs-0.1.7-macOS.pkg` → `suite/0.1.7/<fichier>`.
- Windows : les neuf `…-0.1.7-Windows-Setup.exe` de https://github.com/lalzin/groove-knobs/releases/tag/v0.1.7
  (à télécharger d'abord), mêmes clés.

### 4. Vérifier
`npm run build` ; `/plugins`, `/plugins/crackle` (galerie, mobile) ; une demande de téléchargement de Crackle
et de la suite de bout en bout ; `versions.json` sert 0.1.7 pour les huit.
