import hashlib
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VECTOR_PATH = ROOT / "artifacts/contracts/bcm4352-sub-a04c2-selected-event-vector.json"
MANIFEST_PATH = ROOT / "artifacts/contracts/bcm4352-lpf-23-descriptor-manifest-v1.json"
EXECUTION_PATH = ROOT / "artifacts/contracts/bcm4352-sub-a04c2-cleanroom-execution-v1.json"
VECTOR_SHA256 = "b42c110247111a49fae469d07ac5bd0605e1ab0fc5a5386e25c0310ffd3b0d2f"

# Independently transcribed accepted descriptor boundaries and shapes.
EXPECTED_DESCRIPTORS = [
    (1, 128, 16, 21, 150),
    (2, 38, 8, 151, 190),
    (4, 256, 8, 191, 448),
    (3, 256, 32, 449, 962),
    (5, 22, 32, 963, 1008),
    (64, 128, 16, 1009, 1138),
    (96, 128, 16, 1139, 1268),
    (128, 128, 16, 1269, 1398),
    (65, 128, 32, 1399, 1656),
    (97, 128, 32, 1657, 1914),
    (129, 128, 32, 1915, 2172),
    (66, 128, 16, 2173, 2302),
    (98, 128, 16, 2303, 2432),
    (130, 128, 16, 2433, 2562),
    (64, 128, 16, 2563, 2692),
    (96, 128, 16, 2693, 2822),
    (128, 128, 16, 2823, 2952),
    (71, 64, 32, 2953, 3082),
    (103, 64, 32, 3083, 3212),
    (135, 64, 32, 3213, 3342),
    (72, 64, 32, 3343, 3472),
    (104, 64, 32, 3473, 3602),
    (136, 64, 32, 3603, 3732),
]


class LpfDescriptorManifestTests(unittest.TestCase):
    def setUp(self):
        self.vector = json.loads(VECTOR_PATH.read_text())

    def test_manifest_has_all_accepted_descriptor_shapes_and_boundaries(self):
        manifest = json.loads(MANIFEST_PATH.read_text())
        self.assertEqual(manifest["schema"], "openbrcm-bcm4352-lpf-descriptor-manifest/v1")
        self.assertEqual(manifest["descriptor_count"], 23)
        self.assertEqual(manifest["payload_element_count"], 2620)
        self.assertEqual(manifest["logical_event_count"], 3712)
        self.assertEqual(
            [
                (
                    d["table_id"], d["element_count"], d["element_width_bits"],
                    d["first_logical_event"], d["last_logical_event"],
                )
                for d in manifest["descriptors"]
            ], EXPECTED_DESCRIPTORS,
        )
        self.assertEqual(
            [d["ordinal"] for d in manifest["descriptors"]], list(range(23))
        )

    def test_manifest_payload_values_follow_the_pinned_selected_vector(self):
        manifest = json.loads(MANIFEST_PATH.read_text())
        self.assertEqual(hashlib.sha256(VECTOR_PATH.read_bytes()).hexdigest(), VECTOR_SHA256)
        self.assertEqual(manifest["source_event_vector"]["sha256"], VECTOR_SHA256)
        events_by_descriptor = {}
        for event in self.vector["events"]:
            transaction = event.get("table_transaction")
            if transaction is not None:
                events_by_descriptor.setdefault(transaction["descriptor_index"], []).append(event)
        self.assertEqual(len(events_by_descriptor), 23)
        for descriptor, expected in zip(manifest["descriptors"], EXPECTED_DESCRIPTORS):
            ordinal = descriptor["ordinal"]
            payload = {}
            for event in events_by_descriptor[ordinal]:
                if "element_index" in event:
                    payload.setdefault(event["element_index"], int(event["element_value"], 16))
                    self.assertEqual(payload[event["element_index"]], int(event["element_value"], 16))
            values = [payload[i] for i in range(expected[1])]
            self.assertEqual(descriptor["element_values"], values)

    def test_descriptor_logical_ranges_are_contiguous_and_cover_the_table_stream(self):
        manifest = json.loads(MANIFEST_PATH.read_text())
        next_event = 21
        for descriptor, shape in zip(manifest["descriptors"], EXPECTED_DESCRIPTORS):
            self.assertEqual(descriptor["first_logical_event"], next_event)
            self.assertEqual(descriptor["last_logical_event"], shape[4])
            next_event = shape[4] + 1
        self.assertEqual(next_event, 3733)
        self.assertEqual(sum(d["logical_event_count"] for d in manifest["descriptors"]), 3712)

    def test_execution_contract_scope_and_immutable_reference_hashes(self):
        contract = json.loads(EXECUTION_PATH.read_text())
        self.assertEqual(contract["completion_status"], "COMPLETE_PARAMETERIZED")
        self.assertFalse(contract["hardware_ready"])
        self.assertFalse(contract["normal_probe_dispatch"])
        self.assertEqual(contract["declared_scope"]["logical_event_count"], 3734)
        self.assertEqual(contract["declared_scope"]["callback_access_count"], 3736)
        for reference in contract["immutable_references"]:
            path = (ROOT / reference["path"]).resolve()
            self.assertTrue(path.is_relative_to(ROOT.resolve()))
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), reference["sha256"])


if __name__ == "__main__":
    unittest.main()
