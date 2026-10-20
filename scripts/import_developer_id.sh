#!/usr/bin/env bash
# Imports the Developer ID certificates downloaded from developer.apple.com,
# each with the private key it was requested with, into the login keychain.
#
#   scripts/import_developer_id.sh [certificate.cer ...]
#   (default: ~/Downloads/developerID_application.cer and
#    ~/Downloads/developerID_installer.cer, whichever exist)
#
# The matching private key is looked for in ~/.groove-knobs-signing/*.key
# (the signing requests prepared for Groove Knobs), ~/Downloads/*.key and
# $DEVID_KEYS (extra .key files): a certificate is imported with the key whose
# public key is the same, or not at all.
#
# Then check: security find-identity -v   ("Developer ID" lines listed)
set -euo pipefail
KC="$HOME/Library/Keychains/login.keychain-db"
if [[ $# -gt 0 ]]; then
  CERTS=("$@")
else
  CERTS=()
  for f in "$HOME/Downloads/developerID_application.cer" "$HOME/Downloads/developerID_installer.cer"; do
    [[ -f "$f" ]] && CERTS+=("$f")
  done
fi
[[ ${#CERTS[@]} -gt 0 ]] || { echo "no Developer ID certificate found in ~/Downloads"; exit 1; }

KEYS=()
for k in "$HOME"/.groove-knobs-signing/*.key "$HOME"/Downloads/*.key ${DEVID_KEYS:-}; do
  [[ -f "$k" ]] && KEYS+=("$k")
done

pubOfCert() { openssl x509 -inform DER -in "$1" -noout -pubkey 2>/dev/null || openssl x509 -in "$1" -noout -pubkey; }
pubOfKey() { openssl rsa -in "$1" -pubout 2>/dev/null || openssl pkey -in "$1" -pubout 2>/dev/null; }

for cer in "${CERTS[@]}"; do
  subject="$(openssl x509 -inform DER -in "$cer" -noout -subject 2>/dev/null || openssl x509 -in "$cer" -noout -subject)"
  [[ "$subject" == *"Developer ID"* ]] || { echo "skipped (not a Developer ID certificate): $cer — $subject"; continue; }
  want="$(pubOfCert "$cer" | openssl md5)"
  key=""
  for k in ${KEYS[@]+"${KEYS[@]}"}; do
    [[ "$(pubOfKey "$k" | openssl md5)" == "$want" ]] && { key="$k"; break; }
  done
  [[ -n "$key" ]] || { echo "no private key matches $cer (made from another .certSigningRequest?)"; exit 1; }
  echo "importing $cer with $key"
  # the key usable by the signing tools without a prompt each time; `security`
  # only reads the traditional RSA format, not PKCS#8 ("BEGIN PRIVATE KEY"):
  # a converted copy (owner-only, deleted right after)
  tmp="$(umask 077; mktemp "${TMPDIR:-/tmp}/devid.XXXXXX")"
  openssl rsa -in "$key" -traditional -out "$tmp" 2>/dev/null || openssl rsa -in "$key" -out "$tmp"
  security import "$tmp" -k "$KC" -t priv -f openssl \
    -T /usr/bin/codesign -T /usr/bin/productbuild -T /usr/bin/productsign -T /usr/bin/pkgbuild || true
  rm -f "$tmp"
  security import "$cer" -k "$KC" || true
done

echo
security find-identity -v | grep "Developer ID" || { echo "no Developer ID identity found (Apple's Developer ID intermediate certificate missing?)"; exit 1; }
echo "OK — now: scripts/make_installer_mac.sh FF (BT, BW, PP, SIB)"
