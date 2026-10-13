# Changelog

## [Unreleased] — Backwash 0.1.0 (nouveau)
- Installateurs : pages d'accueil macOS de Backwash et Boogie Trio corrigées (le texte de French Filter s'y était glissé) ; textes Backwash à jour (LIVE / LOCK, SWELL / HITS), note Windows idem.
- **Temps** : SWELL (longueur 1/16 → 1 mesure, ou celle du preset) et HITS (tous / temps 1 / 2 + 4 / dernier temps / 1er temps toutes les 2 mesures), dans les deux modes ; LIVE et LOCK activés par défaut, LOCK sur mémoire vide écoute d'abord une mesure complète.
- **LIVE** (zéro latence) et **LOCK** : mémoire de la dernière mesure, souffle rendu en arrière-plan (FFT) et lu à la position musicale ; mémoire conservée à l'arrêt et dans l'état du projet ; LOCK la fige. Commutation LIVE avec fondu de 10 ms et nouvelle latence annoncée à l'hôte.
- UI : vague sans marche à la crête ni coupure en haut (hauteur plafonnée en douceur), rouleau redessiné sans couture ; Swell garde une pose stable par zone (euphorique en permanence à 100 %), le rythme ne fait plus que l'effleurer.
- Reverb inversée en temps réel, calée sur le tempo, lookahead fixe de 2 s ; convolueur partitionné maison (`gk::PartitionedConvolver`) ; IR générées en arrière-plan avec fondu de 50 ms ; focus sur les attaques ; SHIMMER (octave haute) ; limiteur sur le wet seul.
- 5 presets (Snare Swell, Vocal Suck, Clap Rewind, Ghost Pad, Big Inhale), chips LONG et SHIMMER.
- UI « surf rétro » : knob aux bords ondulés dans les bleus de Swell, arc qui « respire » avant chaque coup, vague vectorielle qui suit le knob et le rythme, mascotte Swell (12 poses) qui inspire en même temps que le souffle.
- Tests `bw_tests` (null, alignement, latence, longueur, stabilité, clics, blocs, état, focus, données UI, CPU), `bw_render`, `bw_screenshots`.

## [Unreleased] — Boogie Trio 0.1.0 (nouveau)
- Chorus triple de string machine : 3 voix (Hermite), LFO chorus / vibrato à 0 / 120 / 240°, stéréo façon Wider (centre intact), basse sèche, VINTAGE et WIDE.
- 8 presets avec effets en plus (feedback, dérive de bande, trémolo / gate synchronisés, reverb, ton, phaser).
- UI « 70s sunset » et mascotte Velvet avec ses clones.

## groove_core (suite)
- « made by LALZIN » cliquable dans les trois plugins : ouvre www.lalzin.com/links (curseur main, soulignement au survol, `OneKnobConfig::companyUrl`).
- Choix globaux (`OneKnobConfig::choices`, sélecteurs légendés `ChoiceChips`, `OneKnobEngine::setChoices`), interrupteurs activables par défaut ; interrupteurs globaux (`OneKnobConfig::switches`, chips en colonne), latence variable selon ces interrupteurs (`latencyForSwitches` / `setSwitches`), état additionnel par plugin (`saveExtraState` / `loadExtraState`), signature rythmique dans `BlockTiming`, `IntegerDelay` à délai variable.
- Toggles avec valeurs par défaut par preset, bus mono → stéréo, makeup par combinaison de toggles, moteur toujours actif, télémétrie en sortie, mode hors temps réel, convolueur partitionné, chips cliquables, libellés d'extrémité, knob aux bandes ondulées et lueur qui « respire », hooks de mascotte, outil `gk_render` commun, `asset_prep` (fond uni, grilles irrégulières, fragments voisins).


## [Unreleased] — 0.1.0
### Modifié (2026-10-13)
- Knob bipolaire : centre neutre, gauche = passe-bas, droite = passe-haut ; presets à deux côtés ; zone morte au centre ; affichage LP / HP ; migration des sessions v1.
- Mascotte : 17 poses (deuxième planche + poses inutilisées de la première), danse au centre, échelles d'émotions par côté, séquence du drop qui finit en lunettes de soleil.
- Nouveau preset **Space** (façon Pioneer DJM) : reverb filtrée qui monte, signal intact.
- Moteur : passe-haut 12 dB, filtre d'envoi de la reverb, niveau de reverb par côté.

### Modifié (après les premiers tests d'Alan)
- Moteur : résonance qui sature (growl), suiveur d'enveloppe (squelch), grain de sampler, pump déclenché par le kick, pentes 12 / 24 dB, mode passe-haut (riser), room / wash.
- Les 8 presets refaits, chacun avec sa signature (étiquettes affichées sous le knob).
- Mascotte : modèle d'émotions tension / relâchement (danse filtre ouvert, inquiète filtre fermé, euphorie au drop).
- Métrique anti-zipper refaite (rafale HF sur 5 ms, seuil −80 dB) ; calibration du makeup avec garde de pic.

### Ajouté
- Monorepo CMake (JUCE 8.0.15, C++20) : French Filter en VST3 / AU / Standalone, binaire universel macOS, signature ad hoc, copie dans les dossiers système.
- Module `groove_core` : `OneKnobProcessor` (paramètres versionnés amount / preset / bypass, état versionné, zone de transparence, makeup calibré, soft clip, bypass sans clic, télémétrie lock-free pour l'UI), `OneKnobEngine`, `PresetManager`, `HostSync`, utilitaires DSP (TPT SVF, saturation, DC blocker, phaser, lisseurs, K-weighting, signaux de test, rendu hors ligne).
- Moteur French Filter : suréchantillonnage 2x FIR (latence entière), drive asymétrique, passe-bas 24 dB/oct (2 SVF TPT), phaser 4 étages calé sur le tempo.
- 4 presets JSON avec tables de makeup calibrées.
- `ff_render` : render, sweep, gen-loop, gen-pink, calibrate, measure.
- Tests Catch2 : null test, stabilité, anti-zipper, changement de preset, tailles de bloc, état, makeup, latence.
- UI : éditeur générique `gk::OneKnobEditor` (en-tête, mascotte, gros knob, barre de presets, On/Off), 420×520 redimensionnable de 75 à 200 %, palette et police Lilita One (OFL).
- BigKnob : glisser vertical (250 px), réglage fin avec Shift ou Cmd/Ctrl, molette, double-clic, flèches du clavier, menu de l'hôte au clic droit, gestes d'automation, accessibilité.
- Mascotte French Filter : 8 poses bitmap (outil `asset_prep`), animations audio et tempo, sommeil en bypass.
- `ff_screenshots` : captures 100 % et 200 % de chaque preset, plus une planche contact.
- 4 presets French Touch : Touch 98, Robot Funk, Sounds Better, Lady Groove (8 au total).
- Crédit « made by LALZIN » dans l'en-tête.
- Installateurs : `.pkg` macOS (make_installer_mac.sh), Inno Setup pour Windows (make_installer_win.ps1), workflow GitHub Actions, licence freeware et mentions tierces.
- Scripts : build_mac.sh, build_win.ps1, run_pluginval.sh, run_auval.sh, calibrate_makeup.sh.
