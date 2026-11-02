#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Phase 38C structural tests T15s/T16s for the D2A ucode path.

The C test (ob_ucode_d2a_test.c) executes the real state machine through a fake
backend. This file checks the properties a C unit test cannot observe, on the
REAL source text with comments and string literals stripped first:

  T15s  no normal teardown runs on a retained unsafe state, and a failure
        after a mutation is retained instead of returned as a probe error
  T16s  no PHY/radio/DMA/IRQ/mac80211/initvals path is reachable from D2A
  T17s  the verified Gate L modes do not include or call the D2A contract
  T18s  the retired rev42_ai_probe_only refusal is still the first decision

Plus the four defect wirings the contract promises:

  D1  the all-ones check precedes the MI_MACSSPNDD success test
  D2  no code path increments writes_confirmed
  D3  device loss is polled inside the upload loop
  D4  the D2A adapter reads control registers only through the trusted accessor

Every check prints a machine-readable RESULT line.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
UCODE_C = ROOT / "src" / "ob_ucode.c"
CONTRACT_H = ROOT / "src" / "ob_ucode_d2a_contract.h"
CORE_C = ROOT / "src" / "ob_core.c"
DIAG_C = ROOT / "src" / "ob_diag.c"
INITVALS_C = ROOT / "src" / "ob_initvals.c"

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
             str(detail).replace("\n", " ")[:400]))
    return ok


FORBIDDEN_CALLS = [
    "ob_phy_", "ob_radio_", "ob_dma_", "ob_d3a0_", "ob_d3a1_", "ob_d3b_",
    "request_irq", "free_irq", "ieee80211_register_hw",
    "ieee80211_unregister_hw", "register_netdev", "netif_",
    "ob_si_probe", "ob_initvals_apply_common", "ob_initvals_run_d2b",
    "ob_acphy_", "ob_mac80211_", "ob_rx_", "ob_tx_", "ob_irq_",
    "dma_alloc", "dma_free", "wlc_", "brcms_",
]

TEARDOWN_CALLS = [
    "ob_dma_free", "ob_dma_quiesce", "ob_irq_free", "ieee80211_unregister_hw",
    "ob_rx_free", "ob_rx_quiesce", "unregister_netdev", "free_irq",
    "ob_d3a0_remove", "ob_d3a1_remove", "ob_d3b_remove",
]


def main():
    ucode = strip_c(UCODE_C.read_text())
    contract = strip_c(CONTRACT_H.read_text())
    core = strip_c(CORE_C.read_text())
    diag = strip_c(DIAG_C.read_text())
    initvals = strip_c(INITVALS_C.read_text())

    run = body_of(ucode, "int ob_ucode_run_d2a(")
    test = body_of(ucode, "int ob_ucode_test(")
    d2a = body_of(contract, "static inline int ob_d2a_run(")
    remove = body_of(core, "void ob_remove(struct bcma_device *core)")
    probe = body_of(core, "int ob_probe(struct bcma_device *core)")
    retain_guard = body_of(core, "static bool ob_d2a_retain_failure(")

    # ---- T16s: nothing beyond D11 register access is reachable ------------
    d2a_text = ucode + "\n" + contract
    hits = [c for c in FORBIDDEN_CALLS if c in d2a_text]
    check("T16s", "no PHY/radio/DMA/IRQ/mac80211 call from D2A",
          not hits, "found=%s" % hits)

    # The shared D2A core has exactly three callers: the two isolated modes and
    # the pre-existing staged rev42 diagnostic (unchanged, and only reached with
    # rev42_diag_full). Ordinary bring-up never calls it.
    callers = []
    for name, text in (("ob_ucode.c", ucode), ("ob_initvals.c", initvals),
                       ("ob_core.c", core), ("ob_diag.c", diag)):
        if "ob_ucode_run_d2a(" in text:
            callers.append(name)
    check("T16s", "D2A core callers are the isolated/staged modes only",
          sorted(callers) == ["ob_diag.c", "ob_initvals.c", "ob_ucode.c"],
          "callers=%s" % sorted(callers))
    diag_call = diag.find("ob_ucode_run_d2a(")
    diag_full = diag.rfind("if (st->full)", 0, diag_call + 1)
    check("T16s", "staged diag call stays behind rev42_diag_full",
          diag_call >= 0 and diag_full > 0, "guarded=%d" % diag_full)

    # No EN_MAC / initvals / bsinitvals in the D2A sequence itself.
    check("T16s", "no EN_MAC write in D2A",
          "MCTL_EN_MAC" not in d2a, "MCTL_EN_MAC referenced")
    check("T16s", "no initvals in D2A", "initvals" not in d2a,
          "initvals referenced")

    # ob_ucode_test stops after the diagnostic: no later stage is called.
    check("T16s", "ucode_test stops after SHM diagnostic",
          "ob_initvals" not in test and "ob_si_probe" not in test,
          "later stage call found")

    # ---- T15s: retained state is not torn down ----------------------------
    ucode_remove = remove.find("if (hw->ucode_test_only)")
    check("T15s", "ucode remove branch exists", ucode_remove >= 0)
    branch_end = remove.find("if (hw->initvals_test_only)", ucode_remove)
    branch = remove[ucode_remove:branch_end] if branch_end > 0 else ""
    check("T15s", "retained remove branch reports the state",
          "RETAINED" in CORE_C.read_text() and "ucode_retain" in branch,
          "ucode_retain reference")
    bad = [c for c in TEARDOWN_CALLS if c in branch]
    check("T15s", "no teardown call in the retained remove branch",
          not bad, "found=%s" % bad)
    check("T15s", "remove branch returns before the normal teardown",
          "return;" in branch, "no early return")

    # ---- D5 wiring: probe retains instead of returning after a mutation ---
    probe_ucode = probe.find("ret = ob_ucode_test(hw)")
    probe_guard = probe.find("ob_d2a_retain_failure(hw, ret)", probe_ucode)
    probe_release = probe.find("bcma_set_drvdata(core, NULL)", probe_ucode)
    check("T15s", "retention guard precedes drvdata release",
          probe_ucode >= 0 and probe_guard > probe_ucode and
          probe_release > probe_guard,
          "guard=%d release=%d" % (probe_guard, probe_release))
    check("T15s", "retention guard keeps the device bound",
          "return true;" in retain_guard and "return false;" in retain_guard,
          "guard returns")
    check("T15s", "retention guard names the reboot requirement",
          "reboot" in CORE_C.read_text(), "reboot token")
    check("T15s", "a latch is always retained",
          "hw->dev_lost" in retain_guard, "dev_lost check")

    # ---- T17s: the verified Gate L modes are untouched --------------------
    check("T17s", "diag does not include the D2A contract",
          "ob_ucode_d2a_contract" not in diag, "contract included")
    gate_l = body_of(diag, "int ob_d11_maccontrol_once_test(")
    check("T17s", "Gate L mode cannot reach the D2A path",
          "run_d2a" not in gate_l and "ob_ucode" not in gate_l,
          "ucode reference in the Gate L mode")
    wrapper = body_of(diag, "int ob_ai_enable_test(")
    check("T17s", "wrapper engine cannot reach the D2A path",
          "run_d2a" not in wrapper and "ob_ucode" not in wrapper,
          "ucode reference in the wrapper engine")
    for mode in ("ob_ai_enable_test(hw)", "ob_d11_maccontrol_once_test(hw)",
                 "ob_ai_wrapper_only_test(hw)"):
        check("T17s", "Gate L dispatch still present: %s" % mode,
              mode in probe, "missing")

    # ---- T18s: retired mode refusal is still first ------------------------
    retired = probe.find("ob_aiw_legacy_retired(rev42_ai_probe_only)")
    alloc = probe.find("devm_kzalloc(")
    first_access = min([x for x in (
        probe.find("ob_axi_read32("), probe.find("ob_axi_write32("),
        probe.find("ob_d11_read32("), probe.find("ob_d11_write32("),
        probe.find("pci_read_config_"), probe.find("pci_write_config_"),
    ) if x >= 0] or [-1])
    check("T18s", "retired mode refused before allocation",
          retired >= 0 and alloc > retired, "retired=%d alloc=%d"
          % (retired, alloc))
    check("T18s", "retired mode refused before any register access",
          retired >= 0 and (first_access < 0 or first_access > retired),
          "retired=%d first_access=%d" % (retired, first_access))
    check("T18s", "retired mode returns EOPNOTSUPP",
          "EOPNOTSUPP" in probe[:retired + 400], "EOPNOTSUPP")

    # ---- D1: suspicious value is classified before the suspended bit ------
    susp = d2a.find("ob_d2a_status_suspicious(status)")
    success = d2a.find("ob_d2a_psm_success(status)")
    check("D1s", "all-ones classified before MI_MACSSPNDD test",
          susp >= 0 and success > susp, "suspicious=%d success=%d"
          % (susp, success))
    check("D1s", "success predicate refuses all-ones",
          "ob_d2a_status_suspicious(status)" in
          body_of(contract, "static inline bool ob_d2a_psm_success("),
          "predicate body")

    # ---- D2: nothing claims a confirmed upload ----------------------------
    check("D2s", "writes_confirmed never incremented",
          "writes_confirmed++" not in contract, "increment found")
    check("D2s", "adapter publishes the contract's confirmed count",
          "run->confirmed = res->writes_confirmed" in ucode,
          "confirmed assignment")
    check("D2s", "contract initialises confirmed to zero",
          "r->writes_confirmed = 0" in contract, "initialiser")
    check("D2s", "no confirmed-upload claim in the log text",
          "upload confirmed" not in UCODE_C.read_text().lower()
          and "writes confirmed" not in UCODE_C.read_text().lower(),
          "claim found")

    # ---- D3: the upload loop polls the latch ------------------------------
    upload = d2a[d2a.find("for (i = 0; i < r->words; i++)"):]
    check("D3s", "device loss checked inside the upload loop",
          "dev_lost" in upload[:1200], "no latch check")
    check("D3s", "loss during upload has its own stop reason",
          "device_loss_during_upload" in CONTRACT_H.read_text(),
          "reason missing")

    # ---- D4: control reads are trusted ------------------------------------
    adapter_read = body_of(ucode, "static int ob_ucode_d2a_read32(")
    check("D4s", "adapter uses the trusted accessor",
          "ob_d11_read32_trusted" in adapter_read, "plain accessor")
    check("D4s", "adapter has no plain D11 read", "ob_d11_read32(" not in
          adapter_read, "plain ob_d11_read32")
    # Amendment 01: the CP0..CP3 sequence is the pure ob_ucore_prep.h contract;
    # ob_ucode_prepare() is the adapter. The properties are asserted where the
    # code that owns them lives, and executed in ob_ucore_prep_test.c.
    prep = body_of(ucode, "static int ob_ucode_prepare(")
    prep_h = strip_c((ROOT / "src" / "ob_ucore_prep.h").read_text())
    check("D4s", "core-prep prerequisite read is trusted",
          "ob_d11_read32_trusted" in
          body_of(ucode, "static u32 ob_ucore_hw_read_clk1e0("),
          "0x1e0 read")
    check("D4s", "core-prep latches a suspicious wrapper state",
          "ops->latch(ops->ctx, reason)" in prep_h, "no latch")
    check("D4s", "adapter wires the latching callback",
          "ops.latch = ob_ucore_hw_latch;" in prep and
          "ops.read_clk1e0 = ob_ucore_hw_read_clk1e0;" in prep,
          "callback wiring")

    # ---- Amendment 01 (A1/A4) wiring -------------------------------------
    check("A1s", "shared D2A path performs no SHM observation",
          "SHM_FIFOSIZE" not in d2a and "OBJADDR_SHM_SEL" not in d2a,
          "SHM access inside ob_d2a_run")
    check("A1s", "SHM diagnostic is an explicit contract function",
          "static inline int ob_d2a_shm_diag(" in contract,
          "ob_d2a_shm_diag missing")
    check("A1s", "ucode_test is the only D2A-caller of the diagnostic",
          test.count("ob_d2a_shm_diag(&ops, &res, fifo)") == 1 and
          "ob_d2a_shm_diag" not in initvals,
          "diagnostic call sites")
    # W2 moved the ordering into the shared pure contract.  The D2B caller
    # still owns the D2A and common callbacks, but it must wire them into
    # ob_fw_chain_core() in D2A-before-common order; neither callback may be
    # invoked directly by the caller.
    d2b = body_of(initvals, "int ob_initvals_run_d2b(")
    ucode_cb = body_of(initvals, "static int ob_d2b_chain_ucode(")
    common_cb = body_of(initvals, "static int ob_d2b_chain_common(")
    check("A1s", "initvals caller applies common initvals right after D2A",
          "ob_ucode_run_d2a(" in ucode_cb and
          "ob_initvals_apply_common_ex(" in common_cb and
          "ob_fw_chain_core(&ops, &policy, &result)" in d2b and
          d2b.find("ops.ucode = ob_d2b_chain_ucode;") <
          d2b.find("ops.common = ob_d2b_chain_common;") and
          "ob_ucode_run_d2a(" not in d2b and
          "ob_initvals_apply_common(" not in d2b,
          "ordering")
    apply_common = body_of(initvals,
                           "static int ob_initvals_apply_common_internal(")
    check("A1s", "common-initvals SHM reads come after the 610 writes",
          apply_common.find("plan.records; i++") <
          apply_common.find("ob_ucode_read_shm16("),
          "premature SHM read")
    check("A4s", "diagnostic stops at the first failing observation",
          "shm_failed_index = (u8)k;" in contract and
          "shm_observations != 4" in contract,
          "fail-stop missing")
    check("A4s", "16-bit payload is never the 32-bit sentinel",
          "ob_d2a_status_suspicious(half)" not in contract,
          "payload misclassified")
    check("A4s", "retry of the diagnostic is not expressible",
          "for (k = 0; k < 4; k++)" in contract and
          contract.count("for (k = 0; k < 4; k++)") == 1,
          "loop shape")

    print("openbrcm d2a source tests: %d checks, %d failures"
          % (checks, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
