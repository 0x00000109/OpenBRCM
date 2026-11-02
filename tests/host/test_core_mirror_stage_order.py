#!/usr/bin/env python3
"""Stage-order contract for the relocated S1 core-mirror block.

The vendor trace fixes where the block belongs. In boot 64536612...

  * the prologue reads 0x07xx at t = 531.521658..531.521700,
  * the two mirror passes follow at t = 531.533405..531.533424 and
    531.535567..531.535587, i.e. 11.7 ms later,
  * the LPF register block runs at t = 531.613518..531.613550, 80 ms later,
    with the whole 0x164 table stream in between.

Until 2026-10-28 OpenBRCM issued the mirror inside
ob_phy_acphy_lpf_stage_run(), i.e. after post-ucode and before the LPF block.
That made it mirror the post-ucode generation of the tuple (0x0725 = 0x0600,
0x0408 = 0x0d82) instead of the prologue generation (0x1fff, 0x0d80), and the
LPF prefix's own 0x1725 = 0x0000 write then erased the 0x0600 the device had
produced. Evidence: audit/.../phy_prefix_point01/ORDERING_DELTA.md.

This test is a source-level regression guard: the stage must be called from the
pre-identity prefix, after the 0x408 == 0x5 gate and the PM1 witness, and the
LPF stage must not issue it.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RADIO = (ROOT / "src" / "ob_radio.c").read_text()
LPF = (ROOT / "src" / "ob_phy_lpf.c").read_text()

PRE_ID_START = "static int ob_radio_vendor_pre_id(struct ob_hw *hw)"
PRE_ID_END = "int ob_radio_vendor_acphy_attach_anacore("
LPF_STAGE_START = "int ob_phy_acphy_lpf_stage_run("


def block(text, start_token, end_token):
    start = text.index(start_token)
    return text[start:text.index(end_token, start)]


def main():
    pre_id = block(RADIO, PRE_ID_START, PRE_ID_END)
    lpf_stage = LPF[LPF.index(LPF_STAGE_START):]

    call = "ob_phy_acphy_core_mirror_stage_run(&ob_radio_mirror_ops, hw)"

    # 1. The block runs in the pre-identity prefix, exactly once.
    assert RADIO.count(call) == 1, "mirror stage is not called exactly once"
    assert call in pre_id, "mirror stage is no longer in ob_radio_vendor_pre_id()"

    # 2. It runs after the wrapper gate that proves 0x408 == 0x5 and after the
    #    PM1 witness, so the prologue tuple it copies is the gated one.
    gate = "if (ioctl != 0x5u)"
    pm1 = "ob_phy_tripoint_observe(hw, OB_PHY_TRIPOINT_PM1)"
    assert gate in pre_id, "wrapper 0x408 == 0x5 gate missing"
    assert pm1 in pre_id, "PM1 witness missing"
    assert pre_id.index(gate) < pre_id.index(pm1) < pre_id.index(call), (
        "mirror stage must follow the 0x5 gate and the PM1 witness"
    )

    # 3. It runs before the prefix hands over to the identity path.
    tail = "Everything below the BCMA-owned reset admit"
    assert tail in pre_id, "prefix tail comment missing"
    assert pre_id.index(call) < pre_id.index(tail), (
        "mirror stage must precede the post-gate prefix tail"
    )

    # 4. Fail-stop: the call result is checked, and the failure returns.
    after = pre_id[pre_id.index(call):]
    assert "if (err)" in after, "mirror stage result is not checked"
    assert "OB_ATTACH_FAIL stage=CORE_MIRROR" in after, (
        "mirror failure is not reported with its stage marker"
    )

    # 5. The LPF stage no longer issues the block.
    assert "ob_phy_acphy_core_mirror_run(" not in lpf_stage, (
        "LPF stage still runs the core-mirror block"
    )

    # 6. Pre-identity access shape: the mirror writes use the vendor's single
    #    32-bit latch write, not the two-write pair form.
    assert "ob_phy_write16_atomic_pre_id" in RADIO, (
        "mirror writes do not use the atomic pre-ID form"
    )

    print("test_core_mirror_stage_order: PASS")


if __name__ == "__main__":
    main()
