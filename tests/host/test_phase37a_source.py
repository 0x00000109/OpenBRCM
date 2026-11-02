#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Phase 37A structural tests A01s/A09s/A11s/A12s/A13s/A14s.

The two C tests execute the real admission and sequencing functions. This file
checks the wiring and absence properties a C unit test cannot observe, on the
REAL source text with comments and string literals stripped first:

  A01s/A14s  the default-load refusal is wired into ob_probe() before every
             allocation, drvdata publication, dispatch branch and ob_si_probe()
  A09s       a suspicious all-ones read is classified before any POST marker,
             and attempts/completions are counted at the accessor boundary
  A11s       no D11 functional-core (0x0FFC / core->addr) access is reachable
             anywhere in the wrapper-only path
  A12s       the adapter latches dev_lost only for a suspicious result and
             ob_probe() retains the device instead of cleaning up
  A13s       no firmware/DMA/IRQ/PHY/radio stage is reachable from it

Every check prints a machine-readable RESULT line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DIAG_C = ROOT / "src" / "ob_diag.c"
CORE_C = ROOT / "src" / "ob_core.c"
HEADER = ROOT / "src" / "ob_ai_wrapper_only.h"

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


def body_of(code: str, signature: str) -> str:
    """Return the brace-balanced body of `signature` in stripped code."""
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


def check(test_id, label, ok, detail=""):
    global failures, checks
    checks += 1
    if not ok:
        failures += 1
    print("RESULT %s %s test=%s detail=%s"
          % (test_id, "PASS" if ok else "FAIL", label,
             detail.replace("\n", " ")[:500]))
    return ok


def main():
    core_raw = CORE_C.read_text()
    core = strip_c(core_raw)
    diag = strip_c(DIAG_C.read_text())
    hdr = strip_c(HEADER.read_text())
    probe = body_of(core, "int ob_probe(struct bcma_device *core)")

    # ---- A01s/A14s: default-load guard is wired before every side effect ---
    guard_call = probe.find("ob_isolated_default_load_refused(")
    alloc = probe.find("devm_kzalloc(")
    drvdata = probe.find("bcma_set_drvdata(core, hw)")
    si = probe.find("ob_si_probe(hw)")
    dispatch = probe.find("ob_ucode_test(hw)")
    first_mmio = min(
        [x for x in (
            probe.find("ob_axi_read32("),
            probe.find("ob_axi_write32("),
            probe.find("ob_d11_read32("),
            probe.find("ob_d11_write32("),
            probe.find("pci_read_config_"),
            probe.find("pci_write_config_"),
        ) if x >= 0] or [-1])
    guard_body = probe[guard_call:guard_call + 700] if guard_call >= 0 else ""
    returns_unsupported = "return -EOPNOTSUPP;" in guard_body
    before_all = (guard_call >= 0 and alloc > guard_call and
                  drvdata > guard_call and si > guard_call and
                  dispatch > guard_call and
                  (first_mmio < 0 or first_mmio > guard_call))
    check("A01s", "default-load guard precedes allocation, drvdata and dispatch",
          before_all and returns_unsupported,
          "guard=%d alloc=%d drvdata=%d dispatch=%d si_probe=%d first_mmio=%d "
          "return_eopnotsupp=%s"
          % (guard_call, alloc, drvdata, dispatch, si, first_mmio,
             returns_unsupported))

    # The predicate must be the one that also admits sprom/sflags diagnostics,
    # not a bare `mode == NONE` check that would break them.
    check("A14s", "default-load guard calls the real predicate with both flags",
          "ob_isolated_default_load_refused(mode, sprom_evidence_only,"
          in probe and "sflags_diag_only)" in probe,
          "predicate wiring present")

    # ---- A09s: suspicious readback precedes POST; counters at boundaries ---
    read_one = body_of(hdr, "static inline int ob_aiw_read_one(")
    pre = read_one.find("OB_AIW_EV_PRE")
    attempted = read_one.find("reads_attempted++")
    access = read_one.find("ops->read32(")
    all_ones = read_one.find("val == OB_AIW_ALL_ONES")
    completed = read_one.find("reads_completed++")
    recorded = read_one.find("*dst = val")
    post = read_one.find("OB_AIW_EV_POST")
    ordered = (0 <= pre < attempted < access < all_ones < recorded <
               completed < post)
    check("A09s", "PRE -> attempt -> access -> all-ones check -> record -> complete -> POST",
          ordered,
          "pre=%d attempted=%d access=%d all_ones=%d recorded=%d completed=%d post=%d"
          % (pre, attempted, access, all_ones, recorded, completed, post))

    # ---- A11s: no functional-core access in the wrapper-only path ----------
    region_start = diag.find("static int ob_aiw_read_bcma(")
    region_end = diag.find("int ob_ai_probe_test(")
    region = diag[region_start:region_end] if region_start >= 0 and region_end > region_start else ""
    # Both the kernel adapter region and the pure contract header are in
    # scope: a fourth read inserted into ob_aiw_run() is just as reachable as
    # one inserted into ob_diag.c.
    core_access = re.findall(
        r"(ob_d11_(?:read|write)\w*|core->addr|0x0ffc\w*)",
        (region + hdr).lower())
    check("A11s", "no D11 functional-core access in the wrapper-only region",
          bool(region) and not core_access,
          "region_len=%d hits=%r" % (len(region), core_access))

    one_window = re.search(r"enum\s+ob_aiw_window\s*\{([^}]*)\}", hdr)
    windows = [w.strip() for w in one_window.group(1).split(",") if w.strip()] \
        if one_window else []
    check("A11s2", "contract exposes only the wrapper window",
          windows == ["OB_AIW_WIN_WRAPPER = 0", "OB_AIW_WIN_MAX"],
          "windows=%r" % (windows,))

    # ---- A12s: latch only on suspicious; ob_probe retains the device -------
    adapter = body_of(diag, "int ob_ai_wrapper_only_test(")
    suspicious = adapter.find("if (res.suspicious)")
    latch = adapter.find("hw->dev_lost = true")
    check("A12s", "adapter latches dev_lost only under res.suspicious",
          0 <= suspicious < latch,
          "suspicious=%d latch=%d" % (suspicious, latch))

    # A10s: the kernel-side failure report must carry both counters, and the
    # removed ambiguity (`reads_issued`) must not survive anywhere.
    a10s = ("res.reads_attempted" in adapter and
            "res.reads_completed" in adapter and
            "attempted" in adapter and "completed" in adapter and
            "reads_issued" not in adapter and "reads_issued" not in hdr)
    check("A10s", "adapter reports attempted and completed reads separately",
          a10s, "attempted=%s completed=%s old_name=%s"
          % ("res.reads_attempted" in adapter,
             "res.reads_completed" in adapter,
             "reads_issued" in adapter or "reads_issued" in hdr))

    block_start = probe.find("if (mode == OB_ISOLATED_REV42_AI_WRAPPER_ONLY)")
    block_end = probe.find("if (mode == OB_ISOLATED_REV42_AI_PROBE)", block_start)
    wrapper_block = probe[block_start:block_end] if block_start >= 0 and block_end > block_start else ""
    retained = ("ob_ai_wrapper_only_test(hw)" in wrapper_block and
                "if (hw->dev_lost || hw->d3a0.lc.fatal)" in wrapper_block and
                "return 0;" in wrapper_block)
    check("A12s2", "ob_probe retains the device on dev_lost/fatal instead of cleanup",
          retained, "wrapper block len=%d" % len(wrapper_block))

    # ---- A13s: no firmware/DMA/IRQ/PHY/radio stage reachable --------------
    forbidden = re.findall(
        r"\b(ob_si_probe|ob_fw_probe|request_firmware|ob_ucode|ob_initvals|"
        r"ob_dma_\w+|ob_irq_\w+|request_irq|ob_mac80211_\w+|wlc_phy_\w+|"
        r"ob_radio_\w+|ob_phy_\w+|ob_acphy_\w+|bcma_core_enable|"
        r"bcma_core_disable|bcma_host_pci_up)\b", region)
    check("A13s", "no firmware/DMA/IRQ/PHY/radio stage in the wrapper-only region",
          not forbidden, "hits=%r" % (forbidden,))

    print("# checks=%d failures=%d" % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
