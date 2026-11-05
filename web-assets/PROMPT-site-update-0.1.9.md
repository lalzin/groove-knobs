# Prompt — lalzin.com : Groove Knobs 0.1.9 (Head Spin et Ballroom entrent dans la suite)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> La section Plugins existe déjà (pages `/plugins`, `/plugins/[slug]`, `/plugins/installation`,
> API `request` / `download`, R2, Resend, `/admin`, `public/plugins/versions.json`, galeries `gallery`).
> La 0.1.8 n'a pas été publiée : ce prompt la remplace (`PROMPT-site-update-0.1.8.md` est à ignorer).
> Si la mise à jour 0.1.7 (`PROMPT-site-update-0.1.7.md`, Crackle) n'a pas été faite, fais-la d'abord.

## Ce qui change
1. **Deux nouveaux plugins**, tous deux des **effets**, en version **0.1.9** :
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
   Les autres plugins ne changent pas de son.
3. **Visuels :**
   - `head-spin/` et `ballroom/` : ui-hero, ui-hero@3x, galerie avec légendes FR/EN dans `gallery`, mascot,
     poses, og, card-square, icon-512.
   - `groove-knobs-banner.webp` / `groove-knobs-og.webp` refaites : les neuf fenêtres en portrait forment un arc,
     et Head Spin est au premier plan, au centre.
4. Téléchargements Windows : la release GitHub `v0.1.9`.

## Tâches
### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
for s in head-spin ballroom; do sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/$s/og.png --out public/images/plugins/$s/og-email.jpg; done
```

### 2. Pages
- `/plugins` : les cartes Head Spin et Ballroom (effets), la suite « dix plugins », la nouvelle bannière.
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
- `/plugins`, `/plugins/head-spin`, `/plugins/ballroom` : galeries, et affichage sur mobile ;
- une demande de téléchargement de Ballroom, puis de la suite, de bout en bout ;
- `versions.json` sert 0.1.9 pour les dix plugins.
