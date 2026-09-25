#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# OpenBRCM tier-2 RE helper: Ghidra headless for facts the canonical Rust
# `re`/`re.db` cannot represent (see docs/re-tooling.md §2.4).
#
# READ-ONLY analysis. It never touches hardware, never loads/unloads modules,
# never executes code and never rebuilds the driver. It only imports the vendor
# blob into a scratch Ghidra project and runs a GhidraScript.
#
# Usage:
#   scripts/ghidra_headless.sh <Script.java> [script args...]
#   scripts/ghidra_headless.sh --reimport <Script.java> [args...]
#   scripts/ghidra_headless.sh --shell            # list available scripts
#
# Env:
#   GHIDRA_HOME   Ghidra install root (default below)
#   GHIDRA_PROJ   scratch project dir   (default /tmp/openbrcm_ghidra)
#   RE_BLOB       vendor blob           (default canonical blob)
#
# Exit 0 = success.

set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

GHIDRA_HOME=${GHIDRA_HOME:-/home/kartashoff/projects/ghidra}
GHIDRA_PROJ=${GHIDRA_PROJ:-/tmp/openbrcm_ghidra}
RE_BLOB=${RE_BLOB:-/media/kartashoff/Storage/opensource/iced/test/wlc_hybrid.o_shipped}
PROJ_NAME=OpenBRCM
SCRIPT_DIR="$ROOT/scripts/ghidra"

HEADLESS="$GHIDRA_HOME/support/analyzeHeadless"
if [ ! -x "$HEADLESS" ]; then
	echo "ghidra_headless: analyzeHeadless not found at $HEADLESS" >&2
	echo "  set GHIDRA_HOME to your Ghidra install root" >&2
	exit 1
fi
if [ ! -f "$RE_BLOB" ]; then
	echo "ghidra_headless: vendor blob not found: $RE_BLOB" >&2
	exit 1
fi

reimport=0
if [ "${1:-}" = "--reimport" ]; then reimport=1; shift; fi
if [ "${1:-}" = "--shell" ] || [ $# -eq 0 ]; then
	echo "available Ghidra scripts in $SCRIPT_DIR:"
	ls "$SCRIPT_DIR"/*.java 2>/dev/null | sed 's#.*/##; s#\.java$##'
	echo
	echo "usage: scripts/ghidra_headless.sh <Script.java> [args...]"
	echo "note: the vendor blob is not shipped in Git; the project imports the"
	echo "      canonical blob at \$RE_BLOB (read-only)."
	exit 0
fi

SCRIPT=$1; shift || true
if [ ! -f "$SCRIPT_DIR/$SCRIPT" ] && [ ! -f "$SCRIPT" ]; then
	echo "ghidra_headless: script not found: $SCRIPT (looked in $SCRIPT_DIR)" >&2
	exit 1
fi

mkdir -p "$GHIDRA_PROJ"
if [ ! -d "$GHIDRA_PROJ/$PROJ_NAME" ] || [ "$reimport" -eq 1 ]; then
	echo "ghidra_headless: importing + analyzing $RE_BLOB (first run may take ~1 min)"
	"$HEADLESS" "$GHIDRA_PROJ" "$PROJ_NAME" -import "$RE_BLOB" \
		-processor x86:LE:64:default -analysisTimeoutPerFile 1800 \
		-scriptPath "$SCRIPT_DIR" >/dev/null
fi

exec "$HEADLESS" "$GHIDRA_PROJ" "$PROJ_NAME" -process "$(basename "$RE_BLOB")" \
	-noanalysis -scriptPath "$SCRIPT_DIR" -postScript "$SCRIPT" "$@"
