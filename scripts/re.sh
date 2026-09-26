#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# OpenBRCM wrapper for the canonical RE tool.
#
# `re` stores the vendor blob as a relative path (`wlc_hybrid.o_shipped`) in
# re.db's meta table, so the disassembling subcommands (fn/card/switch/fields/
# seq/data/...) must run from the tooling workspace. This wrapper cd's there and
# execs `re`, so agents can invoke it from anywhere (including OpenBRCM).
#
# Read-only: it never touches hardware, the driver, or Git.
#
# Usage:
#   scripts/re.sh stats
#   scripts/re.sh fn 0x67efd
#   scripts/re.sh card sub_67efd
#   scripts/re.sh switch 0x67efd
#   scripts/re.sh packet --fn wlc_phy_switch_radio_acphy
# Override the tooling root with RE_TOOLING_ROOT if the layout moves.

set -eu
TOOLING=${RE_TOOLING_ROOT:-/media/kartashoff/Storage/opensource/iced/test}
RE_BIN="$TOOLING/binary_analyzer/target/release/re"

if [ ! -x "$RE_BIN" ]; then
	echo "re not found: $RE_BIN (set RE_TOOLING_ROOT)" >&2
	exit 1
fi

cd "$TOOLING"
exec "$RE_BIN" "$@"
