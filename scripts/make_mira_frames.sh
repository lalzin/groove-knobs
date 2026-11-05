#!/usr/bin/env bash
# Rebuilds BALLROOM's Mira (the disco ball) from Alan's sheets (flat green
# background): five 2 x 2 sheets (mira_12..16) and the hero pose (mira_17), in
# that order -> plugins/ballroom/Assets/mira_0..20.png; the ground shadows (tinted by the green, each its own
# shade) removed (--drop-hue, the bottom fifth): MiraMascot draws one shadow for every pose. Roles: MiraMascot.h.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/ballroom/Assets/source
OUT=plugins/ballroom/Assets
TMP="$(mktemp -d)"
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$TMP" --key-flat --despill --height 520 --scale-to-first --drop-hue 50,220,0.05,0.2 \
  "$S/mira_12.png@2x2:0,1,2,3" "$S/mira_13.png@2x2:0,1,2,3" "$S/mira_14.png@2x2:0,1,2,3" \
  "$S/mira_15.png@2x2:0,1,2,3" "$S/mira_16.png@2x2:0,1,2,3" "$S/mira_17.png@1x1:0"
rm -f "$OUT"/mira_*.png
for f in "$TMP"/mascot_*.png; do mv "$f" "$OUT/mira_${f##*/mascot_}"; done
rm -rf "$TMP"
