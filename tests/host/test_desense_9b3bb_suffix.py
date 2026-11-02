import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / "analysis/tools/generate_desense_9b3bb_suffix.py"
IMAGE = Path("/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/vendor/wlc_hybrid.o_shipped")

spec = importlib.util.spec_from_file_location("desense_suffix", GEN)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class DesenseSuffixTests(unittest.TestCase):
    def test_two_chain_symbolic_suffix_and_corrections(self):
        doc = mod.build(IMAGE)
        self.assertTrue(doc["complete"])
        self.assertTrue(doc["event_vector_is_contiguous"])
        self.assertEqual(doc["event_count"], 24)
        self.assertEqual([e["seq"] for e in doc["events"]], list(range(1, 25)))
        chain0 = [e for e in doc["events"] if e["chain"] == 0]
        chain1 = [e for e in doc["events"] if e["chain"] == 1]
        self.assertEqual(len(chain0), 11)
        self.assertEqual(len(chain1), 11)
        self.assertEqual(chain0[0]["register_or_table"], {"table_id": 0x44, "offset": 0, "count": 2, "width_bits": 8})
        self.assertEqual(chain1[0]["register_or_table"], {"table_id": 0x64, "offset": 0, "count": 2, "width_bits": 8})
        self.assertEqual(chain0[0]["value"], ["nested+0x3e0", "nested+0x3e0"])
        self.assertEqual(chain1[0]["value"], ["nested+0x3e3", "nested+0x3e3"])
        self.assertEqual(chain0[1]["register_or_table"]["register"], "0x173b")
        self.assertEqual(chain0[2]["register_or_table"]["register"], "0x1726")
        read_destinations = {e["site"]: e.get("destination") for e in chain0 if e["operation"] == "TABLE_READ"}
        self.assertEqual(read_destinations["0x9b63f"], "nested+0x4a6+0x78*chain")
        self.assertEqual(read_destinations["0x9b665"], "nested+0x4c4+0x78*chain")
        self.assertEqual(read_destinations["0x9b687"], "nested+0x492+0x78*chain")
        self.assertEqual(read_destinations["0x9b6a9"], "nested+0x49c+0x78*chain")
        self.assertEqual(read_destinations["0x9b6cf"], "nested+0x4ce+0x78*chain")
        self.assertEqual(read_destinations["0x9b6f5"], "nested+0x4d8+0x78*chain")
        self.assertEqual(doc["events"][-2]["site"], "0x9b719")
        self.assertEqual(doc["events"][-1]["site"], "0x9b73b")
        branch = next(b for b in doc["branch_map"] if b["site"] == "0x9b2f5/0x9b344")
        self.assertIn("skips", branch["effect"])
        self.assertEqual(doc["falsification"][0]["final_status"], "DISPROVEN")


if __name__ == "__main__":
    unittest.main()
