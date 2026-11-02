#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""D3B boundary witness (`d3b_boundary_witness=1`) — offline acceptance contract.

The D3A1 exit is the boundary where the vendor's `sub_6656c` reads D11+0x3e0 and
where the isolated D3B slice deliberately omits that read. Two project records
disagree about that boundary (a stalled read on boot 06a8fdeb vs `OK` in the
post-mortem timeline), and neither captured the state below the band. The opt-in
witness records it, read-only, in this order and nothing else:

  B1..B5  ChipCommon/PMU:  0x01e0 clk_ctl_st, 0x0600 PMU_CTL, 0x0608 PMU_STAT,
                           0x0618 PMU_MINRES_MSK, 0x061c PMU_MAXRES_MSK
  B6/B7   D11 after switch_macfreq: 0x01e0 clk_ctl_st, 0x01e8 d11_phypllctl
  B8/B9   the island: 0x03d8 (16-bit), then 0x03da (16-bit) only if B8 completed

Witness v2 (`d3b_witness_v2=1`, refused without the witness) extends the
sequence without changing the v1 order:

  P1..P4  ChipCommon/PMU domain: 0x0604 PMU_CAP, 0x060c PMU_OTPSTAT,
          0x0660/0x0664 PMU_PLLCTL_ADDR/DATA (the rest of the si_pmu_*
          inventory in ob_si.h) — a stall here stops before the island
  C1/C2   controls OUTSIDE the island band: 0x03fe/0x03fc (16-bit), bounding
          the failing region from above
  B10     the vendor's own register 0x03e0 (16-bit), only after B9 completed

v2 gates window_alive on B8, B9 and B10 all being class LIVE, and adds the
verdicts pre_island_stall_window_dead, island_live_vendor_read_dead and
island_anomalous_read.

These tests cannot compile src/ob_d3b.c (kernel headers), so they check the
properties the compiled host test cannot observe, on the real source text:

  T01  the modifier is opt-in, D3B-only, combinable with the FIFO-530
       continuation under an armed-and-logged rule whose state the summary
       records (M1a), and never selectable as an isolated mode of its own
  T02  stage order is exactly CC/PMU -> D11 clocks -> island, and the island is
       reached only after both baselines were LIVE
  T03  read-only: no MMIO write, no window switch, no delay, no PHY/radio path
  T04  the island answer is attributable: a stall latches device loss and one
       read over 1 s stops the witness instead of issuing more reads
  T05  fail-stop wiring: the caller gates on the field, aborts before the
       checkpoint/MHF/bsinitvals writes, and the witness runs after the prefix
  T06  the D11 clock bit masks are the same upstream values L2l already uses
  T07  the harness (scripts/runtime-test.sh) can only arm it for
       bsinitvals_test_only, passes d3b_boundary_witness=1 (optionally with
       d11_fifo530_continue=1), and accepts the run only on the witness marker
       contract, which requires the FIFO-530 state when the continuation was
       armed

Every check prints a machine-readable RESULT line.
"""

from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CORE_C = ROOT / "src" / "ob_core.c"
CORE_H = ROOT / "src" / "ob_core.h"
D3B_C = ROOT / "src" / "ob_d3b.c"
SI_C = ROOT / "src" / "ob_si.c"
SI_H = ROOT / "src" / "ob_si.h"
HARNESS = ROOT / "scripts" / "runtime-test.sh"

failures = 0
checks = 0


def strip_c(text: str) -> str:
    """Remove comments and string/char literals, preserving line structure."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c in ('"', "'"):
            quote = c
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == quote:
                    j += 1
                    break
                if text[j] == "\n":
                    break
                j += 1
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_comments(text: str) -> str:
    """Remove comments only, keeping string literals (stage names, verdicts)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def body_of(code: str, signature: str) -> str:
    """Return the brace-balanced body of `signature`."""
    start = code.find(signature)
    if start < 0:
        raise SystemExit("signature not found: %r" % signature)
    brace = code.find("{", start)
    if brace < 0:
        raise SystemExit("no body for %r" % signature)
    depth = 0
    for i in range(brace, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return code[brace + 1:i]
    raise SystemExit("unbalanced body for %r" % signature)


def depth_at(text: str, pos: int) -> int:
    return text[:pos].count("{") - text[:pos].count("}")


def order_ok(text: str, tokens) -> bool:
    """Every token present and appearing strictly in the given order."""
    pos = -1
    for token in tokens:
        i = text.find(token, pos + 1)
        if i < 0:
            return False
        pos = i
    return True


def check(test_id, label, ok, detail=""):
    global failures, checks
    checks += 1
    if not ok:
        failures += 1
    print("RESULT %s %s test=%s%s"
          % (test_id, "PASS" if ok else "FAIL", label,
             "" if ok else " detail=" +
             str(detail).replace("\n", " ")[:400]))
    return ok


# Region anchors that survive comment stripping: the witness section's first
# define and the function that follows the witness section.
WITNESS_BEGIN = "#define OB_D3BW_LIVE_US"
WITNESS_END = "int ob_d3b_test(struct ob_hw *hw)"

WRITE_TOKENS = ("ob_d11_write", "ob_d11_write16", "ob_d11_write32",
                "ob_si_cc_write", "ob_ucode_write", "ob_initvals_apply",
                "ob_d3b_write_iv", "ob_ucode_write_shm16_checked")
SWITCH_TOKENS = ("switch_core", "BAR0_WIN", "bcma_pci_bridge", "ob_radio_",
                 "ob_phy_", "ob_si_powerup", "request_irq")
DELAY_TOKENS = ("udelay", "mdelay", "usleep_range", "msleep", "ssleep")

CC_STAGES = (
    ('ob_d3bw_cc_read32(hw, "B1", OB_CC_CLKCTLST', "B1/0x01e0"),
    ('ob_d3bw_cc_read32(hw, "B2", OB_CC_PMU_CTL', "B2/0x0600"),
    ('ob_d3bw_cc_read32(hw, "B3", OB_CC_PMU_STAT', "B3/0x0608"),
    ('ob_d3bw_cc_read32(hw, "B4", OB_CC_PMU_MINRES_MSK', "B4/0x0618"),
    ('ob_d3bw_cc_read32(hw, "B5", OB_CC_PMU_MAXRES_MSK', "B5/0x061c"),
)
D11_CLOCK_STAGES = (
    ('ob_d3bw_d11_read32(hw, "B6", 0x01e0u', "B6/0x01e0 32-bit"),
    ('ob_d3bw_d11_read32(hw, "B7", 0x01e8u', "B7/0x01e8 32-bit"),
)
ISLAND_STAGES = (
    ('ob_d3bw_d11_read16(hw, "B8", 0x03d8u', "B8/0x03d8 16-bit"),
    ('ob_d3bw_d11_read16(hw, "B9", 0x03dau', "B9/0x03da 16-bit"),
)
PMU_DOMAIN_STAGES = (
    ('ob_d3bw_cc_read32(hw, "P1", OB_CC_PMU_CAP', "P1/0x0604"),
    ('ob_d3bw_cc_read32(hw, "P2", OB_CC_PMU_OTPSTAT', "P2/0x060c"),
    ('ob_d3bw_cc_read32(hw, "P3", OB_CC_PMU_PLLCTL_ADDR', "P3/0x0660"),
    ('ob_d3bw_cc_read32(hw, "P4", OB_CC_PMU_PLLCTL_DATA', "P4/0x0664"),
)
CONTROL_STAGES = (
    ('ob_d3bw_d11_read16(hw, "C1", 0x03feu', "C1/0x03fe 16-bit"),
    ('ob_d3bw_d11_read16(hw, "C2", 0x03fcu', "C2/0x03fc 16-bit"),
)
VENDOR_STAGE = ('ob_d3bw_d11_read16(hw, "B10", 0x03e0u', "B10/0x03e0 16-bit")
V2_VERDICTS = ("pre_island_stall_window_dead", "island_live_vendor_read_dead",
               "island_anomalous_read")
OB_SI_PMU_INVENTORY = {
    "OB_CC_PMU_CAP": 0x0604,
    "OB_CC_PMU_OTPSTAT": 0x060c,
    "OB_CC_PMU_PLLCTL_ADDR": 0x0660,
    "OB_CC_PMU_PLLCTL_DATA": 0x0664,
}

D3BW_MASKS = ("OB_D3BW_CCS_REQ_PHYPLL", "OB_D3BW_CCS_AVAIL_PHYPLL",
              "OB_D3BW_CCS_AVAIL_D11PLL", "OB_D3BW_CCS_AVAIL_HT")


def defines(text: str, prefix: str) -> dict:
    found = {}
    for name, value in re.findall(r"#define\s+(%s\w*)\s+(0x[0-9a-fA-F]+u?)" % prefix,
                                  text):
        found[name] = int(value.rstrip("uU"), 16)
    return found


def main():
    for path in (CORE_C, CORE_H, D3B_C, SI_C, SI_H, HARNESS):
        if not path.exists():
            print("FAIL: missing %s" % path)
            return 1

    core = CORE_C.read_text(encoding="utf-8")
    core_h = CORE_H.read_text(encoding="utf-8")
    raw_d3b = D3B_C.read_text(encoding="utf-8")
    si = SI_C.read_text(encoding="utf-8")
    si_h = SI_H.read_text(encoding="utf-8")
    harness = HARNESS.read_text(encoding="utf-8")

    code = strip_c(raw_d3b)
    nocomments = strip_comments(raw_d3b)

    # ---------------------------------------------------------------- T01
    check("T01", "param_declared_and_described",
          "static bool d3b_boundary_witness;" in core and
          "module_param(d3b_boundary_witness, bool, 0444);" in core and
          "requires bsinitvals_test_only=1" in core,
          "param declaration/description changed")
    check("T01", "requires_d3b_mode_before_hardware",
          "if (d3b_boundary_witness && !bsinitvals_test_only)" in core and
          core.index("if (d3b_boundary_witness && !bsinitvals_test_only)") <
          core.index("ob_d3b_test(hw)",
                     core.index("int ob_probe(struct bcma_device *core)")),
          "the witness is not refused outside bsinitvals_test_only")
    # M1a (2026-10-27): the witness/FIFO-530 exclusivity made the witness
    # unreachable on this bench (the 0x530 expiry is deterministic and the
    # default prefix refuses on expiry). The combination is now ALLOWED and
    # logged, and the witness records the continuation state it ran on
    # (fifo530_* fields in the summary). This replaces the refusal; it does
    # not weaken the other witness guarantees (T02..T07 below unchanged).
    check("T01", "combination_allowed_and_logged_before_hardware",
          "if (d3b_boundary_witness && d11_fifo530_continue)" not in core and
          "boundary witness armed with d11_fifo530_continue=1" in core and
          core.index("boundary witness armed with d11_fifo530_continue=1") <
          core.index("hw = devm_kzalloc("),
          "the witness/FIFO-530 combination is not accepted and logged "
          "before allocation")
    check("T01", "summary_records_the_fifo530_state",
          all(f in raw_d3b for f in ("fifo530_continue=%u",
                                     "fifo530_expiry_continued=%u",
                                     "fifo530_entries_continued=%u",
                                     "fifo530_first_idx=%d",
                                     "fifo530_last=%04x",
                                     "fifo530_reads=%u",
                                     "fifo530_invalid530=%u",
                                     "fifo530_invalid540=%u")) and
          raw_d3b.index("writes_issued=0 verdict=%s") <
          raw_d3b.index("fifo530_continue=%u"),
          "the witness summary does not record the FIFO-530 continuation "
          "state after verdict=")
    select = core[core.index("mode = ob_isolated_mode_select"):
                  core.index(");", core.index("mode = ob_isolated_mode_select"))]
    check("T01", "not_selected_as_an_isolated_mode",
          "d3b_boundary_witness" not in select,
          "d3b_boundary_witness leaked into the mode selector")
    check("T01", "field_is_published_only_for_d3b",
          "bool\t\t\td3b_boundary_witness;" in core_h and
          "hw->d3b_boundary_witness = d3b_boundary_witness;" in core and
          core.index("hw->d3b_boundary_witness = d3b_boundary_witness;") >
          core.index("if (mode == OB_ISOLATED_D3B_TEST) {"),
          "the hw field is set outside the D3B dispatch")
    check("T01", "v2_param_declared_requires_witness",
          "static bool d3b_witness_v2;" in core and
          "module_param(d3b_witness_v2, bool, 0444);" in core and
          "requires d3b_boundary_witness=1" in core and
          "if (d3b_witness_v2 && !d3b_boundary_witness)" in core and
          core.index("if (d3b_witness_v2 && !d3b_boundary_witness)") <
          core.index("hw = devm_kzalloc(",
                     core.index("int ob_probe(struct bcma_device *core)")),
          "witness v2 is not declared, or not refused without the witness "
          "before allocation")
    check("T01", "v2_armed_logged_before_hardware",
          "witness v2 armed (P1..P4 PMU-domain CC reads" in core and
          core.index("witness v2 armed (P1..P4 PMU-domain CC reads") <
          core.index("hw = devm_kzalloc(",
                     core.index("int ob_probe(struct bcma_device *core)")),
          "the v2 mode is not logged before allocation")
    check("T01", "v2_field_is_published_only_for_d3b",
          "bool\t\t\td3b_witness_v2;" in core_h and
          "hw->d3b_witness_v2 = d3b_witness_v2;" in core and
          core.index("hw->d3b_witness_v2 = d3b_witness_v2;") >
          core.index("if (mode == OB_ISOLATED_D3B_TEST) {"),
          "the v2 hw field is set outside the D3B dispatch")

    # ---------------------------------------------------------------- T02
    nc_begin = nocomments.index(WITNESS_BEGIN)
    region_nc = nocomments[nc_begin:nocomments.index(WITNESS_END, nc_begin)]
    c_begin = code.index(WITNESS_BEGIN)
    region = code[c_begin:code.index(WITNESS_END, c_begin)]
    stages = CC_STAGES + D11_CLOCK_STAGES + ISLAND_STAGES
    missing = [label for token, label in stages if token not in region_nc]
    check("T02", "all_nine_stage_reads_present",
          not missing, "missing stages: %s" % missing)
    check("T02", "stage_order_cc_then_d11_then_island",
          order_ok(region_nc, [t for t, _ in stages]),
          "the witness does not issue CC/PMU -> D11 clocks -> island in order")

    witness = body_of(region_nc, "static bool ob_d3b_run_boundary_witness")
    first_cc = witness.find('ob_d3bw_cc_read32(hw, "B1"')
    first_d11 = witness.find('ob_d3bw_d11_read32(hw, "B6"')
    first_island = witness.find('ob_d3bw_d11_read16(hw, "B8"')
    check("T02", "cc_baseline_precedes_d11_clocks",
          first_cc >= 0 and first_cc < first_d11 < first_island,
          "cc=%d d11=%d island=%d" % (first_cc, first_d11, first_island))
    check("T02", "cc_baseline_gates_the_d11_clocks",
          witness.find('verdict = "void_no_cc_baseline"') >= 0 and
          witness.find('verdict = "void_no_cc_baseline"') < first_d11,
          "D11 clock reads are issued without a LIVE CC baseline")
    check("T02", "d11_baseline_gates_the_island",
          witness.find('verdict = "void_no_d11_baseline"') >= 0 and
          witness.find('verdict = "void_no_d11_baseline"') < first_island,
          "the island is entered without a LIVE D11 clock baseline")
    check("T02", "island_upper_read_only_after_lower_completed",
          witness.find('ob_d3bw_d11_read16(hw, "B9"') >
          witness.find("if (b8 == OB_D3BW_STALL)") >= 0 and
          witness.count('ob_d3bw_d11_read16(hw, "B8"') == 1 and
          witness.count('ob_d3bw_d11_read16(hw, "B9"') == 1,
          "0x03da is not conditional on 0x03d8 completing")
    v2_stages = (CC_STAGES + PMU_DOMAIN_STAGES + D11_CLOCK_STAGES +
                 CONTROL_STAGES + ISLAND_STAGES + (VENDOR_STAGE,))
    missing_v2 = [label for token, label in v2_stages if token not in region_nc]
    check("T02", "v2_all_stage_reads_present",
          not missing_v2, "missing v2 stages: %s" % missing_v2)
    check("T02", "v2_stage_order_cc_pmu_d11_controls_island_vendor",
          order_ok(region_nc, [t for t, _ in v2_stages]),
          "the v2 stages are not CC/PMU -> PMU domain -> D11 clocks -> "
          "controls -> island -> vendor in order")
    check("T02", "v2_pmu_domain_reads_follow_the_cc_baseline",
          witness.find('verdict = "void_no_cc_baseline"') >= 0 and
          witness.find('verdict = "void_no_cc_baseline"') <
          witness.find('ob_d3bw_cc_read32(hw, "P1"'),
          "the PMU-domain reads are issued without a LIVE CC baseline")
    check("T02", "v2_controls_follow_the_d11_baseline",
          witness.find('verdict = "void_no_d11_baseline"') >= 0 and
          witness.find('verdict = "void_no_d11_baseline"') <
          witness.find('ob_d3bw_d11_read16(hw, "C1"'),
          "the controls are issued without a LIVE D11 clock baseline")
    check("T02", "v2_controls_precede_the_island",
          witness.find('ob_d3bw_d11_read16(hw, "C1"') <
          witness.find('ob_d3bw_d11_read16(hw, "B8"'),
          "the controls do not bound the island from above")
    check("T02", "v2_controls_bound_the_band_from_above",
          0x03fe > 0x03fb and 0x03fc > 0x03fb and 0x03d8 <= 0x03fb,
          "the control offsets are not above the island band")
    check("T02", "v2_vendor_read_only_after_the_island",
          witness.find('ob_d3bw_d11_read16(hw, "B10"') >
          witness.find('ob_d3bw_d11_read16(hw, "B9"'),
          "0x03e0 is not issued after the island")
    inv = defines(si_h, "OB_CC_PMU_")
    bad_inv = [name for name, value in OB_SI_PMU_INVENTORY.items()
               if inv.get(name) != value]
    check("T02", "v2_pmu_domain_reads_come_from_the_ob_si_inventory",
          not bad_inv,
          "offsets missing from/changed vs ob_si.h: %s" % bad_inv)

    # ---------------------------------------------------------------- T03
    writes = [t for t in WRITE_TOKENS if t in region]
    switches = [t for t in SWITCH_TOKENS if t in region]
    delays = [t for t in DELAY_TOKENS if t in region]
    check("T03", "witness_is_read_only",
          not writes, "write-capable tokens in the witness: %s" % writes)
    check("T03", "no_window_switch_or_lifecycle",
          not switches, "window/lifecycle tokens in the witness: %s" % switches)
    check("T03", "no_delays_or_polling",
          not delays, "delay tokens in the witness: %s" % delays)
    check("T03", "only_three_access_paths",
          region.count("ob_si_cc_read(") == 1 and
          region.count("ob_d3bw_cc_read32") == 10 and
          region_nc.count("ob_d3bw_cc_read32(hw,") == 9 and
          region_nc.count("ob_d3bw_d11_read32(hw,") == 2 and
          region_nc.count("ob_d3bw_d11_read16(hw,") == 5,
          "unexpected number of access sites in the witness")

    # ---------------------------------------------------------------- T04
    latch = region.count("hw->dev_lost = true;")
    cc_helper = body_of(region, "static enum ob_d3bw_class ob_d3bw_cc_read32")
    d11_32 = body_of(region, "static enum ob_d3bw_class ob_d3bw_d11_read32")
    d11_16 = body_of(region, "static enum ob_d3bw_class ob_d3bw_d11_read16")
    check("T04", "stall_latches_device_loss_in_d11_helpers_only",
          latch == 6 and "hw->dev_lost = true;" in d11_32 and
          "hw->dev_lost = true;" in d11_16 and
          "hw->dev_lost = true;" not in cc_helper,
          "latch sites=%d (the CC helper must not latch; v2 adds one latch "
          "per stalled PMU-domain read in the witness body)" % latch)
    check("T04", "one_second_abort_stops_the_sequence",
          witness.count("if (dur >= OB_D3BW_ABORT_US)") == 11 and
          'verdict = "aborted_slow_read"' in witness,
          "no bounded abort after each read")
    check("T04", "verdicts_cover_both_island_outcomes",
          witness.count('verdict = "island_stall_window_dead"') == 2 and
          'verdict = "island_live_window_alive"' in witness,
          "the island outcomes are not both classified")
    check("T04", "window_alive_is_the_return_value",
          witness.count("window_alive = true;") == 2 and
          witness.find("window_alive = true;") >
          witness.find('ob_d3bw_d11_read16(hw, "B9"') and
          "return window_alive;" in witness,
          "the witness can return true without a completing island read")
    check("T04", "v2_verdicts_cover_the_new_outcomes",
          all('verdict = "%s"' % v in witness for v in V2_VERDICTS),
          "the v2 verdicts are incomplete")
    check("T04", "v2_window_alive_requires_three_live_reads",
          "b8 == OB_D3BW_LIVE && b9 == OB_D3BW_LIVE &&" in witness and
          "b10 == OB_D3BW_LIVE" in witness and
          witness.find("window_alive = true;",
                       witness.find("b10 == OB_D3BW_LIVE")) >
          witness.find("b10 == OB_D3BW_LIVE"),
          "v2 can return true without three LIVE island-range reads")
    check("T04", "v2_summary_carries_the_extended_state",
          all(t in raw_d3b for t in ("witness_v2=1", "pmu_cap=0x%08x",
                                     "pmu_otpstat=0x%08x",
                                     "pllctl_addr=0x%08x",
                                     "pllctl_data=0x%08x", "p1=%s", "p4=%s",
                                     "c1_03fe=%s", "c2_03fc=%s",
                                     "b10_03e0=%s")) and
          raw_d3b.index("fifo530_invalid540=%u%s") >
          raw_d3b.index("writes_issued=0 verdict=%s") and
          raw_d3b.index("v2suffix);") >
          raw_d3b.index("fifo530_invalid540=%u%s"),
          "the v2 summary fields are not appended after the recorded "
          "continuation state")

    # ---------------------------------------------------------------- T05
    caller = body_of(code, "int ob_d3b_test(struct ob_hw *hw)")
    gate = caller.find("if (hw->d3b_boundary_witness) {")
    call = caller.find("ob_d3b_run_boundary_witness(hw)")
    check("T05", "caller_gates_on_the_field",
          gate >= 0 and call > gate and depth_at(caller, call) >= 1,
          "gate=%d call=%d" % (gate, call))
    block = caller[gate:caller.find("ob_d3b_checkpoint(", call)] if \
        gate >= 0 and call > gate else ""
    check("T05", "false_return_is_a_fail_stop",
          "ret = -EIO;" in block and "goto fail_after_dma;" in block,
          "a false return does not abort before the writes")
    check("T05", "witness_precedes_checkpoint_mhf_and_bs",
          order_ok(caller, ["ob_d3b_run_boundary_witness(hw)",
                            "ob_d3b_checkpoint(",
                            "ob_d3b_apply_mhf(",
                            "ob_fw_request_bsinitvals("]),
          "the witness does not precede the checkpoint/MHF/bsinitvals writes")
    check("T05", "witness_runs_after_the_d3a1_prefix",
          order_ok(caller, ["ob_d3a1_run_prefix(", "ob_d3b_run_boundary_witness(hw)"]),
          "the witness does not run at the D3A1 exit")
    check("T05", "fail_after_dma_label_exists",
          "fail_after_dma:" in caller and
          caller.count("goto fail_after_dma;") >= 3,
          "the shared teardown path is gone")

    # ---------------------------------------------------------------- T06
    d3bw = defines(raw_d3b, "OB_D3BW_CCS_")
    l2l = defines(si, "OB_L2L_CCS_")
    pairs = [("OB_D3BW_CCS_REQ_PHYPLL", "OB_L2L_CCS_REQ_PHYPLL"),
             ("OB_D3BW_CCS_AVAIL_PHYPLL", "OB_L2L_CCS_AVAIL_PHYPLL"),
             ("OB_D3BW_CCS_AVAIL_D11PLL", "OB_L2L_CCS_AVAIL_D11PLL"),
             ("OB_D3BW_CCS_AVAIL_HT", "OB_L2L_CCS_AVAIL_HT")]
    diffs = ["%s=%s vs %s=%s" % (a, hex(d3bw.get(a, -1)), b, hex(l2l.get(b, -1)))
             for a, b in pairs if d3bw.get(a) != l2l.get(b) or a not in d3bw]
    check("T06", "ccs_masks_match_the_l2l_upstream_values",
          not diffs, "mask mismatch: %s" % diffs)
    check("T06", "all_four_masks_are_decoded",
          all(m in raw_d3b for m in D3BW_MASKS),
          "the pre-state decode lost one of the upstream mask bits")
    cc_haveht = defines(raw_d3b, "OB_D3BW_CC_HAVEHT")
    si_haveht = defines(si, "OB_CLKCTLST_HAVEHT")
    check("T06", "cc_haveht_matches_ob_si",
          cc_haveht.get("OB_D3BW_CC_HAVEHT") is not None and
          cc_haveht.get("OB_D3BW_CC_HAVEHT") ==
          si_haveht.get("OB_CLKCTLST_HAVEHT"),
          "CC HAVEHT differs: %s vs %s" % (cc_haveht, si_haveht))

    # ---------------------------------------------------------------- T07
    arm = re.findall(
        r'\[\s*"\$MODE"\s*=\s*"bsinitvals_test_only"\s*\]\s*&&\s*\n'
        r'\s*\[\s*"\$\{OB_D3B_BOUNDARY_WITNESS:-0\}"\s*=\s*"1"\s*\]',
        harness)
    check("T07", "harness_env_var_and_insmod_parameter",
          "${OB_D3B_BOUNDARY_WITNESS:-0}" in harness and
          harness.count("d3b_boundary_witness=1") >= 3 and
          len(arm) == 2,
          "the harness cannot arm the witness for the D3B mode (branches=%d, "
          "param sites=%d)" % (len(arm), harness.count("d3b_boundary_witness=1")))
    check("T07", "harness_marker_contract_defined_and_hooked",
          "d3b_boundary_witness_markers_complete() {" in harness and
          'd3b_boundary_witness_markers_complete "$runlog" || return 4' in harness,
          "the witness marker contract is not enforced")
    check("T07", "harness_accepts_only_the_witness_marker",
          '"D3B_BOUNDARY_WITNESS_SUMMARY:"' in harness,
          "the success marker is not the witness summary line")
    marker = body_of(harness, "d3b_boundary_witness_markers_complete() {")
    required = (["D3B_BOUNDARY_WITNESS: begin at the D3A1 exit",
                 "D3B_BOUNDARY_WITNESS_SUMMARY:"] +
                ["stage=%s window=%s offset=%s" % t for t in
                 [("B1", "cc", "0x01e0"), ("B2", "cc", "0x0600"),
                  ("B3", "cc", "0x0608"), ("B4", "cc", "0x0618"),
                  ("B5", "cc", "0x061c"), ("B6", "d11", "0x01e0"),
                  ("B7", "d11", "0x01e8"), ("B8", "d11", "0x03d8")]] +
                ["verdict=void_", "aborted_slow_read",
                 "writes_issued=0 verdict="])
    lost = [r for r in required if r not in marker]
    check("T07", "harness_marker_requires_every_stage",
          not lost, "missing from the marker contract: %s" % lost)
    # M1a: the harness must be able to arm the FIFO-530 continuation with
    # the witness, and the contract must require the recorded state then.
    check("T07", "harness_arms_the_fifo530_continuation",
          "${OB_D11_FIFO530_CONTINUE:-0}" in harness and
          harness.count("d11_fifo530_continue=1") >= 4 and
          "fifo530_continue=1" in marker,
          "the harness cannot arm the witness with the FIFO-530 "
          "continuation")
    check("T07", "marker_requires_the_fifo_state_when_armed",
          "${OB_D11_FIFO530_CONTINUE:-0}" in marker and
          "fifo530_continue=1" in marker and
          "fifo530_entries_continued=" in marker,
          "the marker contract does not require the recorded FIFO-530 "
          "continuation state")
    check("T07", "harness_arms_witness_v2",
          "${OB_D3B_WITNESS_V2:-0}" in harness and
          harness.count("d3b_witness_v2=1") >= 3,
          "the harness cannot arm witness v2 (param sites=%d)" %
          harness.count("d3b_witness_v2=1"))
    check("T07", "harness_refuses_v2_without_the_witness",
          "OB_D3B_WITNESS_V2=1 requires OB_D3B_BOUNDARY_WITNESS=1" in harness and
          "witness v2 precondition" in harness and
          harness.index("witness v2 precondition") <
          harness.index("---------------------------------------------------------------- dry run"),
          "the v2/witness pairing is not refused before any run")
    check("T07", "marker_requires_v2_lines_when_armed",
          "${OB_D3B_WITNESS_V2:-0}" in marker and
          all(s in marker for s in ("witness_v2=1",
                                    "stage=P1 window=cc offset=0x0604",
                                    "stage=P4 window=cc offset=0x0664",
                                    "stage=C1 window=d11 offset=0x03fe",
                                    "stage=C2 window=d11 offset=0x03fc",
                                    "stage=B10 window=d11 offset=0x03e0",
                                    "p1=", "c1_03fe=", "b10_03e0=")),
          "the v2 marker contract requirements are missing")

    # The harness is bash (arrays, `local -a`), so bash -n is the syntax gate.
    syntax = subprocess.run(["bash", "-n", str(HARNESS)],
                            capture_output=True, text=True)
    check("T07", "harness_syntax",
          syntax.returncode == 0, syntax.stderr.strip())

    print("SUMMARY checks=%d failures=%d" % (checks, failures))
    print("== test_d3b_boundary_witness %s ==" % ("PASS" if not failures else "FAIL"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
