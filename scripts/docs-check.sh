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
# The band-switch/PHY-boundary analysis M3.4D3 (not the D3A0 subset) must not be
# claimed hardware proven. `M3.4D3A0` is intentionally excluded from this match.
if grep -rniE 'M3\.4D3([^0-9A-Za-z]|$).*HARDWARE (RUNTIME )?PROVEN' docs/ 2>/dev/null \
		| grep -viE 'not|never|before|remain|unproven' | grep -q .; then
	bad "a document claims M3.4D3 (band-switch/PHY boundary) is hardware proven"
else
	ok "M3.4D3 (band-switch/PHY boundary) not claimed hardware proven"
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
# D3A0 is HARDWARE RUNTIME PROVEN on BCM4352, but only for the isolated DMA
# lifecycle; the analysis/PR must not overclaim the vendor tail/PHY scope.
if [ -f docs/m34d3a0_dma_test.md ] && \
   grep -q 'HARDWARE RUNTIME PROVEN' docs/m34d3a0_dma_test.md; then
	ok "M3.4D3A0 hardware runtime status recorded"
else
	bad "docs/m34d3a0_dma_test.md must record M3.4D3A0 hardware runtime proof"
fi
if grep -rniE 'M3\.4D3A0.*(vendor tail|post-common|sub_67efd|band init|bsinitvals|PHY|radio|channel|calibration).*PROVEN' docs/ 2>/dev/null \
		| grep -viE 'not|never|unproven|before|skip|stops|remain|proves? .*only' | grep -q .; then
	bad "a document overclaims M3.4D3A0 scope"
else
	ok "D3A0 scope limited to the isolated DMA lifecycle"
fi

# 4d. D3A0 scope classification and the conservative quiesce model.
grep -q 'ISOLATED DMA LIFECYCLE TEST' docs/d3a0_dma_test_design.md \
	&& ok "D3A0 classified as ISOLATED DMA LIFECYCLE TEST" \
	|| bad "docs/d3a0_dma_test_design.md missing D3A0 scope classification"
# containment must never be recorded as a free permit
if grep -q 'core containment verified; safe to free' src/ob_d3a0.c; then
	bad "src/ob_d3a0.c still treats core containment as free authorization"
else
	ok "core containment is not a free authorization"
fi
for m in engines_stopped core_contained free_allowed; do
	grep -q "$m" src/ob_d3a0.h \
		|| bad "src/ob_d3a0.h lifecycle missing $m"
done
grep -q 'free_allowed' src/ob_d3a0.h \
	&& ok "lifecycle separates stopped/contained/free/fatal" \
	|| bad "lifecycle flags not separated"

# 4e. D3A1 is HARDWARE RUNTIME PROVEN on BCM4352, but only for the isolated
# vendor post-common / pre-PHY D11 tail. Band init / bsinitvals / PHY stay
# unproven, and D3B must not be overclaimed either.
if [ -f docs/m34d3a1_vendor_tail_test.md ] && \
   grep -q 'IMPLEMENTED' docs/m34d3a1_vendor_tail_test.md && \
   grep -q 'STATIC TESTED' docs/m34d3a1_vendor_tail_test.md && \
   grep -q 'HARDWARE RUNTIME PROVEN' docs/m34d3a1_vendor_tail_test.md; then
	ok "D3A1 implementation recorded (IMPLEMENTED / STATIC TESTED / HARDWARE RUNTIME PROVEN)"
else
	bad "docs/m34d3a1_vendor_tail_test.md must record D3A1 HARDWARE RUNTIME PROVEN"
fi
if [ -f docs/m34d3a1_vendor_tail.md ] && \
   grep -q 'D3A1 IMPLEMENTATION GO: YES' docs/m34d3a1_vendor_tail.md; then
	ok "D3A1 analysis record present (GO: YES)"
else
	bad "docs/m34d3a1_vendor_tail.md must record D3A1 IMPLEMENTATION GO: YES"
fi
if grep -rniE 'M3\.4D3A1.*(band init|bsinitvals|wlc_phy_init|AC PHY|radio|calibration|channel).*PROVEN' docs/ 2>/dev/null \
		| grep -viE 'not|never|unproven|before|does not|stops' | grep -q .; then
	bad "a document overclaims M3.4D3A1 scope"
else
	ok "D3A1 scope limited to the post-common/pre-PHY tail"
fi
if grep -rniE 'M3\.4D3B.*HARDWARE (RUNTIME )?PROVEN' docs/ 2>/dev/null \
		| grep -viE 'not|never|unproven' | grep -q .; then
	bad "a document overclaims M3.4D3B hardware status"
else
	ok "D3B not claimed hardware proven"
fi
if [ -f docs/m34d3b_band_init.md ] && \
   grep -q 'ANALYSIS ONLY' docs/m34d3b_band_init.md && \
   grep -q 'D3B IMPLEMENTATION GO:.*NO' docs/m34d3b_band_init.md && \
   grep -q 'wlc_bmac_write_mhf' docs/m34d3b_band_init.md; then
	ok "D3B analysis record present (ANALYSIS ONLY / blocked GO)"
else
	bad "docs/m34d3b_band_init.md must record the D3B analysis (ANALYSIS ONLY / blocked GO / wlc_bmac_write_mhf)"
fi

# 4f. D3A1 isolated path must prepare ChipCommon + validated MAC itself.
if sed -n '/int ob_si_prepare_board_data_for_d3a1/,/^}/p' src/ob_si.c \
		| grep -q 'hw->cc = hw->bus->drv_cc.core' && \
   sed -n '/int ob_si_prepare_board_data_for_d3a1/,/^}/p' src/ob_si.c \
		| grep -q 'ob_si_read_mac(hw, hw->mac)'; then
	ok "D3A1 board-data prep sets hw->cc and reads the external-SPROM MAC"
else
	bad "ob_si_prepare_board_data_for_d3a1 missing hw->cc / ob_si_read_mac"
fi
if sed -n '/int ob_si_prepare_board_data_for_d3a1/,/^}/p' src/ob_si.c \
		| grep -qE 'ob_si_powerup|ob_si_d11_diag|ob_si_otp_diag|ob_si_sprom_diag|bcma_write|ob_si_cc_write'; then
	bad "D3A1 board-data prep imports normal-probe side effects"
else
	ok "D3A1 board-data prep is read-only (no power-up/diag/register writes)"
fi
d3a1_prep=$(grep -n 'ob_si_prepare_board_data_for_d3a1(hw)' src/ob_core.c | head -1 | cut -d: -f1)
d3a1_test=$(grep -n 'ret = ob_d3a1_test(hw)' src/ob_core.c | head -1 | cut -d: -f1)
if [ -n "$d3a1_prep" ] && [ -n "$d3a1_test" ] && [ "$d3a1_prep" -lt "$d3a1_test" ]; then
	ok "D3A1 dispatch prepares board data before ob_d3a1_test"
else
	bad "D3A1 dispatch must call ob_si_prepare_board_data_for_d3a1 before ob_d3a1_test"
fi
if grep -q 'if (!hw->cc)' src/ob_d3a1.c && \
   grep -q 'if (!hw->mac_valid)' src/ob_d3a1.c; then
	ok "D3A1 refuses without hw->cc and without a validated MAC"
else
	bad "D3A1 must refuse without hw->cc / validated MAC"
fi
if grep -q 'best-effort' src/ob_d3a1.c src/ob_d3a1.h; then
	bad "D3A1 contains best-effort arithmetic"
else
	ok "D3A1 has no best-effort arithmetic"
fi
# 4f. The 0x530 predicate is whole-word zero and 0x540 is bit0 clear; no 0x8000
# mask and no 0x0007 special case may creep in.
if grep -q 'return v == 0u;' src/ob_d3a1.h && \
   grep -q 'return (v & 0x1u) == 0u;' src/ob_d3a1.h && \
   ! grep -qE 'v & 0x8000|== 0x0007|0x8000u? *\)' src/ob_d3a1.c src/ob_d3a1.h; then
	ok "D3A1 poll predicates exact (0x530 word-zero, 0x540 bit0-clear)"
else
	bad "D3A1 poll predicate drift (must be 0x530 word-zero / 0x540 bit0-clear)"
fi
if grep -q 'fifo_poll_expired' src/ob_d3a1.c && \
   grep -q 'vendor continues, non-fatal' src/ob_d3a1.c; then
	ok "D3A1 poll expiry is vendor-non-fatal"
else
	bad "D3A1 poll expiry must be vendor-non-fatal (logged, no abort)"
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
