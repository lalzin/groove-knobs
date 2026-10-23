#!/usr/bin/env bash
# Rebuilds plugins/crackle/Assets/hiss_*.png from Hiss's sprite sheet
# (green screen, 4 x 4 poses, read row by row):
#   row 1  standing, standing (tall), standing, standing
#   row 2  standing, startled (mouth open), arms out (cheerful), standing
#   row 3  standing, squinting (leaning), squinting (leaning the other way), eyes closed
#   row 4  fists up (foot raised), standing, arms out (laughing), arms crossed
# Frame roles: CrackleMascot.h.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/crackle/Assets/source
OUT=plugins/crackle/Assets
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$OUT" --despill --height 520 \
  "$S/hiss_sheet.png@4x4:0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15" "$@"
for f in "$OUT"/hiss_*.png; do [[ -e "$f" ]] && rm "$f"; done
for f in "$OUT"/mascot_*.png; do mv "$f" "$OUT/hiss_${f##*/mascot_}"; done
