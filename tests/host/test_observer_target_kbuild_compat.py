#!/usr/bin/env python3
"""Regression checks for wrapper compatibility required by the stand kernel."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
LINUXVER = (ROOT / "old/amd64/src/include/linuxver.h").read_text()
OSL = (ROOT / "old/amd64/src/shared/linux_osl.c").read_text()
WL = (ROOT / "old/amd64/src/wl/sys/wl_linux.c").read_text()
IW = (ROOT / "old/amd64/src/wl/sys/wl_iw.c").read_text()
CFG = (ROOT / "old/amd64/src/wl/sys/wl_cfg80211_hybrid.c").read_text()
READOUT = (ROOT / "old/amd64/src/wl/sys/obsv_readout.c").read_text()


class TargetKbuildCompatTests(unittest.TestCase):
    def test_ioremap_nocache_has_linux56_compatibility_alias(self):
        self.assertIn("KERNEL_VERSION(5, 6, 0)", LINUXVER)
        self.assertIn("#define ioremap_nocache(addr, size) ioremap((addr), (size))", LINUXVER)

    def test_kernel_read_uses_modern_signature_from_linux414(self):
        self.assertIn("KERNEL_VERSION(4, 14, 0)", OSL)
        self.assertIn("kernel_read(fp, buf, len, &pos)", OSL)
        self.assertIn("fp->f_pos = pos", OSL)
        self.assertIn("kernel_read(fp, fp->f_pos, buf, len)", OSL)

    def test_linux510_path_has_no_address_limit_switching(self):
        for source in (WL, IW, CFG):
            self.assertNotRegex(source, r"\b(?:get_fs|set_fs|KERNEL_DS|segment_eq)\b")
        self.assertIn("wl_ioctl_kernel", WL)
        self.assertIn("return wl_ioctl_kernel(dev, &ioc);", IW)
        self.assertIn("return wl_ioctl_kernel(dev, &ioc);", CFG)

    def test_modern_timer_proc_and_timestamp_compatibility(self):
        self.assertIn("timer_setup(&t->timer, wl_timer_callback, 0)", WL)
        self.assertIn("from_timer(t, timer, timer)", WL)
        self.assertIn("struct proc_ops wl_fops", WL)
        self.assertIn("struct proc_ops obsv_fops", READOUT)
        self.assertNotIn("__DATE__", WL)
        self.assertNotIn("__TIME__", WL)
        self.assertIn("KERNEL_VERSION(4, 11, 0)", WL)


if __name__ == "__main__":
    unittest.main()
