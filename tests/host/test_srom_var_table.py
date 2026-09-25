#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for scripts/srom_var_table.py (vendor NVRAM table decoder).

These tests decode the *pinned* vendor blob, so they skip cleanly when the
blob or pyelftools is unavailable (e.g. CI without the RE workspace).
"""
import importlib.util
import json
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_TOOL = os.path.join(_HERE, "..", "..", "scripts", "srom_var_table.py")
_spec = importlib.util.spec_from_file_location("srom_var_table", _TOOL)
sv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sv)

BLOB = os.environ.get("OPENBRCM_RE_BLOB", sv.DEFAULT_BLOB)


def _blob_available():
    if not os.path.exists(BLOB):
        return False
    try:
        import elftools  # noqa: F401
    except Exception:
        return False
    return True


@unittest.skipUnless(_blob_available(),
                     "pinned vendor blob / pyelftools unavailable")
class TableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.meta, cls.entries = sv.parse_table(BLOB)
        cls.fields = sv.fields_for_revision(cls.entries, 11)
        cls.by_name = {}
        for f in cls.fields:
            cls.by_name.setdefault(f["name"], []).append(f)

    def test_pinned_identity_and_shape(self):
        self.assertEqual(self.meta["blob_sha256"], sv.BLOB_SHA256)
        self.assertEqual(self.meta["table"], ".rodata+0x1b00")
        self.assertEqual(self.meta["entry_size"], 24)
        self.assertEqual(self.meta["entry_count"], 573)

    def test_required_rev11_fields(self):
        want = {
            "boardtype": (0x02, 0xFFFF, 0, 16, False),
            "aa2g": (0x50, 0x00FF, 0, 8, False),
            "aa5g": (0x50, 0xFF00, 8, 8, False),
            "antswitch": (0x54, 0xFF00, 8, 8, True),
        }
        for name, (w, m, sh, wd, skip) in want.items():
            self.assertIn(name, self.by_name, name)
            f = self.by_name[name][0]
            self.assertEqual((f["word_index"], f["mask"], f["shift"],
                              f["width"], f["skip_all_ones"]),
                             (w, m, sh, wd, skip), name)

    def test_boardflags_is_continuation_pair(self):
        f = self.by_name["boardflags"][0]
        self.assertEqual([p["word_index"] for p in f["parts"]], [0x42, 0x43])
        value, present = sv.field_value([0xFFFF] * 234, f)
        self.assertTrue(present)
        self.assertEqual(value, 0xFFFFFFFF)
        words = [0] * 234
        words[0x42] = 0x0008
        words[0x43] = 0x0001
        value, present = sv.field_value(words, f)
        self.assertEqual(value, 0x00010008)

    def test_rev_gate_excludes_other_revision_macaddr(self):
        # macaddr has rev 8-10 entries (table[167]) and a rev>=11 entry
        # (table[377]); only the rev>=11 one must be selected.
        self.assertEqual([f["table_entry"] for f in self.by_name["macaddr"]],
                         [377])
        self.assertEqual(self.by_name["macaddr"][0]["word_index"], 0x48)

    def test_skip_all_ones_absent(self):
        f = self.by_name["antswitch"][0]
        words = [0] * 234
        words[0x54] = 0xFF00  # antswitch byte all-ones
        value, present = sv.field_value(words, f)
        self.assertEqual(value, 0xFF)
        self.assertFalse(present)
        words[0x54] = 0x0500
        value, present = sv.field_value(words, f)
        self.assertEqual((value, present), (5, True))

    def test_synthesize_required_variables(self):
        words = [0] * 234
        words[0x02] = 0x008F           # boardtype
        words[0x42] = 0x0008           # boardflags lo (BFL_RFANTS)
        words[0x43] = 0x0000
        words[0x50] = (0x03 << 8) | 0x07  # aa5g=3, aa2g=7
        words[0x54] = (0x01 << 8) | 0x0022  # antswitch=1, rx=2, tx=2
        syn = sv.synthesize(words, self.entries, 11)
        for name, want in (("boardtype", 0x8F), ("boardflags", 0x0008),
                           ("aa2g", 7), ("aa5g", 3), ("antswitch", 1),
                           ("txchain", 2), ("rxchain", 2)):
            self.assertEqual(syn[name]["value"], want, name)
            self.assertTrue(syn[name]["present"], name)

    def test_sha_mismatch_rejected(self):
        with self.assertRaises(sv.TableError):
            sv.parse_table(BLOB, expected_sha256="00" * 32)


if __name__ == "__main__":
    unittest.main(verbosity=2)
