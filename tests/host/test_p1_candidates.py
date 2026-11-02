#!/usr/bin/env python3
"""Structural acceptance for the P1 outcome: bit 0x4 promoted, SHM witness kept.

The P0 like-for-like trace (p0_attach_trace01/exec-20261028T001326Z) measured
two differences in the state our attach builds immediately before the 16-bit
D11 0x3e0 read:

  P1-A  the vendor's wrapper IOCTL (+0x408) ends at 0x5 (bit 2 persistent,
        "7 -> 5"); our bcma_core_enable(core, 0) ended at 0x1.
  P1-B  the vendor proves its D11 SHM RAM 0.4 ms before the read (OBJADDR
        0x00010000, patterns 0xaa5555aa / 0x55aaaa55 / 0x007f0340 through
        0x164/0x166 with readback); our path never touched 0x160/0x164/0x166.

Two windows settled which one matters:

  exec-20261028T004543Z  both candidates -> P1_D_READ_COMPLETED
  exec-20261028T005446Z  P1-A alone      -> P1_A_ONLY_LIVE

So P1-A is no longer a switch — it is the default attach prefix — and P1-B
stays an opt-in modifier. The promoted contract is:

  * bcma_core_enable() is called with flags 0x4 unconditionally, in the same
    place the vendor performs it, and there is no p1_ioctl4 param or hw field
    left to turn it off;
  * the readback is a HARD gate, not a log line: wrapper 0x408 must equal
    0x5 and anything else fails closed with OB_ATTACH_FAIL
    stage=WRAPPER_IOCTL_BIT4 before any D11 functional access, so a
    regression can never reach the 0x3e0 read;
  * the prefix still issues no D11 read/write of its own;
  * P1-B is still a module param, 0444, default off, refused without
    acphy_pre_radio_test_only=1, and not part of the mode selector; it keeps
    its own fail-stop before the 0x3e0 read.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
RADIO = (ROOT / "src/ob_radio.c").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
SI_H = (ROOT / "src/ob_si.h").read_text(encoding="utf-8")
LIFECYCLE = (ROOT / "src/ob_lifecycle.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


def pre_id() -> str:
    start = RADIO.index("static int ob_radio_vendor_pre_id(struct ob_hw *hw)")
    return RADIO[start:RADIO.index("\n}\n", start)]


def shm_helper() -> str:
    start = SI.index("int ob_si_p1_shm_roundtrip(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


def identity() -> str:
    start = LIFECYCLE.index("int ob_acphy_attach_identity(struct ob_hw *hw)")
    return LIFECYCLE[start:LIFECYCLE.index("\n}\n", start)]


def prepare() -> str:
    start = RADIO.index("int ob_radio_prepare_for_identity(struct ob_hw *hw)")
    return RADIO[start:RADIO.index("\n}\n", start)]


class PromotedIoctl4Tests(unittest.TestCase):
    def test_enable_carries_flags_0x4_unconditionally(self):
        body = pre_id()
        self.assertIn("bcma_core_enable(hw->core, 0x4u);", body)
        self.assertEqual(RADIO.count("bcma_core_enable(hw->core, 0x4u)"), 1)
        # No selector, no fallback to flags=0 anywhere in the tree.
        self.assertNotIn("? 0x4u : 0u", RADIO)
        self.assertNotIn("bcma_core_enable(hw->core, 0)", RADIO)

    def test_no_p1_ioctl4_switch_remains(self):
        for text, where in ((CORE, "ob_core.c"), (CORE_H, "ob_core.h"),
                            (RADIO, "ob_radio.c")):
            self.assertNotIn("p1_ioctl4", text, where)

    def test_readback_gate_is_unconditional_and_hard(self):
        body = pre_id()
        self.assertIn("OB_ATTACH_CP1_IOCTL4 wrapper_0x408=0x%08x", body)
        self.assertIn("expect=0x00000005", body)
        gate = body[body.index("if (ioctl != 0x5u) {"):]
        gate = gate[:gate.index("\n\t}")]
        self.assertIn("OB_ATTACH_FAIL stage=WRAPPER_IOCTL_BIT4 ret=-5", gate)
        self.assertIn("return -EIO;", gate)
        self.assertIn("the 0x3e0 read is not issued", gate)

    def test_gate_is_not_guarded_by_any_selection(self):
        body = pre_id()
        block = body[body.index("OB_ATTACH_CP1_IOCTL4"):]
        block = block[:block.index("return 0;")]
        for selector in ("if (hw->p1_", "if (p1_", "if (mode =="):
            self.assertNotIn(selector, block)

    def test_gate_precedes_the_only_success_return(self):
        body = pre_id()
        self.assertLess(body.index("OB_ATTACH_CP1_IOCTL4"), body.rindex("return 0;"))

    def test_witness_follows_the_cp1_line(self):
        body = pre_id()
        self.assertLess(body.index("OB_ATTACH_CP1_RESET_READY"),
                        body.index("OB_ATTACH_CP1_IOCTL4"))

    def test_prefix_still_issues_no_d11_functional_access(self):
        body = pre_id()
        for accessor in ("ob_d11_read", "ob_d11_write"):
            self.assertNotIn(accessor, body)

    def test_gate_runs_before_the_identity_read(self):
        # ob_radio_prepare_for_identity() -> ob_radio_vendor_pre_id() (the gate)
        # is called from the lifecycle before the 0x3e0 read.
        self.assertIn("ob_radio_vendor_pre_id(hw)", prepare())
        body = identity()
        self.assertLess(body.index("ob_radio_prepare_for_identity(hw)"),
                        body.index("phyver = ob_d11_read16(hw, 0x03e0u);"))

    def test_nothing_re_enables_the_core_between_the_gate_and_the_read(self):
        # Any bcma_core_enable(core, 0) after the prefix would clear the
        # persistent bit 0x4 again; the measured state must survive the gap.
        body = identity()
        block = body[body.index("ob_radio_prepare_for_identity(hw)"):
                     body.index("phyver = ob_d11_read16(hw, 0x03e0u);")]
        self.assertNotIn("bcma_core_enable", block)
        self.assertNotIn("bcma_core_disable", block)


class P1ShmRoundtripTests(unittest.TestCase):
    def test_shm_is_the_only_p1_param_and_is_0444(self):
        self.assertIn("static bool p1_shm_roundtrip;", CORE)
        self.assertIn("module_param(p1_shm_roundtrip, bool, 0444);", CORE)
        self.assertIn("MODULE_PARM_DESC(p1_shm_roundtrip,", CORE)

    def test_shm_requires_the_attach_prefix_mode(self):
        guard = CORE[CORE.index("if (p1_shm_roundtrip && !acphy_pre_radio_test_only)"):]
        guard = guard[:guard.index("}")]
        self.assertIn("return -EINVAL;", guard)
        self.assertIn("rejected before any hardware access", guard)

    def test_shm_is_not_a_mode(self):
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("p1_shm_roundtrip", select)

    def test_hw_field_is_set_only_for_the_attach_mode(self):
        self.assertIn("bool\t\t\tp1_shm_roundtrip;", CORE_H)
        block = CORE[CORE.index(
            "if (mode == OB_ISOLATED_ACPHY_PRE_RADIO_TEST)\n\t\thw->p1_shm_roundtrip"):]
        block = block[:block.index("\n\tif (", 10)]
        self.assertIn("hw->p1_shm_roundtrip = p1_shm_roundtrip;", block)

    def test_helper_is_declared(self):
        self.assertIn("int  ob_si_p1_shm_roundtrip(struct ob_hw *hw);", SI_H)

    def test_exact_vendor_patterns(self):
        body = shm_helper()
        self.assertIn("0xaa5555aau, 0x55aaaa55u, 0x007f0340u", body)
        self.assertIn("OB_UCODE_OBJADDR_SHM_SEL", body)
        self.assertIn("OB_UCODE_REG_OBJADDR", body)
        self.assertIn("OB_UCODE_REG_OBJDATA", body)

    def test_write_and_readback_order(self):
        body = shm_helper()
        order = [
            body.index("ob_d11_write32(hw, OB_UCODE_REG_OBJADDR, addr)"),
            body.index("ob_d11_write16(hw, OB_UCODE_REG_OBJDATA, lo)"),
            body.index("ob_d11_write16(hw, OB_UCODE_REG_OBJDATA + 0x2u, hi)"),
            body.index("ob_d11_read16(hw, OB_UCODE_REG_OBJDATA)"),
            body.index("ob_d11_read16(hw, OB_UCODE_REG_OBJDATA + 0x2u)"),
        ]
        self.assertEqual(order, sorted(order))

    def test_dev_loss_is_checked_after_every_write(self):
        body = shm_helper()
        self.assertEqual(body.count("if (hw->dev_lost)\n\t\t\tgoto lost;"), 4)

    def test_mismatch_and_latch_stop_the_caller(self):
        body = shm_helper()
        mismatch = body[body.index("if (got_lo != lo || got_hi != hi) {"):]
        mismatch = mismatch[:mismatch.index("\n\t\t}")]
        self.assertIn("P1_FAIL_STOP reason=shm_roundtrip", mismatch)
        self.assertIn("return -EIO;", mismatch)
        lost = body[body.index("lost:"):]
        self.assertIn("P1_FAIL_STOP reason=shm_roundtrip", lost)
        self.assertIn("return -EIO;", lost)

    def test_each_stage_logs_its_readback(self):
        body = shm_helper()
        self.assertIn("P1_WITNESS param=shm stage=%d", body)
        self.assertIn("verdict=%s", body)


class P1LifecyclePlacementTests(unittest.TestCase):
    def test_probe_runs_after_cp2_and_before_the_read(self):
        body = identity()
        cp2 = body.index("OB_ATTACH_CP2_PHY_PREREQS_READY")
        probe = body.index("ob_si_p1_shm_roundtrip(hw)")
        read = body.index("phyver = ob_d11_read16(hw, 0x03e0u);")
        self.assertLess(cp2, probe)
        self.assertLess(probe, read)

    def test_failure_returns_without_the_read(self):
        body = identity()
        block = body[body.index("if (hw->p1_shm_roundtrip) {"):]
        block = block[:block.index("\n\t}")]
        self.assertIn("ret = ob_si_p1_shm_roundtrip(hw);", block)
        self.assertIn("if (ret)", block)
        self.assertIn("return ret;", block)
        self.assertNotIn("0x03e0u", block)

    def test_default_path_is_guarded(self):
        body = identity()
        self.assertEqual(body.count("if (hw->p1_shm_roundtrip) {"), 1)
        block = body[body.index("if (hw->p1_shm_roundtrip) {"):]
        self.assertIn("P1_WITNESS param=shm begin wrapper_0x408=", block)

    def test_harness_does_not_need_a_new_fault_marker(self):
        # The new lines are additive evidence; they must not look like a fault.
        for word in ("P1_WITNESS", "P1_FAIL_STOP", "OB_ATTACH_CP1_IOCTL4",
                     "WRAPPER_IOCTL_BIT4"):
            self.assertNotIn(word, HARNESS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
