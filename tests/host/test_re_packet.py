#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for the additive `re packet` command.

These tests exercise the canonical Rust `re` tool against the real re.db only
when the tooling workspace is present; otherwise they SKIP (the blob/re.db are
not part of the OpenBRCM repository). They never touch hardware.
"""
import json
import os
import subprocess
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))

_DEFAULT_TOOLING = "/media/kartashoff/Storage/opensource/iced/test"
_TOOLING = os.environ.get("RE_TOOLING_ROOT", _DEFAULT_TOOLING)
_RE = os.path.join(_TOOLING, "binary_analyzer", "target", "release", "re")
_DB = os.path.join(_TOOLING, "re.db")
_WRAPPER = os.path.join(_ROOT, "scripts", "re.sh")

_HAVE = os.path.isfile(_RE) and os.path.isfile(_DB) and os.path.isfile(_WRAPPER)


def re_run(*args):
    # Run from the tooling workspace so the relative blob path in re.db resolves.
    return subprocess.run(
        [_RE, *args, "--db", _DB], cwd=_TOOLING, capture_output=True, text=True
    )


@unittest.skipUnless(_HAVE, "canonical re tooling / re.db not present")
class PacketTests(unittest.TestCase):
    FN = "wlc_phy_switch_radio_acphy"
    ADDR = "0xaa782"

    def packet(self, *args):
        r = re_run("packet", *args)
        self.assertEqual(r.returncode, 0, r.stderr)
        return json.loads(r.stdout)

    def test_known_function_target(self):
        p = self.packet("--fn", self.FN)
        self.assertEqual(p["packet"], "openbrcm-re-packet/v1")
        self.assertEqual(p["target"]["name"], self.FN)
        self.assertEqual(p["target"]["addr"], self.ADDR)
        self.assertIn("schema_version", p["meta"])
        self.assertEqual(p["meta"]["schema_version"], 5)

    def test_address_target_matches_name(self):
        by_name = self.packet("--fn", self.FN)["target"]
        by_addr = self.packet("--addr", self.ADDR)["target"]
        self.assertEqual(by_name, by_addr)

    def test_callers_and_callees(self):
        p = self.packet("--fn", self.FN, "--callers", "--callees")
        self.assertGreaterEqual(p["callers"]["total"], 1)
        self.assertEqual(p["callers"]["items"][0]["caller"], "wlc_phy_switch_radio")
        self.assertGreaterEqual(p["callees"]["total"], 1)

    def test_mmio_fields_branches_indirect_present(self):
        p = self.packet("--fn", "sub_67efd", "--mmio", "--fields", "--branches",
                        "--indirect")
        for key in ("mmio", "fields", "branches", "indirect"):
            self.assertIn(key, p)
            self.assertIn("total", p[key])

    def test_relocation_backed_field_confidence_preserved(self):
        # phy+0xF8 is a relocation-covered install; the packet must carry the
        # index confidence verbatim (never upgrade to EXACT by itself).
        p = self.packet("--fn", "wlc_phy_attach_acphy", "--fields", "--max-sites", "400")
        vals = [f for f in p["fields"]["items"]
                if f["field_offset"] == 0xF8 and f["kind"] == "store"]
        self.assertTrue(vals)
        self.assertIn(vals[0]["confidence"], ("EXACT", "CONDITIONAL"))

    def test_truncation_is_explicit(self):
        p = self.packet("--fn", self.FN, "--constants", "--max-sites", "3")
        c = p["constants"]
        self.assertEqual(c["shown"], 3)
        self.assertGreater(c["total"], 3)
        self.assertTrue(c["truncated"])
        self.assertEqual(len(c["items"]), 3)

    def test_unresolved_item_surfaced(self):
        # A relocation-free indirect slot must be reported as unresolved with a
        # reason, not omitted.
        p = self.packet("--fn", "wlc_bmac_init", "--indirect", "--max-sites", "50")
        unresolved = [i for i in p["indirect"]["items"]
                      if i.get("confidence") == "UNRESOLVED" or not i.get("candidate_name")]
        self.assertTrue(unresolved)

    def test_deterministic_json(self):
        a = re_run("packet", "--fn", self.FN)
        b = re_run("packet", "--fn", self.FN)
        self.assertEqual(a.stdout, b.stdout)

    def test_existing_cli_still_works(self):
        r = re_run("fn", self.FN)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(self.FN, r.stdout)


if __name__ == "__main__":
    unittest.main()
