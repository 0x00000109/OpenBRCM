#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Sign the built openbrcm.ko with the existing MOK key/certificate.
#
# The private key is used read-only and is never copied, converted, printed or
# modified. If the key format is rejected by sign-file, this script STOPS and
# reports the exact error; it does not attempt conversion.
#
# Usage:
#   ./scripts/sign.sh
#   MOK_DIR=/path/to/MOC ./scripts/sign.sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KO="$ROOT/openbrcm.ko"

MOK_DIR="${MOK_DIR:-/media/kartashoff/Storage/opensource/iced/MOC}"
PRIV="$MOK_DIR/MOK.priv"
CERT="$MOK_DIR/MOK.der"

KVER=$(uname -r)
SIGN_FILE="/usr/src/linux-headers-$KVER/scripts/sign-file"

[ -f "$KO" ]        || { echo "ERROR: $KO not found; run 'make' first" >&2; exit 1; }
[ -r "$PRIV" ]      || { echo "ERROR: private key not readable: $PRIV" >&2; exit 1; }
[ -r "$CERT" ]      || { echo "ERROR: certificate not readable: $CERT" >&2; exit 1; }
[ -x "$SIGN_FILE" ] || { echo "ERROR: sign-file not found: $SIGN_FILE" >&2; exit 1; }

echo "Signing $(basename "$KO")"
echo "  certificate : $CERT"
echo "  hash        : sha256"
echo "  sign-file   : $SIGN_FILE"

if ! "$SIGN_FILE" sha256 "$PRIV" "$CERT" "$KO"; then
	echo "ERROR: sign-file rejected the key/certificate." >&2
	echo "       No conversion was attempted and nothing under $MOK_DIR was modified." >&2
	exit 1
fi

echo "Signature embedded. modinfo:"
modinfo "$KO" | grep -E '^(sig_id|signer|sig_key|sig_hashalgo)' || true
