#!/usr/bin/env python3
"""Independent selected-path callback oracle for sub_96203.

The chronology and formulas below are transcribed from the pinned instruction
audit, not from the C runner. The binary's fake backend uses a simple seeded
register model; this oracle independently applies the same public scenario.
"""
import subprocess
import unittest
from pathlib import Path


PHY, RADIO, CONTROL = 0, 1, 2
READ, WRITE, DELAY, FGC = 0, 1, 2, 3
SELECTED = (0x4352, 0x4360, 0xA9C4, 0xAA06)
TRACE_RUNNER = Path(__file__).resolve().with_name("ob_phy_96203_test")


class HistoricalOracle:
    def __init__(self, selector, bound, second_gate):
        self.selector = selector
        self.bound = bound
        self.second_gate = second_gate
        if selector in SELECTED:
            self.gate, self.outer_ctl, self.outer_data = 0x8EA, 0x121, 0x122
            self.poll, self.inner_ctl = 0x144, 0x15F
            self.pair_read, self.pair_write = 0x143, 0x135
        else:
            self.gate, self.outer_ctl, self.outer_data = 0x8F2, 0x133, 0x134
            self.poll, self.inner_ctl = 0x156, 0x172
            self.pair_read, self.pair_write = 0x155, 0x147
        self.phy = [(i * 37 + 0x1234) & 0xFFFF for i in range(65536)]
        self.radio = [(i * 17 + 0x3456) & 0xFFFF for i in range(65536)]
        self.events = []
        self.logical = 0

    def begin(self):
        self.logical += 1

    def emit(self, kind, site, domain, reg, value):
        self.events.append((kind, self.logical, site, domain, reg, value))

    def raw_read(self, site, domain, reg):
        value = self.phy[reg] if domain == PHY else self.radio[reg]
        if site == 0x96464:
            value = 2
        elif site == 0x964A2:
            value = 1
        self.emit(READ, site, domain, reg, value)
        return value

    def read(self, site, domain, reg):
        self.begin()
        return self.raw_read(site, domain, reg)

    def raw_write(self, site, domain, reg, value):
        self.emit(WRITE, site, domain, reg, value)
        (self.phy if domain == PHY else self.radio)[reg] = value

    def write(self, site, domain, reg, value):
        self.begin()
        self.raw_write(site, domain, reg, value)

    def rmw(self, site, domain, reg, mask, value):
        self.begin()
        old = self.raw_read(site, domain, reg)
        self.raw_write(site, domain, reg,
                       (old & (~mask & 0xFFFF)) | (value & mask))

    def delay(self, site, usec):
        self.begin()
        self.emit(DELAY, site, CONTROL, 0, usec)

    def run(self):
        self.rmw(0x96250, RADIO, self.gate, 0x80, 0x80)
        for core in range(self.bound):
            stride = core * 0x200
            saved = []
            for site, reg in ((0x96275, 0x739 + stride),
                              (0x9629A, 0x73A + stride),
                              (0x962A9, 0x725 + stride)):
                saved.append(self.read(site, PHY, reg))
            reg739 = (0x739, 0x939, 0xB39)[core]
            reg725 = (0x725, 0x925, 0xB25)[core]
            self.rmw(0x962D9, PHY, reg739, 0x80, 0x80)
            self.rmw(0x96305, PHY, reg725, 0x04, 0x04)
            ctrl, data, poll = (self.outer_ctl | stride,
                                self.outer_data | stride,
                                self.poll | stride)
            self.rmw(0x9634D, RADIO, ctrl, 0x1000, 0)
            self.delay(0x96357, 100)
            self.rmw(0x9639B, RADIO, ctrl, 0x1000, 0x1000)
            sample = self.read(0x963D5, RADIO, data)
            self.write(0x9641C, RADIO, data, sample | 0xF)
            self.delay(0x9642A, 10)
            self.read(0x96464, RADIO, poll)
            self.read(0x964A2, RADIO, poll)
            self.write(0x964FF, RADIO, data, sample & 0xFFF0)
            for site, reg, value in ((0x9650E, 0x725 + stride, saved[2]),
                                     (0x9651D, 0x739 + stride, saved[0]),
                                     (0x9652C, 0x73A + stride, saved[1])):
                self.write(site, PHY, reg, value)
        self.rmw(0x96574, RADIO, self.gate, 0x80, 0)
        if self.second_gate <= 3:
            self.rmw(0x965BF, RADIO, self.gate, 0x80, 0x80)
            read_sites = (0x9668A,0x96709,0x96788,0x96807,0x96886,0x96905,
                          0x96984,0x96A03,0x96A82,0x96B01,0x96B80,0x96BFF,
                          0x96C7E,0x96CFD)
            write_sites = (0x966CF,0x9674E,0x967CD,0x9684C,0x968CB,0x9694A,
                           0x969C9,0x96A48,0x96AC7,0x96B46,0x96BC5,0x96C44,
                           0x96CC3,0x96D45)
            for core in range(self.bound):
                stride = core * 0x200
                ctrl = self.inner_ctl | stride
                self.rmw(0x9660C, RADIO, ctrl, 0x20, 0x20)
                self.rmw(0x96650, RADIO, ctrl, 0x10, 0x10)
                for n, (rs, ws) in enumerate(zip(read_sites, write_sites)):
                    value = self.read(rs, RADIO, self.pair_read - n + stride)
                    self.write(ws, RADIO, self.pair_write - n + stride,
                               (~value) & 0xFFFF)
            self.rmw(0x96D8D, RADIO, self.gate, 0x80, 0)
        return self.events, self.logical


class Sub96203IndependentOracle(unittest.TestCase):
    def test_callback_trace_against_instruction_oracle(self):
        for selector, bound, gate in ((0x4352, 1, 3), (0x9999, 2, 4),
                                      (0x9999, 1, 3)):
            with self.subTest(selector=selector, bound=bound, gate=gate):
                proc = subprocess.run(
                    [str(TRACE_RUNNER), "--trace",
                     hex(selector), str(bound), str(gate)],
                    check=True, text=True, capture_output=True)
                lines = proc.stdout.splitlines()
                actual = []
                for line in lines:
                    if line.startswith("RESULT "):
                        result = tuple(int(x, 0) for x in line.split()[1:])
                    else:
                        actual.append(tuple(int(x, 16) if i in (2, 4, 5)
                                            else int(x, 0)
                                            for i, x in enumerate(line.split())))
                oracle = HistoricalOracle(selector, bound, gate)
                expected, logical = oracle.run()
                self.assertEqual(actual, expected,
                                 next((f"first mismatch at callback {n + 1}: "
                                       f"expected {expected[n]}, got {actual[n]}"
                                       for n in range(min(len(actual), len(expected)))
                                       if actual[n] != expected[n]),
                                      f"length expected={len(expected)} actual={len(actual)}"))
                self.assertEqual(result[1], len(expected))
                self.assertEqual(result[0], logical)
                self.assertEqual(result[2], 0x96DA0)


if __name__ == "__main__":
    unittest.main()
