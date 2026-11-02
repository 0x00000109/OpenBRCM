#!/usr/bin/env python3
"""Static contract tests for the bounded observer proc readout."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "old/amd64/src/wl/sys/obsv_readout.c").read_text()
MAKEFILE = (ROOT / "old/amd64/Makefile").read_text()
WRAPPER = (ROOT / "old/amd64/src/wl/sys/wl_linux.c").read_text()


class ObserverReadoutContractTests(unittest.TestCase):
    def test_is_opt_in_and_separate_from_monitor(self):
        self.assertIn("ifeq ($(OBSERVER_READOUT),1)", MAKEFILE)
        self.assertIn("brcm_observer", SOURCE)
        self.assertNotIn("OBSERVER", WRAPPER[WRAPPER.index("wl_proc_read"):WRAPPER.index("wl_proc_write")])
        self.assertIn("obsv_readout_register(wl)", WRAPPER)
        self.assertIn("obsv_readout_unregister(wl)", WRAPPER)
        self.assertIn("observer_thunks.o", MAKEFILE)

    def test_readout_has_wrapper_type_context_before_wl_linux_header(self):
        self.assertLess(SOURCE.index("#include <wlc_pub.h>"), SOURCE.index("#include \"wl_linux.h\""))
        self.assertLess(SOURCE.index("#include <wl_iw.h>"), SOURCE.index("#include \"wl_linux.h\""))

    def test_has_bounded_ring_and_snapshot_validation(self):
        self.assertIn("#define OBSV_RING_CAPACITY 16U", SOURCE)
        self.assertIn("head > OBSV_RING_CAPACITY", SOURCE)
        self.assertIn("reason=overflow", SOURCE)
        self.assertIn('"sequence", index', SOURCE)
        self.assertIn('"torn", index', SOURCE)
        self.assertIn("*used = 0;", SOURCE)
        self.assertIn("READ_ONCE(obsv_capture_ring[index].sequence)", SOURCE)
        self.assertIn("smp_rmb()", SOURCE)

    def test_has_no_hardware_or_vendor_call_path(self):
        forbidden = ("wlc_ioctl", "WL_LOCK", "WL_UNLOCK", "osl_", "bcma_",
                     "pci_", "ioread", "iowrite", "readl", "writel")
        for token in forbidden:
            self.assertNotIn(token, SOURCE)

    def test_no_raw_pointer_is_emitted(self):
        self.assertIn("caller_delta", SOURCE)
        self.assertNotIn("%px", SOURCE)
        self.assertNotIn("%p", SOURCE)

    def test_endpoint_is_read_only_and_one_shot(self):
        self.assertIn("proc_create_data(name, 0400", SOURCE)
        self.assertIn("if (*offset > 0)", SOURCE)
        self.assertNotIn(".write", SOURCE)

    def test_snapshot_buffer_is_heap_bounded_not_kernel_stack_backed(self):
        self.assertIn("buffer = kmalloc(PAGE_SIZE, GFP_KERNEL)", SOURCE)
        self.assertIn("kfree(buffer)", SOURCE)
        self.assertIn("PAGE_SIZE, &used", SOURCE)
        self.assertNotIn("sizeof(buffer)", SOURCE)
        self.assertNotIn("char buffer[PAGE_SIZE]", SOURCE)

    def test_rejects_truncated_snapshot(self):
        self.assertIn("written >= capacity - *used", SOURCE)
        self.assertIn("rc = -EMSGSIZE", SOURCE)


if __name__ == "__main__":
    unittest.main()
