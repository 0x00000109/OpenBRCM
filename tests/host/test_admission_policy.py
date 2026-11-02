#!/usr/bin/env python3
"""M7 admission-policy regression.

Pins the BCMA id table to the only D11 revision with an enabled profile and
re-implements Linux drivers/bcma/main.c bcma_bus_match() to prove that D11
revisions 17/23/24 are NOT_MATCHED (no probe call) after M7.  Also proves the
profile gate still runs before allocation, mode dispatch and any firmware,
DMA or PHY path.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/ob_main.c").read_text(encoding="utf-8")
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")

ANY_MANUF = 0xFFFF
ANY_ID = 0xFFFF
ANY_REV = 0xFF
ANY_CLASS = 0xFF
MANUF_BCM = 0x04BF
CORE_80211 = 0x0812

ENTRY_RE = re.compile(
    r"BCMA_CORE\(\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^)]+)\)")
COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)


def parse_int(token: str) -> int:
    token = token.strip()
    aliases = {
        "BCMA_MANUF_BCM": MANUF_BCM,
        "BCMA_CORE_80211": CORE_80211,
        "BCMA_ANY_MANUF": ANY_MANUF,
        "BCMA_ANY_ID": ANY_ID,
        "BCMA_ANY_REV": ANY_REV,
        "BCMA_ANY_CLASS": ANY_CLASS,
    }
    if token in aliases:
        return aliases[token]
    return int(token, 0)


def parse_table(text: str):
    start = text.index("ob_coreid_table[]")
    end = text.index("};", start)
    code = COMMENT_RE.sub("", text[start:end])
    entries = []
    for match in ENTRY_RE.finditer(code):
        entry = tuple(parse_int(part) for part in match.groups())
        if entry[:3] == (0, 0, 0):
            break
        entries.append(entry)
    return entries


def matches(entries, manuf, core_id, revision, cls):
    return any(
        (entry[0] == manuf or entry[0] == ANY_MANUF)
        and (entry[1] == core_id or entry[1] == ANY_ID)
        and (entry[2] == revision or entry[2] == ANY_REV)
        and (entry[3] == cls or entry[3] == ANY_CLASS)
        for entry in entries
    )


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:end]
    raise AssertionError(f"unterminated function: {signature}")


class AdmissionPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.entries = parse_table(MAIN)
        cls.probe = function_body(CORE, "int ob_probe(struct bcma_device *core)")

    def test_table_advertises_only_rev42(self):
        self.assertEqual([entry[2] for entry in self.entries], [42])
        self.assertEqual(len(self.entries), 1)
        entry = self.entries[0]
        self.assertEqual(entry[0], MANUF_BCM)
        self.assertEqual(entry[1], CORE_80211)
        self.assertEqual(entry[3], ANY_CLASS)

    def test_no_any_rev_wildcard(self):
        self.assertNotIn("BCMA_ANY_REV", COMMENT_RE.sub("", MAIN[
            MAIN.index("ob_coreid_table[]"):MAIN.index("};",
                MAIN.index("ob_coreid_table[]"))]))

    def test_matching_rule_after_m7(self):
        for revision in (17, 23, 24):
            self.assertFalse(matches(self.entries, MANUF_BCM, CORE_80211,
                                     revision, 0),
                             f"D11 rev{revision} must be NOT_MATCHED")
        self.assertTrue(matches(self.entries, MANUF_BCM, CORE_80211, 42, 0))
        self.assertTrue(matches(self.entries, MANUF_BCM, CORE_80211, 42, 1))
        self.assertFalse(matches(self.entries, 0x0000, CORE_80211, 42, 0))
        self.assertFalse(matches(self.entries, MANUF_BCM, 0x0811, 42, 0))

    def test_candidate_records_are_preserved_but_not_advertised(self):
        comment = COMMENT_RE.findall(MAIN[:MAIN.index("ob_coreid_table[]")])[-1]
        self.assertIn("Future compatibility candidates", comment)
        for revision in (17, 23, 24):
            self.assertIn(
                f"BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, {revision},",
                comment)

    def test_profile_gate_precedes_every_side_effect(self):
        resolve = self.probe.index("ob_chip_profile_resolve")
        for token in (
            "ob_isolated_mode_select22",
            "devm_kzalloc",
            "bcma_set_drvdata",
            "ob_si_probe(hw)",
            "ob_acphy_attach_identity(hw)",
            "ob_fw_probe(hw)",
            "ob_ucode_test(hw)",
        ):
            self.assertLess(resolve, self.probe.index(token), token)

    def test_rejection_returns_before_hardware_path(self):
        reject = self.probe[self.probe.index("if (profile_status != "
                                             "OB_CHIP_PROFILE_OK)")::]
        reject = reject[:reject.index("return -ENODEV;")]
        self.assertIn("device profile rejected", reject)
        self.assertIn("identity.d11_revision", reject)
        self.assertNotIn("ob_d11_read", reject)
        self.assertNotIn("ob_si_probe", reject)

    def test_no_firmware_dma_or_phy_before_gate(self):
        resolve = self.probe.index("ob_chip_profile_resolve")
        before = self.probe[:resolve]
        for token in ("ob_fw_probe", "ob_dma_", "ob_phy_", "ob_ucode"):
            self.assertNotIn(token, before, token)


if __name__ == "__main__":
    unittest.main()
