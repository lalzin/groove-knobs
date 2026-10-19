#!/usr/bin/env bash
# Builds dist/<Plugin>-<version>-macOS.pkg (universal VST3 + AU + app).
#
#   scripts/make_installer_mac.sh [--skip-build] [FF|BT]
#   FF = French Filter (default), BT = Boogie Trio, BW = Backwash
#
# Signing: the "Developer ID Application" and "Developer ID Installer"
# certificates are found in the keychain automatically (or set DEV_ID_APP /
# DEV_ID_INSTALLER). Notarisation uses the keychain profile NOTARY_PROFILE
# (default "groove-knobs-notary", created once with
#   xcrun notarytool store-credentials groove-knobs-notary --apple-id <email> --team-id <TEAMID>
# ). Without certificates the package is ad-hoc signed: fine on your own Macs,
# blocked by Gatekeeper on others. UNSIGNED=1 forces the ad-hoc build.
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
# Developer ID identities from the keychain, unless given
if [[ "${UNSIGNED:-0}" != 1 ]]; then
  : "${DEV_ID_APP:=$(security find-identity -v -p codesigning 2>/dev/null | sed -nE 's/.*"(Developer ID Application: [^"]+)".*/\1/p' | head -1)}"
  : "${DEV_ID_INSTALLER:=$(security find-identity -v 2>/dev/null | sed -nE 's/.*"(Developer ID Installer: [^"]+)".*/\1/p' | head -1)}"
  : "${NOTARY_PROFILE:=groove-knobs-notary}"
else
  DEV_ID_APP=""; DEV_ID_INSTALLER=""; NOTARY_PROFILE=""
fi
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
    ent=()
    [[ "$b" == *.app ]] && ent=(--entitlements "$ROOT/installer/mac/standalone.entitlements")
    codesign --force --deep --timestamp --options runtime ${ent[@]+"${ent[@]}"} --sign "$DEV_ID_APP" "$b"
    codesign --verify --strict --deep "$b"
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

if [[ -n "${DEV_ID_INSTALLER:-}" ]]; then
  pkgutil --check-signature "$OUT" | sed -n 1,4p
  if [[ -n "${NOTARY_PROFILE:-}" ]] && xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1; then
    # Apple scans the package; "Accepted" or the script stops
    xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait | tee "$WORK/notary.log"
    grep -q "status: Accepted" "$WORK/notary.log" || { echo "notarisation failed: xcrun notarytool log <id> --keychain-profile $NOTARY_PROFILE"; exit 1; }
    xcrun stapler staple "$OUT"
    xcrun stapler validate "$OUT"
    spctl --assess --type install -vv "$OUT"
  else
    echo "NOTE: signed but NOT notarised (no notary profile \"$NOTARY_PROFILE\" in the keychain)."
  fi
fi

# Companion zip for the website: pkg + licences
( cd "$DIST" && rm -f "$FILE_NAME-$VERSION-macOS.zip" && \
  cp "$ROOT/installer/common/"{LICENSE.txt,THIRD_PARTY_NOTICES.txt,LilitaOne-OFL.txt} . && \
  zip -q "$FILE_NAME-$VERSION-macOS.zip" "$FILE_NAME-$VERSION-macOS.pkg" LICENSE.txt THIRD_PARTY_NOTICES.txt LilitaOne-OFL.txt && \
  rm LICENSE.txt THIRD_PARTY_NOTICES.txt LilitaOne-OFL.txt )
echo "Installer: $OUT"
[[ -n "${DEV_ID_INSTALLER:-}" ]] || echo "NOTE: unsigned (ad-hoc) — fine for your own Macs, blocked by Gatekeeper for public download. See header of this script."
