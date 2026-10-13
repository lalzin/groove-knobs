#!/usr/bin/env bash
# Rebuilds plugins/french_filter/Assets/mascot_*.png from the two sprite sheets.
# Frame order = pose roles used by FrenchFilterMascot (see its poses()).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
O=plugins/french_filter/Assets/source/mascot_sheet.png
N=plugins/french_filter/Assets/source/mascot_sheet_2.jpg
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep plugins/french_filter/Assets \
  "$O:0" "$N:0,1,2,3,4,5,9" "$O:10,8" "$N:7,6,10" "$O:11" "$N:8" "$O:9" "$N:11" "$@"
