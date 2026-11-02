"""Independent static/event oracle for the bounded active-core calibration calls."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path('/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/re-image-manifest.json')
VENDOR = Path('/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/vendor/wlc_hybrid.o_shipped')
WLKO = Path('/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/wlko/wl.ko')


def s8(x):
    x &= 0xff
    return x - 256 if x & 0x80 else x


def category_9175(arg, coeff, r0, r1, r2, nested65c=0, base=0):
    # Reconstructed independently from MOVSX/index instructions at 0x9189f..0x918fc.
    total = (arg + s8(coeff[0x1a + (r0 & 1)])
             + s8(coeff[0x24 + ((r0 >> 1) & 7)])
             + s8(coeff[0x2e + ((r0 >> 4) & 7)])
             + s8(coeff[0x38 + ((r0 >> 7) & 15)])
             + s8(coeff[0x42 + ((r1 >> 4) & 7)]))
    if nested65c == 1:
        total -= base
    metric = total * 10
    limits = (-40, -5, 20, 40, 55, 80, 100, 116)
    if metric <= limits[0]:
        return 0
    if metric > limits[-1]:
        return 7
    for i, (lo, hi) in enumerate(zip(limits, limits[1:])):
        if lo <= metric <= hi:
            return i if metric - lo < hi - metric else i + 1
    raise AssertionError('unreachable interval')


def expected_mask1_trace():
    # First 19 records are the independently accepted 9a121 prefix. Remaining
    # records expand direct calls: three reads, PHY RMW, RADIO RMW, then 91b6e.
    return [
        (0x9a1af,0,0x019e),(0x9a1ca,0,0x019e),(0x9a1ca,1,0x019e),
        (0x9a304,0,0x06dc),(0x99fef,1,0x06dc),(0x99fff,1,0x06dd),
        (0x9a04f,2,0),(0x9a079,1,0x06de),(0x9a10a,1,0x06df),
        (0x9a0a4,1,0x06e0),(0x9a10a,1,0x06e1),(0x9a0fa,1,0x06e4),
        (0x9a10a,1,0x06e5),(0x9a0cf,1,0x06e2),(0x9a10a,1,0x06e3),
        (0x90506,0,0x06dc),(0x9a3b4,0,0x06dc),
        (0x90506,0,0x06dc),(0x9a42c,0,0x06dc),
        (0x91825,0,0x06dc),(0x9185b,0,0x06dd),(0x91879,0,0x06dd),
        (0x91a4a,0,0x06ee),(0x91a4a,1,0x06ee),
        (0x91b57,3,0x0045),(0x91b57,4,0x0045),
        (0x91cb9,0,0x06dc),(0x91db8,0,0x06ee),(0x91db8,1,0x06ee),
        (0x91e3f,3,0x002c),(0x91e3f,4,0x002c),
        (0x9a525,0,0x019e),(0x9a525,1,0x019e),
    ]


def expected_high_category_trace():
    return expected_mask1_trace()[:19] + [
        (0x91825,0,0x06dc),(0x9185b,0,0x06dd),(0x91879,0,0x06dd),
        (0x9197d,0,0x06dd),(0x9197d,1,0x06dd),
        (0x919b2,0,0x06dd),(0x919b2,1,0x06dd),
        (0x91a4a,0,0x06ee),(0x91a4a,1,0x06ee),
        (0x91b57,3,0x0045),(0x91b57,4,0x0045),
        (0x91cb9,0,0x06dc),(0x91db8,0,0x06ee),(0x91db8,1,0x06ee),
        (0x91e3f,3,0x002c),(0x91e3f,4,0x002c),
        (0x9a525,0,0x019e),(0x9a525,1,0x019e),
    ]


class SelectedCalibrationOracle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        env = dict(os.environ)
        env['RE_IMAGE_MANIFEST'] = str(MANIFEST)
        subprocess.run(['make', '-C', 'tests/host', 'ob_phy_9a121_active_test'],
                       cwd=ROOT, env=env, check=True, capture_output=True, text=True)

    def test_mask_one_full_callback_trace(self):
        run = subprocess.run([str(ROOT/'tests/host/ob_phy_9a121_active_test'), '--trace-mask1'],
                             cwd=ROOT, check=True, capture_output=True, text=True)
        actual = []
        for line in run.stdout.splitlines():
            site, kind, reg = line.split()
            actual.append((int(site,16), int(kind), int(reg,16)))
        self.assertEqual(actual, expected_mask1_trace())

    def test_high_category_full_callback_trace(self):
        run = subprocess.run([str(ROOT/'tests/host/ob_phy_9a121_active_test'),
                              '--trace-high-mask1'], cwd=ROOT, check=True,
                             capture_output=True, text=True)
        actual = [(int(site,16),int(kind),int(reg,16))
                  for site,kind,reg in (line.split() for line in run.stdout.splitlines())]
        self.assertEqual(actual, expected_high_category_trace())

    def test_signed_coefficient_and_single_use_indexed_terms(self):
        coeff = bytearray(0x80)
        coeff[0x1a] = 0xf0
        coeff[0x24], coeff[0x2e], coeff[0x38], coeff[0x42] = 2,3,4,5
        self.assertEqual(category_9175(-30, coeff, 0, 0, 0), 0)
        coeff[0x1a] = 1
        coeff[0x2e] = 20
        self.assertEqual(category_9175(-30, coeff, 0, 0, 0), 2)
        # Sample bits select a different element at each indexed coefficient.
        coeff[0x25] = 40
        self.assertEqual(category_9175(-30, coeff, 0x0002, 0, 0), 7)

    def test_vendor_and_linked_pointer_table_relocates_all_three_names(self):
        env = dict(os.environ)
        env['RE_IMAGE_MANIFEST'] = str(MANIFEST)
        for image, offsets in ((VENDOR, ('+ 345c','+ 3460','+ 3869')),
                               (WLKO, ('+ 30d5f0','+ 30d5f4','+ 30d9fd'))):
            output = subprocess.run(['readelf','-rW',str(image)], check=True,
                                    capture_output=True, text=True).stdout
            rows = [line for line in output.splitlines()
                    if any(f'00000000002cc7{n}' in line for n in
                           ('50','58','60','68','70','78','80','88'))]
            self.assertEqual(len(rows), 8)
            self.assertTrue(all(any(off in row for off in offsets) for row in rows))
        vendor_bytes = subprocess.run(
            ['objdump','-s','-j','.rodata.str1.1','--start-address=0x3869',
             '--stop-address=0x386e',str(VENDOR)], check=True,
            capture_output=True, text=True).stdout
        self.assertIn('high.', vendor_bytes)

    def test_active_dual_core_trace_and_return_are_exercised_in_c_suite(self):
        run = subprocess.run([str(ROOT/'tests/host/ob_phy_9a121_active_test')],
                             cwd=ROOT, check=True, capture_output=True, text=True)
        self.assertIn('complete supported-scenario checks passed', run.stdout)


if __name__ == '__main__':
    unittest.main()
