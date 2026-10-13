#!/usr/bin/env bash
# Validates the installed Audio Unit with Apple's auval.
# The codes come from cmake/GrooveIds.cmake (single source of truth).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ids="$ROOT/cmake/GrooveIds.cmake"
get() { sed -nE "s/^set\($1 +\"([^\"]*)\".*/\1/p" "$ids"; }
# scripts/run_auval.sh [FF|BT]   (default FF = French Filter, BT = Boogie Trio)
P="${1:-FF}"
TYPE="$(get ${P}_AU_TYPE)"; SUB="$(get ${P}_PLUGIN_CODE)"; MANU="$(get GK_MANUFACTURER_CODE)"
echo "auval -v $TYPE $SUB $MANU"
# Make sure the AU cache sees a freshly installed component.
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v "$TYPE" "$SUB" "$MANU"
