#!/usr/bin/env python3
"""Static ordering checks for the BCMA-001 fail-closed gate."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/ob_si.c").read_text()
START = SOURCE.index("int ob_si_powerup(struct ob_hw *hw)")
END = SOURCE.index("int ob_si_probe(struct ob_hw *hw)", START)
POWERUP = SOURCE[START:END]
PROBE = SOURCE[SOURCE.index("int ob_si_probe(struct ob_hw *hw)"):]

assert POWERUP.count("bcma_core_enable(hw->core, 0)") == 1
assert POWERUP.count("bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST)") == 1
assert POWERUP.index("if (err)\n\t\t\treturn err;") < POWERUP.index(
    "err = ob_si_core_enable_postcondition"
)
assert POWERUP.index("err = ob_si_core_enable_postcondition") < POWERUP.index(
    "bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST)"
)
assert "return err;" in POWERUP[POWERUP.index("err = ob_si_core_enable_postcondition") :]
assert "sprom_ret = ob_si_powerup(hw);" in PROBE
assert "return sprom_ret;" in PROBE
print("test_ob_si_powerup_contract: PASS")
