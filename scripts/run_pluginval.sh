#!/usr/bin/env bash
# Validates the built VST3 and AU with pluginval.
#   scripts/run_pluginval.sh [build-dir] [strictness]   (defaults: build, 10)
# Downloads pluginval 1.0.4 into .deps/pluginval if it is not installed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
STRICT="${2:-10}"
ART="$BUILD/plugins/french_filter/FrenchFilter_artefacts/Release"

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
for plugin in "$ART/VST3/French Filter.vst3" "$HOME/Library/Audio/Plug-Ins/Components/French Filter.component"; do
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
