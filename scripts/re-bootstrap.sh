#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# OpenBRCM RE-tooling bootstrap / verifier.
#
# READ-ONLY with respect to hardware and Git. It does NOT:
#   - touch hardware, load/unload modules, or run MMIO;
#   - rebuild the driver;
#   - mutate Git state;
#   - alter or rebuild re.db.
#
# It verifies that a fresh agent session can reach the canonical deterministic
# RE tooling and that the persistent OpenCode integration is present.
#
# Exit 0 = PASS, 1 = FAIL. See docs/re-tooling.md.

set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

RE_BIN=${RE_BIN:-/media/kartashoff/Storage/opensource/iced/test/binary_analyzer/target/release/re}
RE_DB=${RE_DB:-/media/kartashoff/Storage/opensource/iced/test/re.db}
RE_BLOB=${RE_BLOB:-/media/kartashoff/Storage/opensource/iced/test/wlc_hybrid.o_shipped}
TOOLING_ROOT=${TOOLING_ROOT:-/media/kartashoff/Storage/opensource/iced/test}
EXPECTED_SHA=${EXPECTED_SHA:-352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743}

fail=0
ok()  { printf '  PASS  %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*" >&2; fail=1; }

echo "== OpenBRCM RE bootstrap =="
echo "  repo:    $ROOT"
echo "  tooling: $TOOLING_ROOT"
echo "  re:      $RE_BIN"
echo "  db:      $RE_DB"

# --- 1. canonical re binary ------------------------------------------------
re_ok=0
if [ -x "$RE_BIN" ]; then
	ok "re binary exists and is executable"
	re_ok=1
else
	bad "re binary missing or not executable: $RE_BIN"
fi

# --- 2. canonical re.db ----------------------------------------------------
db_ok=0
if [ -f "$RE_DB" ] && [ -s "$RE_DB" ]; then
	ok "re.db exists and is non-empty"
	db_ok=1
else
	bad "re.db missing/empty: $RE_DB"
fi

# --- 3. re can open/query the db, counts plausible -------------------------
funcs=0
if [ "$re_ok" -eq 1 ] && [ "$db_ok" -eq 1 ]; then
	stats=$("$RE_BIN" stats --db "$RE_DB" 2>&1) || true
	case $stats in
	*functions:*) ok "re opened and queried re.db (re stats)";;
	*) bad "re could not query re.db: $stats";;
	esac
	funcs=$(printf '%s\n' "$stats" | awk '/^functions:/{print $2; exit}')
	case $funcs in
	''|*[!0-9]*) funcs=0;;
	esac
	if [ "$funcs" -ge 1000 ]; then
		ok "functions count plausible ($funcs >= 1000)"
	else
		bad "functions count implausible ($funcs); db corrupt or stale"
	fi
	case $stats in
	*'reloc-site mismatches: 0'*) ok "reloc-site mismatches = 0";;
	*) bad "reloc-site mismatches non-zero (db integrity)";;
	esac
else
	bad "cannot query re.db without re binary"
fi

# --- 4. indexed blob sha matches the current blob --------------------------
if [ -f "$RE_BLOB" ]; then
	actual=$(sha256sum "$RE_BLOB" | awk '{print $1}')
	if [ "$actual" = "$EXPECTED_SHA" ]; then
		ok "vendor blob sha256 matches expected"
	else
		bad "vendor blob sha256 mismatch: $actual != $EXPECTED_SHA"
	fi
else
	bad "vendor blob missing: $RE_BLOB"
fi

if [ "$db_ok" -eq 1 ] && command -v python3 >/dev/null 2>&1; then
	indexed=$(python3 - "$RE_DB" <<'PY' 2>/dev/null
import sqlite3, sys
try:
    c = sqlite3.connect("file:%s?mode=ro" % sys.argv[1], uri=True)
    row = c.execute("select value from meta where key='sha256'").fetchone()
    print(row[0] if row else "")
except Exception:
    print("")
PY
)
	if [ "$indexed" = "$EXPECTED_SHA" ]; then
		ok "re.db indexes the current vendor blob (meta.sha256)"
	else
		bad "re.db is STALE: indexed sha '$indexed' != expected '$EXPECTED_SHA'"
		printf '%s\n' \
			"        rebuild:  cd $TOOLING_ROOT && \\" \
			"                 ./binary_analyzer/target/release/re db build --elf wlc_hybrid.o_shipped --db re.db" >&2
	fi
fi

# --- 5. mandatory verification scripts -------------------------------------
missing_gates=
for s in verify_edges.py verify_decode.py verify_tables.py verify_crossarch.py \
	 oracle_exec.py coverage.py; do
	if [ -f "$TOOLING_ROOT/scripts/$s" ]; then :; else missing_gates="$missing_gates $s"; fi
done
if [ -z "$missing_gates" ]; then
	ok "mandatory verification gate scripts present"
else
	bad "missing verification gate scripts:$missing_gates"
fi

# --- 6. Git hooks active ---------------------------------------------------
hp=$(git config --local --get core.hooksPath 2>/dev/null || true)
if [ "$hp" = ".githooks" ] && [ -x .githooks/pre-commit ]; then
	ok "Git pre-commit hook active (core.hooksPath=.githooks, executable)"
else
	bad "Git pre-commit hook inactive (core.hooksPath='$hp')"
	printf '%s\n' "        activate: git config --local core.hooksPath .githooks" >&2
fi

# --- 7. OpenCode repo-local RE integration ---------------------------------
oc_cfg=".opencode/opencode.json"
if [ -f "$oc_cfg" ] && command -v python3 >/dev/null 2>&1 \
   && python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$oc_cfg" 2>/dev/null; then
	ok "OpenCode project config present and valid ($oc_cfg)"
	if grep -q 're-hooks.ts' "$oc_cfg"; then
		ok "project config reuses canonical re-hooks.ts"
	else
		bad "project config does not reference the canonical re-hooks.ts"
	fi
else
	bad "OpenCode project config missing/invalid: $oc_cfg"
fi
for f in .opencode/plugins/openbrcm-re.ts .opencode/skills/openbrcm-re/SKILL.md; do
	if [ -f "$f" ]; then ok "OpenCode integration present: $f"; else bad "OpenCode integration missing: $f"; fi
done

# --- 8. state + canonical docs --------------------------------------------
for f in AGENTS.md docs/agent-state.md docs/re-tooling.md scripts/re.sh; do
	if [ -f "$f" ]; then ok "present: $f"; else bad "missing: $f"; fi
done
if [ -x scripts/re.sh ]; then ok "re wrapper executable: scripts/re.sh"; else bad "scripts/re.sh not executable"; fi

if [ "$fail" -eq 0 ]; then
	echo "== RE bootstrap PASS =="
else
	echo "== RE bootstrap FAIL ==" >&2
fi
exit "$fail"
