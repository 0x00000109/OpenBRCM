#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Deterministic source audit for the central device-loss guard.

Proves (no hardware) that every D11/ChipCommon/AXI register read/write in the
driver goes through the guard wrappers in src/ob_guard.{c,h}, that the trusted
detector is wired into the D3B/D3A1 path, and that the core/reset operations are
guarded. If a future caller reintroduces a raw bcma access, this test fails.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src")

RAW = re.compile(r"\bbcma_(a)?(read|write)(8|16|32)\s*\(")
GUARD_BACKEND = "ob_guard.c"
# Core ops that are not plain register windows but must also be gated.
CORE_OPS = ("bcma_core_disable", "bcma_core_enable", "bcma_host_pci_up",
            "bcma_core_set_clockmode")


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def strip_comments(text):
    """Remove C block and line comments so a mention in prose is not a site.

    Block comments are replaced by the same number of newlines so reported
    line numbers still match the original file.
    """
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"),
                  text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def fail(msg):
    print("FAIL: " + msg)
    return 1


def main():
    failures = 0

    # 1. No raw bcma register access outside the guard backend.
    offenders = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".c") or name == GUARD_BACKEND:
            continue
        body = strip_comments(read(os.path.join(SRC, name)))
        for m in RAW.finditer(body):
            line = body[:m.start()].count("\n") + 1
            offenders.append("%s:%d %s" % (name, line, m.group(0)))
    if offenders:
        failures += fail("raw bcma register access outside %s:\n  %s"
                          % (GUARD_BACKEND, "\n  ".join(offenders)))
    else:
        print("ok: no raw bcma register access outside %s" % GUARD_BACKEND)

    # 2. The guard backend supplies all windows.
    backend = read(os.path.join(SRC, "ob_guard.c"))
    for sym in ("ob_d11_read32", "ob_d11_write32", "ob_d11_read16",
                "ob_d11_write16", "ob_d11_read32_trusted", "ob_axi_read32",
                "ob_axi_write32", "ob_cc_read32", "ob_cc_read16",
                "ob_cc_write32"):
        if sym not in backend:
            failures += fail("ob_guard.c missing %s" % sym)
    if not failures:
        print("ok: ob_guard.c implements all guarded windows")

    # 3. The pure header exposes the sentinels and the monotonic latch rule.
    header = read(os.path.join(SRC, "ob_guard.h"))
    for sym in ("OB_GUARD_DEAD32", "OB_GUARD_DEAD16", "OB_GUARD_ALL_ONES32",
                "ob_guard_do_read32", "ob_guard_do_write32",
                "ob_guard_do_read16", "ob_guard_do_write16",
                "ob_guard_next_latch"):
        if sym not in header:
            failures += fail("ob_guard.h missing %s" % sym)
    if not failures:
        print("ok: ob_guard.h exposes the pure guard layer")

    # 4. The D3B/D3A1 detection path uses the trusted latch helper.
    d3b = read(os.path.join(SRC, "ob_d3b.c"))
    d3a1 = read(os.path.join(SRC, "ob_d3a1.c"))
    if "ob_d11_read32_trusted" not in d3b:
        failures += fail("ob_d3b.c does not use ob_d11_read32_trusted")
    if "ob_d11_read32_trusted" not in d3a1:
        failures += fail("ob_d3a1.c does not use ob_d11_read32_trusted")
    if "ob_dev_lost_is_latched(hw)" not in d3b:
        failures += fail("ob_d3b.c failure path does not honour the latch")
    if not failures:
        print("ok: D3B/D3A1 detection uses the trusted latch helper")

    # 5. Core/reset operations are gated on the latch.
    d3a0 = read(os.path.join(SRC, "ob_d3a0.c"))
    d3a0_core = re.search(r"ob_d3a0_core_contain\(.*?\n\{.*?\n\}", d3a0, re.S)
    if not d3a0_core or "dev_lost" not in d3a0_core.group(0):
        failures += fail("ob_d3a0_core_contain is not gated on dev_lost")
    for fname, fn in (("ob_si.c", "ob_si_powerup"), ("ob_ucode.c", "ob_ucode_prepare")):
        body = read(os.path.join(SRC, fname))
        m = re.search(re.escape(fn) + r"\(.*?\n\{.*?\n\}", body, re.S)
        if not m or "dev_lost" not in m.group(0):
            failures += fail("%s is not gated on dev_lost" % fn)
    if not failures:
        print("ok: core/host reset+bring-up operations are gated on the latch")

    # 6. The guard backend is part of the module build.
    mk = read(os.path.join(ROOT, "Makefile"))
    if "src/ob_guard.o" not in mk:
        failures += fail("Makefile openbrcm-y is missing src/ob_guard.o")
    else:
        print("ok: src/ob_guard.o is in the module build")

    # 7. The host guard test exists and is wired into `make hosttest`.
    thm = read(os.path.join(ROOT, "tests", "host", "Makefile"))
    if "ob_guard_test" not in thm:
        failures += fail("tests/host/Makefile does not build/run ob_guard_test")
    else:
        print("ok: ob_guard_test is wired into make hosttest")

    if failures:
        print("== test_device_lost_guard FAIL ==")
        return 1
    print("== test_device_lost_guard PASS ==")
    return 0


if __name__ == "__main__":
    sys.exit(main())
