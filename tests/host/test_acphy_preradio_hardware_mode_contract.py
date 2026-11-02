#!/usr/bin/env python3
"""Static containment checks for the one-shot ACPHY pre-radio mode."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : end]
    raise AssertionError(f"unterminated function: {signature}")


def main() -> None:
    core = (ROOT / "src/ob_core.c").read_text()
    phy = (ROOT / "src/ob_phy.c").read_text()
    radio = (ROOT / "src/ob_radio.c").read_text()
    ucode_h = (ROOT / "src/ob_ucode.h").read_text()

    assert "OB_ISOLATED_ACPHY_PRE_RADIO_TEST" in ucode_h
    assert "ob_isolated_mode_select15" in ucode_h
    assert "acphy_pre_radio_test_only" in core
    assert "module_param(acphy_pre_radio_test_only, bool, 0444)" in core
    assert "OB_ISOLATED_ACPHY_PRE_RADIO_TEST" in core

    class0 = function_body(core, "if (mode == OB_ISOLATED_ACPHY_CLASS0_0413_TEST)")
    assert "ob_acphy_pre_radio_test(hw)" in class0
    assert class0.index("ob_acphy_pre_radio_test(hw)") < class0.index(
        "ob_radio_local_0413_test(hw)"
    ), "class-0 checkpoint must consume the READY-establishing owner first"
    pre_call = class0.index("ret = ob_acphy_pre_radio_test(hw);")
    checkpoint_call = class0.index("ret = ob_radio_local_0413_test(hw);")
    assert class0.index("if (ret) {", pre_call) < checkpoint_call
    assert class0.index("bcma_set_drvdata(core, NULL);", pre_call) < checkpoint_call
    assert "acphy_start_state = OB_ACPHY_START_READY_FOR_RADIO_ON" not in class0
    assert "OB_ACPHY_START_IN_PROGRESS" not in class0
    assert class0.count("ob_radio_local_0413_test(hw)") == 1

    # The checkpoint entry gate rejects every pre-READY lifecycle state and
    # therefore cannot be reached directly from post-ucode or identity-ready.
    checkpoint = function_body(radio, "int ob_radio_local_0413_test(")
    assert "hw->d11_state != OB_D11_POST_UCODE_READY" in checkpoint
    assert "hw->acphy_start_state != OB_ACPHY_START_READY_FOR_RADIO_ON" in checkpoint
    assert "ACPHY_CLASS0_0413_FAIL stage=ENTRY_STATE" in checkpoint
    assert "ob_acphy_first_up_finish" not in checkpoint

    test = function_body(phy, "int ob_acphy_pre_radio_test(")
    ordered = [
        "ACPHY_PRERADIO_HW_CP0_ENTRY",
        "ACPHY_PRERADIO_HW_CP1_IDENTITY_OK",
        "ob_test_chanspec_fixture",
        "hw->chanspec = chanspec",
        "hw->chanspec_valid = true",
        "ACPHY_PRERADIO_HW_CP2_CHANSPEC_1001",
        "ob_acphy_first_up_begin",
        "ob_acphy_pre_radio_setup",
        "ACPHY_PRERADIO_HW_CP3_TRANSACTION_DONE",
        "ob_ucode_read_shm16(hw, 0x00a0)",
        "ACPHY_PRERADIO_HW_CP4_SHM_OK",
        "ob_axi_read32(hw, 0x0408)",
        "ACPHY_PRERADIO_HW_CP5_CFLAGS_OK",
        "ob_acphy_first_up_finish(&hw->acphy_start_state, true)",
        "ACPHY_PRERADIO_HW_PASS",
    ]
    positions = [test.index(token) for token in ordered]
    assert positions == sorted(positions), "checkpoint ordering changed"
    assert test.index("ob_acphy_first_up_finish(&hw->acphy_start_state, true)") > test.index(
        "ACPHY_PRERADIO_HW_CP5_CFLAGS_OK"
    ), "READY must publish after SHM and wrapper-cflags postconditions"
    assert "ACPHY_PRERADIO_HW_FAIL stage=%s ret=%d" in phy
    for required in (
        "hw->chip_id != 0x4352", "hw->chip_rev != 3",
        "hw->core->id.rev != 42", "hw->phy_type != 0x0b",
        "hw->phy_revision != 1", "hw->radio_id != 0x2069",
        "hw->radio_revision != 4", "!hw->phy_radio_identity_valid",
        "!hw->radio_access_initialized",
        "hw->d11_state != OB_D11_POST_UCODE_READY",
    ):
        assert required in test, f"missing target gate: {required}"
    for forbidden in ("ob_radio_", "pll", "calibr", "ob_d11_enable_mac",
                      "ob_dma_", "ob_irq_", "ob_rx_", "ob_mac80211_"):
        assert forbidden not in test.lower(), f"forbidden path: {forbidden}"

    print("test_acphy_preradio_hardware_mode_contract: PASS")


if __name__ == "__main__":
    main()
