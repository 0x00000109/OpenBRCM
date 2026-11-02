#!/usr/bin/env python3
"""Static contract tests for the D11 BCMA postcondition diagnostic.

Phase 38C Amendment 01 moved the CP0..CP3 sequence into the pure contract
`src/ob_ucore_prep.h`; `src/ob_ucode.c` keeps only the kernel adapter (one
callback per wrapper read, core operation and log line). These tests assert
that wiring from both sides:

  * the only raw wrapper reads are the two single-read adapter callbacks,
  * the sequence still runs CP0 -> host up -> CP1 -> enable -> CP2 ->
    clockmode -> CP3 -> functional read, in that order,
  * the postcondition gate still precedes the D11+0x01e0 read,
  * the +0x01e0 sentinel is still the latching trusted accessor.

Executed coverage of the same code lives in ob_ucore_prep_test.c.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SRC_TEXT = (ROOT / "src" / "ob_ucode.c").read_text(encoding="utf-8")
HEADER = (ROOT / "src" / "ob_ucode.h").read_text(encoding="utf-8")
PREP = (ROOT / "src" / "ob_ucore_prep.h").read_text(encoding="utf-8")


def body_of(text, signature):
    """Brace-matched body of the function that starts at @signature."""
    i = text.index(signature)
    j = text.index("{", i)
    depth = 0
    k = j
    while k < len(text):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
        k += 1
    raise AssertionError("unbalanced braces for %r" % signature)


class D11BcmaPostconditionTests(unittest.TestCase):
    def test_snapshot_reads_only_bcma_wrapper_predicate_registers(self):
        ioctl = body_of(SRC_TEXT, "static u32 ob_ucore_hw_read_ioctl(")
        reset = body_of(SRC_TEXT, "static u32 ob_ucore_hw_read_reset(")
        self.assertIn("bcma_aread32(hw->core, BCMA_IOCTL)", ioctl)
        self.assertIn("bcma_aread32(hw->core, BCMA_RESET_CTL)", reset)
        self.assertIn("BCMA_IOCTL_CLK | BCMA_IOCTL_FGC", SRC_TEXT + HEADER)
        self.assertIn("BCMA_RESET_CTL_RESET", SRC_TEXT + HEADER)

    def test_all_postcondition_classifications_exist(self):
        for marker in (
            'return "IOCTL_INVALID"',
            'return "RESET_ASSERTED"',
            'return "IOCTL_INVALID_RESET_ASSERTED"',
            'return "ENABLED"',
        ):
            self.assertIn(marker, SRC_TEXT)

    def test_cp0_to_cp3_and_enable_marker_exist_in_order(self):
        # The order now lives in the pure contract; each point is observed by
        # its own ob_ucore_snapshot() call.
        positions = [
            PREP.index("p = OB_UCORE_CP0;"),
            PREP.index("ob_ucore_log_ev(ops, OB_UCORE_EV_HOST_UP"),
            PREP.index("p = OB_UCORE_CP1;"),
            PREP.index("ob_ucore_log_ev(ops, OB_UCORE_EV_ENABLE_CALLED"),
            PREP.index("p = OB_UCORE_CP2;"),
            PREP.index("ops->clockmode_fast(ops->ctx);"),
            PREP.index("p = OB_UCORE_CP3;"),
        ]
        self.assertEqual(positions, sorted(positions))
        for point in ("CP0", "CP1", "CP2", "CP3"):
            self.assertIn("OB_UCORE_%s" % point, PREP)

    def test_cp2_is_classified_before_the_clock_transition(self):
        # A2-b: a suspicious CP2 must stop before the FAST clock transition.
        snap2 = PREP.index("p = OB_UCORE_CP2;")
        stop2 = PREP.index('"cp2_suspicious"', snap2)
        clock = PREP.index("ops->clockmode_fast(ops->ctx);", snap2)
        self.assertLess(stop2, clock)

    def test_failed_core_enable_stops_before_any_snapshot(self):
        # A2-c: no diagnostic MMIO after a failed bcma_core_enable().
        enable = PREP.index("ret = ops->core_enable(ops->ctx);")
        stop = PREP.index('"core_enable_failed"', enable)
        snap2 = PREP.index("p = OB_UCORE_CP2;")
        self.assertLess(enable, stop)
        self.assertLess(stop, snap2)
        self.assertIn("if (ret) {", PREP[enable:snap2])

    def test_postcondition_guard_precedes_d11_01e0(self):
        guard = PREP.index("ob_ucore_log_ev(ops, OB_UCORE_EV_POSTCONDITION,")
        clk = PREP.index("ops->read_clk1e0(ops->ctx);")
        self.assertLess(guard, clk)
        self.assertIn("return ob_ucore_stop", PREP[guard:clk])

    def test_existing_clk1e0_sentinel_remains(self):
        # Phase 38C (D4): the read is the latching trusted accessor, and the
        # sentinel still gates every later step.
        adapter = body_of(SRC_TEXT, "static u32 ob_ucore_hw_read_clk1e0(")
        self.assertIn("ob_d11_read32_trusted", adapter)
        self.assertIn("OB_UCODE_REG_D11_CLK1E0", adapter)
        self.assertIn("ob_ucode_bootstrap_can_continue(enabled, clk)", PREP)
        self.assertIn("OB_UCORE_EV_CLK1E0_UNAVAILABLE", PREP)
        self.assertIn("D11_ACCESS_UNAVAILABLE stage=ucode_prepare_clk1e0",
                      SRC_TEXT)

    def test_diagnostic_uses_no_raw_window_or_pci_access(self):
        for name in ("ob_ucore_hw_read_ioctl(", "ob_ucore_hw_read_reset(",
                     "static int ob_ucode_prepare("):
            body = body_of(SRC_TEXT, name if name.startswith("static")
                           else "static u32 " + name)
            self.assertNotIn("pci_read_config", body)
            self.assertNotIn("ioread", body)
            self.assertNotIn("iowrite", body)
            self.assertNotIn("bcma_awrite32", body)

    def test_prepare_stays_fail_closed_on_the_state_gate(self):
        body = body_of(SRC_TEXT, "static int ob_ucode_prepare(")
        self.assertIn("hw->dev_lost", body)
        self.assertIn("OB_D11_CORE_READY", body)
        self.assertIn("return -EINVAL", body)


if __name__ == "__main__":
    unittest.main()
