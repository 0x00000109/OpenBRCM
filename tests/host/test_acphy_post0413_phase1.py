#!/usr/bin/env python3
"""Static isolation and operation-order checks for post-0413 phase 1."""
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
    raise AssertionError(f"unterminated function: {signature}")


def main() -> None:
    radio_h = (ROOT / "src/ob_radio.h").read_text()
    post_h = (ROOT / "src/ob_radio_post0413.h").read_text()
    radio_c = (ROOT / "src/ob_radio.c").read_text()
    core_c = (ROOT / "src/ob_core.c").read_text()
    core_h = (ROOT / "src/ob_core.h").read_text()
    ucode_h = (ROOT / "src/ob_ucode.h").read_text()
    harness = (ROOT / "scripts/runtime-test.sh").read_text()

    assert "OB_RADIO_LOCAL_0413_MAX_ATTEMPTS 100u" in radio_h
    phase0 = body(radio_h, "static inline int ob_radio_local_0413_run(")
    assert "0x0413u" in phase0
    assert "0x0414u" not in phase0 and "0x0415u" not in phase0
    old_owner = body(radio_c, "int ob_radio_local_0413_test(")
    pass_pos = old_owner.index('dev_info(hw->dev, "ACPHY_CLASS0_0413_PASS')
    stop_pos = old_owner.find('dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP',
                            pass_pos)
    assert stop_pos > pass_pos
    assert "ob_radio_post0413_run" not in old_owner

    post_run = body(post_h, "static inline int ob_radio_post0413_run(")
    ordered = [
        "OB_RADIO_POST0413_MARK_CP0_ENTRY",
        "0x0411u, 0x0001u, 0x0000u",
        "0x0414u",
        "0x0415u",
        "delta = (u32)result->r415 - (u32)result->r414",
        "0x0410u, 0x0001u, 0x0000u",
        "0x0410u, 0x1000u, 0x0000u",
        "0x0410u, 0x0018u, 0x0010u",
        "0x0411u, 0xff00u, 0x7000u",
        "0x0412u, 0x0101u",
        "OB_RADIO_POST0413_MARK_CP2_SETUP_DONE",
        "/* r12 is phase 1, not 2",
        "0x0410u, 0x0001u, 0x0000u",
        "ob_radio_post0413_delay(ops, ctx, 1u",
        "0x0410u, 0x0001u, 0x0001u",
        "ob_radio_post0413_delay(ops, ctx, 35u",
        "0x0411u, 0x0001u, 0x0001u",
        "OB_RADIO_POST0413_MARK_CP3_PRE_POLL",
        "OB_RADIO_POST0413_POLL_DELAY_US",
        "ops->read16(ctx, 0x0413u",
    ]
    previous = -1
    positions = []
    for token in ordered:
        position = post_run.find(token, previous + 1)
        assert position >= 0, f"missing/out-of-order operation: {token}"
        positions.append(position)
        previous = position
    assert positions == sorted(positions), "post-0413 operation order changed"
    assert "((old & (u16)~value) | (value & mask))" in post_h
    assert "OB_RADIO_POST0413_MAX_ATTEMPTS 100u" in post_h
    assert "OB_RADIO_POST0413_POLL_DELAY_US 100u" in post_h
    assert "0x0416u" not in post_h and "0x011du" not in post_h
    assert "result->derived = (u8)(scaled >> 8)" in post_h

    old_owner = body(radio_c, "static int ob_radio_class0_post0413_run(")
    assert "ob_radio_local_0413_run(&phase0_ops" in old_owner
    assert old_owner.index("ACPHY_CLASS0_0413_PASS") < old_owner.index(
        "ob_radio_post0413_run")
    phase1_pass = old_owner.index("ACPHY_CLASS0_POST0413_PASS")
    phase1_stop = old_owner.index("ACPHY_CLASS0_POST0413_STOP", phase1_pass)
    phase2_gate = old_owner.index("if (!run_phase2)", phase1_pass)
    phase2_suffix = old_owner.index("ob_radio_postphase1_run", phase1_pass)
    assert phase1_pass < phase2_gate < phase1_stop < phase2_suffix
    assert "return ob_radio_class0_post0413_run(hw, false, false, false)" in radio_c
    assert "ACPHY_CLASS0_POST0413_STOP" in old_owner

    branch_pos = core_c.rindex(
        "if (mode == OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST)")
    mode_branch = body(core_c[branch_pos:],
                       "if (mode == OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST)")
    assert mode_branch.index("ob_acphy_pre_radio_test(hw)") < mode_branch.index(
        "ob_radio_class0_post0413_test(hw)")
    assert "OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST" in ucode_h
    # The selector generation advances with each added mode; the contract is
    # that ob_probe() calls the current selector and that it carries this
    # mode's option.
    assert re.search(r"ob_isolated_mode_select\d+\(", core_c)
    assert re.search(r"ob_isolated_mode_select\d+\([\s\S]*?"
                     r"acphy_class0_post0413_phase1_0413_test_only", ucode_h)
    select20 = body(ucode_h, "ob_isolated_mode_select20(")
    assert "ob_isolated_mode_select19(" in select20
    select19 = body(ucode_h, "ob_isolated_mode_select19(")
    assert "ob_isolated_mode_select18(" in select19
    assert "acphy_class0_post0413_phase1_0413_test_only" in core_h
    preserves = body(ucode_h, "static inline bool ob_isolated_mode_preserves_state(")
    assert "OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST" in preserves
    assert "acphy_class0_post0413_phase1_0413_test_only" in harness
    assert "# preserve openbrcm/radio state; no rmmod" in harness
    print("test_acphy_post0413_phase1: PASS")


if __name__ == "__main__":
    main()
