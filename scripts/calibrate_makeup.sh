#!/usr/bin/env bash
# Regenerates the makeupDb tables of every French Filter preset.
# ff_render passes pink noise and the synthetic drum loop (kick, clap, hats)
# through the chain at amount = 0, 0.01 ... 1, measures the K-weighted level
# against the dry signal and writes the 101 values into each preset JSON.
# Rebuild afterwards (presets are embedded as BinaryData).
#   scripts/calibrate_makeup.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
cmake --build "$BUILD" --target ff_render
FF="$BUILD/tools/ff_render/ff_render_artefacts/Release/ff_render"
cd "$ROOT/plugins/french_filter/Presets"
"$FF" calibrate *.json --passes 3
cmake --build "$BUILD"
"$FF" measure
