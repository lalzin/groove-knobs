# DECISIONS

Une ligne par décision : **contexte** — choix — raison.

## Build & outils
- **JUCE** — 8.0.15 (dernier 8.x stable au 2026-10-12), figé via `GK_JUCE_TAG` dans le CMakeLists racine — reproductibilité.
- **Tests** — Catch2 v3.16.1 (FetchContent) plutôt que juce::UnitTest — générateurs, `CAPTURE`, tag `[!mayfail]`, intégration ctest.
- **Sources des dépendances** — partagées dans `.deps/` entre les dossiers de build — évite de recloner JUCE pour le build natif et le build universel.
- **Machine de dev sans Xcode** — générateur Ninja + Command Line Tools (Clang 14, SDK 13.3) ; JUCE signe déjà en ad hoc avant la copie — Xcode n'est pas installé ici, et le résultat est le même pour un usage local.
- **Warnings** — `juce_recommended_warning_flags` + `-Wall -Wextra` sur nos cibles, 0 warning — JUCE compile proprement avec ces flags.
- **Code partagé** — cible INTERFACE `FrenchFilterCode` compilée par le plugin, ff_render et les tests — les modules JUCE sont INTERFACE ; une lib statique créerait des doublons.
- **clang-format** — `.clang-format` fourni mais pas exécuté — pas installé, et sous Ventura Homebrew compile LLVM depuis les sources (plusieurs heures).
- **pluginval** — binaire officiel 1.0.4 téléchargé par le script dans `.deps/` — pas de paquet système.

## Architecture
- **Partage générique / spécifique** — `gk::OneKnobProcessor` porte tout ce qui est générique (paramètres, rampe du macro, dry retardé, zone de transparence, makeup, soft clip, bypass, état, télémétrie UI) ; `gk::OneKnobEngine` ne porte que le chemin wet — Swing Delay n'aura qu'à écrire son moteur + ses presets.
- **Makeup appliqué dans le cadre générique (taux de base)**, après le moteur — équivaut au « après sous-échantillonnage » de la spec, et sert à tous les plugins.
- **Blocs plus grands qu'annoncé** — découpés en interne en morceaux de `maxBlock` — certains hôtes envoient plus que `samplesPerBlock`.
- **Bypass hôte** — `processBlockBypassed` passe par le même chemin forcé en bypass — garde la compensation de latence et le fondu.
- **Seuil de transitoires & RMS (mascotte)** — calculés sur l'entrée — la mascotte réagit à la musique même filtrée à fond.
- **État** — ValueTree explicite (`version`, `amount`, `preset` par **nom** + index de secours, `bypass`, `editorScale`) ; version plus récente → ignorée ; hook `migrateState` — robuste si on réordonne ou renomme des presets.
- **Programmes** — `getNumPrograms`/`setCurrentProgram` pilotent le paramètre `preset` — JUCE expose aussi un paramètre « Program » en VST3, les deux restent synchronisés.

## DSP
- **Drive : marge (headroom) 0,5** — `g = 0,5 · 10^(driveDb/20)` dans la tanh normalisée — avec g = 1, une boucle à 0 dBFS sature déjà à amount ≈ 0 ; à 0,5 la courbe ne mord vraiment qu'aux réglages hauts (Filtered Disco ≈ chaud, Acid ≈ mord).
- **Rampe du macro** — linéaire 20 ms, atterrit exactement sur la cible — condition du bit-exact à 0.
- **Paramètres dérivés : lisseur 2 pôles à amortissement critique** (95 % en 30 ms) au lieu d'un 1 pôle — le 1 pôle démarre avec une cassure de pente, mesurable (−37 dB au test zipper) sur les changements de preset avec le phaser actif.
- **Coefficients du filtre : calculés tous les 8 échantillons suréchantillonnés et interpolés linéairement par échantillon (g, k)** ; gains multiplicatifs (drive, biais, mix, feedback du phaser) lissés à chaque échantillon — les paliers de 8 échantillons produisaient un vrai zipper (ratio −0,5 dB contre −53 dB aujourd'hui).
- **Fondu dry/wet de la zone 0–3 % : limité à au moins 10 ms** — une automation rapide traversait la zone en ~1 ms ; dry et wet diffèrent légèrement en phase (DC blocker, passe-bas à 17 kHz) → clic.
- **Redémarrage du wet après l'inactivité** — reset des états, sortie maintenue sur le dry pendant latence + 16 échantillons, puis fondu de 3 ms en S — les premiers échantillons du suréchantillonneur ne sont que du remplissage.
- **Bypass** — fondu de 20 ms en S (smoothstep) ; bypass complet = dry retardé exact (pas de soft clip) — vrai bypass, sans clic.
- **Soft clip toujours en sortie (hors bypass)** — fidèle au schéma de la spec ; identité exacte sous −1 dBFS, donc le null test reste bit-exact pour du matériel sous −1 dBFS. ⚠ Un signal au-dessus de −1 dBFS sera adouci même à 0 %. **À valider** (alternative : ne clipper que la part wet).
- **Phaser** — 4 passe-tout TPT 1er ordre à fréquence commune, sortie `x + mix·(ap − x)`, LFO en cosinus surélevé (min à la phase 0), fréquence lissée en log (15 ms) — absorbe les sauts de phase (démarrage du transport, boucle, changement de `bars`).
- **Plages de balayage du phaser non données par la spec** — Sunrise 200–4000 Hz (large), Acid 300–3500 Hz, Muffled 250–2500 Hz (mix 0, sans effet) — « large et lent » pour Sunrise, plus mordant pour Acid.
- **Tempo à l'arrêt** — LFO en roue libre depuis la dernière position, au BPM hôte s'il existe, sinon 120 — pas de saut à l'arrêt du transport.

## Makeup / calibration
- **Signaux** — bruit rose −18 dBFS RMS (6 s) + boucle de batterie synthétique 124 BPM (kick, clap, hats) crête −3 dBFS ; mesure K-weighting BS.1770 sans gating, 0,5 s ignorées.
- **Bruit rose et boucle sont incompatibles** — à 100 %, le passe-bas à 140–250 Hz retire 5–10 dB pondérés au bruit rose, mais ≈ 0–1 dB à une boucle portée par le kick (la résonance le remonte même). Une seule courbe ne peut pas tenir ±1 dB sur les deux.
- **Choix : minimax entre les deux, mais la boucle reste à ±0,75 dB de son niveau dry** — un filtre qui rendrait le kick plus fort que le dry pousserait le soft clip et pomperait le niveau des mix ; l'usage cible (boucles disco, bus) est porté par le kick et la basse.
- **Conséquence** — test 7 (boucle) passe ; test 7b (bruit rose) en `[!mayfail]` : écart max 5,0 dB (Acid) à 8,2 dB (Muffled). **À valider à l'oreille.**
- **Boucle de test rééquilibrée** — kick plus court (décroissance 220 ms), clap/hats/accords plus présents — la première version avait 90 % de son énergie pondérée dans le kick, peu représentatif d'une boucle produite.

## Tests
- **Anti-zipper (v2) : rafale la plus forte sur 5 ms de l'énergie > 8 kHz de la dérivée seconde, seuil absolu −80 dB** — la première métrique (énergie totale rapportée à celle d'un sinus à 220 Hz) était beaucoup trop sensible, et son contrôle négatif tombait près d'un passage par zéro. Contrôle : un saut de gain de 0,1 dB sur une crête (tic à peine audible) mesure −72,5 dB. Le plugin mesure −88 à −121 dB (automation en 10 ms) ; les changements de preset passent aussi. Le grain est désactivé pendant ce test (mode diagnostic) : c'est un générateur d'aliasing voulu, qui masquerait les vrais clics.
- **Test de changement de preset à −20 dBFS** — à 100 %, certains presets font résonner un sinus de 220 Hz d'environ +11 dB ; on garde le soft clip hors d'un test qui cherche des discontinuités.
- **Test de latence** — latence = suréchantillonneur seul (mode diagnostic `setDiagnosticPassthrough`), mesurée par l'impulsion et par la corrélation ; la chaîne complète est en plus alignée à un retard de groupe près du passe-bas (≈ 24 µs, en phase minimale), qui n'est pas de la latence.
- **Tailles de bloc : sans tête de lecture** (LFO libre) — avec tête de lecture, la PPQ hôte est lue une fois par bloc, ce qui dépend légitimement du découpage.

## UI (demande d'Alan du 2026-10-12, remplace §8.6 de la spec)
- **Mascotte** — planche bitmap fournie (`Assets/source/mascot_sheet.png`, 4×3 sur fond vert, version upscalée 3072×2048) ; on n'utilise que les **8 premières frames** (2 premières rangées), dans l'ordre de lecture, de 0 % à 100 %.
- **Préparation** — outil `tools/asset_prep` : détourage du vert (dominance du vert, sans débordement vert sur les bords), découpe, alignement de chaque pose sur le centre de la tête et la ligne des pieds, toile commune de 466×560 → `Assets/mascot_0..7.png` (BinaryData) — le personnage ne saute pas quand la pose change.
- **Ce qui ne peut pas venir de la planche** — clignements, yeux fermés, rotation du béret : impossible sur une image fixe. Remplacé par : petit « pop » à chaque changement de pose, respiration ±3 % sur le RMS, rebond squash & stretch sur les transitoires (ressort amorti, ~150 ms), hochement sur les temps, balancement calé sur le LFO du phaser au-delà de 80 %, notes de musique au-delà de 85 %.
- **Bypass** — pose 0 (air inquiet), inclinée, « Z » qui flottent, toute l'UI à 40 % de saturation (copies grises précalculées des frames).
- **Knob** — d'après `Assets/source/knob_reference.png` : corps à bandes bleu / blanc / rouge, contour noir cartoon, pointeur noir brillant en « tête de poulet », anneau de 14 px dont la progression passe du bleu au crème puis au rouge, lueur et tirets d'excitation (qui pulsent sur le temps) au-delà de 80 %. Géométrie et dimensions conformes à §8.4.
- **Palette French Filter** — fond anthracite (#36363C → #1B1B1F), bleu #2F5BD8, crème #F3E9D2, rouge #E63946 ; la palette bleu nuit de la spec reste `Palette::grooveDefault()` — sur du bleu nuit, la moitié bleue du visage se fondait dans le fond ; l'anthracite reprend l'image 2.
- **Police** — Lilita One (SIL OFL 1.1, statique) au lieu de Nunito — Nunito n'est plus publiée qu'en police variable ; Lilita One est ronde, grasse et proche du lettrage de l'image 2. Licence embarquée à côté.
- **Barre de presets** — plaque crème à cadre tricolore et flèches bleue / rouge, comme sur l'image 2.
- **Bouton On/Off** — bouton rond en haut à droite, lié au paramètre `bypass` — pas dans la maquette de la spec, mais indispensable en Standalone ; dans Ableton, il pilote le même paramètre que l'interrupteur de l'appareil.
- **Rendu** — toile de base 420×520 mise à l'échelle par une AffineTransform (75–200 %, ratio fixe, coin de redimensionnement), donc vecteurs et images nets en Retina ; frames pré-mises à l'échelle à la taille réelle en pixels (cache), puis tracé bilinéaire — le Lanczos plein format coûtait ~30 % d'un cœur ; le Standalone entier tourne maintenant autour de 12 %.
- **Valeurs affichées par l'UI** — lues dans les paramètres (lissées côté UI) et non dans le fil audio — la mascotte et le knob suivent l'automation même quand l'hôte ne lit pas, et les captures sont déterministes.

## Presets ajoutés (demande d'Alan du 2026-10-12)
- **Noms évocateurs, pas de noms d'artistes** — Touch 98 (French Touch), Robot Funk (Daft Punk), Sounds Better (Stardust), Lady Groove (Modjo) — pour éviter d'utiliser des marques ou des noms d'artistes dans un produit distribué.
- **Réglages** — Touch 98 : 150 Hz, Q 5, drive 8 dB, phaser léger sur 4 mesures. Robot Funk : 220 Hz, Q 7, résonance plus tôt (68 %), drive 12 dB asymétrique, phaser sur 1 mesure. Sounds Better : 300 Hz, Q 3,5, phaser à 55 % sur 8 mesures, 90° en stéréo. Lady Groove : plancher à 400 Hz, courbe 0,6 (reste longtemps ouvert), Q 2,2, drive 3 dB. Tables de makeup calibrées comme les 4 premiers (boucle à ±0,75 dB).
- **Paramètre `preset` à 8 choix, toujours en ParameterID v1** — rien n'a encore été publié ; l'état mémorise le preset par nom, donc l'ordre peut encore changer.

## « made by LALZIN »
- En haut à gauche de l'en-tête (en face du bouton On/Off), texte tiré de `GK_COMPANY_NAME` (`cmake/GrooveIds.cmake`) — générique pour toute la suite.

## Installateurs
- **macOS** — `.pkg` produit par productbuild avec 3 choix (VST3, AU, app), installé dans les dossiers système `/Library/...` et `/Applications` (et non dans `~/Library`), bundles non relocalisables, postinstall qui rafraîchit le cache AU ; universel ; signature Developer ID + notarisation en option (variables d'environnement).
- **Windows** — Inno Setup 6, x64, VST3 dans `Common Files\VST3` et app dans Program Files, en anglais et en français, désinstallateur ; signature Authenticode en option. **Non testé ici** (pas de Windows) ; le workflow GitHub Actions le construit sur windows-2022.
- **Copie automatique à la compilation désactivée par défaut sous Windows** — elle demande un shell administrateur ; c'est l'installateur qui fait la copie.
- **Licence freeware** — brouillon (usage gratuit, y compris commercial ; pas de revente ni de redistribution) — à faire relire avant publication.

## Refonte du son et de la mascotte (retour d'Alan après test, 2026-10-12)
- **Constat** — les presets ne faisaient que varier des chiffres sur la même chaîne (passe-bas + phaser) ; le filtre ne se distinguait pas.
- **Un caractère pour le filtre** : résonance qui sature (« growl », état passe-bande de la SVF limité par une tanh, donc toujours stable), suiveur d'enveloppe qui ouvre le filtre à chaque coup (« squelch » / filtre qui parle), grain de sampler (S950 / SP-1200 : sample-and-hold + réduction de bits, à taux fixe par preset et mélangé avec le knob ; balayer le taux grésillait), « pump » déclenché par le kick (compression French Touch, release en fraction de temps, profondeur normalisée sur le pic récent donc indépendante du niveau), pentes 12 / 24 dB, mode passe-haut (riser), room / wash (Freeverb de JUCE, au taux de base, après le filtre).
- **Chaque preset a une signature** (affichée en étiquettes sous le knob) : Filtered Disco = growl + pump + phaser ; Sunrise Sweep = **riser passe-haut** + wash ; Muffled Club = passe-bas très sombre + petite pièce (« derrière le mur ») ; Acid Disco = squelch + growl max + drive 15 dB ; Touch 98 = sampler 9 bits / 9 kHz + pump ; Robot Funk = filtre 12 dB qui parle (enveloppe +3,5 oct) + grain ; Sounds Better = gros pump (7 dB) + phaser 8 mesures ; Lady Groove = 12 dB doux + phaser large + air.
- **Morphing sans clic entre presets** — les chaînes passe-bas 24 dB, passe-bas 12 dB et passe-haut tournent en permanence et sont mélangées par des poids lissés (pente, mode) ; la reverb tourne aussi en permanence — l'allumer au changement de preset lui envoyait une attaque abrupte, qu'elle faisait résonner.
- **Fondu de 2 ms sur l'entrée du wet à sa relance** — sinon le suréchantillonneur part du silence et la reverb / les filtres résonants rejouent ce démarrage brutal une fois la porte ouverte.
- **Makeup** — garde de pic ajoutée à la calibration : le makeup ne pousse jamais les boucles batterie et disco (normalisées à −3 dBFS) au-dessus de −1,5 dBFS (le soft clip démarre à −1 dBFS) ; plafond à +9 dB. Conséquence : le riser passe-haut (Sunrise Sweep) peut perdre jusqu'à −2,9 dB en haut de course ; le test 7 tolère −3 dB pour le mode passe-haut, ±1 dB pour les autres.
- **Pump de Sounds Better à 7 dB au lieu de 8** — à 8, la garde de pic faisait sortir la boucle de ±1 dB.
- **Mascotte : nouveau modèle d'émotions** — joyeuse à fond filtré, c'était à contresens. Désormais : filtre ouvert + musique = elle danse (2 poses en alternance sur les temps) ; en fermant = calme, puis concentrée, puis inquiète quand c'est étouffé ; **ré-ouverture rapide (le drop : le knob était au-dessus de 60 % et repasse sous 25 %) = euphorie pendant ~3,5 s** (les 2 poses les plus joyeuses + notes de musique) ; bypass = endormie. Hystérésis de ±0,04 entre les zones.
- **CPU** — DSP plus riche (5 SVF par canal, reverb) : ~46× temps réel sur un cœur en stéréo 48 kHz (~2 % d'un cœur par instance).

## Knob bipolaire (demande d'Alan du 2026-10-13)
- **Centre = neutre, gauche = côté passe-bas, droite = côté passe-haut** ; chaque preset définit deux côtés (`low` / `high`) avec leurs propres réglages (filtre, drive, enveloppe, grain, pump, room, phaser) et leurs étiquettes.
- **Le cadre générique gère le bipolaire** (`OneKnobConfig::bipolar`) : le moteur reçoit une macro signée −1..1 ; le makeup a une table par côté (`makeupLowDb` / `makeupHighDb`) ; Swing Delay pourra rester unipolaire.
- **Zone morte de ±1 % au centre** — le CC 64 d'un contrôleur MIDI vaut 64/127 = 50,4 %, pas 50 % ; sans zone morte, le « centre » d'un contrôleur ne serait jamais bit-exact. Le fondu dry/wet (zone de transparence) commence au bord de la zone morte.
- **Paramètre `amount` en ParameterID v2, défaut 0,5 ; état v2** avec migration : une session v1 (0..1 = passe-bas) est placée sur la moitié gauche (0 → centre).
- **Affichage** — `LP 42 %` · `—` (centre) · `HP 30 %` ; la saisie accepte `LP 40`, `HP 20`, `-40`, `+20`, `0`.
- **Knob** — l'arc part du haut : bleu vers la gauche, rouge vers la droite (le drapeau) ; repère au centre ; libellés LP / HP aux deux bouts ; aimant de ±1,5 % au centre pendant le glisser grossier ; double-clic, touche Début et menu « Back to centre » → centre ; étiquettes du côté actif seulement (rien au centre : visuel épuré).
- **Passe-haut 12 dB ajouté** — symétrie des pentes entre les deux côtés.
- **Makeup côté HP** — un passe-haut retire le kick : les hats / claps portent les pics, et la garde de pic limite la compensation. Le niveau de la boucle peut baisser de 2 à 7,2 dB tout au bout du côté HP — comportement attendu d'un riser, préféré à un soft clip qui travaille. Garde de pic plus stricte côté HP (−3 dBFS au lieu de −1,5) : quand le knob bouge, les balayages résonants dépassent les pics mesurés à position fixe d'environ 1,5 dB. Test 7 : côté LP ±1 dB ; côté HP, jamais plus de +1 dB, pics ≤ −1,4 dBFS, baisse ≤ 8 dB. Grain du côté HP de Touch 98 adouci (10 bits / 11 kHz au lieu de 8 bits / 7 kHz) : ses marches faisaient des pics à −0,7 dBFS sur le trajet du knob.

## Mascotte, deuxième planche (2026-10-13)
- **17 poses** : la nouvelle planche (12) + les 4 poses inutilisées de la première + la pose « inquiète » de la première. `asset_prep` compose plusieurs planches, met chaque planche à la même échelle (hauteur médiane du personnage), aligne tête et pieds, et ne garde que le personnage principal de chaque case (les pieds de la rangée du dessus débordaient).
- **Rôles** (`FrenchFilterMascot::poses()`) : danse au centre avec musique (5 poses, une par temps), sourire en coin au centre sans musique, échelle côté LP (alerte → sceptique → inquiet → abattu), échelle côté HP (alerte → poings levés → mains sur les oreilles), séquence du drop (yeux en étoile → saut bras en l'air → doigts pistolets → doigts pistolets en marchant → lunettes de soleil, qui reste), sommeil = pose abattue.
- **Drop** — le knob était à plus de 60 % d'un côté et revient près du centre (< 20 %) : séquence pendant 8 temps (3,5 s sans transport).

## Preset « Space » (façon Pioneer DJM, 2026-10-13)
- **Principe du Space des DJM** — reverb dont la quantité augmente en s'éloignant du centre ; le signal sec n'est pas filtré ; à gauche, la reverb est filtrée en passe-bas (sombre), à droite en passe-haut (aérienne).
- **Implémentation** — option de côté `"filter": {"target": "room"}` : le balayage du filtre part sur un filtre d'envoi 12 dB (LP + HP) placé avant la reverb, le chemin du signal reste ouvert ; mix de reverb jusqu'à 100 % dès le centre (`start` 0), `level` réglable par côté. Côté gauche plus retenu (niveau 0,45, coupure à 650 Hz) : à 0,75, le grave de la reverb (queue du kick) demandait −8 dB de makeup, ce qui écrasait aussi le sec.
- **Limite connue** — au retour au centre, la reverb est coupée en 10 ms (le centre est bit-exact par contrat) ; sur une vraie DJM, la queue est elle aussi interrompue. Une option « queue libre » est possible plus tard.
