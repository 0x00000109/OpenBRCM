#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Static regression for the rev42 diagnostic addressing path.

INVARIANT
---------
The active ob_diag D11 core/wrapper addressing must come from the enumerated
`struct bcma_device` (core->addr / core->wrap) reached through the BCMA
accessors. It must NOT be a privately reconstructed backplane address -- not as
a literal, not split across lines, not hidden behind macros, and not folded out
of an expression.

This deliberately does NOT ban 0x180xxxxx globally: such values may legitimately
appear in unrelated evidence, docs or constants. The scan is scoped to the
diagnostic access path (src/ob_diag.{c,h} and the active scripts/observers).

METHODS (strongest available first, each reported explicitly)
------------------------------------------------------------
1. object-level  - disassemble the compiled src/ob_diag.o and look for any
                   backplane-range immediate. The compiler folds
                   `(0x1800 << 16) | 0x0000`, macro pairs and multi-line
                   definitions into a single immediate, so this catches forms a
                   text scan cannot. Requires the module to have been built.
2. token scan of the PREPROCESSED translation unit - runs the real kernel
                   preprocessor (flags recovered from src/.ob_diag.o.cmd) and
                   lexes the result, so macros are expanded before matching.
3. token scan of the source - a real C lexer (comments, string and char
                   literals excluded), so a mention in prose is never a hit.
                   Always available; this is the layer CI's host-tests job runs.
4. AST scan of the active diagnostic scripts - integer constants in the Broadcom
                   backplane range, comments and docstrings excluded.

Behavioural invariants (see also tests/host/ob_diag_test.c):
- all-ones is an OBSERVATION only; it must never gate an error return,
- dev_lost is dominant: after the latch no D11/BCMA/window access is issued.

5. ROUTING LAYER - src/ob_diag.{c,h} may not call bcma_read*/bcma_write*/
   bcma_aread*/bcma_awrite* at all. Every diagnostic hardware access must go
   through the central ob_guard wrappers (ob_d11_*/ob_axi_*/ob_cc_*), which is
   what enforces the dev_lost latch. A direct bcma_* call is an unguarded MMIO
   access.
6. ROUTING LAYER - each known window offset must be reached through the ONE
   accessor that owns its window: BCMA-wrapper offsets through ob_axi_read32,
   D11 core-functional offsets through ob_d11_read32/ob_d11_read16/write16. This
   is a per-offset routing proof, not a presence check: swapping one accessor for
   the other is exactly the wrong-window bug, and the mere presence of both
   accessor names does not catch it.
"""

import ast
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src")
SCRIPTS = os.path.join(ROOT, "scripts")
OB_DIAG_C = os.path.join(SRC, "ob_diag.c")
OB_DIAG_H = os.path.join(SRC, "ob_diag.h")
OB_DIAG_O = os.path.join(SRC, "ob_diag.o")
OB_DIAG_CMD = os.path.join(SRC, ".ob_diag.o.cmd")

# The Broadcom 43xx backplane lives in the 0x1xxxxxxx window. A D11 core or
# wrapper base is a large constant here; every legitimate value the diagnostic
# uses is a small window offset (< 0x1000).
BACKPLANE_LO = 0x18000000
BACKPLANE_HI = 0x1FFFFFFF

# LAYER 5: the BCMA backend belongs exclusively to src/ob_guard.c. Any direct
# use from the diagnostic path bypasses the central dev_lost latch. Matched as
# a call name followed by '(' (the lexer has already dropped comments and
# string/char literals, so prose mentioning bcma_read32 is never a hit).
BCMA_DIRECT_FORBIDDEN = (
    "bcma_read32", "bcma_read16", "bcma_read8",
    "bcma_write32", "bcma_write16", "bcma_write8",
    "bcma_aread32", "bcma_aread16",
    "bcma_awrite32", "bcma_awrite16",
)

# LAYER 6: per-offset window routing. Each offset names the ONE accessor that
# owns the window it lives in. "BCMA_WRAPPER" == the core->wrap / agent window
# reached by bcma_aread32 (ob_axi_read32); "D11_CORE" == the core->addr
# functional window reached by bcma_read32 (ob_d11_*).
AXI_ACCESSORS = ("ob_axi_read32", "ob_axi_write32")
D11_ACCESSORS = ("ob_d11_read32", "ob_d11_read16", "ob_d11_write16",
                 "ob_d11_write32", "ob_d11_read32_trusted")
CC_ACCESSORS = ("ob_cc_read32", "ob_cc_read16", "ob_cc_write32")
# The same offset constant is also quoted in the PRE/POST trace markers and in
# ob_diag_op()/ob_sb_op() log arguments. Those are LOG uses, not hardware
# accesses, so they are not routing claims and must not be flagged. Any
# enclosing call that is neither a log helper nor a known accessor is a
# violation: it means the offset reaches hardware by an unrecognised path.
TRACE_HELPERS = ("_Static_assert", "static_assert",
                 "ob_diag_op", "ob_diag_op_read32", "ob_sb_op",
                 "ob_sb_read_op", "ob_sb_write_op", "scnprintf",
                 "ob_diag_radio_log")

AI_WRAPPER_OFFSETS = ("OB_DIAG_AI_IOCTL", "OB_DIAG_AI_RESET_CTL",
                      "OB_DIAG_AI_RESET_ST")
D11_CORE_OFFSETS = ("OB_DIAG_PROBE_OFF_0FFC", "OB_DIAG_REG_RADIO_ADDR",
                    "OB_DIAG_REG_RADIO_DATA", "OB_DIAG_REG_PHY_ANALOG",
                    "OB_DIAG_REG_PHY_VER", "OB_DIAG_REG_MACCONTROL",
                    "OB_DIAG_REG_MACINTSTATUS", "OB_DIAG_REG_MACINTMASK",
                    "OB_DIAG_REG_SB_CHIPCONTROL", "OB_DIAG_REG_D11_CLK1E0",
                    "OB_SB_REG_CTRL", "OB_SB_REG_STATUS_S",
                    "OB_SB_REG_STATUS", "OB_SB_REG_FORCEGATE", "OB_SB_REG_CLK",
                    "OB_SB_FIRST_D11_REG")
# Offsets that live in the BCMA wrapper window (core->wrap) and must therefore
# be reached with ob_axi_*: the AI/agent registers plus the shared BCMA IOCTL
# word used for the MAC/PHY clock gate and the AC-PHY cflags.
WRAPPER_WINDOW_OFFSETS = AI_WRAPPER_OFFSETS + ("OB_DIAG_AXI_IOCTL",
                                               "BCMA_IOCTL")

# Active diagnostic observers (not historical/reference material).
ACTIVE_SCRIPTS = ("rev42-ai-wrapper-probe.py", "rev42_diag_trace.py")

problems = []
notes = []


def fail(msg):
    problems.append(msg)
    print("FAIL: " + msg)


def note(msg):
    notes.append(msg)
    print("ok: " + msg)


# ---------------------------------------------------------------------------
# A real C lexer. Comments and string/char literals are consumed as units and
# never emitted, so a backplane address named in prose or in a log format
# string is not a hit -- only real tokens are.
# ---------------------------------------------------------------------------
MULTI_OPS = (
    "<<=", ">>=", "...",
    "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##",
)


class Tok(object):
    __slots__ = ("kind", "value", "line")

    def __init__(self, kind, value, line):
        self.kind = kind
        self.value = value
        self.line = line

    def __repr__(self):
        return "Tok(%s,%r,L%d)" % (self.kind, self.value, self.line)


def lex(text):
    """Tokenize C source. Returns a list of Tok (identifiers, numbers, punct)."""
    toks = []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        # comments
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                if text[i] == "\n":
                    line += 1
                i += 1
            i += 2
            continue
        # string / char literal
        if c in "\"'":
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                if text[i] == "\n":
                    line += 1
                i += 1
            continue
        # number (0x / 0b / decimal, with suffixes)
        if c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            j = i
            if text.startswith(("0x", "0X"), i):
                j = i + 2
                while j < n and (text[j] in "0123456789abcdefABCDEF_"):
                    j += 1
            elif text.startswith(("0b", "0B"), i):
                j = i + 2
                while j < n and text[j] in "01_":
                    j += 1
            else:
                while j < n and (text[j].isdigit() or text[j] == "_"):
                    j += 1
            while j < n and text[j] in "uUlL":
                j += 1
            raw = text[i:j].replace("_", "")
            toks.append(Tok("num", raw, line))
            i = j
            continue
        # identifier
        if c.isalpha() or c == "_" or c == "$":
            j = i
            while j < n and (text[j].isalnum() or text[j] in "_$"):
                j += 1
            toks.append(Tok("id", text[i:j], line))
            i = j
            continue
        # punctuation / operators
        for op in MULTI_OPS:
            if text.startswith(op, i):
                toks.append(Tok("punct", op, line))
                i += len(op)
                break
        else:
            toks.append(Tok("punct", c, line))
            i += 1
    return toks


def num_value(raw):
    """Evaluate a C integer literal. Returns None if it is not an integer."""
    s = raw.rstrip("uUlL")
    try:
        if s.lower().startswith("0x"):
            return int(s, 16)
        if s.lower().startswith("0b"):
            return int(s[2:], 2)
        if s.isdigit():
            return int(s, 10)
    except ValueError:
        return None
    return None


def backplane_hits(toks, where):
    """Backplane-range literals, plus shift/OR reconstructions."""
    hits = []
    for idx, t in enumerate(toks):
        if t.kind == "num":
            v = num_value(t.value)
            if v is not None and BACKPLANE_LO <= v <= BACKPLANE_HI:
                hits.append((t.line, "literal %s" % t.value))
        elif t.kind == "punct" and t.value == "<<":
            # `<const> << N` can rebuild a backplane base. The left operand
            # must be an actual numeric token (not a `)` from an unrelated
            # expression) and the shift must be large enough to reach the
            # backplane window.
            lhs = None
            if idx >= 1 and toks[idx - 1].kind == "num":
                lhs = num_value(toks[idx - 1].value)
            if lhs is not None and lhs >= 0x1000:
                nxt = toks[idx + 1] if idx + 1 < len(toks) else None
                if nxt is not None and nxt.kind == "num":
                    sh = num_value(nxt.value)
                    if sh is not None and sh >= 8:
                        hits.append((t.line, "reconstruction %s << %d"
                                     % (toks[idx - 1].value, sh)))
    for line, what in hits:
        fail("%s:%d: diagnostic path contains a backplane address (%s); "
             "use bcma_device core->addr / core->wrap via the BCMA accessors"
             % (where, line, what))
    return hits


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


# ---------------------------------------------------------------------------
# Structural invariants on the token stream
# ---------------------------------------------------------------------------
def calls_of(toks, name):
    return [i for i, t in enumerate(toks)
            if t.kind == "id" and t.value == name
            and i + 1 < len(toks) and toks[i + 1].value == "("]


def has_member(toks, member):
    return any(t.kind == "id" and t.value == member for t in toks)


def check_structure(toks, where, expect_accessors=True):
    for fn in ("pci_write_config_dword", "pci_write_config_word",
               "pci_write_config_byte"):
        if calls_of(toks, fn):
            fail("%s: calls %s; BCMA owns the window switch and "
                 "bus->mapped_core would go stale" % (where, fn))
    for fn in ("pci_iomap", "pci_iounmap"):
        if calls_of(toks, fn):
            fail("%s: calls %s; use the BCMA accessors instead of a private "
                 "BAR0 mapping" % (where, fn))
    for fn in ("ioread8", "ioread16", "ioread32", "ioread32be",
               "iowrite8", "iowrite16", "iowrite32"):
        if calls_of(toks, fn):
            fail("%s: calls %s; route D11/wrapper access through "
                 "ob_d11_read32()/ob_axi_read32() so dev_lost is enforced "
                 "centrally" % (where, fn))
    # LAYER 5: no direct BCMA accessors anywhere in the diagnostic path.
    # ob_guard.c is the single owner of the bcma_* backend; a direct call in
    # ob_diag would issue MMIO with the dev_lost latch unenforced.
    for fn in BCMA_DIRECT_FORBIDDEN:
        if calls_of(toks, fn):
            fail("%s: calls %s directly; src/ob_guard.c owns the BCMA backend "
                 "and is the only place allowed to call bcma_*, so that the "
                 "dev_lost latch is enforced centrally. Use ob_d11_read*/"
                 "ob_d11_write*/ob_axi_read32()/ob_axi_write32() instead"
                 % (where, fn))
    if not expect_accessors:
        return
    for need in ("ob_d11_read32", "ob_axi_read32"):
        if not calls_of(toks, need):
            fail("%s: does not use %s() to reach a BCMA-owned window" % (where, need))
    for member in ("addr", "wrap"):
        if not has_member(toks, member):
            fail("%s: never references the BCMA-owned core->%s" % (where, member))
    if not calls_of(toks, "ob_diag_ai_may_access"):
        fail("%s: the AI probe is not gated on dev_lost" % where)


# ---------------------------------------------------------------------------
# Behavioural invariants: all-ones is observation only; dev_lost is dominant
# ---------------------------------------------------------------------------
LOG_CALLS = {"dev_emerg", "dev_err", "dev_warn", "dev_info", "dev_crit",
             "dev_notice", "dev_dbg"}


def enclosing_call(toks, i):
    """Name of the function whose argument list contains toks[i], else None."""
    depth = 0
    for k in range(i - 1, -1, -1):
        v = toks[k].value
        if v in (")", "]", "}"):
            depth += 1
        elif v in ("(", "["):
            if depth == 0:
                if v == "(" and k > 0 and toks[k - 1].kind == "id":
                    return toks[k - 1].value
                return None
            depth -= 1
    return None


def check_observation_only(toks, where):
    """Every use of an all-ones flag must be an argument to a log call."""
    flagged = ("wrapper_all_ones", "core_all_ones")
    bad = 0
    for i, t in enumerate(toks):
        if t.kind != "id" or t.value not in flagged:
            continue
        fn = enclosing_call(toks, i)
        if fn not in LOG_CALLS:
            fail("%s:%d: all-ones flag %s is an argument to %s, not to a log "
                 "call; all-ones is an observation and must never gate behaviour"
                 % (where, t.line, t.value, fn or "<none>"))
            bad += 1
    if not bad:
        note("all-ones flags appear only as log arguments "
             "(observation-only rule holds)")


def accessor_of(toks, off_name):
    """Return (accessor, count) for every call passing `off_name` as an argument."""
    found = []
    for i, t in enumerate(toks):
        if t.kind != "id" or t.value != off_name:
            continue
        fn = enclosing_call(toks, i)
        if fn:
            found.append((fn, t.line))
    return found


def check_offset_routing(toks, where):
    """Every known offset must be reached through the accessor that owns its window.

    This is the per-offset wrong-window guard. Presence of both accessor names
    proves nothing; what matters is WHICH accessor carries each offset. A
    wrapper offset carried by ob_d11_* (or the reverse) is read from the wrong
    BCMA window, and that is the bug this layer exists to catch.
    """
    problems = 0
    accessors = AXI_ACCESSORS + D11_ACCESSORS + CC_ACCESSORS
    for off in WRAPPER_WINDOW_OFFSETS + D11_CORE_OFFSETS:
        window = ("BCMA_WRAPPER" if off in WRAPPER_WINDOW_OFFSETS
                  else "D11_CORE")
        for fn, line in accessor_of(toks, off):
            if fn in TRACE_HELPERS or fn in LOG_CALLS:
                continue          # quoted in a trace/log argument: not an access
            if fn not in accessors:
                fail("%s:%d: %s is passed to %s(), which is neither a log "
                     "helper nor a guarded ob_guard accessor; the offset would "
                     "reach hardware by an unrecognised path"
                     % (where, line, off, fn))
                problems += 1
                continue
            if window == "BCMA_WRAPPER" and fn not in AXI_ACCESSORS:
                fail("%s:%d: %s is a BCMA WRAPPER-window (core->wrap) offset "
                     "but is reached through %s(); use an ob_axi_* accessor, "
                     "otherwise the register is read from the D11 core->addr "
                     "window" % (where, line, off, fn))
                problems += 1
            if window == "D11_CORE" and fn not in D11_ACCESSORS:
                fail("%s:%d: %s is a D11 CORE-window (core->addr) offset but is "
                     "reached through %s(); use an ob_d11_* accessor, otherwise "
                     "the register is read from the BCMA wrapper core->wrap "
                     "window" % (where, line, off, fn))
                problems += 1
    if not problems:
        note("offset routing: every BCMA-wrapper offset uses ob_axi_*, every "
             "D11 core offset uses ob_d11_* (no wrong-window access)")


def check_branch_ordering():
    """The rev42 isolated-mode branches must precede the full bring-up.

    ob_si_probe() performs PHY init, radio config, calibration, DMA, IRQ and
    mac80211 registration. If an isolated-mode dispatch branch were ever moved
    below it, that mode would run the whole production bring-up while still
    claiming to be bounded. This is the call-path invariant for the mode
    arbitration; ob_diag_test.c pins the arbitration itself.
    """
    path = os.path.join(SRC, "ob_core.c")
    try:
        toks = lex(read(path))
    except OSError as exc:
        fail("cannot read %s (%s)" % (path, exc))
        return
    bring_up = [i for i, t in enumerate(toks)
                if t.kind == "id" and t.value == "ob_si_probe"
                and i + 1 < len(toks) and toks[i + 1].value == "("]
    if not bring_up:
        fail("src/ob_core.c: ob_si_probe() call not found; cannot verify that "
             "the isolated-mode branches precede the full bring-up")
        return
    first_bring_up = bring_up[0]
    ok = True
    modes = ("OB_ISOLATED_REV42_DIAG", "OB_ISOLATED_REV42_ATTACH",
             "OB_ISOLATED_REV42_SB_RESET", "OB_ISOLATED_REV42_AI_PROBE",
             "OB_ISOLATED_REV42_AI_ENABLE", "OB_ISOLATED_RADIO_ID_PROBE")
    for mode_enum in modes:
        branches = [i for i, t in enumerate(toks)
                    if t.kind == "id" and t.value == mode_enum
                    and i > 0 and toks[i - 1].value == "=="]
        if not branches:
            fail("src/ob_core.c: no dispatch branch for %s" % mode_enum)
            ok = False
            continue
        if min(branches) > first_bring_up:
            fail("src/ob_core.c: the %s dispatch branch is AFTER ob_si_probe(); "
                 "the mode would run the full bring-up (PHY/radio/cal/DMA/IRQ/"
                 "mac80211) instead of stopping at its diagnostic boundary"
                 % mode_enum)
            ok = False
    if ok:
        note("call-path: all %d isolated-mode dispatch branches precede "
             "ob_si_probe()" % len(modes))


def check_diag_full_guard():
    """ob_probe() must enforce the rev42_diag_full coupling before any hardware access.

    ob_diag_test.c pins the pure predicate
    ob_isolated_diag_full_allowed(). This pins the WIRING: the call must be
    present in ob_probe()'s body and must precede both the mode arbitration and
    the first allocation, so `insmod openbrcm.ko rev42_diag_full=1` is rejected
    before anything is touched. Deleting the guard from ob_core.c while leaving
    the predicate intact is exactly the mutation this catches.
    """
    path = os.path.join(SRC, "ob_core.c")
    toks = lex(read(path))
    start = None
    for i, t in enumerate(toks):
        if (t.kind == "id" and t.value == "ob_probe"
                and i + 1 < len(toks) and toks[i + 1].value == "("):
            start = i
            break
    if start is None:
        fail("src/ob_core.c: ob_probe() not found; cannot verify the "
             "rev42_diag_full guard is wired in")
        return
    depth, end = 0, None
    for i in range(start, len(toks)):
        if toks[i].value == "{":
            depth += 1
        elif toks[i].value == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    if end is None:
        fail("src/ob_core.c: could not bound ob_probe()")
        return
    body = toks[start:end + 1]

    def first_call(name):
        for i, t in enumerate(body):
            if (t.kind == "id" and t.value == name
                    and i + 1 < len(body) and body[i + 1].value == "("):
                return i
        return None

    guard = first_call("ob_isolated_diag_full_allowed")
    if guard is None:
        fail("src/ob_core.c: ob_probe() does not call "
             "ob_isolated_diag_full_allowed(); rev42_diag_full=1 without "
             "rev42_diag_probe_only=1 would select no isolated mode and fall "
             "through to the full ob_si_probe() bring-up")
        return
    for later, what in ((first_call("ob_isolated_mode_select11"),
                         "the isolated-mode arbitration"),
                        (first_call("devm_kzalloc"),
                         "the first device allocation"),
                        (first_call("bcma_core_enable"),
                         "the first core-enable"),
                        (first_call("ob_si_probe"),
                         "the full bring-up")):
        if later is not None and later < guard:
            fail("src/ob_core.c: the rev42_diag_full guard runs AFTER %s; it "
                 "must reject the invalid pairing before any hardware access"
                 % what)
            return
    note("rev42_diag_full guard is wired into ob_probe() before the mode "
         "arbitration and before any allocation or core access")


# Documented error-return contracts, per rev42 entry point. A rev42 entry point
# may return -EIO only for the dev_lost latch, so -EIO is never a generic
# failure code and the all-ones (MMIO) error path cannot be reported through it.
#
# Phase 36Z RETIRED the historical probe sequence: ob_ai_probe_test survives
# only as a documented refusal that issues no register access, so its contract
# is exactly (-ENODEV when there is no bcma_device, otherwise -EOPNOTSUPP) and
# it may never report a dev_lost -EIO, because it cannot reach an accessor.
RETURN_CONTRACTS = {
    "ob_ai_probe_test": {("ENODEV", "EOPNOTSUPP")},
    "ob_ai_enable_test": {(), ("EIO",), ("ENODEV",), ("EIO", "ENODEV")},
}
DEFAULT_RETURN_CONTRACT = {(), ("EIO",), ("ENODEV",), ("EIO", "ENODEV")}


def check_return_semantics(toks, where, fn="ob_ai_probe_test"):
    """A rev42 entry point may return -EIO only for the dev_lost latch."""
    # isolate the function body by brace matching from its definition
    start = None
    for i, t in enumerate(toks):
        if (t.kind == "id" and t.value == fn
                and i + 1 < len(toks) and toks[i + 1].value == "("):
            start = i
            break
    if start is None:
        fail("%s: %s() not found" % (where, fn))
        return
    depth, end = 0, None
    for i in range(start, len(toks)):
        if toks[i].value == "{":
            depth += 1
        elif toks[i].value == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    if end is None:
        fail("%s: could not bound %s()" % (where, fn))
        return
    body = toks[start:end + 1]

    returns = []
    for i, t in enumerate(body):
        if t.kind == "id" and t.value == "return" and i + 1 < len(body):
            nxt = body[i + 1]
            if nxt.kind == "punct" and nxt.value == "-":
                name = body[i + 2].value if i + 2 < len(body) else "?"
                returns.append((t.line, name))
    errs = [r for r in returns if r[1].startswith("E")]
    names = sorted({r[1] for r in errs})
    allowed = RETURN_CONTRACTS.get(fn, DEFAULT_RETURN_CONTRACT)
    if tuple(names) not in allowed:
        fail("%s: %s returns unexpected error codes %r; the documented "
             "semantics for this entry point are %s"
             % (where, fn, names,
                ", ".join(sorted("none" if not contract else
                                 "/".join("-" + n for n in contract)
                                 for contract in allowed))))
        return
    if [r for r in errs if r[1] == "EIO"]:
        if len([r for r in errs if r[1] == "EIO"]) != 1:
            fail("%s: %s has %d -EIO returns; exactly one is "
                 "allowed (the dev_lost latch)"
                 % (where, fn, len([r for r in errs if r[1] == "EIO"])))
            return
        # the -EIO must be inside the dev_lost guard
        eio_line = [r[0] for r in errs if r[1] == "EIO"][0]
        guard = [i for i, t in enumerate(body)
                 if t.kind == "id" and t.value == "ob_diag_ai_may_access"]
        if not guard:
            fail("%s: %s has no dev_lost gate" % (where, fn))
            return
        if not (body[guard[0]].line <= eio_line):
            fail("%s: the -EIO return (line %d) is not inside the dev_lost "
                 "guard (line %d)" % (where, eio_line, body[guard[0]].line))
            return
    note("%s error returns are %s (dev_lost dominant, "
         "no all-ones error path)" % (fn, names or "none"))


OB_AIE_HEADER = os.path.join(SRC, "ob_ai_enable_only.h")

# Phase 37B: one single-purpose adapter per permitted write, in
# bcma_core_enable() order with flags = 0.
ENABLE_ADAPTER_WRITES = (
    ("ob_aie_write_ioctl_set_fgc", "OB_DIAG_AI_IOCTL",
     "OB_AIE_E1_IOCTL_VALUE"),
    ("ob_aie_write_reset_ctl_deassert", "OB_DIAG_AI_RESET_CTL",
     "OB_AIE_E2_RCTL_VALUE"),
    ("ob_aie_write_ioctl_clr_fgc", "OB_DIAG_AI_IOCTL",
     "OB_AIE_E3_IOCTL_VALUE"),
)

# The six operations the engine may express, and nothing else.
ENABLE_ENGINE_CALLBACKS = ("read_reset_st", "read_reset_ctl", "read_ioctl",
                           "write_ioctl_set_fgc", "write_reset_ctl_deassert",
                           "write_ioctl_clr_fgc")

# Each read adapter performs exactly one wrapper read. The D11 functional-core
# accessors are absent by construction: ob_ai_enable_only stops at the wrapper
# boundary even when the vendor predicate reports CORE_UP (Phase 37B D4).
ENABLE_ADAPTER_READS = {
    "ob_aie_read_reset_st": ["ob_axi_read32"],
    "ob_aie_read_reset_ctl": ["ob_axi_read32"],
    "ob_aie_read_ioctl": ["ob_axi_read32"],
}

# The engine must be pure: `udelay` is reached through the delay callback, and
# the D11 functional-core window is not expressible at all.
ENGINE_FORBIDDEN = ("ob_d11_", "ob_axi_", "ob_cc_", "bcma_", "pci_",
                    "ob_si_probe", "ob_fw_probe", "ob_ucode", "ob_initvals",
                    "ob_dma", "ob_irq_", "ob_mac80211", "mac80211", "phy_",
                    "radio", "pll", "calibrat", "udelay", "msleep", "mdelay",
                    "wait_event", "while", "for")


def bound_body(toks, fn):
    """Return the token span of the definition body of `fn`, or None.

    Only a DEFINITION is accepted: between the name and the opening brace of
    a function definition there is no `;`. A call site (statement, argument or
    initializer) always terminates with `;` before any later `{`, and binding
    one of those would silently widen the span to whatever block follows it,
    attributing an unrelated function's accessors to `fn`. Names that are
    defined in a header and only CALLED in this file therefore bind to
    nothing, and their per-function containment checks are skipped rather
    than satisfied by a neighbouring function.
    """
    for i, t in enumerate(toks):
        if not (t.kind == "id" and t.value == fn
                and i + 1 < len(toks) and toks[i + 1].value == "("):
            continue
        start = i
        brace = None
        for j in range(i + 1, len(toks)):
            if toks[j].value == ";":
                break
            if toks[j].value == "{":
                brace = j
                break
        if brace is None:
            continue
        depth, end = 0, None
        for j in range(brace, len(toks)):
            if toks[j].value == "{":
                depth += 1
            elif toks[j].value == "}":
                depth -= 1
                if depth == 0:
                    end = j
                    break
        if end is None:
            return None
        return toks[start:end + 1]
    return None


ACCESSOR_NAMES = AXI_ACCESSORS + D11_ACCESSORS + CC_ACCESSORS


def check_enable_write_containment(toks, where):
    """rev42_ai_enable_only is the only rev42 mode that WRITES.

    Phase 37B moved its sequencing into src/ob_ai_enable_only.h so the host
    tests execute the real state machine. The containment proof follows the
    code: the ENTRY POINT may issue no register access at all (it must only
    delegate), each permitted write has its own single-purpose adapter, and the
    engine header may not name a single kernel accessor.

    That is stronger than "the three writes appear in one function": a fourth
    access cannot be added to the engine without a new callback, and a new
    callback cannot be added without failing the write-set comparison here.
    """
    problems = 0

    # 1. The entry point delegates; it does not touch hardware itself.
    body = bound_body(toks, "ob_ai_enable_test")
    if body is None:
        fail("%s: ob_ai_enable_test() not found" % where)
        return
    direct = [t.value for t in body
              if t.kind == "id" and t.value in ACCESSOR_NAMES]
    if direct:
        fail("%s: ob_ai_enable_test() issues %r directly; every hardware "
             "operation must go through the reviewed adapter callback that "
             "owns it" % (where, sorted(set(direct))))
        problems += 1
    if not calls_of(body, "ob_aie_run"):
        fail("%s: ob_ai_enable_test() does not call ob_aie_run(); the "
             "host-tested state machine would not be the one that runs"
             % where)
        problems += 1

    # 2. Each permitted write is one single-purpose adapter.
    seen = []
    for fn_name, off_name, val_name in ENABLE_ADAPTER_WRITES:
        ab = bound_body(toks, fn_name)
        if ab is None:
            fail("%s: adapter %s() not found" % (where, fn_name))
            problems += 1
            continue
        writes = [t.value for t in ab if t.kind == "id"
                  and t.value in ACCESSOR_NAMES]
        offs = [t.value for t in ab if t.kind == "id" and t.value == off_name]
        vals = [t.value for t in ab if t.kind == "id" and t.value == val_name]
        if writes != ["ob_axi_write32"]:
            fail("%s: %s() issues %r; exactly one ob_axi_write32 is allowed"
                 % (where, fn_name, writes))
            problems += 1
        if not offs:
            fail("%s: %s() does not use %s" % (where, fn_name, off_name))
            problems += 1
        if not vals:
            fail("%s: %s() does not write %s" % (where, fn_name, val_name))
            problems += 1
        seen.append((fn_name, off_name, val_name))

    # 3. The three read adapters perform exactly one wrapper read each, and
    #    every OTHER ob_aie_* helper performs no accessor call at all. The
    #    diagnostic file has other, unrelated modes that legitimately touch
    #    AXI and D11 registers, so this containment claim is scoped to the
    #    rev42_ai_enable_only helpers by name.
    #
    #    Phase 37B correction (mutation M1): scoping this to writes alone let
    #    an injected ob_d11_read32() inside a read adapter pass every check
    #    here, because that accessor is not a write32. The D11 accessors are
    #    now rejected explicitly, per function, in every branch.
    adapter_names = {a[0] for a in ENABLE_ADAPTER_WRITES}
    for name in sorted({t.value for t in toks if t.kind == "id"
                        and t.value.startswith("ob_aie_")}):
        if name in adapter_names:
            continue
        ab = bound_body(toks, name)
        if ab is None:
            continue
        used = [t.value for t in ab if t.kind == "id"
                and t.value in AXI_ACCESSORS + D11_ACCESSORS + CC_ACCESSORS]
        if not used:
            continue
        d11 = [t for t in used if t in D11_ACCESSORS]
        if d11:
            fail("%s: %s() issues %r; the rev42_ai_enable_only path must never "
                 "reach the D11 functional-core window (0x0FFC), even when "
                 "the wrapper predicate reports CORE_UP - Phase 37B D4"
                 % (where, name, sorted(set(d11))))
            problems += 1
            continue
        allowed = ENABLE_ADAPTER_READS.get(name, [])
        if sorted(used) != sorted(allowed):
            fail("%s: %s() issues %r; the reviewed operation set for this "
                 "function is %r" % (where, name, sorted(set(used)),
                                      sorted(allowed)))
            problems += 1

    # 4. The engine header is pure: no kernel accessor, no poll, no bring-up.
    try:
        engine = lex(read(OB_AIE_HEADER))
    except OSError as exc:
        fail("cannot read %s (%s)" % (OB_AIE_HEADER, exc))
        return
    hits = []
    for t in engine:
        if t.kind != "id":
            continue
        for bad in ENGINE_FORBIDDEN:
            if t.value == bad or t.value.startswith(bad):
                hits.append((t.value, t.line))
                break
    if hits:
        fail("%s: the enable engine references %r; the contract admits no "
             "kernel accessor, no poll and no PHY/radio/PLL/cal/DMA/IRQ/"
             "mac80211 reachability"
             % (os.path.basename(OB_AIE_HEADER),
                sorted({h[0] for h in hits})))
        problems += 1
    engine_ids = {t.value for t in engine if t.kind == "id"}
    missing = [cb for cb in ENABLE_ENGINE_CALLBACKS if cb not in engine_ids]
    if missing:
        fail("%s: the ops struct no longer declares %r"
             % (os.path.basename(OB_AIE_HEADER), missing))
        problems += 1
    if problems == 0:
        note("rev42_ai_enable_only: the entry point delegates, the write set is "
             "exactly three single-purpose adapters (%s), and the pure engine "
             "names no kernel accessor"
             % ", ".join("%s->%s" % (o, v) for _, o, v in seen))


# ---------------------------------------------------------------------------
# Layer 1: object-level constant inspection (strongest)
# ---------------------------------------------------------------------------
def check_object():
    if not os.path.isfile(OB_DIAG_O):
        if os.environ.get("OB_DIAG_REQUIRE_OBJECT") == "1":
            fail("object scan required but src/ob_diag.o is absent; the "
                 "strongest layer must not be silently skipped "
                 "(set this in the job that builds the module)")
        else:
            note("object scan skipped: src/ob_diag.o not built (run `make` to "
                 "enable the strongest layer, or set OB_DIAG_REQUIRE_OBJECT=1)")
        return None
    try:
        r = subprocess.run(["objdump", "-d", OB_DIAG_O],
                           capture_output=True, text=True, timeout=120)
    except (OSError, subprocess.SubprocessError):
        note("object scan skipped: objdump unavailable")
        return None
    if r.returncode != 0:
        note("object scan skipped: objdump failed")
        return None
    hits = set()
    for m in re.finditer(r"\b0x([0-9a-fA-F]{8})\b|\b([0-9a-fA-F]{8})\b", r.stdout):
        raw = m.group(1) or m.group(2)
        try:
            v = int(raw, 16)
        except ValueError:
            continue
        if BACKPLANE_LO <= v <= BACKPLANE_HI:
            hits.add(raw.lower())
    if hits:
        fail("src/ob_diag.o contains backplane-range immediates %s; a "
             "backplane address is being compiled into the diagnostic path"
             % sorted(hits))
        return False
    note("object scan: no backplane-range immediate in src/ob_diag.o "
         "(folded constants would appear here)")
    return True


# ---------------------------------------------------------------------------
# Layer 2: preprocessed token scan
# ---------------------------------------------------------------------------
LINEMARKER = re.compile(r'^#\s+\d+\s+"([^"]+)"')


def own_region(text, want="ob_diag.c"):
    """Restrict preprocessed text to the lines belonging to ob_diag.c.

    `cc -E` inlines every kernel header, and unrelated headers legitimately
    contain backplane-range constants. Line markers (`# N "file"`) name the
    file each following line came from.

    The file's own code can appear in SEVERAL disjoint regions (every include
    splits it), so this tracks the current file line by line and keeps every
    line attributed to ob_diag.c. Taking only the text after the LAST marker
    would silently drop the earlier regions -- a false negative.
    """
    kept, current = [], None
    for line in text.splitlines():
        m = LINEMARKER.match(line)
        if m:
            current = os.path.basename(m.group(1))
            continue
        if current == want:
            kept.append(line)
    if not kept:
        # No markers (or a preprocessor that emits none): fall back to the
        # whole text rather than silently scanning nothing.
        return text
    return "\n".join(kept)


def check_preprocessed():
    if not os.path.isfile(OB_DIAG_CMD):
        note("preprocessed scan skipped: src/.ob_diag.o.cmd absent")
        return None
    raw = None
    for line in read(OB_DIAG_CMD).splitlines():
        if line.startswith("savedcmd_src/ob_diag.o :="):
            raw = line.split(":=", 1)[1].strip()
            break
    if not raw or " -c " not in raw:
        note("preprocessed scan skipped: could not recover the build command")
        return None
    argv = shlex.split(raw)
    try:
        argv.remove("-c")
    except ValueError:
        note("preprocessed scan skipped: no -c in the build command")
        return None
    out_arg = None
    for i, a in enumerate(argv):
        if a == "-o" and i + 1 < len(argv):
            out_arg = i + 1
    if out_arg is None:
        note("preprocessed scan skipped: no -o in the build command")
        return None
    with tempfile.TemporaryDirectory(prefix="ob-pp-") as tmp:
        pp = os.path.join(tmp, "ob_diag.i")
        argv[out_arg] = pp
        argv.insert(1, "-E")
        try:
            # Kbuild records paths relative to the repository root.  Host tests
            # invoke this script from tests/host, so preserve Kbuild's cwd or
            # an otherwise valid command silently degrades into a skipped gate.
            r = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
                               timeout=300)
        except (OSError, subprocess.SubprocessError) as exc:
            note("preprocessed scan skipped: preprocessor unavailable (%s)" % exc)
            return None
        if r.returncode != 0 or not os.path.isfile(pp):
            note("preprocessed scan skipped: preprocessor failed")
            return None
        toks = lex(own_region(read(pp)))
    if backplane_hits(toks, "src/ob_diag.c (preprocessed)"):
        return False
    note("preprocessed scan: no backplane address survives macro expansion "
         "in src/ob_diag.c")
    return True


# ---------------------------------------------------------------------------
# Layer 4: AST scan of the active diagnostic scripts
# ---------------------------------------------------------------------------
def check_scripts():
    checked = 0
    for name in ACTIVE_SCRIPTS:
        path = os.path.join(SCRIPTS, name)
        if not os.path.isfile(path):
            continue
        checked += 1
        try:
            tree = ast.parse(read(path), filename=path)
        except SyntaxError as exc:
            fail("%s: does not parse (%s)" % (path, exc))
            continue
        for node in ast.walk(tree):
            if isinstance(node, ast.Constant) and isinstance(node.value, int):
                v = node.value
                if BACKPLANE_LO <= v <= BACKPLANE_HI:
                    fail("%s:%d: active diagnostic script embeds a backplane "
                         "address %#x; core->addr / core->wrap must come from "
                         "evidence, never from a literal" % (name, node.lineno, v))
    if checked:
        note("AST scan: %d active diagnostic script(s) carry no backplane "
             "address literal" % checked)
    return True


def main():
    for path in (OB_DIAG_C, OB_DIAG_H):
        if not os.path.isfile(path):
            print("ERROR: missing %s" % path)
            return 2

    # Layer 3: source token scan (always available).
    for path in (OB_DIAG_C, OB_DIAG_H):
        rel = os.path.relpath(path, ROOT)
        toks = lex(read(path))
        backplane_hits(toks, rel)
        # The header declares the pure layer; the accessors live in the .c.
        check_structure(toks, rel, expect_accessors=(path == OB_DIAG_C))
    note("source token scan: ob_diag uses BCMA accessors, writes no PCI "
         "window, maps no BAR0, performs no raw MMIO")

    toks_c = lex(read(OB_DIAG_C))
    check_observation_only(toks_c, "src/ob_diag.c")
    check_offset_routing(toks_c, "src/ob_diag.c")
    check_return_semantics(toks_c, "src/ob_diag.c")
    check_return_semantics(toks_c, "src/ob_diag.c", fn="ob_ai_enable_test")
    check_enable_write_containment(toks_c, "src/ob_diag.c")
    check_branch_ordering()
    check_diag_full_guard()

    check_preprocessed()
    check_object()
    check_scripts()

    # The guard wrappers must still be the BCMA ones, and ob_guard.c must be
    # the ONLY translation unit allowed to call the BCMA backend.
    guard = lex(read(os.path.join(SRC, "ob_guard.c")))
    for fn in ("bcma_read32", "bcma_aread32"):
        if not calls_of(guard, fn):
            fail("src/ob_guard.c no longer routes through %s(); a BCMA window "
                 "would no longer be BCMA-owned" % fn)
    if not problems:
        note("ob_guard.c routes D11 and wrapper access through bcma_read32 / "
             "bcma_aread32")
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".c") or name == "ob_guard.c":
            continue
        if calls_of(lex(read(os.path.join(SRC, name))), "bcma_read32"):
            fail("src/%s calls bcma_read32() directly; src/ob_guard.c is the "
                 "sole owner of the BCMA backend" % name)

    if problems:
        print("== test_ob_diag_addressing FAIL ==")
        return 1
    print("== test_ob_diag_addressing PASS ==")
    return 0


if __name__ == "__main__":
    sys.exit(main())
