#!/usr/bin/env python3
"""Source-level lifecycle assertions for the first-up AC-PHY .start path."""

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
    mac = (ROOT / "src/ob_mac80211.c").read_text()
    irq = (ROOT / "src/ob_irq.c").read_text()
    core = (ROOT / "src/ob_core.c").read_text()
    phy_src = (ROOT / "src/ob_phy.c").read_text()
    phy_hdr = (ROOT / "src/ob_phy.h").read_text()
    radio_src = (ROOT / "src/ob_radio.c").read_text()
    ucode_src = (ROOT / "src/ob_ucode.c").read_text()

    start = function_body(mac, "static int ob_ops_start(")
    chandef_to_chanspec = function_body(mac, "static int ob_start_chanspec(")
    target_ready = function_body(mac, "static bool ob_start_target_ready(")
    handler = function_body(irq, "static irqreturn_t ob_irq_handler(")
    irq_init = function_body(irq, "int ob_irq_init(")
    probe = function_body(core, "int ob_probe(")
    pre_radio = function_body(phy_src, "int ob_acphy_pre_radio_setup(")
    checked_shm = function_body(ucode_src, "int ob_ucode_write_shm16_checked(")

    # The service flag is checked before every first D11 touch in the ISR.
    gate = handler.index("ob_d11_irq_service_allowed(READ_ONCE(hw->irq.active))")
    first_d11 = handler.index("ob_d11_read32(")
    assert gate < first_d11, "ISR can access D11 before the service gate"
    assert "ob_rx_irq(hw)" in handler
    assert handler.index("ob_d11_irq_service_allowed") < handler.index("ob_rx_irq(hw)")

    # Registration records the IRQ but leaves service disabled.
    request = irq_init.index("request_irq(")
    installed = irq_init.index("hw->irq.installed = true")
    inactive = irq_init.index("hw->irq.active = false")
    assert inactive < request < installed
    assert "hw->irq.active = true" not in irq_init

    # Probe must not arm RX before a lifecycle owner can prove chained-buffer
    # handling, status validity, and DMA quiescence on failure.
    assert "ob_rx_init(hw)" not in probe
    assert probe.index("ob_irq_init(hw)") < probe.index("ob_mac80211_register(hw)")
    assert "hw->irq.active = true" not in probe

    # Checked SHM reuses the existing select/readback/data sequence and turns
    # an existing monotonic device-loss indication into an error.
    shm_order = [
        "ob_d11_write32(hw, OB_UCODE_REG_OBJADDR",
        "ob_d11_read32_trusted(hw, \"acphy_pre_radio_shm_objaddr\"",
        "ob_d11_write16(hw, OB_UCODE_REG_OBJDATA",
    ]
    shm_positions = [checked_shm.index(token) for token in shm_order]
    assert shm_positions == sorted(shm_positions)
    assert "return hw->dev_lost ? -19 : 0" in checked_shm

    # .start validates one chandef, then runs the pre-radio prefix only when the
    # explicit incomplete bring-up option is set; the production default still
    # stops at the gate before any hardware access.
    ordered = [
        "ob_acphy_first_up_begin",
        "ob_phy_init_begin(&ob->phy_init)",
        "ob_start_chanspec(hw, &encoded)",
        "ob_start_target_ready(ob)",
        "ob_acphy_init_production_gate(ob)",
        "ob->chanspec = encoded",
        "ob->chanspec_valid = true",
        "ob_acphy_pre_radio_setup(ob)",
        "ob_phy_init_advance(&ob->phy_init",
        "OB_PHY_INIT_PRE_RADIO_READY",
        "ob_acphy_first_up_finish(&ob->acphy_start_state, true)",
        "ob_acphy_radio_initialize(ob)",
        "BRINGUP_TEST_RADIO_STOP",
        "ob_ops_apply_chan_tuning(ob, encoded",
        "ob->base_ready = true",
        "ob_acphy_calibration_initialize(ob)",
        "ob_acphy_channel_initialize(ob)",
        "ob_phy_init_is_operational(&ob->phy_init)",
        "fail:",
    ]
    positions = [start.index(token) for token in ordered]
    assert positions == sorted(positions), "first-up start order changed"
    validation_order = [
        "!chandef->chan",
        "cfg80211_chandef_valid(chandef)",
        "cfg80211_chandef_usable(ieee->wiphy, chandef, 0)",
        "pub->hw->bringup_test_only",
        "NL80211_CHAN_WIDTH_20_NOHT",
        "NL80211_CHAN_WIDTH_40",
        "NL80211_CHAN_WIDTH_80",
        "NL80211_BAND_2GHZ",
        "chandef->width != NL80211_CHAN_WIDTH_20",
        "center_freq1 != chan->center_freq",
        "ob_acphy_encode_2g_20mhz",
    ]
    validation_positions = [chandef_to_chanspec.index(token)
                            for token in validation_order]
    assert validation_positions == sorted(validation_positions)
    assert "OB_TEST_CHANSPEC_BCM4352_CH1_20MHZ" not in start
    assert "OB_TEST_CHANSPEC_BCM4352_CH1_20MHZ" not in chandef_to_chanspec
    assert "ob->irq.active = true" not in start
    gate_index = start.index("ob_acphy_init_production_gate(ob)")
    first_hardware_stage = start.index("ob_acphy_pre_radio_setup(ob)")
    assert gate_index < first_hardware_stage
    assert "if (ret)" in start[gate_index:first_hardware_stage]
    for forbidden in ("ob_acphy_pre_radio_setup(", "ob_acphy_radio_initialize(",
                      "ob_ucode_", "ob_axi_", "ob_d11_", "ob_radio_",
                      "ob_rx_init(", "ob_tx"):
        assert forbidden not in start[:gate_index], (
            f".start reached hardware before production gate: {forbidden}"
        )
    assert "ob->irq.active = false" in start
    assert start.count("ob_acphy_init_production_gate(ob)") == 1
    assert "ob_acphy_first_up_fail(&ob->acphy_start_state)" in start
    assert "ob_phy_init_fail(&ob->phy_init, ret)" in start
    gate = function_body(phy_src, "int ob_acphy_init_production_gate(")
    assert "ob_phy_acphy_lpf_input_status" in gate
    assert "OB_BOARDFLAGS3_SOURCE_VALIDATED_REV11" in gate
    assert "ob_acphy_lpf_blocker(input_status)" in gate
    assert "return -EOPNOTSUPP" in gate
    assert "hw->bringup_test_only" in gate
    assert gate.index("hw->bringup_test_only") < gate.index("ob_phy_init_block")
    for forbidden in ("ob_acphy_pre_radio_setup(", "ob_radio_", "ob_ucode_",
                      "ob_axi_", "ob_d11_"):
        assert forbidden not in gate
    radio_init = function_body(phy_src, "static int ob_acphy_radio_prefix_initialize(")
    radio_complete = function_body(phy_src, "int ob_acphy_radio_initialize(")
    assert "analog_tx_lpf" in radio_init
    assert radio_complete.index("hw->phy_revision != 1") < radio_complete.index(
        "ob_acphy_radio_prefix_initialize(hw)")
    assert radio_complete.index("hw->chanspec != 0x1001") < radio_complete.index(
        "ob_acphy_radio_prefix_initialize(hw)")
    assert radio_complete.index("ob_acphy_radio_prefix_initialize(hw)") < radio_complete.index(
        "ob_acphy_post_lpf_radio_complete(hw, false)")
    assert "OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD" in phy_hdr
    assert "ob_phy_init_begin_radio(&hw->phy_init)" in radio_init
    assert "ob_phy_acphy_lpf_input_status" in radio_init
    assert radio_init.index("ob_phy_acphy_lpf_input_status") < radio_init.index(
        "ob_phy_init_begin_radio(&hw->phy_init)")
    assert radio_init.index("ob_phy_init_begin_radio") < radio_init.index(
        "ob_radio_class0_initialize_to_sub9591e")
    assert radio_init.index("ob_radio_class0_initialize_to_sub9591e") < radio_init.index(
        "ob_phy_init_radio_prefix_complete")
    assert radio_init.index("ob_phy_init_radio_prefix_complete") < radio_init.index(
        "ob_acphy_analog_tx_lpf_initialize")
    lpf_call = radio_init.index("ob_acphy_analog_tx_lpf_initialize")
    post_lpf_lookup = radio_init.index("ob_radio2069_rev4_lookup_2g20", lpf_call)
    assert post_lpf_lookup < radio_init.index("ob_phy_sub92500_run")
    assert radio_init.index("ob_phy_sub92500_run") < radio_init.index(
        "OB_PHY_BLOCK_REV1_AFTER_SUB92500")
    assert "OB_RADIO2069_CHANSPEC_CH1_20MHZ" not in radio_init
    for forbidden in ("ob_phy_read16(", "ob_phy_write16(", "ob_radio_read16(",
                      "ob_radio_write16(", "ob_ucode_", "ob_axi_",
                      "ob_d11_write", "ob_d11_read",
                      "bcma_core_is_enabled("):
        assert forbidden not in radio_init, (
            f"radio lifecycle gate performed hardware access: {forbidden}"
        )
    calibration = function_body(phy_src, "int ob_acphy_calibration_initialize(")
    channel_init = function_body(phy_src, "int ob_acphy_channel_initialize(")
    assert "OB_PHY_BLOCK_CALIBRATION_INPUTS" in calibration
    assert "OB_PHY_BLOCK_CHANNEL_LOOKUP" in channel_init
    for stage in (calibration, channel_init):
        for forbidden in ("ob_phy_read16(", "ob_phy_write16(",
                          "ob_radio_read16(", "ob_radio_write16(",
                          "ob_ucode_", "ob_axi_", "ob_d11_"):
            assert forbidden not in stage
    add_interface = function_body(mac, "static int ob_ops_add_interface(")
    assert "NL80211_IFTYPE_MONITOR" in add_interface
    assert "bringup_test_only" in add_interface
    register = function_body(mac, "int ob_mac80211_register(struct ob_hw *ob)")
    assert "bringup_test_only" in register
    assert "BIT(NL80211_IFTYPE_MONITOR)" in register
    apply = function_body(mac, "static int ob_ops_apply_chan_tuning(struct ob_hw *ob, u16 chanspec, u16 freq)\n{")
    assert "phy_writes" in apply
    assert "BCM43XX_CHAN_TUNING_BW_COUNT" in apply
    assert "CHAN_TUNING_APPLIED" in apply
    config = function_body(mac, "static int ob_ops_config(")
    assert "ob_phy_init_is_operational(&ob->phy_init) && !ob->base_ready" in config
    assert "operational/base-ready" in config
    assert "ob_ops_apply_chan_tuning(ob, chanspec" in config
    assert "hw->acphy_start_state != OB_ACPHY_START_IN_PROGRESS" in pre_radio
    assert "hw->irq.active" in pre_radio
    assert "ob_d11_write16(hw, off, value)" in phy_src

    # Kernel BCMA exposes the D11 core revision through id.rev, not a
    # nonexistent top-level bcma_device::rev field.
    assert "ob->core->id.rev == 42" in target_ready
    for forbidden in ("ob_radio_", "calibr", "pll", "ob_d11_enable_mac"):
        assert forbidden not in pre_radio.lower(), (
            f"pre-radio helper entered forbidden phase: {forbidden}"
        )

    # Generic phase advancement cannot substitute for stage completion.
    advance = function_body(phy_hdr, "static inline int ob_phy_init_advance(")
    for token in ("radio_prefix_complete", "analog_tx_lpf_complete", "radio_sequence_complete",
                  "calibration_complete", "channel_programmed"):
        assert token in advance

    # The shared radio helper rejects wrong identity before any SHM/AXI/BCMA
    # state reads. In particular bcma_core_is_enabled() is MMIO-backed.
    radio_run = function_body(radio_src, "static int ob_radio_class0_post0413_run(")
    identity_gate = radio_run.index("ob_radio_local_0413_identity_ok(&target)")
    first_postcondition_read = min(
        radio_run.index("ob_ucode_read_shm16(hw"),
        radio_run.index("ob_axi_read32(hw"),
        radio_run.index("bcma_core_is_enabled(hw->core)"),
    )
    assert identity_gate < first_postcondition_read

    print("test_acphy_start_contract: PASS")


if __name__ == "__main__":
    main()
