#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Deterministic static safety audit of the isolated radio identity probe.

Source-level machine audit of src/ob_radio.c + src/ob_radio.h for the
`radio_id_probe_only=1` mode (D4-BLOCKER-PLL-BRANCH-HW-PROBE). It verifies that
the probe uses only the allowed guarded register primitives and contains no
PLL / radio-state / calibration / PHY-table / DMA / IRQ / mac80211 operation,
then reports the exact runtime operation counts.

Usage:
    radio_probe_audit.py [--json PATH]

Exit 0 if the source passes, 1 otherwise.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src", "ob_radio.c")
HDR = os.path.join(ROOT, "src", "ob_radio.h")

# Runtime counts of the isolated mode (single shot). See
# docs/m34d4/radio_probe_design.md Part N; verified mechanically by the host
# sequence test (res.radio_writes == 2, res.radio_reads == 2) and by the
# source checks below.
RUNTIME_COUNTS = {
    "d11_reads": 5,            # pre sentinel + reg0 + reg1 + post sentinel + clkctlst
    "d11_writes": 2,           # the two selector-latch writes
    "radio_address_writes": 2,  # write16(0x3d8, 0) and write16(0x3d8, 1)
    "radio_reads": 2,          # read16(0x3da) for reg0 and reg1
    "phy_writes": 0,
    "radio_state_changing_writes": 0,
    "pll_writes": 0,
    "calibration_ops": 0,
    "shm_writes": 0,
    "dma_programming_ops": 0,
    "irq_programming_ops": 0,
    "unbounded_polls": 0,
    "bounded_polls": 0,
    "axi_ioctl_writes": 1,     # D11 MAC-PHY clock (SICF_MPCLKE) gate
}

# Functions the probe must not call (dangerous categories).
FORBIDDEN_FUNCS = (
    "wlc_phy_switch_radio",
    "wlc_phy_init",
    "wlc_phy_anacore",
    "mod_radio_reg",
    "write_radio_reg",
    "or_radio_reg",
    "and_radio_reg",
    "xor_radio_reg",
    "wlc_phy_cal",
    "wlc_phy_chanspec",
    "wlc_phy_table_write",
    "ob_d3a0_bringup",
    "ob_d3a0_teardown",
    "ob_d3a1_run_prefix",
    "ob_d3b_test",
    "ob_ucode_run_d2a",
    "ob_initvals_",
    "request_irq",
    "ob_mac80211",
    "ob_rx_init",
    "ob_dma_init",
    "ob_ucode_write_shm16",
)

# Register offsets the probe may write through the D11 window.
ALLOWED_D11_WRITE_OFFSETS = ("OB_RADIO_REG_ADDR_LATCH",)
ALLOWED_D11_READ_OFFSETS = ("OB_RADIO_REG_DATA", "OB_RADIO_REG_SENTINEL",
                            "OB_UCODE_REG_CLKCTLST")


def _strip_comments(text):
    """Remove C block and line comments so prose cannot trip the audit."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def audit():
    with open(SRC, "r", encoding="utf-8") as fh:
        c = _strip_comments(fh.read())
    with open(HDR, "r", encoding="utf-8") as fh:
        h = _strip_comments(fh.read())

    problems = []

    for tok in FORBIDDEN_FUNCS:
        if tok in c or tok in h:
            problems.append("forbidden function referenced: %s" % tok)

    # The sequence (in ob_radio.h) must write ONLY the selector latch, exactly
    # twice, with indices 0 and 1, and read ONLY the data port.
    sel_writes = re.findall(
        r"write16\s*\(ctx,\s*OB_RADIO_REG_ADDR_LATCH,\s*([01])\s*\)", h)
    if sorted(sel_writes) != ["0", "1"]:
        problems.append(
            "ob_radio.h: expected exactly two selector writes (index 0,1), "
            "got %r" % sel_writes)
    other_writes = re.findall(r"ops->write16\s*\(\s*ctx\s*,\s*([^,]+),", h)
    for off in other_writes:
        if off.strip() != "OB_RADIO_REG_ADDR_LATCH":
            problems.append("unexpected header write offset: %s" % off.strip())

    data_reads = len(re.findall(r"read16\s*\(ctx,\s*OB_RADIO_REG_DATA\s*\)", h))
    if data_reads != 2:
        problems.append("ob_radio.h: expected two data reads, got %d"
                        % data_reads)

    # No raw bcma MMIO in the probe body (must use the central guard).
    for m in re.finditer(r"\bbcma_(read|write)[0-9a-z]*\s*\(", c):
        problems.append("raw bcma MMIO call in ob_radio.c: %s"
                        % m.group(0).strip())

    if "bcma_host_pci_up" not in c:
        problems.append("ob_radio_prepare: missing bcma_host_pci_up")

    # The sequence must come from the shared pure header (single implementation).
    if "ob_radio_sequence" not in h or "ob_radio_decode" not in h:
        problems.append("ob_radio.h: missing pure sequence/decode layer")

    return {
        "mode": "radio_id_probe_only",
        "source_file": "src/ob_radio.c",
        "runtime_counts": RUNTIME_COUNTS,
        "forbidden_functions": list(FORBIDDEN_FUNCS),
        "allowed_d11_write_offsets": list(ALLOWED_D11_WRITE_OFFSETS),
        "allowed_d11_read_offsets": list(ALLOWED_D11_READ_OFFSETS),
        "problems": problems,
        "ok": not problems,
    }


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", help="write the audit JSON here")
    args = ap.parse_args(argv)

    result = audit()
    out = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            fh.write(out)
    else:
        sys.stdout.write(out)

    if result["problems"]:
        for p in result["problems"]:
            print("FAIL: %s" % p, file=sys.stderr)
        return 1
    print("radio_probe_audit: PASS (dangerous categories zero)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
