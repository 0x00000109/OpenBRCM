#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""M-D3B-PREMHF-SAFE-READ-01 — offline regression tests for the D3B pre-MHF path.

The isolated D3B slice used to read 16-bit PHYVERSION from D11+0x3e0 before the
five MHF writes, discard the result, and corroborate only when the value was
0xffff. On the BCM4352 that read is the vendor's post-PHY-reset status read; the
isolated D3B state never establishes the vendor's SICF_PCLKE / wlc_bmac_phy_reset
prerequisites, and the hardware run of 2026-10-26 stalled there and latched
device loss before any MHF write.

These tests are source/static contracts plus one executed guard scenario. They
cannot compile src/ob_d3b.c (kernel headers), so they check the properties the
compiled host test cannot observe, on the real source text with comments and
string literals stripped first:

  T01  no PHYVERSION MMIO read at D11+0x3e0 anywhere in the D3B path
  T02  exactly one unconditional trusted MACCONTROL checkpoint, executed
       before the first MHF write
  T03  fail-closed composition: an all-ones trusted read latches, the
       checkpoint returns -ENODEV, and the caller stops before any MHF or
       bsinitvals write while the fatal DMA retention path stays intact
  T05  no lifecycle expansion: no PCLKE/PHY reset/D11 reset/radio prep/IRQ, and
       the pre-MHF region issues exactly one read and zero writes

T04 (the exact 5 MHF + 73 bsinitvals operation plan) is executed by the
compiled host test tests/host/ob_d3b_test.c (test_mhf_operation_plan,
test_bs_golden_contract).

Every check prints a machine-readable RESULT line.
"""

from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
D3B_C = ROOT / "src" / "ob_d3b.c"
GUARD_H = ROOT / "src" / "ob_guard.h"

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


def depth_at(text: str, pos: int) -> int:
    return text[:pos].count("{") - text[:pos].count("}")


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


MMIO_TOKENS = ("ob_d11_read", "ob_d11_write", "ob_ucode_write", "ob_ucode_read",
               "ob_axi_read", "ob_axi_write", "ob_cc_read", "ob_cc_write",
               "ob_bcma_", "bcma_read", "bcma_write")

FORBIDDEN_LIFECYCLE = (
    "SICF_", "PCLKE", "MPCLKE", "corereset", "phy_reset", "macphyclk",
    "ob_radio_prepare_for_identity", "ob_si_powerup", "request_irq",
    "ieee80211_", "dma_alloc", "ob_initvals_apply_common",
    "ob_acphy_", "clkctl",
)


def guard_scenario(tmp: Path) -> int:
    """Execute the real pure guard layer with the checkpoint's predicate."""
    src = tmp / "guard_scenario.c"
    exe = tmp / "guard_scenario"
    src.write_text(
        """
#include <stdio.h>
#include <string.h>
#include "%s"
""" % str(GUARD_H) + """

struct io { unsigned reads, writes; u32 r32; u16 r16; };

static u32 rd32(void *c, u16 off) { (void)off; ((struct io *)c)->reads++; return ((struct io *)c)->r32; }
static u16 rd16(void *c, u16 off) { (void)off; ((struct io *)c)->reads++; return ((struct io *)c)->r16; }
static void wr32(void *c, u16 off, u32 v) { (void)off; (void)v; ((struct io *)c)->writes++; }
static void wr16(void *c, u16 off, u16 v) { (void)off; (void)v; ((struct io *)c)->writes++; }

static const struct ob_guard_ops ops = { rd32, rd16, wr32, wr16 };

int main(void)
{
\tstruct io io;
\tstruct ob_guard_stat st;
\tbool lost;
\tint failures = 0;

\t/* 1. healthy trusted MACCONTROL read does not latch */
\tlost = ob_guard_next_latch(false, true, 0x44020402u);
\tif (lost) { printf("FAIL healthy read latched\\n"); failures++; }

\t/* 2. injected all-ones trusted read is the exact checkpoint predicate */
\tlost = ob_guard_next_latch(false, true, OB_GUARD_ALL_ONES32);
\tif (lost != true) { printf("FAIL all-ones did not latch\\n"); failures++; }

\t/* 3. a 16-bit 0xffff observation is NOT authoritative (no latch) */
\tif (ob_guard_next_latch(false, false, 0x0000ffffu)) {
\t\tprintf("FAIL 16-bit observation latched\\n"); failures++;
\t}

\t/* 4. after the latch no MHF/bsinitvals write reaches the backend */
\tmemset(&io, 0, sizeof(io));
\tmemset(&st, 0, sizeof(st));
\tob_guard_do_write32(lost, &st, &ops, &io, 0x0160, 0x00010017u);
\tob_guard_do_write16(lost, &st, &ops, &io, 0x0166, 0x0100u);
\tob_guard_do_write16(lost, &st, &ops, &io, 0x0686, 0x09d0u);
\tif (io.writes != 0) { printf("FAIL writes after latch: %u\\n", io.writes); failures++; }
\tif (st.writes_blocked != 3) { printf("FAIL blocked count %u\\n", st.writes_blocked); failures++; }

\t/* 5. and the latch is monotonic */
\tif (!ob_guard_next_latch(lost, true, 0x44020402u)) {
\t\tprintf("FAIL latch cleared\\n"); failures++;
\t}

\tif (failures)
\t\treturn 1;
\tprintf("guard scenario PASS\\n");
\treturn 0;
}
""",
        encoding="utf-8")

    # tests/host/linux/types.h is found through the -I of the host test dir.
    cmd = ["cc", "-O2", "-Wall", "-Wextra",
           "-I", str(ROOT / "src"), "-I", str(ROOT / "tests" / "host"),
           "-o", str(exe), str(src)]
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout)
        print(build.stderr)
        check("T03", "guard_scenario_compiles", False, build.stderr.strip())
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    if run.stdout.strip():
        print(run.stdout.strip())
    if run.stderr.strip():
        print(run.stderr.strip())
    return run.returncode


def main():
    if not D3B_C.exists() or not GUARD_H.exists():
        print("FAIL: src/ob_d3b.c or src/ob_guard.h missing")
        return 1

    code = strip_c(D3B_C.read_text(encoding="utf-8"))
    guard = strip_c(GUARD_H.read_text(encoding="utf-8"))
    body = body_of(code, "int ob_d3b_test(struct ob_hw *hw)")
    cp_body = body_of(code, "static int ob_d3b_checkpoint")
    mhf_body = body_of(code, "static int ob_d3b_apply_mhf")
    bs_body = body_of(code, "static int ob_d3b_apply_bs")

    check("T01", "no_d11_3e0_token_in_file",
          not re.search(r"0x0*3e0\b", code),
          "0x3e0 still present in src/ob_d3b.c")
    check("T01", "no_phyver_constant_in_d3b_test",
          "OB_D3B_REG_PHYVER_STATUS" not in body, "constant still used")
    check("T01", "no_16bit_read_in_d3b_test",
          "ob_d11_read16" not in body, "16-bit read still issued")

    # ---- T02: one unconditional trusted checkpoint before the first MHF ----
    n_cp = body.count("ob_d3b_checkpoint(")
    check("T02", "single_premhf_checkpoint", n_cp == 1,
          "checkpoint call sites in ob_d3b_test: %d" % n_cp)

    cp_pos = body.find("ob_d3b_checkpoint(")
    mhf_pos = body.find("ob_d3b_apply_mhf(")
    check("T02", "checkpoint_before_first_mhf",
          cp_pos >= 0 and mhf_pos >= 0 and cp_pos < mhf_pos,
          "checkpoint=%d mhf=%d" % (cp_pos, mhf_pos))
    check("T02", "checkpoint_is_unconditional",
          cp_pos >= 0 and depth_at(body, cp_pos) == 0,
          "brace depth at checkpoint: %d" % (depth_at(body, cp_pos) if cp_pos >= 0 else -1))

    tail = body[body.find("ob_d3b_checkpoint("):mhf_pos] if cp_pos >= 0 and mhf_pos > cp_pos else ""
    check("T02", "checkpoint_result_gates_mhf",
          "if (ret)" in tail and "goto fail_after_dma" in tail,
          "no fail-closed guard between checkpoint and MHF")
    check("T02", "no_mmio_between_checkpoint_and_mhf",
          not any(tok in tail for tok in MMIO_TOKENS),
          "MMIO between checkpoint and MHF")

    check("T02", "checkpoint_reads_trusted_maccontrol",
          "ob_d11_read32_trusted" in cp_body and
          "OB_D3A1_REG_MACCONTROL" in cp_body,
          "checkpoint is not a trusted MACCONTROL read")
    check("T02", "checkpoint_returns_enodev_on_loss",
          "hw->dev_lost" in cp_body and "-ENODEV" in cp_body,
          "checkpoint does not stop on a latched loss")

    # ---- T03: fail-closed composition ----
    fail = body[body.find("fail_after_dma:") + len("fail_after_dma:"):] if "fail_after_dma:" in body else ""
    check("T03", "fail_path_has_latch_retention",
          "ob_dev_lost_is_latched(hw)" in fail and
          "OB_DMA_OWNER_REASON_DEVICE_LOST" in fail and
          "ob_d3b_retain_dma" in fail,
          "fatal path lost the durable-owner retention")
    check("T03", "fail_path_returns_before_teardown",
          "if (ob_dev_lost_is_latched(hw))" in fail and
          fail.find("if (ob_dev_lost_is_latched(hw))") <
          fail.find("ob_d3a0_teardown(hw)"),
          "teardown reachable before the latch check")
    pre_latch = fail[:fail.find("if (ob_dev_lost_is_latched(hw))")] if "if (ob_dev_lost_is_latched(hw))" in fail else fail
    check("T03", "fail_path_issues_no_mmio_before_latch_check",
          not any(tok in pre_latch for tok in MMIO_TOKENS),
          "MMIO on the failure path before the latch check")
    check("T03", "no_mhf_or_bs_after_checkpoint_failure",
          "ob_d3b_apply_mhf" not in fail and "ob_d3b_apply_bs" not in fail,
          "failure path can still reach MHF/bsinitvals")

    with tempfile.TemporaryDirectory() as td:
        rc = guard_scenario(Path(td))
    check("T03", "executed_guard_scenario", rc == 0,
          "guard scenario exit=%d" % rc)

    # ---- T05: no lifecycle expansion, no extra pre-MHF access ----
    prefix_pos = body.find("ob_d3a1_run_prefix(")
    pre = body[prefix_pos:mhf_pos] if 0 <= prefix_pos < mhf_pos else ""
    check("T05", "premhf_region_single_mmio_site",
          pre.count("ob_d3b_checkpoint(") == 1 and
          not any(tok in pre for tok in MMIO_TOKENS),
          "pre-MHF region MMIO sites: %d checkpoint, tokens=%s"
          % (pre.count("ob_d3b_checkpoint("),
             [t for t in MMIO_TOKENS if t in pre]))
    check("T05", "premhf_region_has_no_writes",
          "ob_d11_write" not in pre and "ob_ucode_write" not in pre,
          "pre-MHF region performs a write")

    hits = [tok for tok in FORBIDDEN_LIFECYCLE if tok in code]
    check("T05", "no_lifecycle_expansion_in_d3b",
          not hits, "forbidden lifecycle tokens in src/ob_d3b.c: %s" % hits)

    check("T05", "mhf_and_bs_helpers_unchanged_shape",
          mhf_body.count("ob_ucode_write_shm16_checked") == 1 and
          "OB_D3B_MHF_COUNT" in mhf_body and
          bs_body.count("ob_d3b_write_iv") == 1 and
          "OB_D3B_BS_RECORDS" in bs_body,
          "MHF/bsinitvals appliers no longer operate on the 5/73 contract")

    # The guard's latch predicate must stay the trusted all-ones predicate.
    check("T05", "guard_predicate_unchanged",
          "OB_GUARD_ALL_ONES32" in guard and "ob_guard_next_latch" in guard,
          "guard predicate no longer the all-ones trusted rule")

    print("SUMMARY checks=%d failures=%d" % (checks, failures))
    print("== test_d3b_premhf_safe_read %s ==" % ("PASS" if not failures else "FAIL"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
