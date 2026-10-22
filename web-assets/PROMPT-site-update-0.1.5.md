# Prompt — lalzin.com : Groove Knobs 0.1.5 (toute la suite, Kick Lock dedans)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> La section Plugins existe déjà (pages `/plugins`, `/plugins/[slug]`, `/plugins/installation`,
> API `request` / `download`, R2, Resend, `/admin`, `public/plugins/versions.json`).
> Si la mise à jour « Kick Lock 0.1.4 » (`PROMPT-site-update-2026-10-22.md`) n'a pas été faite,
> fais d'abord sa partie « Pages » (la page Kick Lock), puis celle-ci.

## Ce qui change
1. **Toute la suite passe en 0.1.5** : les six plugins (French Filter, Boogie Trio, Backwash, Pew Pew,
   Sing It Back, **Kick Lock + Kick Lock Send**) et l'installateur de la suite, qui contient maintenant
   les six (Kick Lock avec Kick Lock Send).
2. **Kick Lock** : mode **PRECISE** par défaut (aucune détection de note, toute la collision vue à
   l'avance, latence ~360 ms compensée par le DAW : pour le mixage) ; **LIVE** (32 ms) pour jouer ;
   **PHASE** éteint = un sidechain simple pour n'importe quelle piste ; **MONO BELOW**. Nouvelle
   description, nouvelles `features`, `latency_note`, et une **galerie** (`gallery` : fichier + légende
   FR/EN) — à afficher sur `/plugins/kick-lock`.
3. **Sing It Back** : un chœur à l'unisson par défaut (texte inchangé dans `plugins.json`).
4. Les téléchargements Windows pointent vers la release GitHub `v0.1.5` ; la suite n'a plus de
   `includes_note` mais `includes` (les six).

## Tâches
### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/kick-lock/og.png --out public/images/plugins/kick-lock/og-email.jpg
```
Champs nouveaux à ajouter au type `Plugin` (optionnels) : `gallery?: { file: string; caption: { fr: string; en: string } }[]`,
`latency_note?: { fr: string; en: string }`, `suite.includes?: string[]`.

### 2. Pages
- `/plugins/kick-lock` : la galerie (`gallery`, format paysage 1520 × 960 sauf `ui-send` en portrait),
  la `latency_note` près du bouton de téléchargement.
- `/plugins` et la page de la suite : 0.1.5, la suite « six plugins » (`suite.includes`).

### 3. R2 (bucket `groove-knobs`) — je téléverse moi-même, donne-moi les commandes exactes
- macOS (dans `../groove-knobs/dist/`) : `FrenchFilter-0.1.5-macOS.pkg`, `BoogieTrio-0.1.5-macOS.pkg`,
  `Backwash-0.1.5-macOS.pkg`, `PewPew-0.1.5-macOS.pkg`, `SingItBack-0.1.5-macOS.pkg`, `KickLock-0.1.5-macOS.pkg`
  → `<slug>/0.1.5/<fichier>` ; `GrooveKnobs-0.1.5-macOS.pkg` → `suite/0.1.5/<fichier>`.
- Windows : les sept `…-0.1.5-Windows-Setup.exe` de https://github.com/lalzin/groove-knobs/releases/tag/v0.1.5
  (à télécharger d'abord), mêmes clés.

### 4. Vérifier
`npm run build` ; `/plugins`, `/plugins/kick-lock` (galerie, mobile), `/plugins/installation` ; une demande de
téléchargement de la suite et de Kick Lock de bout en bout ; `versions.json` sert 0.1.5 pour les six.
