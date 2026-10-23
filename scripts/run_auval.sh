#!/usr/bin/env bash
# Validates the installed Audio Unit with Apple's auval.
# The codes come from cmake/GrooveIds.cmake (single source of truth).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ids="$ROOT/cmake/GrooveIds.cmake"
get() { sed -nE "s/^set\($1 +\"([^\"]*)\".*/\1/p" "$ids"; }
# scripts/run_auval.sh [FF|BT|BW|PP|SIB|KL|KLS|SL|CR] [build-dir]   (default FF = French Filter, BT = Boogie Trio, BW = Backwash, PP = Pew Pew, SL = Slapper)
P="${1:-FF}"
TYPE="$(get ${P}_AU_TYPE)"; SUB="$(get ${P}_PLUGIN_CODE)"; MANU="$(get GK_MANUFACTURER_CODE)"
echo "auval -v $TYPE $SUB $MANU"
# auval only finds registered components: the build's AU is put in the user
# Components folder for the validation only, then removed (the build no longer
# copies plug-ins anywhere; a DAW keeps using the installed ones).
BUILD="${2:-$ROOT/build}"
case "$P" in
  FF) ART="$BUILD/plugins/french_filter/FrenchFilter_artefacts/Release" ;;
  BT) ART="$BUILD/plugins/boogie_trio/BoogieTrio_artefacts/Release" ;;
  BW) ART="$BUILD/plugins/backwash/Backwash_artefacts/Release" ;;
  PP) ART="$BUILD/plugins/pew_pew/PewPew_artefacts/Release" ;;
  SIB) ART="$BUILD/plugins/sing_it_back/SingItBack_artefacts/Release" ;;
  KL) ART="$BUILD/plugins/kick_lock/KickLock_artefacts/Release" ;;
  KLS) ART="$BUILD/plugins/kick_lock_send/KickLockSend_artefacts/Release" ;;
  SL) ART="$BUILD/plugins/slapper/Slapper_artefacts/Release" ;;
  CR) ART="$BUILD/plugins/crackle/Crackle_artefacts/Release" ;;
esac
COMP="$(ls -d "$ART"/AU/*.component 2>/dev/null | head -1 || true)"
TMP=""
if [[ -n "$COMP" ]]; then
  DEST="$HOME/Library/Audio/Plug-Ins/Components/$(basename "$COMP")"
  if [[ ! -e "$DEST" ]]; then
    TMP="$DEST"
    trap 'rm -rf "$TMP"; killall -9 AudioComponentRegistrar 2>/dev/null || true' EXIT
    cp -R "$COMP" "$DEST"
  fi
fi
# Make sure the AU cache sees a freshly installed component.
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v "$TYPE" "$SUB" "$MANU"
