#!/usr/bin/env python3
"""Static isolation and operation-order checks for the phase-2 checkpoint."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:end]
    raise AssertionError(f"unterminated body: {signature}")


def ordered(source: str, tokens: list[str]) -> list[int]:
    positions = []
    previous = -1
    for token in tokens:
        position = source.find(token, previous + 1)
        assert position >= 0, f"missing/out-of-order token: {token}"
        positions.append(position)
        previous = position
    return positions


def main() -> None:
    core = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
    core_h = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
    radio = (ROOT / "src/ob_radio.c").read_text(encoding="utf-8")
    radio_h = (ROOT / "src/ob_radio.h").read_text(encoding="utf-8")
    seq = (ROOT / "src/ob_radio_postphase1.h").read_text(encoding="utf-8")
    modes = (ROOT / "src/ob_ucode.h").read_text(encoding="utf-8")
    harness = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")
    phase0_test = (ROOT / "tests/host/test_acphy_class0_radio_local_0413.py").read_text(encoding="utf-8")
    phase1_test = (ROOT / "tests/host/test_acphy_post0413_phase1.py").read_text(encoding="utf-8")

    mode = "acphy_class0_postphase1_phase2_0413_test_only"
    assert f"module_param({mode}, bool, 0444)" in core
    assert f"bool\t\t\t{mode}" in core_h
    assert "OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST" in modes
    # The selector generation advances with each added mode; the contract is
    # that ob_probe() calls the current selector and that it carries this
    # mode's option.
    assert re.search(r"ob_isolated_mode_select\d+\(", core)
    assert re.search(r"ob_isolated_mode_select\d+\([\s\S]*?"
                     r"acphy_class0_postphase1_phase2_0413_test_only", modes)
    select20 = body(modes, "ob_isolated_mode_select20(")
    assert "ob_isolated_mode_select19(" in select20
    select18 = body(modes, "ob_isolated_mode_select18(")
    assert "ob_isolated_mode_select17(" in select18
    assert "if (acphy_class0_postphase1_phase2_0413_test_only &&" in select18
    assert "return acphy_class0_postphase1_phase2_0413_test_only ?" in select18
    assert "OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST : m;" in select18
    select19 = body(modes, "ob_isolated_mode_select19(")
    assert "ob_isolated_mode_select18(" in select19
    preserve = body(modes, "static inline bool ob_isolated_mode_preserves_state(")
    assert "OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST" in preserve

    # The new return mode shares the pre-radio prefix, then selects its
    # separate owner; the established phase-1 mode retains its old owner.
    branch_at = core.rindex(
        "if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST ||")
    branch = body(core[branch_at:],
                  "if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST ||")
    assert branch.index("ob_acphy_pre_radio_test(hw)") < branch.index(
        "ob_radio_class0_postphase2_return_test(hw)")
    assert "if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST)" in branch
    assert "else if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST)" in branch
    assert "ob_radio_class0_postbmac_branch_gate_test(hw)" in branch
    assert "ob_radio_class0_postphase1_test(hw)" in branch
    assert "return ob_radio_class0_post0413_run(hw, false, false, false)" in radio
    assert "return ob_radio_class0_post0413_run(hw, true, false, false)" in radio
    assert "return ob_radio_class0_post0413_run(hw, true, true, false)" in radio[
        radio.index("int ob_radio_class0_initialize_to_sub9591e("):]
    assert "return ob_radio_class0_initialize_to_sub9591e(hw)" in radio
    owner = body(radio, "static int ob_radio_class0_post0413_run(")
    p0 = owner.index("ob_radio_local_0413_run(&phase0_ops")
    p1 = owner.index("ob_radio_post0413_run(&phase1_ops")
    p2_gate = owner.index("if (!run_phase2)", p1)
    suffix = owner.index("ob_radio_postphase1_run(&phase2_ops", p2_gate)
    assert p0 < p1 < p2_gate < suffix
    assert owner.index('"ACPHY_CLASS0_POST0413_STOP\\n"', p2_gate) < suffix
    assert "acphy_class0_post0413_phase1_0413_test_only" in phase1_test
    assert "ob_radio_postphase1_run" not in phase0_test
    assert "if (mode == OB_ISOLATED_ACPHY_CLASS0_0413_TEST)" in core

    # The pure sequence is enabled only after same-run phase-1 PASS. It does
    # not accept or consume the earlier derived phase-0 byte.
    assert "ob_radio_postphase1_run" not in radio_h
    assert "ob_radio_postphase1_run" in radio
    run = body(seq, "static inline int ob_radio_postphase1_run(")
    assert "bool phase1_success" in seq
    assert "if (!phase1_success)" in run
    assert "derived" not in seq and "r414" not in seq and "r415" not in seq
    assert run.count("0x0416u") == 1

    required_order = [
        "OB_RADIO_POSTPHASE1_MARK_CP0_ENTRY",
        "0x0411u, 0x0001u, 0x0000u",
        "0x0416u",
        "result->sample_low5 = (u16)(result->sample_0416 & 0x001fu)",
        "phase1_regs[0] | (u16)(i << 9)",
        "phase1_regs[1] | (u16)(i << 9)",
        "OB_RADIO_POSTPHASE1_MARK_CP2_PHASE1_CHAIN_APPLY_DONE",
        "0x0410u, 0x0001u, 0x0000u",
        "0x0410u, 0x1000u, 0x0000u",
        "0x0410u, 0x0018u, 0x0008u",
        "0x0411u, 0xff00u, 0x4000u",
        "0x0412u, 0x011au",
        "OB_RADIO_POSTPHASE1_MARK_CP3_PHASE2_SETUP_DONE",
        "phase2_clear_regs[i]",
        "phase2_set_regs[i]",
        "OB_RADIO_POSTPHASE1_MARK_CP4_PHASE2_CHAIN_SETUP_DONE",
        "0x0410u, 0x0001u, 0x0000u",
        "ob_radio_postphase1_delay(ops, ctx, 1u",
        "0x0410u, 0x0001u, 0x0001u",
        "ob_radio_postphase1_delay(ops, ctx, 35u",
        "0x0411u, 0x0001u, 0x0001u",
        "OB_RADIO_POSTPHASE1_MARK_CP5_PRE_POLL",
        "OB_RADIO_POSTPHASE1_POLL_DELAY_US",
        "0x0413u",
        "OB_RADIO_POSTPHASE1_MARK_POLL",
    ]
    assert ordered(run, required_order) == sorted(ordered(run, required_order))

    assert "(old & (u16)~value) | (value & mask)" in seq
    assert "phase1_regs[] = { 0x0126u, 0x0043u }" in seq
    assert "phase2_clear_regs[] = { 0x011du, 0x031du }" in seq
    assert "phase2_set_regs[] = { 0x0171u, 0x0371u }" in seq
    assert "i < 4u" not in run and "i < 2u" in run
    assert "OB_RADIO_POSTPHASE1_MAX_ATTEMPTS 100u" in seq
    assert "OB_RADIO_POSTPHASE1_POLL_DELAY_US 100u" in seq
    assert "if (value & OB_RADIO_POSTPHASE1_POLL_MASK)" in run
    poll = run[run.index("result->poll_started = true;"):]
    assert "for (i = 0; i < OB_RADIO_POSTPHASE1_MAX_ATTEMPTS; i++)" in poll
    assert poll.index("ob_radio_postphase1_delay") < poll.index(
        "ob_radio_postphase1_read")
    assert "0x0416u" not in poll
    assert "return 0;" in poll and "return -110;" in poll

    # Every indirect MMIO helper checks the monotonic loss latch before and
    # after access. The sequence has no post-poll continuation.
    for helper in ("ob_radio_postphase1_read(", "ob_radio_postphase1_write(",
                   "ob_radio_postphase1_delay("):
        helper_body = body(seq, f"static inline int {helper}")
        assert helper_body.count("ops->device_lost(ctx)") >= 2
    assert "ACPHY_CLASS0_POSTPHASE1_STOP" in radio
    assert "ACPHY_CLASS0_POSTPHASE1_PASS" in radio
    assert "0x0416" not in poll
    assert mode in harness
    assert harness.count(f'"{mode}"') >= 2
    assert "preserve openbrcm/radio state; no rmmod" in harness
    assert '"ACPHY_CLASS0_POSTPHASE1_STOP"' in harness

    print("test_acphy_postphase1_phase2: PASS")


if __name__ == "__main__":
    main()
