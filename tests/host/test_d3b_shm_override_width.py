#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""M-D3B-SHM16-VALIDATOR-FIX-01 — SHM override comparison-width regression.

The BCM4352 D3B run of 2026-10-26 (boot bae0cf4c, module 60cb9693…) issued all
78 operations and then failed validation with `SHM override mismatch` (ret=-5).
The three SHM overrides that d11ac1bsinitvals42 performs at 0x0010, 0x001c and
0x0094 are single 16-bit low-half writes (OBJDATA-lo 0x0164) carrying
0x0014/0x0183/0x01f4. ob_d3b_read_shm32() composes a 32-bit observation out of
that word and its neighbour (off and off+2), and the validator compared the
composed value against the 16-bit constant, so any non-zero neighbour made a
correct write look wrong.

This regression exercises the production postcondition predicate
ob_d3b_post_ok() (src/ob_d3b.h) — the same predicate ob_d3b_validate() uses for
its SHM check after the fix — with the hardware-observed vectors, and it checks
the source contract that the D3B validator still routes each override through
the low-half comparison while every other D3B predicate (MHF, MACCONTROL,
MACINTMASK, counts, offset allow-list) stays present and unchanged.

Every check prints a machine-readable RESULT line.
"""

from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
D3B_C = ROOT / "src" / "ob_d3b.c"
D3B_H = ROOT / "src" / "ob_d3b.h"

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
    print("RESULT %s %s test=%s%s"
          % (test_id, "PASS" if ok else "FAIL", label,
             "" if ok else " detail=" +
             str(detail).replace("\n", " ")[:400]))
    return ok


# The hardware-observed postcondition snapshot (run_kernel_log_20261026T141244Z
# line 133): shm10=00000014 shm1c=00640183 shm94=003201f4, MHF vector captured,
# MACCONTROL=44020402, MACINTMASK=00000000, counts 73/39/34.
VECTORS_C = r"""
#include <stdio.h>
#include <string.h>
#include "ob_d3b.h"

static int failures;
static int checks;

static void ok(const char *test_id, const char *label, int cond)
{
	checks++;
	if (!cond) {
		failures++;
		printf("RESULT %s FAIL test=%s\n", test_id, label);
	} else {
		printf("RESULT %s PASS test=%s\n", test_id, label);
	}
}

static void baseline(struct ob_d3b_post *p)
{
	memset(p, 0, sizeof(*p));
	p->mhf[0] = 0x0100u;
	p->mhf[4] = 0x0080u;
	p->shm_10 = OB_D3B_SHM_OVR_10_VAL;
	p->shm_1c = OB_D3B_SHM_OVR_1C_VAL;
	p->shm_94 = OB_D3B_SHM_OVR_94_VAL;
	p->maccontrol = OB_D3B_MACCONTROL_EXPECTED;
	p->macintmask = OB_D3B_MACINTMASK_EXPECTED;
	p->records = OB_D3B_BS_RECORDS;
	p->w16 = OB_D3B_BS_W16;
	p->w32 = OB_D3B_BS_W32;
}

int main(void)
{
	struct ob_d3b_post p;

	/* T01 — the hardware-observed snapshot must validate. */
	baseline(&p);
	p.shm_10 = 0x00000014u;
	p.shm_1c = 0x00640183u;
	p.shm_94 = 0x003201f4u;
	ok("T01", "hardware_snapshot_accepted", ob_d3b_post_ok(&p));

	/* T01 — each override independently, with its observed neighbour. */
	baseline(&p);
	p.shm_10 = 0x00000014u;
	ok("T01", "shm10_observed_00000014", ob_d3b_post_ok(&p));

	baseline(&p);
	p.shm_1c = 0x00640183u;
	ok("T01", "shm1c_observed_00640183", ob_d3b_post_ok(&p));

	baseline(&p);
	p.shm_94 = 0x003201f4u;
	ok("T01", "shm94_observed_003201f4", ob_d3b_post_ok(&p));

	/* T02 — an all-ones neighbour must not influence the low-half check. */
	baseline(&p);
	p.shm_1c = 0xffff0183u;
	ok("T02", "shm1c_neighbour_ffff_accepted", ob_d3b_post_ok(&p));

	/* T03 — a wrong low half must still be rejected. */
	baseline(&p);
	p.shm_1c = 0x00640182u;
	ok("T03", "shm1c_wrong_low_00640182_rejected", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.shm_94 = 0x003201f5u;
	ok("T03", "shm94_wrong_low_003201f5_rejected", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.shm_10 = 0x00000013u;
	ok("T03", "shm10_wrong_low_00000013_rejected", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.shm_94 = 0x00000000u;
	ok("T03", "shm94_zero_rejected", !ob_d3b_post_ok(&p));

	/* T04 — the 32-bit observations stay 32-bit diagnostics. */
	baseline(&p);
	ok("T04", "observation_fields_are_32bit",
	   sizeof(p.shm_10) == 4 && sizeof(p.shm_1c) == 4 && sizeof(p.shm_94) == 4);
	p.shm_10 = 0xdeadbeefu;
	ok("T04", "high_half_preserved_not_masked_by_call",
	   p.shm_10 == 0xdeadbeefu);

	/* T05 — unrelated D3B predicates unchanged. */
	baseline(&p);
	p.mhf[2] = 0x0003u;
	ok("T05", "mhf_predicate_rejects_mhf3", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.mhf[0] = 0x0101u;
	ok("T05", "mhf_predicate_rejects_mhf1", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.maccontrol |= OB_D3B_MCTL_EN_MAC;
	ok("T05", "maccontrol_rejects_en_mac", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.maccontrol = 0x04020402u;
	ok("T05", "maccontrol_rejects_wrong_value", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.macintmask = 0x00000001u;
	ok("T05", "macintmask_rejects_nonzero", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.records = 72u;
	ok("T05", "counts_reject_short_records", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.w16 = 38u;
	ok("T05", "counts_reject_wrong_w16", !ob_d3b_post_ok(&p));

	baseline(&p);
	p.w32 = 33u;
	ok("T05", "counts_reject_wrong_w32", !ob_d3b_post_ok(&p));

	baseline(&p);
	ok("T05", "baseline_accepted", ob_d3b_post_ok(&p));

	printf("SUMMARY checks=%d failures=%d\n", checks, failures);
	return failures ? 1 : 0;
}
"""


def compiled_vectors() -> int:
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "shm_override_width.c"
        exe = Path(td) / "shm_override_width"
        src.write_text(VECTORS_C, encoding="utf-8")
        cmd = ["cc", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "src"),
               "-I", str(ROOT / "tests" / "host"), "-o", str(exe), str(src)]
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.returncode != 0:
            print(build.stdout)
            print(build.stderr)
            check("T01", "vector_program_compiles", False,
                  build.stderr.strip())
            return 1
        print(build.stderr.strip())
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        for line in run.stdout.splitlines():
            if line.startswith("RESULT ") and " test=" in line:
                _, tid, status, rest = line.split(" ", 3)
                check(tid, rest[len("test="):], status == "PASS", line)
            else:
                print(line)
        return run.returncode


def main() -> int:
    if not D3B_C.exists() or not D3B_H.exists():
        print("FAIL: src/ob_d3b.c or src/ob_d3b.h missing")
        return 1

    # T01-T05 compiled: the production predicate on the hardware vectors.
    rc = compiled_vectors()
    check("T01", "vector_program_exit_zero", rc == 0, "exit=%d" % rc)

    code = strip_c(D3B_C.read_text(encoding="utf-8"))
    hdr = strip_c(D3B_H.read_text(encoding="utf-8"))
    val_body = body_of(code, "static int ob_d3b_validate(struct ob_hw *hw)")
    post_body = body_of(hdr, "ob_d3b_post_ok(")
    read_body = body_of(code, "static u32 ob_d3b_read_shm32(struct ob_hw *hw, u16 off)")

    # T06 — the observation read is unchanged: two 16-bit words, composed.
    check("T06", "read_shm32_still_composes_two_words",
          read_body.count("ob_ucode_read_shm16") == 2 and
          "<< 16" in read_body,
          "ob_d3b_read_shm32 no longer composes the neighbour word")

    # T07 — expected constants and offsets are unchanged.
    def define(name):
        m = re.search(r"#define\s+%s\s+((?:0x[0-9a-fA-F]+)|(?:[0-9]+))u\b"
                      % name, hdr)
        return int(m.group(1), 0) if m else None

    check("T07", "override_offsets_unchanged",
          define("OB_D3B_SHM_OVR_10") == 0x0010 and
          define("OB_D3B_SHM_OVR_1C") == 0x001C and
          define("OB_D3B_SHM_OVR_94") == 0x0094,
          "override offsets changed")
    check("T07", "override_values_unchanged",
          define("OB_D3B_SHM_OVR_10_VAL") == 0x0014 and
          define("OB_D3B_SHM_OVR_1C_VAL") == 0x0183 and
          define("OB_D3B_SHM_OVR_94_VAL") == 0x01F4,
          "override expected values changed")

    # T08 — one shared low-half predicate, used by both production sites.
    cases = ((val_body, "ob_d3b_validate"), (post_body, "ob_d3b_post_ok"))
    for body, who in cases:
        check("T08", "%s_uses_low_half_helper" % who,
              "ob_d3b_shm_override_ok" in body,
              "%s does not route the SHM overrides through the shared "
              "low-half predicate" % who)
    check("T08", "helper_compares_low_16_bits",
          re.search(r"static inline bool ob_d3b_shm_override_ok", hdr) is not None,
          "shared helper is missing from src/ob_d3b.h")

    # T09 — the old 32-bit equality comparisons are gone.
    for expr in ("p->shm_10 !=", "p->shm_1c !=", "p->shm_94 !="):
        check("T09", "no_full_word_compare_%s" % expr.split("->")[1][:6],
              expr not in val_body and expr not in post_body,
              "%s still compares the composed 32-bit value" % expr)

    # T10 — every other D3B predicate stays present and unchanged.
    check("T10", "validate_keeps_mhf", "ob_d3b_mhf_is_captured" in val_body,
          "MHF predicate removed")
    check("T10", "validate_keeps_maccontrol",
          "OB_D3B_MACCONTROL_EXPECTED" in val_body and
          "OB_D3B_MCTL_EN_MAC" in val_body,
          "MACCONTROL predicate removed")
    check("T10", "validate_keeps_macintmask",
          "OB_D3B_MACINTMASK_EXPECTED" in val_body,
          "MACINTMASK predicate removed")
    check("T10", "validate_keeps_counts",
          "ob_d3b_bs_counts_ok" in val_body, "counts predicate removed")
    check("T10", "validate_keeps_offset_gate",
          "offset_allowed" in val_body, "offset allow-list gate removed")
    check("T10", "validate_issues_no_mmio",
          not any(tok in val_body for tok in
                  ("ob_d11_read", "ob_d11_write", "ob_ucode_read",
                   "ob_ucode_write", "ob_axi_", "ob_cc_")),
          "validate gained an MMIO access")
    check("T10", "post_ok_keeps_maccontrol_and_mask",
          "OB_D3B_MACCONTROL_EXPECTED" in post_body and
          "OB_D3B_MCTL_EN_MAC" in post_body and
          "OB_D3B_MACINTMASK_EXPECTED" in post_body,
          "post predicate lost a register check")

    # T11 — the contract is still 5 MHF + 73 records (39/34).
    check("T11", "mhf_count_and_table_shape_unchanged",
          define("OB_D3B_MHF_COUNT") == 5 and
          define("OB_D3B_BS_RECORDS") == 73 and
          define("OB_D3B_BS_W16") == 39 and
          define("OB_D3B_BS_W32") == 34,
          "D3B operation contract changed")

    print("SUMMARY checks=%d failures=%d" % (checks, failures))
    print("== test_d3b_shm_override_width %s =="
          % ("PASS" if not failures else "FAIL"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
