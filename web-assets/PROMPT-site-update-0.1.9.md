# Prompt — lalzin.com : Groove Knobs 0.1.9 (Crackle, Head Spin et Ballroom entrent dans la suite)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> La section Plugins existe déjà (pages `/plugins`, `/plugins/[slug]`, `/plugins/installation`,
> API `request` / `download`, R2, Resend, `/admin`, `public/plugins/versions.json`, galeries `gallery`).
> Ce prompt remplace les mises à jour 0.1.7 (Crackle) et 0.1.8 (Head Spin), jamais faites sur le site :
> `PROMPT-site-update-0.1.7.md` et `PROMPT-site-update-0.1.8.md` sont à ignorer. Le site est en 0.1.6,
> avec sept plugins, Slapper compris.

## Ce qui change
1. **Trois nouveaux plugins**, tous des **effets**, en version **0.1.9** :
   - **Crackle** (`crackle`) : le grain lo-fi en un knob, GRAIN, avec vinyle, cassette, saturation (six types),
     radio AM et bitcrush. Chaque effet se règle dans « ⋯ » ; AUTO lui fait suivre GRAIN. Mascotte **Hiss**,
     la cassette (lilas, encre, papier). Fenêtre 420 × 520, comme les autres.
   - **Head Spin** (`head-spin`) : un panoramique circulaire. Le son tourne autour de la tête, vue de dessus,
     sur une forme : rond, ovale, triangle, carré, huit, étoile, ou un dessin libre au crayon. Il est calé sur
     le tempo ou en Hz, avec MIX en knob principal. Rendu enceintes ou casque (binaural), avec 3D, GROOVE et
     RELANCE. Mascotte **Kiko**, le b-boy. **Fenêtre large : 760 × 480**, comme Kick Lock ; ses `ui-*.webp`
     sont à ce format.
   - **Ballroom** (`ballroom`) : une réverbe algorithmique, avec SPACE comme knob principal pour le dry / wet.
     SPACE à 0 % ne met aucune réverbe ; à 100 %, on n'entend que la réverbe, au niveau du son d'origine.
     Sept salles, early et late séparés, FREEZE, 22 presets. Mascotte **Mira**, la boule disco (nuit, argent,
     or, champagne).
     **CLEAR** est la signature de Ballroom : la queue s'efface toute seule, bande par bande, là où la musique
     joue, et revient dans les silences. Fenêtre en portrait (420 × 520), comme les autres plugins.
2. **Toute la suite passe en 0.1.9.** L'installateur de la suite contient les dix plugins (`suite.includes`).
   Changements des autres plugins depuis la 0.1.6 (venus avec la 0.1.7 ; `features` et `presets` sont à jour
   dans `plugins.json`) :
   - French Filter : 16 presets.
   - Boogie Trio : « ⋯ » avec 12 réglages, 15 presets.
   - Pew Pew : « ⋯ » (WAVE et 16 réglages), pitch bend et molette, 18 presets.
   - Kick Lock : S-curve et MIX 100 % par défaut.
   - Slapper : FUNK instantané, plus de notes oubliées en MIDI, nouveaux réglages dans « ⋯ ».
   - Sing It Back : notation en lettres ou en solfège, au choix.
   - Chaque application a l'icône de sa mascotte.
3. **Visuels :**
   - `crackle/`, `head-spin/` et `ballroom/` : ui-hero, ui-hero@3x, galerie avec légendes FR/EN dans
     `gallery`, mascot, poses, og, card-square, icon-512.
   - `groove-knobs-banner.webp` / `groove-knobs-og.webp` refaites : les neuf fenêtres en portrait forment un arc,
     et Head Spin est au premier plan, au centre.
4. Téléchargements Windows : la release GitHub `v0.1.9`.

## Tâches
### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
for s in crackle head-spin ballroom; do sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/$s/og.png --out public/images/plugins/$s/og-email.jpg; done
```

### 2. Pages
- `/plugins` : les cartes Crackle, Head Spin et Ballroom (effets), la suite « dix plugins », la nouvelle bannière.
- `/plugins/crackle` :
  - hero, `features`, presets, la galerie ;
  - un encadré **« GRAIN et AUTO »** : GRAIN règle tout. Dans « ⋯ », chaque effet suit GRAIN tant que son voyant
    AUTO est allumé ; le tourner le rend indépendant, et un clic sur AUTO le rend à GRAIN. NOISE règle le niveau
    des bruits.
- `/plugins/head-spin` :
  - hero : la fenêtre est large (760 × 480) ; vérifier que la mise en page de la fiche l'accepte, comme pour Kick Lock ;
  - `features`, presets, la galerie ;
  - un encadré **« Dessine ta forme »** : clic sur le crayon, puis trace le chemin autour de Kiko d'un seul geste ;
  - un conseil **« Enceintes ou casque »**.
- `/plugins/ballroom` :
  - hero, `features`, les 22 presets, la galerie ;
  - un encadré **« CLEAR, la réverbe qui s'efface »** : sur une voix ou une boucle, monte CLEAR ; la queue recule
    pendant que la musique joue et fleurit dans les silences, bande par bande ;
  - un conseil **« En insert ou en retour »** : en insert, SPACE règle le dry / wet ; sur une piste de retour,
    SPACE à 100 %.

### 3. R2 (bucket `groove-knobs`) — je téléverse moi-même, donne-moi les commandes exactes
- macOS (dans `../groove-knobs/dist/`) : les dix `…-0.1.9-macOS.pkg` → `<slug>/0.1.9/<fichier>` :
  FrenchFilter, BoogieTrio, Backwash, PewPew, SingItBack, KickLock, Slapper, Crackle, HeadSpin, Ballroom.
  `GrooveKnobs-0.1.9-macOS.pkg` → `suite/0.1.9/<fichier>`.
- Windows : les onze `…-0.1.9-Windows-Setup.exe` de https://github.com/lalzin/groove-knobs/releases/tag/v0.1.9
  (à télécharger d'abord), mêmes clés.

### 4. Vérifier
- `npm run build` ;
- `/plugins`, `/plugins/crackle`, `/plugins/head-spin`, `/plugins/ballroom` : galeries, et affichage sur mobile ;
- une demande de téléchargement de Crackle, de Ballroom, puis de la suite, de bout en bout ;
- `versions.json` sert 0.1.9 pour les dix plugins.
