#!/usr/bin/env bash
# Imports the Developer ID certificates downloaded from developer.apple.com
# with the private keys generated for their signing requests
# (~/.groove-knobs-signing), into the login keychain.
#
#   scripts/import_developer_id.sh [application.cer] [installer.cer]
#   (default: ~/Downloads/developerID_application.cer, ~/Downloads/developerID_installer.cer)
#
# Then check: security find-identity -v   (both "Developer ID" lines listed)
set -euo pipefail
KEYS="$HOME/.groove-knobs-signing"
APP_CER="${1:-$HOME/Downloads/developerID_application.cer}"
INST_CER="${2:-$HOME/Downloads/developerID_installer.cer}"
KC="$HOME/Library/Keychains/login.keychain-db"

for f in "$APP_CER" "$INST_CER" "$KEYS/devid_application.key" "$KEYS/devid_installer.key"; do
  [[ -f "$f" ]] || { echo "missing: $f"; exit 1; }
done

# Each certificate must match its key (same public key), or nothing is imported
pub() { openssl x509 -inform DER -in "$1" -noout -pubkey 2>/dev/null || openssl x509 -in "$1" -noout -pubkey; }
check() {
  [[ "$(pub "$1" | openssl md5)" == "$(openssl rsa -in "$2" -pubout 2>/dev/null | openssl md5)" ]] \
    || { echo "$1 does not match $2 (was it made from the right .certSigningRequest?)"; exit 1; }
}
check "$APP_CER" "$KEYS/devid_application.key"
check "$INST_CER" "$KEYS/devid_installer.key"

# Keys usable by the signing tools without a prompt each time
for k in application installer; do
  security import "$KEYS/devid_$k.key" -k "$KC" -t priv -f openssl \
    -T /usr/bin/codesign -T /usr/bin/productbuild -T /usr/bin/productsign -T /usr/bin/pkgbuild || true
done
security import "$APP_CER" -k "$KC" || true
security import "$INST_CER" -k "$KC" || true

echo
security find-identity -v | grep "Developer ID" || { echo "no Developer ID identity found (intermediate certificate missing?)"; exit 1; }
echo "OK — now: scripts/make_installer_mac.sh FF (BT, BW)"
