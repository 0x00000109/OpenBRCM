#!/usr/bin/env python3
"""Source-order regression for the fail-closed profile admission gate."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ChipProfileAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = (ROOT / "src/ob_core.c").read_text()
        cls.start = cls.core.index("int ob_probe(struct bcma_device *core)")
        # Comments inside ob_probe() name the very symbols this test orders
        # ("before devm_kzalloc(), before any driver-initiated MMIO"), so blank
        # them out while preserving offsets: otherwise the comparison measures
        # prose rather than code order.
        cls.probe = re.sub(
            r"/\*.*?\*/|//[^\n]*",
            lambda m: " " * len(m.group(0)),
            cls.core[cls.start:],
            flags=re.S,
        )

    def mode_select_index(self):
        """Locate the isolated-mode selector whatever its generation suffix.

        The selector carries a generation number (ob_isolated_mode_select24 as
        of this writing) that advances whenever the arbitration gains a mode.
        The ordering property under test does not depend on which generation is
        current, so pinning the literal would only make this test rot.
        """
        match = re.search(r"ob_isolated_mode_select\d+\(", self.probe)
        self.assertIsNotNone(match, "no isolated-mode selector in ob_probe()")
        return match.start()

    def test_profile_gate_precedes_mode_selection_and_dispatch(self):
        resolve = self.probe.index("ob_chip_profile_resolve")
        mode_select = self.mode_select_index()
        allocation = self.probe.index("devm_kzalloc")
        drvdata = self.probe.index("bcma_set_drvdata(core, hw)")
        first_test_dispatch = self.probe.index("ob_ucode_test(hw)")
        normal_si = self.probe.index("ob_si_probe(hw)")
        self.assertLess(resolve, mode_select)
        self.assertLess(resolve, allocation)
        self.assertLess(resolve, drvdata)
        self.assertLess(resolve, first_test_dispatch)
        self.assertLess(resolve, normal_si)

    def test_gate_uses_cached_pci_and_bcma_identity_without_hardware_reads(self):
        resolve = self.probe.index("ob_chip_profile_resolve")
        gate = self.probe[max(0, resolve - 1000):resolve + 800]
        for expression in (
            "core->bus->host_pci",
            "pdev->vendor",
            "pdev->device",
            "pdev->revision",
            "core->bus->chipinfo.id",
            "core->bus->chipinfo.rev",
            "core->id.rev",
        ):
            self.assertIn(expression, gate)
        self.assertNotIn("pci_read_config", gate)
        self.assertNotIn("ob_d11_read", gate)

    def test_profile_pointer_is_per_device_and_const(self):
        core_h = (ROOT / "src/ob_core.h").read_text()
        self.assertIn("const struct ob_chip_profile *profile", core_h)
        self.assertNotIn("active_chip_profile", self.core)

    def test_later_phy_and_radio_checks_remain(self):
        lifecycle = (ROOT / "src/ob_lifecycle.c").read_text()
        phy = (ROOT / "src/ob_phy.c").read_text()
        self.assertIn("hw->phy_type = (u8)((phyver >> 8) & 0x0fu)", lifecycle)
        self.assertIn("!radio.id_is_2069 || radio.radio_rev != 4u", lifecycle)
        self.assertIn("hw->phy_type != 0x0b", phy)
        self.assertIn("hw->radio_id != 0x2069", phy)

    def test_profile_does_not_fold_board_subsystem_into_chip_identity(self):
        # M6 moved the interface to ob_profile.h.  Board evidence is present
        # there as a separate group, but the resolver must never consult it:
        # admission is based on PCI/silicon/D11 identity only.
        profile = (ROOT / "src/ob_profile.h").read_text()
        resolver = (ROOT / "src/chip/ob_profile.c").read_text()
        self.assertIn("subsystem_known", profile)
        self.assertIn("never silicon identity", profile)
        self.assertNotIn("subsystem", resolver.lower())


if __name__ == "__main__":
    unittest.main()
