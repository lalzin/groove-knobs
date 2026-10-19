#!/usr/bin/env bash
# Rebuilds plugins/pew_pew/Assets/tomi_*.png from Tomi's two sprite sheets
# (flat green background, 256 px cells). Sheet 1 is irregular: a 4-frame top
# row, a 2 x 3 block on the left and one big jump on the right (cut on its own
# and brought to the same size). From sheet 2 (4 x 4), Alan kept the top row,
# the 2nd of row 2 and the first three of the last row. Frame order = poses in
# TomiMascot:
#   0-3   calm, sticks at the chest (0 %)      4 smile   5 stick twirl (25 %)
#   6-7   arms out, sparkles (50 %)            8-9 sticks up, sparkles (75 %)
#   10    euphoric jump (100 %)                11-14 strike cycle (sticks up -> hit)
#   15    enthusiasm (arms open)               16-18 asleep (sitting, eyes closing)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/pew_pew/Assets/source
OUT=plugins/pew_pew/Assets
S1="$S/tomi_sheet_1.png"
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$OUT" --key-flat --scale-to-first --erase-grid --height 520 \
  "$S1#0,0,1024,256@4x1:0,1,2,3" "$S1#0,256,512,768@2x3:0,1,2,3,4,5" "$S1#512,256,512,768@1x1*1.15:0" \
  "$S/tomi_sheet_2.png@4x4:0,1,2,3,5,12,13,14" "$@"
for f in "$OUT"/mascot_*.png; do mv "$f" "$OUT/tomi_${f##*/mascot_}"; done
