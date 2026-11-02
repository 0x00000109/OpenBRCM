#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Structural safety tests for the Phase 36Z AI-wrapper-only diagnostic.

The C test (ob_ai_wrapper_only_test.c) executes the real sequencing logic.
This file checks the things a C unit test cannot observe, on the REAL source
text, with comments and string literals stripped first so a mention inside a
log message is not mistaken for a call:

  Z03s  the retired mode is refused before allocation / mode selection, and
        its entry point contains no accessor call at all
  Z17s  the new diagnostic performs no PCI config access
  Z19s  the new diagnostic performs no register write of any kind
  Z20s  no DMA / firmware / IRQ / PHY / radio stage is reachable from it
  Z21s  it publishes no lifecycle state and registers nothing

Every check prints a machine-readable RESULT line, same shape as the C test.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DIAG_C = ROOT / "src" / "ob_diag.c"
CORE_C = ROOT / "src" / "ob_core.c"
DIAG_H = ROOT / "src" / "ob_diag.h"
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
          % (test_id, "PASS" if ok else "FAIL", label, detail.replace("\n", " ")[:400]))
    return ok


def main():
    diag_raw = DIAG_C.read_text()
    diag = strip_c(diag_raw)
    core_raw = CORE_C.read_text()
    core = strip_c(core_raw)

    new_fns = {
        "ob_aiw_read_bcma": body_of(diag, "static int ob_aiw_read_bcma("),
        "ob_aiw_log_dev": body_of(diag, "static void ob_aiw_log_dev("),
        "ob_aiw_dev_lost_bcma": body_of(diag, "static bool ob_aiw_dev_lost_bcma("),
        "ob_ai_wrapper_only_test": body_of(diag, "int ob_ai_wrapper_only_test("),
        "ob_ai_probe_test": body_of(diag, "int ob_ai_probe_test("),
    }
    region_start = diag.find("/* ---- AI-wrapper-only diagnostic")
    region_start = diag.find("static int ob_aiw_read_bcma(") if region_start < 0 else region_start
    region_end = diag.find("int ob_ai_probe_test(")
    region_end = diag.find("\n}\n", region_end) + 3
    region = diag[region_start:region_end]

    # ---- Z03s: retired mode refused before allocation / selection ---------
    gate = core.find("ob_aiw_legacy_retired(rev42_ai_probe_only)")
    alloc = core.find("devm_kzalloc(&core->dev")
    select = core.find("ob_isolated_mode_select23(")
    si = core.find("ret = ob_si_probe(hw);")
    registered = core.find("bcma_set_drvdata(core, hw);")
    # The gate must precede every effect: allocation, mode selection, the
    # first normal-path stage and the drvdata publication.
    ordered = 0 <= gate < min(x for x in (alloc, select, si, registered) if x > 0)
    retire_return = "return -EOPNOTSUPP;" in core[gate:gate + 900]
    stub_clean = not re.search(
        r"\b(ob_d11_|ob_axi_|ob_cc_|pci_read_config|pci_write_config|bcma_read|bcma_aread|ioread|iowrite|readl|writel)\b",
        new_fns["ob_ai_probe_test"])
    no_zeroffc_in_stub = "0ffc" not in new_fns["ob_ai_probe_test"].lower()
    check("Z03s", "legacy refused before allocation and mode selection",
          ordered and retire_return and stub_clean and no_zeroffc_in_stub,
          "gate=%d alloc=%d select=%d si_probe=%d drvdata=%d return_eopnotsupp=%s stub_clean=%s no_0ffc=%s"
          % (gate, alloc, select, si, registered, retire_return, stub_clean, no_zeroffc_in_stub))

    # ---- Z17s: no PCI config access anywhere in the new diagnostic --------
    cfg_hits = re.findall(r"\b(pci_read_config_\w+|pci_write_config_\w+|pci_bus_read_config_\w+|pci_bus_write_config_\w+)\b", region)
    check("Z17s", "no PCI config access in the wrapper-only path",
          not cfg_hits, "hits=%r" % (cfg_hits,))

    # ---- Z19s: no register write of any kind ------------------------------
    write_hits = re.findall(
        r"\b(ob_d11_write\w*|ob_axi_write\w*|ob_cc_write\w*|bcma_write\w*|bcma_awrite\w*|iowrite\w*|writel|writew|writeb|wlc_phy_switch_radio)\b",
        region)
    assignment_hits = re.findall(r"ob_axi_write32\s*\(|ob_d11_write32\s*\(", region)
    check("Z19s", "no write accessor or register write in the wrapper-only path",
          not write_hits and not assignment_hits,
          "writes=%r assignments=%r" % (write_hits, assignment_hits))

    # ---- Z20s: no DMA / firmware / IRQ / PHY / radio reachable ------------
    forbidden = re.findall(
        r"\b(ob_si_probe|ob_fw_probe|request_firmware|ob_ucode|ob_initvals|ob_dma_\w+|ob_irq_\w+|request_irq|ob_mac80211_\w+|wlc_phy_\w+|ob_radio_\w+|ob_phy_\w+|ob_acphy_\w+|bcma_core_enable|bcma_core_disable|bcma_host_pci_up)\b",
        region)
    check("Z20s", "no DMA/firmware/IRQ/PHY/radio stage in the wrapper-only path",
          not forbidden, "hits=%r" % (forbidden,))

    # ---- Z21s: no lifecycle state published -------------------------------
    state_hits = re.findall(
        r"(d11_state|acphy_start_state|phy_radio_identity_valid|radio_access_initialized|ieee80211_register|mac80211_register|wiphy_register)",
        region)
    check("Z21s", "no lifecycle state or registration published by the diagnostic",
          not state_hits, "hits=%r" % (state_hits,))

    # ---- the contract itself: exactly one window, no write callback ------
    hdr = strip_c(HEADER.read_text())
    ops = body_of(hdr, "struct ob_aiw_ops {")
    fields = [ln.strip() for ln in ops.split(";") if ln.strip()]
    single_window = re.search(r"enum\s+ob_aiw_window\s*\{([^}]*)\}", hdr)
    windows = [w.strip() for w in single_window.group(1).split(",") if w.strip()] if single_window else []
    check("Z18s", "contract exposes one window and no write/config callback",
          len(fields) == 4 and "write" not in ops and "config" not in ops
          and windows == ["OB_AIW_WIN_WRAPPER = 0", "OB_AIW_WIN_MAX"],
          "fields=%r windows=%r" % (fields, windows))

    print("# checks=%d failures=%d" % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
