#!/usr/bin/env bash
# Rebuilds plugins/boogie_trio/Assets/velvet_*.png from the Velvet sprite sheet
# (flat sage background). Frame order = roles used by VelvetMascot (see poses()).
# Cell 4 (open mouth, singing) is deliberately not used.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/boogie_trio/Assets/source/velvet_sheet.png
OUT=plugins/boogie_trio/Assets
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$OUT" --key-flat --auto-cells --height 520 "$S:0,1,2,3,5,6,7,8,9,10,11" "$@"
for f in "$OUT"/mascot_*.png; do mv "$f" "$OUT/velvet_${f##*/mascot_}"; done
