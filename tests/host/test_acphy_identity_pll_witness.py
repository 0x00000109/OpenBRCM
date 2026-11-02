#!/usr/bin/env python3
"""Structural acceptance for the L2l PHY-PLL pre-state witness.

The address bisection (L2..L2j) located the ~201.7 ms band at 0x3d8..0x3fb and
L2k showed the failure goes function-wide. Neither observed the clock state
before the first stalling access. L2l reads the two clock registers that sit
BELOW the band and then enters the island:

    P1  0x01e0  clk_ctl_st     32-bit  guarded
    P2  0x01e8  d11_phypllctl  32-bit  guarded
    P3  0x03d8  radioregaddr   16-bit  guarded, first word of the band
    P4  0x03da  radioregdata   16-bit  only if P3 completed

The witness is only sound if:

  * it is a modifier of L2, refused without acphy_identity_timing_only, and
    refused in combination with any other L2 probe modifier (it must be the
    first access after the eight-read block and it replaces the legacy 0x3e0
    read);
  * both pre-state reads are 32-bit and guarded (dev_lost is still 0 there, so
    the guard is a pass-through and the read is a real bus access);
  * the island entry is 0x03d8, falling through to the L2j-measured 0x03da
    only when 0x03d8 completed, and a stall latches dev_lost exactly like L2j;
  * a missing baseline stops before the island and marks the run VOID;
  * the decode uses the upstream Broadcom bit values from brcmsmac d11.h;
  * no write symbol is referenced by the mode.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")

L2_MODIFIERS = (
    "acphy_identity_3e2_first",
    "acphy_identity_span_probe",
    "acphy_identity_control_reprobe",
    "acphy_identity_control16_reprobe",
    "acphy_identity_upper_probe",
    "acphy_identity_edge_probe",
    "acphy_identity_wide_probe",
    "acphy_identity_edge2_probe",
    "acphy_identity_lower_probe",
    "acphy_identity_wrapper_witness",
)


def helper(name: str) -> str:
    start = SI.index(f"static void {name}(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


def read16_helper() -> str:
    start = SI.index("static enum ob_l2k_class ob_si_l2l_read16(struct ob_hw *hw")
    return SI[start:SI.index("\n}\n", start)]


def l2_block() -> str:
    start = SI.index("if (hw->acphy_identity_timing_only) {")
    return SI[start:SI.index("\n\t}\n", start)]


class AcphyIdentityPllWitnessTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_pll_witness;", CORE)
        self.assertIn("module_param(acphy_identity_pll_witness, bool, 0444);", CORE)
        self.assertIn(
            "if (acphy_identity_pll_witness && !acphy_identity_timing_only)",
            CORE,
        )
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_pll_witness", select)

    def test_witness_is_exclusive_with_every_other_l2_modifier(self):
        start = CORE.index("if (acphy_identity_pll_witness &&\n")
        conflict = CORE[start:CORE.index("return -EINVAL;", start)]
        self.assertIn("exclusive", conflict)
        for name in L2_MODIFIERS:
            self.assertIn(name, conflict, f"{name} is not covered by the exclusion")
        # The exclusion must be symmetric: L2k's own list covers L2l too.
        wrap = CORE.index("if (acphy_identity_wrapper_witness &&\n")
        wrap_conflict = CORE[wrap:CORE.index("return -EINVAL;", wrap)]
        self.assertIn("acphy_identity_pll_witness", wrap_conflict)

    def test_stage_order_offsets_and_widths(self):
        body = helper("ob_si_d11_identity_pll_witness")
        order = [
            body.index('"P1", OB_L2K_WIN_D11, 0x01e0u'),
            body.index('"P2", OB_L2K_WIN_D11, 0x01e8u'),
            body.index('"P3", 0x03d8u'),
            body.index('"P4", 0x03dau'),
        ]
        self.assertEqual(order, sorted(order), "stages are out of order")
        # P1/P2 are 32-bit and guarded (dev_lost is still 0: pass-through).
        self.assertIn('"P1", OB_L2K_WIN_D11, 0x01e0u, true', body)
        self.assertIn('"P2", OB_L2K_WIN_D11, 0x01e8u, true', body)
        # P3 is the first word of the measured band, 16-bit, same latch.
        self.assertIn('"P3", 0x03d8u', body)
        self.assertIn('"P4", 0x03dau', body)
        self.assertIn("hw->dev_lost = true;", read16_helper())
        self.assertIn("ob_d11_read16(hw, off)", read16_helper())

    def test_p4_is_issued_only_when_p3_completed(self):
        body = helper("ob_si_d11_identity_pll_witness")
        p3_stall = body.index("if (p3 == OB_L2K_STALL)")
        p4 = body.index('"P4", 0x03dau')
        self.assertLess(p3_stall, p4)
        # The fall-through lives in the else branch of the P3 classification.
        between = body[p3_stall:p4]
        self.assertIn("else {", between)

    def test_void_branch_precedes_the_island(self):
        body = helper("ob_si_d11_identity_pll_witness")
        self.assertIn("hw->acphy_identity_witness_void = true;", body)
        self.assertIn('verdict = "void_no_baseline";', body)
        self.assertLess(
            body.index("void_no_baseline"), body.index('"P3", 0x03d8u')
        )

    def test_decode_uses_upstream_bit_values(self):
        for const, value in (
            ("OB_L2L_CCS_REQ_HT", "0x00000010u"),
            ("OB_L2L_CCS_REQ_D11PLL", "0x00000100u"),
            ("OB_L2L_CCS_REQ_PHYPLL", "0x00000200u"),
            ("OB_L2L_CCS_AVAIL_HT", "0x00020000u"),
            ("OB_L2L_CCS_AVAIL_D11PLL", "0x01000000u"),
            ("OB_L2L_CCS_AVAIL_PHYPLL", "0x02000000u"),
        ):
            self.assertIn(f"#define {const}", SI)
            self.assertIn(f"#define {const}\t{value}", SI)
        # Availability wins over request; request without avail is its own state.
        name = SI[SI.index("static const char *ob_si_l2l_phypll_name(u32 ccs)"):]
        name = name[:name.index("\n}\n")]
        self.assertLess(
            name.index("OB_L2L_CCS_AVAIL_PHYPLL"), name.index("OB_L2L_CCS_REQ_PHYPLL")
        )

    def test_verdict_vocabulary(self):
        body = helper("ob_si_d11_identity_pll_witness")
        for verdict in (
            "phypll_avail_at_island",
            "phypll_requested_not_available_at_island",
            "phypll_off_at_island",
            "no_island_reproduction",
            "aborted_slow_read",
            "void_no_baseline",
        ):
            self.assertIn(verdict, SI)
        self.assertIn("ACPHY_IDENTITY_PLLWITNESS_SUMMARY:", body)
        self.assertIn("writes_issued=0", body)
        self.assertIn("island_reproduced=%u", body)
        self.assertIn("p3_03d8=%s p4_03da=%s", body)

    def test_no_write_symbol_is_referenced_by_the_mode(self):
        for body in (
            helper("ob_si_d11_identity_pll_witness"),
            read16_helper(),
        ):
            for forbidden in (
                "bcma_write",
                "bcma_awrite",
                "ob_d11_write",
                "ob_axi_write",
                "pci_write",
                "iowrite",
                "awrite",
            ):
                self.assertNotIn(forbidden, body)

    def test_witness_is_first_and_replaces_the_legacy_0x3e0_read(self):
        block = l2_block()
        witness = block.index("ob_si_d11_identity_pll_witness(hw)")
        for later in (
            "ob_si_d11_identity_wrapper_witness(hw)",
            "ob_si_d11_identity_lower_probe(hw)",
            "ob_si_d11_identity_edge2_probe(hw)",
            "ob_si_d11_identity_wide_probe(hw)",
            "ob_si_d11_identity_control16_reprobe(hw)",
            "ob_si_d11_identity_control_reprobe(hw)",
            "ob_si_d11_identity_span_probe(hw)",
            "ob_si_d11_identity_3e2_probe(hw)",
        ):
            self.assertLess(witness, block.index(later))
        self.assertIn("if (!hw->acphy_identity_wrapper_witness &&", block)
        self.assertIn("!hw->acphy_identity_pll_witness)", block)
        self.assertLess(witness, block.index("ob_si_d11_identity_timing(hw);"))
        # A stall must end the probe before any later stage.
        self.assertIn("hw->dev_lost", block)
        self.assertIn("hw->acphy_identity_early_stop = true;", block)

    def test_state_flags_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_pll_witness;", CORE_H)
        # Shared fail-stop state is reused, not duplicated.
        self.assertIn("bool\t\t\tacphy_identity_witness_failed;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_witness_void;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_early_stop;", CORE_H)

    def test_harness_enables_the_witness_only_on_request(self):
        self.assertIn('"${OB_L2_PLL_WITNESS:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_pll_witness=1"', HARNESS)
        self.assertIn("acphy_identity_pllwitness_markers_complete", HARNESS)
        self.assertIn("ACPHY_IDENTITY_PLLWITNESS_SUMMARY", HARNESS)
        self.assertIn("0x3e0 NOT read", HARNESS)
        # Both load paths (dry run and real insmod) must carry the parameter.
        self.assertGreaterEqual(HARNESS.count("acphy_identity_pll_witness=1"), 2)
        self.assertIn("OB_L2_PLL_WITNESS", HARNESS)


if __name__ == "__main__":
    unittest.main()
