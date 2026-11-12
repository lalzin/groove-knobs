#!/usr/bin/env bash
# Rebuilds DAFT CHUNK's Chunky (the block robot) from Alan's two 4 x 4 sheets
# (flat green background) -> plugins/daft_chunk/Assets/chunky_0..31.png:
#   0..15  sheet B, the spec's order: row 1 idle bounce, row 2 shaking the
#          dice, row 3 glitch (the face changes; the blocks shift in code),
#          row 4 giving a loop (the block held, stretched out, thumbs up)
#   16..31 sheet A: dancing (arms up, the cube tossed, hugged; steps; the
#          fist, the cube thrown, sitting down)
# --flood-key: only the green reached from the border goes (the navy limbs
# and the screen's black stay whole). Roles: ChunkyMascot.h.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/daft_chunk/Assets/source
OUT=plugins/daft_chunk/Assets
TMP="$(mktemp -d)"
all="0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15"
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$TMP" --key-flat --flood-key --despill --height 400 --scale-to-first \
  "$S/chunky_sheet_b.png@4x4:$all" "$S/chunky_sheet_a.png@4x4:$all"
rm -f "$OUT"/chunky_*.png
for f in "$TMP"/mascot_*.png; do mv "$f" "$OUT/chunky_${f##*/mascot_}"; done
rm -rf "$TMP"
