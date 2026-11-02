#!/usr/bin/env python3
"""Fail-closed tests for the offline layered device-loss trace validator."""

from __future__ import annotations

import copy
import importlib.util
import pathlib
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "analysis" / "tools" / "device_loss_trace.py"
SPEC = importlib.util.spec_from_file_location("device_loss_trace", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


EXPECTED_IDS = [
    "pre.pci.vendor_device",
    "pre.pci.command_status",
    "pre.pcie.link_status",
    "pre.bar.resource",
    "pre.bar.window_core",
    "pre.bar.window_wrap",
    "pre.bar.mmio_probe",
    "pre.bcma.ioctl",
    "pre.bcma.reset_ctl",
    "pre.d11.probe_0ffc",
    "pre.prereq.d11_01e0",
    "pre.d11.maccontrol_0120",
    "radio.selector.0",
    "radio.data.0",
    "radio.selector.1",
    "radio.data.1",
    "mid.pci.vendor_device",
    "mid.pci.command_status",
    "mid.pcie.link_status",
    "mid.bar.resource",
    "mid.bar.window_core",
    "mid.bar.window_wrap",
    "mid.bar.mmio_probe",
    "mid.bcma.ioctl",
    "mid.bcma.reset_ctl",
    "mid.d11.probe_0ffc",
    "mid.prereq.d11_01e0",
    "sentinel.d11.maccontrol_0120",
    "post.pci.vendor_device",
    "post.pci.command_status",
    "post.pcie.link_status",
    "post.bar.resource",
    "post.bar.window_core",
    "post.bar.window_wrap",
    "post.bar.mmio_probe",
    "post.bcma.ioctl",
    "post.bcma.reset_ctl",
    "post.d11.probe_0ffc",
    "post.prereq.d11_01e0",
]


def event(event_id: str, seq: int, *, raw: int | None, layer: str,
          phase: str, operation: str, address: str, width: int,
          status: str = "ok",
          side_effect: str = "READ-ONLY-BUT-POTENTIALLY-STATE-SENSITIVE",
          decoded: dict[str, object] | None = None,
          trusted: bool = False,
          classification: str | None = None) -> dict[str, object]:
    record: dict[str, object] = {
        "id": event_id,
        "seq": seq,
        "timestamp_ns": 10_000 + seq,
        "sample_id": f"sample-{seq}",
        "fresh": True,
        "phase": phase,
        "layer": layer,
        "operation": operation,
        "address": address,
        "width": width,
        "raw": raw,
        "status": status,
        "side_effect": side_effect,
        "trusted_sentinel": trusted,
    }
    if decoded is not None:
        record["decoded"] = decoded
    if classification is not None:
        record["classification"] = classification
    if event_id.endswith("bar.resource"):
        record["metadata"] = {"start": "0xf0000000", "length": "0x00100000", "mapped": True}
    return record


def full_trace() -> dict[str, object]:
    specs = {
        "pre.pci.vendor_device": ("L0", "pre", "PCI_CFG_READ", "PCI_CFG+0x00", 32, 0x43B114E4),
        "pre.pci.command_status": ("L0", "pre", "PCI_CFG_READ", "PCI_CFG+0x04", 32, 0x00100007),
        "pre.pcie.link_status": ("L1", "pre", "PCI_CFG_READ", "PCI_EXP_CAP+0x12", 16, 0x1022),
        "pre.bar.resource": ("L2", "pre", "PCI_RESOURCE_METADATA", "PCI_BAR0_RESOURCE", 0, 1),
        "pre.bar.window_core": ("L2", "pre", "PCI_CFG_READ", "PCI_CFG+0x80", 32, 0x18001000),
        "pre.bar.window_wrap": ("L2", "pre", "PCI_CFG_READ", "PCI_CFG+0x70", 32, 0x18101000),
        "pre.bar.mmio_probe": ("L2", "pre", "MMIO_READ", "BAR0+APPROVED_PROBE", 32, 0x00000001),
        "pre.bcma.ioctl": ("L3", "pre", "BCMA_AREAD32", "BCMA_WRAPPER+0x0408", 32, 0x00000011),
        "pre.bcma.reset_ctl": ("L3", "pre", "BCMA_AREAD32", "BCMA_WRAPPER+0x0800", 32, 0x00000000),
        "pre.d11.probe_0ffc": ("L4", "pre", "BCMA_READ32", "D11_CORE+0x0ffc", 32, 0x00000001),
        "pre.prereq.d11_01e0": ("L5", "pre", "BCMA_READ32", "D11_CORE+0x01e0", 32, 0x070B0042),
        "pre.d11.maccontrol_0120": ("L4", "pre", "BCMA_READ32", "D11_CORE+0x0120", 32, 0x00000000),
        "radio.selector.0": ("L6", "radio", "BCMA_WRITE16", "D11_CORE+0x03d8", 16, 0x0000),
        "radio.data.0": ("L6", "radio", "BCMA_READ16", "D11_CORE+0x03da", 16, 0x2069),
        "radio.selector.1": ("L6", "radio", "BCMA_WRITE16", "D11_CORE+0x03d8", 16, 0x0001),
        "radio.data.1": ("L6", "radio", "BCMA_READ16", "D11_CORE+0x03da", 16, 0x030B),
        "mid.pci.vendor_device": ("L0", "mid", "PCI_CFG_READ", "PCI_CFG+0x00", 32, 0x43B114E4),
        "mid.pci.command_status": ("L0", "mid", "PCI_CFG_READ", "PCI_CFG+0x04", 32, 0x00100007),
        "mid.pcie.link_status": ("L1", "mid", "PCI_CFG_READ", "PCI_EXP_CAP+0x12", 16, 0x1022),
        "mid.bar.resource": ("L2", "mid", "PCI_RESOURCE_METADATA", "PCI_BAR0_RESOURCE", 0, 1),
        "mid.bar.window_core": ("L2", "mid", "PCI_CFG_READ", "PCI_CFG+0x80", 32, 0x18001000),
        "mid.bar.window_wrap": ("L2", "mid", "PCI_CFG_READ", "PCI_CFG+0x70", 32, 0x18101000),
        "mid.bar.mmio_probe": ("L2", "mid", "MMIO_READ", "BAR0+APPROVED_PROBE", 32, 0x00000001),
        "mid.bcma.ioctl": ("L3", "mid", "BCMA_AREAD32", "BCMA_WRAPPER+0x0408", 32, 0x00000011),
        "mid.bcma.reset_ctl": ("L3", "mid", "BCMA_AREAD32", "BCMA_WRAPPER+0x0800", 32, 0x00000000),
        "mid.d11.probe_0ffc": ("L4", "mid", "BCMA_READ32", "D11_CORE+0x0ffc", 32, 0x00000001),
        "mid.prereq.d11_01e0": ("L5", "mid", "BCMA_READ32", "D11_CORE+0x01e0", 32, 0x070B0042),
        "sentinel.d11.maccontrol_0120": ("L4", "sentinel", "BCMA_READ32", "D11_CORE+0x0120", 32, 0x00000000),
        "post.pci.vendor_device": ("L0", "post", "PCI_CFG_READ", "PCI_CFG+0x00", 32, 0x43B114E4),
        "post.pci.command_status": ("L0", "post", "PCI_CFG_READ", "PCI_CFG+0x04", 32, 0x00100007),
        "post.pcie.link_status": ("L1", "post", "PCI_CFG_READ", "PCI_EXP_CAP+0x12", 16, 0x1022),
        "post.bar.resource": ("L2", "post", "PCI_RESOURCE_METADATA", "PCI_BAR0_RESOURCE", 0, 1),
        "post.bar.window_core": ("L2", "post", "PCI_CFG_READ", "PCI_CFG+0x80", 32, 0x18001000),
        "post.bar.window_wrap": ("L2", "post", "PCI_CFG_READ", "PCI_CFG+0x70", 32, 0x18101000),
        "post.bar.mmio_probe": ("L2", "post", "MMIO_READ", "BAR0+APPROVED_PROBE", 32, 0x00000001),
        "post.bcma.ioctl": ("L3", "post", "BCMA_AREAD32", "BCMA_WRAPPER+0x0408", 32, 0x00000011),
        "post.bcma.reset_ctl": ("L3", "post", "BCMA_AREAD32", "BCMA_WRAPPER+0x0800", 32, 0x00000000),
        "post.d11.probe_0ffc": ("L4", "post", "BCMA_READ32", "D11_CORE+0x0ffc", 32, 0x00000001),
        "post.prereq.d11_01e0": ("L5", "post", "BCMA_READ32", "D11_CORE+0x01e0", 32, 0x070B0042),
    }
    events = []
    for seq, event_id in enumerate(EXPECTED_IDS, 1):
        layer, phase, operation, address, width, raw = specs[event_id]
        decoded = {"link_up": True} if event_id.endswith("link_status") else None
        side_effect = "STATE-CHANGING" if event_id.startswith("radio.selector") else (
            "PASSIVE" if event_id.endswith("bar.resource") else
            "READ-ONLY-BUT-POTENTIALLY-STATE-SENSITIVE"
        )
        events.append(event(event_id, seq, raw=raw, layer=layer, phase=phase,
                            operation=operation, address=address, width=width,
                            side_effect=side_effect, decoded=decoded,
                            trusted=event_id.endswith("maccontrol_0120")))
    return {
        "schema": "openbrcm-device-loss-trace/v1",
        "run_id": "synthetic-full-bracket",
        "expected": {
            "vendor_device": 0x43B114E4,
            "core_addr": 0x18001000,
            "core_wrap": 0x18101000,
            "bar_probe_approved": True,
        },
        "events": events,
    }


def source_trace() -> dict[str, object]:
    binary = ROOT / "tests" / "host" / "ob_device_loss_trace_test"
    completed = subprocess.run(
        [str(binary), "--emit-source"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return MODULE.parse_source_trace(completed.stdout)


class DeviceLossTraceTests(unittest.TestCase):
    def test_full_bracket_preserves_raw_and_localizes_no_failure(self) -> None:
        result = MODULE.analyze_trace(full_trace())
        self.assertTrue(result["valid"], result)
        self.assertIsNone(result["first_abnormal"])
        self.assertEqual(result["layers"]["L0"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L1"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L2"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L3"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L4"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L5"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L6"]["status"], "SUPPORTED")

    def test_radio_all_ones_does_not_become_device_lost_when_d11_survives(self) -> None:
        trace = full_trace()
        radio = trace["events"][13]
        radio["raw"] = 0xFFFF
        radio["classification"] = "ABNORMAL_RAW"
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["first_abnormal"]["id"], "radio.data.0")
        self.assertEqual(result["first_abnormal"]["raw"], 0xFFFF)
        self.assertEqual(result["device_lost"], "UNKNOWN")
        self.assertEqual(result["layers"]["L4"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L6"]["status"], "UNKNOWN")

    def test_d11_all_ones_with_valid_pci_config_localizes_only_to_d11_boundary(self) -> None:
        trace = full_trace()
        sentinel = next(item for item in trace["events"] if item["id"] == "sentinel.d11.maccontrol_0120")
        sentinel["raw"] = 0xFFFFFFFF
        sentinel["classification"] = "DEVICE_LOST"
        trace["events"] = [
            item for item in trace["events"]
            if not (str(item["id"]).startswith("post.bcma") or
                    str(item["id"]).startswith("post.d11") or
                    str(item["id"]).startswith("post.prereq") or
                    str(item["id"]) == "post.bar.mmio_probe")
        ]
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["first_abnormal"]["id"], "sentinel.d11.maccontrol_0120")
        self.assertEqual(result["device_lost"], "SUPPORTED")
        self.assertEqual(result["layers"]["L0"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L1"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L4"]["status"], "UNKNOWN")
        self.assertEqual(result["decision"]["first_unknown_or_bad"], "L4")

    def test_bar_probe_error_does_not_invalidate_independent_pci_config(self) -> None:
        trace = full_trace()
        probe = trace["events"][6]
        probe["status"] = "error"
        probe["raw"] = 0xFFFFFFFF
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["layers"]["L0"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L2"]["status"], "UNKNOWN")
        self.assertNotEqual(result["layers"]["L0"]["status"], result["layers"]["L2"]["status"])

    def test_pci_config_error_is_not_interpreted_as_all_ones_register_content(self) -> None:
        trace = full_trace()
        cfg = trace["events"][0]
        cfg["status"] = "error"
        cfg["raw"] = 0xFFFFFFFF
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["layers"]["L0"]["status"], "UNKNOWN")
        self.assertIsNone(result["layers"]["L0"]["raw_observation"])

    def test_conflicting_window_readbacks_fail_closed(self) -> None:
        trace = full_trace()
        trace["events"][4]["raw"] = 0xDEADBEEF
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"])
        self.assertIn("CONFLICTING_WINDOW_READBACK", result["errors"])
        self.assertEqual(result["layers"]["L2"]["status"], "UNKNOWN")

    def test_missing_observation_fails_closed_without_inference(self) -> None:
        trace = full_trace()
        trace["events"].pop(7)
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"])
        self.assertIn("MISSING_REQUIRED_OBSERVATION", result["errors"])
        self.assertEqual(result["decision"]["first_unknown_or_bad"], "UNKNOWN")

    def test_reordered_observation_fails_closed(self) -> None:
        trace = full_trace()
        trace["events"][4], trace["events"][5] = trace["events"][5], trace["events"][4]
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"])
        self.assertIn("ORDER_MISMATCH", result["errors"])

    def test_duplicate_observation_fails_closed(self) -> None:
        trace = full_trace()
        trace["events"].insert(5, copy.deepcopy(trace["events"][4]))
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"])
        self.assertIn("DUPLICATE_OBSERVATION", result["errors"])

    def test_device_lost_before_post_bracket_is_incomplete_not_localized(self) -> None:
        trace = full_trace()
        trace["events"] = trace["events"][:28]
        trace["events"][-1]["raw"] = 0xFFFFFFFF
        trace["events"][-1]["classification"] = "DEVICE_LOST"
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"])
        self.assertIn("POST_BRACKET_INCOMPLETE", result["errors"])
        self.assertEqual(result["decision"]["first_unknown_or_bad"], "UNKNOWN")

    def test_unknown_side_effect_observer_cannot_support_layer_health(self) -> None:
        trace = full_trace()
        trace["events"][7]["side_effect"] = "UNKNOWN-SIDE-EFFECT"
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"])
        self.assertIn("UNKNOWN_SIDE_EFFECT", result["errors"])
        self.assertEqual(result["layers"]["L3"]["status"], "UNKNOWN")

    def test_stale_sample_reuse_fails_closed(self) -> None:
        trace = full_trace()
        trace["events"][8]["sample_id"] = trace["events"][7]["sample_id"]
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"])
        self.assertIn("STALE_OR_DUPLICATE_SAMPLE", result["errors"])

    def test_source_emitter_output_is_accepted_by_the_existing_validator(self) -> None:
        result = MODULE.analyze_trace(source_trace())
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["profile"], MODULE.SOURCE_PROFILE)
        self.assertEqual(result["layers"]["L3"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L2"]["status"], "UNKNOWN")

    def test_source_emitter_brackets_sentinel_with_mid_pci_observers(self) -> None:
        trace = source_trace()
        ids = [event["id"] for event in trace["events"]]
        data1 = ids.index("radio.data.1")
        sentinel = ids.index("sentinel.d11.maccontrol_0120")
        self.assertLess(data1, ids.index("mid.pci.vendor_device"))
        self.assertLess(ids.index("mid.pci.vendor_device"), sentinel)
        self.assertLess(ids.index("mid.pcie.link_status"), sentinel)
        self.assertEqual(trace["source_metadata"]["observer_set"],
                         "mid-v2-pci-vendor-link-no-bar-probe")
        observers = {
            item["observer_id"]: item for item in trace["observers"]
        }
        for observer_id in ("l0.pci.config", "l1.pcie.link"):
            self.assertEqual(observers[observer_id]["admission"], "ADMITTED")
            self.assertEqual(observers[observer_id]["implemented"], "1")
            self.assertEqual(observers[observer_id]["enabled"], "1")

    def test_source_pci_observers_preserve_access_status_and_capability_base(self) -> None:
        trace = source_trace()
        event_map = {event["id"]: event for event in trace["events"]}
        for event_id in (
            "mid.pci.vendor_device",
            "mid.pcie.link_status",
        ):
            self.assertIn("access_rc", event_map[event_id])
            self.assertEqual(event_map[event_id]["access_rc"], 0)
        self.assertTrue(event_map["mid.pcie.link_status"]["base_valid"])
        self.assertGreaterEqual(event_map["mid.pcie.link_status"]["base_offset"], 0x40)

    def test_mid_pci_config_error_preserves_transport_raw_but_not_l0_health(self) -> None:
        trace = source_trace()
        event = next(item for item in trace["events"]
                     if item["id"] == "mid.pci.vendor_device")
        event.update({
            "status": "error",
            "access_rc": 0x86,
            "access_rc_valid": True,
            "raw_valid": False,
            "raw": None,
            "raw_transport": 0xFFFFFFFF,
            "abnormal": True,
            "first_abnormal": True,
            "classification": "PCI_CONFIG_ERROR",
        })
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["layers"]["L0"]["status"], "UNKNOWN")
        self.assertEqual(event["raw_transport"], 0xFFFFFFFF)
        self.assertEqual(result["device_lost"], "UNKNOWN")

    def test_mid_positive_pcibios_error_cannot_be_marked_raw_valid(self) -> None:
        trace = source_trace()
        event = next(item for item in trace["events"]
                     if item["id"] == "mid.pci.vendor_device")
        event.update({
            "status": "error",
            "access_rc": 0x86,
            "access_rc_valid": True,
            "raw_valid": True,
            "raw": 0xFFFFFFFF,
            "raw_transport": 0xFFFFFFFF,
            "abnormal": True,
            "first_abnormal": True,
            "classification": "PCI_CONFIG_ERROR",
        })
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"], result)
        self.assertIn("PCI_RAW_VALID_MISMATCH", result["errors"])

    def test_mid_successful_all_ones_config_read_is_valid_raw_but_not_healthy(self) -> None:
        trace = source_trace()
        event = next(item for item in trace["events"]
                     if item["id"] == "mid.pci.vendor_device")
        event.update({
            "status": "ok",
            "access_rc": 0,
            "access_rc_valid": True,
            "raw_valid": True,
            "raw": 0xFFFFFFFF,
            "raw_transport": 0xFFFFFFFF,
            "abnormal": True,
            "first_abnormal": True,
            "classification": "PCI_CONFIG_ALL_ONES",
        })
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["layers"]["L0"]["status"], "UNKNOWN")
        self.assertEqual(result["first_abnormal"]["raw"], 0xFFFFFFFF)
        self.assertEqual(result["device_lost"], "UNKNOWN")

    def test_source_missing_pci_device_is_explicitly_unavailable(self) -> None:
        trace = source_trace()
        for event_id in ("mid.pci.vendor_device", "mid.pcie.link_status"):
            event = next(item for item in trace["events"] if item["id"] == event_id)
            event.update({
                "operation": "NONE",
                "access": "NONE",
                "status": "unavailable",
                "raw_valid": False,
                "raw": None,
                "raw_transport": 0,
                "access_rc_valid": False,
                "access_rc": 0,
                "base_valid": False,
                "abnormal": False,
                "first_abnormal": False,
                "classification": "UNAVAILABLE",
            })
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["layers"]["L0"]["status"], "UNKNOWN")
        self.assertEqual(result["layers"]["L1"]["status"], "UNKNOWN")

    def test_source_pci_observers_use_zero_only_for_raw_valid(self) -> None:
        radio_source = (ROOT / "src" / "ob_radio.c").read_text(encoding="utf-8")
        self.assertIn("rc == 0, vendor_device", radio_source)
        self.assertIn("rc == 0, link_status", radio_source)

    def test_mid_link_down_is_abnormal_without_device_lost_classification(self) -> None:
        trace = source_trace()
        event = next(item for item in trace["events"]
                     if item["id"] == "mid.pcie.link_status")
        event.update({
            "raw": 0,
            "raw_transport": 0,
            "abnormal": True,
            "first_abnormal": True,
            "classification": "LINK_NOT_ACTIVE",
            "decoded": {"link_up": False},
        })
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["layers"]["L0"]["status"], "SUPPORTED")
        self.assertEqual(result["layers"]["L1"]["status"], "UNKNOWN")
        self.assertEqual(result["device_lost"], "UNKNOWN")

    def test_mid_link_unavailable_is_explicit_and_fails_closed(self) -> None:
        trace = source_trace()
        event = next(item for item in trace["events"]
                     if item["id"] == "mid.pcie.link_status")
        event.update({
            "operation": "NONE",
            "access": "NONE",
            "status": "unavailable",
            "raw_valid": False,
            "raw": None,
            "raw_transport": 0,
            "access_rc_valid": False,
            "access_rc": 0,
            "base_valid": False,
            "abnormal": False,
            "first_abnormal": False,
            "classification": "UNAVAILABLE",
        })
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["layers"]["L1"]["status"], "UNKNOWN")

    def test_source_radio_all_ones_is_raw_abnormal_without_device_loss(self) -> None:
        trace = source_trace()
        item = next(event for event in trace["events"] if event["id"] == "radio.data.0")
        item["raw"] = 0xFFFF
        item["raw_valid"] = True
        item["abnormal"] = True
        item["classification"] = "ABNORMAL_RAW"
        item["first_abnormal"] = True
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["first_abnormal"]["raw"], 0xFFFF)
        self.assertEqual(result["device_lost"], "UNKNOWN")
        self.assertEqual(result["layers"]["L6"]["status"], "UNKNOWN")

    def test_source_trusted_sentinel_preserves_raw_and_latches(self) -> None:
        trace = source_trace()
        item = next(
            event for event in trace["events"]
            if event["id"] == "sentinel.d11.maccontrol_0120"
        )
        item["raw"] = 0xFFFFFFFF
        item["raw_valid"] = True
        item["abnormal"] = True
        item["classification"] = "DEVICE_LOST"
        item["device_lost_after"] = True
        item["first_abnormal"] = True
        trace["source_terminal"]["classification"] = "DEVICE_LOST"
        result = MODULE.analyze_trace(trace)
        self.assertTrue(result["valid"], result)
        self.assertEqual(result["device_lost"], "SUPPORTED")
        self.assertEqual(result["first_abnormal"]["raw"], 0xFFFFFFFF)
        self.assertEqual(result["layers"]["L4"]["status"], "UNKNOWN")

    def test_source_missing_raw_is_not_equivalent_to_all_ones(self) -> None:
        trace = source_trace()
        item = next(event for event in trace["events"] if event["id"] == "radio.data.1")
        item.pop("raw", None)
        item["raw_valid"] = False
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"], result)
        self.assertIn("RAW_VALUE_MISSING", result["errors"])

    def test_source_unknown_side_effect_fails_closed(self) -> None:
        trace = source_trace()
        item = next(event for event in trace["events"] if event["id"] == "radio.data.0")
        item["side_effect"] = "UNKNOWN-SIDE-EFFECT"
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"], result)
        self.assertIn("UNKNOWN_SIDE_EFFECT", result["errors"])

    def test_source_latch_state_mismatch_fails_closed(self) -> None:
        trace = source_trace()
        item = next(
            event for event in trace["events"]
            if event["id"] == "sentinel.d11.maccontrol_0120"
        )
        item["raw"] = 0xFFFFFFFF
        item["raw_valid"] = True
        item["abnormal"] = True
        item["classification"] = "DEVICE_LOST"
        item["device_lost_before"] = True
        item["device_lost_after"] = True
        item["first_abnormal"] = True
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"], result)
        self.assertIn("LATCH_STATE_MISMATCH", result["errors"])

    def test_source_observer_metadata_keeps_bar_unavailable(self) -> None:
        trace = source_trace()
        bar = next(item for item in trace["observers"] if item["observer_id"] == "l2.bar.raw")
        self.assertEqual(bar["admission"], "UNAVAILABLE")
        self.assertEqual(bar["reason"], "NO_ADMITTED_RAW_BAR_PROBE")
        result = MODULE.analyze_trace(trace)
        self.assertEqual(result["layers"]["L2"]["status"], "UNKNOWN")

    def test_source_build_identity_is_required_even_when_explicitly_unavailable(self) -> None:
        trace = source_trace()
        trace["source_metadata"].pop("build_id")
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"], result)
        self.assertIn("MISSING_BUILD_IDENTITY", result["errors"])

    def test_source_observer_set_is_visible_and_distinguishable(self) -> None:
        first = source_trace()
        second = source_trace()
        second["source_metadata"]["observer_set"] = "base-v1-different-set"
        self.assertNotEqual(
            first["source_metadata"]["observer_set"],
            second["source_metadata"]["observer_set"],
        )

    def test_source_event_after_trusted_loss_fails_closed(self) -> None:
        trace = source_trace()
        sentinel = next(
            event for event in trace["events"]
            if event["id"] == "sentinel.d11.maccontrol_0120"
        )
        sentinel["raw"] = 0xFFFFFFFF
        sentinel["raw_valid"] = True
        sentinel["abnormal"] = True
        sentinel["classification"] = "DEVICE_LOST"
        sentinel["device_lost_after"] = True
        sentinel["first_abnormal"] = True
        extra = copy.deepcopy(trace["events"][0])
        extra["id"] = "prep.d11.01e0"
        extra["seq"] = len(trace["events"]) + 1
        extra["timestamp_ns"] += 100
        trace["events"].append(extra)
        result = MODULE.analyze_trace(trace)
        self.assertFalse(result["valid"], result)
        self.assertIn("MMIO_AFTER_DEVICE_LOST", result["errors"])

    def test_source_path_has_no_new_reset_recovery_retry_or_bar_probe(self) -> None:
        radio_source = (ROOT / "src" / "ob_radio.c").read_text(encoding="utf-8")
        trace_source = (ROOT / "src" / "ob_device_loss_trace.c").read_text(encoding="utf-8")
        self.assertNotIn("bcma_core_reset", trace_source)
        self.assertNotIn("wlc_phy", trace_source)
        self.assertNotIn("ob_d11", trace_source)
        self.assertNotIn("ob_axi", trace_source)
        self.assertNotIn("retry", trace_source.lower())
        self.assertNotIn("SYNTHETIC", radio_source)
        self.assertNotIn("BAR0+", radio_source)
        self.assertNotIn("pci_write_config", radio_source)
        self.assertEqual(radio_source.count("pci_read_config_dword(pdev"), 1)
        self.assertEqual(radio_source.count("pci_read_config_word(pdev"), 1)
        self.assertNotIn("pci_find_capability", radio_source)
        # Identity probing uses one guarded selector write callback; the
        # production arbitrary-radio backend adds the bounded latch/data
        # transactions (no new BAR/reset/retry path). The remaining two sites
        # are the two DISTINCT vendor anacore writes to D11+0x3e6
        # (wlc_phy_anacore default arm 0xbabce..0xbabee): the attach-path one
        # at 0xbed85 in ob_radio_vendor_acphy_attach_anacore(), and the
        # reset-path one at 0x65e04 in ob_radio_vendor_acphy_reset(). Each
        # replaced a two-write 0x3fc/0x3fe PHY-window transaction, so they add
        # no BAR/reset/retry path and stay inside ob_d11_write16.
        self.assertEqual(radio_source.count("ob_d11_write16("), 6)
        self.assertEqual(radio_source.count("ob_d11_read16((struct ob_hw *)ctx"), 1)


if __name__ == "__main__":
    unittest.main()
