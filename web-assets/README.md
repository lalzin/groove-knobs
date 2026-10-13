# Visuels web — Groove Knobs

Générés depuis les vrais plugins (fenêtres rendues hors écran, mascottes des planches) :

```bash
for t in ff bt bw; do build/tests/**/${t}_screenshots --web web-assets; done   # bw en dernier (visuels de la suite)
cd web-assets && find . -name '*.png' ! -name 'poses-sheet.png' -exec sh -c 'cwebp -quiet -q 86 -alpha_q 100 "$1" -o "${1%.png}.webp"' _ {} \;
```

Pose principale de la mascotte : variable `GK_HERO` (FF 13, BT 1, BW 11). `poses-sheet.png` numérote toutes les poses.

| Fichier | Taille | Usage |
|---|---|---|
| `<slug>/ui-hero.webp` | 840×1040 | fenêtre du plugin (hero, carte) |
| `<slug>/ui-hero@3x.webp` | 1260×1560 | idem, écrans haute densité / grand format |
| `<slug>/ui-<état>.webp` | 840×1040 | galerie (autres réglages) |
| `<slug>/mascot.webp`, `poses/*.webp` | ~340–650 px | mascottes détourées |
| `<slug>/og.webp` | 1200×630 | partage (Open Graph, X, WhatsApp) |
| `<slug>/card-square.webp` | 1080×1080 | Instagram, grille produits |
| `<slug>/icon-512.webp` | 512×512 | icône, favicon de la page produit |
| `groove-knobs-og.webp` | 1200×630 | partage de la page suite |
| `groove-knobs-banner.webp` | 2400×1000 | bannière de la page suite |

Les PNG d'origine sont à côté (pour Instagram / impression). `plugins.json` = textes FR/EN,
presets, couleurs, fichiers d'installation. `PROMPT-site-lalzin.md` = le prompt d'intégration.

Limite : les mascottes viennent des planches actuelles (~340 px par personnage pour Velvet et
Swell) — nettes jusqu'à ~350 px affichés ; au-delà, prévoir les planches HD (voir `ASSETS_TODO.md`).
