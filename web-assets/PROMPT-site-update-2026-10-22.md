# Prompt — lalzin.com : Kick Lock 0.1.4 (nouveau plugin, sorti seul)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> La section Plugins existe déjà (pages `/plugins`, `/plugins/[slug]`, `/plugins/installation`,
> API `request` / `download`, R2, Resend, `/admin`, `public/plugins/versions.json`). On ajoute
> Kick Lock, on ne refait rien. Données et visuels : `../groove-knobs/web-assets/`.

---

## Ce qui change

1. **Un sixième plugin : Kick Lock** (`kick-lock`), version **0.1.4**, sorti **seul** (les cinq
   autres plugins et la suite restent en 0.1.2, inchangés).
   - Sur la piste de **basse** : ducking de sidechain (courbe à dessiner) + **LOCK**, la phase du sub
     calée sur chaque kick. Mascotte **Phaze**, le tambour de kick (vert lime).
   - Livré avec **Kick Lock Send** (même installateur), à poser sur la piste du **kick** : il envoie
     les kicks à Kick Lock sans aucun routage (même numéro LINK des deux côtés).
   - Latence ~32 ms (compensée par le DAW).
2. **Sa fenêtre est large (760 × 480)**, pas 420 × 520 comme les autres : `ui-hero.webp` fait
   1520 × 960 (`ui-hero@3x.webp` 2280 × 1440). Ne pas la forcer dans le cadre portrait des autres
   cartes : une carte / un hero au format paysage pour ce plugin (voir `images_note` dans
   `plugins.json`).
3. Visuels en plus pour sa page : `ui-curve.webp` (la courbe de ducking dessinée),
   `ui-explain.webp` (l'écran qui explique un réglage), `ui-send.webp` / `ui-send@3x.webp`
   (la fenêtre de Kick Lock Send, 840 × 1040 — même format que les autres plugins).
4. **Visuels de la suite** : six plugins maintenant. `groove-knobs-banner.webp` montre Kick Lock
   Send à la place de la grande fenêtre de Kick Lock ; `groove-knobs-og.webp` montre Kick Lock.
5. Le téléchargement Windows de Kick Lock est sur la release GitHub `kick-lock-v0.1.4`
   (`downloads.windows.url`). La suite (`suite`) reste la 0.1.2 à cinq plugins : la page de la
   suite doit dire que Kick Lock s'installe à part (`suite.includes_note`).

## Tâches

### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/kick-lock/og.png --out public/images/plugins/kick-lock/og-email.jpg
```
(si `og.png` manque : le régénérer dans `../groove-knobs` avec `build/tests/kick_lock/kl_screenshots_artefacts/Release/kl_screenshots --web web-assets`)
- Champs nouveaux / optionnels de `lib/plugins.json` à ajouter au type `Plugin` sans rien casser :
  `images_note?: string`, `downloads.<os>.includes?: string[]` (Kick Lock : « Kick Lock »,
  « Kick Lock Send »), `suite.includes_note?: string`.

### 2. Pages
- `/plugins` : la carte Kick Lock (format paysage), avec la mention « + Kick Lock Send ».
- `/plugins/kick-lock` : hero paysage, galerie (`ui-curve`, `ui-explain`, `ui-send`), les
  `features`, et un encadré **« Comment ça marche »** : 1) Kick Lock sur la basse, 2) Kick Lock Send
  sur la piste du kick, 3) le même numéro LINK (1 par défaut), 4) lecture : le statut passe à
  LOCKED. Plusieurs paires : un numéro LINK par paire.
- `/plugins/installation` : Kick Lock installe **deux** plugins (Kick Lock + Kick Lock Send) ;
  macOS signé et notarisé ; Windows non signé (SmartScreen : « Informations complémentaires →
  Exécuter quand même »).

### 3. R2 (bucket `groove-knobs`, clés `<slug>/<version>/<fichier>`)
- Téléverser `../groove-knobs/dist/KickLock-0.1.4-macOS.pkg` → `kick-lock/0.1.4/KickLock-0.1.4-macOS.pkg`
- Téléverser l'installateur Windows (télécharge-le d'abord depuis
  https://github.com/lalzin/groove-knobs/releases/tag/kick-lock-v0.1.4) →
  `kick-lock/0.1.4/KickLock-0.1.4-Windows-Setup.exe`
- Donne-moi les commandes exactes (une ligne par fichier) : **je les lance moi-même** (les clés R2
  sont à moi).

### 4. Vérifier
- `npm run build` OK ; les pages `/plugins`, `/plugins/kick-lock`, `/plugins/installation` en local,
  sur mobile aussi (la carte paysage ne déborde pas) ; une demande de téléchargement Kick Lock de
  bout en bout (e-mail Resend avec `og-email.jpg`, lien R2) ; `https://…/plugins/versions.json`
  sert bien `kick-lock` en 0.1.4.
