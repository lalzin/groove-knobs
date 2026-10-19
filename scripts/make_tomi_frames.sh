#!/usr/bin/env bash
# Rebuilds plugins/pew_pew/Assets/tomi_*.png from Tomi's sprite sheets (flat
# green background, grid lines erased). Two hit sheets, 4 x 4: each row is one
# hit in four phases (stick raised, downswing, impact, follow-through):
#   tomi_hits_one_hand.png  (gentler: one stick, hand on the hip)
#   tomi_hits_two_hands.png (stronger: both sticks, big sparks)
# Rows ordered by energy (TomiMascot): within a sheet, row 1 cheerful < row 3
# smug < row 2 star eyes < row 4 strained -> dizzy.
#   0-15  one hand:  rows 1, 3, 2, 4 (4 frames each)
#   16-31 two hands: rows 1, 3, 2, 4
#   32-34 asleep (sitting, eyes closing) from tomi_sheet_2.png, last row
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/pew_pew/Assets/source
OUT=plugins/pew_pew/Assets
ORDER="0,1,2,3,8,9,10,11,4,5,6,7,12,13,14,15"
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$OUT" --key-flat --scale-to-first --erase-grid --height 520 \
  "$S/tomi_hits_one_hand.png@4x4:$ORDER" "$S/tomi_hits_two_hands.png@4x4:$ORDER" \
  "$S/tomi_sheet_2.png@4x4:12,13,14" "$@"
rm -f "$OUT"/tomi_*.png.keep
for f in "$OUT"/tomi_*.png; do [[ -e "$f" ]] && mv "$f" "$f.old"; done
for f in "$OUT"/mascot_*.png; do mv "$f" "$OUT/tomi_${f##*/mascot_}"; done
rm -f "$OUT"/tomi_*.png.old
