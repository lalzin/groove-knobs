#!/usr/bin/env bash
# Builds dist/<Plugin>-<version>-macOS.pkg (universal VST3 + AU + app).
#
#   scripts/make_installer_mac.sh [--skip-build] [FF|BT]
#   FF = French Filter (default), BT = Boogie Trio, BW = Backwash
#
# Free public download: without an Apple Developer ID the package is only
# ad-hoc signed and Gatekeeper will block it on other Macs (users would have
# to right-click → Open, or allow it in System Settings → Privacy & Security). To sign and
# notarise, export before running:
#   DEV_ID_APP="Developer ID Application: Your Name (TEAMID)"
#   DEV_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)"
#   NOTARY_PROFILE="profile-name"   # created once with: xcrun notarytool store-credentials
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ids="$ROOT/cmake/GrooveIds.cmake"
# Reads a value from GrooveIds.cmake, expanding ${OTHER_VARIABLES} it references.
get() {
  local v; v="$(sed -nE "s/^set\($1 +\"([^\"]*)\".*/\1/p" "$ids")"
  while [[ "$v" =~ \$\{([A-Z0-9_]+)\} ]]; do
    local name="${BASH_REMATCH[1]}"
    v="${v//\$\{$name\}/$(get "$name")}"
  done
  printf '%s' "$v"
}
SKIP_BUILD=0; P=FF
for a in "$@"; do
  case "$a" in
    --skip-build) SKIP_BUILD=1 ;;
    FF|BT|BW) P="$a" ;;
    *) echo "unknown argument: $a"; exit 2 ;;
  esac
done
VERSION="$(get ${P}_VERSION)"
BUNDLE_ID="$(get ${P}_BUNDLE_ID)"
PREFIX="$(get GK_BUNDLE_ID_PREFIX)"
PRODUCT="$(get ${P}_PRODUCT_NAME)"
case "$P" in
  FF) DIR=french_filter; TARGET=FrenchFilter; RES="$ROOT/installer/mac" ;;
  BT) DIR=boogie_trio;   TARGET=BoogieTrio;   RES="$ROOT/installer/mac/boogie_trio" ;;
  BW) DIR=backwash;      TARGET=Backwash;     RES="$ROOT/installer/mac/backwash" ;;
esac
FILE_NAME="${PRODUCT// /}"   # "French Filter" -> FrenchFilter

[[ "$SKIP_BUILD" == 1 ]] || "$ROOT/scripts/build_mac.sh" --no-tests
ART="$ROOT/build-release/plugins/$DIR/${TARGET}_artefacts/Release"
WORK="$ROOT/build-release/installer-$P"
DIST="$ROOT/dist"
rm -rf "$WORK"; mkdir -p "$WORK"/{vst3,au,app,res} "$DIST"

cp -R "$ART/VST3/$PRODUCT.vst3" "$WORK/vst3/"
cp -R "$ART/AU/$PRODUCT.component" "$WORK/au/"
cp -R "$ART/Standalone/$PRODUCT.app" "$WORK/app/"

for b in "$WORK/vst3/$PRODUCT.vst3" "$WORK/au/$PRODUCT.component" "$WORK/app/$PRODUCT.app"; do
  lipo -info "$b/Contents/MacOS/$PRODUCT" | grep -q "x86_64 arm64\|arm64 x86_64" || { echo "not universal: $b"; exit 1; }
  if [[ -n "${DEV_ID_APP:-}" ]]; then
    codesign --force --deep --timestamp --options runtime --sign "$DEV_ID_APP" "$b"
  else
    codesign --force --deep --sign - "$b"
  fi
done

# Component packages; bundles must not be "relocated" to an existing copy.
component() { # name dir install-location
  local plist="$WORK/$1.plist"
  pkgbuild --analyze --root "$WORK/$2" "$plist" >/dev/null
  local i=0
  while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null \
      || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsVersionChecked false" "$plist" 2>/dev/null || true
    i=$((i + 1))
  done
  local extra=()
  [[ "$1" == "au" ]] && extra=(--scripts "$ROOT/installer/mac/scripts")
  pkgbuild --root "$WORK/$2" --component-plist "$plist" --install-location "$3" \
    --identifier "$BUNDLE_ID.$1" --version "$VERSION" ${extra[@]+"${extra[@]}"} "$WORK/$1.pkg"
}
component vst3 vst3 "/Library/Audio/Plug-Ins/VST3"
component au au "/Library/Audio/Plug-Ins/Components"
component app app "/Applications"

cp "$ROOT/installer/common/LICENSE.txt" "$RES/welcome.html" "$RES/conclusion.html" "$WORK/res/"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@BUNDLE_ID@/$BUNDLE_ID/g" -e "s/@BUNDLE_PREFIX@/$PREFIX/g" -e "s/@PRODUCT@/$PRODUCT/g" \
  "$ROOT/installer/mac/distribution.xml.in" > "$WORK/distribution.xml"

OUT="$DIST/$FILE_NAME-$VERSION-macOS.pkg"
sign=()
[[ -n "${DEV_ID_INSTALLER:-}" ]] && sign=(--sign "$DEV_ID_INSTALLER" --timestamp)
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/res" --package-path "$WORK" ${sign[@]+"${sign[@]}"} "$OUT"

if [[ -n "${NOTARY_PROFILE:-}" && -n "${DEV_ID_INSTALLER:-}" ]]; then
  xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$OUT"
fi

# Companion zip for the website: pkg + licences
( cd "$DIST" && rm -f "$FILE_NAME-$VERSION-macOS.zip" && \
  cp "$ROOT/installer/common/"{LICENSE.txt,THIRD_PARTY_NOTICES.txt,LilitaOne-OFL.txt} . && \
  zip -q "$FILE_NAME-$VERSION-macOS.zip" "$FILE_NAME-$VERSION-macOS.pkg" LICENSE.txt THIRD_PARTY_NOTICES.txt LilitaOne-OFL.txt && \
  rm LICENSE.txt THIRD_PARTY_NOTICES.txt LilitaOne-OFL.txt )
echo "Installer: $OUT"
[[ -n "${DEV_ID_INSTALLER:-}" ]] || echo "NOTE: unsigned (ad-hoc) — fine for your own Macs, blocked by Gatekeeper for public download. See header of this script."
