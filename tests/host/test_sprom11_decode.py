#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for scripts/sprom11_decode.py (D3B MHF3 evidence decoder)."""
import importlib.util
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_DECODER = os.path.join(_HERE, "..", "..", "scripts", "sprom11_decode.py")
_spec = importlib.util.spec_from_file_location("sprom11_decode", _DECODER)
sd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sd)

WORDS = sd.WORDS_R11


def make_image(rev=11, seed=0):
    w = [(seed + i) & 0xFFFF for i in range(WORDS)]
    w[WORDS - 1] = (0 << 8) | rev
    crc = sd.sprom_crc(w)
    w[WORDS - 1] = (crc << 8) | rev
    return w


def to_text(w, header=None):
    if header is None:
        crc = (w[WORDS - 1] >> 8) & 0xFF
        rev = w[WORDS - 1] & 0xFF
        header = {"words": WORDS, "revision": rev, "crc": crc,
                  "calc": crc, "valid": 1 if rev == 11 else 0}
    return sd.serialize(header, w)


class ParseTests(unittest.TestCase):
    def test_parse_roundtrip(self):
        w = make_image()
        h, got = sd.parse_log(to_text(w))
        self.assertEqual(got, w)
        self.assertEqual(h["words"], WORDS)
        v = sd.validate(h, got)
        self.assertTrue(v["ok"], v["errors"])

    def test_serialize_deterministic(self):
        w = make_image()
        t1 = to_text(w)
        h, got = sd.parse_log(t1)
        self.assertEqual(sd.serialize(h, got), t1)

    def test_truncated_image_rejected(self):
        w = make_image()
        t = to_text(w)
        lines = [ln for ln in t.splitlines() if "005" not in ln]
        # remove one word from the middle data line deterministically
        t2 = t.replace(" %04x" % w[5], "", 1)
        with self.assertRaises(sd.SpromError):
            sd.parse_log(t2)

    def test_bad_word_count_header_rejected(self):
        w = make_image()
        t = to_text(w, header={"words": 233, "revision": 11, "crc": 0,
                               "calc": 0, "valid": 1})
        with self.assertRaises(sd.SpromError):
            sd.parse_log(t)

    def test_malformed_word_rejected(self):
        w = make_image()
        t = to_text(w).replace(" %04x" % w[10], " zzzz", 1)
        with self.assertRaises(sd.SpromError):
            sd.parse_log(t)

    def test_missing_begin_rejected(self):
        with self.assertRaises(sd.SpromError):
            sd.parse_log("nothing here\n")

    def test_crc_failure_detected(self):
        w = make_image()
        h, got = sd.parse_log(to_text(w))
        got[7] ^= 0xFFFF  # corrupt without updating CRC
        v = sd.validate(h, got)
        self.assertFalse(v["ok"])
        self.assertIn("crc", v["errors"])
        self.assertFalse(v["crc_match"])

    def test_revision_not_11_rejected(self):
        w = make_image(rev=10)
        h, got = sd.parse_log(to_text(w))
        v = sd.validate(h, got)
        self.assertIn("revision", v["errors"])
        self.assertFalse(v["ok"])


class FieldDecodeTests(unittest.TestCase):
    """Decode the proven rev11 field map from synthetic 234-word images."""

    def _image(self):
        return make_image()

    def test_boardtype_word(self):
        w = self._image()
        w[0x02] = 0x008F
        f = sd.decode_field(w, "boardtype", sd.PROVEN_REV11_FIELDS["boardtype"])
        self.assertEqual(f["value"], 0x8F)
        self.assertTrue(f["present"])

    def test_aa2g_aa5g_split(self):
        w = self._image()
        w[0x50] = (0x03 << 8) | 0x07
        a2 = sd.decode_field(w, "aa2g", sd.PROVEN_REV11_FIELDS["aa2g"])
        a5 = sd.decode_field(w, "aa5g", sd.PROVEN_REV11_FIELDS["aa5g"])
        self.assertEqual((a2["value"], a5["value"]), (7, 3))

    def test_boardflags_continuation(self):
        w = self._image()
        w[0x42] = 0x0008
        w[0x43] = 0x4000
        f = sd.decode_field(w, "boardflags",
                            sd.PROVEN_REV11_FIELDS["boardflags"])
        self.assertEqual(f["value"], 0x40000008)

    def test_antswitch_present_and_absent(self):
        w = self._image()
        w[0x54] = 0x0500
        f = sd.decode_field(w, "antswitch",
                            sd.PROVEN_REV11_FIELDS["antswitch"])
        self.assertEqual((f["value"], f["present"]), (5, True))
        w[0x54] = 0xFF00
        f = sd.decode_field(w, "antswitch",
                            sd.PROVEN_REV11_FIELDS["antswitch"])
        self.assertEqual(f["value"], 0xFF)
        self.assertFalse(f["present"])

    def test_zero_image(self):
        w = [0] * WORDS
        for name in ("boardtype", "aa2g", "aa5g", "antswitch"):
            f = sd.decode_field(w, name, sd.PROVEN_REV11_FIELDS[name])
            self.assertEqual(f["value"], 0)
        # antswitch==0 -> type 1 iff BFL_RFANTS, else 0 (zero image -> 0).
        self.assertEqual(sd.antsel_type(0, 0, 0, 0, 0), (0, False))

    def test_mhf3_end_to_end_from_image(self):
        w = self._image()
        w[0x02] = 0x0005          # boardtype > 3
        w[0x54] = 0x0100          # antswitch = 1 -> type 2
        w[0x50] = 0x0000
        vals = {n: sd.decode_field(w, n, sd.PROVEN_REV11_FIELDS[n])["value"]
                for n in ("boardtype", "boardflags", "antswitch", "aa2g",
                          "aa5g")}
        atype, avail = sd.antsel_type(*(vals[n] for n in
                                        ("boardtype", "boardflags", "antswitch",
                                         "aa2g", "aa5g")))
        self.assertEqual((atype, avail, sd.mhf3(atype)), (2, False, 0x3))

    def test_field_map_never_has_unknown_provenance(self):
        for name, spec in sd.PROVEN_REV11_FIELDS.items():
            self.assertIn("provenance", spec, name)
            self.assertTrue(spec["provenance"], name)
            self.assertIn("parts", spec, name)
            for part in spec["parts"]:
                self.assertLess(part["word"], sd.WORDS_R11, name)


class AntselTests(unittest.TestCase):
    def test_antsel_type_vectors(self):
        cases = [
            # (boardtype, boardflags, antswitch, aa2g, aa5g) -> (type, avail)
            ((0x0, 0x0, 0x0, 0x0, 0x0), (0, False)),
            ((0x0, 0x8, 0x0, 0x0, 0x0), (1, True)),   # L_bf BFL_RFANTS
            ((0x3, 0x8, 0x9, 0x0, 0x0), (1, True)),
            ((0x4, 0x0, 0x0, 0x7, 0x0), (2, True)),   # L_bt0
            ((0x4, 0x0, 0x0, 0x6, 0x0), (0, False)),
            ((0x5, 0x0, 0x1, 0x7, 0x0), (2, True)),   # {1,2,3}->2
            ((0x5, 0x0, 0x3, 0x0, 0x0), (2, False)),
            ((0x5, 0x0, 0x4, 0x0, 0x6), (3, True)),   # {4}->3
            ((0x5, 0x0, 0x5, 0x0, 0x7), (4, True)),   # {5}->4
            ((0x5, 0x0, 0x6, 0x6, 0x0), (5, True)),   # {6}->5
            ((0x5, 0x0, 0x7, 0x6, 0x0), (6, True)),   # {7}->6
            ((0x5, 0x0, 0x8, 0x0, 0x0), (0, False)),  # >7
        ]
        for args, want in cases:
            self.assertEqual(sd.antsel_type(*args), want, args)

    def test_mhf3_mapping(self):
        self.assertEqual(sd.mhf3(0), 0x0)
        self.assertEqual(sd.mhf3(1), 0x1)
        self.assertEqual(sd.mhf3(2), 0x3)
        self.assertEqual(sd.mhf3(3), 0x3)
        self.assertEqual(sd.mhf3(4), 0x0)
        self.assertEqual(sd.mhf3(5), 0x0)
        self.assertEqual(sd.mhf3(6), 0x3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
