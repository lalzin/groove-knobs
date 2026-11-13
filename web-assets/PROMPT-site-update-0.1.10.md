# Prompt — lalzin.com : Groove Knobs 0.1.10 (Daft Chunk entre dans la suite)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> Suppose que la mise à jour 0.1.9 (`PROMPT-site-update-0.1.9.md` : Crackle, Head Spin, Ballroom) est faite.
> Sinon, la faire d'abord.

## Ce qui change
1. **Un nouveau plugin**, un **instrument**, en version **0.1.10** :
   - **Daft Chunk** (`daft-chunk`) : le découpeur de samples qui crée tes boucles. On glisse un sample
     (WAV, AIFF, FLAC, MP3) ; il trouve le tempo, le premier temps, la tonalité et les attaques, et le découpe
     en tranches jouées en MIDI comme des pads (à partir de Do1). **LOOP CREATOR** invente des boucles dans six
     styles, de ORDER à WILD, calées sur le tempo d'Ableton ; on garde les meilleures parmi 8, on les corrige
     pas à pas, et on les **glisse dans Ableton** en WAV, MIDI ou KIT. Effets SWEEP, PHASER, FLANGER, PUMP,
     CRUSH. 10 presets. Mascotte **Chunky**, le robot à tête d'écran (corail, crème, noir).
   - **Fenêtre large : 860 × 660** (plus grande que Head Spin et Kick Lock) ; ses `ui-*.webp` sont en
     1720 × 1320 (2x). Vérifier que la fiche l'accepte.
2. **Toute la suite passe en 0.1.10**, sans changement de son pour les autres plugins. L'installateur de la
   suite contient les onze plugins (`suite.includes`).
3. **Visuels :**
   - `daft-chunk/` : ui-hero, ui-hero@3x, galerie (`ui-slice`, `ui-fx`, `ui-sound`) avec légendes FR/EN dans
     `gallery`, mascot, 32 poses, og, card-square, icon-512.
   - `groove-knobs-banner.webp` / `groove-knobs-og.webp` refaites : Daft Chunk rejoint Head Spin au premier plan.
4. Téléchargements Windows : la release GitHub `v0.1.10`. Les tailles (`bytes`) sont à `null` dans
   `plugins.json` tant que les installateurs ne sont pas faits ; elles seront remplies dans un commit suivant
   du repo groove-knobs (recopier alors `plugins.json`).

## Tâches
### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/daft-chunk/og.png --out public/images/plugins/daft-chunk/og-email.jpg
```

### 2. Pages
- `/plugins` : la carte Daft Chunk (instrument), la suite « onze plugins », la nouvelle bannière.
- `/plugins/daft-chunk` :
  - hero (fenêtre large), `features`, les 10 presets, la galerie ;
  - un encadré **« LOOP CREATOR »** : choisis un style, tourne CHOP de ORDER (l'ordre du sample) à WILD
    (la découpe sauvage), clique sur LOOP CREATOR : une boucle naît, jouée au tempo d'Ableton. ENCORE en fait
    4 variantes ; l'étoile garde une boucle ;
  - un encadré **« Glisse dans Ableton »** : sous chaque boucle, WAV (l'audio, N mesures exactes), MIDI (les
    notes, à rejouer dans Daft Chunk) ou KIT (les tranches pour un Drum Rack) se glissent sur une piste ;
  - un conseil **« Instrument »** : sur une piste MIDI ; les tranches se jouent à partir de Do1 (C1).
- `/plugins/installation` : Daft Chunk est un instrument, comme Pew Pew et Slapper.

### 3. R2 (bucket `groove-knobs`) — je téléverse moi-même, donne-moi les commandes exactes
- macOS (dans `../groove-knobs/dist/`) : les onze `…-0.1.10-macOS.pkg` → `<slug>/0.1.10/<fichier>` :
  FrenchFilter, BoogieTrio, Backwash, PewPew, SingItBack, KickLock, Slapper, Crackle, HeadSpin, Ballroom,
  DaftChunk. `GrooveKnobs-0.1.10-macOS.pkg` → `suite/0.1.10/<fichier>`.
- Windows : les douze `…-0.1.10-Windows-Setup.exe` de https://github.com/lalzin/groove-knobs/releases/tag/v0.1.10
  (à télécharger d'abord), mêmes clés.

### 4. Vérifier
- `npm run build` ;
- `/plugins` et `/plugins/daft-chunk` : galerie (fenêtre large), affichage sur mobile ;
- `public/plugins/versions.json` en 0.1.10 partout, avec `daft-chunk`.
