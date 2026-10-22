#!/usr/bin/env bash
# Rebuilds plugins/slapper/Assets/pluck_*.png from Pluck's sprite sheets
# (green screen; the "Zzz" drawn on the sleeping pose is painted out):
#   sheet10.png  4 poses: rest, slap (hand on the strings), pop (jump), ghost (cool, hands on the body)
#   sheet8.png   3 poses: jump arms up, laughing (its shake lines painted out), asleep
#   sheet9.png   3 poses: smiling, arms out, eyes closed
# Frame roles (PluckMascot.h):
#   0 rest  1 slap  2 pop  3 ghost  4 hype (arms up)  5 laughing  6 asleep  7 smiling  8 arms out  9 eyes closed
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --build "$ROOT/build" --target asset_prep >/dev/null
cd "$ROOT"
S=plugins/slapper/Assets/source
OUT=plugins/slapper/Assets
build/tools/asset_prep/asset_prep_artefacts/Release/asset_prep "$OUT" --despill --height 520 \
  "$S/sheet10.png@4x1:0,1,2,3" "$S/sheet8.png~1570,140,150,175;598,370,42,250;1040,380,85,230@3x1:0,1,2" "$S/sheet9.png@3x1:0,1,2" "$@"
for f in "$OUT"/pluck_*.png; do [[ -e "$f" ]] && rm "$f"; done
for f in "$OUT"/mascot_*.png; do mv "$f" "$OUT/pluck_${f##*/mascot_}"; done
