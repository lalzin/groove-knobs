#!/usr/bin/env bash
# Rebuilds plugins/sing_it_back/Assets/sugar_*.png from Sugar's sprite sheets
# (green screen, 1254 px):
#   sugar_sheet_2.png  4 x 4: calm, arm out (mouth closed), pointing up
#                      (singing), jump with sparkles
#   sugar_sheet_1.png  rows 1-3 (4 x 3): calm, arm out singing, pointing up
#                      singing; row 4 (5 drawings): jump, wink jump, hand on
#                      heart, asleep sitting, asleep lying
# Frame roles: SugarMascot.h.
#   0-3 calm  4-7 inviting  8-11 pointing  12-15 jump      (sheet 2)
#   16-19 calm  20-23 singing, arm out  24-27 pointing     (sheet 1)
#   28 jump  29 wink jump  30 hand on heart  31-32 asleep  (sheet 1, row 4)
# The lying pose straddles the 5-column grid of row 4: it gets its own region,
# scaled by its height / the row's median height (176 / 276 px) so it keeps
# the same size as the other poses.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/sing_it_back/Assets/source
OUT=plugins/sing_it_back/Assets
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$OUT" --scale-to-first --height 520 \
  "$S/sugar_sheet_2.png@4x4:0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15" \
  "$S/sugar_sheet_1.png#0,0,1254,940@4x3:0,1,2,3,4,5,6,7,8,9,10,11" \
  "$S/sugar_sheet_1.png#0,940,1254,314@5x1:0,1,2,3" \
  "$S/sugar_sheet_1.png#975,940,279,314@1x1*0.638:0" "$@"
for f in "$OUT"/sugar_*.png; do [[ -e "$f" ]] && rm "$f"; done
for f in "$OUT"/mascot_*.png; do mv "$f" "$OUT/sugar_${f##*/mascot_}"; done
