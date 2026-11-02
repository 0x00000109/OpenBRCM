#!/usr/bin/env python3
"""Host tests for scripts/runtime-test.sh argument parsing and --dry-run.

These tests never load/unload the module and never touch hardware; they only
exercise the CLI (--help, --dry-run) as an unprivileged user.
"""
import re
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts" / "runtime-test.sh"

MODES = [
    "fw_validate_only",
    "ucode_test_only",
    "initvals_test_only",
    "fw_chain_test_only",
    "dma_test_only",
    "d11_tail_test_only",
    "sprom_evidence_only",
    "bsinitvals_test_only",
    "radio_id_probe_only",
    "radio2069_rev4_probe_only",
    "acphy_pre_radio_test_only",
    "acphy_radio_postlpf_test_only",
    "acphy_cal_inputs_probe_only",
    "acphy_class0_radio_local_0413_test_only",
    "acphy_class0_post0413_phase1_0413_test_only",
    "acphy_class0_postphase1_phase2_0413_test_only",
    "acphy_class0_postphase2_sub9591e_return_test_only",
    "acphy_class0_postbmac_phyreg1b0_branch_gate_test_only",
    "rev42_ai_probe_only",
    "rev42_ai_enable_only",
]

# A complete, clean radio_id_probe_only current-run log (exact marker strings).
RADIO_CLEAN = """\
radio-probe: BEGIN
radio-probe: prep host up (host_is_pcie2=1)
radio-probe: d11_clk1e0=070b0042 bit1=1 core_enabled=1
radio-probe: ioctl=00000010 mpclke=1
radio-probe: pre_access=0x04020402
radio-probe: reg0_raw=0x0010
radio-probe: reg1_raw=0x2069
radio-probe: radioid=0x2069
radio-probe: radiorev=0x10
radio-probe: revision_class=1
radio-probe: id_accepted=1 id_is_2069=1
radio-probe: pll_branch=A
radio-probe: post_access=0x04020402
radio-probe: PASS
radio-probe: STOPPED BEFORE PLL/RADIO INIT
"""

# A complete, clean W2 chain stop at POST_UCODE_READY.  The attach prefix and
# exact identity/counts are part of the contract; the success line alone is not.
FW_CHAIN_CLEAN = """\
fw-chain: identity PASS scope=runtime chip=4352 d11=04bf:0812 phy=000b:01 radio=2069:04:00
OB_ATTACH_CP0_ENTRY state=7 chip=0x43a0 chiprev=3 d11rev=42
OB_ATTACH_CP2_PHY_PREREQS_READY
OB_ATTACH_CP3_PHY_IDENTITY raw=0x0b2a type=0x0b rev=42
OB_ATTACH_CP4_RADIO_IDENTITY id=0x2069 rev=4 class=4
OB_ATTACH_CP5_IDENTITY_READY state=8
fw-chain: PASS stage=done image=brcm/bcm4352-d11ac1initvals42.bin words=10850 records=610 psm_polls=37 runtime_identity=1
fw-chain-test: PASS at POST_UCODE_READY; stopped before PHY/radio/DMA/IRQ/mac80211
"""

RADIO2069_CLEAN = """\
FIXED_WINDOW_BASELINE=READABLE FABRIC_BASELINE_ACCEPTED=YES
radio2069-rev4: RADIO_REV4_PREFS_REACHED
radio2069-rev4: RADIO_REV4_PREFS_COMPLETE
radio2069-rev4: RADIO2069_SYNTH_BEGIN
radio2069-rev4: RADIO2069_SYNTH_COMPLETE
radio2069-rev4: FINAL_TRUSTED_SENTINEL=0x04000400
radio2069-rev4: BOUNDED_STOP
"""

# A complete, clean rev42_ai_probe_only current-run log. Every line mirrors an
# exact dev_emerg() format string in src/ob_diag.c:ob_ai_probe_test(), in the
# order the function emits them. Values are illustrative placeholders only --
# this fixture proves marker agreement between the driver and the harness, it
# asserts nothing about the hardware.
AI_CLEAN = """\
rev42-ai: BEGIN (BCMA-owned addressing; core->addr=0x18001000 core->wrap=0x18101000; no config write, no iomap)
PRE  seq=1 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0804 window=core->wrap
POST seq=1 result=ok readback=0x00000000
PRE  seq=2 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0800 window=core->wrap
POST seq=2 result=ok readback=0x00000000
PRE  seq=3 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0408 window=core->wrap
POST seq=3 result=ok readback=0x00000003
PRE  seq=4 phase=ai_wrapper_validate op=BCMA_READ32 off=0x0ffc window=core->addr
POST seq=4 result=ok readback=0x0000ffff
PRE  seq=5 phase=ai_wrapper_validate op=PCI_CFG_READ offset=0x80+0x70 (observation only)
rev42-ai: cfg-readback rc(0x80)=0 rc(0x70)=0 status=ok
rev42-ai: IOCTL=0x00000003 RESET_CTL=0x00000000 RESET_ST=0x00000000 probe_0ffc=0x0000ffff (observation only) ai_iscoreup=1
rev42-ai: window cfg(0x80)=0x18001000 cfg(0x70)=0x18101000 vs core->addr=0x18001000 core->wrap=0x18101000 coherent=1
rev42-ai: all-ones observed (wrapper=0 core=0); observation only, not a wrong-window discriminator
rev42-ai: classification WRAPPER_REACHABLE_CORE_UP
rev42-ai: fault-latch=0 (0 = no device-loss latch was set during this probe)
rev42-ai: END rev42_ai_probe_only COMPLETE - read-only; no window write, no core reset, no PHY/radio/PLL/DMA/IRQ
"""

# A clean rev42_ai_enable_only run: the upstream bcma_core_enable()
# sequence (flags=0) in the BCMA wrapper window, each write validated by its
# own documented readback, followed by the vendor predicate. No D11 core read
# exists in this mode at all.
# Values are illustrative placeholders; this proves marker agreement between
# the driver and the harness, and asserts nothing about hardware.
AI_EN_CLEAN = """\
rev42-ai-en: BEGIN (AI wrapper only; BCMA-owned addressing; core->addr=0x18001000 core->wrap=0x18101000; no D11 functional-core access in this mode)
rev42-ai-en: PRE  seq=1 step=PRE_RESET_ST op=BCMA_AREAD32 off=0x0804 window=core->wrap
rev42-ai-en: POST seq=1 step=PRE_RESET_ST op=BCMA_AREAD32 status=ok readback=0x00000000
rev42-ai-en: PRE  seq=2 step=PRE_RESET_CTL op=BCMA_AREAD32 off=0x0800 window=core->wrap
rev42-ai-en: POST seq=2 step=PRE_RESET_CTL op=BCMA_AREAD32 status=ok readback=0x00000001
rev42-ai-en: PRE  seq=3 step=PRE_IOCTL op=BCMA_AREAD32 off=0x0408 window=core->wrap
rev42-ai-en: POST seq=3 step=PRE_IOCTL op=BCMA_AREAD32 status=ok readback=0x00000000
rev42-ai-en: PRE  seq=4 step=E1_IOCTL_SET_FGC op=BCMA_AWRITE32 off=0x0408 value=0x00000003 window=core->wrap
rev42-ai-en: POST seq=4 step=E1_IOCTL_SET_FGC op=BCMA_AWRITE32 status=confirmed wrote=0x00000003 readback=0x00000003
rev42-ai-en: PRE  seq=5 step=E2_RESET_CTL_DEASSERT op=BCMA_AWRITE32 off=0x0800 value=0x00000000 window=core->wrap
rev42-ai-en: POST seq=5 step=E2_RESET_CTL_DEASSERT op=BCMA_AWRITE32 status=confirmed wrote=0x00000000 readback=0x00000000
rev42-ai-en: DELAY seq=5 step=E2_RESET_CTL_DEASSERT udelay=1
rev42-ai-en: PRE  seq=6 step=E3_IOCTL_CLR_FGC op=BCMA_AWRITE32 off=0x0408 value=0x00000001 window=core->wrap
rev42-ai-en: POST seq=6 step=E3_IOCTL_CLR_FGC op=BCMA_AWRITE32 status=confirmed wrote=0x00000001 readback=0x00000001
rev42-ai-en: DELAY seq=6 step=E3_IOCTL_CLR_FGC udelay=1
rev42-ai-en: pre RESET_ST=0x00000000 RESET_CTL=0x00000001 IOCTL=0x00000000
rev42-ai-en: post IOCTL_E1=0x00000003 RESET_CTL_E2=0x00000000 IOCTL_E3=0x00000001 ai_iscoreup=1
rev42-ai-en: counters reads_attempted=6 reads_completed=6 writes_attempted=3 writes_confirmed=3 (max 6 reads / 3 writes)
rev42-ai-en: D11_WRAPPER_CORE_UP validated on the documented readbacks (wrapper predicate only)
rev42-ai-en: classification D11_WRAPPER_CORE_UP
rev42-ai-en: remove-policy=RETAIN (RETAIN = device kept bound, no teardown, reboot to recover)
rev42-ai-en: fault-latch=0 (0 = no device-loss latch was set during this mode)
rev42-ai-en: END rev42_ai_enable_only D11_WRAPPER_CORE_UP - wrapper writes only (IOCTL x2, RESET_CTL); no D11 functional-core access in any branch; no PHY/radio/PLL/cal/DMA/IRQ/mac80211
"""

# The fail-closed branch: the pre-state is not the reset-held state this
# contract covers, so the mode refuses before the first write.
AI_EN_PRECHECK = """\
rev42-ai-en: BEGIN (AI wrapper only; BCMA-owned addressing; core->addr=0x18001000 core->wrap=0x18101000; no D11 functional-core access in this mode)
rev42-ai-en: PRE  seq=1 step=PRE_RESET_ST op=BCMA_AREAD32 off=0x0804 window=core->wrap
rev42-ai-en: POST seq=1 step=PRE_RESET_ST op=BCMA_AREAD32 status=ok readback=0x00000000
rev42-ai-en: PRE  seq=2 step=PRE_RESET_CTL op=BCMA_AREAD32 off=0x0800 window=core->wrap
rev42-ai-en: POST seq=2 step=PRE_RESET_CTL op=BCMA_AREAD32 status=ok readback=0x00000000
rev42-ai-en: PRE  seq=3 step=PRE_IOCTL op=BCMA_AREAD32 off=0x0408 window=core->wrap
rev42-ai-en: POST seq=3 step=PRE_IOCTL op=BCMA_AREAD32 status=ok readback=0x00000000
rev42-ai-en: STOP seq=0 step=admission reason=prestate_unexpected error=-5 (no further register access)
rev42-ai-en: pre RESET_ST=0x00000000 RESET_CTL=0x00000000 IOCTL=0x00000000
rev42-ai-en: counters reads_attempted=3 reads_completed=3 writes_attempted=0 writes_confirmed=0 (max 6 reads / 3 writes)
rev42-ai-en: ENABLE_PRECHECK_REJECTED step=admission op=none off=0x0000 reason=prestate_unexpected last_value=0x00000000 error=-5
rev42-ai-en: classification ENABLE_PRECHECK_REJECTED
rev42-ai-en: remove-policy=CLEAN (RETAIN = device kept bound, no teardown, reboot to recover)
rev42-ai-en: fault-latch=0 (0 = no device-loss latch was set during this mode)
rev42-ai-en: END rev42_ai_enable_only ENABLE_PRECHECK_REJECTED - wrapper writes only (IOCTL x2, RESET_CTL); no D11 functional-core access in any branch; no PHY/radio/PLL/cal/DMA/IRQ/mac80211
"""

ACPHY_PRERADIO_CLEAN = """\
ACPHY_PRERADIO_HW_CP0_ENTRY
ACPHY_PRERADIO_HW_CP1_IDENTITY_OK
ACPHY_PRERADIO_HW_CP2_CHANSPEC_1001
ACPHY_PRERADIO_HW_CP3_TRANSACTION_DONE
ACPHY_PRERADIO_HW_CP4_SHM_OK
ACPHY_PRERADIO_HW_CP5_CFLAGS_OK
ACPHY_PRERADIO_HW_PASS
"""

ACPHY_POSTLPF_CLEAN = """\
ACPHY_PRERADIO_HW_PASS
ACPHY_POSTLPF prefix complete (blocked_at=REV1_AFTER_SUB92500)
ACPHY_A7089_GOLDEN_DONE ops=1168
ACPHY_INIT_DONE
ACPHY_RADIO_COMPLETE
"""

ACPHY_CAL_INPUTS_CLEAN = ACPHY_POSTLPF_CLEAN + """\
ACPHY_CAL_INPUTS_BEGIN chanspec=0x1001 radio_revision_class=0
ACPHY_CAL_INPUT phycal_tempdelta=UNKNOWN source=vendor_runtime_getter
ACPHY_CAL_INPUT pll_selector=0 source=attach_identity
ACPHY_CAL_INPUT channel_row=63 key=1 offset=0x1c8c source=controlled_fixture
ACPHY_CAL_CONTRACT_BLOCKED first=4 unknown_mask=0x01
ACPHY_CAL_CONTRACT_STOP
"""

POST0413_CLEAN = """\
OB_ATTACH_CP0_ENTRY
OB_ATTACH_CP1_RESET_READY
OB_ATTACH_CP2_PHY_PREREQS_READY
OB_ATTACH_CP3_PHY_IDENTITY raw=0xcb01 type=0x0b rev=1
OB_ATTACH_CP4_RADIO_IDENTITY id=0x2069 rev=4 class=0
OB_ATTACH_CP5_IDENTITY_READY state=4
OB_UCODE_CP0_START
POST_UCODE_READY
ACPHY_PRERADIO_HW_PASS
ACPHY_CLASS0_0413_CP0_ENTRY
ACPHY_CLASS0_0413_TARGET_OK
ACPHY_CLASS0_0413_MAC_QUIESCE result=PASS polls=0
ACPHY_CLASS0_0413_PRE_MUTATION
ACPHY_CLASS0_0413_POLL attempts=7 last_value=0x0010 success=1
ACPHY_CLASS0_0413_PASS
ACPHY_CLASS0_POST0413_CP0_ENTRY
ACPHY_CLASS0_POST0413_CP1_SAMPLES r414=0x0011 r415=0x0022 derived=0x08
ACPHY_CLASS0_POST0413_CP2_PHASE1_SETUP_DONE
ACPHY_CLASS0_POST0413_CP3_PRE_POLL
ACPHY_CLASS0_POST0413_POLL attempts=3 last_value=0x0010 success=1
ACPHY_CLASS0_POST0413_PASS
ACPHY_CLASS0_POST0413_STOP
"""

POSTPHASE1_CLEAN = POST0413_CLEAN.replace(
    "ACPHY_CLASS0_POST0413_STOP\n",
    """ACPHY_CLASS0_POSTPHASE1_CP0_ENTRY
ACPHY_CLASS0_POSTPHASE1_CP1_0416 sample=0xa5c3 low5=0x0003
ACPHY_CLASS0_POSTPHASE1_CP2_PHASE1_CHAIN_APPLY_DONE
ACPHY_CLASS0_POSTPHASE1_CP3_PHASE2_SETUP_DONE
ACPHY_CLASS0_POSTPHASE1_CP4_PHASE2_CHAIN_SETUP_DONE
ACPHY_CLASS0_POSTPHASE1_CP5_PRE_POLL
ACPHY_CLASS0_POSTPHASE1_POLL attempts=7 last_value=0x0010 success=1
ACPHY_CLASS0_POSTPHASE1_PASS
ACPHY_CLASS0_POSTPHASE1_STOP
""",
)

POSTPHASE2_CLEAN = POSTPHASE1_CLEAN.replace(
    "ACPHY_CLASS0_POSTPHASE1_STOP\n",
    """ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY
ACPHY_CLASS0_POSTPHASE2_CP1_0411_CLEANUP_DONE
ACPHY_CLASS0_POSTPHASE2_CP2_0416 sample=0x02a5 field=0x15
ACPHY_CLASS0_POSTPHASE2_CP3_CHAIN0_DONE
ACPHY_CLASS0_POSTPHASE2_CP4_CHAIN1_DONE
ACPHY_CLASS0_POSTPHASE2_CP5_PHASE_END_DONE
ACPHY_CLASS0_POSTPHASE2_RETURN
ACPHY_CLASS0_POSTPHASE2_PASS
ACPHY_CLASS0_POSTPHASE2_STOP
""",
)

POSTBMAC_CLEAN = POSTPHASE2_CLEAN.replace(
    "ACPHY_CLASS0_POSTPHASE2_PASS\nACPHY_CLASS0_POSTPHASE2_STOP\n", ""
).replace(
    "OB_ATTACH_CP0_ENTRY\n",
    "ACPHY_OTP8E2_OBS word=0x0600 value=0x06 jbe_le3=0\n"
    "OB_ATTACH_CP0_ENTRY\n",
) + """\
ACPHY_CLASS0_POSTBMAC_CP0_ENTRY
ACPHY_CLASS0_POSTBMAC_CP1_PHY1B0_RMW_DONE
ACPHY_CLASS0_POSTBMAC_CP2_PHY0_SAMPLE sample=0xcb01 low4=0x1 ja_gt1=0
ACPHY_CLASS0_POSTBMAC_CP3_BRANCH_GATE
ACPHY_CLASS0_POSTBMAC_CP4_PMU_0658 old=0xdeadbeef write=0x00000000 readback=0x00000000
ACPHY_CLASS0_POSTBMAC_CP5_PMU_065C old=0xa5fabcde write=0xa43abcde readback=0xa43abcde
ACPHY_CLASS0_POSTBMAC_CP6_GPIO_006C old=0x12345678 write=0x12340000 readback=0x12340000
ACPHY_CLASS0_POSTBMAC_PASS
ACPHY_CLASS0_POSTBMAC_STOP
"""

D11_CORE_STATE_CAPTURE = ACPHY_PRERADIO_CLEAN + """\
D11_CORE_STATE_CP0 ioctl=0x00000000 reset=0x00000001 enabled=0
D11_CORE_STATE_CP1 ioctl=0x00000000 reset=0x00000001 enabled=0
D11_CORE_ENABLE_CALLED=1
D11_CORE_STATE_CP2 ioctl=0x00000001 reset=0x00000001 enabled=0
D11_CORE_STATE_CP3 ioctl=0x00000001 reset=0x00000001 enabled=0
D11_CORE_POSTCONDITION=RESET_ASSERTED
D11_LIFECYCLE_6_CLK1E0_RESULT value=0xffffffff
"""

# The shape of the REAL 2026-09-27 run (marker openbrcm-test-20260927-014301-8836)
# after the config-readback-status fix. Both pci_read_config_dword() calls
# FAILED, so the kernel left PCI_ERROR_RESPONSE (0xffffffff) in the out
# parameters. The old code ignored the status and printed
#   cfg(0x80)=0xffffffff ... coherent=0
# which reads as "the BCMA window was not selected" -- a claim the run does not
# support, since the three wrapper reads answered normally. The driver now
# reports the status and prints <unreadable> / coherent=unknown.
#
# Note the values are the MEASURED D11-ADDR-U1 addresses: core->addr is the
# D11 core window and core->wrap is the BCMA wrapper window.
AI_CFG_READ_FAIL = """\
rev42-ai: BEGIN (BCMA-owned addressing; core->addr=0x18001000 core->wrap=0x18101000; no config write, no iomap)
PRE  seq=1 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0804 window=core->wrap
POST seq=1 result=ok readback=0x00000000
PRE  seq=2 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0800 window=core->wrap
POST seq=2 result=ok readback=0x00000001
PRE  seq=3 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0408 window=core->wrap
POST seq=3 result=ok readback=0x00000000
PRE  seq=4 phase=ai_wrapper_validate op=BCMA_READ32 off=0x0ffc window=core->addr
POST seq=4 result=ok readback=0xffffffff
PRE  seq=5 phase=ai_wrapper_validate op=PCI_CFG_READ offset=0x80+0x70 (observation only)
rev42-ai: cfg-readback rc(0x80)=-2 rc(0x70)=-2 status=error
rev42-ai: IOCTL=0x00000000 RESET_CTL=0x00000001 RESET_ST=0x00000000 probe_0ffc=0xffffffff (observation only) ai_iscoreup=0
rev42-ai: window cfg(0x80)=<unreadable> cfg(0x70)=<unreadable> vs core->addr=0x18001000 core->wrap=0x18101000 coherent=unknown (PCI config readback failed; not a window-selection verdict)
rev42-ai: all-ones observed (wrapper=0 core=1); observation only, not a wrong-window discriminator
rev42-ai: classification WRAPPER_REACHABLE_CORE_DOWN
rev42-ai: fault-latch=0 (0 = no device-loss latch was set during this probe)
rev42-ai: END rev42_ai_probe_only COMPLETE - read-only; no window write, no core reset, no PHY/radio/PLL/DMA/IRQ
"""

# The same run with every register read back all-ones. This MUST still be a
# clean pass: all-ones is recorded as an observation and is never used as a
# reachability verdict or as a condition (ob_diag.h:ob_diag_ai_summarise).
# The config readback itself SUCCEEDED here, hence status=ok and coherent=1.
AI_ALL_ONES = """\
rev42-ai: BEGIN (BCMA-owned addressing; core->addr=0x18001000 core->wrap=0x18101000; no config write, no iomap)
PRE  seq=1 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0804 window=core->wrap
POST seq=1 result=ok readback=0xffffffff
PRE  seq=2 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0800 window=core->wrap
POST seq=2 result=ok readback=0xffffffff
PRE  seq=3 phase=ai_wrapper_validate op=BCMA_AREAD32 off=0x0408 window=core->wrap
POST seq=3 result=ok readback=0xffffffff
PRE  seq=4 phase=ai_wrapper_validate op=BCMA_READ32 off=0x0ffc window=core->addr
POST seq=4 result=ok readback=0xffffffff
PRE  seq=5 phase=ai_wrapper_validate op=PCI_CFG_READ offset=0x80+0x70 (observation only)
rev42-ai: cfg-readback rc(0x80)=0 rc(0x70)=0 status=ok
rev42-ai: IOCTL=0xffffffff RESET_CTL=0xffffffff RESET_ST=0xffffffff probe_0ffc=0xffffffff (observation only) ai_iscoreup=0
rev42-ai: window cfg(0x80)=0x18001000 cfg(0x70)=0x18101000 vs core->addr=0x18001000 core->wrap=0x18101000 coherent=1
rev42-ai: all-ones observed (wrapper=1 core=1); observation only, not a wrong-window discriminator
rev42-ai: classification WRAPPER_REACHABLE_CORE_DOWN
rev42-ai: fault-latch=0 (0 = no device-loss latch was set during this probe)
rev42-ai: END rev42_ai_probe_only COMPLETE - read-only; no window write, no core reset, no PHY/radio/PLL/DMA/IRQ
"""

# A complete, contract-satisfying D3B boundary witness run that rode the
# vendor-faithful FIFO-530 continuation. Every line mirrors an exact driver or
# harness string (src/ob_d3a1.c `EXPIRED_AND_CONTINUED`, src/ob_d3b.c
# `D3B_BOUNDARY_WITNESS*`) in the order the driver emits it; the register
# values are illustrative placeholders. The two continuation lines are the
# ONLY fault-word carriers in this log ("completion NOT verified"), and the
# run is clean: M1a launch checklist finding F-A1.
WITNESS_LIVE_FIFO = """\
d3b-test: BEGIN (DMA stays live through D3B in vendor position)
d3a1-test: sub_67efd 0x530[0] EXPIRED_AND_CONTINUED programmed=8007 last=0007 post=0007 reads=21 (vendor-faithful opt-in; completion NOT verified)
d3a1-test: sub_67efd 0x530[1] EXPIRED_AND_CONTINUED programmed=8007 last=0007 post=0007 reads=21 (vendor-faithful opt-in; completion NOT verified)
d3a1-test: PASS WITH UNVERIFIED FIFO COMPLETION - 0x530 expired and continued at 2 of 2 entries (first index 0 last=0007 reads=21) under d11_fifo530_continue=1; the tail ran vendor-faithfully but the FIFO completion was never proven
D3B_BOUNDARY_WITNESS: begin at the D3A1 exit
D3B_BOUNDARY_WITNESS: stage=B1 window=cc offset=0x01e0 width=32 value=0x070b0042 dev_lost=0 start_ns=100 end_ns=102500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B2 window=cc offset=0x0600 width=32 value=0x00000000 dev_lost=0 start_ns=200 end_ns=202500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B3 window=cc offset=0x0608 width=32 value=0x00000001 dev_lost=0 start_ns=300 end_ns=302500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B4 window=cc offset=0x0618 width=32 value=0x0000ffff dev_lost=0 start_ns=400 end_ns=402500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B5 window=cc offset=0x061c width=32 value=0x0000ffff dev_lost=0 start_ns=500 end_ns=502500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B6 window=d11 offset=0x01e0 width=32 value=0x070b0042 dev_lost=0 start_ns=600 end_ns=602500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B7 window=d11 offset=0x01e8 width=32 value=0x01002000 dev_lost=0 start_ns=700 end_ns=702500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B8 window=d11 offset=0x03d8 width=16 value=0x0000 dev_lost=0 start_ns=800 end_ns=802500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B9 window=d11 offset=0x03da width=16 value=0x0000 dev_lost=0 start_ns=900 end_ns=902500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS_SUMMARY: cc_clk_ctl_st=0x070b0042 cc_haveht=1 pmu_ctl=0x00000000 pmu_stat=0x00000001 pmu_minres=0x0000ffff pmu_maxres=0x0000ffff d11_clk_ctl_st=0x070b0042 d11_phypllctl=0x01002000 b1=LIVE b2=LIVE b3=LIVE b4=LIVE b5=LIVE b6=LIVE b7=LIVE b8_03d8=LIVE b9_03da=LIVE window_alive=1 writes_issued=0 verdict=island_live_window_alive fifo530_continue=1 fifo530_expiry_continued=1 fifo530_entries_continued=2 fifo530_first_idx=0 fifo530_last=0007 fifo530_reads=21 fifo530_invalid530=0 fifo530_invalid540=0
"""

# Witness v2 (d3b_witness_v2=1) — a contract-complete extended capture:
# the v1 sequence plus the P1..P4 PMU-domain reads, the C1/C2 controls and
# the B10 vendor read, with the extended summary fields.
WITNESS_V2_LIVE = """\
d3b-test: BEGIN (DMA stays live through D3B in vendor position)
D3B_BOUNDARY_WITNESS: begin at the D3A1 exit (the vendor's 0x3e0 read site)
D3B_BOUNDARY_WITNESS: stage=B1 window=cc offset=0x01e0 width=32 value=0x070b0042 dev_lost=0 start_ns=100 end_ns=102500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B2 window=cc offset=0x0600 width=32 value=0x00000000 dev_lost=0 start_ns=200 end_ns=202500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B3 window=cc offset=0x0608 width=32 value=0x00000001 dev_lost=0 start_ns=300 end_ns=302500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B4 window=cc offset=0x0618 width=32 value=0x0000ffff dev_lost=0 start_ns=400 end_ns=402500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B5 window=cc offset=0x061c width=32 value=0x0000ffff dev_lost=0 start_ns=500 end_ns=502500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=P1 window=cc offset=0x0604 width=32 value=0x00600805 dev_lost=0 start_ns=550 end_ns=552500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=P2 window=cc offset=0x060c width=32 value=0x00000000 dev_lost=0 start_ns=560 end_ns=562500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=P3 window=cc offset=0x0660 width=32 value=0x00000003 dev_lost=0 start_ns=570 end_ns=572500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=P4 window=cc offset=0x0664 width=32 value=0x0006130c dev_lost=0 start_ns=580 end_ns=582500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B6 window=d11 offset=0x01e0 width=32 value=0x070b0042 dev_lost=0 start_ns=600 end_ns=602500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B7 window=d11 offset=0x01e8 width=32 value=0x01002000 dev_lost=0 start_ns=700 end_ns=702500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=C1 window=d11 offset=0x03fe width=16 value=0x0000 dev_lost=0 start_ns=750 end_ns=752500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=C2 window=d11 offset=0x03fc width=16 value=0x0000 dev_lost=0 start_ns=760 end_ns=762500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B8 window=d11 offset=0x03d8 width=16 value=0xcb01 dev_lost=0 start_ns=800 end_ns=802500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B9 window=d11 offset=0x03da width=16 value=0x0000 dev_lost=0 start_ns=900 end_ns=902500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS: stage=B10 window=d11 offset=0x03e0 width=16 value=0x0b2a dev_lost=0 start_ns=1000 end_ns=1002500 duration_us=2 class=LIVE
D3B_BOUNDARY_WITNESS_SUMMARY: cc_clk_ctl_st=0x070b0042 cc_haveht=1 pmu_ctl=0x00000000 pmu_stat=0x00000001 pmu_minres=0x0000ffff pmu_maxres=0x0000ffff d11_clk_ctl_st=0x070b0042 d11_phypllctl=0x01002000 b1=LIVE b2=LIVE b3=LIVE b4=LIVE b5=LIVE b6=LIVE b7=LIVE b8_03d8=LIVE b9_03da=LIVE window_alive=1 writes_issued=0 verdict=island_live_window_alive fifo530_continue=0 fifo530_expiry_continued=0 fifo530_entries_continued=0 fifo530_first_idx=-1 fifo530_last=0000 fifo530_reads=0 fifo530_invalid530=0 fifo530_invalid540=0 witness_v2=1 pmu_cap=0x00600805 pmu_otpstat=0x00000000 pllctl_addr=0x00000003 pllctl_data=0x0006130c p1=LIVE p2=LIVE p3=LIVE p4=LIVE c1_03fe=LIVE c2_03fc=LIVE b10_03e0=LIVE
"""


def run(*args, env=None):
    return subprocess.run(
        ["bash", str(SCRIPT), *args],
        cwd=str(REPO),
        capture_output=True,
        text=True,
        env=dict(os.environ, **(env or {})),
    )


def evaluate(mode, text, marker=None, env=None):
    with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as fh:
        fh.write(text)
        path = fh.name
    args = ["--evaluate-log", mode, path]
    if marker:
        args += ["--marker", marker]
    return run(*args, env=env)


def head_rev():
    return subprocess.check_output(
        ["git", "-C", str(REPO), "rev-parse", "HEAD"], text=True
    ).strip()


class RuntimeTestCli(unittest.TestCase):
    def test_help_lists_all_modes(self):
        r = run("--help")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("Usage:", r.stdout)
        for m in MODES:
            self.assertIn(m, r.stdout)

    def test_missing_mode_rejected(self):
        r = run("--dry-run")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("exactly one --mode", r.stderr)

    def test_invalid_mode_rejected(self):
        r = run("--dry-run", "--mode", "bogus")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("invalid --mode", r.stderr)

    def test_unknown_option_rejected(self):
        r = run("--dry-run", "--bogus")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("unknown option", r.stderr)

    def test_requires_argument(self):
        for flag in ("--mode", "--candidate"):
            r = run("--dry-run", flag)
            self.assertNotEqual(r.returncode, 0, flag)
            self.assertIn("requires an argument", r.stderr)

    def test_dry_run_uses_mode_unity_and_never_bare_insmod(self):
        for m in MODES:
            r = run("--dry-run", "--mode", m)
            self.assertEqual(r.returncode, 0, (m, r.stderr))
            self.assertIn('insmod', r.stdout)
            self.assertIn(f'"{m}=1"', r.stdout)
            # no printed insmod line may lack the mode=1 argument
            for line in r.stdout.splitlines():
                if line.lstrip().startswith("insmod"):
                    self.assertIn(f"{m}=1", line, f"bare insmod for {m}: {line!r}")

    def test_witness_fifo530_env_composition(self):
        """M1a: OB_D11_FIFO530_CONTINUE composes the insmod line (alone or
        with OB_D3B_BOUNDARY_WITNESS) and changes nothing without it."""
        def dry(mode, env=None):
            r = run("--dry-run", "--mode", mode, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            lines = [l for l in r.stdout.splitlines()
                     if l.lstrip().startswith("insmod") and f"{mode}=1" in l]
            self.assertEqual(len(lines), 1, r.stdout)
            return lines[0]

        mode = "bsinitvals_test_only"
        plain = dry(mode)
        self.assertNotIn("d3b_boundary_witness", plain)
        self.assertNotIn("d11_fifo530_continue", plain)

        witness = dry(mode, {"OB_D3B_BOUNDARY_WITNESS": "1"})
        self.assertIn('"d3b_boundary_witness=1"', witness)
        self.assertNotIn("d11_fifo530_continue", witness)

        both = dry(mode, {"OB_D3B_BOUNDARY_WITNESS": "1",
                          "OB_D11_FIFO530_CONTINUE": "1"})
        self.assertIn(f'"{mode}=1" "d3b_boundary_witness=1" '
                      f'"d11_fifo530_continue=1"', both)

        fifo = dry(mode, {"OB_D11_FIFO530_CONTINUE": "1"})
        self.assertIn(f'"{mode}=1" "d11_fifo530_continue=1"', fifo)
        self.assertNotIn("d3b_boundary_witness", fifo)

        tail = "d11_tail_test_only"
        tail_line = dry(tail, {"OB_D11_FIFO530_CONTINUE": "1"})
        self.assertIn(f'"{tail}=1" "d11_fifo530_continue=1"', tail_line)

    def test_witness_v2_env_composition(self):
        """Witness v2 composes the insmod line (with or without the FIFO-530
        continuation) and is refused when the witness itself is not armed."""
        def dry(mode, env=None):
            r = run("--dry-run", "--mode", mode, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            lines = [l for l in r.stdout.splitlines()
                     if l.lstrip().startswith("insmod") and f"{mode}=1" in l]
            self.assertEqual(len(lines), 1, r.stdout)
            return lines[0]

        mode = "bsinitvals_test_only"
        v2 = dry(mode, {"OB_D3B_BOUNDARY_WITNESS": "1",
                        "OB_D3B_WITNESS_V2": "1"})
        self.assertIn('"d3b_boundary_witness=1" "d3b_witness_v2=1"', v2)
        self.assertNotIn("d11_fifo530_continue", v2)

        all_three = dry(mode, {"OB_D3B_BOUNDARY_WITNESS": "1",
                               "OB_D11_FIFO530_CONTINUE": "1",
                               "OB_D3B_WITNESS_V2": "1"})
        self.assertIn('"d3b_boundary_witness=1" "d11_fifo530_continue=1" '
                      '"d3b_witness_v2=1"', all_three)

        # v2 is a modifier: without the witness the run is refused (rc 2),
        # including --dry-run, before any hardware action.
        r = run("--dry-run", "--mode", mode, env={"OB_D3B_WITNESS_V2": "1"})
        self.assertEqual(r.returncode, 2, (r.stdout, r.stderr))
        self.assertIn("OB_D3B_WITNESS_V2=1 requires OB_D3B_BOUNDARY_WITNESS=1",
                      r.stderr)

    def test_t4_env_composition(self):
        """T4 (OB_L2_3E0_FIRST) composes the insmod line on the L2 mode only
        and is refused on any other mode or together with a sibling L2
        variable, including --dry-run and before any hardware action."""
        def dry(mode, env=None):
            r = run("--dry-run", "--mode", mode, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            lines = [l for l in r.stdout.splitlines()
                     if l.lstrip().startswith("insmod") and f"{mode}=1" in l]
            self.assertEqual(len(lines), 1, r.stdout)
            return lines[0]

        mode = "acphy_identity_timing_only"
        plain = dry(mode)
        self.assertNotIn("acphy_identity_3e0_first", plain)

        t4 = dry(mode, {"OB_L2_3E0_FIRST": "1"})
        self.assertIn(f'"{mode}=1" "acphy_identity_3e0_first=1"', t4)
        self.assertNotIn("acphy_identity_pll_witness", t4)

        r = run("--dry-run", "--mode", "d11_transaction_timing_only",
                env={"OB_L2_3E0_FIRST": "1"})
        self.assertEqual(r.returncode, 2, (r.stdout, r.stderr))
        self.assertIn(
            "OB_L2_3E0_FIRST=1 requires --mode acphy_identity_timing_only",
            r.stderr,
        )

        r = run("--dry-run", "--mode", mode,
                env={"OB_L2_3E0_FIRST": "1", "OB_L2_PLL_WITNESS": "1"})
        self.assertEqual(r.returncode, 2, (r.stdout, r.stderr))
        self.assertIn("exclusive with every other OB_L2_*", r.stderr)

    def test_post0413_mode_is_isolated_and_preserves_state(self):
        mode = "acphy_class0_post0413_phase1_0413_test_only"
        r = run("--dry-run", "--mode", mode)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout, rf'(?m)^insmod .*"{mode}=1"$')
        self.assertIn("preserve openbrcm/radio state; no rmmod", r.stdout)

    def test_post0413_parser_requires_complete_order(self):
        mode = "acphy_class0_post0413_phase1_0413_test_only"
        r = evaluate(mode, POST0413_CLEAN)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ACPHY_CLASS0_POST0413_PASS", r.stdout)
        self.assertIn("radio state preserved; no rmmod", r.stdout)

        missing = POST0413_CLEAN.replace(
            "ACPHY_CLASS0_POST0413_CP2_PHASE1_SETUP_DONE\n", "")
        r = evaluate(mode, missing)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertIn("no rmmod", r.stdout)

    def test_post0413_parser_rejects_reordered_pass_path(self):
        mode = "acphy_class0_post0413_phase1_0413_test_only"
        cp1 = "ACPHY_CLASS0_POST0413_CP1_SAMPLES r414=0x0011 r415=0x0022 derived=0x08\n"
        cp2 = "ACPHY_CLASS0_POST0413_CP2_PHASE1_SETUP_DONE\n"
        reordered = POST0413_CLEAN.replace(cp1 + cp2, cp2 + cp1)
        r = evaluate(mode, reordered)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        bad_poll = POST0413_CLEAN.replace(
            "ACPHY_CLASS0_POST0413_POLL attempts=3 last_value=0x0010 success=1",
            "ACPHY_CLASS0_POST0413_POLL attempts=101 last_value=0x0000 success=1")
        r = evaluate(mode, bad_poll)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_post0413_failure_is_preserved_without_success_inference(self):
        mode = "acphy_class0_post0413_phase1_0413_test_only"
        failed = """\
ACPHY_CLASS0_POST0413_CP0_ENTRY
ACPHY_CLASS0_POST0413_CP1_SAMPLES r414=0x0001 r415=0x0002 derived=0x00
ACPHY_CLASS0_POST0413_CP2_PHASE1_SETUP_DONE
ACPHY_CLASS0_POST0413_CP3_PRE_POLL
ACPHY_CLASS0_POST0413_POLL attempts=100 last_value=0x0000 success=0
ACPHY_CLASS0_POST0413_FAIL stage=POLL_TIMEOUT ret=-110
ACPHY_CLASS0_POST0413_STOP
"""
        r = evaluate(mode, failed)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertIn("radio state preserved; no rmmod", r.stdout)
        self.assertNotIn("CLEAN PASS", r.stdout)

        no_stop = failed.replace("ACPHY_CLASS0_POST0413_STOP\n", "")
        r = evaluate(mode, no_stop)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postphase1_mode_dry_run_preserves_state(self):
        mode = "acphy_class0_postphase1_phase2_0413_test_only"
        r = run("--dry-run", "--mode", mode)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout, rf'(?m)^insmod .*"{mode}=1"$')
        self.assertIn("preserve openbrcm/radio state; no rmmod", r.stdout)

    def test_postphase1_parser_requires_full_order_and_success(self):
        mode = "acphy_class0_postphase1_phase2_0413_test_only"
        r = evaluate(mode, POSTPHASE1_CLEAN)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ACPHY_CLASS0_POSTPHASE1_PASS", r.stdout)
        self.assertIn("radio state preserved; no rmmod", r.stdout)

        missing = POSTPHASE1_CLEAN.replace(
            "ACPHY_CLASS0_POSTPHASE1_CP4_PHASE2_CHAIN_SETUP_DONE\n", "")
        r = evaluate(mode, missing)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertIn("no rmmod", r.stdout)

        reordered = POSTPHASE1_CLEAN.replace(
            "ACPHY_CLASS0_POSTPHASE1_CP3_PHASE2_SETUP_DONE\n"
            "ACPHY_CLASS0_POSTPHASE1_CP4_PHASE2_CHAIN_SETUP_DONE\n",
            "ACPHY_CLASS0_POSTPHASE1_CP4_PHASE2_CHAIN_SETUP_DONE\n"
            "ACPHY_CLASS0_POSTPHASE1_CP3_PHASE2_SETUP_DONE\n",
        )
        r = evaluate(mode, reordered)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        premature_stop = POSTPHASE1_CLEAN.replace(
            "ACPHY_CLASS0_POST0413_PASS\n",
            "ACPHY_CLASS0_POST0413_PASS\nACPHY_CLASS0_POST0413_STOP\n",
        )
        r = evaluate(mode, premature_stop)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        wrong_poll = POSTPHASE1_CLEAN.replace(
            "attempts=7 last_value=0x0010 success=1",
            "attempts=101 last_value=0x0000 success=1",
        )
        r = evaluate(mode, wrong_poll)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postphase1_failure_stops_and_never_infers_pass(self):
        mode = "acphy_class0_postphase1_phase2_0413_test_only"
        failure = """\
ACPHY_CLASS0_POSTPHASE1_FAIL stage=POLL_TIMEOUT ret=-110
ACPHY_CLASS0_POSTPHASE1_STOP
"""
        r = evaluate(mode, failure)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postphase2_mode_dry_run_preserves_state(self):
        mode = "acphy_class0_postphase2_sub9591e_return_test_only"
        r = run("--dry-run", "--mode", mode)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout, rf'(?m)^insmod .*"{mode}=1"$')
        self.assertIn("preserve openbrcm/radio state; no rmmod", r.stdout)

    def test_postphase2_parser_accepts_complete_return_path(self):
        mode = "acphy_class0_postphase2_sub9591e_return_test_only"
        r = evaluate(mode, POSTPHASE2_CLEAN)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ACPHY_CLASS0_POSTPHASE2_RETURN", r.stdout)
        self.assertIn("radio state preserved; no rmmod", r.stdout)

    def test_postphase2_parser_rejects_missing_duplicate_and_reordered_markers(self):
        mode = "acphy_class0_postphase2_sub9591e_return_test_only"
        mutations = [
            POSTPHASE2_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE2_CP4_CHAIN1_DONE\n", ""),
            POSTPHASE2_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE2_CP3_CHAIN0_DONE\n",
                "ACPHY_CLASS0_POSTPHASE2_CP3_CHAIN0_DONE\n"
                "ACPHY_CLASS0_POSTPHASE2_CP3_CHAIN0_DONE\n"),
            POSTPHASE2_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE2_CP3_CHAIN0_DONE\n"
                "ACPHY_CLASS0_POSTPHASE2_CP4_CHAIN1_DONE\n",
                "ACPHY_CLASS0_POSTPHASE2_CP4_CHAIN1_DONE\n"
                "ACPHY_CLASS0_POSTPHASE2_CP3_CHAIN0_DONE\n"),
        ]
        for text in mutations:
            with self.subTest(text=text[-220:]):
                r = evaluate(mode, text)
                self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postphase2_parser_rejects_premature_stop_and_invalid_terminals(self):
        mode = "acphy_class0_postphase2_sub9591e_return_test_only"
        mutations = [
            POSTPHASE2_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE1_PASS\n",
                "ACPHY_CLASS0_POSTPHASE1_PASS\n"
                "ACPHY_CLASS0_POSTPHASE1_STOP\n"),
            POSTPHASE2_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE2_RETURN\n", ""),
            POSTPHASE2_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n",
                "ACPHY_CLASS0_POSTPHASE2_FAIL stage=READ_0416 ret=-5\n"
                "ACPHY_CLASS0_POSTPHASE2_STOP\n"
                "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n"),
            POSTPHASE2_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE2_STOP\n", ""),
            POSTPHASE2_CLEAN + "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n",
            POSTPHASE2_CLEAN + POSTPHASE2_CLEAN,
            POSTPHASE2_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n",
                "ACPHY_CLASS0_0413_CP0_ENTRY\n"
                "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n"),
        ]
        for text in mutations:
            with self.subTest(text=text[-220:]):
                r = evaluate(mode, text)
                self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postphase2_parser_requires_third_poll_before_cp0(self):
        mode = "acphy_class0_postphase2_sub9591e_return_test_only"
        text = POSTPHASE2_CLEAN.replace(
            "ACPHY_CLASS0_POSTPHASE1_POLL attempts=7 last_value=0x0010 success=1\n",
            "ACPHY_CLASS0_POSTPHASE1_POLL attempts=7 last_value=0x0000 success=0\n",
        )
        r = evaluate(mode, text)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postphase2_failure_requires_fail_then_stop_without_return(self):
        mode = "acphy_class0_postphase2_sub9591e_return_test_only"
        failure = POSTPHASE1_CLEAN.replace(
            "ACPHY_CLASS0_POSTPHASE1_STOP\n",
            "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n"
            "ACPHY_CLASS0_POSTPHASE2_FAIL stage=CHAIN1_0371 ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE2_STOP\n",
        )
        r = evaluate(mode, failure)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertIn("radio state preserved; no rmmod", r.stdout)
        self.assertIn("ACPHY_POSTPHASE2_LOG_VALIDATION: FAIL_PATH_COMPLETE",
                      r.stdout)
        self.assertNotIn("CLEAN PASS", r.stdout)
        no_entry = POSTPHASE1_CLEAN.replace(
            "ACPHY_CLASS0_POSTPHASE1_STOP\n",
            "ACPHY_CLASS0_POSTPHASE2_FAIL stage=DEVICE_LOST ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE2_STOP\n",
        )
        r = evaluate(mode, no_entry)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertIn("DIAGNOSTIC FAIL (radio state preserved; no rmmod)",
                      r.stdout)
        self.assertIn("ACPHY_POSTPHASE2_LOG_VALIDATION: FAIL_PATH_COMPLETE",
                      r.stdout)
        self.assertNotIn("ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY", r.stdout)
        early_no_entry = no_entry.replace(
            "ACPHY_CLASS0_POSTPHASE2_FAIL stage=DEVICE_LOST ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE2_STOP\n", "")
        early_no_entry = (
            "ACPHY_CLASS0_POSTPHASE2_FAIL stage=DEVICE_LOST ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE2_STOP\n" + early_no_entry
        )
        duplicate_cp0 = failure.replace(
            "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n",
            "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n"
            "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n",
        )
        cp0_after_terminal = failure.replace(
            "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n"
            "ACPHY_CLASS0_POSTPHASE2_FAIL stage=CHAIN1_0371 ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE2_STOP\n",
            "ACPHY_CLASS0_POSTPHASE2_FAIL stage=CHAIN1_0371 ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE2_STOP\n"
            "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n",
        )
        for invalid in (
            failure.replace("ACPHY_CLASS0_POSTPHASE2_STOP\n", ""),
            failure.replace(" ret=-5\n", "\n"),
            failure.replace(
                "ACPHY_CLASS0_POSTPHASE2_STOP\n",
                "ACPHY_CLASS0_POSTPHASE2_STOP\n"
                "ACPHY_CLASS0_POSTPHASE2_PASS\n"),
            failure.replace(
                "ACPHY_CLASS0_POSTPHASE2_FAIL stage=CHAIN1_0371 ret=-5\n",
                "ACPHY_CLASS0_POSTPHASE2_FAIL stage=CHAIN1_0371 ret=-5\n"
                "ACPHY_CLASS0_POSTPHASE2_RETURN\n"),
            failure.replace(
                "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n",
                "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n"
                "ACPHY_CLASS0_POSTPHASE2_CP2_0416 sample=0x0000 field=0x00\n"
                "ACPHY_CLASS0_POSTPHASE2_FAIL stage=READ_0416 ret=-5\n"
                "ACPHY_CLASS0_POSTPHASE2_STOP\n"),
            failure.replace("OB_ATTACH_CP0_ENTRY\n", ""),
            early_no_entry,
            duplicate_cp0,
            cp0_after_terminal,
        ):
            with self.subTest(text=invalid[-160:]):
                r = evaluate(mode, invalid)
                self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
                self.assertIn("ACPHY_POSTPHASE2_LOG_VALIDATION: INCOMPLETE",
                              r.stdout)
        self.assertIn("radio state preserved; no rmmod", r.stdout)
        self.assertNotIn("CLEAN PASS", r.stdout)
        self.assertNotIn("rmmod openbrcm", r.stdout)
        no_stop = failure.replace("ACPHY_CLASS0_POSTPHASE1_STOP\n", "")
        r = evaluate(mode, no_stop)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postbmac_mode_dry_run_preserves_state(self):
        mode = "acphy_class0_postbmac_phyreg1b0_branch_gate_test_only"
        help_result = run("--help")
        self.assertEqual(help_result.returncode, 0, help_result.stderr)
        self.assertIn(mode, help_result.stdout)
        r = run("--dry-run", "--mode", mode)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout, rf'(?m)^insmod .*"{mode}=1"$')
        self.assertIn("preserve openbrcm/radio state; no rmmod", r.stdout)

    def test_postbmac_accepts_only_the_selected_historical_branch(self):
        mode = "acphy_class0_postbmac_phyreg1b0_branch_gate_test_only"
        r = evaluate(mode, POSTBMAC_CLEAN)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ACPHY_POSTBMAC_LOG_VALIDATION: PASS_PATH_COMPLETE",
                      r.stdout)
        self.assertIn("radio state preserved; no rmmod", r.stdout)

        unselected = POSTBMAC_CLEAN.replace(
            "sample=0xcb01 low4=0x1 ja_gt1=0",
            "sample=0xcb02 low4=0x2 ja_gt1=1",
        )
        r = evaluate(mode, unselected)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postbmac_requires_one_valid_otp8e2_observation(self):
        mode = "acphy_class0_postbmac_phyreg1b0_branch_gate_test_only"
        original = "ACPHY_OTP8E2_OBS word=0x0600 value=0x06 jbe_le3=0\n"
        self.assertEqual(evaluate(mode, POSTBMAC_CLEAN).returncode, 0)
        for word, value, predicate in (
            ("0x0500", "0x05", "0"),
            ("0x0700", "0x07", "0"),
        ):
            text = POSTBMAC_CLEAN.replace(
                original,
                f"ACPHY_OTP8E2_OBS word={word} value={value} "
                f"jbe_le3={predicate}\n",
                1,
            )
            r = evaluate(mode, text)
            self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        malformed = [
            POSTBMAC_CLEAN.replace(original, "", 1),
            POSTBMAC_CLEAN.replace(original, original + original, 1),
            POSTBMAC_CLEAN.replace(original, "", 1).replace(
                "OB_ATTACH_CP0_ENTRY\n",
                "OB_ATTACH_CP0_ENTRY\n" + original,
                1,
            ),
            POSTBMAC_CLEAN.replace(
                original,
                "ACPHY_OTP8E2_OBS word=0x0300 value=0x04 jbe_le3=0\n",
                1,
            ),
            POSTBMAC_CLEAN.replace(
                original,
                "ACPHY_OTP8E2_OBS word=0x0000 value=0x00 jbe_le3=0\n",
                1,
            ),
            POSTBMAC_CLEAN.replace(
                original,
                "ACPHY_OTP8E2_OBS word=0x0000 value=0x20 jbe_le3=0\n",
                1,
            ),
            POSTBMAC_CLEAN.replace(
                original,
                "ACPHY_OTP8E2_FAIL stage=READ_WORD ret=-5\n" + original,
                1,
            ),
        ]
        for text in malformed:
            with self.subTest(text=text[:160]):
                r = evaluate(mode, text)
                self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postbmac_otp_acquisition_failure_is_terminal_before_attach(self):
        mode = "acphy_class0_postbmac_phyreg1b0_branch_gate_test_only"
        log = (
            "ACPHY_OTP8E2_FAIL stage=READ_WORD ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE1_FAIL stage=BUS_SI ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE1_STOP\n"
        )
        r = evaluate(mode, log)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertIn("ACPHY_POSTBMAC_LOG_VALIDATION: FAILURE_PATH_COMPLETE",
                      r.stdout)
        self.assertNotIn("ACPHY_CLASS0_POSTBMAC_CP0_ENTRY", r.stdout)

    def test_postbmac_rejects_malformed_success_chronology(self):
        mode = "acphy_class0_postbmac_phyreg1b0_branch_gate_test_only"
        mutations = [
            POSTBMAC_CLEAN.replace("ACPHY_CLASS0_POSTBMAC_CP1_PHY1B0_RMW_DONE\n", ""),
            POSTBMAC_CLEAN.replace(
                "ACPHY_CLASS0_POSTBMAC_CP0_ENTRY\n",
                "ACPHY_CLASS0_POSTBMAC_CP0_ENTRY\n"
                "ACPHY_CLASS0_POSTBMAC_CP0_ENTRY\n",
            ),
            POSTBMAC_CLEAN.replace(
                "ACPHY_CLASS0_POSTBMAC_CP1_PHY1B0_RMW_DONE\n"
                "ACPHY_CLASS0_POSTBMAC_CP2_PHY0_SAMPLE",
                "ACPHY_CLASS0_POSTBMAC_CP2_PHY0_SAMPLE",
            ),
            POSTBMAC_CLEAN.replace(
                "ACPHY_CLASS0_POSTBMAC_CP3_BRANCH_GATE\n",
                "",
            ),
            POSTBMAC_CLEAN.replace(
                "ACPHY_CLASS0_POSTBMAC_PASS\n",
                "ACPHY_CLASS0_POSTBMAC_PASS\n"
                "ACPHY_CLASS0_POSTBMAC_PASS\n",
            ),
            POSTBMAC_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE2_RETURN\n",
                "ACPHY_CLASS0_POSTPHASE2_RETURN\n"
                "ACPHY_CLASS0_POSTPHASE2_PASS\n"
                "ACPHY_CLASS0_POSTPHASE2_STOP\n",
            ),
            POSTBMAC_CLEAN.replace("ACPHY_CLASS0_POSTBMAC_STOP\n", ""),
            POSTBMAC_CLEAN + "ACPHY_CLASS0_POSTBMAC_CP4_AFTER_GATE\n",
            POSTBMAC_CLEAN.replace("ja_gt1=0", "ja_gt1=1"),
            POSTBMAC_CLEAN.replace(
                "write=0xa43abcde",
                "write=0xa53abcde",
            ),
            POSTBMAC_CLEAN.replace(
                "write=0x12340000",
                "write=0x12340001",
            ),
            POSTBMAC_CLEAN.replace(
                "readback=0x00000000\nACPHY_CLASS0_POSTBMAC_CP5",
                "ACPHY_CLASS0_POSTBMAC_CP5",
            ),
            POSTBMAC_CLEAN.replace(
                "ACPHY_CLASS0_POSTBMAC_PASS\n",
                "ACPHY_CLASS0_POSTBMAC_FAIL stage=OP2_DATA_READ ret=-5\n"
                "ACPHY_CLASS0_POSTBMAC_PASS\n",
            ),
            POSTBMAC_CLEAN.replace(
                "ACPHY_CLASS0_POSTBMAC_STOP\n",
                "ACPHY_CLASS0_POSTBMAC_STOP\n"
                "ACPHY_CLASS0_POSTBMAC_CP0_ENTRY\n",
            ),
            POSTBMAC_CLEAN.replace(
                "ACPHY_CLASS0_POSTPHASE1_POLL attempts=7 last_value=0x0010 success=1",
                "ACPHY_CLASS0_POSTPHASE1_POLL attempts=7 last_value=0x0000 success=0",
            ),
            POSTBMAC_CLEAN + POSTBMAC_CLEAN,
        ]
        for text in mutations:
            with self.subTest(text=text[-180:]):
                r = evaluate(mode, text)
                self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_postbmac_failures_are_terminal_and_legacy_phase2_failure_stays_valid(self):
        mode = "acphy_class0_postbmac_phyreg1b0_branch_gate_test_only"
        predecessor_failure = POSTPHASE1_CLEAN.replace(
            "ACPHY_CLASS0_POSTPHASE1_STOP\n",
            "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n"
            "ACPHY_CLASS0_POSTPHASE2_FAIL stage=DEVICE_LOST ret=-5\n"
            "ACPHY_CLASS0_POSTPHASE2_STOP\n",
        )
        predecessor_failure = predecessor_failure.replace(
            "OB_ATTACH_CP0_ENTRY\n",
            "ACPHY_OTP8E2_OBS word=0x0000 value=0x00 jbe_le3=1\n"
            "OB_ATTACH_CP0_ENTRY\n",
            1,
        )
        r = evaluate(mode, predecessor_failure)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertIn("ACPHY_POSTBMAC_LOG_VALIDATION: FAILURE_PATH_COMPLETE",
                      r.stdout)
        self.assertNotIn("ACPHY_CLASS0_POSTBMAC_CP0_ENTRY", r.stdout)

        suffix_failure = POSTBMAC_CLEAN.replace(
            "ACPHY_CLASS0_POSTBMAC_CP1_PHY1B0_RMW_DONE\n"
            "ACPHY_CLASS0_POSTBMAC_CP2_PHY0_SAMPLE sample=0xcb01 low4=0x1 ja_gt1=0\n"
            "ACPHY_CLASS0_POSTBMAC_CP3_BRANCH_GATE\n"
            "ACPHY_CLASS0_POSTBMAC_CP4_PMU_0658 old=0xdeadbeef write=0x00000000 readback=0x00000000\n"
            "ACPHY_CLASS0_POSTBMAC_CP5_PMU_065C old=0xa5fabcde write=0xa43abcde readback=0xa43abcde\n"
            "ACPHY_CLASS0_POSTBMAC_CP6_GPIO_006C old=0x12345678 write=0x12340000 readback=0x12340000\n"
            "ACPHY_CLASS0_POSTBMAC_PASS\n"
            "ACPHY_CLASS0_POSTBMAC_STOP\n",
            "ACPHY_CLASS0_POSTBMAC_FAIL stage=OP1_DATA_READ ret=-5\n"
            "ACPHY_CLASS0_POSTBMAC_STOP\n",
        )
        r = evaluate(mode, suffix_failure)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
        self.assertIn("ACPHY_POSTBMAC_LOG_VALIDATION: FAILURE_PATH_COMPLETE",
                      r.stdout)
        self.assertNotIn("ACPHY_CLASS0_POSTBMAC_PASS", r.stdout)

    def test_dry_run_prints_dependency_commands(self):
        r = run("--dry-run", "--mode", "dma_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        for dep in ("cfg80211", "mac80211", "bcma"):
            self.assertIn(f"modprobe {dep}", r.stdout)

    def test_dry_run_does_not_require_root(self):
        r = run("--dry-run", "--mode", "dma_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn("must run as root", r.stderr)
        self.assertNotIn("REBOOT REQUIRED", r.stdout)

    def test_mode_equals_form(self):
        r = run("--dry-run", "--mode=initvals_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('"initvals_test_only=1"', r.stdout)

    def test_positional_module_path(self):
        r = run("--dry-run", "--mode", "ucode_test_only", "/tmp/openbrcm-x.ko")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("/tmp/openbrcm-x.ko", r.stdout)

    def test_candidate_match_passes(self):
        r = run("--dry-run", "--mode", "ucode_test_only", "--candidate", head_rev())
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_candidate_mismatch_aborts(self):
        parent = subprocess.check_output(
            ["git", "-C", str(REPO), "rev-parse", "HEAD~1"], text=True
        ).strip()
        r = run(
            "--dry-run",
            "--mode",
            "ucode_test_only",
            "--candidate",
            parent,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("mismatch", r.stderr)

    def test_candidate_unresolvable_aborts(self):
        r = run("--dry-run", "--mode", "ucode_test_only", "--candidate", "not-a-rev")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("cannot resolve", r.stderr)

    def test_expected_sha256_is_checked_before_a_dry_run(self):
        with tempfile.NamedTemporaryFile("wb", delete=False) as fh:
            fh.write(b"candidate")
            path = fh.name
        digest = hashlib.sha256(b"candidate").hexdigest()
        r = run("--dry-run", "--mode", "ucode_test_only",
                "--expected-sha256", digest, path)
        self.assertEqual(r.returncode, 0, r.stderr)
        r = run("--dry-run", "--mode", "ucode_test_only",
                "--expected-sha256", "0" * 64, path)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("module SHA256 mismatch", r.stderr)

    # ---- radio_id_probe_only --------------------------------------------

    def test_radio_mode_accepted_by_cli(self):
        r = run("--help")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("radio_id_probe_only", r.stdout)

    # ---- acphy_pre_radio_test_only --------------------------------------

    def test_acphy_preradio_mode_accepted_by_cli(self):
        r = run("--help")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("acphy_pre_radio_test_only", r.stdout)

    def test_acphy_preradio_clean_log_permits_rmmod(self):
        r = evaluate("acphy_pre_radio_test_only", ACPHY_PRERADIO_CLEAN)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_acphy_preradio_missing_checkpoint_prohibits_rmmod(self):
        text = "".join(
            line for line in ACPHY_PRERADIO_CLEAN.splitlines(True)
            if "CP5_CFLAGS_OK" not in line
        )
        r = evaluate("acphy_pre_radio_test_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
        self.assertIn("INCOMPLETE", r.stdout)

    def test_acphy_preradio_failure_prohibits_rmmod(self):
        r = evaluate("acphy_pre_radio_test_only", ACPHY_PRERADIO_CLEAN +
                     "ACPHY_PRERADIO_HW_FAIL stage=transaction ret=-5\n")
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))
        self.assertIn("REBOOT REQUIRED", r.stdout)

    def test_acphy_preradio_markers_match_driver(self):
        src = (REPO / "src" / "ob_phy.c").read_text(encoding="utf-8")
        for marker in (
            "ACPHY_PRERADIO_HW_CP0_ENTRY",
            "ACPHY_PRERADIO_HW_CP1_IDENTITY_OK",
            "ACPHY_PRERADIO_HW_CP2_CHANSPEC_1001",
            "ACPHY_PRERADIO_HW_CP3_TRANSACTION_DONE",
            "ACPHY_PRERADIO_HW_CP4_SHM_OK",
            "ACPHY_PRERADIO_HW_CP5_CFLAGS_OK",
            "ACPHY_PRERADIO_HW_PASS",
            "ACPHY_PRERADIO_HW_FAIL stage=%s ret=%d",
        ):
            self.assertIn(marker, src)

    # ---- acphy_radio_postlpf_test_only ---------------------------------

    def test_acphy_postlpf_dry_run_sets_fixed_scope_and_preserves_state(self):
        mode = "acphy_radio_postlpf_test_only"
        r = run("--dry-run", "--mode", mode)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout, rf'(?m)^insmod .*"{mode}=1" '
                         r'"acphy_table_probe=0" "acphy_a7089_golden_cap=0"$')
        self.assertIn("preserve openbrcm/radio state; no rmmod", r.stdout)

    def test_acphy_postlpf_clean_log_requires_ordered_checkpoint(self):
        mode = "acphy_radio_postlpf_test_only"
        r = evaluate(mode, ACPHY_POSTLPF_CLEAN)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("CLEAN PASS (radio state preserved; no rmmod)", r.stdout)

        reordered = ACPHY_POSTLPF_CLEAN.replace(
            "ACPHY_A7089_GOLDEN_DONE ops=1168\nACPHY_INIT_DONE\n",
            "ACPHY_INIT_DONE\nACPHY_A7089_GOLDEN_DONE ops=1168\n")
        r = evaluate(mode, reordered)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        r = evaluate(mode, ACPHY_POSTLPF_CLEAN +
                     "ACPHY_POSTLPF_FAIL stage=prefix ret=-5\n")
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)

    # ---- acphy_cal_inputs_probe_only -----------------------------------

    def test_acphy_cal_inputs_dry_run_forces_radio_checkpoint_options(self):
        mode = "acphy_cal_inputs_probe_only"
        r = run("--dry-run", "--mode", mode)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertRegex(r.stdout, rf'(?m)^insmod .*"{mode}=1" '
                         r'"acphy_table_probe=0" "acphy_a7089_golden_cap=0"$')
        self.assertIn("preserve openbrcm/radio state; no rmmod", r.stdout)

    def test_acphy_cal_inputs_log_requires_unknown_input_stop_in_order(self):
        mode = "acphy_cal_inputs_probe_only"
        r = evaluate(mode, ACPHY_CAL_INPUTS_CLEAN)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("CLEAN STOP (calibration inputs unresolved; radio state preserved; no rmmod)", r.stdout)

        missing = ACPHY_CAL_INPUTS_CLEAN.replace(
            "ACPHY_CAL_INPUT channel_row=63 key=1 offset=0x1c8c source=controlled_fixture\n", "")
        r = evaluate(mode, missing)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        reordered = ACPHY_CAL_INPUTS_CLEAN.replace(
            "ACPHY_CAL_CONTRACT_BLOCKED first=4 unknown_mask=0x01\n"
            "ACPHY_CAL_CONTRACT_STOP\n",
            "ACPHY_CAL_CONTRACT_STOP\n"
            "ACPHY_CAL_CONTRACT_BLOCKED first=4 unknown_mask=0x01\n")
        r = evaluate(mode, reordered)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        false_progress = ACPHY_CAL_INPUTS_CLEAN + "ACPHY_CAL_STAGE_COMPLETE\n"
        r = evaluate(mode, false_progress)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    # ---- acphy_cal_stage1_test_only (M1-3 one-boot alias) ---------------

    def test_acphy_cal_stage1_alias_is_one_insmod_with_stage_flag(self):
        r = run("--dry-run", "--mode", "acphy_cal_stage1_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        insmods = [ln for ln in r.stdout.splitlines()
                   if ln.lstrip().startswith("insmod")]
        self.assertEqual(len(insmods), 1, r.stdout)
        self.assertIn('"acphy_cal_inputs_probe_only=1"', insmods[0])
        self.assertIn('"acphy_cal_stage_witness=1"', insmods[0])
        self.assertIn("acphy_cal_stage1_test_only (expands to "
                      "acphy_cal_inputs_probe_only + acphy_cal_stage_witness=1)",
                      r.stdout)

    def test_acphy_cal_stage1_log_requires_admitted_stage_evidence(self):
        mode = "acphy_cal_stage1_test_only"
        stage = (
            "ACPHY_CAL_STAGE_BEGIN contract=ADMITTED periodic_tempdelta_known=0\n"
            "ACPHY_CAL_STAGE_PRE_STATE phy_0401=0x0000 phy_0400=0x0000 "
            "phy_0070=0x0000\n"
            "ACPHY_CAL_STAGE_READY last=0x0000 reads=1 delays=0 exhausted=0\n"
            "ACPHY_CAL_STAGE_RESULT status=WITNESS_OK\n"
            "ACPHY_CAL_STAGE_CONTEXT shared_a5=0x03 "
            "source=accepted_chain_state\n"
            "ACPHY_CAL_STAGE_STOP calibration_issued=0 entry_context_owned=0\n"
        )
        admitted = ACPHY_CAL_INPUTS_CLEAN.replace(
            "ACPHY_CAL_CONTRACT_BLOCKED first=4 unknown_mask=0x01",
            "ACPHY_CAL_CONTRACT_ADMITTED periodic_tempdelta_known=0")

        r = evaluate(mode, admitted + stage)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("CLEAN STOP", r.stdout)

        # The same stage lines cannot rescue a blocked contract.
        r = evaluate(mode, ACPHY_CAL_INPUTS_CLEAN + stage)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        # Admission without stage evidence is not M1-3 evidence.
        r = evaluate(mode, admitted)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        # A failed, blocked or "complete" stage is never a PASS.
        for bad in (
            "ACPHY_CAL_STAGE_FAIL status=FAILED_CALLBACK site=0x097d30 ret=-5\n",
            "ACPHY_CAL_STAGE_BLOCKED contract_ret=-2 first=4 unknown_mask=0x01\n",
            "ACPHY_CAL_STAGE_COMPLETE\n",
        ):
            r = evaluate(mode, admitted + stage + bad)
            self.assertEqual(r.returncode, 4, (bad, r.stdout + r.stderr))

        # Ordering is part of the contract: RESULT before READY must fail.
        reordered = stage.replace(
            "ACPHY_CAL_STAGE_READY last=0x0000 reads=1 delays=0 exhausted=0\n"
            "ACPHY_CAL_STAGE_RESULT status=WITNESS_OK\n",
            "ACPHY_CAL_STAGE_RESULT status=WITNESS_OK\n"
            "ACPHY_CAL_STAGE_READY last=0x0000 reads=1 delays=0 exhausted=0\n")
        r = evaluate(mode, admitted + reordered)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    # ---- acphy_cal_write1_test_only (M1-4 one-boot alias) --------------

    def test_acphy_cal_write1_alias_is_one_insmod_with_write_flag(self):
        r = run("--dry-run", "--mode", "acphy_cal_write1_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        insmods = [ln for ln in r.stdout.splitlines()
                   if ln.lstrip().startswith("insmod")]
        self.assertEqual(len(insmods), 1, r.stdout)
        self.assertIn('"acphy_cal_inputs_probe_only=1"', insmods[0])
        self.assertIn('"acphy_cal_write_stage=1"', insmods[0])
        self.assertNotIn("acphy_cal_stage_witness=1", insmods[0])
        self.assertIn("acphy_cal_write1_test_only (expands to "
                      "acphy_cal_inputs_probe_only + acphy_cal_write_stage=1)",
                      r.stdout)

    def test_acphy_cal_write1_log_requires_ordered_arm_and_clean_post_state(self):
        mode = "acphy_cal_write1_test_only"
        write = (
            "ACPHY_CAL_WRITE_BEGIN contract=ADMITTED periodic_tempdelta_known=0\n"
            "ACPHY_CAL_WRITE_PRE_STATE phy_0401=0x7777 phy_0400=0x0000 "
            "phy_0070=0x0100 ready_last=0x0000 ready_reads=1 ready_delays=0\n"
            "ACPHY_CAL_WRITE_ARM index=1 read=0x0100 predicate=0 value=0x0100 "
            "site=0x09071a pi_fa0=0 site_pi_fa0=0x090704\n"
            "ACPHY_CAL_WRITE_RMW index=1 wrote=0x0100 readback=0x0100 "
            "site=0x090760\n"
            "ACPHY_CAL_WRITE_STABLE index=1 delay_us=1000 value=0x0100\n"
            "ACPHY_CAL_WRITE_ARM index=2 read=0x0100 predicate=0 value=0x0100 "
            "site=0x09071a pi_fa0=0 site_pi_fa0=0x090704\n"
            "ACPHY_CAL_WRITE_RMW index=2 wrote=0x0100 readback=0x0100 "
            "site=0x090760\n"
            "ACPHY_CAL_WRITE_POST_STATE phy_0401=0x7777 phy_0400=0x0000 "
            "phy_0070=0x0100 phy_0380=0x0000\n"
            "ACPHY_CAL_WRITE_RESULT status=ARM_OK writes=2\n"
            "ACPHY_CAL_WRITE_STOP writes=2 calibration_issued=0 "
            "entry_context_owned=0\n"
        )
        admitted = ACPHY_CAL_INPUTS_CLEAN.replace(
            "ACPHY_CAL_CONTRACT_BLOCKED first=4 unknown_mask=0x01",
            "ACPHY_CAL_CONTRACT_ADMITTED periodic_tempdelta_known=0")

        r = evaluate(mode, admitted + write)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("write arm executed", r.stdout)

        # A blocked contract cannot be rescued by write markers.
        r = evaluate(mode, ACPHY_CAL_INPUTS_CLEAN + write)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        # Admission without the arm is not M1-4 evidence.
        r = evaluate(mode, admitted)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        # Missing, reordered or reduced evidence is never a PASS.
        for bad in (
            write.replace("ACPHY_CAL_WRITE_ARM index=2 read=0x0100",
                          "ACPHY_CAL_WRITE_ARM index=3 read=0x0100"),
            write.replace("ACPHY_CAL_WRITE_STABLE index=1 delay_us=1000 value=0x0100\n"
                          "ACPHY_CAL_WRITE_ARM index=2",
                          "ACPHY_CAL_WRITE_ARM index=2")
                 .replace("ACPHY_CAL_WRITE_RMW index=2 wrote=0x0100 readback=0x0100 "
                          "site=0x090760\n",
                          "ACPHY_CAL_WRITE_RMW index=2 wrote=0x0100 readback=0x0100 "
                          "site=0x090760\n"
                          "ACPHY_CAL_WRITE_STABLE index=1 delay_us=1000 value=0x0100\n"),
            write.replace("ACPHY_CAL_WRITE_STOP writes=2",
                          "ACPHY_CAL_WRITE_STOP writes=1"),
            write.replace("phy_0070=0x0100 phy_0380=0x0000",
                          "phy_0070=0x0300 phy_0380=0x0000"),
            write + "ACPHY_CAL_WRITE_REFUSED reason=prestate_diverged writes=0\n",
            write + "ACPHY_CAL_WRITE_FAIL status=FAILED_CALLBACK site=0x090760 ret=-5\n",
            write + "ACPHY_CAL_WRITE_BLOCKED contract_ret=-2 first=4 unknown_mask=0x01\n",
            write.replace("entry_context_owned=0", "entry_context_owned=1"),
        ):
            r = evaluate(mode, admitted + bad)
            self.assertEqual(r.returncode, 4, (bad, r.stdout + r.stderr))

    # ---- acphy_cal_write2_test_only (M1-6 one-boot alias) --------------

    def test_acphy_cal_write2_alias_is_one_insmod_with_write_flag(self):
        r = run("--dry-run", "--mode", "acphy_cal_write2_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        insmods = [ln for ln in r.stdout.splitlines()
                   if ln.lstrip().startswith("insmod")]
        self.assertEqual(len(insmods), 1, r.stdout)
        self.assertIn('"acphy_cal_inputs_probe_only=1"', insmods[0])
        self.assertIn('"acphy_cal_write_stage=1"', insmods[0])
        self.assertIn('"acphy_table_probe=0"', insmods[0])
        self.assertIn('"acphy_a7089_golden_cap=0"', insmods[0])
        self.assertNotIn("acphy_cal_stage_witness=1", insmods[0])
        self.assertIn("acphy_cal_write2_test_only (expands to "
                      "acphy_cal_inputs_probe_only + acphy_cal_write_stage=1)",
                      r.stdout)

    def test_acphy_cal_write2_log_requires_bracket_and_save_restore(self):
        mode = "acphy_cal_write2_test_only"
        write = (
            "ACPHY_CAL_WRITE_BEGIN contract=ADMITTED periodic_tempdelta_known=0\n"
            "ACPHY_CAL_WRITE_PRE_STATE phy_0401=0x7777 phy_0400=0x0000 "
            "phy_0070=0x0100 ready_last=0x0000 ready_reads=1 ready_delays=0\n"
            "ACPHY_CAL_WRITE_ENABLE_STATE value=0 source=ob_hw_shadow "
            "restore_supported=1\n"
            "ACPHY_CAL_WRITE_MAC_OPEN state=already_quiesced "
            "maccontrol=0x04020402 resume_on_close=0\n"
            "ACPHY_CAL_WRITE_BRACKET completed=1 mac_left_suspended=0 "
            "phyreg_left_entered=0\n"
            "ACPHY_CAL_WRITE_ROLE index=1 role=disable "
            "gate=vendor_save_disable_restore\n"
            "ACPHY_CAL_WRITE_ARM index=1 read=0x0100 predicate=0 value=0x0100 "
            "site=0x09071a pi_fa0=0 site_pi_fa0=0x090704\n"
            "ACPHY_CAL_WRITE_RMW index=1 wrote=0x0100 readback=0x0100 "
            "site=0x090760\n"
            "ACPHY_CAL_WRITE_STABLE index=1 delay_us=1000 value=0x0100\n"
            "ACPHY_CAL_WRITE_ROLE index=2 role=restore "
            "gate=vendor_save_disable_restore\n"
            "ACPHY_CAL_WRITE_ARM index=2 read=0x0100 predicate=0 value=0x0100 "
            "site=0x0b184f pi_fa0=0 site_pi_fa0=0x090704\n"
            "ACPHY_CAL_WRITE_RMW index=2 wrote=0x0100 readback=0x0100 "
            "site=0x090760\n"
            "ACPHY_CAL_WRITE_POST_STATE phy_0401=0x7777 phy_0400=0x0000 "
            "phy_0070=0x0100 phy_0380=0x0000\n"
            "ACPHY_CAL_WRITE_RESULT status=ARM_OK writes=2\n"
            "ACPHY_CAL_WRITE_STOP writes=2 calibration_issued=0 "
            "entry_context_owned=0\n"
        )
        admitted = ACPHY_CAL_INPUTS_CLEAN.replace(
            "ACPHY_CAL_CONTRACT_BLOCKED first=4 unknown_mask=0x01",
            "ACPHY_CAL_CONTRACT_ADMITTED periodic_tempdelta_known=0")

        r = evaluate(mode, admitted + write)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("write arm executed", r.stdout)

        # A blocked contract cannot be rescued by bracket/write markers.
        r = evaluate(mode, ACPHY_CAL_INPUTS_CLEAN + write)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        # Admission without the bracket/arm evidence is not M1-6 evidence.
        r = evaluate(mode, admitted)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

        # The saved byte, the bracket and the role order are part of the
        # contract; so is STABLE between the two arms, not after them.
        for bad in (
            write.replace("ACPHY_CAL_WRITE_ENABLE_STATE value=0",
                          "ACPHY_CAL_WRITE_ENABLE_STATE value=1"),
            write.replace("restore_supported=1", "restore_supported=0"),
            write.replace("ACPHY_CAL_WRITE_BRACKET completed=1",
                          "ACPHY_CAL_WRITE_BRACKET completed=0"),
            write.replace("mac_left_suspended=0", "mac_left_suspended=1"),
            write.replace("phyreg_left_entered=0", "phyreg_left_entered=1"),
            write.replace("ROLE index=1 role=disable", "ROLE index=1 role=restore"),
            write.replace("ACPHY_CAL_WRITE_STABLE index=1 delay_us=1000 value=0x0100\n",
                          "")
                 .replace("ACPHY_CAL_WRITE_RESULT status=ARM_OK",
                          "ACPHY_CAL_WRITE_STABLE index=1 delay_us=1000 "
                          "value=0x0100\nACPHY_CAL_WRITE_RESULT status=ARM_OK"),
            write.replace("ACPHY_CAL_WRITE_STOP writes=2",
                          "ACPHY_CAL_WRITE_STOP writes=1"),
            write.replace("phy_0070=0x0100 phy_0380=0x0000",
                          "phy_0070=0x0300 phy_0380=0x0000"),
            write + "ACPHY_CAL_WRITE_REFUSED reason=restore_unsupported_enable_state_nonzero writes=0\n",
            write + "ACPHY_CAL_WRITE_FAIL status=BRACKET_FAILED site=0x0925fe ret=-22\n",
            write + "ACPHY_CAL_WRITE_BLOCKED contract_ret=-2 first=4 unknown_mask=0x01\n",
            write.replace("entry_context_owned=0", "entry_context_owned=1"),
        ):
            r = evaluate(mode, admitted + bad)
            self.assertEqual(r.returncode, 4, (bad, r.stdout + r.stderr))

    def test_d11_core_state_extraction_is_read_only(self):
        r = evaluate("acphy_pre_radio_test_only", D11_CORE_STATE_CAPTURE)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        for marker in (
            "D11_CP0_IOCTL: 0x00000000",
            "D11_CP0_RESET_CTL: 0x00000001",
            "D11_CP0_ENABLED: 0",
            "D11_CP3_IOCTL: 0x00000001",
            "D11_CP3_RESET_CTL: 0x00000001",
            "D11_CP3_ENABLED: 0",
            "CORE_ENABLE_CALLED: 1",
            "D11_CORE_POSTCONDITION: RESET_ASSERTED",
            "D11_0X01E0: 0xffffffff",
        ):
            self.assertIn(marker, r.stdout)

    def test_radio2069_rev4_mode_emits_one_isolated_insmod(self):
        r = run("--dry-run", "--mode", "radio2069_rev4_probe_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        insmods = [ln for ln in r.stdout.splitlines()
                   if ln.lstrip().startswith("insmod")]
        self.assertEqual(len(insmods), 1, r.stdout)
        self.assertIn("radio2069_rev4_probe_only=1", insmods[0])

    def test_radio_dry_run_emits_exactly_one_isolated_insmod(self):
        r = run("--dry-run", "--mode", "radio_id_probe_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        insmods = [
            ln for ln in r.stdout.splitlines()
            if ln.lstrip().startswith("insmod")
        ]
        self.assertEqual(len(insmods), 1, r.stdout)
        self.assertIn("radio_id_probe_only=1", insmods[0])

    def test_radio_candidate_mismatch_aborts_before_hardware(self):
        parent = subprocess.check_output(
            ["git", "-C", str(REPO), "rev-parse", "HEAD~1"], text=True
        ).strip()
        r = run("--dry-run", "--mode", "radio_id_probe_only",
                "--candidate", parent)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("mismatch", r.stderr)

    def test_radio_success_markers_permit_rmmod(self):
        r = evaluate("radio_id_probe_only", RADIO_CLEAN)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_radio_missing_marker_prohibits_rmmod(self):
        text = "".join(
            ln for ln in RADIO_CLEAN.splitlines(True)
            if "post_access=" not in ln
        )
        r = evaluate("radio_id_probe_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
        self.assertIn("INCOMPLETE", r.stdout)

    def test_radio_dev_lost_prohibits_rmmod(self):
        text = RADIO_CLEAN + (
            "openbrcm: DEVICE LOST (radio-probe) - reboot required\n"
        )
        r = evaluate("radio_id_probe_only", text)
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))
        self.assertIn("REBOOT REQUIRED", r.stdout)

    def test_radio_current_run_fail_line_detected(self):
        text = RADIO_CLEAN.replace(
            "radio-probe: PASS\n",
            "radio-probe: FAIL unexpected radioid=0x1234 (AC accepts 0x2069/0x030b)\n",
        )
        r = evaluate("radio_id_probe_only", text)
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))

    def test_radio2069_requires_readable_fixed_window_baseline(self):
        r = evaluate("radio2069_rev4_probe_only", RADIO2069_CLEAN)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("FIXED_WINDOW_BASELINE: READABLE", r.stdout)
        self.assertIn("FABRIC_BASELINE_ACCEPTED: YES", r.stdout)

    def test_radio2069_rejects_missing_or_bad_fixed_window_baseline(self):
        for text, classification in (
            (RADIO2069_CLEAN.replace(
                "FIXED_WINDOW_BASELINE=READABLE FABRIC_BASELINE_ACCEPTED=YES\n",
                ""), "NOT_REACHED"),
            (RADIO2069_CLEAN.replace(
                "READABLE FABRIC_BASELINE_ACCEPTED=YES",
                "ALL_ONES FABRIC_BASELINE_ACCEPTED=NO"), "ALL_ONES"),
            (RADIO2069_CLEAN.replace(
                "READABLE FABRIC_BASELINE_ACCEPTED=YES",
                "INDETERMINATE FABRIC_BASELINE_ACCEPTED=NO"), "INDETERMINATE"),
        ):
            with self.subTest(classification=classification):
                r = evaluate("radio2069_rev4_probe_only", text)
                self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
                self.assertIn(f"FIXED_WINDOW_BASELINE: {classification}", r.stdout)
                self.assertIn("INCOMPLETE", r.stdout)

    def test_stale_fault_above_marker_ignored(self):
        full = (
            "historical noise\n"
            "BUG: stale crash from a previous boot\n"
            "Call Trace:\n"
            "WARNING: stale warning above the marker\n"
            "[openbrcm-test] marker TESTMARK\n"
            + RADIO_CLEAN
        )
        r = evaluate("radio_id_probe_only", full, marker="TESTMARK")
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_current_run_fatal_below_marker_detected(self):
        full = (
            "[openbrcm-test] marker TESTMARK\n"
            "radio-probe: BEGIN\n"
            "radio-probe: pre_access=0xffffffff\n"
            "openbrcm: DEVICE LOST (radio-probe) - reboot required\n"
        )
        r = evaluate("radio_id_probe_only", full, marker="TESTMARK")
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))

    def test_marker_absent_aborts(self):
        r = evaluate("radio_id_probe_only", RADIO_CLEAN, marker="NOPE")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("marker not found", r.stderr)

    # ---- rev42_ai_probe_only --------------------------------------------

    def test_ai_mode_accepted_by_cli(self):
        r = run("--help")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("rev42_ai_probe_only", r.stdout)

    def test_ai_dry_run_emits_exactly_one_isolated_insmod(self):
        r = run("--dry-run", "--mode", "rev42_ai_probe_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        insmods = [
            ln for ln in r.stdout.splitlines()
            if ln.lstrip().startswith("insmod")
        ]
        self.assertEqual(len(insmods), 1, r.stdout)
        self.assertIn("rev42_ai_probe_only=1", insmods[0])

    def test_ai_candidate_mismatch_aborts_before_hardware(self):
        parent = subprocess.check_output(
            ["git", "-C", str(REPO), "rev-parse", "HEAD~1"], text=True
        ).strip()
        r = run("--dry-run", "--mode", "rev42_ai_probe_only",
                "--candidate", parent)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("mismatch", r.stderr)

    def test_ai_clean_log_permits_rmmod(self):
        r = evaluate("rev42_ai_probe_only", AI_CLEAN)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_ai_all_ones_is_still_a_clean_pass(self):
        # All-ones is an observation, never a reachability verdict: it must not
        # fail the run, and it must not be read as "unreachable".
        r = evaluate("rev42_ai_probe_only", AI_ALL_ONES)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_ai_cfg_readback_failure_is_still_a_clean_pass(self):
        # A failed config read is an OBSERVATION, not a fault: the run still
        # completed cleanly, so rmmod stays permitted. It must not be escalated
        # into a run failure -- it just makes the coherency verdict UNKNOWN.
        r = evaluate("rev42_ai_probe_only", AI_CFG_READ_FAIL)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_ai_cfg_readback_failure_is_not_reported_as_incoherent(self):
        # The regression this pins: a failed read used to be printed as
        # cfg(0x80)=0xffffffff ... coherent=0, which reads as "the BCMA window
        # was not selected". The sentinel must not be presented as register
        # content, and the verdict must be unknown rather than 0.
        self.assertIn("coherent=unknown", AI_CFG_READ_FAIL)
        self.assertNotIn("coherent=0", AI_CFG_READ_FAIL)
        self.assertNotIn("cfg(0x80)=0xffffffff", AI_CFG_READ_FAIL)
        self.assertNotIn("cfg(0x70)=0xffffffff", AI_CFG_READ_FAIL)
        self.assertIn("status=error", AI_CFG_READ_FAIL)
        # The evidence must still carry the BCMA-owned windows, so a reader can
        # see that the MMIO windows were never in question.
        self.assertIn("core->addr=0x18001000", AI_CFG_READ_FAIL)
        self.assertIn("core->wrap=0x18101000", AI_CFG_READ_FAIL)

    def test_ai_missing_cfg_readback_status_prohibits_rmmod(self):
        # The status line is required evidence. A log without it cannot be
        # distinguished from the pre-fix behaviour, so it must be INCOMPLETE
        # (verdict 4) rather than synthesised into a PASS.
        text = "".join(
            ln for ln in AI_CLEAN.splitlines(True)
            if "cfg-readback" not in ln
        )
        r = evaluate("rev42_ai_probe_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
        self.assertIn("INCOMPLETE", r.stdout)

    def test_ai_harness_accepts_coherent_unknown_token(self):
        # The harness greps the bare token "coherent="; the three-state verdict
        # must therefore still satisfy it, or every failed-read run would be
        # reported INCOMPLETE.
        r = evaluate("rev42_ai_probe_only", AI_CFG_READ_FAIL)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertNotIn("INCOMPLETE", r.stdout)

    # ---- rev42_ai_enable_only -------------------------------------------
    #
    # The first mode that writes registers, so the marker set doubles as
    # containment evidence: every write accounted for, in bcma_core_enable()
    # order, and the single D11 read present or explicitly skipped.

    def test_ai_en_mode_accepted_by_cli(self):
        r = run("--help")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("rev42_ai_enable_only", r.stdout)

    def test_ai_en_clean_log_retains_the_device(self):
        # Even a fully validated core-up is NOT a teardown: there is no removal
        # contract for a post-write core state, so the harness must forbid the
        # rmmod that Phase 37A's observation-only mode allowed.
        r = evaluate("rev42_ai_enable_only", AI_EN_CLEAN)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("post-transition state retained; no rmmod", r.stdout)
        self.assertNotIn("rmmod permitted", r.stdout)

    def test_ai_en_abort_is_still_a_clean_pass(self):
        # The fail-closed branch issues no write and latches nothing, so it is
        # a complete clean observation, not a fault.
        r = evaluate("rev42_ai_enable_only", AI_EN_PRECHECK)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        # Even a write-free rejection keeps the operator out of rmmod: the
        # Phase 37B live contract never removes the module.
        self.assertIn("no rmmod", r.stdout)
        # And it must not contain any of the three enable writes.
        for marker in ("step=E1_IOCTL_SET_FGC", "step=E2_RESET_CTL_DEASSERT",
                       "step=E3_IOCTL_CLR_FGC", "BCMA_AWRITE32"):
            self.assertNotIn(marker, AI_EN_PRECHECK)

    def test_ai_en_requires_every_wrapper_write_marker(self):
        # Dropping any one of the three writes must make the run INCOMPLETE.
        # A partially-executed enable sequence is the dangerous case: the core
        # would be left clocked but unreset, or reset-released but still gated.
        for marker in ("step=E1_IOCTL_SET_FGC", "step=E2_RESET_CTL_DEASSERT",
                       "step=E3_IOCTL_CLR_FGC"):
            with self.subTest(marker=marker):
                text = "".join(
                    ln for ln in AI_EN_CLEAN.splitlines(True)
                    if marker not in ln
                )
                r = evaluate("rev42_ai_enable_only", text)
                self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
                self.assertIn("INCOMPLETE", r.stdout)

    def test_ai_en_requires_the_retention_policy_to_be_accounted_for(self):
        # A post-write state must be reported as retained. A log that shows the
        # transition but claims a clean removal is a contradiction.
        text = "".join(
            ln for ln in AI_EN_CLEAN.splitlines(True)
            if "remove-policy=" not in ln
        )
        r = evaluate("rev42_ai_enable_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
        self.assertIn("INCOMPLETE", r.stdout)

    def test_ai_en_abort_log_must_contain_no_write(self):
        # The containment check: a fail-closed ABORT that nonetheless shows an
        # enable write is a contradiction and must never be read as a pass.
        # The core would be left clocked-but-unreset or reset-released-but-
        # gated, which is exactly the half-applied state to avoid.
        for marker in ("step=E1_IOCTL_SET_FGC", "step=E2_RESET_CTL_DEASSERT",
                       "step=E3_IOCTL_CLR_FGC", "op=BCMA_AWRITE32"):
            with self.subTest(marker=marker):
                text = AI_EN_PRECHECK + f"rev42-ai-en: {marker} wrote=0x00000003 readback=0x00000003\n"
                r = evaluate("rev42_ai_enable_only", text)
                self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
                self.assertIn("INCOMPLETE", r.stdout)

    def test_ai_en_rejects_any_d11_functional_core_access(self):
        # Phase 37B removed the 0x0FFC read. A run that reports a functional
        # core access -- a core->addr window or any d11-first-read -- is a
        # contract violation even when every write validated.
        for injected in (
            "rev42-ai-en: d11-first-read off=0x0ffc value=0x0000ffff readable=1\n",
            "PRE  seq=7 phase=ai_core_enable op=BCMA_READ32 off=0x0ffc window=core->addr\n",
        ):
            with self.subTest(injected=injected.strip()):
                r = evaluate("rev42_ai_enable_only", AI_EN_CLEAN + injected)
                self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
                self.assertIn("INCOMPLETE", r.stdout)

    def test_ai_en_requires_iscoreup_recheck_and_classification(self):
        for marker in ("ai_iscoreup=", "classification "):
            with self.subTest(marker=marker):
                text = "".join(
                    ln for ln in AI_EN_CLEAN.splitlines(True)
                    if marker not in ln
                )
                r = evaluate("rev42_ai_enable_only", text)
                self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))

    def test_ai_en_missing_end_marker_prohibits_rmmod(self):
        text = "".join(
            ln for ln in AI_EN_CLEAN.splitlines(True)
            if "END rev42_ai_enable_only " not in ln
        )
        r = evaluate("rev42_ai_enable_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
        self.assertIn("INCOMPLETE", r.stdout)

    def test_ai_en_latched_state_prohibits_rmmod(self):
        text = AI_EN_CLEAN.replace(
            "rev42-ai-en: fault-latch=0", "rev42-ai-en: fault-latch=1"
        )
        r = evaluate("rev42_ai_enable_only", text)
        self.assertNotEqual(r.returncode, 0, (r.stdout, r.stderr))

    def test_ai_en_markers_match_driver_format_strings(self):
        # The harness must not require a marker the driver does not emit.
        src = (REPO / "src" / "ob_diag.c").read_text(encoding="utf-8")
        for marker in [
            "rev42-ai-en: BEGIN",
            "window=core->wrap",
            "E1_IOCTL_SET_FGC",
            "E2_RESET_CTL_DEASSERT",
            "E3_IOCTL_CLR_FGC",
            "ai_iscoreup=%d",
            "rev42-ai-en: counters reads_attempted=%u",
            "rev42-ai-en: remove-policy=%s",
            "rev42-ai-en: classification %s",
            "rev42-ai-en: fault-latch=%d",
            "rev42-ai-en: END rev42_ai_enable_only ",
        ]:
            self.assertIn(marker, src,
                          f"harness requires a marker the driver lacks: {marker}")

    def test_ai_en_driver_lines_do_not_trip_fault_signature(self):
        # A clean or aborted run must never be misread as a fault.
        script = (REPO / "scripts" / "runtime-test.sh").read_text(encoding="utf-8")
        m = re.search(r"FAULT_RE='([^']+)'", script)
        self.assertIsNotNone(m, "FAULT_RE not found in runtime-test.sh")
        fault_re = m.group(1)
        for text in (AI_EN_CLEAN, AI_EN_PRECHECK):
            for line in text.splitlines():
                if not line.startswith("rev42-ai-en:"):
                    continue
                self.assertIsNone(
                    re.search(fault_re, line, re.IGNORECASE),
                    f"clean/aborted line would trip the fault signature: {line}",
                )

    def test_benign_dev_lost_field_never_trips_the_fault_signature(self):
        # acphy_mmio_trace01 REPORT.md section 17: every ACPHY identity/witness
        # probe marker prints `dev_lost=0` regardless of state. The former bare
        # `dev_lost` alternative classified those clean probe runs as faults
        # (rc=3 listing benign lines). The matcher must require an asserted
        # latch, and must still catch one.
        script = (REPO / "scripts" / "runtime-test.sh").read_text(encoding="utf-8")
        m = re.search(r"FAULT_RE='([^']+)'", script)
        self.assertIsNotNone(m, "FAULT_RE not found in runtime-test.sh")
        fault_re = m.group(1)
        benign = [
            "ACPHY_IDENTITY_WIDE: offset=0x0800 width=16 value=0xffff"
            " dev_lost=0 duration_us=2 stalled=0",
            "ACPHY_IDENTITY_TIMING: phyver=0xffff dev_lost=0"
            " read_before_radio_prepare=1",
            "ACPHY_IDENTITY_CONTROL: offset=0x0120 value=0x40000000 dev_lost=0"
            " duration_us=3 stalled=0",
            "D3B_BOUNDARY_WITNESS: stage=S3 window=d11 offset=0x03e0 width=16"
            " value=0xffff dev_lost=0 start_ns=1 end_ns=201727000 duration_us=201727"
            " class=TIMEOUT",
        ]
        for line in benign:
            self.assertIsNone(
                re.search(fault_re, line, re.IGNORECASE),
                f"benign dev_lost=0 line trips the fault signature: {line}",
            )
        for latched in (
            "ACPHY_IDENTITY_WITNESS: stage=S3 offset=0x03da window=d11 width=16"
            " guarded=1 value=0xffffffff dev_lost=1 start_ns=1 end_ns=2"
            " duration_us=201748 class=TIMEOUT",
            "rev42-sb: refusing: dev_lost latched",
            "openbrcm: DEVICE LOST (radio-probe) - reboot required",
        ):
            self.assertIsNotNone(
                re.search(fault_re, latched, re.IGNORECASE),
                f"latched device-loss line is not a fault signature: {latched}",
            )

    def test_ai_missing_end_marker_prohibits_rmmod(self):
        text = "".join(
            ln for ln in AI_CLEAN.splitlines(True)
            if "END rev42_ai_probe_only COMPLETE" not in ln
        )
        r = evaluate("rev42_ai_probe_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
        self.assertIn("INCOMPLETE", r.stdout)

    def test_ai_missing_fault_latch_state_prohibits_rmmod(self):
        text = "".join(
            ln for ln in AI_CLEAN.splitlines(True)
            if "fault-latch=" not in ln
        )
        r = evaluate("rev42_ai_probe_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))

    def test_ai_missing_coherency_evidence_prohibits_rmmod(self):
        # Incomplete address/window evidence must never be synthesised into a
        # PASS; D11-ADDR-U1 has to stay UNKNOWN instead.
        text = "".join(
            ln for ln in AI_CLEAN.splitlines(True)
            if "coherent=" not in ln
        )
        r = evaluate("rev42_ai_probe_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))

    def test_ai_missing_wrapper_window_read_prohibits_rmmod(self):
        text = "".join(
            ln for ln in AI_CLEAN.splitlines(True)
            if "window=core->wrap" not in ln
        )
        r = evaluate("rev42_ai_probe_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))

    def test_ai_missing_cfg_readback_prohibits_rmmod(self):
        text = "".join(
            ln for ln in AI_CLEAN.splitlines(True)
            if "PCI_CFG_READ" not in ln
        )
        r = evaluate("rev42_ai_probe_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))

    def test_ai_latched_state_is_not_a_clean_pass(self):
        # fault-latch=1 alone must not satisfy the completeness gate.
        text = AI_CLEAN.replace("fault-latch=0", "fault-latch=1")
        r = evaluate("rev42_ai_probe_only", text)
        self.assertNotEqual(r.returncode, 0)
        self.assertNotIn("CLEAN PASS", r.stdout)

    def test_ai_device_lost_demands_reboot(self):
        text = AI_CLEAN + (
            "openbrcm: rev42-ai FATAL/dev-lost; device kept bound, reboot required\n"
        )
        r = evaluate("rev42_ai_probe_only", text)
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))
        self.assertIn("REBOOT REQUIRED", r.stdout)

    def test_ai_skipped_latched_entry_demands_reboot(self):
        text = (
            "rev42-ai: SKIPPED (dev_lost latched; no D11/BCMA/window access)\n"
            + AI_CLEAN
        )
        r = evaluate("rev42_ai_probe_only", text)
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))

    def test_ai_stale_fault_above_marker_ignored(self):
        full = (
            "historical noise\n"
            "BUG: stale crash from a previous boot\n"
            "WARNING: stale warning above the marker\n"
            "[openbrcm-test] marker AIMARK\n"
            + AI_CLEAN
        )
        r = evaluate("rev42_ai_probe_only", full, marker="AIMARK")
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_ai_current_run_fault_below_marker_detected(self):
        full = (
            "[openbrcm-test] marker AIMARK\n"
            "rev42-ai: BEGIN (BCMA-owned addressing; core->addr=0x18001000"
            " core->wrap=0x18101000; no config write, no iomap)\n"
            "POST seq=1 result=ok readback=0xffffffff\n"
            "Call Trace:\n"
        )
        r = evaluate("rev42_ai_probe_only", full, marker="AIMARK")
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))

    def test_ai_marker_absent_aborts(self):
        r = evaluate("rev42_ai_probe_only", AI_CLEAN, marker="NOPE")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("marker not found", r.stderr)

    def test_ai_markers_match_driver_format_strings(self):
        # The harness must not require a marker the driver does not emit.
        # Checked against src/ob_diag.c, the source of every rev42-ai* line.
        #
        # Phase 36Z retired rev42_ai_probe_only after the live PCIe failure:
        # its sequence is deleted, so the markers it used to emit are REQUIRED
        # TO BE ABSENT, and the new wrapper-only mode's exact format strings
        # are required to be present. This keeps the check's purpose (harness
        # and driver agree on the vocabulary) while pinning the retirement.
        src = (REPO / "src" / "ob_diag.c").read_text(encoding="utf-8")
        required = [
            "rev42-aiw: BEGIN (subset rev42_ai_wrapper_only",
            "PRE  seq=%u phase=ai_wrapper_only op=BCMA_AREAD32 off=0x%04x window=wrapper",
            "POST seq=%u status=ok readback=0x%08x",
            "STOP seq=%u reason=%s error=%d",
            "rev42-aiw: END rev42_ai_wrapper_only %s",
            "rev42-aiw: core_state=%s",
            "rev42-aiw: fault-latch=%d",
            "RETIRED after the live PCIe failure (Phase 36Z)",
        ]
        for marker in required:
            self.assertIn(marker, src, f"harness requires a marker the driver lacks: {marker}")
        # The deleted four-read sequence left these markers behind; none of
        # them may come back with the retired mode.
        retired = [
            "rev42-ai: BEGIN (BCMA-owned addressing",
            "PCI_CFG_READ",
            "rev42-ai: cfg-readback rc(",
            "coherent=%d",
            "rev42-ai: END rev42_ai_probe_only COMPLETE",
        ]
        for marker in retired:
            self.assertNotIn(marker, src,
                             f"retired marker still emitted by the driver: {marker}")

    def test_ai_driver_lines_do_not_trip_fault_signature(self):
        # A clean run must never be misread as a fault. The latch state line
        # is deliberately spelled "fault-latch" because FAULT_RE matches
        # "DEVICE LOST" and "dev_lost" case-insensitively.
        script = (REPO / "scripts" / "runtime-test.sh").read_text(encoding="utf-8")
        m = re.search(r"FAULT_RE='([^']+)'", script)
        self.assertIsNotNone(m, "FAULT_RE not found in runtime-test.sh")
        fault_re = m.group(1)
        src = (REPO / "src" / "ob_diag.c").read_text(encoding="utf-8")
        # Phase 36Z: the clean lines now belong to rev42_ai_wrapper_only.
        clean_lines = [
            "rev42-aiw: fault-latch=%d (0 = no device-loss latch was set"
            " during this diagnostic)",
            "rev42-aiw: END rev42_ai_wrapper_only %s - three wrapper reads only",
            "rev42-aiw: core_state=%s",
            "rev42-aiw: classification %s",
        ]
        for line in clean_lines:
            self.assertIn(line, src, f"clean driver line not found: {line}")
            self.assertIsNone(
                re.search(fault_re, line, re.IGNORECASE),
                f"clean driver line would trip the fault signature: {line}",
            )

    # ---- documented non-fault markers (M1a checklist finding F-A1) --------
    # The vendor-faithful FIFO-530 continuation prints
    # `... EXPIRED_AND_CONTINUED programmed=8007 last=0007 post=0007 reads=21
    #  (vendor-faithful opt-in; completion NOT verified)` (src/ob_d3a1.c). It
    # names a RECORDED opt-in, not a device loss, so it must be reported and
    # counted - never classified as a fault. The bare `NOT verified` catch-all
    # keeps its force on every other line.

    def test_documented_fifo530_continuation_is_reported_not_a_fault(self):
        env = {"OB_D3B_BOUNDARY_WITNESS": "1", "OB_D11_FIFO530_CONTINUE": "1"}
        r = evaluate("bsinitvals_test_only", WITNESS_LIVE_FIFO, env=env)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("verdict: CLEAN PASS", r.stdout)
        # Reported (counted) on stderr, and still present verbatim in the log.
        self.assertIn(
            "DOCUMENTED_NONFAULT_MARKERS: 2 line(s) matching"
            " 'EXPIRED_AND_CONTINUED'",
            r.stderr,
        )
        self.assertIn("FIFO-530 completion stays UNPROVEN", r.stderr)
        self.assertIn("EXPIRED_AND_CONTINUED", WITNESS_LIVE_FIFO)
        self.assertIn("completion NOT verified", WITNESS_LIVE_FIFO)
        # The unproven completion stays in the recorded evidence.
        self.assertIn("fifo530_continue=1", WITNESS_LIVE_FIFO)
        self.assertIn("fifo530_entries_continued=2", WITNESS_LIVE_FIFO)

    def test_genuine_fault_beside_the_continuation_is_still_a_fault(self):
        env = {"OB_D3B_BOUNDARY_WITNESS": "1", "OB_D11_FIFO530_CONTINUE": "1"}
        text = WITNESS_LIVE_FIFO + (
            "openbrcm: DEVICE LOST (radio-probe) - reboot required\n"
        )
        r = evaluate("bsinitvals_test_only", text, env=env)
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))
        self.assertIn("verdict: FAULT -> REBOOT REQUIRED (no rmmod)", r.stdout)
        self.assertIn("DOCUMENTED_NONFAULT_MARKERS: 2 line(s)", r.stderr)
        # The live-path fault listing must show the genuine hits only; the
        # 40-line tail is what the operator reads first, and the excised
        # opt-in lines must not dominate it (only the live path prints it,
        # hence the source pin instead of a log assertion).
        script = (REPO / "scripts" / "runtime-test.sh").read_text(encoding="utf-8")
        self.assertIn('fault_hits "$RUNLOG" | tail -n 40', script)
        self.assertNotIn('grep -iE "$FAULT_RE" "$RUNLOG" | tail -n 40', script)

    # ---- witness v2 (P0.1/P0.3 batch) ------------------------------------
    # v2 requirements are enforced only when OB_D3B_WITNESS_V2=1 is armed, so
    # historical v1 captures keep their original contract (checked below).

    def test_witness_v2_contract_accepts_the_extended_capture(self):
        env = {"OB_D3B_BOUNDARY_WITNESS": "1", "OB_D3B_WITNESS_V2": "1"}
        r = evaluate("bsinitvals_test_only", WITNESS_V2_LIVE, env=env)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("verdict: CLEAN PASS", r.stdout)
        for line in ("stage=P1 window=cc offset=0x0604",
                     "stage=P2 window=cc offset=0x060c",
                     "stage=P3 window=cc offset=0x0660",
                     "stage=P4 window=cc offset=0x0664",
                     "stage=C1 window=d11 offset=0x03fe",
                     "stage=C2 window=d11 offset=0x03fc",
                     "stage=B10 window=d11 offset=0x03e0",
                     "witness_v2=1", "p1=LIVE", "c1_03fe=LIVE",
                     "b10_03e0=LIVE"):
            self.assertIn(line, WITNESS_V2_LIVE)

    def test_witness_v2_contract_rejects_a_v1_capture(self):
        # The same v1 capture that is a clean PASS under the v1 contract is
        # INCOMPLETE under the v2 contract: the extended evidence is absent.
        v1_env = {"OB_D3B_BOUNDARY_WITNESS": "1",
                  "OB_D11_FIFO530_CONTINUE": "1"}
        r1 = evaluate("bsinitvals_test_only", WITNESS_LIVE_FIFO, env=v1_env)
        self.assertEqual(r1.returncode, 0, (r1.stdout, r1.stderr))
        v2_env = dict(v1_env, OB_D3B_WITNESS_V2="1")
        r2 = evaluate("bsinitvals_test_only", WITNESS_LIVE_FIFO, env=v2_env)
        self.assertEqual(r2.returncode, 4, (r2.stdout, r2.stderr))
        self.assertIn("verdict: INCOMPLETE (no rmmod)", r2.stdout)

    def test_witness_v2_contract_rejects_a_truncated_v2_capture(self):
        # A v2 run whose controls/vendor lines never reached the log (with the
        # summary still claiming v2) is INCOMPLETE, not a pass.
        text = "\n".join(line for line in WITNESS_V2_LIVE.splitlines()
                         if "stage=C1" not in line and "stage=B10" not in line) + "\n"
        env = {"OB_D3B_BOUNDARY_WITNESS": "1", "OB_D3B_WITNESS_V2": "1"}
        r = evaluate("bsinitvals_test_only", text, env=env)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))

    def test_not_verified_catchall_survives_off_the_documented_marker(self):
        # A future `NOT verified` line stays a fault: the excision is a
        # whole-line exemption for documented markers, not a weakened matcher.
        r = evaluate("ucode_test_only", "some-future-marker: state NOT verified\n")
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))
        # The driver's genuine quiesce fault keeps its classification too.
        r2 = evaluate(
            "ucode_test_only",
            "dma-test: quiesce NOT verified; DMA memory retained,"
            " reboot required\n",
        )
        self.assertEqual(r2.returncode, 3, (r2.stdout, r2.stderr))

    def test_not_verified_census_over_driver_sources(self):
        """Every `NOT verified` phrase in src/ has a deliberate classification.

        The phrase is on the same log LINE as its documented marker in the
        continuation (so the whole-line exemption applies); any other source
        line that carries it stays a fault. A new occurrence must be
        classified deliberately instead of inheriting either behaviour.
        """
        script = (REPO / "scripts" / "runtime-test.sh").read_text(encoding="utf-8")
        m_fault = re.search(r"FAULT_RE='([^']+)'", script)
        self.assertIsNotNone(m_fault, "FAULT_RE not found in runtime-test.sh")
        m_doc = re.search(r"DOCUMENTED_NONFAULT_RE='([^']+)'", script)
        self.assertIsNotNone(
            m_doc, "DOCUMENTED_NONFAULT_RE not found in runtime-test.sh"
        )
        fault_re, doc_re = m_fault.group(1), m_doc.group(1)
        # classify() must scan through the exemption, not around it.
        self.assertIn("fault_hits()", script)
        self.assertNotIn('if grep -qiE "$FAULT_RE" "$runlog"', script)

        hits = []
        for path in sorted((REPO / "src").glob("*.[ch]")):
            for num, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1
            ):
                if "NOT verified" in line:
                    hits.append((path.name, num, line))
        self.assertEqual(
            sorted({name for name, _, _ in hits}),
            ["ob_d3a0.c", "ob_d3a1.c"],
            "unclassified `NOT verified` occurrence(s): "
            + repr([(n, i) for n, i, _ in hits]),
        )
        for name, num, line in hits:
            # The phrase alone is a fault word in every one of them...
            self.assertIsNotNone(
                re.search(fault_re, line, re.IGNORECASE),
                f"{name}:{num}: phrase lost its fault force",
            )
            if name == "ob_d3a1.c":
                # ...but this one is the documented marker, on the same line.
                self.assertIn("EXPIRED_AND_CONTINUED", line)
                self.assertIsNotNone(
                    re.search(doc_re, line, re.IGNORECASE),
                    f"{name}:{num}: documented marker not on the fault line",
                )
            else:
                self.assertIsNone(
                    re.search(doc_re, line, re.IGNORECASE),
                    f"{name}:{num}: fault line would be excused",
                )

    # ---- fw_chain_test_only ---------------------------------------------

    def test_fw_chain_clean_log_passes(self):
        r = evaluate("fw_chain_test_only", FW_CHAIN_CLEAN)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("CLEAN PASS", r.stdout)

    def test_fw_chain_missing_stop_marker_is_incomplete(self):
        text = FW_CHAIN_CLEAN.replace(
            "fw-chain-test: PASS at POST_UCODE_READY; stopped before PHY/radio/DMA/IRQ/mac80211\n",
            "",
        )
        r = evaluate("fw_chain_test_only", text)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_fw_chain_wrong_counts_are_incomplete(self):
        text = FW_CHAIN_CLEAN.replace("words=10850 records=610",
                                      "words=10848 records=610")
        r = evaluate("fw_chain_test_only", text)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_fw_chain_missing_attach_prefix_is_incomplete(self):
        text = FW_CHAIN_CLEAN.replace("OB_ATTACH_CP5_IDENTITY_READY state=8\n", "")
        r = evaluate("fw_chain_test_only", text)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_fw_chain_normal_bootstrap_leak_is_incomplete(self):
        text = FW_CHAIN_CLEAN + "normal-bootstrap: POST_UCODE_READY\n"
        r = evaluate("fw_chain_test_only", text)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_fw_chain_retained_failure_is_incomplete(self):
        text = FW_CHAIN_CLEAN + (
            "fw-chain: RETAINED stage=common-apply image=brcm/common "
            "mutated=1; device must stay bound, no teardown/reboot to recover\n"
        )
        r = evaluate("fw_chain_test_only", text)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_fw_chain_failure_line_is_incomplete(self):
        text = FW_CHAIN_CLEAN + (
            "fw-chain: FAIL stage=psm-poll image=brcm/ucode ret=-110 "
            "mutated=1 retained=1 (no cleanup writes)\n"
        )
        r = evaluate("fw_chain_test_only", text)
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_fw_chain_out_of_order_result_is_incomplete(self):
        lines = FW_CHAIN_CLEAN.splitlines()
        result = next(i for i, line in enumerate(lines)
                      if line.startswith("fw-chain: PASS"))
        lines.insert(0, lines.pop(result))
        r = evaluate("fw_chain_test_only", "\n".join(lines) + "\n")
        self.assertEqual(r.returncode, 4, r.stdout + r.stderr)

    def test_fw_chain_dry_run_preserves_state(self):
        r = run("--dry-run", "--mode", "fw_chain_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('"fw_chain_test_only=1"', r.stdout)
        self.assertIn("preserve openbrcm state; no rmmod", r.stdout)
        self.assertNotIn("# rmmod openbrcm", r.stdout)

    def test_old_modes_verdict_unchanged(self):
        # ucode mode: marker present -> pass; removed -> incomplete
        ok = evaluate(
            "ucode_test_only",
            "ucode-test: PASS - stopped before initvals/PHY/radio/DMA\n",
        )
        self.assertEqual(ok.returncode, 0, (ok.stdout, ok.stderr))
        bad = evaluate("ucode_test_only", "ucode-test: BEGIN\n")
        self.assertEqual(bad.returncode, 4, (bad.stdout, bad.stderr))


if __name__ == "__main__":
    sys.exit(0 if unittest.main(verbosity=2).result.wasSuccessful() else 1)
