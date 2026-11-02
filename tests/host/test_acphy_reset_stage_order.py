#!/usr/bin/env python3
"""Stage-order contract for the relocated AC-PHY reset.

Phase M-ACPHY-RESET-STAGE-IMPLEMENT-02 split the vendor's attach-time work into
three stages, and the vendor binary fixes where each one belongs:

  * wlc_bmac_attach / wlc_bmac_up_prep reset admit  -> before the identity read
    (ob_radio_vendor_pre_id, called from ob_radio_prepare_for_identity),
  * wlc_phy_attach 0xbed85 attach-path anacore write -> after the radio-identity
    block, before band->pi is published (0x6a140),
  * wlc_bmac_up_prep 0x66561 effective AC reset      -> after band->pi and
    band+0x1c == 0x0b are published (0x6a140/0x6a19c).

The equivalent OpenBRCM order is enforced here at source level. This test is a
regression guard for defects O-1 (early D11+0x3e0 sample) and O-2 (reset block
before the identity is known), and it is also used as a negative control by
test_acphy_arm_mutation.py.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RADIO = (ROOT / "src" / "ob_radio.c").read_text()
LIFECYCLE = (ROOT / "src" / "ob_lifecycle.c").read_text()

PRE_ID_START = "static int ob_radio_vendor_pre_id("
PRE_ID_END = "int ob_radio_vendor_acphy_attach_anacore("
ANACORE_START = "int ob_radio_vendor_acphy_attach_anacore("
RESET_START = "int ob_radio_vendor_acphy_reset("
RESET_END = "int ob_radio_prepare_for_identity"


def block(text, start_token, end_token):
    start = text.index(start_token)
    return text[start:text.index(end_token, start)]


def main():
    pre_id = block(RADIO, PRE_ID_START, PRE_ID_END)
    anacore = block(RADIO, ANACORE_START, RESET_START)
    reset = block(RADIO, RESET_START, RESET_END)
    attach = LIFECYCLE[LIFECYCLE.index("int ob_acphy_attach_identity("):]

    # 1. Stage order inside ob_acphy_attach_identity().
    assert attach.index("phyver = ob_d11_read16(hw, 0x03e0u)") < attach.index(
        "ob_radio_vendor_acphy_attach_anacore("
    ) < attach.index("hw->phy_type =") < attach.index(
        "ob_radio_vendor_acphy_reset("
    ) < attach.index("OB_ATTACH_CP5_IDENTITY_READY"), (
        "attach stage order is not read -> anacore -> publish -> reset"
    )

    # 2. O-1 regression guard: exactly one D11+0x3e0 sample in the whole driver,
    #    and it is the one in ob_acphy_attach_identity().
    assert RADIO.count("ob_d11_read16(hw, 0x03e0u)") == 0
    assert LIFECYCLE.count("ob_d11_read16(hw, 0x03e0u)") == 1

    # 3. O-2 regression guard: the prefix carries no type-dependent work.
    for forbidden in ("ob_acphy_reset_arm(", "ob_d11_write16(",
                      "ob_d11_read16(", "0x04000400", "0x03e0u"):
        assert forbidden not in pre_id, forbidden

    # 4. Vendor intra-block order inside the relocated reset helper.
    order = [
        "0x300, 0x100",
        "0x006, 0x000",
        "0x004, 0x004",
        "OB_RADIO_D11_MACCONTROL",
        "ob_acphy_reset_arm(",
        "udelay(2)",
        "0x00e, 0x000",
        "ob_d11_write16(hw, 0x03e6u",
    ]
    pos = [reset.index(token) for token in order]
    assert pos == sorted(pos), "relocated reset block is not in vendor order"

    # 5. The two distinct vendor 0x3e6 sites are neither merged nor duplicated.
    token = "ob_d11_write16(hw, 0x03e6u, 0x0000u)"
    assert anacore.count(token) == 1, "attach-path anacore write missing"
    assert reset.count(token) == 1, "reset-path anacore write missing"

    print("test_acphy_reset_stage_order: PASS")


if __name__ == "__main__":
    main()
