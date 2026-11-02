#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Structural tests for Phase 38A (rev42_d11_maccontrol_once).

The C test (ob_phase38a_maccontrol_test.c) executes the real gate, one-read and
classification logic. This file checks the things a C unit test cannot observe,
on the REAL source text with comments and string literals stripped first so a
mention inside a log message is not mistaken for a call:

  P01  exactly one functional offset (0x0120) and exactly one read callback,
       no write callback, no forbidden candidate in the contract
  P02  successful retention is logged as POST_WRITE_RETAINED, never as
       FATAL/dev-lost (Phase 37B Task C correction)
  P03  genuine device-loss and fatal-lifecycle states keep distinct messages
       and the same fail-closed return
  P04  no firmware / PHY / radio / PLL / DMA / IRQ / SHM / mac80211 path is
       reachable from the new mode
  P05  the new mode's dispatch precedes ob_si_probe() and returns; the remove
       hook routes through the diagnostic retention path
  P06  the real 24-way selector composes the 23-way one and the parameter is a
       read-only bool that defaults to 0
  P07  the kernel adapter reuses the Phase 37B engine and issues exactly one
       trusted functional read of OB_D11M_OFF_MACCONTROL

Every check prints a machine-readable RESULT line, same shape as the C test.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DIAG_C = ROOT / "src" / "ob_diag.c"
CORE_C = ROOT / "src" / "ob_core.c"
CORE_H = ROOT / "src" / "ob_core.h"
UCODE_H = ROOT / "src" / "ob_ucode.h"
HEADER = ROOT / "src" / "ob_d11_maccontrol_once.h"

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
             detail.replace("\n", " ")[:400]))
    return ok


def main():
    hdr_raw = HEADER.read_text()
    hdr = strip_c(hdr_raw)
    diag_raw = DIAG_C.read_text()
    diag = strip_c(diag_raw)
    core_raw = CORE_C.read_text()
    core = strip_c(core_raw)

    # ---- P01: one offset, one read callback, no write ---------------------
    offsets = sorted(set(re.findall(r"#define\s+OB_D11M_OFF_\w+\s+(0x[0-9a-fA-F]+)u",
                                    hdr)))
    ops = body_of(hdr, "struct ob_d11m_ops {")
    callbacks = re.findall(r"int\s+\(\*\s*(\w+)\)", ops)
    forbidden_in_hdr = [t for t in ("0x0FFC", "0x0FFCu", "0xffc",
                                   "0x03E0", "0x03E0u", "0x3e0")
                        if t in hdr.lower()]
    check("P01", "contract exposes one offset and one read-only callback",
          offsets == ["0x0120"] and callbacks == ["read_maccontrol"]
          and "write" not in ops.lower() and not forbidden_in_hdr,
          "offsets=%r callbacks=%r forbidden=%r"
          % (offsets, callbacks, forbidden_in_hdr))

    # ---- P02: Task C — successful retention is not device loss ------------
    ai_branch = body_of(core, "if (mode == OB_ISOLATED_REV42_AI_ENABLE) {")
    ai_branch_raw = body_of(core_raw,
                            "if (mode == OB_ISOLATED_REV42_AI_ENABLE) {")
    three_way = (
        "if (hw->dev_lost) {" in ai_branch
        and "if (hw->d3a0.lc.fatal) {" in ai_branch
        and "if (hw->ai_enable_retain) {" in ai_branch
    )
    retained_line = ""
    for ln in core_raw.splitlines():
        if "POST_WRITE_RETAINED" in ln:
            retained_line = ln
            break
    old_shared = re.findall(
        r"dev_crit\([^;]*rev42-ai-en FATAL/dev-lost[^;]*;", core_raw, re.S)
    retained_is_clean = (
        "POST_WRITE_RETAINED" in retained_line
        and "FATAL" not in retained_line
        and "dev-lost" not in retained_line
        and "dev_lost" not in retained_line
    )
    check("P02", "retention logged as POST_WRITE_RETAINED, not device loss",
          three_way and retained_is_clean and not old_shared,
          "three_way=%s old_shared=%r retained=%s"
          % (three_way, old_shared, retained_line.strip()[:160]))

    # ---- P03: genuine loss / fatal keep distinct reporting ----------------
    loss_idx = ai_branch_raw.find("DEVICE_LOSS_SUSPECTED")
    fatal_idx = ai_branch_raw.find("FATAL_LIFECYCLE_STATE")
    retain_idx = ai_branch_raw.find("POST_WRITE_RETAINED")
    loss_returns = "return 0;" in ai_branch_raw[loss_idx:loss_idx + 500]
    fatal_returns = "return 0;" in ai_branch_raw[fatal_idx:fatal_idx + 500]
    check("P03", "device-loss and fatal-lifecycle stay distinct and fail closed",
          0 <= loss_idx < retain_idx and 0 <= fatal_idx < retain_idx
          and loss_returns and fatal_returns,
          "loss=%d fatal=%d retain=%d" % (loss_idx, fatal_idx, retain_idx))

    # ---- P04: nothing beyond the wrapper plus one D11 read ----------------
    region_start = diag.find("static int ob_d11m_read_maccontrol(")
    region_end = diag.find("\nint ob_d11_maccontrol_once_test(")
    region_end = diag.find("\n}\n", region_end) + 3
    region = diag[region_start:region_end]
    forbidden = re.findall(
        r"\b(ob_si_probe|ob_fw_probe|request_firmware|ob_ucode_\w+|ob_initvals\w*|"
        r"ob_dma_\w+|ob_irq_\w+|request_irq|ob_mac80211_\w+|ieee80211_\w+|"
        r"wiphy_register|wlc_phy_\w+|ob_radio_\w+|ob_phy_\w+|ob_acphy_\w+|"
        r"ob_d11_write\w*|ob_axi_write\w*|bcma_write\w*|bcma_awrite\w*|"
        r"pci_read_config\w*|pci_write_config\w*|ob_cc_\w+)\b",
        region)
    check("P04", "no firmware/PHY/radio/DMA/IRQ/mac80211/write path in the mode",
          not forbidden, "hits=%r" % (forbidden,))

    # ---- P05: dispatch precedes ob_si_probe and remove hook routes --------
    branch = core.find("if (mode == OB_ISOLATED_REV42_D11_MACCONTROL_ONCE) {")
    si = core.find("ret = ob_si_probe(hw);")
    branch_body = body_of(core, "if (mode == OB_ISOLATED_REV42_D11_MACCONTROL_ONCE) {")
    reaches_remove = "hw->rev42_d11_maccontrol_once) {" in core
    check("P05", "new mode dispatches before ob_si_probe and retains via diag remove",
          0 <= branch < si and "return 0;" in branch_body and reaches_remove,
          "branch=%d si_probe=%d remove_hook=%s"
          % (branch, si, reaches_remove))

    # ---- P06: real 24-way selector and read-only parameter ---------------
    ucode = strip_c(UCODE_H.read_text())
    sel_hdr = body_of(ucode,
                      "ob_isolated_mode_select24(bool fw_validate_only,")
    param = re.search(
        r"static bool rev42_d11_maccontrol_once;\s*"
        r"module_param\(rev42_d11_maccontrol_once, bool, 0444\);", core)
    check("P06", "24-way selector composes 23-way and param is 0444 bool",
          "ob_isolated_mode_select23(" in sel_hdr
          and "OB_ISOLATED_CONFLICT" in sel_hdr
          and "OB_ISOLATED_REV42_D11_MACCONTROL_ONCE" in sel_hdr
          and param is not None,
          "selector_ok=%s param=%s" % ("ob_isolated_mode_select23(" in sel_hdr,
                                       param is not None))

    # ---- P07: reuse the verified engine, exactly one trusted read --------
    execute = body_of(diag, "int ob_ai_enable_test(")
    mode_entry = body_of(diag, "int ob_d11_maccontrol_once_test(")
    mode_entry_raw = body_of(diag_raw, "int ob_d11_maccontrol_once_test(")
    trusted = re.findall(r"ob_d11_read32_trusted\s*\(", region)
    untrusted = re.findall(r"\bob_d11_read32\s*\(", region)
    check("P07", "adapter reuses one wrapper engine and one trusted D11 read",
          "ob_aie_run(&ops, hw->dev_lost, &res)" in execute
          and "ob_ai_enable_test(hw)" in mode_entry_raw
          and "hw->ai_enable_last" in mode_entry_raw
          and "rev42_d11_maccontrol_once/stage-B1" in mode_entry_raw
          and "ob_d11_read32_trusted(hw," in region
          and "OB_D11M_OFF_MACCONTROL" in region
          and len(trusted) == 1 and not untrusted,
          "trusted=%r untrusted=%r" % (trusted, untrusted))

    # ---- P08: the kernel uses the shared POST formatter, never "ok" ------
    post_fmt = re.search(r'#define OB_D11M_FMT_POST_PENDING \\\n?\s*"([^"]*)"',
                         hdr_raw)
    post_fmt = post_fmt.group(1) if post_fmt else ""
    check("P08", "kernel POST uses shared pending-classification formatter",
          "ob_d11m_format_post_read(" in region
          and "status=pending_classification" in post_fmt
          and "status=ok" not in post_fmt
          and "status=ok" not in region
          and "ob_d11m_format_post_read(" in hdr
          and "not yet classified" in post_fmt,
          "formatter=%r adapter_uses=%s"
          % (post_fmt[:80], "ob_d11m_format_post_read(" in region))

    print("# checks=%d failures=%d" % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
