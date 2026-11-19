# Prompt — lalzin.com : Groove Knobs 0.1.11 (Kick Lock : PRECISE à ~50 ms)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> Suppose que la mise à jour 0.1.10 (`PROMPT-site-update-0.1.10.md` : Daft Chunk) est faite.
> Sinon, la faire d'abord.

## Ce qui change
1. **Kick Lock** : le mode PRECISE passe de ~360 ms à **~50 ms de latence**, pour le même résultat. Nouveau
   réglage **PRECISION** sous PRECISE : 35, 50 (par défaut), 65 ou 340 ms. LIVE reste à 32 ms.
   `plugins.json` : `latency_ms` 50, `latency_note` et la première ligne de `features` mises à jour.
2. **Toute la suite passe en 0.1.11**, sans changement de son pour les autres plugins.
3. **Visuels** : `kick-lock/` refaits (le sélecteur PRECISION dans la fenêtre, « latency 50 ms » en bas) ;
   `groove-knobs-banner.webp` / `groove-knobs-og.webp` refaites.
4. Téléchargements Windows : la release GitHub `v0.1.11`.

## Tâches
### 1. Données et visuels
```bash
rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
cp ../groove-knobs/web-assets/versions.json public/plugins/versions.json
sips -s format jpeg -s formatOptions 82 ../groove-knobs/web-assets/kick-lock/og.png --out public/images/plugins/kick-lock/og-email.jpg
```

### 2. Pages
- `/plugins/kick-lock` : là où la page parle de la latence de PRECISE (~360 ms), mettre **~50 ms**, et ajouter
  une ligne sur **PRECISION** : « 35 ms le plus léger, 50 ms par défaut, 65 ms la rotation en place avant le
  kick, 340 ms toute la collision vue ». Le DAW compense la latence (pour le mixage) ; LIVE (32 ms) pour jouer.
- Les numéros de version affichés (0.1.10 → 0.1.11), partout où ils ne viennent pas de `plugins.json`.

### 3. R2 (bucket `groove-knobs`) — je téléverse moi-même
- Le script `../groove-knobs/dist/upload-r2-0.1.11.sh` (macOS depuis `dist/`, Windows depuis la release).

### 4. Vérifier
- `npm run build` ;
- `/plugins/kick-lock` : galerie, latence, affichage sur mobile ;
- `public/plugins/versions.json` en 0.1.11 partout.
