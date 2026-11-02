#!/usr/bin/env python3
"""Independent instruction-derived callback oracle for selected arg-zero arm."""
import subprocess
import unittest
import json
import hashlib
from pathlib import Path

from tests.host.test_bcm4352_sub_9175a_91b6e import expected_mask1_trace
from tests.host.test_bcm4352_sub_9a121_active_core import (
    pack_words, selected_sub_8f086,
)

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "tests/host/ob_phy_9a539_test"


def expected(chanspec):
    # Callback kind values are the fake-backend protocol: suspend=0, read=1,
    # write=2, ACI software update=3, enable=4. Source sites come from the
    # admitted call instructions in sub_9a539/sub_8f84d/sub_90b77.
    out = [(0, 0x9A58F, 0, 0), (1, 0x9A59C, 0x019E, 0xA5A5),
           (1, 0x9A5BA, 0x019E, 0xA5A5), (2, 0x9A5BA, 0x019E, 0xA5A7)]
    regs = [0x324, 0x330, 0x321, 0x32D, 0x32A, 0x336, 0x327, 0x333]
    sites = [0x8F86F, 0x8F884, 0x8F899, 0x8F8AE,
             0x8F8C3, 0x8F8D8, 0x8F8ED, 0x8F902]
    for site, reg in zip(sites, regs):
        out.extend([(1, site, reg, 0), (2, site, reg, 0x3600)])
    for site, reg in zip([0x90BA0, 0x90BB0, 0x90BC0, 0x90BD0],
                         [0x304, 0x307, 0x30A, 0x30D]):
        out.append((2, site, reg, 0x4E51))
    if not (chanspec & 0xC000):
        out.extend([(2, 0x9A5F7, 0x299, 0x4477),
                    (2, 0x9A840, 0x3C1, 0x0010)])
    out.extend([(3, 0x9A848, 0, 0), (1, 0x9A86A, 0x019E, 0xA5A7),
                (2, 0x9A86A, 0x019E, 0xA5A5), (4, 0x9A877, 0, 0)])
    return out


def expected_nonzero_high_band():
    """Instruction-derived explicit fixture: PI+164=0, two loops, no cores."""
    out = [(0, 0x9A58F, 0, 0), (1, 0x9A59C, 0x019E, 0xA5A5),
           (1, 0x9A5BA, 0x019E, 0xA5A5), (2, 0x9A5BA, 0x019E, 0xA5A7)]
    out.extend((2, site, reg, 0x5F62) for site, reg in zip(
        (0x90BA0, 0x90BB0, 0x90BC0, 0x90BD0),
        (0x0304, 0x0307, 0x030A, 0x030D)))
    regs = {0x019E: 0xA5A7}

    def rmw(site, reg, mask, value):
        old = regs.get(reg, 0)
        out.append((1, site, reg, old))
        new = (old & (~mask & 0xFFFF)) | (value & mask)
        regs[reg] = new
        out.append((2, site, reg, new))

    g0_regs = (0x0324, 0x0330, 0x0321, 0x032D,
               0x032A, 0x0336, 0x0327, 0x0333)
    g0_sites = (0x8F64C, 0x8F661, 0x8F676, 0x8F68B,
                0x8F6A0, 0x8F6B5, 0x8F6CA, 0x8F829)
    g1_regs = (0x0910, 0x0910, 0x0912, 0x0912,
               0x0911, 0x0911, 0x0913, 0x0913)
    g1_masks = (0xFF00, 0x00FF, 0xFF00, 0x00FF,
                0x00FF, 0xFF00, 0x00FF, 0xFF00)
    g1_sites = (0x8F6F1, 0x8F706, 0x8F71B, 0x8F730,
                0x8F745, 0x8F75A, 0x8F76F, 0x8F829)
    g2_regs = (0x0B10, 0x0B10, 0x0B12, 0x0B12,
               0x0B11, 0x0B11, 0x0B13, 0x0B13)
    g2_masks = g1_masks
    g2_sites = (0x8F796, 0x8F7AB, 0x8F7C0, 0x8F7D5,
                0x8F7EA, 0x8F7FF, 0x8F814, 0x8F829)
    for _core in range(2):
        for site, reg in zip(g0_sites, g0_regs):
            rmw(site, reg, 0x00FF, 0x36)
        for site, reg, mask in zip(g1_sites, g1_regs, g1_masks):
            rmw(site, reg, mask, 0)
        for site, reg, mask in zip(g2_sites, g2_regs, g2_masks):
            rmw(site, reg, mask, 0)

    for site, reg in zip((0x8F86F, 0x8F884, 0x8F899, 0x8F8AE,
                          0x8F8C3, 0x8F8D8, 0x8F8ED, 0x8F902),
                         g0_regs):
        rmw(site, reg, 0xFF00, 0x3600)
    out.extend([(3, 0x9A848, 0, 0)])
    rmw(0x9A86A, 0x019E, 0x0002, 0)
    out.append((4, 0x9A877, 0, 0))
    return out


def expected_nonzero_low_band():
    """Instruction-derived row indexing and low-band PHY writes.

    The fixture rows are explicit synthetic bytes; the vendor literals remain
    research-only. nested+0x669 is 2, so index=(min(2,24)+1)>>1 == 1.
    The copied count row is .rodata+0x2cc700 (0x31, clamped to 12).
    The PHY[0x0299] byte comes from .rodata+0x2cc730 (4), and is ORed into DL
    while 0x44 is ORed into DH. PHY[0x03c1] is the target little-endian word.
    """
    out = expected_nonzero_high_band()
    aci_pos = next(i for i, event in enumerate(out)
                   if event[0] == 3 and event[1] == 0x9A848)
    out[aci_pos:aci_pos] = [
        (2, 0x9A82D, 0x0299, 0x4404),
        (2, 0x9A840, 0x03C1, 0x4121),
    ]
    return out


class Sub9a539TraceTests(unittest.TestCase):
    def trace(self, chanspec):
        raw = subprocess.check_output([str(BIN), "--trace", hex(chanspec)], text=True)
        rows = [line.split() for line in raw.splitlines()]
        events = [tuple(int(x, 16) for x in row) for row in rows if row[0] != "RESULT"]
        summary = next(row for row in rows if row[0] == "RESULT")
        return events, (int(summary[1]), int(summary[2]), int(summary[3], 16))

    def nonzero_trace(self, argument):
        raw = subprocess.check_output(
            [str(BIN), "--trace-nonzero", hex(argument)], text=True)
        rows = [line.split() for line in raw.splitlines()]
        events = [tuple(int(x, 16) for x in row)
                  for row in rows if row[0] != "RESULT"]
        summary = next(row for row in rows if row[0] == "RESULT")
        return events, (int(summary[1]), int(summary[2]), int(summary[3], 16))

    def nonzero_lowband_trace(self):
        raw = subprocess.check_output(
            [str(BIN), "--trace-nonzero-lowband"], text=True)
        rows = [line.split() for line in raw.splitlines()]
        events = [tuple(int(x, 16) for x in row)
                  for row in rows if row[0] != "RESULT"]
        summary = next(row for row in rows if row[0] == "RESULT")
        return events, (int(summary[1]), int(summary[2]), int(summary[3], 16))

    def nonzero_lowband_core_trace(self):
        raw = subprocess.check_output(
            [str(BIN), "--trace-nonzero-lowband-core"], text=True)
        rows = [line.split() for line in raw.splitlines()]
        events = [tuple(int(x, 16) for x in row)
                  for row in rows if row[0] not in ("RESULT", "TABLE", "SELECTORS")]
        summary = next(row for row in rows if row[0] == "RESULT")
        selectors = next(row for row in rows if row[0] == "SELECTORS")
        tables = []
        for row in rows:
            if row[0] == "TABLE":
                tables.append((int(row[1]), int(row[2]), int(row[3]),
                               int(row[4]), int(row[5]), int(row[6]),
                               int(row[7]), tuple(int(x, 16) for x in row[8:])))
        return (events,
                (int(summary[1]), int(summary[2]), int(summary[3], 16)),
                tables, tuple(int(x, 16) for x in selectors[1:]))

    def nonzero_998cc1_trace(self, nested_66c=5):
        raw = subprocess.check_output(
            [str(BIN), "--trace-nonzero-998cc1", str(nested_66c)], text=True)
        rows = [line.split() for line in raw.splitlines()]
        events = [tuple(int(x, 16) for x in row)
                  for row in rows if row[0] not in ("RESULT", "PAYLOAD")]
        summary = next(row for row in rows if row[0] == "RESULT")
        payload = next(row for row in rows if row[0] == "PAYLOAD")
        return (events,
                (int(summary[1]), int(summary[2]), int(summary[3], 16)),
                tuple(int(x, 16) for x in payload[2:]))

    def nonzero_99658arg1_trace(self):
        raw = subprocess.check_output(
            [str(BIN), "--trace-nonzero-99658arg1"], text=True)
        rows = [line.split() for line in raw.splitlines()]
        events = [tuple(int(x, 16) for x in row)
                  for row in rows if row[0] not in ("RESULT", "PAYLOAD")]
        summary = next(row for row in rows if row[0] == "RESULT")
        payloads = [tuple(int(x, 16) for x in row[2:])
                    for row in rows if row[0] == "PAYLOAD"]
        return (events,
                (int(summary[1]), int(summary[2]), int(summary[3], 16)),
                payloads)

    def nonzero_child_trace(self, option, *args):
        raw = subprocess.check_output([str(BIN), option, *map(str, args)], text=True)
        rows = [line.split() for line in raw.splitlines()]
        events = [tuple(int(x, 16) for x in row)
                  for row in rows if row[0] not in ("RESULT", "PAYLOAD")]
        summary = next(row for row in rows if row[0] == "RESULT")
        payloads = [tuple(int(x, 16) for x in row[2:])
                    for row in rows if row[0] == "PAYLOAD"]
        return events, (int(summary[1]), int(summary[2]), int(summary[3], 16)), payloads

    def nonzero_per_core_trace(self, mask, cap=1):
        option = "--trace-nonzero-per-core" if cap == 1 else "--trace-nonzero-per-core-cap"
        args = [str(BIN), option, str(mask)]
        if cap != 1:
            args.append(str(cap))
        raw = subprocess.check_output(args, text=True)
        rows = [line.split() for line in raw.splitlines()]
        events = [tuple(int(x, 16) for x in row)
                  for row in rows if row[0] not in ("RESULT", "TABLE", "SELECTORS")]
        summary = next(row for row in rows if row[0] == "RESULT")
        tables = []
        selectors = None
        for row in rows:
            if row[0] == "TABLE":
                tables.append((int(row[1]), int(row[2]), int(row[3]),
                               int(row[4]), int(row[5]), int(row[6]),
                               int(row[7]), tuple(int(x, 16) for x in row[8:])))
            elif row[0] == "SELECTORS":
                selectors = tuple(int(x, 16) for x in row[1:])
        return (events,
                (int(summary[1]), int(summary[2]), int(summary[3], 16)),
                tables, selectors)

    def table_active_trace(self):
        raw = subprocess.check_output([str(BIN), "--trace-table-active"], text=True)
        events, tables, matrix, selector = [], [], None, None
        summary = None
        for row in (line.split() for line in raw.splitlines()):
            if row[0] == "RESULT":
                summary = (int(row[1]), int(row[2]), int(row[3], 16))
            elif row[0] == "MATRIX":
                matrix = tuple(int(x, 16) for x in row[1:])
            elif row[0] == "SELECTOR":
                selector = int(row[1], 16)
            elif row[0] == "TABLE":
                tables.append((int(row[1]), int(row[2]), int(row[3]),
                               int(row[4]), int(row[5]), int(row[6]),
                               int(row[7]), int(row[8]),
                               tuple(int(x, 16) for x in row[9:])))
            else:
                events.append(tuple(int(x, 16) for x in row))
        return events, tables, matrix, selector, summary

    @staticmethod
    def expected_per_core(mask):
        # Direct instruction semantics applied to the explicit fixture rows.
        events = expected_nonzero_high_band()
        insert = []
        tables = []
        for core in range(2):
            if not (mask & (1 << core)):
                continue
            generated = [core * 6 + k + 1 for k in range(6)]
            reg = 0x06dc + core * 0x0200
            first = ((generated[3] << 7) | (generated[2] << 4) |
                     (generated[1] << 1)) & 0xffff
            second = ((generated[4] << 4) | (generated[5] << 8) | 8) & 0xffff
            table_word = ((generated[4] << 14) | (generated[5] << 13) |
                          generated[1] | (generated[3] << 6) |
                          (generated[2] << 3)) & 0xffff
            insert.extend([(2, 0x99fef, reg, first),
                           (2, 0x99fff, reg + 1, second),
                           (5, 0x9a04f, 0xf9 + core, 7)])
            tables.append((8 + 3 * len(tables) + 2, 0, core, 7, 1, 16, 2,
                           (table_word & 0xff, table_word >> 8)))
        events[8:8] = insert
        return events, tables

    def test_low_chanspec_full_selected_trace(self):
        events, summary = self.trace(0x1001)
        self.assertEqual(events, expected(0x1001))
        self.assertEqual(summary, (20, 30, 0x9A88A))

    def test_nonzero_low_band_relocated_rows_are_explicit_and_indexed(self):
        events, summary = self.nonzero_lowband_trace()
        self.assertEqual(events, expected_nonzero_low_band())
        self.assertEqual(summary, (68, 126, 0x9A88A))
        self.assertEqual(events[120:122], [
            (2, 0x9A82D, 0x0299, 0x4404),
            (2, 0x9A840, 0x03C1, 0x4121),
        ])

    def test_lowband_row_count_reaches_outer_active_core_argument(self):
        events, summary, tables, selectors = self.nonzero_lowband_core_trace()
        expected_events = expected_nonzero_low_band()
        expected_events[8:8] = [
            (2, 0x99FEF, 0x06DC, 0x0234),
            (2, 0x99FFF, 0x06DD, 0x0658),
            (5, 0x9A04F, 0x00F9, 7),
        ]
        self.assertEqual(events, expected_events)
        self.assertEqual(summary, (71, 129, 0x9A88A))
        self.assertEqual(selectors, (0x39, 0))
        self.assertEqual(tables, [(10, 0, 0, 7, 1, 16, 2, (0x1A, 0xC1))])

    def test_additive_state_overlay_pins_unchanged_predecessors(self):
        path = ROOT / "artifacts/contracts/bcm4352-sub-9a539-state-propagation-correction-v1.json"
        overlay = json.loads(path.read_text())
        self.assertEqual(overlay["status"],
                         "ADDITIVE_CORRECTION_SYNTHETIC_IMPLEMENTATION")
        provenance = overlay["provenance"]
        for path_key, hash_key in (
                ("predecessor_payload_contract", "predecessor_payload_contract_sha256"),
                ("predecessor_99658_execution_contract", "predecessor_99658_execution_contract_sha256"),
                ("predecessor_9a539_nonzero_contract", "predecessor_9a539_nonzero_contract_sha256")):
            data = (ROOT / provenance[path_key]).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), provenance[hash_key])
        self.assertFalse(overlay["host_implementation"]["hardware_ready"])

    def test_txpower_handoff_is_explicit_and_not_claimed_implemented(self):
        path = ROOT / "artifacts/contracts/bcm4352-txpwr-by-index-continuation-research-v1.json"
        contract = json.loads(path.read_text())
        self.assertEqual(contract["status"], "IMPLEMENTATION_HANDOFF_NOT_IMPLEMENTED")
        caller = contract["bounded_selected_caller_block"]
        self.assertIn("sign_extend_s8(nested+0x10+i)",
                      caller["operations"][1]["arguments"]["arg2"])
        threshold = next(op for op in caller["operations"]
                         if op.get("site") == "0xaa61e")
        self.assertIn("0x4350", threshold["predicate"])
        self.assertEqual(contract["bounded_selected_caller_block"]["operations"][1]
                         ["callee"], "wlc_phy_txpwr_by_index_acphy@0x9cdf8")
        scratch = next(op for op in contract["bounded_selected_caller_block"]["operations"]
                       if op.get("callee") == "sub_9868f")["operations"]
        transform = next(op for op in scratch
                         if op.get("site") == "0x98704-0x9873b")["software_effect"]
        self.assertIn("d1+(d2<<9)", transform)
        self.assertIn("d3+(d4<<8)", transform)
        self.assertFalse(contract["implementation_status"]["source_written"])
        self.assertFalse(contract["implementation_status"]["hardware_ready"])

    def test_high_chanspec_skips_only_two_band_writes(self):
        events, summary = self.trace(0xC001)
        self.assertEqual(events, expected(0xC001))
        self.assertEqual(summary, (18, 28, 0x9A88A))
        self.assertFalse(any(e[1] in (0x9A5F7, 0x9A840) for e in events))

    def test_selected_child_call_sites_and_rmw_width_sequence(self):
        events, _ = self.trace(0x1001)
        self.assertEqual([e[1] for e in events if e[1] in
                          (0x8F86F, 0x8F884, 0x8F899, 0x8F8AE,
                           0x8F8C3, 0x8F8D8, 0x8F8ED, 0x8F902)],
                         [0x8F86F, 0x8F86F, 0x8F884, 0x8F884,
                          0x8F899, 0x8F899, 0x8F8AE, 0x8F8AE,
                          0x8F8C3, 0x8F8C3, 0x8F8D8, 0x8F8D8,
                          0x8F8ED, 0x8F8ED, 0x8F902, 0x8F902])

    def test_nonzero_high_band_selected_trace_and_common_return(self):
        events, summary = self.nonzero_trace(1)
        self.assertEqual(events, expected_nonzero_high_band())
        self.assertEqual(summary, (66, 124, 0x9A88A))
        self.assertEqual(events[4], (2, 0x90BA0, 0x0304, 0x5F62))
        self.assertEqual(events[-1], (4, 0x9A877, 0, 0))

    def test_nonzero_selector_values_share_only_proven_nonzero_arm(self):
        one, one_summary = self.nonzero_trace(1)
        high, high_summary = self.nonzero_trace(0x80)
        self.assertEqual(one, high)
        self.assertEqual(one_summary, high_summary)

    def test_nonzero_998cc1_then_zero_core_9a121(self):
        events, summary, payload = self.nonzero_998cc1_trace()
        expected_events = expected_nonzero_high_band()
        # The caller's first TABLE comparison selects 998cc(PI,1). Its
        # accepted transfer is one TABLE API callback, then the nonzero return
        # flag selects sub_9a121 with an explicit zero active-core mask.
        expected_events[8:8] = [(5, 0x99981, 0x000B, 0x0008)]
        expected_events[9:9] = [(1, 0x9A1AF, 0x019E, 0xA5A7),
                                (1, 0x9A1CA, 0x019E, 0xA5A7),
                                (2, 0x9A1CA, 0x019E, 0xA5A7),
                                (1, 0x9A525, 0x019E, 0xA5A7),
                                (2, 0x9A525, 0x019E, 0xA5A7)]
        # Existing later operations follow the child helper; its own saved
        # bit restoration returns PHY[0x019e] to A5A5 before the parent resumes.
        self.assertEqual(events, expected_events)
        self.assertEqual(summary, (70, 130, 0x9A88A))
        self.assertEqual(payload, (0x7F,) * 6)

    def test_998cc_payload_tracks_explicit_nested_66c(self):
        default, _summary_default, payload_default = self.nonzero_998cc1_trace(5)
        alternate, _summary_alternate, payload_alternate = self.nonzero_998cc1_trace(3)
        self.assertEqual(payload_default, (0x7F,) * 6)
        self.assertEqual(payload_alternate,
                         (0x0B, 0x0C, 0x7F, 0x7F, 0x7F, 0x7F))
        self.assertEqual(len(default), len(alternate))
        self.assertEqual(default, alternate)
        self.assertEqual([e for e in default if e[1] != 0x99981],
                         [e for e in alternate if e[1] != 0x99981])

    def test_998cc_arg2_fill_tracks_each_explicit_nested_66d(self):
        traces = []
        for nested_66d in (0, 3, 6, 7):
            events, summary, payloads = self.nonzero_child_trace(
                "--trace-nonzero-998cc2", str(nested_66d))
            expected_payload = [0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16]
            start = max(6 - nested_66d, 0)
            expected_payload[start:] = [0x7F] * (7 - start)
            self.assertEqual(payloads, [tuple(expected_payload)])
            self.assertEqual(summary, (70, 130, 0x9A88A))
            traces.append(events)
        self.assertTrue(all(trace == traces[0] for trace in traces[1:]))

    def test_99658_arg1_then_zero_core_a121_independent_trace(self):
        events, summary, payloads = self.nonzero_99658arg1_trace()
        expected_events = expected_nonzero_high_band()
        # The first comparison selects sub_99658(PI,1). Its two selected
        # chain iterations each issue TABLE IDs 0x44/0x45 plus 0x64/0x65.
        table_events = [(5, 0x99875, 0x44, 8),
                        (5, 0x998AA, 0x45, 8),
                        (5, 0x99875, 0x64, 8),
                        (5, 0x998AA, 0x65, 8)]
        expected_events[8:8] = table_events
        expected_events[12:12] = [
            (1, 0x9A1AF, 0x019E, 0xA5A7),
            (1, 0x9A1CA, 0x019E, 0xA5A7),
            (2, 0x9A1CA, 0x019E, 0xA5A7),
            (1, 0x9A525, 0x019E, 0xA5A7),
            (2, 0x9A525, 0x019E, 0xA5A7),
        ]
        self.assertEqual(events, expected_events)
        self.assertEqual(summary, (73, 133, 0x9A88A))
        self.assertEqual(payloads, [(0x12, 0x12, 0x13, 0x13, 0x13, 0x13),
                                    (1, 1, 2, 2, 2, 2),
                                    (0x12, 0x12, 0x13, 0x13, 0x13, 0x13),
                                    (1, 1, 2, 2, 2, 2)])

    def test_99658_arg2_then_zero_core_a121_independent_trace(self):
        events, summary, payloads = self.nonzero_child_trace(
            "--trace-nonzero-99658arg2")
        expected_events = expected_nonzero_high_band()
        expected_events[8:8] = [(5, 0x99875, 0x44, 16),
                                (5, 0x998AA, 0x45, 16),
                                (5, 0x99875, 0x64, 16),
                                (5, 0x998AA, 0x65, 16)]
        expected_events[12:12] = [
            (1, 0x9A1AF, 0x019E, 0xA5A7),
            (1, 0x9A1CA, 0x019E, 0xA5A7),
            (2, 0x9A1CA, 0x019E, 0xA5A7),
            (1, 0x9A525, 0x019E, 0xA5A7),
            (2, 0x9A525, 0x019E, 0xA5A7),
        ]
        self.assertEqual(events, expected_events)
        self.assertEqual(summary, (73, 133, 0x9A88A))
        self.assertEqual(payloads, [(0x22, 0x22, 0x23, 0x23, 0x23, 0x23, 0x23),
                                    (1, 1, 2, 2, 2, 2, 2)] * 2)

    def test_998cc_arg2_then_zero_core_a121_independent_trace(self):
        events, summary, payloads = self.nonzero_child_trace(
            "--trace-nonzero-998cc2", "3")
        expected_events = expected_nonzero_high_band()
        expected_events[8:8] = [(5, 0x99981, 0x000B, 0x0010)]
        expected_events[9:9] = [
            (1, 0x9A1AF, 0x019E, 0xA5A7),
            (1, 0x9A1CA, 0x019E, 0xA5A7),
            (2, 0x9A1CA, 0x019E, 0xA5A7),
            (1, 0x9A525, 0x019E, 0xA5A7),
            (2, 0x9A525, 0x019E, 0xA5A7),
        ]
        self.assertEqual(events, expected_events)
        self.assertEqual(summary, (70, 130, 0x9A88A))
        self.assertEqual(payloads, [(0x10, 0x11, 0x12, 0x7F, 0x7F, 0x7F, 0x7F)])

    def test_all_four_child_variants_keep_historical_caller_order(self):
        events, summary, _payloads = self.nonzero_child_trace(
            "--trace-nonzero-all-children")
        expected_events = expected_nonzero_high_band()
        children = [
            (5, 0x99875, 0x44, 8), (5, 0x998AA, 0x45, 8),
            (5, 0x99875, 0x64, 8), (5, 0x998AA, 0x65, 8),
            (5, 0x99875, 0x44, 16), (5, 0x998AA, 0x45, 16),
            (5, 0x99875, 0x64, 16), (5, 0x998AA, 0x65, 16),
            (5, 0x99981, 0x000B, 8), (5, 0x99981, 0x000B, 16),
        ]
        expected_events[8:8] = children
        expected_events[18:18] = [
            (1, 0x9A1AF, 0x019E, 0xA5A7),
            (1, 0x9A1CA, 0x019E, 0xA5A7),
            (2, 0x9A1CA, 0x019E, 0xA5A7),
            (1, 0x9A525, 0x019E, 0xA5A7),
            (2, 0x9A525, 0x019E, 0xA5A7),
        ]
        self.assertEqual(events, expected_events)
        self.assertEqual(summary, (79, 139, 0x9A88A))

    def test_outer_per_core_selector0_masks_against_instruction_oracle(self):
        for mask in (1, 2, 3):
            with self.subTest(mask=mask):
                events, summary, tables, selectors = self.nonzero_per_core_trace(mask)
                expected_events, expected_tables = self.expected_per_core(mask)
                self.assertEqual(events, expected_events)
                self.assertEqual(tables, expected_tables)
                self.assertEqual(summary,
                                 (66 + 3 * mask.bit_count(),
                                  124 + 3 * mask.bit_count(), 0x9A88A))
                self.assertEqual(selectors[0], 0x45 if mask & 1 else 0)
                self.assertEqual(selectors[1], 0x45 if mask & 2 else 0)

    def test_outer_selector0_argument_uses_capped_count_byte(self):
        for cap, expected in ((2, 0x44), (10, 0x3C), (0xFF, 0x39)):
            with self.subTest(cap=cap):
                _events, _summary, _tables, selectors = self.nonzero_per_core_trace(1, cap)
                self.assertEqual(selectors, (expected, 0))

    def test_table_payload_mutation_reaches_internal_and_outer_active_core(self):
        events, tables, matrix, selector, summary = self.table_active_trace()
        self.assertEqual(summary, (100, 164, 0x9A88A))
        self.assertEqual(selector, 0x45)
        a = (0x12, 0x12, 0x13, 0x13, 0x13, 0x13)
        b = (1, 1, 2, 2, 2, 2)
        self.assertEqual(matrix, a + b)
        self.assertEqual(tables[:4], [
            (8, 1, 0, 0x44, 8, 6, 8, 6, a),
            (9, 1, 0, 0x45, 8, 6, 8, 6, b),
            (10, 1, 1, 0x64, 8, 6, 8, 6, a),
            (11, 1, 1, 0x65, 8, 6, 8, 6, b),
        ])
        baseline = expected_nonzero_high_band()
        expected_prefix = baseline[:8] + [
            (5, 0x99875, 0x44, 8), (5, 0x998AA, 0x45, 8),
            (5, 0x99875, 0x64, 8), (5, 0x998AA, 0x65, 8),
        ]
        self.assertEqual(events[:12], expected_prefix)
        a121 = expected_mask1_trace()
        kind_map = {0: 1, 1: 2, 2: 5, 3: 6, 4: 7}
        a121_parent = []
        for site, kind, reg in a121:
            if site == 0x9A04F:
                a121_parent.append((5, site, 0xF9, 7))
            else:
                if site == 0x91E3F:
                    reg = 0x33  # selected high-band TSSI category register
                a121_parent.append((kind_map[kind], site, reg, 0))
        actual_a121 = [(kind, site, reg) for kind, site, reg, _value in events[12:45]]
        expected_a121 = [(kind, site, reg) for kind, site, reg, _value in a121_parent]
        self.assertEqual(actual_a121, expected_a121)
        nested = bytearray([0xA5] * 0x122)
        nested[0x24:0x2A] = bytes(a)
        nested[0x60:0x66] = bytes(b)
        generated, _return_byte = selected_sub_8f086(
            nested, [1] * 6, 0, 0x45, 1,
            [0x2F, 0x2F, 0x2F, 0x34, 0x34, 0x64])
        first, second, table_word = pack_words(generated, 1)
        outer = [event for event in events if event[1] in
                 (0x99FEF, 0x99FFF, 0x9A04F)][-3:]
        self.assertEqual(outer, [(2, 0x99FEF, 0x06DC, first),
                                 (2, 0x99FFF, 0x06DD, second),
                                 (5, 0x9A04F, 0x00F9, 7)])
        self.assertEqual(tables[-1], (47, 0, 0, 0xF9, 7, 1, 16, 2,
                                      (table_word & 0xFF, table_word >> 8)))
        self.assertEqual(events[45 + 3:], baseline[8:])

    def test_parallel_handoffs_preserve_unknowns_and_exact_stop(self):
        contract_dir = ROOT / "artifacts/contracts"
        a539 = json.loads((contract_dir /
            "bcm4352-sub-9a539-nonzero-branch-extension-v1.json").read_text())
        c962 = json.loads((contract_dir /
            "bcm4352-sub-96203-research-prefix-v1.json").read_text())
        scenarios = {x["scenario"]: x for x in a539["supported_synthetic_variants"]}
        self.assertEqual(scenarios["nonzero_select_sub_99658_argument_1"]["total_callbacks"], 133)
        self.assertEqual(scenarios["nonzero_select_sub_99658_argument_2"]["total_callbacks"], 133)
        self.assertEqual(scenarios["nonzero_select_sub_998cc_argument_1"]["total_callbacks"], 130)
        self.assertEqual(scenarios["nonzero_select_sub_998cc_argument_2"]["total_callbacks"], 130)
        self.assertEqual(scenarios["all_four_table_children_then_zero_core_a121"]["total_callbacks"], 139)
        self.assertEqual(a539["entry"]["actual_nested_670"], "UNKNOWN")
        self.assertEqual(c962["caller"]["actual_predicate_value"], "UNKNOWN")
        self.assertEqual(c962["sub_96203"]["first_hardware_event"]["site"], "0x96250")
        self.assertNotIn("callback_count_from_index", c962["sub_96203"])


if __name__ == "__main__":
    unittest.main()
