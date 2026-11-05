#!/usr/bin/env bash
# Rebuilds BALLROOM's Mira (the disco ball) from Alan's sheets (flat green
# background): five 2 x 2 sheets (mira_12..16) and the hero pose (mira_17), in
# that order -> plugins/ballroom/Assets/mira_0..20.png. --flood-key: only the background reached from the
# border goes (the slate facets, as dark as the green, stay whole: no holes); the ground shadows (dark teal,
# hue 140-205) go with it (MiraMascot draws one shadow for every pose); --match-colours: the gold, lenses,
# facets and creams of every frame brought to frame 0's (the sheets were drawn apart). Roles: MiraMascot.h.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/ballroom/Assets/source
OUT=plugins/ballroom/Assets
TMP="$(mktemp -d)"
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$TMP" --key-flat --flood-key --drop-ground-shadow 140,205 --despill --match-colours --height 520 --scale-to-first \
  "$S/mira_12.png@2x2:0,1,2,3" "$S/mira_13.png@2x2:0,1,2,3" "$S/mira_14.png@2x2:0,1,2,3" \
  "$S/mira_15.png@2x2:0,1,2,3" "$S/mira_16.png@2x2:0,1,2,3" "$S/mira_17.png@1x1:0"
rm -f "$OUT"/mira_*.png
for f in "$TMP"/mascot_*.png; do mv "$f" "$OUT/mira_${f##*/mascot_}"; done
rm -rf "$TMP"
