#!/usr/bin/env python3
"""Vendor-contract checks for the attach prefix and the relocated AC reset.

Phase M-ACPHY-RESET-STAGE-IMPLEMENT-02 moved the PHY-type-dependent reset block
out of ob_radio_vendor_pre_id() into ob_radio_vendor_acphy_reset(), which
ob_acphy_attach_identity() issues only after the identity read and the state
publication. This test pins:

  * the pre-identity prefix emits the BCMA reset admit only: no D11 read, no
    PHY/radio register write, no MACCONTROL write,
  * the bandwidth-dependent AC value (bw | 0x0e), not the constant 0x0e,
  * the absence of the extra 0x0cc/0x0c transaction on the AC path,
  * PHY-type gating of the arm,
  * the direct D11 0x3e6 anacore write (not the 0x3fc/0x3fe PHY window),
  * the D11 +0x1e0 clock-off (proven arm A), not ChipCommon CLKCTLST.

Executable behaviour lives in ob_acphy_attach_arm_test.c; these checks cover
the MMIO shape, which no host harness can execute.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = (ROOT / "src" / "ob_radio.c").read_text()
HDR = (ROOT / "src" / "ob_radio.h").read_text()

PRE_ID_START = "static int ob_radio_vendor_pre_id("
PRE_ID_END = "int ob_radio_vendor_acphy_attach_anacore("
RESET_START = "int ob_radio_vendor_acphy_reset("
RESET_END = "int ob_radio_prepare_for_identity"


def strip_comments(text):
    """Code only: a register literal in prose is not a transaction."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def code_block(code, start_token, end_token):
    start = code.index(start_token)
    return code[start:code.index(end_token, start)]


def pre_id():
    return code_block(strip_comments(SRC), PRE_ID_START, PRE_ID_END)


def reset():
    return code_block(strip_comments(SRC), RESET_START, RESET_END)


def cflags_window(body):
    start = body.index("ob_d11_write32(hw, OB_RADIO_D11_MACCONTROL, 0x04000400u)")
    return body[start:body.index("udelay(2)", start)]


def main():
    body = pre_id()
    reset_body = reset()

    # Change 1: the prefix keeps only the proven, type-independent operations.
    required = [
        "bcma_core_enable(hw->core, 0x4u)",
        "ob_attach_core_reset_postcondition",
        "OB_ATTACH_CP1_RESET_READY",
    ]
    pos = [body.index(token) for token in required]
    assert pos == sorted(pos), "pre-ID prefix operations are not in vendor order"
    for forbidden in ("ob_d11_read16(hw, 0x03e0u)", "ob_acphy_reset_arm(",
                      "ob_d11_write16(", "ob_d11_write32(",
                      "0x04000400", "0x03e0u", "0x03e6u", "udelay(",
                      "OB_CC_CLKCTLST"):
        assert forbidden not in body, forbidden

    # Change 3: the moved block keeps the vendor's intra-block order.
    order = [
        "ob_radio_vendor_rmw_wrapper(hw, 0x300, 0x100)",
        "ob_radio_vendor_rmw_wrapper(hw, 0x006, 0x000)",
        "ob_radio_vendor_rmw_wrapper(hw, 0x004, 0x004)",
        "ob_d11_write32(hw, OB_RADIO_D11_MACCONTROL, 0x04000400u)",
        "ob_acphy_reset_arm(phy_type, OB_ACPHY_BW_STATE_ATTACH",
        "ob_radio_vendor_rmw_wrapper(hw, cflags_mask, cflags_value)",
        "udelay(2)",
        "ob_radio_vendor_rmw_wrapper(hw, 0x00e, 0x000)",
        "ob_d11_write16(hw, 0x03e6u, 0x0000u)",
        "ob_d11_read32(hw, 0x01e0u)",
        "ob_d11_write32(hw, 0x01e0u, clkctl & ~0x2u)",
    ]
    pos = [reset_body.index(token) for token in order]
    assert pos == sorted(pos), "relocated reset block is not in vendor order"

    # D2/D3: exactly one cflags transaction between the MACCONTROL write and
    # the vendor's udelay(2), and the arm constants never appear inline.
    window = cflags_window(reset_body)
    assert window.count("ob_radio_vendor_rmw_wrapper(") == 1
    for forbidden in ("0xce", "0xcc", "| 0x0e", "| 0x0c", "0x0e)", "0x0c)"):
        assert forbidden not in window, forbidden
    assert "ob_acphy_reset_arm(phy_type, OB_ACPHY_BW_STATE_ATTACH" in window
    code = strip_comments(SRC)
    assert "0x4e" not in code and "0x8e" not in code and "0xce," not in code

    # D5: single direct D11 write per vendor site, no indirect PHY window.
    assert reset_body.count("ob_d11_write16(hw, 0x03e6u, 0x0000u)") == 1
    for absent in ("OB_PHY_REG_ADDR_LATCH", "OB_PHY_REG_DATA",
                   "ob_phy_write16_pre_id", "ob_radio_vendor_phy_write16"):
        assert absent not in reset_body, absent
    assert "ob_radio_vendor_phy_write16" not in code

    # Preserved negative checks.
    assert "udelay(150)" not in reset_body
    assert "bcma_awrite32" not in reset_body
    assert "bcma_write32" not in reset_body
    assert "bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST)" not in code
    assert "ob_d11_read32(hw, OB_RADIO_D11_MACCONTROL) != 0x04000400u" not in code
    assert "OB_UCODE_REG_D11_CLK1E0" not in reset_body

    # Target 1 (FALSIFICATION_PROOF.md): arm A is the vendor arm on this target,
    # so the clock-off must use D11 +0x1e0 and never ChipCommon CLKCTLST.
    assert "OB_CC_CLKCTLST" not in code
    assert "ob_cc_read32(hw, " not in code

    # The recovered constants live in the header contract, with their binary
    # references, and are exercised by ob_acphy_attach_arm_test.c.
    assert "static inline u16 ob_acphy_clk_bwbits(u8 phy_type, u16 bw_state)" in HDR
    assert "static inline void ob_acphy_reset_arm(u8 phy_type, u16 bw_state" in HDR
    for token in ("OB_ACPHY_CFLAGS_AC_MASK", "OB_ACPHY_CFLAGS_AC_BASE",
                  "OB_ACPHY_CFLAGS_OTHER_MASK", "OB_ACPHY_CFLAGS_OTHER_BASE",
                  "OB_ACPHY_BW_STATE_ATTACH", "0x00ceu", "0x000eu",
                  "0x00ccu", "0x000cu", "0x65da6", "0xb1913", "0xbe731"):
        assert token in HDR, token
    print("test_pre_id_contract: PASS")


if __name__ == "__main__":
    main()
