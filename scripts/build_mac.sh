#!/usr/bin/env bash
# Release build on macOS: universal binary (arm64 + x86_64), VST3 + AU +
# Standalone, copied to ~/Library/Audio/Plug-Ins and ad-hoc signed by JUCE.
#   scripts/build_mac.sh [--native] [--no-tests]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCHS="arm64;x86_64"; BUILD="$ROOT/build-release"; TESTS=ON
for a in "$@"; do
  case "$a" in
    --native) ARCHS="$(uname -m)"; BUILD="$ROOT/build" ;;
    --no-tests) TESTS=OFF ;;
  esac
done
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DGK_BUILD_TESTS=$TESTS
cmake --build "$BUILD" --parallel
if [[ "$TESTS" == ON ]]; then
  ctest --test-dir "$BUILD" --output-on-failure
fi
echo "Installed: ~/Library/Audio/Plug-Ins/VST3/French Filter.vst3 and Components/French Filter.component"
echo "Ableton: Preferences > Plug-Ins > Rescan (hold Alt/Option for a full rescan)."
