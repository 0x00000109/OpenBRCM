#!/usr/bin/env python3
"""Parser and evidence regressions for the offline BCM4360 trace decoder."""

from __future__ import annotations

import pathlib
import hashlib
import json
import os
import tempfile
import unittest

from scripts.trace import bcm4360_trace as trace


EVIDENCE = pathlib.Path(os.environ.get(
    "BCM4360_EVIDENCE_DIR",
    "/home/kartashoff/.local/share/openbrcm/research-inputs/bcm4360-stand",
))


def pcidev_line(bus: int, vendor: int, device: int, bar0: int, bar2: int,
                *, bar0_size: int = 0x8000, bar2_size: int = 0x200000,
                driver: str = "wl") -> str:
    """Build the mmiotrace PCIDEV BAR words and resource-size fields."""
    fields = [
        "PCIDEV", f"{bus:02x}00", f"{vendor:04x}{device:04x}", "10",
        f"{bar0 | 0x4:x}", "0", f"{bar2 | 0x4:x}", "0", "0", "0", "0",
        f"{bar0_size:x}", "0", f"{bar2_size:x}", "0", "0", "0", "0", driver,
    ]
    return " ".join(fields) + "\n"


DUAL_PCIDEV = (
    pcidev_line(5, 0x14E4, 0x43B1, 0xF7A00000, 0xF7800000)
    + pcidev_line(6, 0x14E4, 0x43A0, 0xF7600000, 0xF7400000)
)


class MmiotraceParserTests(unittest.TestCase):
    def write(self, text: str) -> pathlib.Path:
        tmp = tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False)
        tmp.write(text)
        tmp.close()
        self.addCleanup(pathlib.Path(tmp.name).unlink, missing_ok=True)
        return pathlib.Path(tmp.name)

    def test_preserves_mapping_id_and_does_not_call_it_cpu(self) -> None:
        source = self.write(
            "VERSION 20070824\n"
            "PCIDEV 0500 14e443a0 10 f7a00004 0 f7800004 0 0 0 0 8000 0 200000 0 0 0 0 wl\n"
            "MAP 1.000000 7 0xf7a00000 0xffffa000 0x4000 0x0 0\n"
            "MARK 1.000001 MARK: BEGIN up\n"
            "R 4 1.000002 7 0xf7a00000 0x12345678 0x0 0\n"
            "W 2 1.000003 7 0xf7a00004 0x1234 0x0 0\n"
            "UNMAP 1.000004 7\n"
        )
        parsed = trace.parse_mmiotrace(source)
        self.assertEqual(parsed.version, "20070824")
        mmio = [r for r in parsed.records if r["kind"] in {"R", "W"}]
        self.assertEqual([r["map_id"] for r in mmio], [7, 7])
        self.assertEqual([r["cpu"] for r in mmio], [None, None])
        self.assertEqual([r["width_bytes"] for r in mmio], [4, 2])
        self.assertEqual(mmio[0]["physical_address"], 0xF7A00000)
        self.assertEqual(mmio[0]["value"], 0x12345678)
        self.assertEqual(parsed.maps[7]["length"], 0x4000)

    def test_unknown_noncomment_record_is_not_silently_discarded(self) -> None:
        source = self.write("VERSION 20070824\nBOGUS 1 2\n")
        with self.assertRaises(trace.TraceParseError):
            trace.parse_mmiotrace(source)


class ConfigTraceParserTests(unittest.TestCase):
    def write(self, text: str) -> pathlib.Path:
        tmp = tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False)
        tmp.write(text)
        tmp.close()
        self.addCleanup(pathlib.Path(tmp.name).unlink, missing_ok=True)
        return pathlib.Path(tmp.name)

    def test_parses_cfg_width_offset_value_cpu_and_timestamp(self) -> None:
        source = self.write(
            "# tracer: nop\n"
            " task-10 [002] .... 1.250000: cfgw16: (pci_bus_write_config_word+0x0/0x30) devfn=0x0 where=0x4 val=0x6\n"
            " task-10 [001] .N.. 1.250001: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x80 val=0x18001000\n"
        )
        records = trace.parse_cfgtrace(source)
        self.assertEqual(len(records), 2)
        self.assertEqual((records[0]["width_bytes"], records[0]["offset"], records[0]["value"]), (2, 4, 6))
        self.assertEqual(records[0]["cpu"], 2)
        self.assertEqual(records[1]["offset"], 0x80)

    def test_parses_optional_full_bdf_without_promoting_devfn(self) -> None:
        source = self.write(
            " task-10 [002] .... 1.250001: cfgw32: (pci_bus_write_config_dword+0x0/0x30) "
            "devfn=0x0 bdf=0000:06:00.0 where=0x80 val=0x18001000\n"
        )
        record = trace.parse_cfgtrace(source)[0]
        self.assertEqual(record["devfn"], 0)
        self.assertEqual(record["bdf"], "0000:06:00.0")

    def test_unknown_event_line_is_reported(self) -> None:
        source = self.write("# tracer: nop\n task-10 [000] .... 1.0: other: value=1\n")
        with self.assertRaises(trace.TraceParseError):
            trace.parse_cfgtrace(source)


class DecodeStateTests(unittest.TestCase):
    def test_window_registers_remain_independent_and_repeated_writes_survive(self) -> None:
        records = [
            {"offset": 0x80, "value": 0x18001000, "timestamp_s": 1.0, "line": 1},
            {"offset": 0xAC, "value": 0x18101000, "timestamp_s": 1.1, "line": 2},
            {"offset": 0xAC, "value": 0x18101000, "timestamp_s": 1.2, "line": 3},
            {"offset": 0x70, "value": 0x18101000, "timestamp_s": 1.3, "line": 4},
        ]
        timeline = trace.build_window_timeline(records)
        self.assertEqual([event["offset"] for event in timeline], [0x80, 0xAC, 0xAC, 0x70])
        self.assertEqual(timeline[1]["selector_name"], "secondary_alt_0xac")
        self.assertEqual(timeline[3]["selector_name"], "secondary_pcie2_0x70")

    def test_fixed_bar_slots_are_source_qualified_not_enumeration_guesses(self) -> None:
        slot2 = trace.classify_bar0_offset(0x2000)
        slot3 = trace.classify_bar0_offset(0x3000)
        self.assertEqual((slot2["classification"], slot2["source_core_id"]), ("AMBIGUOUS", None))
        self.assertEqual(slot2["region"], "fixed_pci_core_candidate")
        self.assertEqual((slot3["classification"], slot3["source_core_id"]), ("LINUX_SOURCE_DEFINED", 0x800))
        self.assertIsNone(slot2["capture_core_id"])
        self.assertIsNone(slot3["capture_core_id"])

    def test_fixed_chipcommon_candidate_does_not_claim_captured_owner(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "fixed.mmiotrace"
            source.write_text(
                "VERSION 20070824\n"
                "PCIDEV 0500 14e443a0 10 f7a00004 0 f7800004 0 0 0 0 8000 0 200000 0 0 0 0 wl\n"
                "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                "R 4 1.1 7 0xf7a03000 0x1234 0 0\n",
                encoding="utf-8")
            event = trace.analyze_capture(source)["mmio_events"][0]
            self.assertIsNone(event["core_id"])
            self.assertEqual(event["core_id_candidate"], 0x800)
            self.assertEqual(event["certainty"], "LINUX_SOURCE_DEFINED")
            self.assertEqual(event["window_join_certainty"], "NOT_APPLICABLE_FIXED_WINDOW")

    def test_erom_core_inventory_preserves_unknown_ids(self) -> None:
        words = [
            0x4BF80001, 0x2B084411, 0x3, 0x18000005, 0x181000C5,
            0x4BF81201, 0x2A004211, 0x103, 0x18001005, 0x181010C5,
            0x4BFABC01, 0x12345678, 0x3, 0x18009005,
        ]
        cores = trace.decode_erom_words(words)
        self.assertEqual([c["core_id"] for c in cores], [0x800, 0x812, 0xABC])
        self.assertEqual([c["primary_base"] for c in cores], [0x18000000, 0x18001000, 0x18009000])

    def test_d11_indirect_phy_pairs_and_combined_write_keep_raw_refs(self) -> None:
        records = [
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x3FC, "value": 0x739, "line": 10, "core_id": 0x812},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x3FE, "value": 0x1234, "line": 11, "core_id": 0x812},
            {"kind": "W", "width_bytes": 4, "bar_offset": 0x3FC, "value": 0xABCD073A, "line": 12, "core_id": 0x812},
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x160, "value": 0x10042, "line": 13, "core_id": 0x812},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x164, "value": 0xBEEF, "line": 14, "core_id": 0x812},
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x166, "value": 0xCAFE, "line": 15, "core_id": 0x812},
        ]
        logical = trace.decode_d11_events(records)
        self.assertEqual([(e["domain"], e["direction"], e["register"]) for e in logical], [
            ("PHY", "READ", 0x739), ("PHY", "WRITE", 0x73A),
            ("SHM", "READ", 0x108), ("SHM", "WRITE", 0x10A)
        ])
        self.assertEqual(logical[0]["value"], 0x1234)
        self.assertEqual(logical[1]["value"], 0xABCD)
        self.assertEqual(logical[0]["raw_lines"], [10, 11])
        self.assertEqual(logical[2]["raw_lines"], [13, 14])
        self.assertEqual(logical[3]["raw_lines"], [13, 15])
        self.assertEqual(logical[3]["object_selector"], 1)

    def test_structural_diff_finds_insertions_without_value_equality(self) -> None:
        left = [{"direction": "R", "width_bytes": 4, "bar_offset": 0x10},
                {"direction": "W", "width_bytes": 4, "bar_offset": 0x14},
                {"direction": "R", "width_bytes": 2, "bar_offset": 0x3FE}]
        right = [left[0], {"direction": "R", "width_bytes": 4, "bar_offset": 0x18}, left[1], left[2]]
        diff = trace.compare_structure(left, right)
        self.assertEqual(diff["first_positional_mismatch"], {"left_index": 1, "right_index": 1})
        self.assertEqual(diff["longest_matching_block"], 2)

    def test_malformed_incomplete_map_reference_is_explicit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "short.mmiotrace"
            source.write_text(
                "VERSION 20070824\n"
                "MAP 1.0 4 0x1000 0x2000 0x1000 0 0\n"
                "R 4 1.1 5 0x1000 0x1 0 0\n",
                encoding="utf-8",
            )
            parsed = trace.parse_mmiotrace(source)
            event = next(r for r in parsed.records if r["kind"] == "R")
            self.assertFalse(event["map_known"])
            self.assertEqual(event["map_id"], 5)

    def test_normalized_output_retains_capture_and_source_record_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "tiny.mmiotrace"
            source.write_text(
                "VERSION 20070824\n"
                "PCIDEV 0500 14e443a0 10 f7a00004 0 f7800004 0 0 0 0 8000 0 200000 0 0 0 0 wl\n"
                "MAP 1.000000 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                "R 4 1.000001 7 0xf7a00020 0x4bf81201 0 0\n",
                encoding="utf-8",
            )
            analysis = trace.analyze_capture(source)
            trace.write_analysis_outputs(analysis, root / "derived")
            row = json.loads((root / "derived" / "normalized.jsonl").read_text().splitlines()[0])
            self.assertEqual(row["capture_id"], analysis["capture_sha256"][:16])
            self.assertEqual(row["source_line"], 4)
            self.assertEqual(row["device_profile_id"], "14e4:43a0@0000:05:00.0")
            self.assertEqual(row["value"], 0x4BF81201)
            source_records = [json.loads(line) for line in
                              (root / "derived" / "capture-records.jsonl").read_text().splitlines()]
            self.assertEqual({record["kind"] for record in source_records}, {"VERSION", "PCIDEV", "MAP"})

    def test_logical_port_transaction_retains_raw_and_selector_evidence(self) -> None:
        records = [
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x3FC, "value": 0x739,
             "line": 10, "core_id": 0x812, "source_file": "capture.mmiotrace",
             "source_sha256": "a" * 64, "timestamp_s": 1.0,
             "window_join_evidence": {"equal_displayed_timestamp_selector_records": [],
                                       "numeric_timestamp_predecessor_candidate": {"0x80": {"value": 0x18001000}}}},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x3FE, "value": 0x1234,
             "line": 11, "core_id": 0x812, "source_file": "capture.mmiotrace",
             "source_sha256": "a" * 64, "timestamp_s": 1.1,
             "window_join_evidence": {"equal_displayed_timestamp_selector_records": [],
                                       "numeric_timestamp_predecessor_candidate": {"0x80": {"value": 0x18001000}}}},
        ]
        logical = trace.decode_d11_events(records)[0]
        self.assertEqual(logical["raw_events"][0]["event_id"], "mmio:aaaaaaaaaaaa:10")
        self.assertEqual(logical["raw_events"][1]["event_id"], "mmio:aaaaaaaaaaaa:11")
        self.assertEqual(logical["port_sequence_classification"], "CAPTURE_PROVEN_SAME_STREAM_ADDRESS_DATA")
        self.assertEqual(logical["selector_record_refs"], [])
        self.assertEqual(logical["window_join_classification"], "INFERRED_NOT_PROVEN")

    def test_port_pair_is_downgraded_when_primary_window_candidate_changes(self) -> None:
        records = [
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x3FC, "value": 0x739,
             "line": 10, "core_id": 0x812, "timestamp_s": 1.0,
             "window_join_evidence": {"ordering_status": "NUMERIC_ORDER_IS_INFERENCE_ONLY",
                                       "numeric_timestamp_predecessor_candidate": {"0x80": {"value": 0x18001000}}}},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x3FE, "value": 0x1234,
             "line": 11, "core_id": 0x812, "timestamp_s": 1.1,
             "window_join_evidence": {"ordering_status": "NUMERIC_ORDER_IS_INFERENCE_ONLY",
                                       "numeric_timestamp_predecessor_candidate": {"0x80": {"value": 0x18002000}}}},
        ]
        logical = trace.decode_d11_events(records)[0]
        self.assertEqual(logical["window_join_classification"],
                         "AMBIGUOUS_WINDOW_CHANGED_WITHIN_INDIRECT_TRANSACTION")

    def test_equal_displayed_timestamp_does_not_apply_selector_as_predecessor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            mmio = root / "tiny.mmiotrace"
            cfg = root / "tiny.cfgtrace"
            mmio.write_text(
                "VERSION 20070824\n"
                "PCIDEV 0500 14e443a0 10 f7a00004 0 f7800004 0 0 0 0 8000 0 200000 0 0 0 0 wl\n"
                "MAP 1.000000 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                "R 4 1.000002 7 0xf7a003fc 0x739 0 0\n",
                encoding="utf-8")
            cfg.write_text(
                "# tracer: nop\n#P: 4\n"
                " task-1 [000] .... 1.000002: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x80 val=0x18001000\n",
                encoding="utf-8")
            analysis = trace.analyze_capture(mmio, config_path=cfg)
            event = analysis["mmio_events"][0]
            self.assertEqual(event["window_state_candidate"], {})
            self.assertEqual(event["window_join_certainty"], "AMBIGUOUS_EQUAL_DISPLAYED_TIMESTAMP_ORDER")
            self.assertEqual(len(event["window_join_evidence"]["equal_displayed_timestamp_selector_records"]), 1)

    def test_secondary_window_keeps_70_and_ac_candidates_distinct(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            mmio = root / "tiny.mmiotrace"
            cfg = root / "tiny.cfgtrace"
            mmio.write_text(
                "VERSION 20070824\n"
                "PCIDEV 0500 14e443a0 10 f7a00004 0 f7800004 0 0 0 0 8000 0 200000 0 0 0 0 wl\n"
                "MAP 1.000000 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                "R 4 1.000003 7 0xf7a01004 0x1 0 0\n",
                encoding="utf-8")
            cfg.write_text(
                "# tracer: nop\n"
                " task-1 [000] .... 1.000001: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x70 val=0x18101000\n"
                " task-1 [000] .... 1.000002: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0xac val=0x18102000\n"
                " task-1 [000] .... 1.000004: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x04 val=0x00000006\n",
                encoding="utf-8")
            event = trace.analyze_capture(mmio, config_path=cfg)["mmio_events"][0]
            self.assertNotIn("selected_primary_window_candidate", event)
            self.assertEqual(event["secondary_0x70_window_candidate"], 0x18101000)
            self.assertEqual(event["secondary_0xac_window_candidate"], 0x18102000)
            self.assertEqual(event["secondary_selector_identity"], "AMBIGUOUS_0x70_VERSUS_0xac")
            self.assertEqual(event["window_join_certainty"], "AMBIGUOUS_SECONDARY_SELECTOR_IDENTITY")
            self.assertEqual(event["window_state_candidate"]["0x80"] if "0x80" in event["window_state_candidate"] else None, None)


class MultiDeviceAttributionTests(unittest.TestCase):
    def write(self, directory: pathlib.Path, name: str, text: str) -> pathlib.Path:
        path = directory / name
        path.write_text(text, encoding="utf-8")
        return path

    def test_interleaved_mmio_uses_capture_bdf_and_matching_bar_not_device_name(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "dual.mmiotrace",
                "VERSION 20070824\n" + DUAL_PCIDEV
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                + "MAP 1.0 8 0xf7600000 0xffffb000 0x4000 0 0\n"
                + "R 4 1.1 7 0xf7a00020 0x11 0 0\n"
                + "R 4 1.2 8 0xf7600020 0x22 0 0\n")
            analysis = trace.analyze_capture(source)
            first, second = analysis["mmio_events"]
            self.assertEqual((first["capture_time_bdf"], first["pci_device_id"], first["bar_index"]),
                             ("0000:05:00.0", 0x43B1, 0))
            self.assertEqual((second["capture_time_bdf"], second["pci_device_id"], second["bar_index"]),
                             ("0000:06:00.0", 0x43A0, 0))
            self.assertEqual(first["device_attribution"], "DEVICE_PROVEN")
            self.assertEqual(second["device_attribution"], "DEVICE_PROVEN")
            self.assertNotEqual(first["device_key"], second["device_key"])
            self.assertEqual((first["bar_offset"], second["bar_offset"]), (0x20, 0x20))

    def test_map_id_reuse_and_unmap_preserve_event_mapping_lifetime(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = self.write(pathlib.Path(directory), "reuse.mmiotrace",
                "VERSION 20070824\n" + DUAL_PCIDEV
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                + "R 4 1.1 7 0xf7a00010 0x1 0 0\n"
                + "UNMAP 1.2 7\n"
                + "MAP 1.3 7 0xf7600000 0xffffb000 0x4000 0 0\n"
                + "R 4 1.4 7 0xf7600010 0x2 0 0\n"
                + "UNMAP 1.5 7\n"
                + "R 4 1.6 7 0xf7600010 0x3 0 0\n")
            parsed = trace.parse_mmiotrace(source)
            reads = [r for r in parsed.records if r["kind"] == "R"]
            self.assertEqual([r["mapping_epoch"] for r in reads], [0, 1, None])
            analysis = trace.analyze_capture(source)
            first, second, after_unmap = analysis["mmio_events"]
            self.assertEqual(first["capture_time_bdf"], "0000:05:00.0")
            self.assertEqual(second["capture_time_bdf"], "0000:06:00.0")
            self.assertEqual(after_unmap["device_attribution"], "DEVICE_UNKNOWN")
            self.assertIsNone(after_unmap["bar_offset"])

    def test_device_scoped_cfg_selectors_do_not_cross_contaminate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "dual.mmiotrace",
                "VERSION 20070824\n" + DUAL_PCIDEV
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                + "MAP 1.0 8 0xf7600000 0xffffb000 0x4000 0 0\n"
                + "R 4 1.14 7 0xf7a00020 0x11 0 0\n"
                + "R 4 1.15 8 0xf7600020 0x22 0 0\n")
            cfg = self.write(root, "dual.cfgtrace",
                " task-1 [000] .... 1.10: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 bdf=0000:05:00.0 where=0x80 val=0x18001000\n"
                " task-1 [001] .... 1.11: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 bdf=0000:06:00.0 where=0x80 val=0x18002000\n"
                " task-1 [000] .... 1.12: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 bdf=0000:05:00.0 where=0x70 val=0x18101000\n"
                " task-1 [001] .... 1.13: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 bdf=0000:06:00.0 where=0xac val=0x18102000\n"
                " task-1 [000] .... 1.16: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 bdf=0000:05:00.0 where=0x04 val=0x6\n"
                " task-1 [001] .... 1.17: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 bdf=0000:06:00.0 where=0x04 val=0x6\n")
            analysis = trace.analyze_capture(source, config_path=cfg)
            first, second = analysis["mmio_events"]
            self.assertEqual(first["selected_primary_window_candidate"], 0x18001000)
            self.assertEqual(second["selected_primary_window_candidate"], 0x18002000)
            self.assertIn("0x70", first["window_state_candidate"])
            self.assertNotIn("0xac", first["window_state_candidate"])
            self.assertIn("0xac", second["window_state_candidate"])
            self.assertNotIn("0x70", second["window_state_candidate"])

    def test_devfn_only_cfg_is_ambiguous_for_two_broadcom_devices(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "dual.mmiotrace",
                "VERSION 20070824\n" + DUAL_PCIDEV
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                + "R 4 1.2 7 0xf7a00020 0x11 0 0\n")
            cfg = self.write(root, "old.cfgtrace",
                " task-1 [000] .... 1.1: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x80 val=0x18001000\n")
            analysis = trace.analyze_capture(source, config_path=cfg)
            self.assertEqual(analysis["config_events"][0]["device_attribution"], "DEVICE_AMBIGUOUS")
            self.assertEqual(len(analysis["config_events"][0]["device_candidate_keys"]), 2)
            event = analysis["mmio_events"][0]
            self.assertNotIn("selected_primary_window_candidate", event)
            self.assertEqual(event["window_join_certainty"], "UNKNOWN_CONFIG_DEVICE")

    def test_ambiguous_same_time_cfg_is_referenced_but_applied_to_neither_device(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "dual.mmiotrace",
                "VERSION 20070824\n" + DUAL_PCIDEV
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x1000 0 0\n"
                + "MAP 1.0 8 0xf7600000 0xffffb000 0x1000 0 0\n"
                + "R 4 1.1 7 0xf7a00010 0x11 0 0\n"
                + "R 4 1.1 8 0xf7600010 0x22 0 0\n")
            cfg = self.write(root, "legacy.cfgtrace",
                " task-1 [000] .... 1.1: cfgw32: (pci_bus_write_config_dword+0x0/0x30) "
                "devfn=0x0 where=0x80 val=0x18001000\n")
            analysis = trace.analyze_capture(source, config_path=cfg)
            config_event = analysis["config_events"][0]
            candidates = {d["device_key"] for d in analysis["device_inventory"]
                          if d["vendor_id"] == 0x14E4}
            self.assertEqual(config_event["device_attribution"], "DEVICE_AMBIGUOUS")
            self.assertEqual(set(config_event["device_candidate_keys"]), candidates)
            self.assertEqual(len(analysis["mmio_events"]), 2)
            for event in analysis["mmio_events"]:
                self.assertEqual(event["device_attribution"], "DEVICE_PROVEN")
                self.assertEqual(event["window_join_certainty"], "UNKNOWN_CONFIG_DEVICE")
                self.assertEqual(event["window_state_candidate"], {})
                self.assertNotIn("selected_primary_window_candidate", event)
                refs = event["window_join_evidence"]["equal_displayed_timestamp_selector_records"]
                self.assertEqual(len(refs), 1)
                self.assertEqual(refs[0]["device_attribution"], "DEVICE_AMBIGUOUS")
                self.assertEqual(refs[0]["device_candidate_keys"], config_event["device_candidate_keys"])
                self.assertEqual(refs[0]["raw_record_reference"], config_event["raw_record_reference"])

    def test_full_bdf_with_conflicting_devfn_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "identity.mmiotrace",
                "VERSION 20070824\n" + DUAL_PCIDEV
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x1000 0 0\n"
                + "R 4 1.2 7 0xf7a00010 0x1 0 0\n")
            cfg = self.write(root, "identity.cfgtrace",
                " task-1 [000] .... 1.1: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x8 bdf=0000:05:00.0 where=0x80 val=0x18001000\n"
                " task-1 [000] .... 1.3: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x8 bdf=0000:05:00.0 where=0x04 val=0x6\n")
            analysis = trace.analyze_capture(source, config_path=cfg)
            self.assertEqual(analysis["config_events"][0]["device_attribution"], "DEVICE_UNKNOWN")
            self.assertEqual(analysis["config_events"][0]["attribution_confidence"],
                             "CONFIG_BDF_DEVFN_CONTRADICTION")
            self.assertNotIn("selected_primary_window_candidate", analysis["mmio_events"][0])

    def test_single_target_devfn_only_cfg_is_contextual_not_hardware_proven(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "single.mmiotrace",
                "VERSION 20070824\n" + pcidev_line(5, 0x14E4, 0x43A0, 0xF7A00000, 0xF7800000)
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                + "R 4 1.2 7 0xf7a00020 0x11 0 0\n")
            cfg = self.write(root, "paired.cfgtrace",
                " task-1 [000] .... 1.1: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x80 val=0x18001000\n"
                " task-1 [000] .... 1.3: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x04 val=0x6\n")
            analysis = trace.analyze_capture(source, config_path=cfg)
            self.assertEqual(analysis["config_events"][0]["device_attribution"],
                             "CAPTURE_CONTEXT_INFERRED_UNIQUE_BROADCOM_DEVICE")
            self.assertEqual(analysis["mmio_events"][0]["window_join_certainty"],
                             "INFERRED_LIKELY_SHARED_TIMEBASE_NOT_PROVEN")
            cfg_event = analysis["config_events"][0]
            selector_ref = analysis["mmio_events"][0]["window_state_candidate"]["0x80"]
            self.assertEqual(selector_ref["device_attribution"],
                             "CAPTURE_CONTEXT_INFERRED_UNIQUE_BROADCOM_DEVICE")
            self.assertEqual(selector_ref["attribution_confidence"],
                             "DEVFN_ONLY_UNIQUE_BROADCOM_PCIDEV_IN_PAIRED_CAPTURE")
            self.assertEqual(selector_ref["raw_record_reference"],
                             cfg_event["raw_record_reference"])
            join_ref = analysis["mmio_events"][0]["window_join_evidence"]
            self.assertEqual(join_ref["numeric_timestamp_predecessor_candidate"]["0x80"],
                             selector_ref)
            self.assertEqual(analysis["window_timeline"][0]["attribution_confidence"],
                             "DEVFN_ONLY_UNIQUE_BROADCOM_PCIDEV_IN_PAIRED_CAPTURE")
            self.assertEqual(analysis["selector_state_alternatives"]
                             [analysis["device_inventory"][0]["device_key"]]["0x80"][0]
                             ["selector_record_refs"][0]["raw_record_reference"],
                             cfg_event["raw_record_reference"])

    def test_missing_bar_inventory_and_out_of_range_accesses_are_not_assigned(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            incomplete = self.write(root, "incomplete.mmiotrace",
                "VERSION 20070824\nPCIDEV 0500 14e443b1 10 0 0 0 0 0 0 0 0 0 0 0 0 0 0 wl\n"
                "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                "R 4 1.1 7 0xf7a00000 0x1 0 0\n")
            event = trace.analyze_capture(incomplete)["mmio_events"][0]
            self.assertEqual(event["device_attribution"], "DEVICE_UNKNOWN")
            self.assertIsNone(event["device_key"])
            outside = self.write(root, "outside.mmiotrace",
                "VERSION 20070824\n" + pcidev_line(5, 0x14E4, 0x43B1, 0xF7A00000, 0xF7800000)
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                + "R 4 1.1 7 0xf7a03fff 0x1 0 0\n")
            event = trace.analyze_capture(outside)["mmio_events"][0]
            self.assertEqual(event["device_attribution"], "DEVICE_UNKNOWN")
            self.assertEqual(event["mapping_validation"], "ACCESS_CROSSES_MAP_END")

    def test_overlapping_bar_candidates_remain_ambiguous(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = self.write(pathlib.Path(directory), "overlap.mmiotrace",
                "VERSION 20070824\n"
                + pcidev_line(5, 0x14E4, 0x43B1, 0xF7A00000, 0xF7800000)
                + pcidev_line(6, 0x14E4, 0x43A0, 0xF7A00000, 0xF7400000)
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\n"
                + "R 4 1.1 7 0xf7a00010 0x1 0 0\n")
            event = trace.analyze_capture(source)["mmio_events"][0]
            self.assertEqual(event["device_attribution"], "DEVICE_AMBIGUOUS")
            self.assertEqual(len(event["device_candidate_keys"]), 2)
            self.assertIsNone(event["capture_time_bdf"])

    def test_indirect_phy_and_shm_address_state_is_keyed_by_device(self) -> None:
        records = [
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x3FC, "value": 0x739,
             "line": 1, "core_id": 0x812, "device_key": "cap:05", "device_attribution": "DEVICE_PROVEN"},
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x3FC, "value": 0x725,
             "line": 2, "core_id": 0x812, "device_key": "cap:06", "device_attribution": "DEVICE_PROVEN"},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x3FE, "value": 0x11,
             "line": 3, "core_id": 0x812, "device_key": "cap:05", "device_attribution": "DEVICE_PROVEN"},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x3FE, "value": 0x22,
             "line": 4, "core_id": 0x812, "device_key": "cap:06", "device_attribution": "DEVICE_PROVEN"},
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x160, "value": 0x10042,
             "line": 5, "core_id": 0x812, "device_key": "cap:05", "device_attribution": "DEVICE_PROVEN"},
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x160, "value": 0x10051,
             "line": 6, "core_id": 0x812, "device_key": "cap:06", "device_attribution": "DEVICE_PROVEN"},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x164, "value": 0x33,
             "line": 7, "core_id": 0x812, "device_key": "cap:05", "device_attribution": "DEVICE_PROVEN"},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x164, "value": 0x44,
             "line": 8, "core_id": 0x812, "device_key": "cap:06", "device_attribution": "DEVICE_PROVEN"},
        ]
        logical = trace.decode_d11_events(records)
        phy = [event for event in logical if event["domain"] == "PHY"]
        shm = [event for event in logical if event["domain"] == "SHM"]
        self.assertEqual([(e["register"], e["value"]) for e in phy], [(0x739, 0x11), (0x725, 0x22)])
        self.assertEqual([e["device_key"] for e in phy], ["cap:05", "cap:06"])
        self.assertEqual([(e["register"], e["value"]) for e in shm], [(0x108, 0x33), (0x144, 0x44)])
        self.assertEqual([e["device_key"] for e in shm], ["cap:05", "cap:06"])

    def test_unresolved_core_access_invalidates_pending_indirect_address(self) -> None:
        records = [
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x3FC, "value": 0x739,
             "line": 1, "core_id": 0x812, "device_key": "capture:device"},
            {"kind": "W", "width_bytes": 2, "bar_offset": 0x3FC, "value": 0x725,
             "line": 2, "core_id": None, "device_key": "capture:device"},
            {"kind": "R", "width_bytes": 2, "bar_offset": 0x3FE, "value": 0x11,
             "line": 3, "core_id": 0x812, "device_key": "capture:device"},
        ]
        self.assertEqual(trace.decode_d11_events(records), [])

    def test_device_key_is_capture_scoped_when_bdf_is_reused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            first = self.write(root, "old.mmiotrace",
                "VERSION 20070824\n" + pcidev_line(5, 0x14E4, 0x43A0, 0xF7A00000, 0xF7800000)
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\nR 4 1.1 7 0xf7a00000 1 0 0\n")
            second = self.write(root, "new.mmiotrace",
                "VERSION 20070824\n" + pcidev_line(5, 0x14E4, 0x43B1, 0xF7A00000, 0xF7800000)
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x4000 0 0\nR 4 1.1 7 0xf7a00000 1 0 0\n")
            old_key = trace.analyze_capture(first)["mmio_events"][0]["device_key"]
            new_key = trace.analyze_capture(second)["mmio_events"][0]["device_key"]
            self.assertNotEqual(old_key, new_key)

    def test_capture_key_includes_the_paired_config_stream_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "same-mmio.mmiotrace",
                "VERSION 20070824\n"
                + pcidev_line(5, 0x14E4, 0x43A0, 0xF7A00000, 0xF7800000)
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x1000 0 0\n"
                + "R 4 1.2 7 0xf7a00010 0x1 0 0\n")
            cfg_a = self.write(root, "a.cfgtrace",
                " task-1 [000] .... 1.1: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x80 val=0x18001000\n"
                " task-1 [000] .... 1.3: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x4 val=0x6\n")
            cfg_b = self.write(root, "b.cfgtrace",
                " task-1 [000] .... 1.1: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x80 val=0x18002000\n"
                " task-1 [000] .... 1.3: cfgw32: (pci_bus_write_config_dword+0x0/0x30) devfn=0x0 where=0x4 val=0x6\n")
            first = trace.analyze_capture(source, config_path=cfg_a)["mmio_events"][0]
            second = trace.analyze_capture(source, config_path=cfg_b)["mmio_events"][0]
            self.assertNotEqual(first["capture_id"], second["capture_id"])
            self.assertNotEqual(first["device_key"], second["device_key"])

    def test_same_device_id_at_two_bdfs_remains_two_devices(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "same-id.mmiotrace",
                "VERSION 20070824\n"
                + pcidev_line(5, 0x14E4, 0x43A0, 0xF7A00000, 0xF7800000)
                + pcidev_line(6, 0x14E4, 0x43A0, 0xF7600000, 0xF7400000)
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x1000 0 0\n"
                + "MAP 1.0 8 0xf7600000 0xffffb000 0x1000 0 0\n"
                + "R 4 1.1 7 0xf7a00000 0x1 0 0\n"
                + "R 4 1.2 8 0xf7600000 0x2 0 0\n")
            events = trace.analyze_capture(source)["mmio_events"]
            self.assertEqual([e["capture_time_bdf"] for e in events],
                             ["0000:05:00.0", "0000:06:00.0"])
            self.assertNotEqual(events[0]["device_key"], events[1]["device_key"])

    def test_bar2_mapping_is_not_decoded_as_bar0_window_or_phy_port(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "bar2.mmiotrace",
                "VERSION 20070824\n"
                + pcidev_line(5, 0x14E4, 0x43A0, 0xF7A00000, 0xF7800000)
                + "MAP 1.0 7 0xf7800000 0xffffa000 0x200000 0 0\n"
                + "W 2 1.1 7 0xf78003fc 0x739 0 0\n"
                + "R 2 1.2 7 0xf78003fe 0x1234 0 0\n")
            analysis = trace.analyze_capture(source)
            self.assertEqual([e["bar_index"] for e in analysis["mmio_events"]], [2, 2])
            self.assertTrue(all(e["bar_region"] == "BAR_NONZERO_RAW" for e in analysis["mmio_events"]))
            self.assertEqual(analysis["logical_events"], [])

    def test_map_without_matching_bar_remains_unknown(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "unknown-map.mmiotrace",
                "VERSION 20070824\n"
                + pcidev_line(5, 0x14E4, 0x43A0, 0xF7A00000, 0xF7800000)
                + "MAP 1.0 7 0xf7b00000 0xffffa000 0x1000 0 0\n"
                + "R 4 1.1 7 0xf7b00010 0x1 0 0\n")
            event = trace.analyze_capture(source)["mmio_events"][0]
            self.assertEqual(event["device_attribution"], "DEVICE_UNKNOWN")
            self.assertEqual(event["mapping_validation"], "ACCESS_WITHIN_LIVE_MAP_EPOCH")
            self.assertEqual(event["mapping_classification"], "UNKNOWN_NO_CONTAINING_CAPTURE_BAR")

    def test_all_dual_capture_raw_records_have_one_explicit_device_status(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "statuses.mmiotrace",
                "VERSION 20070824\n" + DUAL_PCIDEV
                + "MAP 1.0 7 0xf7a00000 0xffffa000 0x1000 0 0\n"
                + "MAP 1.0 8 0xf7600000 0xffffb000 0x1000 0 0\n"
                + "R 4 1.1 7 0xf7a00000 0x1 0 0\n"
                + "R 4 1.2 8 0xf7600000 0x2 0 0\n"
                + "R 4 1.3 9 0xf7600000 0x3 0 0\n")
            events = trace.analyze_capture(source)["mmio_events"]
            self.assertEqual({e["device_attribution"] for e in events},
                             {"DEVICE_PROVEN", "DEVICE_UNKNOWN"})
            self.assertTrue(all(e["device_attribution"] in
                {"DEVICE_PROVEN", "DEVICE_AMBIGUOUS", "DEVICE_UNKNOWN"} for e in events))

    def test_device_specific_normalized_fields_do_not_hardcode_historical_bdf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = self.write(root, "new-bdf.mmiotrace",
                "VERSION 20070824\n" + pcidev_line(6, 0x14E4, 0x43A0, 0xF7600000, 0xF7400000)
                + "MAP 1.0 8 0xf7600000 0xffffb000 0x4000 0 0\n"
                + "R 4 1.1 8 0xf7600004 0x1234 0 0\n")
            analysis = trace.analyze_capture(source)
            trace.write_analysis_outputs(analysis, root / "derived")
            row = json.loads((root / "derived" / "normalized.jsonl").read_text().splitlines()[0])
            self.assertEqual(row["capture_time_bdf"], "0000:06:00.0")
            self.assertEqual(row["pci_device_id"], 0x43A0)
            self.assertEqual(row["device_key"], analysis["mmio_events"][0]["device_key"])
            self.assertIn("raw_record_reference", row)

    def test_historical_capture_keeps_its_capture_time_bdf(self) -> None:
        if not EVIDENCE.is_dir():
            self.skipTest("admitted capture input is unavailable")
        analysis = trace.analyze_capture(EVIDENCE / "s2_up_20261011_102757.mmiotrace")
        self.assertTrue(all(event["capture_time_bdf"] == "0000:05:00.0"
                            for event in analysis["mmio_events"]
                            if event["device_attribution"] == "DEVICE_PROVEN"))
        self.assertTrue(all(event["pci_device_id"] == 0x43A0
                            for event in analysis["mmio_events"]
                            if event["device_attribution"] == "DEVICE_PROVEN"))


@unittest.skipUnless(EVIDENCE.is_dir(), "admitted stand captures are not installed")
class RealCaptureRegressionTests(unittest.TestCase):
    EXPECTED_MMIO = {
        "s1_load_20261011_102753.mmiotrace": 864,
        "s2_up_20261011_102757.mmiotrace": 30889,
        "s2_up_cfg_20261011_104750.mmiotrace": 30905,
        "s3_scan_20261011_102802.mmiotrace": 118577,
        "wl_probe_20261011_102247.mmiotrace": 205883,
    }

    def test_raw_mmio_totals_and_target_identity(self) -> None:
        for name, expected in self.EXPECTED_MMIO.items():
            with self.subTest(name=name):
                parsed = trace.parse_mmiotrace(EVIDENCE / name)
                self.assertEqual(sum(r["kind"] in {"R", "W"} for r in parsed.records), expected)
                target = [d for d in parsed.pcidev if d["device_id"] == 0x43A0 and d["vendor_id"] == 0x14E4]
                self.assertEqual(len(target), 1)
                self.assertEqual(target[0]["bdf"], "0000:05:00.0")
                # A capture stopped while mappings were still active; this is
                # surfaced as a boundary warning instead of silently assumed closed.
                self.assertTrue(any(w["kind"] == "MAP_REMAINS_OPEN_AT_CAPTURE_END"
                                    for w in parsed.capture_warnings))

    def test_paired_cfg_counts_and_selector_registers(self) -> None:
        records = trace.parse_cfgtrace(EVIDENCE / "s2_up_cfg.cfgtrace")
        self.assertEqual(len(records), 150)
        offsets = trace.count_config_offsets(records)
        self.assertEqual(offsets, {0x80: 74, 0x70: 66, 0xAC: 6, 0x04: 1, 0x94: 1, 0xB4: 1, 0xD4: 1})

    def test_command_phase_totals_are_based_on_raw_markers(self) -> None:
        expected = {
            "s1_load_20261011_102753.mmiotrace": {"modprobe wl": 864},
            "s2_up_20261011_102757.mmiotrace": {"modprobe wl": 864, "ip link set wlp5s0 up": 29405},
            "s2_up_cfg_20261011_104750.mmiotrace": {"modprobe wl": 864, "ip link set wlp5s0 up": 29406},
            "s3_scan_20261011_102802.mmiotrace": {"modprobe wl": 864, "ip link set wlp5s0 up": 29406, "iw dev wlp5s0 scan": 87793},
        }
        for name, phases in expected.items():
            with self.subTest(name=name):
                parsed = trace.parse_mmiotrace(EVIDENCE / name)
                actual = trace.count_mmio_by_command(parsed)
                for command, count in phases.items():
                    self.assertEqual(actual[command], count)

    def test_admitted_copy_hashes_match_manifest(self) -> None:
        manifest = json.loads((EVIDENCE / "capture-admission-manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(len(manifest["files"]), 10)
        for entry in manifest["files"]:
            with self.subTest(file=entry["name"]):
                path = EVIDENCE / entry["name"]
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertEqual(digest, entry["sha256"])
                self.assertEqual(entry["remote_local_identity"], "MATCH")

    def test_real_trace_reconstructs_main_erom_inventory_and_d11_ports(self) -> None:
        analysis = trace.analyze_capture(
            EVIDENCE / "s2_up_cfg_20261011_104750.mmiotrace",
            config_path=EVIDENCE / "s2_up_cfg.cfgtrace",
        )
        main = [c for c in analysis["erom_inventory"] if c["core_id"] in {0x800, 0x812, 0x83E, 0x83C, 0x81A}]
        self.assertEqual([c["core_id"] for c in main], [0x800, 0x812, 0x83E, 0x83C, 0x81A])
        self.assertEqual([c["primary_base"] for c in main], [0x18000000, 0x18001000, 0x18002000, 0x18003000, 0x18004000])
        regs = {e["register"] for e in analysis["logical_events"] if e["domain"] == "PHY"}
        self.assertTrue({0x739, 0x73A, 0x725}.issubset(regs))
        self.assertTrue(all("raw_events" in e and e["core_id"] is None and
                            e["core_id_candidate"] in {None, 0x812}
                            for e in analysis["logical_events"]))
        self.assertIn("INFERRED_CORE_CANDIDATE_0x812",
                      {e["certainty"] for e in analysis["logical_events"]})
        self.assertTrue(all(e["raw_events"][0]["event_id"].startswith("mmio:")
                            for e in analysis["logical_events"]))
        self.assertTrue(all(e["port_sequence_classification"] ==
                            "CAPTURE_PROVEN_SAME_STREAM_ADDRESS_DATA"
                            for e in analysis["logical_events"]))
        self.assertEqual(analysis["clock_provenance"]["exact_displayed_timestamp_pair_count"], 16)
        self.assertFalse(analysis["clock_provenance"]["shared_trace_instance_or_clock_proven"])
        self.assertEqual(analysis["window_join_counts"]["strictly_proven"], 0)
        self.assertEqual(analysis["window_join_counts"]["inferred_numeric_predecessor"], 13560)
        self.assertEqual(analysis["window_join_counts"]["equal_displayed_timestamp_order_ambiguous"], 15)
        self.assertEqual(analysis["window_join_counts"]["secondary_selector_identity_ambiguous"], 197)
        self.assertEqual(analysis["window_join_counts"]["unproven_dynamic_joins_within_paired_range"], 13772)
        self.assertEqual(analysis["window_join_counts"]["fixed_chipcommon_source_defined_records"], 308)
        self.assertEqual(analysis["window_join_counts"]["fixed_pci_core_ambiguous_records"], 11)
        self.assertEqual(analysis["clock_provenance"]["loss_assessment"],
                         "NO_EXPLICIT_LOSS_MARKER_FOUND; SILENT_OR_PRE-CAPTURE_LOSS_CANNOT_BE_EXCLUDED")

    def test_output_labels_indirect_accesses_as_candidates_and_emits_clock_evidence(self) -> None:
        analysis = trace.analyze_capture(
            EVIDENCE / "s2_up_cfg_20261011_104750.mmiotrace",
            config_path=EVIDENCE / "s2_up_cfg.cfgtrace")
        with tempfile.TemporaryDirectory() as directory:
            trace.write_analysis_outputs(analysis, directory)
            root = pathlib.Path(directory)
            clock = json.loads((root / "clock-provenance.json").read_text())
            alternatives = json.loads((root / "window-state-alternatives.json").read_text())
            stats = json.loads((root / "statistics.json").read_text())
            logical = json.loads((root / "logical-phy-shm.jsonl").read_text().splitlines()[0])
            self.assertEqual(clock["mmiotrace_trace_clock"], "NOT_RECORDED")
            self.assertEqual(alternatives["initial_state"], "UNKNOWN")
            self.assertTrue(alternatives["selector_registers_are_independent"])
            self.assertEqual(stats["proven_logical_phy_transactions"], 0)
            self.assertIn("raw_events", logical)
            self.assertIn("selector_record_refs", logical)

    def test_unpaired_raw_capture_keeps_candidate_phy_ports_ambiguous(self) -> None:
        analysis = trace.analyze_capture(EVIDENCE / "s2_up_20261011_102757.mmiotrace")
        self.assertEqual(analysis["logical_events"], [])
        self.assertGreater(analysis["port_group_counts"]["ip link set wlp5s0 up::D11_PHY_ADDRESS_PORT_CANDIDATE"]["TOTAL"], 0)
        self.assertEqual(analysis["window_join_status"], "UNKNOWN_NO_PAIRED_CONFIG_TRACE")

    def test_up_capture_difference_is_structural_and_value_aware(self) -> None:
        left = trace.analyze_capture(EVIDENCE / "s2_up_20261011_102757.mmiotrace")
        right = trace.analyze_capture(
            EVIDENCE / "s2_up_cfg_20261011_104750.mmiotrace",
            config_path=EVIDENCE / "s2_up_cfg.cfgtrace",
        )
        left_up = [r for r in left["mmio_events"] if r.get("phase") == "ip link set wlp5s0 up"]
        right_up = [r for r in right["mmio_events"] if r.get("phase") == "ip link set wlp5s0 up"]
        diff = trace.compare_structure(left_up, right_up)
        self.assertEqual((diff["left_events"], diff["right_events"]), (29405, 29406))
        self.assertIsNotNone(diff["first_positional_mismatch"])


if __name__ == "__main__":
    unittest.main()
