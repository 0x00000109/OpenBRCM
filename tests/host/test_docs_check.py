#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regression tests for scripts/docs-check.sh and scripts/docs-check-c-scan.py.

Protected behaviour
-------------------
A. C scanner: mentions inside comments, block comments, string literals and
   char literals are NOT calls; real calls ARE, including a call that shares a
   physical line with a string literal (the historic shell-grep blind spot).
B. Status-claim guards: a positive proof claim about a rev42 probe mode is
   rejected in BOTH word orders (mode-then-verb and verb-then-mode), while
   explicit negative wording stays allowed.
C. Canonical documentation gate: docs/m34d4/rev42_diag_modes.md must exist and
   carry the AGENTS.md status vocabulary.
D. Wiring gate: each of the four modes must keep its module param, enum value,
   dispatch branch and ob_hw state field; entry points and the remove hook must
   stay declared and dispatched.
E. Containment: a real call from ob_diag.c into a forbidden stage must be
   detected for every forbidden identifier; mentions must stay allowed.
F. Self-reference: the generated export tree embeds whole-repository content
   (including docs-check.sh's own diagnostic strings) and must not be scanned.

Test architecture
-----------------
The real repository is READ-ONLY input. A disposable copy is made ONCE per
module into a TemporaryDirectory (setUpModule) and every mutation is applied to
that copy and reverted afterwards, so tests are order-independent and the real
tree is never written to.

Assertions target the stable `ok:` / `FAIL:` marker strings that docs-check.sh
emits for each specific gate, never the process exit code or full output: a
copied tree legitimately fails unrelated checks (e.g. current-context
freshness), and the checks under test are exactly the ones whose behaviour we
pin. Where a case may legitimately be caught by more than one of the three
status-claim guards, `assertAnyFlagged` accepts any of them.
"""

import contextlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
_CHECK = os.path.join(_ROOT, "scripts", "docs-check.sh")
_CSCAN = os.path.join(_ROOT, "scripts", "docs-check-c-scan.py")
_EXPORT_DIR = os.path.join(_ROOT, "docs", "dify-export")
_REV42_DOC = "docs/m34d4/rev42_diag_modes.md"
_OB_DIAG_C = "src/ob_diag.c"

# Sandbox is created once by setUpModule and shared by every test.
_SANDBOX = None
_TMPDIR = None


# --------------------------------------------------------------------------
# Phrase construction
#
# Dangerous phrasings are assembled at runtime from these fragments rather than
# written as contiguous literals, so this test source can never be mistaken for
# a violating document. (docs-check.sh scans docs/, root *.md and AGENTS.md --
# never tests/ -- but the indirection is cheap insurance and keeps the
# production regexes free of any accommodation for the tests.)
# --------------------------------------------------------------------------
_MODES = (
    "rev42_" + "diag_probe_only",
    "rev42_" + "attach_probe_only",
    "rev42_" + "sb_reset_only",
    "rev42_" + "ai_probe_only",
)
_VERBS = ("proven", "proved", "proves", "prove")
_SCOPE_NOUN = "radio path"


def _forward_claim(mode, verb):
    """Mode name first: '<mode> <verb> the radio path'."""
    return "%s %s the %s" % (mode, verb, _SCOPE_NOUN)


def _reverse_claim(mode, verb):
    """Mode name last: 'the radio path is <verb> by <mode>'."""
    return "the %s is %s by %s" % (_SCOPE_NOUN, verb, mode)


# --------------------------------------------------------------------------
# Sandbox lifecycle
# --------------------------------------------------------------------------
_SKIP_DIRS = frozenset(
    {".git", "__pycache__", "artifacts", "dify-export", ".openbrcm-local", ".freebuff", "target"}
)
_SKIP_FILE_RE = re.compile(
    r"(\.o$|\.ko$|\.mod$|\.mod\.c$|\.cmd$|\.symvers$|\.order$|\.pyc$|^\.tmp_versions$)"
)


def _ignore(dirpath, names):
    return {n for n in names if n in _SKIP_DIRS or _SKIP_FILE_RE.search(n)}


def setUpModule():
    """Copy the repository once; every test mutates only this copy."""
    global _SANDBOX, _TMPDIR
    _TMPDIR = tempfile.TemporaryDirectory(prefix="ob-docs-check-")
    _SANDBOX = os.path.join(_TMPDIR.name, "repo")
    shutil.copytree(_ROOT, _SANDBOX, ignore=_ignore, symlinks=True)


def tearDownModule():
    if _TMPDIR is not None:
        _TMPDIR.cleanup()


# --------------------------------------------------------------------------
# Stable gate markers (verbatim from scripts/docs-check.sh)
#
# The two proof-claim "ok" markers changed in the 2026-09-27 M3.4D
# reconciliation. They used to be the unconditional phrases
#   "rev42 probe modes not claimed hardware proven"
#   "no positive proof claim for any rev42 probe mode"
# which encoded the (now false) premise that NO rev42 mode had ever run. Two
# modes have since been executed exactly once each, so the guards now answer
# the narrower, structured question: is every such claim about a mode that
# docs/state/current-state.json records as executed? A truthful narrow claim is
# therefore accepted and an over-broad one is still rejected. See
# scripts/docs-check-rev42-status.py.
# --------------------------------------------------------------------------
M_HW_FAIL = "a document claims a rev42 probe mode is HARDWARE PROVEN"
M_HW_OK = "rev42 hardware-proof claims limited to modes recorded as executed"
M_HW_OK_LEGACY = "rev42 probe modes not claimed hardware proven"
M_SCOPE_FAIL = "a document claims a rev42 probe mode proved initvals/PHY/radio/calibration/DMA"
M_SCOPE_OK = "rev42 probe mode scope limited to its declared STOP"
M_POS_FAIL = "a document makes a positive proof claim about a rev42 probe mode"
M_POS_OK = "no unsupported positive proof claim for any rev42 probe mode"
M_POS_OK_LEGACY = "no positive proof claim for any rev42 probe mode"
M_STRUCT_FAIL = "M3.4D structured status invariants violated"
M_STRUCT_OK = "M3.4D structured status invariants hold"
M_NOREC_FAIL = "rev42 probe modes have no canonical record"
M_VOCAB_FAIL = "rev42 probe-mode record missing the expected status vocabulary"
M_VOCAB_OK = "rev42 probe-mode record present"
M_WIRE_FAIL = "rev42 mode not wired (param/enum/dispatch/state): "
M_WIRE_OK = "rev42 mode wired (param/enum/dispatch/state): "
M_ENTRY_FAIL = "rev42 entry point missing or not dispatched: "
M_ENTRY_OK = "rev42 entry point declared and dispatched: "
M_REMOVE_FAIL = "rev42 remove hook missing (ob_diag_remove)"
M_REMOVE_OK = "rev42 remove hook present"
M_SCAN_FAIL = "src/ob_diag.c calls a forbidden stage (comment/string-aware scan)"
M_SCAN_OK = "rev42 probe code passes the comment/string-aware scan"
M_PHY_FAIL = "src/ob_diag.c calls a forbidden PHY/mac80211 stage"
M_PHY_OK = "rev42 probe code does not reach PHY/mac80211"
M_IRQ_FAIL = "src/ob_diag.c calls a forbidden host-IRQ stage"
M_IRQ_OK = "rev42 probe code does not reach the host IRQ route"

# Identifiers docs-check.sh forbids in ob_diag.c, per category.
FORBIDDEN_PHY = ("wlc_phy_init", "wlc_phy_anacore", "wlc_phy_switch_radio")
FORBIDDEN_MAC80211 = ("ob_mac80211_register_hw", "ob_mac80211_unregister_hw")
FORBIDDEN_IRQ = ("request_irq", "ob_irq_setup")
ALL_FORBIDDEN = FORBIDDEN_PHY + FORBIDDEN_MAC80211 + FORBIDDEN_IRQ

# Sections 4l/4m of docs-check.sh are themselves conditional on src/ob_diag.c
# existing and report "not applicable" until the rev42 feature lands. The tests
# mirror that condition instead of assuming the feature is present: on a clean
# checkout (or in CI) without src/ob_diag.c these groups are skipped rather
# than failing, and they light up automatically as soon as the source lands.
# Groups A (scanner) and G (self-reference) do not depend on the feature.
_REV42_PRESENT = os.path.isfile(os.path.join(_ROOT, _OB_DIAG_C))

requires_rev42 = unittest.skipUnless(
    _REV42_PRESENT,
    "rev42 ob_diag gates not applicable: src/ob_diag.c is absent",
)


class Result(object):
    """Subprocess outcome with merged streams for stable-substring assertions."""

    def __init__(self, proc):
        self.rc = proc.returncode
        self.stdout = proc.stdout
        self.stderr = proc.stderr
        self.out = proc.stdout + proc.stderr


def _diag(case, expected, res, marker=""):
    return (
        "\n--- case: %s\n--- expected: %s\n--- rc: %s\n--- marker: %s"
        "\n--- stdout:\n%s\n--- stderr:\n%s\n---"
        % (case, expected, res.rc, marker or "(none)", res.stdout, res.stderr)
    )


def _run(args, cwd):
    return Result(
        subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    )


class SandboxTestCase(unittest.TestCase):
    """Base class providing the sandbox and the mutation helpers."""

    def check(self):
        """Run the sandbox's own docs-check.sh (the gate under test)."""
        return _run(
            ["sh", os.path.join(_SANDBOX, "scripts", "docs-check.sh")], _SANDBOX
        )

    def scan(self, path, idents):
        """Run the real comment/string-aware scanner on one file."""
        return _run(["python3", _CSCAN, path] + list(idents), _ROOT)

    # -- assertions ------------------------------------------------------
    def assertFlagged(self, case, res, marker):
        self.assertIn("FAIL: " + marker, res.out, _diag(case, "FAIL: " + marker, res, marker))

    def assertAnyFlagged(self, case, res, markers):
        if not any("FAIL: " + m in res.out for m in markers):
            self.fail(_diag(case, "any of %r" % (markers,), res))

    def assertNotFlagged(self, case, res, markers, ok_markers):
        """The guard must stay quiet: no FAIL line, and its own ok line present."""
        for m in markers:
            self.assertNotIn("FAIL: " + m, res.out, _diag(case, "no FAIL: " + m, res, m))
        for m in ok_markers:
            self.assertIn("ok: " + m, res.out, _diag(case, "ok: " + m, res, m))

    def assertNotFlaggedAny(self, case, res, markers, ok_markers):
        """As assertNotFlagged, but ANY one of the ok markers is acceptable.

        Used where a guard has two legitimate phrasings (for example a guard
        that either finds nothing to complain about, or defers to a structured
        check that then passes). Requiring both would pin an implementation
        detail; requiring neither would weaken the assertion to "it did not
        crash".
        """
        for m in markers:
            self.assertNotIn("FAIL: " + m, res.out, _diag(case, "no FAIL: " + m, res, m))
        if not any("ok: " + m in res.out for m in ok_markers):
            self.fail(_diag(case, "any ok of %r" % (ok_markers,), res))

    # -- mutations -------------------------------------------------------
    @contextlib.contextmanager
    def mutate(self, relpath, transform):
        """Edit a sandbox file for the duration of one test, then restore it."""
        path = os.path.join(_SANDBOX, relpath)
        with open(path, "r", encoding="utf-8") as f:
            original = f.read()
        try:
            new = transform(original)
            self.assertNotEqual(
                new, original, "mutation of %s was a no-op; test proves nothing" % relpath
            )
            with open(path, "w", encoding="utf-8") as f:
                f.write(new)
            yield path
        finally:
            with open(path, "w", encoding="utf-8") as f:
                f.write(original)

    @contextlib.contextmanager
    def add_doc(self, name, text):
        """Add a throwaway document under docs/ for the duration of one test."""
        path = os.path.join(_SANDBOX, "docs", name)
        self.assertFalse(os.path.exists(path), "sandbox leftover %s" % name)
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text + "\n")
            yield path
        finally:
            if os.path.exists(path):
                os.remove(path)

    def strip_line(self, pattern):
        """Build a transform that removes every line matching `pattern`."""
        rx = re.compile(pattern, re.M)

        def _t(s):
            return rx.sub("", s)

        return _t


# ==========================================================================
# A. C scanner: comment/string awareness
# ==========================================================================
class CScannerTests(SandboxTestCase):
    """Unit-level tests of scripts/docs-check-c-scan.py against synthetic files."""

    def scan_source(self, case, source, expect_findings):
        with tempfile.TemporaryDirectory(prefix="ob-cscan-") as tmp:
            path = os.path.join(tmp, "fixture.c")
            with open(path, "w", encoding="utf-8") as f:
                f.write(source)
            res = self.scan(path, FORBIDDEN_PHY)
        if expect_findings:
            self.assertEqual(
                res.rc, 1, _diag(case, "rc=1 (a forbidden call was found)", res)
            )
            self.assertIn("forbidden call", res.stdout, _diag(case, "finding on stdout", res))
        else:
            self.assertEqual(res.rc, 0, _diag(case, "rc=0 (clean)", res))
        return res

    def test_A1_line_comment_only_is_clean(self):
        self.scan_source(
            "A1 line comment only",
            "// wlc_phy_init(hw);\n"
            "static int probe(void) { return 0; }\n",
            expect_findings=False,
        )

    def test_A2_block_comment_only_is_clean(self):
        self.scan_source(
            "A2 block comment only",
            "/*\n"
            " * wlc_phy_anacore(pi, true);\n"
            " */\n"
            "static int probe(void) { return 0; }\n",
            expect_findings=False,
        )

    def test_A3_string_literal_only_is_clean(self):
        self.scan_source(
            "A3 string literal only",
            'void probe(struct device *dev)\n'
            '{\n'
            '\tdev_info(dev, "calling wlc_phy_init(hw)");\n'
            '}\n',
            expect_findings=False,
        )

    def test_A4_real_call_is_found(self):
        self.scan_source(
            "A4 real call",
            "void probe(struct hw *hw)\n"
            "{\n"
            "\twlc_phy_init(hw);\n"
            "}\n",
            expect_findings=True,
        )

    def test_A5_real_call_on_string_bearing_line_is_found(self):
        """The historic blind spot: a call sharing a line with a string literal.

        The line-based shell filter drops any line containing a quote, so this
        is the case it cannot see. The scanner must still find it.
        """
        self.scan_source(
            "A5 real call on string-bearing line",
            "void probe(struct hw *hw)\n"
            "{\n"
            '\tdev_info(hw->dev, "x"); wlc_phy_init(hw);\n'
            "}\n",
            expect_findings=True,
        )

    def test_A6_char_literal_and_escaped_string_noise_is_clean(self):
        self.scan_source(
            "A6 char literal / escaped string noise",
            "/* wlc_phy_switch_radio( in a block comment */\n"
            "// wlc_phy_init( in a line comment\n"
            "void probe(struct device *dev)\n"
            "{\n"
            "\tchar c = '(';\n"
            "\tchar d = '\"';\n"
            '\tdev_info(dev, "escaped \\" quote wlc_phy_init( inside");\n'
            '\tdev_info(dev, "unterminated-looking wlc_phy_anacore(");\n'
            "}\n",
            expect_findings=False,
        )

    def test_A7_scanner_reports_line_numbers(self):
        res = self.scan_source(
            "A7 line numbers",
            "int a;\n"
            "int b;\n"
            "\twlc_phy_init(hw);\n",
            expect_findings=True,
        )
        self.assertIn(":3:", res.stdout, _diag("A7 line numbers", "hit on line 3", res))


# ==========================================================================
# B. Status-claim guards
# ==========================================================================
@requires_rev42
class StatusClaimGuardTests(SandboxTestCase):
    """Positive proof claims are rejected in both word orders; negatives pass."""

    CLAIM_GUARDS = (M_HW_FAIL, M_SCOPE_FAIL, M_POS_FAIL)
    CLAIM_OKS = (M_HW_OK, M_SCOPE_OK, M_POS_OK)

    @contextlib.contextmanager
    def _with_claim(self, text):
        with self.add_doc("_test_claim.md", text):
            yield self.check()

    def _expect_flagged(self, case, text):
        with self._with_claim(text) as res:
            self.assertAnyFlagged(case, res, self.CLAIM_GUARDS)

    def _expect_clean(self, case, text):
        with self._with_claim(text) as res:
            # Either the guard stayed quiet outright (legacy phrasing) or it
            # deferred to the structured check and that check passed. Both are
            # "clean"; the marker differs, so accept the new one or the old one.
            self.assertNotFlaggedAny(
                case, res, self.CLAIM_GUARDS,
                self.CLAIM_OKS + (M_HW_OK_LEGACY, M_POS_OK_LEGACY),
            )

    # -- B1: plain hardware-proof claim ---------------------------------
    def test_B1_hardware_proven_claim_is_flagged(self):
        self._expect_flagged(
            "B1 HARDWARE PROVEN", "%s is HARDWARE PROVEN" % _MODES[0]
        )

    def test_B1b_hardware_runtime_proven_claim_is_flagged(self):
        self._expect_flagged(
            "B1b HARDWARE RUNTIME PROVEN",
            "%s is HARDWARE RUNTIME PROVEN on BCM4352" % _MODES[2],
        )

    # -- B2: natural-language 'proved', mode first ----------------------
    def test_B2_forward_proved_claim_is_flagged(self):
        self._expect_flagged(
            "B2 forward proved", _forward_claim(_MODES[2], "proved")
        )

    # -- B3: reverse word order, mode last ------------------------------
    def test_B3_reverse_claim_is_flagged(self):
        self._expect_flagged(
            "B3 reverse proven-by", _reverse_claim(_MODES[2], "proven")
        )

    def test_B3b_reverse_hardware_proven_claim_is_flagged(self):
        self._expect_flagged(
            "B3b reverse HARDWARE PROVEN by",
            "the %s is HARDWARE PROVEN by %s" % (_SCOPE_NOUN, _MODES[1]),
        )

    # -- B4: perfect-tense / generic completion -------------------------
    def test_B4_has_been_proven_is_flagged(self):
        # _MODES[0] is rev42_diag_probe_only, which has NEVER been executed.
        # It was _MODES[3] (rev42_ai_probe_only) before the M3.4D
        # reconciliation, when no rev42 mode had run. That mode has since been
        # executed exactly once, so a claim about it is no longer
        # unconditionally false and the structured check now adjudicates it --
        # see OverBroadClaimOnExecutedModeTests for the replacement coverage.
        self._expect_flagged(
            "B4 has been proven", "%s has been proven" % _MODES[0]
        )

    def test_B4b_proves_wording_is_flagged(self):
        self._expect_flagged(
            "B4b proves", "it %s correct: %s" % ("proves", _MODES[0])
        )

    # -- verb x order matrix --------------------------------------------
    def test_B5_all_verbs_forward_order_flagged(self):
        for verb in _VERBS:
            with self.subTest(verb=verb, order="forward"):
                self._expect_flagged(
                    "B5 forward %s" % verb, _forward_claim(_MODES[0], verb)
                )

    def test_B6_all_verbs_reverse_order_flagged(self):
        for verb in _VERBS:
            with self.subTest(verb=verb, order="reverse"):
                self._expect_flagged(
                    "B6 reverse %s" % verb, _reverse_claim(_MODES[1], verb)
                )

    def test_B7_every_mode_is_covered_reverse_order(self):
        for mode in _MODES:
            with self.subTest(mode=mode):
                self._expect_flagged(
                    "B7 reverse %s" % mode, _reverse_claim(mode, "proven")
                )

    # -- negative / explicitly-unproven wording must stay allowed --------
    def test_B8_not_proven_is_allowed(self):
        self._expect_clean("B8 not proven", "%s is not proven" % _MODES[0])

    def test_B9_never_been_executed_is_allowed(self):
        self._expect_clean(
            "B9 never executed", "%s has never been executed" % _MODES[3]
        )

    def test_B10_not_hardware_proven_is_allowed(self):
        self._expect_clean(
            "B10 not hardware proven",
            "the %s mode is not hardware proven" % _MODES[2],
        )

    def test_B11_remains_unproven_is_allowed(self):
        self._expect_clean("B11 remains unproven", "%s remains unproven" % _MODES[0])

    def test_B12_awaiting_approval_is_allowed(self):
        self._expect_clean(
            "B12 awaiting", "%s is awaiting explicit approval" % _MODES[1]
        )

    def test_B13_reverse_order_negative_is_allowed(self):
        """Negation must win in the reverse order too."""
        self._expect_clean(
            "B13 reverse negated",
            "the %s is not proven by %s" % (_SCOPE_NOUN, _MODES[2]),
        )

    def test_B14_reverse_order_unproven_is_allowed(self):
        self._expect_clean(
            "B14 reverse unproven",
            "nothing about the %s is proven by %s" % (_SCOPE_NOUN, _MODES[3]),
        )


# ==========================================================================
# C. Canonical documentation gate
# ==========================================================================
@requires_rev42
class CanonicalDocGateTests(SandboxTestCase):
    """docs/m34d4/rev42_diag_modes.md must exist and carry the status vocabulary."""

    def test_C1_record_present_with_vocabulary_passes(self):
        res = self.check()
        self.assertIn("ok: " + M_VOCAB_OK, res.out, _diag("C1", "ok: " + M_VOCAB_OK, res, M_VOCAB_OK))
        self.assertNotIn("FAIL: " + M_NOREC_FAIL, res.out, _diag("C1", "record exists", res))
        self.assertNotIn("FAIL: " + M_VOCAB_FAIL, res.out, _diag("C1", "vocabulary present", res))

    def test_C2_missing_record_is_flagged(self):
        path = os.path.join(_SANDBOX, _REV42_DOC)
        self.assertTrue(os.path.isfile(path), "canonical record missing in sandbox")
        os.remove(path)
        try:
            res = self.check()
            self.assertFlagged("C2 record removed", res, M_NOREC_FAIL)
        finally:
            shutil.copyfile(
                os.path.join(_ROOT, _REV42_DOC), path
            )

    def test_C3_status_vocabulary_removed_is_flagged(self):
        def drop(s):
            return s.replace("## 1a. Per-mode hardware status", "## 1a. removed")

        with self.mutate(_REV42_DOC, drop):
            res = self.check()
            self.assertFlagged("C3 status table removed", res, M_VOCAB_FAIL)

    def test_C3b_each_required_term_is_enforced(self):
        # The record's status vocabulary changed with the M3.4D reconciliation:
        # a blanket "NOT HARDWARE PROVEN" is no longer truthful, because two of
        # the five rev42 modes HAVE been executed. The gate now requires the
        # per-mode status table and its binding to the structured hardware_runs
        # field instead.
        for term in ("IMPLEMENTED", "STATIC TESTED",
                     "## 1a. Per-mode hardware status", "hardware_runs"):
            with self.subTest(term=term):
                with self.mutate(_REV42_DOC, lambda s, t=term: s.replace(t, "removed")):
                    res = self.check()
                    self.assertFlagged(
                        "C3b %s removed" % term, res, M_VOCAB_FAIL
                    )

    def test_C4_positive_claim_injected_into_record_is_flagged(self):
        extra = "\n\n%s %s HARDWARE PROVEN.\n" % (_MODES[0], "is")
        with self.mutate(_REV42_DOC, lambda s: s + extra):
            res = self.check()
            self.assertAnyFlagged("C4 claim injected into record", res, StatusClaimGuardTests.CLAIM_GUARDS)


# ==========================================================================
# D. Wiring / mode declarations
# ==========================================================================
@requires_rev42
class WiringGateTests(SandboxTestCase):
    """Removing any required wiring component must fail the gate."""

    def assertWiringFail(self, case, res, mode):
        self.assertFlagged(case, res, M_WIRE_FAIL + mode)

    def test_D1_missing_module_param_is_flagged(self):
        mode = _MODES[0]
        with self.mutate(
            "src/ob_core.c", self.strip_line(r"^module_param\(" + re.escape(mode) + r",.*\n")
        ):
            self.assertWiringFail("D1 module param removed", self.check(), mode)

    def test_D2_missing_enum_value_is_flagged(self):
        mode = _MODES[1]
        with self.mutate(
            "src/ob_ucode.h", self.strip_line(r"^.*OB_ISOLATED_REV42.*\n")
        ):
            self.assertWiringFail("D2 enum removed", self.check(), mode)

    def test_D3_missing_dispatch_branch_is_flagged(self):
        mode = _MODES[2]
        with self.mutate(
            "src/ob_core.c",
            self.strip_line(r"^\s*hw->" + re.escape(mode) + r" = true;\s*\n"),
        ):
            self.assertWiringFail("D3 dispatch removed", self.check(), mode)

    def test_D4_missing_state_field_is_flagged(self):
        mode = _MODES[3]
        with self.mutate(
            "src/ob_core.h",
            self.strip_line(r"^\s*bool\s+" + re.escape(mode) + r";\s*\n"),
        ):
            self.assertWiringFail("D4 state field removed", self.check(), mode)

    def test_D5_missing_entry_point_declaration_is_flagged(self):
        with self.mutate(
            "src/ob_diag.h", self.strip_line(r"^int\s+ob_diag_test\(.*\n")
        ):
            self.assertFlagged("D5 declaration removed", self.check(), M_ENTRY_FAIL + "ob_diag_test")

    def test_D5b_missing_entry_point_dispatch_is_flagged(self):
        with self.mutate(
            "src/ob_core.c", self.strip_line(r"^\s*ret = ob_attach_test\(hw\);\s*\n")
        ):
            self.assertFlagged("D5b dispatch removed", self.check(), M_ENTRY_FAIL + "ob_attach_test")

    def test_D6_missing_remove_hook_is_flagged(self):
        with self.mutate(
            "src/ob_diag.h", self.strip_line(r"^void\s+ob_diag_remove\(.*\n")
        ):
            res = self.check()
            self.assertFlagged("D6 remove hook removed", res, M_REMOVE_FAIL)

    def test_D6b_remove_hook_present_in_baseline(self):
        res = self.check()
        self.assertIn("ok: " + M_REMOVE_OK, res.out, _diag("D6b", "remove hook ok", res, M_REMOVE_OK))

    def test_D7_baseline_all_modes_wired(self):
        res = self.check()
        for mode in _MODES:
            self.assertIn("ok: " + M_WIRE_OK + mode, res.out, _diag("D7", mode + " wired", res, mode))
        for fn in ("ob_diag_test", "ob_attach_test", "ob_sb_reset_test", "ob_ai_probe_test"):
            self.assertIn(
                "ok: " + M_ENTRY_OK + fn, res.out, _diag("D7", fn + " declared", res, fn)
            )


# ==========================================================================
# E. Forbidden surface containment (through the real gate)
# ==========================================================================
@requires_rev42
class ContainmentGateTests(SandboxTestCase):
    """A real call into any forbidden stage is detected; mentions are allowed."""

    def inject_call(self, ident):
        """Insert a real call that shares its line with a string literal.

        The string prefix guarantees the line-based shell filter cannot see it,
        so only the comment/string-aware scan can catch it -- which is exactly
        the coverage we want pinned for every forbidden identifier.
        """
        return self.mutate(
            _OB_DIAG_C,
            lambda s: s.replace(
                "int ob_diag_test(struct ob_hw *hw)\n{",
                'int ob_diag_test(struct ob_hw *hw)\n{\n\tdev_info(hw->dev, "probe"); %s(hw);\n'
                % ident,
                1,
            ),
        )

    def test_E1_each_forbidden_identifier_is_detected(self):
        for ident in ALL_FORBIDDEN:
            with self.subTest(ident=ident):
                with self.inject_call(ident):
                    res = self.check()
                    self.assertFlagged("E1 %s called" % ident, res, M_SCAN_FAIL)

    def test_E2_mentions_only_stay_allowed(self):
        source = (
            "/* wlc_phy_init( in a block comment */\n"
            "// wlc_phy_anacore( in a line comment\n"
            "void probe(struct device *dev)\n"
            "{\n"
            '\tdev_info(dev, "wlc_phy_switch_radio( labelled");\n'
            '\tdev_info(dev, "request_irq( / ob_irq_setup( labels");\n'
            "}\n"
        )
        with self.mutate(_OB_DIAG_C, lambda s: s + "\n" + source):
            res = self.check()
            self.assertNotFlagged(
                "E2 mentions only", res, (M_SCAN_FAIL, M_PHY_FAIL, M_IRQ_FAIL),
                (M_SCAN_OK, M_PHY_OK, M_IRQ_OK),
            )

    def test_E3_shell_layer_catches_a_plain_call(self):
        with self.mutate(
            _OB_DIAG_C,
            lambda s: s.replace(
                "int ob_diag_test(struct ob_hw *hw)\n{",
                "int ob_diag_test(struct ob_hw *hw)\n{\n\twlc_phy_init(hw);\n",
                1,
            ),
        ):
            res = self.check()
            self.assertFlagged("E3 plain PHY call", res, M_PHY_FAIL)

    def test_E4_host_irq_call_is_flagged(self):
        with self.inject_call("request_irq"):
            res = self.check()
            self.assertFlagged("E4 request_irq", res, M_SCAN_FAIL)

    def test_E5_baseline_is_clean(self):
        res = self.check()
        self.assertNotFlagged(
            "E5 baseline", res, (M_SCAN_FAIL, M_PHY_FAIL, M_IRQ_FAIL),
            (M_SCAN_OK, M_PHY_OK, M_IRQ_OK),
        )


# ==========================================================================
# F. The mandatory blind-spot witness
# ==========================================================================
@requires_rev42
class ShellBlindSpotTests(SandboxTestCase):
    """Encode the exact historic regression that motivated the string-aware scan.

    The line-based filter used by section 4l-3 (documented in the gate itself)
    drops any line containing a double quote. On the witness line below it
    therefore reports nothing, while section 4l-3b's scanner finds the call.
    This test fails loudly if 4l-3b is ever removed or weakened.
    """

    WITNESS = '\tdev_info(hw->dev, "x"); wlc_phy_init(hw);\n'

    def _shell_filter(self, path):
        """Reproduce the line-based filter exactly as docs-check.sh defines it.

        This is a regression WITNESS, not gate logic: it demonstrates the old
        behaviour that the string-aware scanner was added to supersede.
        """
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        lines = text.splitlines(True)
        out = []
        for line in lines:
            line = re.sub(r"//.*", "", line)
            if re.match(r"^\s*(\*|/\*|\*/)", line):
                continue
            if '"' in line:
                continue
            out.append(line)
        return "".join(out)

    def test_F1_witness_line_is_invisible_to_the_shell_filter(self):
        with self.mutate(
            _OB_DIAG_C,
            lambda s: s.replace(
                "int ob_diag_test(struct ob_hw *hw)\n{",
                "int ob_diag_test(struct ob_hw *hw)\n{" + self.WITNESS,
                1,
            ),
        ):
            path = os.path.join(_SANDBOX, _OB_DIAG_C)
            filtered = self._shell_filter(path)
            self.assertNotIn(
                "wlc_phy_init", filtered,
                "witness line was visible to the shell filter; the historic "
                "blind spot no longer reproduces, so this test must be reviewed",
            )
            # ...and the real gate must still catch it.
            res = self.check()
            self.assertFlagged("F1 witness line", res, M_SCAN_FAIL)
            # The shell layer's own opinion is unchanged (it misses it) ...
            self.assertIn("ok: " + M_PHY_OK, res.out, _diag("F1", M_PHY_OK, res, M_PHY_OK))
            # ... while the scan below reports the finding.
            with open(path, "r", encoding="utf-8") as f:
                text = f.read()
            self.assertIn("wlc_phy_init(", text, "witness call was not written into the fixture")

    def test_F2_scanner_finds_the_witness_line_directly(self):
        with tempfile.TemporaryDirectory(prefix="ob-witness-") as tmp:
            path = os.path.join(tmp, "witness.c")
            with open(path, "w", encoding="utf-8") as f:
                f.write("void probe(struct hw *hw)\n{\n" + self.WITNESS + "}\n")
            res = self.scan(path, FORBIDDEN_PHY)
        self.assertEqual(res.rc, 1, _diag("F2 scanner", "rc=1", res))
        self.assertIn("wlc_phy_init", res.stdout, _diag("F2 scanner", "names the call", res))


# ==========================================================================
# G. Self-reference regression (preserved)
# ==========================================================================
class SelfReferenceRegressionTests(SandboxTestCase):
    """The generated export embeds the checker itself; it must not be scanned.

    These run against the real tree but only *execute* the checker read-only.
    """

    def test_G1_self_reference_does_not_create_a_claim_violation(self):
        res = _run(["sh", _CHECK], _ROOT)
        self.assertIn(
            "ok: M3.4D3 (band-switch/PHY boundary) not claimed hardware proven",
            res.out,
            _diag("G1 self-reference", "the self-reference gate passes", res),
        )
        self.assertFalse(
            any(line.startswith("FAIL:") and "claims M3.4D3" in line for line in res.out.splitlines()),
            _diag("G1 self-reference", "no exported checker text is treated as a claim", res),
        )

    def test_G2_generated_export_is_not_a_violation(self):
        if not os.path.isdir(_EXPORT_DIR):
            self.skipTest("docs/dify-export not present")
        res = _run(["sh", _CHECK], _ROOT)
        offenders = [
            l for l in res.out.splitlines()
            if l.strip().startswith("FAIL") and "claims M3.4D3" in l
        ]
        self.assertEqual(offenders, [], _diag("G2 self-reference", "no false positive", res))

    def test_G3_real_violation_in_docs_is_still_rejected(self):
        """The exclusion must not silence a genuine violation (sandboxed)."""
        with self.add_doc(
            "_test_m34d3.md", "M3.4D3 band-switch boundary is HARDWARE PROVEN."
        ):
            res = self.check()
            self.assertIn(
                "FAIL: a document claims M3.4D3", res.out,
                _diag("G3 real M3.4D3 violation", "must be rejected", res),
            )

    def test_G4_negative_controls_leave_no_claim_violation(self):
        res = self.check()
        self.assertIn(
            "ok: no unsupported positive proof claim for any rev42 probe mode",
            res.out,
            _diag("G4 clean after controls", "the negative controls leave no proof claim", res),
        )
        self.assertFalse(
            any(line.startswith("FAIL:") and "claims M3.4D3" in line for line in res.out.splitlines()),
            _diag("G4 clean after controls", "sandboxed negative controls are reverted", res),
        )


# ==========================================================================
# H. M3.4D reconciliation controls
#
# These are the five mutation checks the M3.4D reconciliation was required to
# demonstrate, turned into permanent regressions. Each one reintroduces a stale
# claim or identifier that the reconciliation retired, and asserts that the
# governance layer catches it. Every mutation is applied to the SANDBOX copy
# only and reverted by the context manager; the real tree is never touched.
# ==========================================================================
_EXECUTED_MODES = ("rev42_" + "ai_probe_only", "rev42_" + "ai_enable_only")
_NEVER_RUN_MODE = _MODES[0]
_OB_DIAG_H = "src/ob_diag.h"
_STATE = "docs/state/current-state.json"
_LEDGER = "docs/artifact-ledger.json"
_REV42_AI_FREEZE = "docs/m34d4/rev42_ai_enable_freeze.md"
_ADDR_PROV = "docs/m34d4/addr_provenance.md"


@requires_rev42
class M34DReconciliationControlTests(SandboxTestCase):
    """The retired assumptions must stay retired."""

    def _assert_structured_fails(self, case, res):
        self.assertIn(
            "FAIL: " + M_STRUCT_FAIL, res.out,
            _diag(case, "structured invariants must fail", res, M_STRUCT_FAIL),
        )

    def _assert_structured_ok(self, case, res):
        self.assertIn(
            "ok: " + M_STRUCT_OK, res.out,
            _diag(case, "structured invariants must hold", res, M_STRUCT_OK),
        )

    # -- H1: the retired 0x0FFC "core ID" naming must not come back ------
    def test_H1_core_id_naming_in_source_is_flagged(self):
        for relpath, old, new in (
            (_OB_DIAG_H, "OB_DIAG_" + "PROBE_OFF_0FFC", "OB_DIAG_" + "CORE_ID_0FFC"),
            ("tests/host/ob_diag_test.c", "OB_DIAG_" + "PROBE_OFF_0FFC",
             "OB_DIAG_" + "CORE_ID_0FFC"),
        ):
            with self.subTest(relpath=relpath):
                with self.mutate(relpath, lambda s, o=old, n=new: s.replace(o, n)):
                    res = self.check()
                    self._assert_structured_fails("H1 %s" % relpath, res)

    def test_H1b_core_id_log_token_in_source_is_flagged(self):
        # Phase 36Z: the 0x0FFC read the old 'probe_0ffc=' token named is
        # deleted with the retired mode, so the neutral-token rule now binds
        # the wrapper-only marker that replaced it. Renaming it back to a
        # core-window claim must still be caught.
        relpath = "src/ob_diag.c"
        with self.mutate(
            relpath, lambda s: s.replace("window=wrapper", "window=core")
        ):
            res = self.check()
            self._assert_structured_fails("H1b log token", res)

    # -- H2: 0x0FFC must not return as a readability discriminator ---------
    def test_H2_readability_discriminator_claim_is_flagged(self):
        bad = (
            "\n\nThe %s value at 0x0FFC is the D11 readability "
            "discriminator for the core window.\n" % _NEVER_RUN_MODE
        )
        with self.mutate(_ADDR_PROV, lambda s: s + bad):
            res = self.check()
            self._assert_structured_fails("H2 readability discriminator", res)

    def test_H2b_liveness_discriminator_claim_is_flagged(self):
        bad = (
            "\n\n0x0FFC serves as the D11 liveness discriminator.\n"
        )
        with self.mutate(_ADDR_PROV, lambda s: s + bad):
            res = self.check()
            self._assert_structured_fails("H2b liveness discriminator", res)

    # -- H3: the RUN 1 config sentinel must stay INVALID_EVIDENCE ---------
    def test_H3_dropping_run1_invalid_evidence_is_flagged(self):
        def drop(_s):
            with open(os.path.join(_SANDBOX, _STATE), encoding="utf-8") as fh:
                state = json.load(fh)
            for run in state["hardware_runs"]:
                if run.get("run") == 1:
                    run["invalid_evidence"] = []
            with open(os.path.join(_SANDBOX, _STATE), "w", encoding="utf-8") as fh:
                json.dump(state, fh, indent=2)
            return ""

        with self.mutate(_STATE, drop):
            res = self.check()
            self._assert_structured_fails("H3 run1 invalid evidence", res)

    def test_H3b_revalidating_run1_cfg_as_evidence_is_flagged(self):
        bad = (
            "\n\nThe RUN 1 config readback `cfg 0x80 = 0xffffffff` is valid "
            "evidence that the window was not selected.\n"
        )
        with self.mutate(_ADDR_PROV, lambda s: s + bad):
            res = self.check()
            self.assertNotEqual(res.rc, 0, _diag("H3b", "gate must fail", res))

    # -- H4: an executed mode must not be described as never executed -----
    def test_H4_restoring_not_executed_is_flagged(self):
        bad = (
            "\n\nThe candidate rev42_ai_enable_only has never been executed "
            "on hardware.\n"
        )
        with self.mutate(_REV42_AI_FREEZE, lambda s: s + bad):
            res = self.check()
            self._assert_structured_fails("H4 not-executed restored", res)

    def test_H4b_setting_executions_to_two_is_flagged(self):
        def bump(_s):
            with open(os.path.join(_SANDBOX, _STATE), encoding="utf-8") as fh:
                state = json.load(fh)
            for run in state["hardware_runs"]:
                run["executions"] = 2
            with open(os.path.join(_SANDBOX, _STATE), "w", encoding="utf-8") as fh:
                json.dump(state, fh, indent=2)
            return ""

        with self.mutate(_STATE, bump):
            res = self.check()
            self._assert_structured_fails("H4b executed twice", res)

    # -- H5: a superseded window base must not return as a current fact ---
    def test_H5_guessed_wrapper_base_as_current_fact_is_flagged(self):
        bad = (
            "\n\nOn this board the D11 core base is 0x18000000 and the wrapper "
            "base is 0x18100000.\n"
        )
        with self.mutate(_ADDR_PROV, lambda s: s + bad):
            res = self.check()
            self._assert_structured_fails("H5 guessed bases", res)

    def test_H5b_hardcoding_a_window_base_in_source_is_flagged(self):
        with self.mutate(
            _OB_DIAG_H, lambda s: s + "\n#define OB_BAD 0x18001000u\n"
        ):
            res = self.check()
            self._assert_structured_fails("H5b hardcoded base", res)

    # -- over-broad claims about an EXECUTED mode are still rejected ------
    def test_H6_over_broad_claim_about_an_executed_mode_is_flagged(self):
        for mode in _EXECUTED_MODES:
            with self.subTest(mode=mode):
                bad = "\n\n%s is HARDWARE PROVEN.\n" % mode
                with self.mutate(_REV42_AI_FREEZE, lambda s, b=bad: s + b):
                    res = self.check()
                    # Rejected either by the structured scope guard (the mode
                    # may not be said to have proved a downstream stage) or by
                    # the claim guard itself.
                    self.assertNotEqual(
                        res.rc, 0,
                        _diag("H6 over-broad %s" % mode, "must be rejected", res),
                    )

    # -- milestone disagreement is rejected -------------------------------
    def test_H7_milestone_disagreement_is_flagged(self):
        def disagree(_s):
            with open(os.path.join(_SANDBOX, _STATE), encoding="utf-8") as fh:
                state = json.load(fh)
            state["last_hardware_proven_milestone"]["id"] = "M3.4D3A1"
            with open(os.path.join(_SANDBOX, _STATE), "w", encoding="utf-8") as fh:
                json.dump(state, fh, indent=2)
            return ""

        with self.mutate(_STATE, disagree):
            res = self.check()
            self._assert_structured_fails("H7 milestone disagreement", res)

    # -- a clean tree passes ----------------------------------------------
    def test_H8_clean_tree_passes_the_structured_invariants(self):
        res = self.check()
        self._assert_structured_ok("H8 clean tree", res)


# ==========================================================================
# I. M3 D11 FIFO poll policy: the fail-closed contract and its documentation
#    are one gate.  A revert to the vendor fallthrough must be flagged in code
#    and in the hardware-safety record, not just in review.
# ==========================================================================
M_POLL_FAIL = "D3A1 poll expiry must be fail-closed"
M_POLL_OK = "D3A1 poll expiry is fail-closed"
M_POLL_DMA_FAIL = "D3A1 timeout failure record / no-DMA-teardown contract missing"
M_POLL_DMA_OK = "D3A1 timeout records the failing stage and does not tear down unstarted DMA"
M_POLL_DOC_FAIL = "docs/m34d3a1_vendor_tail_test.md must document the fail-closed poll policy"
M_POLL_DOC_OK = "D3A1 fail-closed poll policy is documented"


class D3A1PollPolicyGateTests(SandboxTestCase):
    """M3: expiry is a failed postcondition, in code and in the record."""

    def test_I0_clean_tree_passes_the_poll_gates(self):
        res = self.check()
        self.assertNotFlagged(
            "clean poll policy", res,
            [M_POLL_FAIL, M_POLL_DMA_FAIL, M_POLL_DOC_FAIL],
            [M_POLL_OK, M_POLL_DMA_OK, M_POLL_DOC_OK],
        )

    def test_I1_expiry_without_unverified_mark_is_flagged(self):
        with self.mutate(
            "src/ob_d3a1.c",
            lambda s: s.replace(
                "fifo_state_unverified = true;",
                "fifo_state_unverified = false;",
            ),
        ):
            res = self.check()
            self.assertFlagged("unverified mark removed", res, M_POLL_FAIL)

    def test_I2_dma_teardown_on_fixup_failure_is_flagged(self):
        old = (
            "ob_d3a1_fail_record(hw, OB_D3A1_FAIL_FIXUP, ret, false,\n"
            "\t\t\t\t    OB_D3A1_TEARDOWN_NOT_NEEDED);"
        )
        new = (
            "ob_d3a1_fail_record(hw, OB_D3A1_FAIL_FIXUP, ret, false,\n"
            "\t\t\t\t    OB_D3A1_TEARDOWN_OK);"
        )
        with self.mutate(
            "src/ob_d3a1.c", lambda s: s.replace(old, new)
        ):
            res = self.check()
            self.assertFlagged("fixup teardown", res, M_POLL_DMA_FAIL)

    def test_I3_hardware_record_losing_the_contract_is_flagged(self):
        with self.mutate(
            "docs/m34d3a1_vendor_tail_test.md",
            lambda s: s.replace("fail-closed", "vendor fallthrough"),
        ):
            res = self.check()
            self.assertFlagged("doc revert", res, M_POLL_DOC_FAIL)


if __name__ == "__main__":
    unittest.main()
