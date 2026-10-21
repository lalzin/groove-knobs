#!/usr/bin/env bash
# Rebuilds plugins/kick_lock/Assets/phaze_*.png from Phaze's sprite sheets
# (flat green background; the drum itself is lime, so the key is the sampled
# background colour, not green dominance):
#   phaze_sheet_happy.png  4 poses in a row: arms up, dance, jump, dance
#   phaze_sheet_calm.png   4 poses in a row: calm, standing, jump (fists), calm
#   phaze_sheet_moods.png  4 x 4: asleep, puzzled "?", tapping its foot, worried
# Frame roles (PhazeMascot.h):
#   0-3 happy  4-7 calm  8 asleep (one frame)  9-12 puzzled  13-16 foot tap  17-20 worried
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/kick_lock/Assets/source
OUT=plugins/kick_lock/Assets
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$OUT" --key-flat --despill --height 520 \
  "$S/phaze_sheet_happy.png@4x1:0,1,2,3" "$S/phaze_sheet_calm.png@4x1:0,1,2,3" \
  "$S/phaze_sheet_moods.png@4x4:0,4,5,6,7,8,9,10,11,12,13,14,15" "$@"
for f in "$OUT"/phaze_*.png; do [[ -e "$f" ]] && rm "$f"; done
for f in "$OUT"/mascot_*.png; do mv "$f" "$OUT/phaze_${f##*/mascot_}"; done
