import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / "analysis/tools/generate_a04c2_event_vector.py"
IMAGE = Path("/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/vendor/wlc_hybrid.o_shipped")


class A04C2EventVectorTests(unittest.TestCase):
    def test_selected_vector_has_ordered_operations_and_23_payloads(self):
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "a04c2.json"
            subprocess.run([sys.executable, str(GEN), "--image", str(IMAGE), "--json", str(out)],
                           cwd=ROOT, check=True, capture_output=True, text=True)
            doc = json.loads(out.read_text())
        events = doc["events"]
        self.assertEqual(doc["complete"], True)
        self.assertEqual(doc["event_vector_is_contiguous"], True)
        self.assertEqual(doc["event_count"], 3734)
        self.assertEqual([e["seq"] for e in events], list(range(1, len(events) + 1)))
        self.assertEqual(events[0]["site"], "0xa04e9")
        self.assertEqual(events[0]["register"], "0x0410")
        table_events = [e for e in events if "table_transaction" in e]
        descriptor_ids = sorted({e["table_transaction"]["descriptor_index"] for e in table_events})
        self.assertEqual(len(descriptor_ids), 23)
        first_table = next(e for e in table_events if e["table_transaction"]["descriptor_index"] == 0)
        self.assertEqual(first_table["table_transaction"]["payload_symbol"], "acphy_mcs_tbl_rev0")
        self.assertEqual(first_table["table_transaction"]["table_id"], 1)
        self.assertEqual(first_table["table_transaction"]["table_offset"], 0)
        self.assertEqual(first_table["table_transaction"]["element_count"], 128)
        self.assertEqual(first_table["table_transaction"]["element_width_bits"], 16)
        self.assertEqual(events[-1]["register"], "0x1645")
        self.assertEqual(events[-1]["value"], "0x025c")
        self.assertEqual(len(doc["direct_call_map"]), 30)
        self.assertEqual([int(x["site"], 16) for x in doc["direct_call_map"]], sorted(int(x["site"], 16) for x in doc["direct_call_map"]))
        self.assertNotIn("0xa0b9e", [e["site"] for e in events])
        self.assertIn("0xa0510 rev-{5,2,6} write", doc["selected_skips"])
        self.assertIn("0xa077e rev3-only write", doc["selected_skips"])
        self.assertIn("0xa0b9e revision-specific 0x164c write", doc["selected_skips"])
        self.assertIn("0xa0bd0 nested+0x348-gated wlapi_bmac_mhf", doc["selected_skips"])


if __name__ == "__main__":
    unittest.main()
