#!/usr/bin/env python3
"""Offline contract tests for pre-ucode attach runtime observability."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "runtime-test.sh"
LIFECYCLE = (ROOT / "src" / "ob_lifecycle.c").read_text(encoding="utf-8")
RADIO = (ROOT / "src" / "ob_radio.c").read_text(encoding="utf-8")
UCODE = (ROOT / "src" / "ob_ucode.c").read_text(encoding="utf-8")

GOOD = """\
OB_ATTACH_CP0_ENTRY state=1 chip=0x4352 chiprev=3 d11rev=42
OB_ATTACH_CP1_RESET_READY ioctl=0x00000005 reset_ctl=0x00000000 enabled=1
OB_ATTACH_CP2_PHY_PREREQS_READY
OB_ATTACH_CP3_PHY_IDENTITY raw=0x0b01 type=0x0b rev=1
OB_ATTACH_CP4_RADIO_IDENTITY id=0x2069 rev=4 class=0
OB_ATTACH_CP5_IDENTITY_READY state=2
OB_UCODE_CP0_START
normal-bootstrap: POST_UCODE_READY state=7
ACPHY_PRERADIO_HW_CP0_ENTRY
ACPHY_PRERADIO_HW_CP1_IDENTITY_OK
ACPHY_PRERADIO_HW_CP2_CHANSPEC_1001
ACPHY_PRERADIO_HW_CP3_TRANSACTION_DONE
ACPHY_PRERADIO_HW_CP4_SHM_OK
ACPHY_PRERADIO_HW_CP5_CFLAGS_OK
ACPHY_PRERADIO_HW_PASS
"""


def evaluate(text):
    with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as fh:
        fh.write(text)
        path = fh.name
    return subprocess.run(
        ["bash", str(SCRIPT), "--evaluate-log", "acphy_pre_radio_test_only", path],
        cwd=ROOT, capture_output=True, text=True, check=False,
    )


def main():
    markers = (
        "OB_ATTACH_CP0_ENTRY",
        "OB_ATTACH_CP1_RESET_READY",
        "OB_ATTACH_CP2_PHY_PREREQS_READY",
        "OB_ATTACH_CP3_PHY_IDENTITY",
        "OB_ATTACH_CP4_RADIO_IDENTITY",
        "OB_ATTACH_CP5_IDENTITY_READY",
        "OB_UCODE_CP0_START",
    )
    combined = LIFECYCLE + RADIO + UCODE
    for marker in markers:
        assert marker in combined, marker

    attach_start = LIFECYCLE.index("int ob_acphy_attach_identity(")
    attach = LIFECYCLE[attach_start:]
    assert attach.index("OB_ATTACH_CP0_ENTRY") < attach.index(
        "ob_radio_prepare_for_identity(hw)"
    ) < attach.index("OB_ATTACH_CP2_PHY_PREREQS_READY")
    assert attach.index("OB_ATTACH_CP2_PHY_PREREQS_READY") < attach.index(
        "OB_ATTACH_CP3_PHY_IDENTITY"
    ) < attach.index("OB_ATTACH_CP4_RADIO_IDENTITY") < attach.index(
        "OB_ATTACH_CP5_IDENTITY_READY"
    )
    pre_id_start = RADIO.index("static int ob_radio_vendor_pre_id(")
    pre_id = RADIO[pre_id_start:RADIO.index(
        "int ob_radio_prepare_for_identity", pre_id_start
    )]
    assert pre_id.index("ob_attach_core_reset_postcondition") < pre_id.index(
        "OB_ATTACH_CP1_RESET_READY"
    )
    # Phase 38C moved the register sequence into the pure contract, so the
    # claim is now: the CP0 start marker is emitted by the adapter before the
    # contract that performs the first MACCONTROL update is entered.
    # Amendment 01: the adapter body lives in ob_ucode_d2a_exec(); the public
    # ob_ucode_run_d2a() is a one-line wrapper over it.
    run = UCODE[UCODE.index("static int ob_ucode_d2a_exec("):]
    assert run.index("OB_UCODE_CP0_START") < run.index("ob_d2a_run(")

    result = evaluate(GOOD)
    assert result.returncode == 0, (result.stdout, result.stderr)
    out = result.stdout
    assert "ATTACH_RESET_REACHED: YES" in out
    assert "PHYVERSION_VALUE: 0x0b01" in out
    assert "PHY_TYPE: 0x0b" in out
    assert "PHY_REV: 1" in out
    assert "RADIO_ID: 0x2069" in out
    assert "RADIO_REV: 4" in out
    assert "UCODE_STARTED_AFTER_ATTACH_IDENTITY: YES" in out
    assert "RUNTIME_ORDER_OBSERVABILITY: COMPLETE" in out

    no_phy = GOOD.replace(
        "OB_ATTACH_CP3_PHY_IDENTITY raw=0x0b01 type=0x0b rev=1\n", ""
    ).replace("OB_ATTACH_CP5_IDENTITY_READY state=2\n", "")
    out = evaluate(no_phy).stdout
    assert "ATTACH_IDENTITY_READY_REACHED: NO" in out
    assert "RUNTIME_ORDER_OBSERVABILITY: INSUFFICIENT" in out

    no_radio = GOOD.replace(
        "OB_ATTACH_CP4_RADIO_IDENTITY id=0x2069 rev=4 class=0\n", ""
    ).replace("OB_ATTACH_CP5_IDENTITY_READY state=2\n", "")
    out = evaluate(no_radio).stdout
    assert "ATTACH_IDENTITY_READY_REACHED: NO" in out
    assert "RUNTIME_ORDER_OBSERVABILITY: INSUFFICIENT" in out

    bad_order = GOOD.replace(
        "OB_ATTACH_CP5_IDENTITY_READY state=2\nOB_UCODE_CP0_START",
        "OB_UCODE_CP0_START\nOB_ATTACH_CP5_IDENTITY_READY state=2",
    )
    out = evaluate(bad_order).stdout
    assert "UCODE_STARTED_AFTER_ATTACH_IDENTITY: NOT_OBSERVABLE" in out
    assert "RUNTIME_ORDER_OBSERVABILITY: INSUFFICIENT" in out

    # Logging must not duplicate the authoritative identity read or ucode
    # upload entry point; these counts are source-level operation guards.
    assert LIFECYCLE.count("ob_d11_read16(hw, 0x03e0u)") == 1
    assert UCODE.count("OB_UCODE_CP0_START") == 1
    print("test_attach_runtime_markers: PASS")


if __name__ == "__main__":
    main()
