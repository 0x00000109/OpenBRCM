#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for scripts/decode_radio_probe.py (no hardware).

Covers the decoder acceptance and every rejection class required by
D4-BLOCKER-PLL-BRANCH-HW-PROBE Part H/M: malformed log, missing values,
duplicate conflicting values, candidate mismatch, impossible extraction,
explicit dev_lost/fault and unexpected radio id.
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import decode_radio_probe as drp  # noqa: E402


def good_log(reg0, reg1, candidate=None, module_sha=None):
    dec = drp.decode(reg0, reg1)
    lines = [
        "openbrcm: chip 0x4352 rev 3, d11 core rev 42",
        "Linux version 7.0.0-34-generic (buildd@lcy02) #34",
        "radio-probe: BEGIN",
        "radio-probe: prep host up (host_is_pcie2=1)",
        "radio-probe: clkctlst=070b0042 HAVEHT=1 core_enabled=1",
        "radio-probe: ioctl=00000010 mpclke=1",
    ]
    if candidate is not None:
        lines.append("radio-probe: candidate=%s" % candidate)
    if module_sha is not None:
        lines.append("radio-probe: module_sha256=%s" % module_sha)
    lines += [
        "radio-probe: pre_access=0x04020402",
        "radio-probe: reg0_raw=0x%04x" % reg0,
        "radio-probe: reg1_raw=0x%04x" % reg1,
        "radio-probe: radioid=0x%04x" % dec["radio_id"],
        "radio-probe: radiorev=0x%02x" % dec["radio_rev"],
        "radio-probe: revision_class=%d" % dec["revision_class"],
        "radio-probe: id_accepted=%d id_is_2069=%d" % (
            1 if dec["id_accepted"] else 0,
            1 if dec["id_is_2069"] else 0),
        "radio-probe: pll_branch=%s" % dec["pll_branch"],
        "radio-probe: post_access=0x04020402",
        "radio-probe: PASS",
        "radio-probe: STOPPED BEFORE PLL/RADIO INIT",
    ]
    return "\n".join(lines) + "\n"


def parse_and_build(text, **kw):
    fields = drp.parse_log(text)
    return drp.build_artifact(fields, text.splitlines(), **kw)


class DecoderTest(unittest.TestCase):
    def test_accept_branch_A(self):
        art = parse_and_build(good_log(0x0010, 0x2069))
        self.assertEqual(art["status"], "PASS")
        self.assertEqual(art["decoded"]["pll_branch"], "A")
        self.assertEqual(art["decoded"]["revision_class"], 1)
        self.assertEqual(art["identity"]["radio_id"], "0x2069")
        self.assertEqual(art["state_transition"]["selector_to"],
                         "PROVEN_A")
        self.assertFalse(art["state_transition"]["applied"])

    def test_accept_branch_B(self):
        art = parse_and_build(good_log(0x0020, 0x2069))
        self.assertEqual(art["decoded"]["pll_branch"], "B")
        self.assertEqual(art["state_transition"]["selector_to"],
                         "PROVEN_B")

    def test_accept_skip_class0(self):
        art = parse_and_build(good_log(0x0003, 0x2069))
        self.assertEqual(art["decoded"]["pll_branch"], "SKIP")
        self.assertEqual(art["state_transition"]["selector_to"],
                         "PROVEN_SKIP")

    def test_kernel_identity_present(self):
        art = parse_and_build(good_log(0x0010, 0x2069))
        self.assertIn("7.0.0-34-generic", art["kernel"])

    def test_reject_missing_begin(self):
        text = good_log(0x0010, 0x2069).replace("radio-probe: BEGIN\n", "")
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text)

    def test_reject_missing_pass(self):
        text = good_log(0x0010, 0x2069).replace("radio-probe: PASS\n", "")
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text)

    def test_reject_missing_value(self):
        text = good_log(0x0010, 0x2069)
        text = text.replace("radio-probe: reg1_raw=0x2069\n", "")
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text)

    def test_reject_duplicate_conflict(self):
        text = good_log(0x0010, 0x2069)
        text = text.replace(
            "radio-probe: radiorev=0x10\n",
            "radio-probe: radiorev=0x10\nradio-probe: radiorev=0x20\n")
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text)

    def test_reject_dev_lost(self):
        text = "openbrcm: DEVICE LOST (radio-probe) - reboot required\n"
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text)

    def test_reject_fail_line(self):
        text = good_log(0x0010, 0x2069)
        text = text.replace("radio-probe: PASS\n",
                            "radio-probe: FAIL dev_lost\n")
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text)

    def test_reject_impossible_extraction_rev(self):
        text = good_log(0x0010, 0x2069)
        text = text.replace("radio-probe: radiorev=0x10\n",
                            "radio-probe: radiorev=0x99\n")
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text)

    def test_reject_impossible_extraction_branch(self):
        text = good_log(0x0010, 0x2069)  # class 1 -> A
        text = text.replace("radio-probe: pll_branch=A\n",
                            "radio-probe: pll_branch=B\n")
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text)

    def test_reject_all_ones_pair(self):
        with self.assertRaises(drp.DecodeError):
            parse_and_build(good_log(0xffff, 0xffff))

    def test_reject_unexpected_radio_id(self):
        # a plausible but non-AC id must be rejected
        with self.assertRaises(drp.DecodeError):
            parse_and_build(good_log(0x0010, 0x1234))

    def test_reject_candidate_mismatch(self):
        text = good_log(0x0010, 0x2069, candidate="aa" * 32)
        with self.assertRaises(drp.DecodeError):
            parse_and_build(text, expect_candidate="bb" * 32)

    def test_accept_candidate_match(self):
        sha = "ab" * 32
        text = good_log(0x0010, 0x2069, candidate=sha)
        art = parse_and_build(text, expect_candidate=sha)
        self.assertEqual(art["candidate"], sha)


if __name__ == "__main__":
    unittest.main(verbosity=2)
