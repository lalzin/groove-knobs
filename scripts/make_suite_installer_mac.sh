#!/usr/bin/env bash
# Builds dist/GrooveKnobs-<version>-macOS.pkg: the whole suite in one installer.
#
#   scripts/make_suite_installer_mac.sh [--skip-build | --check-versions]
#   (--check-versions: only checks every plug-in is at the suite's version)
#
# Reuses each plug-in's component packages (VST3 / AU / app, their bundles
# already signed, notarised and stapled) made by make_installer_mac.sh, which
# is run for any plug-in whose packages are missing or of another version.
# One choice per plug-in, each with its three formats (Customize); all on.
# Signed with the Developer ID Installer certificate and notarised like the
# single installers (same NOTARY_PROFILE, see make_installer_mac.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ids="$ROOT/cmake/GrooveIds.cmake"
get() {
  local v; v="$(sed -nE "s/^set\($1 +\"([^\"]*)\".*/\1/p" "$ids")"
  while [[ "$v" =~ \$\{([A-Z0-9_]+)\} ]]; do
    local name="${BASH_REMATCH[1]}"
    v="${v//\$\{$name\}/$(get "$name")}"
  done
  printf '%s' "$v"
}
SKIP_BUILD=0
[[ "${1:-}" == "--skip-build" ]] && SKIP_BUILD=1
if [[ "${UNSIGNED:-0}" != 1 ]]; then
  : "${DEV_ID_INSTALLER:=$(security find-identity -v 2>/dev/null | sed -nE 's/.*"(Developer ID Installer: [^"]+)".*/\1/p' | head -1)}"
  : "${NOTARY_PROFILE:=groovy-knobs}"
else
  DEV_ID_INSTALLER=""; NOTARY_PROFILE=""
fi

VERSION="$(get GK_SUITE_VERSION)"
PREFIX="$(get GK_BUNDLE_ID_PREFIX)"
PLUGINS=(FF BT BW PP SIB KL SL)
for P in "${PLUGINS[@]}"; do
  [[ "$(get ${P}_VERSION)" == "$VERSION" ]] || { echo "$P is $(get ${P}_VERSION), the suite is $VERSION: align cmake/GrooveIds.cmake"; exit 1; }
done
[[ "${1:-}" == "--check-versions" ]] && exit 0

[[ "$SKIP_BUILD" == 1 ]] || "$ROOT/scripts/build_mac.sh" --no-tests
WORK="$ROOT/build-release/installer-suite"
DIST="$ROOT/dist"
rm -rf "$WORK"; mkdir -p "$WORK/res" "$DIST"

outline=""; choices=""; refs=""
for P in "${PLUGINS[@]}"; do
  SRC="$ROOT/build-release/installer-$P"
  # each plug-in's component packages, of this version (else make them)
  if ! grep -q "version=\"$VERSION\"" "$SRC/distribution.xml" 2>/dev/null || [[ ! -f "$SRC/vst3.pkg" ]]; then
    "$ROOT/scripts/make_installer_mac.sh" --skip-build "$P"
  fi
  PRODUCT="$(get ${P}_PRODUCT_NAME)"
  BUNDLE_ID="$(get ${P}_BUNDLE_ID)"
  KIND="effet audio"; [[ "$P" == PP || "$P" == SL ]] && KIND="instrument"; [[ "$P" == KL ]] && KIND="effet audio, avec Kick Lock Send"
  outline+="        <line choice=\"$P\">
            <line choice=\"$P-vst3\"/>
            <line choice=\"$P-au\"/>
            <line choice=\"$P-app\"/>
        </line>
"
  choices+="    <choice id=\"$P\" title=\"$PRODUCT\" description=\"$PRODUCT $VERSION ($KIND) : VST3, Audio Unit, application\"/>
    <choice id=\"$P-vst3\" title=\"VST3\" description=\"$PRODUCT.vst3 → /Library/Audio/Plug-Ins/VST3\"><pkg-ref id=\"$BUNDLE_ID.vst3\"/></choice>
    <choice id=\"$P-au\" title=\"Audio Unit\" description=\"$PRODUCT.component → /Library/Audio/Plug-Ins/Components (Live, Logic, GarageBand)\"><pkg-ref id=\"$BUNDLE_ID.au\"/></choice>
    <choice id=\"$P-app\" title=\"Application\" description=\"$PRODUCT.app → /Applications\"><pkg-ref id=\"$BUNDLE_ID.app\"/></choice>
"
  for f in vst3 au app; do
    cp "$SRC/$f.pkg" "$WORK/$P-$f.pkg"
    refs+="    <pkg-ref id=\"$BUNDLE_ID.$f\" version=\"$VERSION\">$P-$f.pkg</pkg-ref>
"
  done
done

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Groove Knobs $VERSION — by LALZIN, with groove and love</title>
    <organization>$PREFIX</organization>
    <domains enable_localSystem="true"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="LICENSE.txt" mime-type="text/plain"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <allowed-os-versions><os-version min="10.13"/></allowed-os-versions>
    <choices-outline>
$outline    </choices-outline>
$choices$refs</installer-gui-script>
EOF
cp "$ROOT/installer/common/LICENSE.txt" "$ROOT/installer/mac/suite/welcome.html" "$ROOT/installer/mac/suite/conclusion.html" "$WORK/res/"

OUT="$DIST/GrooveKnobs-$VERSION-macOS.pkg"
sign=()
[[ -n "${DEV_ID_INSTALLER:-}" ]] && sign=(--sign "$DEV_ID_INSTALLER" --timestamp)
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/res" --package-path "$WORK" ${sign[@]+"${sign[@]}"} "$OUT"

if [[ -n "${DEV_ID_INSTALLER:-}" && -n "${NOTARY_PROFILE:-}" ]] && xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1; then
  xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait | tee "$WORK/notary.log"
  grep -q "status: Accepted" "$WORK/notary.log" || { echo "notarisation failed: xcrun notarytool log <id> --keychain-profile $NOTARY_PROFILE"; exit 1; }
  xcrun stapler staple "$OUT"
  xcrun stapler validate "$OUT"
  spctl --assess --type install -vv "$OUT"
else
  echo "NOTE: not signed / notarised (no Developer ID Installer certificate or notary profile)."
fi

# Companion zip for the website: pkg + licences
( cd "$DIST" && rm -f "GrooveKnobs-$VERSION-macOS.zip" && \
  cp "$ROOT/installer/common/"{LICENSE.txt,THIRD_PARTY_NOTICES.txt,LilitaOne-OFL.txt} . && \
  zip -q "GrooveKnobs-$VERSION-macOS.zip" "GrooveKnobs-$VERSION-macOS.pkg" LICENSE.txt THIRD_PARTY_NOTICES.txt LilitaOne-OFL.txt && \
  rm LICENSE.txt THIRD_PARTY_NOTICES.txt LilitaOne-OFL.txt )
echo "Installer: $OUT"
