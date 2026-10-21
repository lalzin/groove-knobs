# Prompt — Mise à jour de la section Plugins de lalzin.com (5 plugins, macOS notarisé, mises à jour)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> La section Plugins existe déjà (pages `/plugins`, `/plugins/[slug]`, `/plugins/installation`,
> `/plugins/merci`, API `request` / `download`, R2, Resend, `/admin`). On la met à jour, on ne la refait pas.
> Les visuels et les données viennent de `../groove-knobs/web-assets/`.

---

## Ce qui change

1. **Deux nouveaux plugins** dans la suite Groove Knobs :
   - **Pew Pew** (`pew-pew`) — un **instrument** MIDI (VSTi / AU instrument), le « pew » disco ; mascotte Tomi.
   - **Sing It Back** (`sing-it-back`) — un **effet pour voix seule** : une foule qui chante avec la voix ; mascotte Sugar.
2. **Les installateurs macOS sont maintenant signés (Developer ID) et notarisés par Apple** : ils
   s'installent sans aucun avertissement. Les installateurs Windows ne sont toujours pas signés
   (SmartScreen : « Informations complémentaires → Exécuter quand même »).
3. **Les plugins vérifient les mises à jour** une fois par jour en lisant
   `https://www.lalzin.com/plugins/versions.json`.
4. Nouveaux visuels de la suite (cinq fenêtres) : `groove-knobs-og` et `groove-knobs-banner`.

## Tâches

### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
mkdir -p public/plugins && cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
for s in pew-pew sing-it-back; do
  sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/$s/og.png --out public/images/plugins/$s/og-email.jpg
done
```
- `lib/plugins.json` a de nouveaux champs, à ajouter au type `Plugin` de `lib/plugins.ts` (optionnels,
  ne rien casser pour les trois premiers) :
  - `type?: 'instrument' | 'effect'` (Pew Pew = instrument, Sing It Back = effect, absent = effect) ;
  - `latency_ms?: number` (Sing It Back : 21,3) ;
  - `downloads.<os>.signed?: string` (macOS : « Developer ID + notarised by Apple ») ;
  - `downloads.windows.url?: string` (lien GitHub de la release v0.1.2, pour information ; le
    téléchargement passe toujours par R2).
- Pour Sing It Back, `presets` contient les **styles** de foule (Club, Gospel, Stade, Chorale,
  Enfants, Géants, Robots) : sur la page produit, titre « Styles » au lieu de « Presets » quand
  `slug === 'sing-it-back'` (ou mieux : `type === 'effect'` et un champ `presetsLabel` si tu préfères
  le généraliser — reste simple).
- Galerie : Pew Pew a `ui-25`, `ui-100` ; Sing It Back a `ui-25`, `ui-100`, `ui-gospel` (la galerie
  liste déjà les `ui-*.webp` existants : vérifie qu'elle les prend sans code en dur).
- `DEFAULT_PUBLISHED` reste tel quel : la publication se fait depuis `/admin` (voir « À faire à la main »).

### 2. Pages
- `/plugins` : la grille doit tenir **5 cartes** proprement (mobile d'abord ; 2 colonnes tablette,
  3 desktop, la 5e centrée ou en ligne de 2 — à toi de voir, sans trou disgracieux).
- Page produit : badge « Instrument » / « Effet » selon `type` ; pour Pew Pew, une ligne
  « À charger sur une piste MIDI (instruments) » ; pour Sing It Back, « Sur une piste de voix seule
  (stem, a cappella) — pas un morceau complet ; règle d'abord le ton et la gamme » et la latence.
  JSON-LD `SoftwareApplication` : `applicationSubCategory` « Instrument » / « Audio effect ».
- **Version affichée** sur chaque page produit (`version`), et un petit « Quoi de neuf » si tu
  veux (optionnel, texte FR/EN à ajouter dans `plugins.json` plus tard : ne l'invente pas).

### 3. Guide d'installation (`/plugins/installation`)
- **macOS** : « Ouvre le `.pkg` et suis l'installation : il est signé et vérifié par Apple. »
  Supprime l'étape « Réglages Système → Confidentialité et sécurité → Ouvrir quand même ».
- **Windows** : inchangé (SmartScreen : « Informations complémentaires → Exécuter quand même »).
- **Ableton** : Réglages → Plug-Ins → Rescan ; ajoute : Pew Pew est dans les **instruments**
  (piste MIDI), les autres dans les **effets audio**. Backwash démarre en LIVE (il écoute la
  première mesure). Sing It Back : piste de voix seule, régler le ton et la gamme, puis CROWD.
- **Mises à jour** : « Le plugin vérifie une fois par jour s'il existe une nouvelle version ;
  si oui, un badge MAJ apparaît à côté de *made by LALZIN* — un clic ouvre cette page. Installe
  la nouvelle version par-dessus l'ancienne. Clic droit sur *made by LALZIN* pour désactiver. »

### 4. Fichier des versions (`/plugins/versions.json`)
- Servi depuis `public/plugins/versions.json` à l'URL exacte `https://www.lalzin.com/plugins/versions.json`
  (vérifie qu'aucune route `app/plugins/...` ne l'intercepte ; sinon crée une route
  `app/plugins/versions.json/route.ts` qui renvoie le fichier).
- En-têtes : `Content-Type: application/json; charset=utf-8`, `Cache-Control: public, max-age=3600`.
- Format (ne pas le changer, les plugins le lisent tel quel) :
  `{ "updated": "2026-10-20", "plugins": { "<slug>": { "version": "0.1.2", "url": "https://www.lalzin.com/plugins/<slug>" } } }`
- Toutes les `url` en **https** vers la page produit.
- Ajoute un test (ou une vérification au build) qui échoue si un slug de `plugins.json` manque dans
  `versions.json` ou si les versions diffèrent.

### 5. Email et confidentialité
- L'email de téléchargement liste déjà les plugins demandés : vérifie qu'il marche avec 5 plugins
  (visuels `og-email.jpg` des nouveaux), et le texte macOS (plus d'avertissement à contourner).
- `/confidentialite` : ajoute la vérification des mises à jour (le serveur voit l'adresse IP, le
  nom et la version du plugin dans l'en-tête `User-Agent: GrooveKnobs/<slug>/<version>` ; aucune
  autre donnée ; désactivable dans le plugin ; journaux du serveur seulement).

## Critères d'acceptation
- `npm run build` passe ; rien d'existant n'est cassé (home, `/links`, `/admin`, parcours de
  téléchargement des trois premiers plugins).
- `curl -i https://<preview>/plugins/versions.json` → 200, JSON, bon `Content-Type`.
- `/plugins` avec les 5 plugins publiés (en local, en publiant les deux nouveaux dans `/admin`) :
  responsive, Lighthouse ≥ 90 perf / accessibilité.
- FR et EN partout.

## À la fin, liste-moi ce que je dois faire à la main
1. **R2** (bucket `groove-knobs`, clés `<slug>/<version>/<fichier>`) — téléverser / remplacer :
   - les 5 `.pkg` macOS **0.1.2 notarisés** de `../groove-knobs/dist/` (clés R2 `<slug>/0.1.2/<fichier>` : la version est dans le chemin) (`FrenchFilter-0.1.2-macOS.pkg`,
     `BoogieTrio-0.1.2-macOS.pkg`, `Backwash-0.1.2-macOS.pkg`, `PewPew-0.1.2-macOS.pkg`,
     `SingItBack-0.1.2-macOS.pkg`) — nouvelle version : nouveaux chemins, les anciens fichiers 0.1.0 peuvent rester ou être supprimés ;
   - les 5 `.exe` Windows de la release GitHub v0.1.2
     (https://github.com/lalzin/groove-knobs/releases/tag/v0.1.2) (fichiers `…-0.1.2-Windows-Setup.exe`).
   Donne-moi les commandes exactes (`rclone` ou `aws s3 cp --endpoint-url`), une ligne par fichier.
2. **/admin** : publier `pew-pew` et `sing-it-back` (et les autres s'ils ne le sont pas).
3. Vérifier en production : `https://www.lalzin.com/plugins/versions.json` répond 200.
