# Groove Knobs — French Filter, Boogie Trio, Backwash

La suite **Groove Knobs** (LALZIN) : des plugins à un seul knob pour la funky house et la disco. **French Filter** (le filtre French Touch), **Boogie Trio** (chorus triple de string machine), **Backwash** (reverb inversée calée sur le tempo).
VST3 / AU / Standalone, macOS (universel) et Windows.

> État : DSP, presets, calibration, interface et mascotte faits (étapes 1–5). Reste la finalisation après validation des captures.

## Build

Prérequis : CMake ≥ 3.22, Ninja ou Xcode (macOS), Visual Studio 2022 (Windows). JUCE et Catch2 sont récupérés automatiquement (tags figés).

```bash
scripts/build_mac.sh            # universel arm64 + x86_64, tests compris → build-release/
scripts/build_mac.sh --native   # plus rapide pour le développement → build/
```

Windows : `powershell -ExecutionPolicy Bypass -File scripts\build_win.ps1`.

Le build copie le plugin dans `~/Library/Audio/Plug-Ins/VST3` et `.../Components` (signature ad hoc). Dans Ableton : Préférences → Plug-ins → Rescan.

Identifiants (nom, codes, bundle ID, version) : un seul fichier, `cmake/GrooveIds.cmake`.

## Installateurs (téléchargement gratuit)

```bash
scripts/make_installer_mac.sh                      # → dist/FrenchFilter-<v>-macOS.pkg (+ .zip avec les licences)
```

Windows (sur un PC avec Visual Studio 2022 et Inno Setup 6) :
`powershell -ExecutionPolicy Bypass -File scripts\make_installer_win.ps1` → `dist\FrenchFilter-<v>-Windows-Setup.exe`.
Sans PC, le workflow GitHub Actions (`.github/workflows/build.yml`) construit les deux installateurs dès que le dépôt est sur GitHub.

**Avant de publier sur ton site :** sans signature, macOS (Gatekeeper) bloque le `.pkg` téléchargé et Windows (SmartScreen) affiche un avertissement.
- macOS : compte Apple Developer (99 $/an), certificats « Developer ID Application » et « Developer ID Installer », puis
  `DEV_ID_APP=... DEV_ID_INSTALLER=... NOTARY_PROFILE=... scripts/make_installer_mac.sh` (signature + notarisation + agrafage).
- Windows : un certificat de signature de code (Authenticode), via `WIN_SIGN_CERT` / `WIN_SIGN_PASSWORD`.

Licence : `installer/common/LICENSE.txt` (freeware, à relire) et `THIRD_PARTY_NOTICES.txt`.

## Validation

```bash
ctest --test-dir build --output-on-failure   # ou build/tests/ff_tests_artefacts/Release/ff_tests
scripts/run_pluginval.sh build 10            # VST3 + AU, strictness 10
scripts/run_auval.sh                         # auval -v aufx FrFl Lalz
```

## Le son

**Un knob bipolaire.** Au centre (défaut), le plugin est transparent : la sortie est l'entrée exacte, retardée de la latence. Vers la **gauche**, le côté passe-bas (LP), vers la **droite**, le côté passe-haut (HP). Chaque preset a deux personnalités, une par côté. Une zone morte de ±1 % au centre absorbe le CC 64 d'un contrôleur MIDI.

Chaîne wet, suréchantillonnée 2x : drive asymétrique → grain de sampler → DC blocker → filtre (passe-bas ou passe-haut, 12 / 24 dB, résonance qui sature, cutoff poussé par un suiveur d'enveloppe) → phaser calé sur le tempo → pump déclenché par le kick → reverb (avec son propre filtre d'envoi) → makeup calibré. Les étiquettes sous le knob rappellent la signature du côté actif.

| Preset | ◀ côté LP | côté HP ▶ |
|---|---|---|
| **Filtered Disco** (défaut) | le French Touch classique : 24 dB, résonance qui grogne, léger pump, phaser | passe-haut résonant qui grogne + phaser |
| **Sunrise Sweep** | 12 dB doux + nappe de reverb | **riser** jusqu'à 1,4 kHz + grande reverb qui monte |
| **Muffled Club** | très sombre (110 Hz) + petite pièce mate : « derrière le mur » | « radio » : passe-haut + grain + petite pièce |
| **Acid Disco** | squelch (le filtre s'ouvre à chaque coup), résonance au max, drive 15 dB | squelch passe-haut, même mordant |
| **Touch 98** | grain de sampler 9 bits / 9 kHz + pump | sampler 8 bits sur le haut du spectre + pump |
| **Robot Funk** | filtre 12 dB qui « parle » (enveloppe +3,5 oct) + grain | passe-haut qui parle |
| **Sounds Better** | gros pump sur le kick + phaser lent (8 mesures) | pump + phaser large, passe-haut 12 dB |
| **Lady Groove** | 12 dB doux qui garde de l'air, phaser, un peu de pièce | passe-haut soyeux, phaser large, de l'air |
| **Space** | façon « Space » des DJM Pioneer : le signal reste intact, une reverb sombre (filtrée en passe-bas) monte | même chose avec une reverb aérienne (filtrée en passe-haut) |

## L'interface

Un gros knob tricolore, un personnage à béret qui change de pose avec le knob (8 poses), une barre de presets, un bouton On/Off.

- **Knob** : centre = neutre, gauche = LP, droite = HP · glisser verticalement (250 px pour toute la course, petit aimant au centre) · **Shift** ou **Cmd/Ctrl** pour un réglage ×10 plus fin · molette = 1 % par cran · **double-clic = retour au centre** · flèches du clavier · clic droit = menu de l'hôte (MIDI learn, automation). Saisie texte : `LP 40`, `HP 20`, `-40`, `+20`, `0`.
- **Presets** : ◀ ▶ ou clic sur le nom.
- **Fenêtre** : coin en bas à droite, de 75 à 200 % ; la taille est sauvegardée avec le projet.
- **Mascotte** (17 poses tirées des deux planches) : au centre avec de la musique, elle **danse** (une pose par temps) ; côté LP, elle devient sceptique, puis inquiète, puis abattue quand c'est étouffé ; côté HP, elle s'excite avec la montée (poings levés) puis **se bouche les oreilles** tout au bout ; quand tu reviens vite au centre (le **drop**), elle enchaîne yeux en étoile, saut bras en l'air, doigts pistolets, puis termine avec les **lunettes de soleil**. En bypass, elle dort.

Captures : `build/tests/ff_screenshots_artefacts/Release/ff_screenshots screenshots` (une planche contact est incluse).
Assets de la mascotte : `scripts/make_mascot_frames.sh` régénère `plugins/french_filter/Assets/mascot_*.png` depuis les deux planches de `Assets/source/` (détourage, alignement, mise à l'échelle commune) ; l'ordre des frames correspond aux rôles définis dans `FrenchFilterMascot::poses()`.

## ff_render

```bash
FF=build/tools/ff_render/ff_render_artefacts/Release/ff_render
$FF sweep --out-dir renders                         # trajet centre → LP → centre → HP → centre sur 16 mesures à 124 BPM, chaque preset
$FF render in.wav out.wav --preset "Acid Disco" --automation curve.txt --beats --bpm 124
$FF gen-loop loop.wav --music                       # boucle de test (batterie + basse + accords)
$FF measure                                         # niveau K-weighted vs dry, par preset
scripts/calibrate_makeup.sh                         # régénère les tables makeupDb
```

Fichier d'automation : une paire `<temps> <amount>` par ligne (secondes, ou temps avec `--beats`), interpolation linéaire.

## Structure

`modules/groove_core` (code partagé de la suite) · `plugins/french_filter` (moteur, presets, assets) · `tools/ff_render` · `tests` · `scripts`. Choix et compromis : voir `DECISIONS.md`.

## Licence JUCE

JUCE 8 est sous double licence (AGPLv3 / commerciale). Vérifie la licence adaptée avant une distribution publique.

---

# Boogie Trio

Un chorus triple façon *string machine* des années 70 : à 0 % une seule voix, à 100 % un trio.

- **Stéréo façon « Wider »** : le centre reste exactement le signal d'origine (phase intacte), le chorus vit sur les côtés ; en mono, la somme L + R redonne l'entrée.
- **La basse reste sèche et mono** (passe-haut 24 dB sur l'envoi des voix).
- Chips **VINTAGE** (couleur BBD : passe-bas, saturation douce, souffle qui suit le signal) et **WIDE** (côtés plus larges). Chaque preset a ses valeurs par défaut, tes choix sont sauvegardés.
- Bus mono, stéréo et mono → stéréo. Latence nulle.

| Preset | Signature |
|---|---|
| Disco Strings | nappe de cordes disco, brillante, petite salle |
| Lush Pad | immense et lent, grande reverb, légère dérive de bande |
| Warble | instable, façon cassette : forte dérive, un peu de feedback, sombre |
| Velvet | épaississement discret et propre (bus, voix) |
| Funk Keys | Rhodes / clavinet : autopan en croches calé sur le tempo |
| Jet Set | flanger « jet » à feedback |
| Disco Gate | cordes hachées en doubles croches, calées sur le tempo |
| Moon Phaser | phaser sur 2 mesures + salle |

Mascotte : **Velvet**, le violoniste disco ; des clones apparaissent à partir de 50 % et se balancent sur la vraie phase du LFO.

Outils : `bt_render` (mêmes commandes que `ff_render`), captures `bt_screenshots`, installateur `scripts/make_installer_mac.sh BT`.

---

# Backwash

Une **reverb inversée en temps réel, calée sur le tempo** : chaque coup (snare, clap, voix, accord) est précédé d'un souffle qui « aspire » vers lui et s'arrête pile au moment du coup. Ce qui demandait plusieurs minutes (bounce, reverse, reverb, re-bounce, re-reverse, recalage) se fait en un knob, en lecture, sur n'importe quelle piste.

> **Deux modes de latence.** Par défaut, Backwash « voit » 2 s à l'avance pour préparer le souffle : c'est exact sur n'importe quel son, mais Ableton retarde tout le set de 2 s (≈ 1 mesure à 120 BPM : lecture, knobs et entrées monitorées répondent en retard).
>
> **LIVE (zéro latence)** : Backwash garde en mémoire la dernière mesure jouée et rejoue, avant chaque coup, le souffle du même coup une mesure plus tôt (calculé en arrière-plan, lu à la position musicale). Sur une boucle, c'est identique au mode 2 s ; le premier coup après lecture a déjà son souffle, la mémoire est gardée à l'arrêt et enregistrée avec le projet. Un coup qui change d'une mesure à l'autre reçoit le souffle de l'ancien ; il faut que le transport tourne.
>
> **LOCK** fige la mémoire : le souffle continue sur la grille même si la piste change ou s'arrête (break, drop), jusqu'au déverrouillage. Si la mémoire est vide, LOCK écoute d'abord une mesure complète, puis la fige.
>
> **LIVE et LOCK sont activés par défaut** (point jaune = mémoire vide / écoute en cours) : lance la lecture, la première mesure est mémorisée, les souffles arrivent dès la suivante, et y restent.

**Le temps** (à droite du knob) :
- **SWELL** : longueur du souffle — PRESET (celle du preset), 1/4 BEAT, 1/2 BEAT, 1 BEAT, 2 BEATS, 1 BAR ; LONG la double toujours (plafond ~1,95 s, point jaune sur LONG au-delà).
- **HITS** : quels coups reçoivent un souffle — ALL (tous), 1 2 3 4 (chaque temps, sans les contretemps), BEAT 1, 2 + 4 (le backbeat), LAST (le dernier temps de la mesure), 1 / 2 BARS (le premier temps une mesure sur deux, pour les drops). Fenêtre de 1/8 de temps avant à 3/8 après chaque temps visé (claps en avance, swing). Un niveleur rapproche les coups faibles (kick, hats) du plus fort, pour que chaque coup visé ait un souffle audible.

- Longueur du souffle = beats du preset × (2 avec **LONG**) au tempo de l'hôte (120 BPM sans tempo), bridée à ~1,95 s ; à tempo trop lent, un point s'affiche sur le chip LONG.
- **SHIMMER** : une octave haute dans le souffle.
- Le knob règle le niveau (courbe douce), la brillance et la largeur du souffle ; il ne change pas sa longueur.
- À 0 %, la sortie est exactement l'entrée retardée de 2 s.

| Preset | Pour quoi |
|---|---|
| Snare Swell (défaut) | le classique : un souffle d'un temps vers chaque snare / clap |
| Vocal Suck | la voix « aspirée » avant chaque phrase, qui monte un peu en hauteur et bouge dans la stéréo |
| Clap Rewind | court (½ temps), la hauteur grimpe de 7 demi-tons comme une bande rembobinée, un peu saturé |
| Ghost Pad | tout le signal s'aspire en nappe fantôme qui tourne lentement dans la stéréo ; LONG par défaut |
| Big Inhale | énorme, avec SHIMMER et un filtre résonant qui monte jusqu'au coup : l'effet « avant le drop » |
| Reverse Echo | pas de nappe : des échos inversés à la double-croche, en ping-pong, qui arrivent sur le coup |
| Stutter Suck | souffle de 2 temps haché à la double-croche, ouvert juste avant chaque pas de la grille |
| Dusty Tape | sombre, saturé, lo-fi (échantillonnage réduit), hauteur qui monte de 3 demi-tons |

Mascotte : **Swell**, le surfeur. La vague derrière lui suit le knob et le rythme ; grâce au lookahead, il **inspire avant chaque coup**, en même temps que le vrai souffle, et l'arc du knob s'illumine pendant le souffle puis s'éteint d'un coup sur le coup.

Outils : `bw_render` (rendus à 100 %), captures `bw_screenshots`, installateur `scripts/make_installer_mac.sh BW`.

# Pew Pew

Le **pew disco** en instrument : chaque note MIDI joue un tom électronique qui glisse vers le bas, façon *disco lazer*. Le knob **PEW** va du tom plat (0 %) au pew le plus fou (100 %). Se charge sur une piste MIDI (instrument ou drum rack), **latence nulle**.

- **La note-off est ignorée** : chaque coup est un one-shot qui se joue jusqu'au bout de son enveloppe, comme un vrai tom. La longueur de la note MIDI ne change rien ; la vélocité règle la force du coup (courbe propre à chaque preset).
- La note règle la hauteur (le Do 60 = la hauteur de base du preset), 8 voix ; une 9e note reprend la plus ancienne avec un fondu de 2 ms.
- **ROOM** : la reverb (son mix maximal dépend du preset). **ECHO** : un écho stéréo calé sur le tempo, avec **SYNC** = 1/16, 1/8 pointée (défaut), 1/8 ou 1/4. Changer de preset ne touche ni ROOM, ni ECHO, ni SYNC.
- All Notes Off / reset : coupure propre en 5 ms.
- Chips (sous ROOM, chaque preset a ses valeurs par défaut) : **UP** retourne le glissement (le pew monte au lieu de descendre) ; **LONG** double la tenue et la décroissance ; **ROLL** transforme chaque note en roulement de doubles-croches au tempo (5 coups, chacun plus doux).
- La **vélocité** règle aussi la profondeur du glissement : un coup fort glisse plus loin (60 % de la profondeur au plus doux).

| Preset | Pour quoi |
|---|---|
| Laser Pew (défaut) | le pew disco de référence : la note tenue qui glisse doucement vers le bas, grosse reverb |
| Disco Tom | le tom disco : chute rapide, un peu de chaleur |
| Robo Zap | carré filtré qui glisse profond et régulier : effet robot, pour les fills |
| Space Toms | FM métallique à longue queue, pour les breaks et les nappes rythmiques |
| Deep Drop | un sub qui glisse lentement d'une octave et demie, pour les basses et les drops |
| Syndrum 76 | le tom électronique de la fin des années 70 : sinus pur, chute vive, court |
| Bubble Pop | un « bloop » court qui monte (UP par défaut), pour les fills funky |
| Arcade Zap | carré brillant, glissement rapide, lo-fi : le laser de jeu d'arcade |
| Disco Cowbell | deux carrés désaccordés (~540 / 800 Hz) : une cloche disco jouable en hauteur |
| Siren Sweep | glissement très long et lent avec un léger flottement : la sirène dub, pour les breaks |

Mascotte : **Tomi**, le batteur disco. Il frappe à chaque note, plus fort avec la vélocité ; sa pose suit le knob (coup tranquille à 0 %, tour de baguette, étincelles, baguettes en l'air, saut euphorique à 100 %). Son tambour (dessiné par le code) se soulève avec le pitch, des ondes au sol suivent ROOM, des « fantômes » de Tomi rejouent le coup aux temps de l'écho. En bypass, il s'endort à côté du tambour.

Outils : `gb_render` (rendu MIDI : `pattern`, `render`, `sweep`, `analyze`), captures `pp_screenshots`, poses de Tomi `scripts/make_tomi_frames.sh`.

## Installateurs de la suite

```bash
scripts/make_installer_mac.sh FF    # French Filter
scripts/make_installer_mac.sh BT    # Boogie Trio
scripts/make_installer_mac.sh BW    # Backwash
```

Windows : `scripts\make_installer_win.ps1 -Plugin FF|BT|BW` (Visual Studio 2022 + Inno Setup 6), ou le workflow GitHub Actions, qui construit les trois plugins et leurs installateurs Mac et Windows à chaque push.
