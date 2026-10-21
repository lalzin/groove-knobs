#!/usr/bin/env bash
# Validates the built VST3 and AU with pluginval.
#   scripts/run_pluginval.sh [build-dir] [strictness] [plugin]
#   defaults: build, 10, "French Filter"   (plugin: "French Filter" | "Boogie Trio" | "Backwash" | "Pew Pew")
# Downloads pluginval 1.0.4 into .deps/pluginval if it is not installed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
STRICT="${2:-10}"
PLUGIN="${3:-French Filter}"
case "$PLUGIN" in
  "French Filter") ART="$BUILD/plugins/french_filter/FrenchFilter_artefacts/Release" ;;
  "Boogie Trio")   ART="$BUILD/plugins/boogie_trio/BoogieTrio_artefacts/Release" ;;
  "Backwash")      ART="$BUILD/plugins/backwash/Backwash_artefacts/Release" ;;
  "Pew Pew")       ART="$BUILD/plugins/pew_pew/PewPew_artefacts/Release" ;;
  "Sing It Back")  ART="$BUILD/plugins/sing_it_back/SingItBack_artefacts/Release" ;;
  *) echo "unknown plugin: $PLUGIN"; exit 2 ;;
esac

PV="$(command -v pluginval || true)"
if [[ -z "$PV" ]]; then
  PV="$ROOT/.deps/pluginval/pluginval.app/Contents/MacOS/pluginval"
  if [[ ! -x "$PV" ]]; then
    mkdir -p "$ROOT/.deps/pluginval"
    curl -sSL -o "$ROOT/.deps/pluginval/pluginval_macOS.zip" \
      https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip
    (cd "$ROOT/.deps/pluginval" && unzip -q -o pluginval_macOS.zip)
  fi
fi

status=0
for plugin in "$ART/VST3/$PLUGIN.vst3" "$ART/AU/$PLUGIN.component"; do
  [[ -e "$plugin" ]] || { echo "missing: $plugin"; status=1; continue; }
  log="$BUILD/pluginval_$(basename "$plugin" | tr ' ' '_').log"
  echo "== pluginval strictness $STRICT: $plugin"
  if "$PV" --strictness-level "$STRICT" --validate-in-process --timeout-ms 600000 "$plugin" >"$log" 2>&1; then
    echo "   SUCCESS (log: $log)"
  else
    echo "   FAILED (log: $log)"; grep -E "FAILED|!!!" "$log" | head -20; status=1
  fi
done
exit $status
