import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).resolve().parents[2] / "analysis/tools/generate_precal_contract.py"
spec = importlib.util.spec_from_file_location("precal_contract", MODULE_PATH)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PrecalContractGeneratorTests(unittest.TestCase):
    def test_tx_lpf_expands_historical_chain_slot_order(self):
        ops = module.tx_lpf_events()
        self.assertEqual(len(ops), 72)
        self.assertEqual([(o["operation"], o["offset"]) for o in ops[:4]], [
            ("TABLE_READ", "0x142"), ("TABLE_READ", "0x362"),
            ("TABLE_WRITE", "0x142"), ("TABLE_WRITE", "0x362"),
        ])
        self.assertEqual(ops[-1]["offset"], "0x37a")
        self.assertEqual(ops[-1]["value"], "0x0101")

    def test_dacbuf_expands_offsets_and_cap_packing(self):
        ops = module.dacbuf_events()
        self.assertEqual(len(ops), 36)
        self.assertEqual(ops[0]["offset"], "0x3fb")
        self.assertIsNone(ops[0]["value"])
        self.assertEqual(ops[1]["value"], "((old & 0x0fc0) | ((old & 0x20) | 0x0c))")
        self.assertEqual(ops[1]["offset"], "0x3fb")

    def test_rx_lpf_expands_selected_offsets_and_pairs(self):
        ops = module.rx_lpf_events()
        self.assertEqual(len(ops), 24)
        self.assertEqual([(x["offset"], x["operation"]) for x in ops[:4]], [
            ("0x140", "TABLE_READ"), ("0x360", "TABLE_READ"),
            ("0x140", "TABLE_WRITE"), ("0x360", "TABLE_WRITE"),
        ])

    def test_parent_payloads_decode_from_pinned_elf_data(self):
        calls = module.parent_payload_calls(module.DEFAULT_IMAGE)
        self.assertEqual((len(calls), sum(c["count"] for c in calls)), (13, 62))
        self.assertEqual(calls[0]["values"], [4, 3, 6, 5, 2, 1, 8, 0x2a,
                                                0x2b, 0xf, 0x1f, 0x1f, 0x1f,
                                                0x1f, 0x1f, 0x1f])
        apparent_zero = [c for c in calls if "pointer_resolution" in c]
        self.assertEqual(len(apparent_zero), 7)
        self.assertTrue(all(c["pointer_resolution"]["raw_r9_immediate"] == "0x00000000"
                            and c["pointer_resolution"]["relocation_type"] == "R_X86_64_32S"
                            and c["pointer_resolution"]["relocation_symbol_section"] == ".data"
                            for c in apparent_zero))
        self.assertEqual([c["pointer_resolution"]["resolved_data_object"] for c in apparent_zero], [
            ".data+0xd9f70", ".data+0xd9f90", ".data+0xd9fb0", ".data+0xd9fb0",
            ".data+0xd9fb4", ".data+0xd9fb4", ".data+0xda000"])

    def test_emitted_table7_and_phy_counts_reconcile(self):
        result = module.build(module.DEFAULT_IMAGE)
        self.assertEqual(result["counts"]["table7_reads"], 66)
        self.assertEqual(result["counts"]["table7_writes"], 128)
        self.assertEqual(result["counts"]["phy_019e_reads"], 6)
        self.assertEqual(result["counts"]["phy_019e_rmws"], 12)
        self.assertEqual(result["resume_after_accepted_farrow_rows"]["site"], "0xa53b1")
        self.assertEqual(result["resume_after_accepted_farrow_rows"]["next_selected_operation"]["selector"], "0x1601")

    def test_farrow_selected_branch_map_and_resume(self):
        result = module.build(module.DEFAULT_IMAGE)
        branches = {item["site"]: item for item in result["farrow_branch_map"]}
        self.assertEqual(branches["0xa56c8"]["effect"], "NOT_TAKEN to 0xa5ab2")
        self.assertEqual(branches["0xa56c8"]["selected_value"], "0")
        self.assertEqual(branches["0xa5d3e"]["effect"], "TAKEN")
        ops = [e for e in result["events"] if e["caller"] == "sub_a4adc"]
        resume = next(e for e in ops if e["site"] == "0xa53b1")
        self.assertEqual((resume["domain"], resume["operation"], resume["selector"]),
                         ("PHY", "READ", "0x0601"))
        self.assertEqual(next(e for e in ops if e["site"] == "0xa53fc")["selector"], "0x1601")

    def test_farrow_accepted_initial_rows_and_table7_iterations(self):
        ops = [e for e in module.farrow_selected_events() if e["caller"] == "sub_a4adc"]
        expected = [(0x019a, 0xcccd), (0x019b, 0x0069), (0x019c, 0x1400), (0x0199, 0x00a7),
                    (0x01a1, 0xcccd), (0x01a2, 0x0069), (0x01a3, 0x1400), (0x01a0, 0x00a7),
                    (0x1603, 0x91c3), (0x1602, 0x005a), (0x1607, 0x91c3), (0x1606, 0x005a)]
        self.assertEqual([(int(e["selector"], 16), int(e["value"], 16)) for e in ops[:12]], expected)
        t7 = [e for e in ops if e["domain"] == "TABLE"]
        self.assertEqual([(e["operation"], e["offset"], e["loop_iteration"]) for e in t7], [
            ("TABLE_READ", "0x3cd", 0), ("TABLE_WRITE", "0x3cd", 0),
            ("TABLE_READ", "0x3dd", 1), ("TABLE_WRITE", "0x3dd", 1),
        ])
        self.assertEqual([e["value"] for e in t7 if e["operation"] == "TABLE_WRITE"], [0, 0])

    def test_farrow_table8_is_skipped_and_revision_tail_is_exact(self):
        result = module.build(module.DEFAULT_IMAGE)
        ops = [e for e in result["events"] if e["caller"] == "sub_a4adc"]
        self.assertEqual(result["counts"]["farrow_table8_reads"], 0)
        self.assertEqual(result["counts"]["farrow_table8_writes"], 0)
        writes = [(int(e["selector"], 16), e["value"]) for e in ops if e["operation"] == "WRITE"]
        self.assertEqual(writes[-10:], [
            (0x0371, "0x03c9"), (0x0372, "0x03c5"), (0x0373, "0x03c1"),
            (0x0374, "0x043a"), (0x0375, "0x043f"), (0x0376, "0x0443"),
            (0x031c, "0x00ff"), (0x031d, "0x00ff"), (0x031e, "0x00ff"), (0x031f, "0x00ff"),
        ])
        self.assertEqual(ops[-1]["selector"], "0x031f")
        self.assertEqual(result["farrow_scope"]["selected_helper_calls"], [])
        self.assertNotIn("crs_min_pwr_cal", " ".join(str(e) for e in ops))

    def test_generated_artifact_remains_incomplete_until_predecessor_is_merged(self):
        result = module.build(module.DEFAULT_IMAGE)
        self.assertFalse(result["complete"])
        gap = result["first_uncomposed_predecessor_operation"]
        self.assertEqual((gap["site"], gap["domain"], gap["register"], gap["operation"], gap["value"]),
                         ("0xa04c2", "PHY", "0x0410", "WRITE", "0x0077"))
        self.assertEqual([event["seq"] for event in result["events"]],
                         list(range(1, len(result["events"]) + 1)))
        required = {"seq", "caller", "site", "domain", "selector", "table_id", "offset",
                    "width", "operation", "mask", "value", "value_source", "chain", "slot",
                    "loop_iteration", "poll_bound", "save_restore_group", "branch_provenance"}
        self.assertTrue(all(required <= event.keys() for event in result["events"]))
        self.assertEqual(result["events"][-1]["selector"], "0x031f")
        self.assertEqual(result["events"][-1]["value"], "0x00ff")


if __name__ == "__main__":
    unittest.main()
