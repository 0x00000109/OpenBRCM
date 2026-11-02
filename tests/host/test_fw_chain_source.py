#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Static audit for the W2 firmware-chain ownership boundary.

This is a source-level guard, not a proof of runtime behaviour.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"

chain = (SRC / "ob_fw_chain.c").read_text()
contract = (SRC / "ob_fw_chain_contract.h").read_text()
core = (SRC / "ob_core.c").read_text()
lifecycle = (SRC / "ob_lifecycle.c").read_text()
initvals = (SRC / "ob_initvals.c").read_text()
ucode = (SRC / "ob_ucode.h").read_text()

failures = []

def require(condition, message):
    if not condition:
        failures.append(message)

# The chain adapter must use the shared cores and the pure contract.
require("ob_fw_chain_core(" in chain, "chain adapter does not call the pure core")
require("ob_ucode_run_d2a(" in chain, "chain adapter does not use the D2A core")
require("ob_initvals_apply_common_ex(" in chain,
        "chain adapter does not use the common-initvals core")
require("ob_fw_chain_identity_gate(" in chain,
        "chain adapter has no identity gate")
require("request_firmware" not in chain,
        "chain adapter performs its own firmware request")
require("ob_d11_post_ucode_init" not in chain,
        "chain adapter must not call the normal post-ucode owner")

# No PHY/radio/DMA/IRQ/band-init reachability from the chain object.
for token in ("ob_d3a1_", "ob_d3b_", "ob_phy_", "ob_radio_", "ob_dma_",
              "ob_irq_", "request_irq", "ieee80211_", "sub_6656c"):
    require(token not in chain, f"forbidden chain reference: {token}")

# The normal owner delegates instead of duplicating D2A/common.
require("ob_fw_chain_run(" in lifecycle,
        "ob_d11_post_ucode_init does not delegate to the chain")
require("ob_ucode_run_d2a(" not in lifecycle,
        "normal lifecycle still performs its own D2A")
require("ob_initvals_apply_common(" not in lifecycle,
        "normal lifecycle still applies common initvals directly")

# The isolated D2B owner shares the same contract core.
start = initvals.find("int ob_initvals_run_d2b(")
end = initvals.find("int ob_initvals_test(", start)
require(start >= 0 and end > start, "ob_initvals_run_d2b definition not found")
if start >= 0 and end > start:
    d2b = initvals[start:end]
    require("ob_fw_chain_core(" in d2b,
            "ob_initvals_run_d2b does not use the pure core")
    require("ob_ucode_run_d2a(" not in d2b,
            "ob_initvals_run_d2b still sequences D2A directly")
    require("ob_initvals_apply_common_ex(" not in d2b,
            "ob_initvals_run_d2b still sequences common initvals directly")

# Probe order: attach -> identity gate -> firmware preflight -> test branch.
attach = core.find("ob_acphy_attach_identity(hw)")
gate = core.find("ob_fw_chain_identity_gate(hw, true)")
preflight = core.find("ob_fw_probe(hw)", gate)
branch = core.find("mode == OB_ISOLATED_FW_CHAIN_TEST", preflight)
post = core.find("ob_d11_post_ucode_init(hw)")
require(attach >= 0 and gate > attach, "identity gate is not after attach")
require(preflight > gate, "firmware preflight is not after the identity gate")
require(branch > preflight, "fw_chain_test branch is not after preflight")
require(post > branch, "normal post-ucode call is not after the test branch")
require(core.count("ob_d11_post_ucode_init(hw)") == 1,
        "normal post-ucode owner is not called exactly once")
require("hw->fw_chain_test_only" in core,
        "probe/remove does not record fw_chain_test_only")
require(core.find("if (hw->fw_chain_test_only)") > core.find("void ob_remove("),
        "remove guard is missing or misplaced")

# Mode is appended and select26 is the probe selector.
require("OB_ISOLATED_FW_CHAIN_TEST" in ucode,
        "fw_chain_test_only enum value missing")
require("ob_isolated_mode_select26(" in ucode,
        "select26 wrapper missing")
require("ob_isolated_mode_select26(" in core,
        "ob_probe does not use select26")

# The pure contract must not contain kernel MMIO/firmware APIs.
for token in ("request_firmware", "ob_d11_", "ob_dma_", "ob_irq_",
              "ieee80211_"):
    require(token not in contract,
            f"kernel API leaked into the pure contract: {token}")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)
print("test_fw_chain_source: PASS")
