#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Static checks used by CI and contributors.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

echo "== host math tests =="
make hosttest

if command -v sparse >/dev/null 2>&1 && [ -n "${KDIR:-}" ]; then
	echo "== sparse =="
	make KDIR="$KDIR" C=2 modules || true
else
	echo "== sparse skipped (install sparse and set KDIR) =="
fi

if [ -x /tmp/checkpatch.pl ]; then
	echo "== checkpatch =="
	for f in src/*.c src/*.h; do
		perl /tmp/checkpatch.pl --no-tree --terse -f "$f" || true
	done
fi
