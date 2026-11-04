#!/usr/bin/env bash
# Rebuilds HEAD SPIN's Kiko from Alan's sheets (green screen):
#   plugins/headspin/Assets/kiko_top.png   the view from above (kiko_top_view.png; drawn with the
#                                          face down: the plugin turns it so the front is up)
#   plugins/headspin/Assets/kiko_0..15.png the 16 poses seen from the front (kiko_sheet.png, 4 x 4,
#                                          grid lines erased), read row by row:
#     row 1  standing, leaning (groove), standing, looking aside
#     row 2  standing, hand on the right headphone, standing, hand on the left headphone
#     row 3  crouching (into the spin), head spin, head spin (legs open), arms crossed
#     row 4  standing, cap flying off (surprised), cap flying (waving), hands on hips
# Frame roles: KikoMascot.h.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/headspin/Assets/source
OUT=plugins/headspin/Assets
TOOL=build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep
TMP="$(mktemp -d)"
"$TOOL" "$TMP/top" --despill --height 600 "$S/kiko_top_view.png@1x1:0"
# 13 and 14 (the cap flying off): the cap reaches above the grid line into
# the row above, so these two are cut from a region starting higher (above
# the line, below the row above's drawings), at the sheet's scale (--scale-to-first)
"$TOOL" "$TMP/poses" --despill --erase-grid --inset 4 --height 520 --scale-to-first \
  "$S/kiko_sheet.png@4x4:0,1,2,3,4,5,6,7,8,9,10,11,12" \
  "$S/kiko_sheet.png#304,940,606,355@2x1:0,1" \
  "$S/kiko_sheet.png@4x4:15"
rm -f "$OUT"/kiko_*.png
mv "$TMP/top/mascot_0.png" "$OUT/kiko_top.png"
for f in "$TMP"/poses/mascot_*.png; do mv "$f" "$OUT/kiko_${f##*/mascot_}"; done
rm -rf "$TMP"
