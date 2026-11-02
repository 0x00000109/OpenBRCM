#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Backend-context discipline for the FIFO530 stage (regression guard).

The defect this test exists for
-------------------------------
ob_d3a1_fifo_fixup() built the stage ops with `.ctx = &fctx`, where fctx was
`struct ob_d3a1_fifo530_ctx { struct ob_hw *hw; }`, while the MMIO adapters
cast the same ctx to `struct ob_hw *`. Both compiled without a warning: the
field is `void *`. At runtime the adapters would read `ob_hw.core` and
`ob_hw.dev_lost` out of the wrapper's bytes (i.e. out of the stored ob_hw
pointer itself) and issue MMIO at a bogus base.

Why a source test and not a unit test
-------------------------------------
The adapters are `static` functions in the kernel translation unit; the host
integration test supplies its own mock and therefore never exercises the cast.
A compile-time check cannot help either, because the callback typedef takes
`void *`. So the invariant is pinned textually, which does catch exactly the
shape of the original bug.

Invariant checked
-----------------
1. The kernel stage initialiser uses `.ctx = hw;` and no `.ctx = &...` exists
   anywhere in ob_d3a1.c.
2. Every callback assigned to that ops resides in ob_d3a1.c and treats ctx as
   `struct ob_hw *` (`(struct ob_hw *)ctx` or `struct ob_hw *hw = ctx;`).
3. No wrapper struct over ob_hw is declared in ob_d3a1.c.
4. ob_d3a1_poll16() hands `hw` itself to ob_d3a1_poll_core().

Exit status: 0 when every check holds, 1 otherwise.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "src", "ob_d3a1.c"))

CALLBACK_FIELDS = ("read16", "write16", "dev_lost", "on_complete",
                   "on_expired_continued")


def initializer(text):
    m = re.search(r"struct ob_d3a1_fifo530_ops ops = \{(.*?)\n\t\};", text, re.S)
    if not m:
        raise SystemExit("FAIL: no `struct ob_d3a1_fifo530_ops ops = {...}`")
    return m.group(1)


def function_body(text, name):
    m = re.search(r"\n(static [^{;]*?\b%s\s*\([^)]*\)\s*\{)" % re.escape(name),
                  text, re.S)
    if not m:
        return None
    start = m.end() - 1
    depth = 0
    for k in range(start, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[start:k + 1]
    return None


def main():
    text = open(SRC).read()
    fails = []

    init = initializer(text)
    if not re.search(r"\.ctx\s*=\s*hw\s*,", init):
        fails.append("kernel stage initialiser does not use `.ctx = hw;`")
    if ".ctx = &" in text:
        fails.append("a FIFO530 ops context is a wrapper address (`.ctx = &...`)")
    if re.search(r"struct\s+ob_d3a1_fifo530_ctx\b", text):
        fails.append("a wrapper struct over ob_hw is declared (type confusion)")
    if "fctx" in text:
        fails.append("`fctx` is back; ops->ctx must be the ob_hw pointer itself")

    assigned = {}
    for field in CALLBACK_FIELDS:
        m = re.search(r"\.%s\s*=\s*(\w+)\s*," % field, init)
        if not m:
            fails.append("ops.%s is not assigned in the stage initialiser" % field)
            continue
        assigned[field] = m.group(1)

    for field, fn in sorted(assigned.items()):
        body = function_body(text, fn)
        if body is None:
            fails.append("callback %s (ops.%s) is not defined in ob_d3a1.c"
                         % (fn, field))
            continue
        ok_ctx = ("(struct ob_hw *)ctx" in body or
                  "(const struct ob_hw *)ctx" in body or
                  re.search(r"struct ob_hw \*hw = ctx;", body) is not None)
        if not ok_ctx:
            fails.append("callback %s (ops.%s) does not treat ctx as "
                         "struct ob_hw *" % (fn, field))

    body = function_body(text, "ob_d3a1_poll16")
    if body is None or "ob_d3a1_poll_core(ob_d3a1_poll_read, " not in body:
        fails.append("ob_d3a1_poll16 does not delegate to ob_d3a1_poll_core")
    elif not re.search(r"ob_d3a1_poll_delay,\s*hw,", body):
        fails.append("ob_d3a1_poll16 does not pass `hw` as the backend context")

    for f in fails:
        print("FAIL: " + f)
    if fails:
        return 1
    print("test_d3a1_fifo530_ctx: PASS (ops->ctx is struct ob_hw * everywhere)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
