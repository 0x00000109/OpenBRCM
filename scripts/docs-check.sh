#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Deterministic OpenBRCM documentation/consistency checks.
# Non-destructive; safe from CI, hooks and by hand. Exit 0 = pass.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

fail=0
ok()  { printf '  ok: %s\n' "$*"; }
bad() { printf '  FAIL: %s\n' "$*" >&2; fail=1; }

echo "== docs-check =="

# 1. AGENTS.md must define the status vocabulary.
if [ ! -f AGENTS.md ]; then
	bad "AGENTS.md missing"
else
	for w in "ANALYSIS ONLY" "IMPLEMENTED" "STATIC TESTED" "RUNTIME TESTED" \
		 "HARDWARE PROVEN" "FAILED" "SUPERSEDED" "UNKNOWN"; do
		grep -qF "$w" AGENTS.md || bad "AGENTS.md missing status word: $w"
	done
	[ "$fail" -eq 0 ] && ok "AGENTS.md status vocabulary present"
fi

# 2. M3.4D2A status consistency (BCM4352 runtime pass recorded, 2026).
#    The runtime pass must be recorded; and no document may claim that D2A
#    proved initvals/PHY/radio/channel/DMA (those remain unproven).
if grep -rniE 'M3\.4D2A.*HARDWARE (RUNTIME )?PROVEN' docs/ AGENTS.md 2>/dev/null \
		| grep -q .; then
	ok "M3.4D2A hardware status recorded"
else
	bad "no document records M3.4D2A hardware status"
fi
if grep -rniE 'M3\.4D2A.*(initvals|PHY|radio|channel|DMA).*PROVEN' docs/ AGENTS.md 2>/dev/null \
		| grep -viE 'not|never|unproven|before|skip|stops' | grep -q .; then
	bad "a document claims D2A proved initvals/PHY/radio/channel/DMA"
else
	ok "D2A scope limited to ucode upload + PSM start"
fi

# 3. The state/handoff document must exist and stay structured.
if [ -f docs/agent-state.md ]; then
	grep -qi 'current milestone' docs/agent-state.md \
		|| bad "docs/agent-state.md missing 'Current milestone'"
	grep -qi 'STOP boundary' docs/agent-state.md \
		|| bad "docs/agent-state.md missing 'STOP boundary'"
	[ "$fail" -eq 0 ] && ok "docs/agent-state.md structured"
else
	bad "docs/agent-state.md missing"
fi

# 4. Canonical source constants (the recovery-critical invariants).
if grep -q 'DMA_BIT_MASK(64)' src/ob_dma.c; then
	bad "src/ob_dma.c uses DMA_BIT_MASK(64) (must be 32-bit)"
elif grep -q 'DMA_BIT_MASK(32)' src/ob_dma.c; then
	ok "DMA window is 32-bit"
else
	bad "src/ob_dma.c missing DMA_BIT_MASK(32)"
fi

if grep -qE '^#define[[:space:]]+OB_DMA_RING_DESC_COUNT_RX[[:space:]]+256' src/ob_dma.h; then
	ok "RX ring = 256"
else
	bad "src/ob_dma.h RX ring is not 256"
fi

if grep -qE '^#define[[:space:]]+OB_D11_RX_CONTROL_INIT[[:space:]]+0x0000084du' src/ob_rx.h; then
	ok "RX CONTROL = 0x0000084d"
else
	bad "src/ob_rx.h RX CONTROL is not 0x0000084d"
fi

if grep -qE '^#define[[:space:]]+OB_FW_UCODE42_SIZE[[:space:]]+43400u' src/ob_fw.h; then
	ok "ucode size = 43400"
else
	bad "src/ob_fw.h ucode size is not 43400"
fi

for m in OB_FW_UCODE42_NAME OB_FW_UCODE42_NAME_LEGACY \
	 OB_FW_AC1INITVALS42_NAME OB_FW_AC1BSINITVALS42_NAME; do
	grep -q "$m" src/ob_main.c || bad "src/ob_main.c MODULE_FIRMWARE missing $m"
done
for n in bcm4352-d11ucode42.bin bcm43xx-ucode.fw \
	 bcm4352-d11ac1initvals42.bin bcm4352-d11ac1bsinitvals42.bin; do
	grep -q "$n" src/ob_fw.h || bad "src/ob_fw.h missing firmware name $n"
done

if [ -f src/ob_ucode.h ]; then
	grep -qE 'OB_UCODE_OBJADDR_AUTO_INC[[:space:]]+0x03000000u' src/ob_ucode.h \
		&& ok "ucode OBJADDR auto-inc = 0x03000000" \
		|| bad "src/ob_ucode.h OBJADDR auto-inc mismatch"
	grep -qE 'OB_UCODE_POLL_TIMEOUT[[:space:]]+0x000f4249u' src/ob_ucode.h \
		&& ok "ucode poll timeout = 0x000f4249" \
		|| bad "src/ob_ucode.h poll timeout mismatch"
	if [ -f tests/host/ob_ucode_test.c ]; then
		grep -q '0x04000404L' tests/host/ob_ucode_test.c \
			|| bad "tests/host/ob_ucode_test.c missing 0x04000404 assert"
		grep -q '0x04020402L' tests/host/ob_ucode_test.c \
			|| bad "tests/host/ob_ucode_test.c missing 0x04020402 assert"
	fi
fi

# 4b. M3.4D3 analysis consistency (band-switch initvals / PHY boundary).
#     The analysis must be present and must stay ANALYSIS ONLY.
if [ -f docs/m34d3_bsinitvals.md ] && \
   [ -f docs/m34d3/bsinitvals_classification.json ] && \
   [ -f scripts/analyze_bsinitvals.py ]; then
	grep -qE 'ANALYSIS ONLY' docs/m34d3_bsinitvals.md \
		&& ok "M3.4D3 analysis present" \
		|| bad "docs/m34d3_bsinitvals.md is not marked ANALYSIS ONLY"
else
	bad "M3.4D3 analysis artifacts missing"
fi
if grep -rniE 'M3\.4D3.*HARDWARE (RUNTIME )?PROVEN' docs/ 2>/dev/null \
		| grep -viE 'not|never|before|remain|unproven' | grep -q .; then
	bad "a document claims M3.4D3 is hardware proven"
else
	ok "M3.4D3 not claimed hardware proven"
fi

# 4c. D3A0 entry state MUST be the full D2B common-initvals exit (no D2A-only
#     bypass). The shared applier must carry the exact 610/113/497 shape, and
#     the D3A0 test must call it, and dma_test_only must be marked as applying
#     the common table.
if grep -q 'ob_initvals_run_d2b' src/ob_d3a0.c; then
	ok "D3A0 runs the shared D2B common-initvals prefix"
else
	bad "src/ob_d3a0.c does not run ob_initvals_run_d2b (D2A-only bypass)"
fi
if awk '/static inline bool ob_isolated_mode_applies_initvals/,/^}/' \
		src/ob_ucode.h | grep -q 'OB_ISOLATED_DMA_TEST'; then
	ok "dma_test_only marked as applying common initvals"
else
	bad "ob_isolated_mode_applies_initvals must include OB_ISOLATED_DMA_TEST"
fi
grep -qE '^#define[[:space:]]+OB_INITVALS_RECORDS[[:space:]]+610u' src/ob_initvals.h \
	&& ok "common initvals = 610 records" \
	|| bad "src/ob_initvals.h common-initvals record count is not 610"
grep -qE '^#define[[:space:]]+OB_INITVALS_W16[[:space:]]+113u' src/ob_initvals.h \
	&& ok "common initvals w16 = 113" \
	|| bad "src/ob_initvals.h common-initvals w16 count is not 113"
grep -qE '^#define[[:space:]]+OB_INITVALS_W32[[:space:]]+497u' src/ob_initvals.h \
	&& ok "common initvals w32 = 497" \
	|| bad "src/ob_initvals.h common-initvals w32 count is not 497"
if grep -rniE 'M3\.4D3A0.*HARDWARE (RUNTIME )?PROVEN' docs/ 2>/dev/null \
		| grep -viE 'not|never|before|remain|unproven' | grep -q .; then
	bad "a document claims M3.4D3A0 is hardware proven"
else
	ok "M3.4D3A0 not claimed hardware proven"
fi

# 5. No proprietary firmware/blob may be tracked.
if git ls-files | grep -qE '\.(bin|fw)$|wlc_hybrid'; then
	bad "proprietary firmware/blob appears tracked in Git"
else
	ok "no proprietary firmware tracked"
fi

if [ "$fail" -eq 0 ]; then
	echo "== docs-check PASS =="
else
	echo "== docs-check FAIL =="
fi
exit "$fail"
