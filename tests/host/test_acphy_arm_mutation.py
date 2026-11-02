#!/usr/bin/env python3
"""Negative controls: each mutation of the corrected source must fail the tests.

The real repository is READ-ONLY input; every mutation is applied to a
disposable copy under a TemporaryDirectory.

Phase M-ACPHY-RESET-STAGE-IMPLEMENT-02 moved the type-dependent block from
src/ob_radio.c into ob_radio_vendor_acphy_reset() and added two calls in
src/ob_lifecycle.c, so the sandbox now also carries ob_lifecycle.c and the
stage-order contract test, and one mutation reverses the new call order.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path("/media/kartashoff/Storage/opensource/driver/OpenBRCM")
RADIO_H = REPO / "src/ob_radio.h"
RADIO_C = REPO / "src/ob_radio.c"
LIFECYCLE_C = REPO / "src/ob_lifecycle.c"

MUTATIONS = [
    (
        "D1 constant 0x0e instead of bw|0x0e",
        "ob_acphy_reset_arm(phy_type, OB_ACPHY_BW_STATE_ATTACH",
        "ob_radio_vendor_rmw_wrapper(hw, 0xce, 0x0e); if (0) "
        "ob_acphy_reset_arm(phy_type, OB_ACPHY_BW_STATE_ATTACH",
    ),
    (
        "D2 extra 0xcc/0x0c transaction re-added",
        "udelay(2);",
        "ob_radio_vendor_rmw_wrapper(hw, 0xcc, 0x0c);\n\tudelay(2);",
    ),
    (
        "D3 no PHY-type gate (hardcoded AC arm)",
        "ob_acphy_reset_arm(phy_type, OB_ACPHY_BW_STATE_ATTACH",
        "ob_acphy_reset_arm(0x0b, OB_ACPHY_BW_STATE_ATTACH",
    ),
    (
        "D5 anacore via the 0x3fc/0x3fe PHY window",
        "ob_d11_write16(hw, 0x03e6u, 0x0000u);",
        "ob_phy_write16_pre_id(hw, 0x03e6, 0);",
    ),
    (
        "type not sampled (constant phy_type)",
        "phyver = ob_d11_read16(hw, 0x03e0u);",
        "phyver = 0x0b2au;",
    ),
    (
        "bandwidth bits dropped from the AC base table",
        "return 0x0040u;",
        "return 0x0000u;",
    ),
    (
        "reset issued before the identity read (stage reversal)",
        "\tphyver = ob_d11_read16(hw, 0x03e0u);",
        "\tret = ob_radio_vendor_acphy_reset(hw, OB_RADIO_PHYTYPE_AC);\n"
        "\tif (ret)\n"
        "\t\treturn ret;\n"
        "\tphyver = ob_d11_read16(hw, 0x03e0u);",
    ),
    (
        "attach-path anacore call deleted",
        "\tret = ob_radio_vendor_acphy_attach_anacore(hw,\n"
        "\t\t\t\t\t\t   (u8)((phyver >> 8) & 0x0fu));\n"
        "\tif (ret)\n\t\treturn ret;\n",
        "\t/* attach-path anacore write deleted */\n",
    ),
]

CONTRACTS = ("tests/host/test_pre_id_contract.py",
             "tests/host/test_acphy_reset_stage_order.py")


def apply_mutation(tree, old, new):
    """Apply one mutation to a disposable copy; return the mutated file or None."""
    for rel in ("src/ob_radio.c", "src/ob_radio.h", "src/ob_lifecycle.c"):
        source = (tree / rel).read_text()
        if old in source:
            (tree / rel).write_text(source.replace(old, new, 1))
            return rel
    return None


def run(mutation_name, old, new, tmp):
    tree = Path(tmp) / "tree"
    (tree / "src").mkdir(parents=True)
    (tree / "tests/host/linux").mkdir(parents=True)
    shutil.copy(RADIO_C, tree / "src/ob_radio.c")
    shutil.copy(RADIO_H, tree / "src/ob_radio.h")
    shutil.copy(LIFECYCLE_C, tree / "src/ob_lifecycle.c")
    mutated = apply_mutation(tree, old, new)
    if mutated is None:
        return f"{mutation_name}: MUTATION TARGET MISSING"
    shutil.copy(REPO / "src/ob_guard.h", tree / "src/ob_guard.h")
    shutil.copy(REPO / "tests/host/linux/types.h", tree / "tests/host/linux/types.h")
    for contract in CONTRACTS:
        shutil.copy(REPO / contract, tree / contract)
    shutil.copy(REPO / "tests/host/ob_acphy_attach_arm_test.c",
                tree / "tests/host/ob_acphy_attach_arm_test.c")
    contract_rcs = []
    for contract in CONTRACTS:
        proc = subprocess.run([sys.executable, contract], cwd=tree,
                              capture_output=True, text=True)
        contract_rcs.append(proc.returncode)
    cc = subprocess.run(
        ["cc", "-O2", "-Wall", "-Wextra", "-Itests/host", "-Isrc",
         "-o", "arm_test", "tests/host/ob_acphy_attach_arm_test.c"],
        cwd=tree, capture_output=True, text=True)
    arm = None
    if cc.returncode == 0:
        arm = subprocess.run(["./arm_test"], cwd=tree, capture_output=True,
                             text=True)
    c_rc = cc.returncode if arm is None else arm.returncode
    failed = any(contract_rcs) or c_rc
    status = "REJECTED" if failed else "ACCEPTED (BAD)"
    return (f"{mutation_name}: {status} file={mutated} "
            f"contract_rc={contract_rcs} behaviour_rc={c_rc}")


def main():
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for i, (name, old, new) in enumerate(MUTATIONS):
            out = run(name, old, new, Path(tmp) / str(i))
            print(out)
            if "REJECTED" not in out:
                failures += 1
    if failures:
        print(f"negative control: {failures} mutation(s) not detected")
        return 1
    print(f"negative control: all {len(MUTATIONS)} mutations rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
