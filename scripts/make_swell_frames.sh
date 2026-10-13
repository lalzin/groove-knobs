#!/usr/bin/env bash
# Rebuilds plugins/backwash/Assets/swell_*.png from Swell's sprite sheet (flat
# background, 4 x 3). Frame order = roles used by SwellMascot (see poses):
#   0 beach, bored (also the sleep pose)   1-3 beach, board under the arm, smiling
#   4-5 lying on the board, paddling        6 kneeling on the board   7 standing, surfing
#   8-10 crouched, surfing                  11 euphoric on the wave
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/backwash/Assets/source/swell_sheet.png
OUT=plugins/backwash/Assets
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$OUT" --key-flat --auto-cells --height 520 "$S:0,1,2,3,4,5,6,7,8,9,10,11" "$@"
for f in "$OUT"/mascot_*.png; do mv "$f" "$OUT/swell_${f##*/mascot_}"; done
