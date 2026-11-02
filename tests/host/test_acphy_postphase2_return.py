#!/usr/bin/env python3
"""Offline contracts for the post-phase-2 sub_9591e return endpoint."""
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


class Postphase2ReturnContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = (REPO / "src/ob_core.c").read_text(encoding="utf-8")
        cls.core_h = (REPO / "src/ob_core.h").read_text(encoding="utf-8")
        cls.ucode = (REPO / "src/ob_ucode.h").read_text(encoding="utf-8")
        cls.radio = (REPO / "src/ob_radio.c").read_text(encoding="utf-8")
        cls.suffix = (REPO / "src/ob_radio_postphase2.h").read_text(
            encoding="utf-8"
        )
        cls.postbmac = (REPO / "src/ob_radio_postbmac.h").read_text(
            encoding="utf-8"
        )

    def test_mode_is_selected_and_preserves_device_state(self):
        self.assertIn("acphy_class0_postphase2_sub9591e_return_test_only", self.core)
        self.assertIn("OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST", self.ucode)
        self.assertIn("OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST", self.core)
        self.assertIn("acphy_class0_postphase2_sub9591e_return_test_only", self.core_h)
        self.assertRegex(
            self.ucode,
            r"ob_isolated_mode_preserves_state[\s\S]*?"
            r"OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST",
        )

    def test_suffix_is_gated_by_same_call_third_poll_success(self):
        run = re.search(
            r"static int ob_radio_class0_post0413_run\([\s\S]*?\n}\n",
            self.radio,
        )
        self.assertIsNotNone(run)
        body = run.group(0)
        phase2 = body.index("ob_radio_postphase1_run(")
        phase2_failure = body.index("if (ret) {", phase2)
        suffix = body.index("ob_radio_postphase2_run(", phase2)
        self.assertLess(phase2, phase2_failure)
        self.assertLess(phase2_failure, suffix)
        self.assertIn("phase2.success", body[suffix:])

    def test_old_phase2_endpoint_keeps_its_stop(self):
        self.assertRegex(
            self.radio,
            r"if \(!run_return_suffix\) \{[\s\S]*?"
            r"ACPHY_CLASS0_POSTPHASE1_STOP",
        )
        self.assertIn("ACPHY_CLASS0_POSTPHASE2_RETURN", self.radio)
        self.assertIn("ACPHY_CLASS0_POSTPHASE2_STOP", self.radio)
        self.assertIn("return ob_radio_class0_post0413_run(hw, false, false, false);",
                      self.radio)
        self.assertIn("return ob_radio_class0_post0413_run(hw, true, false, false);",
                      self.radio)
        self.assertIn("return ob_radio_class0_post0413_run(hw, true, true, false);",
                      self.radio[self.radio.index(
                          "int ob_radio_class0_initialize_to_sub9591e("):])
        self.assertIn("return ob_radio_class0_initialize_to_sub9591e(hw);",
                      self.radio)
        self.assertIn("return ob_radio_class0_post0413_run(hw, true, true, true);",
                      self.radio)

    def test_pure_suffix_has_exact_operations_and_no_timing_or_poll(self):
        for reg in ("0x0411u", "0x0416u", "0x0171u", "0x0371u", "0x0410u", "0x08eau"):
            self.assertIn(reg, self.suffix)
        self.assertIn("(old & (u16)~value) | (value & mask)", self.suffix)
        self.assertIn("(sample & 0x03e0u) >> 5", self.suffix)
        self.assertNotRegex(self.suffix, r"delay_us|attempts|0x0413|poll_fn")
        self.assertNotIn("pi+0x33b", self.core_h)

    def test_endpoint_is_test_mode_only_and_preserves_state_on_remove(self):
        self.assertIn("ob_radio_class0_postphase2_return_test(hw)", self.core)
        self.assertEqual(self.core.count("ob_radio_class0_postphase2_return_test(hw)"),
                         1)
        branch = self.core.index(
            "if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST ||")
        branch_end = self.core.index(
            "if (mode == OB_ISOLATED_RADIO2069_REV4_PROBE)", branch)
        self.assertIn("ob_radio_class0_postphase2_return_test(hw)",
                      self.core[branch:branch_end])
        remove = self.core[self.core.index("void ob_remove("):]
        self.assertRegex(
            remove,
            r"if \(hw->acphy_class0_postphase2_sub9591e_return_test_only\) \{[\s\S]*?"
            r"radio state preserved",
        )

    def test_owner_emits_only_fail_stop_on_suffix_error(self):
        runner = re.search(
            r"static int ob_radio_class0_post0413_run\([\s\S]*?\n}\n",
            self.radio,
        )
        self.assertIsNotNone(runner)
        text = runner.group(0)
        suffix = text.index("ob_radio_postphase2_run(")
        self.assertIn("if (ret)\n\t\treturn ret;", text[suffix:])
        marker_sink = re.search(
            r"static void ob_radio_postphase2_marker\([\s\S]*?\n}\n",
            self.radio,
        )
        self.assertIsNotNone(marker_sink)
        for marker in ("RETURN", "PASS", "FAIL", "STOP"):
            self.assertIn(f"OB_RADIO_POSTPHASE2_MARK_{marker}",
                          marker_sink.group(0))
        sequence = re.search(
            r"static inline int ob_radio_postphase2_run\([\s\S]*?\n}\n",
            self.suffix,
        )
        self.assertIsNotNone(sequence)
        success = sequence.group(0)
        self.assertLess(success.index("OB_RADIO_POSTPHASE2_MARK_RETURN"),
                        success.index("OB_RADIO_POSTPHASE2_MARK_PASS"))
        self.assertLess(success.index("OB_RADIO_POSTPHASE2_MARK_PASS"),
                        success.index("OB_RADIO_POSTPHASE2_MARK_STOP"))
        failure = self.suffix[self.suffix.index("ob_radio_postphase2_fail("):]
        self.assertLess(failure.index("OB_RADIO_POSTPHASE2_MARK_FAIL"),
                        failure.index("OB_RADIO_POSTPHASE2_MARK_STOP"))

    def test_new_suffix_uses_same_call_results_and_preserves_old_terminal(self):
        runner = re.search(
            r"static int ob_radio_class0_post0413_run\([\s\S]*?\n}\n",
            self.radio,
        )
        self.assertIsNotNone(runner)
        body = runner.group(0)
        self.assertIn("run_postbmac", body)
        self.assertIn("phase2.success", body)
        self.assertIn("suffix.success", body)
        self.assertIn("suppress_success_terminal = run_postbmac", body)
        self.assertIn("if (ret)\n\t\treturn ret;", body)
        self.assertIn("if (!run_postbmac)\n\t\treturn 0;", body)
        self.assertNotIn("ob_radio_postbmac_run", body[:body.index("ob_radio_postphase2_run")])
        marker_sink = re.search(
            r"static void ob_radio_postphase2_marker\([\s\S]*?\n}\n",
            self.radio,
        )
        self.assertIsNotNone(marker_sink)
        sink = marker_sink.group(0)
        self.assertIn("OB_RADIO_POSTPHASE2_MARK_FAIL", sink)
        self.assertIn("OB_RADIO_POSTPHASE2_MARK_STOP", sink)
        self.assertIn("OB_RADIO_POSTPHASE2_MARK_PASS", sink)

    def test_postbmac_phy_prefix_is_two_ops_five_callbacks(self):
        branch = self.postbmac.index(
            "/* The accepted target takes ja @0xb01f0 and jne @0xb01fd"
        )
        phy_prefix = self.postbmac[:branch]
        self.assertIn("ob_phy_window_rmw16_once", phy_prefix)
        self.assertIn("0x01b0u", phy_prefix)
        self.assertIn("0x8000u, 0x8000u", phy_prefix)
        self.assertIn("ob_phy_window_read16_once", phy_prefix)
        self.assertIn("0x0000u", phy_prefix)
        self.assertIn("result->sample & 0x000fu", phy_prefix)
        self.assertIn("result->low4 > 1u", phy_prefix)
        self.assertNotRegex(phy_prefix, r"delay|poll|ja\s|ob_phy_rmw16_hw")
        self.assertIn("window_callbacks", phy_prefix)
        self.assertIn("historical_branch_executed", self.postbmac)


if __name__ == "__main__":
    unittest.main(verbosity=2)
