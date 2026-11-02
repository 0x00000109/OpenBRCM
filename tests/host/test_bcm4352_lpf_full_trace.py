import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import compare_bcm4352_lpf_trace as trace


BINARY = ROOT / "tests/host/ob_phy_test"


class CompleteLpfTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-C", "tests/host", "ob_phy_test"], cwd=ROOT, check=True)

    def get_trace(self):
        raw = subprocess.run([str(BINARY), "--emit-lpf-trace"], cwd=ROOT,
                             check=True, capture_output=True, text=True).stdout
        return raw, json.loads(raw)

    def test_full_callback_trace_matches_independently_pinned_vector(self):
        _, actual = self.get_trace()
        self.assertIsNone(trace.compare_trace(actual))
        self.assertEqual(actual["callback_count"], 3736)
        self.assertEqual(actual["saved_019e"], 0xa540)

    def test_repeated_synthetic_runs_are_byte_deterministic(self):
        first_raw, _ = self.get_trace()
        second_raw, _ = self.get_trace()
        self.assertEqual(first_raw, second_raw)

    def test_first_difference_report_identifies_callback_and_descriptor(self):
        _, actual = self.get_trace()
        actual["events"][23]["value"] ^= 1
        mismatch = trace.compare_trace(actual)
        self.assertEqual(set(mismatch), {
            "EVENT_INDEX", "EXPECTED_SOURCE_SITE", "EXPECTED_OPERATION",
            "ACTUAL_OPERATION", "REGISTER", "EXPECTED_VALUE", "ACTUAL_VALUE",
            "DESCRIPTOR_ORDINAL", "FIRST_DIVERGENCE_REASON",
        })
        self.assertEqual(mismatch["EVENT_INDEX"], 24)
        self.assertEqual(mismatch["EXPECTED_SOURCE_SITE"],
                         "wlc_phy_write_table_ext@0xb6a34/0xb6a64/0xb6a9f/0xb6aee/0xb6b0f")
        self.assertEqual(mismatch["EXPECTED_OPERATION"], "WRITE")
        self.assertEqual(mismatch["ACTUAL_OPERATION"], "W")
        self.assertEqual(mismatch["DESCRIPTOR_ORDINAL"], 0)
        self.assertEqual(mismatch["REGISTER"], "0x000f")
        self.assertEqual(mismatch["EXPECTED_VALUE"], "0x0000")
        self.assertEqual(mismatch["ACTUAL_VALUE"], "0x0001")
        self.assertEqual(mismatch["FIRST_DIVERGENCE_REASON"],
                         "callback kind, register, or value differs from accepted-vector expansion")

    def test_generator_is_deterministic_and_probe_unreachable(self):
        subprocess.run(["python3", "scripts/generate_bcm4352_lpf_contract.py", "--check"],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        source_refs = []
        for path in (ROOT / "src").glob("*.[ch]"):
            if "ob_phy_acphy_lpf_complete_test_only" in path.read_text():
                source_refs.append(path.name)
        # The post-LPF composition entry is test-only and composes the already
        # isolated LPF runner with a fake backend.  It must remain the sole
        # additional source reference; no production source may dispatch it.
        self.assertEqual(sorted(source_refs), [
            "ob_phy_lpf.c", "ob_phy_lpf.h", "ob_phy_post_lpf.c",
        ])
        post_lpf = (ROOT / "src/ob_phy_post_lpf.c").read_text()
        self.assertEqual(post_lpf.count("ob_phy_acphy_lpf_complete_test_only("), 1)
        self.assertIn("int ob_phy_acphy_post_lpf_compose_fixture_test_only(", post_lpf)
        a0be4 = (ROOT / "src/ob_phy_a0be4.c").read_text()
        self.assertEqual(a0be4.count("ob_phy_acphy_post_lpf_compose_fixture_test_only("), 1)
        self.assertIn("int ob_phy_acphy_initial_fixture_through_a0be4_test_only(", a0be4)
        isolated_composition_sources = {"ob_phy_post_lpf.c", "ob_phy_post_lpf.h",
                                        "ob_phy_a0be4.c", "ob_phy_a0be4.h",
                                        "ob_phy_a6b0f.c", "ob_phy_a6b0f.h"}
        a6b0f = (ROOT / "src/ob_phy_a6b0f.c").read_text()
        self.assertEqual(a6b0f.count(
            "ob_phy_acphy_initial_fixture_through_a0be4_test_only("), 1)
        self.assertIn("int ob_phy_acphy_initial_fixture_through_a6b0f_test_only(",
                      a6b0f)
        non_isolated_sources = "\n".join(
            path.read_text() for path in (ROOT / "src").glob("*.[ch]")
            if path.name not in isolated_composition_sources
        )
        self.assertNotIn("ob_phy_acphy_post_lpf_compose_fixture_test_only(",
                         non_isolated_sources)
        self.assertNotIn("ob_phy_acphy_initial_fixture_through_a0be4_test_only(",
                         non_isolated_sources)


if __name__ == "__main__":
    unittest.main()
