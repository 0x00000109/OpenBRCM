#!/usr/bin/env python3
import importlib.util
import hashlib
import os
import pathlib
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "stand-lab" / "standctl.py"
SPEC = importlib.util.spec_from_file_location("standctl", MODULE_PATH)
standctl = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(standctl)


class StandCtlSafetyTests(unittest.TestCase):
    def test_btf_raw_layout_parser_returns_exact_required_members(self):
        text = """
[1] INT 'unsigned int' size=4 bits_offset=0 nr_bits=32 encoding=(none)
[2] INT 'unsigned short' size=2 bits_offset=0 nr_bits=16 encoding=(none)
[3] INT 'unsigned char' size=1 bits_offset=0 nr_bits=8 encoding=(none)
[4] PTR '(anon)' type_id=5
[5] STRUCT 'pci_bus' size=64 vlen=2
    'number' type_id=3 bits_offset=0
    'domain_nr' type_id=1 bits_offset=32
[6] STRUCT 'pci_dev' size=256 vlen=4
    'vendor' type_id=2 bits_offset=0
    'device' type_id=2 bits_offset=16
    'devfn' type_id=1 bits_offset=32
    'bus' type_id=4 bits_offset=64
[7] STRUCT 'sk_buff' size=224 vlen=6
    'len' type_id=1 bits_offset=896
    'data_len' type_id=1 bits_offset=928
    'tail' type_id=1 bits_offset=1472
    'end' type_id=1 bits_offset=1504
    'head' type_id=4 bits_offset=1536
    'data' type_id=4 bits_offset=1600
"""
        result = standctl.parse_btf_raw_layout(text, "5.10.0-46-amd64", "a" * 64)
        self.assertEqual(result["structs"]["sk_buff"]["size"], 224)
        self.assertEqual(result["structs"]["sk_buff"]["fields"]["data"]["offset"], 200)
        self.assertEqual(result["structs"]["pci_dev"]["fields"]["bus"]["width"], 8)
        self.assertEqual(result["structs"]["pci_bus"]["fields"]["domain_nr"]["width"], 4)
        self.assertEqual(result["btf_sha256"], "a" * 64)

    def test_btf_layout_parser_rejects_missing_or_duplicate_required_structs(self):
        with self.assertRaises(ValueError):
            standctl.parse_btf_raw_layout("", "5.10.0-46-amd64", "a" * 64)

    def test_staged_pahole_layout_is_exact_and_requires_btf_hash(self):
        text = """
KERNEL_RELEASE: 5.10.0-46-amd64
BTF_SHA256: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
struct sk_buff {
 unsigned int len; /* 112 4 */
 unsigned int data_len; /* 116 4 */
 unsigned int tail; /* 184 4 */
 unsigned int end; /* 188 4 */
 unsigned char *head; /* 192 8 */
 unsigned char *data; /* 200 8 */
}; /* size: 224 */
struct pci_dev {
 unsigned short vendor; /* 20 2 */
 unsigned short device; /* 22 2 */
 unsigned int devfn; /* 24 4 */
 struct pci_bus *bus; /* 32 8 */
}; /* size: 256 */
struct pci_bus {
 unsigned char number; /* 40 1 */
 int domain_nr; /* 44 4 */
}; /* size: 64 */
"""
        result = standctl.parse_pahole_layout(text, "5.10.0-46-amd64", "a" * 64)
        self.assertTrue(result["verified"])
        self.assertEqual(result["structs"]["pci_bus"]["fields"]["domain_nr"],
                         {"offset": 44, "width": 4, "type": "int"})
        with self.assertRaises(ValueError):
            standctl.parse_pahole_layout(text, "5.10.0-46-amd64", "")

    def test_kernel_layout_command_is_read_only_and_allowlisted(self):
        self.assertEqual(standctl.parse_remote_command("standctl kernel layout --json"),
                         ["kernel", "layout", "--json"])
        with self.assertRaises(ValueError):
            standctl.parse_remote_command("standctl kernel layout; id --json")

    def test_kernel_layout_reads_btf_and_never_returns_runtime_addresses(self):
        text = """
[1] INT 'unsigned int' size=4 bits_offset=0 nr_bits=32 encoding=(none)
[2] INT 'unsigned short' size=2 bits_offset=0 nr_bits=16 encoding=(none)
[3] INT 'unsigned char' size=1 bits_offset=0 nr_bits=8 encoding=(none)
[4] PTR '(anon)' type_id=5
[5] STRUCT 'pci_bus' size=64 vlen=2
    'number' type_id=3 bits_offset=0
    'domain_nr' type_id=1 bits_offset=32
[6] STRUCT 'pci_dev' size=256 vlen=4
    'vendor' type_id=2 bits_offset=0
    'device' type_id=2 bits_offset=16
    'devfn' type_id=1 bits_offset=32
    'bus' type_id=4 bits_offset=64
[7] STRUCT 'sk_buff' size=224 vlen=6
    'len' type_id=1 bits_offset=896
    'data_len' type_id=1 bits_offset=928
    'tail' type_id=1 bits_offset=1472
    'end' type_id=1 bits_offset=1504
    'head' type_id=4 bits_offset=1536
    'data' type_id=4 bits_offset=1600
"""
        with tempfile.TemporaryDirectory() as tmp:
            btf = pathlib.Path(tmp) / "vmlinux"
            btf.write_bytes(b"btf")
            with mock.patch.object(standctl, "KERNEL_BTF", btf), \
                 mock.patch.object(standctl.shutil, "which", return_value="/usr/sbin/bpftool"), \
                 mock.patch.object(standctl.os, "uname", return_value=SimpleNamespace(
                     release="5.10.0-46-amd64", machine="x86_64")), \
                 mock.patch.object(standctl.subprocess, "run", return_value=subprocess.CompletedProcess(
                     [], 0, text, "")):
                result = standctl.kernel_layout()
        self.assertTrue(result["verified"])
        self.assertEqual(result["btf_sha256"], hashlib.sha256(b"btf").hexdigest())
        self.assertEqual(result["structs"]["pci_bus"]["fields"]["domain_nr"]["width"], 4)
        self.assertNotIn("text_base", result)
        self.assertNotIn("runtime_address", result)

    def test_ssh_peer_comes_from_server_connection_environment(self):
        self.assertEqual(
            standctl.ssh_peer("192.0.2.10 52344 192.0.2.20 22"), "192.0.2.10"
        )

    def test_malformed_ssh_connection_has_no_peer(self):
        self.assertIsNone(standctl.ssh_peer("192.0.2.10 22"))

    def test_remote_dispatch_accepts_only_read_only_commands(self):
        self.assertEqual(
            standctl.parse_remote_command("standctl wifi status --json"),
            ["wifi", "status", "--json"],
        )
        with self.assertRaises(ValueError):
            standctl.parse_remote_command("sh -c 'ip link set wlp5s0 down'")
        self.assertEqual(standctl.parse_remote_command("standctl tx preflight --json"),
                         ["tx", "preflight", "--json"])
        self.assertEqual(standctl.parse_remote_command("standctl tx capture-status --json"),
                         ["tx", "capture-status", "--json"])

    def test_remote_dispatch_rejects_state_changing_wireless_commands(self):
        for command in (
            "standctl wifi scan",
            "standctl wifi connect",
            "standctl wifi disconnect",
            "standctl wifi monitor",
            "standctl wifi capture",
            "standctl trace start",
        ):
            with self.subTest(command=command), self.assertRaises(ValueError):
                standctl.parse_remote_command(command)

    def test_bounded_output_marks_truncation(self):
        text, truncated = standctl.bound_text("abcdefgh", 5)
        self.assertTrue(truncated)
        self.assertEqual(text, "abcde\n[output truncated]")

    def test_log_redaction_removes_ssid_and_credentials(self):
        safe = standctl.redact("SSID: private-net\npsk=secret-value\nstate=connected")
        self.assertNotIn("private-net", safe)
        self.assertNotIn("secret-value", safe)
        self.assertIn("state=connected", safe)

    def test_remote_dispatch_rejects_shell_metacharacters_and_oversized_input(self):
        for command in (
            "standctl inventory; reboot",
            "standctl wifi link && touch /tmp/should-not-exist",
            "standctl logs collect $(id)",
            "x" * 1025,
        ):
            with self.subTest(command=command[:32]), self.assertRaises(ValueError):
                standctl.parse_remote_command(command)

    def test_artifact_read_only_accepts_only_generated_log_names(self):
        self.assertEqual(
            standctl.parse_remote_command("standctl artifact read stand-logs-20261015T120000Z.txt --json"),
            ["artifact", "read", "stand-logs-20261015T120000Z.txt", "--json"],
        )
        for name in ("../../etc/passwd", "anything.txt", "stand-logs-x.txt"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                standctl.parse_remote_command(f"standctl artifact read {name}")

    def test_state_changing_command_fails_without_invoking_tools(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"OPENBRCM_LAB_HOME": tmp}):
            with mock.patch.object(standctl, "run", side_effect=AssertionError("must not run")):
                self.assertEqual(standctl.main(["wifi", "scan"]), 2)

    def test_tx_preflight_without_installed_loader_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ, {"OPENBRCM_LAB_HOME": tmp}):
            result = standctl.tx_preflight()
        self.assertFalse(result["preflight_pass"])
        self.assertFalse(result["capture_enabled"])
        self.assertIn("root_preflight_status_missing_stale_or_mismatched", result["blockers"])

    def test_blocked_tx_preflight_has_nonzero_command_status(self):
        blocked = {"preflight_pass": False, "capture_enabled": False,
                   "blockers": ["resident_build_id"]}
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.dict(os.environ, {"OPENBRCM_LAB_HOME": tmp}), \
             mock.patch.object(standctl, "tx_preflight", return_value=blocked):
            self.assertEqual(standctl.main(["tx", "preflight", "--json"]), 1)

    def test_capture_status_does_not_claim_global_absence(self):
        with mock.patch.object(standctl, "trace_capabilities", return_value={"tracefs_present": True,
                           "kernel_config": {"CONFIG_KPROBES": True}}), \
             mock.patch.object(standctl, "modules", return_value={"wl_loaded": True}):
            status = standctl.tx_capture_status()
        self.assertEqual(status["capture_state"], "UNKNOWN")
        self.assertFalse(status["global_probe_absence_proven"])


if __name__ == "__main__":
    unittest.main()
