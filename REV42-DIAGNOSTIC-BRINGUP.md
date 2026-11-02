# REV42-DIAGNOSTIC-BRINGUP — staged BCM4352/D11 rev42 diagnostic to a radio-ID read

> ## ⛔ HISTORICAL ONLY — NOT A CURRENT PROCEDURE, NOT A CURRENT FACT
>
> **Canonical replacement for every address, mode and status claim in this
> file:**
>
> | topic | canonical source |
> | :--- | :--- |
> | D11 address model, `core->addr`, `core->wrap`, superseded models | [`docs/m34d4/addr_provenance.md`](docs/m34d4/addr_provenance.md) |
> | the RUN 1 measurement and `0x0FFC` semantics | [`docs/m34d4/d11_addr_u1_measured.md`](docs/m34d4/d11_addr_u1_measured.md) |
> | the RUN 2 execution and the core-enable proof | [`docs/m34d4/rev42_ai_enable_freeze.md`](docs/m34d4/rev42_ai_enable_freeze.md) |
> | the five rev42 probe modes and their per-mode hardware status | [`docs/m34d4/rev42_diag_modes.md`](docs/m34d4/rev42_diag_modes.md) |
> | live project state and STOP boundary | [`docs/agent-state.md`](docs/agent-state.md) |
>
> **What changed after this file was written (2026-09-27 M3.4D reconciliation):**
>
> - `core->addr` (D11 core functional) was **measured** as **`0x18001000`** and
>   `core->wrap` (BCMA wrapper) as **`0x18101000`**. These are
>   `PROVEN_HARDWARE` (RUN 1, marker `openbrcm-test-20260927-014301-8836`).
>   Any statement here that a window base is "UNKNOWN", or that proposes
>   `0x18000000` / `0x18001000` / `0x18100000` as the base, is **SUPERSEDED**.
> - The `+0x1000` (ROOT+slot) and "two-root" relations are **RETRACTED**. The two
>   window addresses are independent eROM-derived values in two independent PCI
>   config registers (`0x80` and `0x70` on PCIe2); no arithmetic relates them.
> - **No positive role is claimed for `0x18000000`** — specifically, it is *not*
>   claimed to be ChipCommon. The only canonical statements about it are
>   negative.
> - Raw `PCI_CFG_WRITE` of `0x80` / `0x70` / `0xAC` is **withdrawn**. The driver
>   performs no window-register write; BCMA owns the window switch.
> - `AI_WRAPPER_UNREACHABLE` and any "all-ones proves a wrong window" rule are
>   **RETRACTED**. All-ones is an observation only.
> - `0x0FFC` is a core-window **probe offset of UNKNOWN register identity**. It
>   is **not** a readability discriminator, **not** a liveness discriminator and
>   **not** evidence of a wrong window. Hardware measured `0xffffffff` there both
>   with the core down (RUN 1) and with `ai_iscoreup = 1` (RUN 2, marker
>   `openbrcm-test-20260927-100501-6116`) — which is exactly why it discriminates
>   nothing.
> - `rev42_ai_probe_only` (RUN 1) and `rev42_ai_enable_only` (RUN 2) have each
>   been executed **once**. Both candidates are **spent**; neither is to be
>   re-run merely for confirmation, and this file authorises no execution.
> - **The `clkctlst=070b0042` / `HAVEHT=1` observations below are RETRACTED as
>   `CLKCTLST` evidence (2026-09-27, M3.4E `ANALYSIS ONLY`).** `0x01E0` is
>   **two registers in two windows**: `CLKCTLST` (`FORCEHT` bit1, `HAVEHT`
>   bit17) is the **ChipCommon** register (`re.db` named:
>   `si_pmu_pll_init +0x1e0 => CLKCTLST`); the **D11-window** `0x1e0` is a
>   different register of **`UNKNOWN`** identity, vendor-written with bit1
>   (`sub_64887`, the mac-clock on/off helper), bit5 (`wlc_bmac_radio_hw`) and
>   bits 8|9 (`wlc_bmac_core_phypll_ctl`) — the vendor never manipulates bit17
>   there. The value above was read from the **D11** window
>   (`artifacts/rev42_probe_trace.json` seq 3, `"block": "D11"`,
>   `"offset": 480`). The raw observation is `PROVEN_HARDWARE` and **unchanged**;
>   only the `HAVEHT=1` / "D11 alive at CP0, CLKCTLST" conclusion is retracted.
>   There is **no** captured observation of ChipCommon `CLKCTLST`; the driver
>   reads that one correctly via `ob_si_cc_read(hw, OB_CC_CLKCTLST)`
>   (`src/ob_si.c:211,334`). Evidence:
>   [`docs/m34d4/m34e_d11_mac_accessibility.md`](docs/m34d4/m34e_d11_mac_accessibility.md).
>
> The run records and analysis **inside this file are kept verbatim** as
> historical evidence. Nothing here should be read as a current fact or as an
> instruction to touch hardware.


Status: **`IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE ATTEMPTED` —
`FAILED` (device lost at CP4, no radio ID read).** The device-lost fail-safe
worked (no platform reset). **Reboot required; no retry in this boot.**

- Target: Broadcom **BCM4352 PCIe**, chip rev 3, D11 core `0x812` rev `0x2A`
  (42), AC-PHY `phy_type = 0x0B`.
- Vendor specification: `/media/kartashoff/Storage/mrbin/REV42-VENDOR-BRINGUP.md`
  + `artifacts/rev42_vendor_sequence.yaml` + `artifacts/rev42_radio_probe.yaml`.
  `CONFIRMED_VENDOR_REV42` is treated as direct rev42 evidence.
- Vendor blob: `wlc_hybrid.o_shipped`,
  sha256 `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743`.
- New mode: `rev42_diag_probe_only=1` (+ staged modifier `rev42_diag_full=1`).
- Machine-readable trace: `artifacts/rev42_probe_trace.json`.
- Tested module: `openbrcm.ko` sha256
  `f65e773845ba96e96a29192eac55f51137b659b18e44371a63d2a4ef5f174df9`,
  srcversion `862BF9AA307B678A8F281E6`, signer `Broadcom Driver MOK`.
- Hardware event: 2026-09-26, kernel `7.0.0-34-generic`.

Vocabulary: **FACT** (proven), **HISTORICAL** (older implementation, not rev42),
**HYPOTHESIS** (plausible), **UNKNOWN** (not recovered).

---

## 1. Current actual OpenBRCM sequence

Traced from `src/ob_core.c` (`ob_probe`), `ob_si.c`, `ob_fw.c`, `ob_dma.c`,
`ob_irq.c`, `ob_rx.c`, `ob_mac80211.c`. Stage status compared with the vendor
rev42 state machine (§3 of `REV42-VENDOR-BRINGUP.md`).

| Stage | Where | Status | Notes |
| --- | --- | --- | --- |
| PCI probe / bcma bind | `ob_probe` (ob_core.c) | **PRESENT** | identity gate `0x812` rev 42; binds `bcma0:1` |
| BCMA / ChipCommon discovery | `ob_si_probe` | **PRESENT** | `hw->cc = bus->drv_cc.core` |
| PCIe2 host up | `ob_si_powerup` stage A / `bcma_host_pci_up` | **PRESENT** | `host_is_pcie2=1` |
| D11 core enable/reset | `bcma_core_enable` | **PRESENT** | rev42 reset flag `| 0x4` is inside bcma |
| FAST clock | `bcma_core_set_clockmode(FAST)` | **PRESENT** | `clkctlst=070b0042 HAVEHT=1` — **`HAVEHT` claim RETRACTED 2026-09-27**: this is a **D11**-window read, not ChipCommon `CLKCTLST`; see the banner |
| MAC/PHY clock (SICF_MPCLKE) | `ob_d3a0`/`ob_radio`/`ob_diag` | **PARTIAL** | diag uses BCMA_IOCTL MPCLKE; vendor uses `si_core_cflags 0x10 bit4` |
| PMU / xtal / `si_clkctl_init` / `si_pci_setup` | — | **MISSING** | vendor `wlc_bmac_up_prep` items not implemented |
| SPROM / board info | `ob_si_read_mac` (rev11) | **PRESENT** | CRC-validated; board fields for MHF |
| ucode acquisition/validation | `ob_fw_probe` | **PRESENT** (validate only) | exact size + FNV-1a-64; not uploaded in production |
| ucode upload + PSM | `ob_ucode_run_d2a` | **EXPERIMENTAL** | isolated `ucode_test_only` (HARDWARE PROVEN); not in production |
| common initvals (610) | `ob_initvals_run_d2b` | **EXPERIMENTAL** | isolated `initvals_test_only` (HARDWARE PROVEN); not in production |
| D11 tail (sub_67efd/T1/DMA/T2/macfreq) | `ob_d3a1` | **EXPERIMENTAL** | isolated `d11_tail_test_only` (HARDWARE PROVEN) |
| band init / bsinitvals (73) | `ob_d3b` | **EXPERIMENTAL** | isolated `bsinitvals_test_only`; **HARDWARE ATTEMPTED → platform crash** |
| AC-PHY init (`wlc_phy_init`→`anacore`→`switch_radio`→`init_aphy`) | — | **MISSING** | D4 `GO: NO`; no code |
| radio config / PLL / channel / calibration | — | **MISSING** | — |
| DMA rings | `ob_dma_init` (SW) | **PRESENT** | 32-bit window, 8 KiB rings, no HW regs |
| RX engine | `ob_rx_init` | **PRESENT (early)** | FIFO0 programmed + `MI_DMAINT` enabled **before** ucode/PHY → **ordering divergence** |
| IRQ registration | `ob_irq_init` | **PRESENT** | `request_irq`; sources masked at init |
| host IRQ route | `ob_rx_enable_irq` | **PRESENT (early)** | route opened at RX init, not after PHY (vendor opens in `wlc_bmac_up_finish`) |
| `EN_MAC` | — | **MISSING** | never set in production |
| mac80211 | `ob_mac80211_register` | **PRESENT** | 2.4 GHz HT 2x2; `start()` = `-EOPNOTSUPP`; `tx` drops |
| TX path | — | **MISSING** | no TX descriptors/ring |

**FACT:** the production path never uploads ucode, never applies initvals, never
initialises the PHY/radio, never selects a channel, never sets `EN_MAC` and
enables the RX engine + host IRQ route too early. The rev42 stages exist only as
isolated experimental modes.

---

## 2. Vendor rev42 comparison

Hardware-operation/ordering comparison (not source similarity). Vendor evidence
from `artifacts/rev42_vendor_sequence.yaml` (62 records, 53 CONFIRMED).

| phase | vendor rev42 requirement | current OpenBRCM | difference | evidence | action |
| --- | --- | --- | --- | --- | --- |
| PCIe | enumerate, D11 core enable | present | none | runtime | — |
| reset | `si_core_reset` with rev42 flag `\|4` (phyrev>0x11) | bcma_core_enable (flag inside bcma) | not proven to apply the `\|4` flag | vendor seq 2 | verify bcma reset flags or add explicit |
| clocks | xtal on, `si_clkctl_init`, `si_pci_setup/up`, `macphyclk_set(1)` | FAST clock + BCMA_IOCTL MPCLKE | PARTIAL; no xtal/clkctl_init | vendor seq 1 | add `si_clkctl_init`/xtal if required |
| radio-ID read | in `wlc_phy_attach`, **after `wlc_phy_anacore(pi,1)`**; no ucode/initvals | not run in production; diag ran it | **DIVERGENCE** (was attempted, device lost) | vendor §9/§10; this run | recover `[pi+0x118]` anacore callback first (§11) |
| ucode | `copyto_objmem` OBJADDR/dummy-read/OBJDATA per word | `ob_ucode_run_d2a` identical | MATCH (isolated) | vendor seq 19-21 | integrate (proven) |
| common initvals | 610 records after PSM run | `ob_initvals_run_d2b` 610/113/497 | MATCH (isolated) | vendor seq 26 | integrate (proven) |
| D11 tail | sub_67efd → T1 → DMA → T2 → macfreq | `ob_d3a1` (isolated) | MATCH (isolated) | vendor seq 29-33 | integrate (proven) |
| band init/bsinitvals | full prefix through `sub_60f67@0x669bd` | `ob_d3b` (isolated, crashed) | DIVERGENCE (crash) | `d3b_crash_postmortem.md` | retire/successor D3+4 |
| PHY init | `wlc_phy_init` → `anacore`/`switch_radio`/`init_aphy` | missing | MISSING | vendor seq 40-44 | after radio-ID + callback recovery |
| radio config | `switch_radio_acphy` consumes cached `pi+0x16e` | missing | MISSING | vendor seq 45-52 | later |
| DMA | DMA init in tail, **host IRQ off** | production programs RX + opens route early | **DIVERGENCE** | vendor seq 32/34, §17.2 | gate RX/IRQ behind bring-up |
| EN_MAC | after PHY, in `wlc_bmac_enable_mac` | never set | MISSING | vendor seq 57 | later |
| historical | `bcm43xx-0.fw`, `phy_type==7` radio switch, `M_FIFOSIZE` block, `dma_txinit x6` | not used | MATCH (avoided) | vendor §20 | — |

---

## 3. Diagnostic sequence

New isolated mode **`rev42_diag_probe_only=1`**, staged modifier
**`rev42_diag_full=1`** (`src/ob_diag.{c,h}`). It bypasses the normal production
path; keeps it unchanged when not requested.

```
core prep: bcma_host_pci_up -> bcma_core_enable(D11) -> FAST clock -> MPCLKE
  -> CP0 trusted sentinel
  -> radio read #1 (core-only baseline, before anacore)
  -> [ if rev42_diag_full=1:
         D2A: ob_ucode_run_d2a (ucode + PSM start)   -> CP1
         D2B: ob_initvals_apply_common (610 records) -> CP2, CP3 ]
  -> CP4 prerequisite: wlc_phy_anacore(pi,1) direct side -> writew(0x0000, D11+0x3e6)
  -> radio read #2 (vendor 0x3d8 addr / 0x3da data) + vendor decode
  -> CP5 radio-ID; CP6 device-alive
  -> STOP
```

Design decisions grounded in `CONFIRMED_VENDOR_REV42`:

- **Vendor-minimal by default.** The vendor proves the radio-ID read needs only
  core + clock + `anacore`; ucode/initvals are gated behind `rev42_diag_full=1`
  and are run only to exercise CP1/CP2/CP3 (they are reused verbatim from the
  hardware-proven D2A/D2B code; `ob_initvals_apply_common()` was factored out).
- **Vendor-faithful decode.** `pi+0x16e = RADIO[0x0] >> 4` is recorded as the
  radio-ID byte; `pi+0x16f = RADIO[0x0] & 0x0f`; `pi+0x16c = RADIO[0x0] & 0xff`;
  `pi+0x16a = RADIO[0x1]`. The prior frozen `radio_id_probe_only` treated
  `RADIO[0x1]` as a BCM2069 chip id (expecting `0x2069`); that is **not** the
  vendor AC criterion and is not used here.
- **Radio window orientation** per vendor: `0x3d8` = address, `0x3da` = data
  (prior prose had them inverted).
- **No EN_MAC, no MACINTMASK, no host IRQ route, no DMA, no TX.**
- **`[pi+0x118]` anacore callback is UNKNOWN and is not run**; only the
  confirmed direct `0x3e6` write is issued (this is the step that failed, §7).

Every operation emits a `rev42-diag-op: {json}` line; the offline parser
`scripts/rev42_diag_trace.py` builds `artifacts/rev42_probe_trace.json`.

---

## 4. Checkpoints

| CP | meaning | record |
| --- | --- | --- |
| CP0 | D11 accessible before ucode | trusted `MACCONTROL` sentinel + `MACINTSTATUS`/`MACINTMASK` |
| CP1 | D11 accessible after ucode (staged only) | same |
| CP2 | D11 accessible after common initvals (staged only) | same |
| CP3 | required PSM/MAC state established (staged only) | same (`MACCONTROL=0x04020402`) |
| CP4 | PHY/radio interface accessible | after `0x3e6` write + `0x3e0` read |
| CP5 | radio ID successfully read | vendor decode of `R0`/`R1` |
| CP6 | device still alive after read | trusted sentinel |

A checkpoint sentinel equal to `0xffffffff` latches `hw->dev_lost`, stops all
MMIO and records the checkpoint as lost (no retry).

**Result:** CP0 reached and alive; CP1–CP3 not reached (staged path not run);
CP4 **DEVICE LOST**; CP5/CP6 not reached.

---

## 5. Hardware observations

Run: `sudo scripts/runtime-test.sh --mode rev42_diag_probe_only` (2026-09-26).

```
openbrcm: chip 0x4352 rev 3, d11 core rev 42
rev42-diag: BEGIN full=0
rev42-diag: prep host up (host_is_pcie2=1)
rev42-diag: clkctlst=070b0042 HAVEHT=1 core_enabled=1
rev42-diag: ioctl=00000011 mpclke=1
rev42-diag: CP0 pre-ucode alive=1 maccontrol=00000000 macintstatus=00000000 macintmask=00000000
rev42-diag: core-only r0=0xffff r1=0xffff id32=0xffffffff id_byte=0xff low_nibble=0xf low_byte=0xff pa_r1=0xffff valid=0 all_ones=1 all_zero=0 family=0
...
rev42-diag-op: {"seq":10,"phase":"phy","function":"wlc_phy_anacore","block":"PHY","offset":998,"width":16,"value_written":"0x0000","value_read":"none","result":"ok","checkpoint":"CP4","device_alive":1}
rev42-diag-op: {"seq":11,"phase":"phy","function":"phyversion","block":"PHY","offset":992,"width":16,"value_written":"none","value_read":"0xffff","result":"ok","checkpoint":"CP4","device_alive":1}
openbrcm: DEVICE LOST (phy-iface) - D11/BCMA/PCIe inaccessible; no further D11 MMIO; ...
rev42-diag-op: {"seq":12,...,"value_read":"0xffffffff","result":"dev_lost","checkpoint":"CP4","device_alive":0}
rev42-diag: CP4 DEVICE LOST sentinel=0xffffffff; no further MMIO
rev42-diag: FAIL ret=-5
```

`insmod rc=0`. The full trace is in `artifacts/rev42_probe_trace.json`.

Observations:

1. **D11 alive at CP0.** `MACCONTROL = MACINTSTATUS = MACINTMASK = 0`,
   `CLKCTLST = 0x070b0042` (`HAVEHT=1`), core enabled, MPCLKE set. The core is
   responsive and the trusted sentinel is not all-ones.
   **(2026-09-27 correction: the `0x070b0042` figure is a **D11**-window read at
   offset `0x1e0`, so the `HAVEHT=1` label is **RETRACTED** — `CLKCTLST` is the
   ChipCommon register of that offset. The "D11 alive at CP0" conclusion still
   rests on the `MACCONTROL`/`MACINTSTATUS`/`MACINTMASK` zeros, which is
   unaffected. See
   [`docs/m34d4/m34e_d11_mac_accessibility.md`](docs/m34d4/m34e_d11_mac_accessibility.md).)**
2. **Core-only radio read = `0xffff`/`0xffff`.** With the radio window read
   *before* `anacore`, both radio registers return all-ones. The D11 MACCONTROL
   sentinel remained `0x00000000` (D11 alive), so this is the radio window, not
   the D11 window. This **confirms the vendor ordering** (the radio window is
   not live before `wlc_phy_anacore`) and **disproves the frozen
   `radio_id_probe_only` premise** (that a radio ID is readable after core prep
   alone).
3. **The `0x3e6` write caused device loss.** After writing `0x0000` to
   `D11+0x3e6`, the next `0x3e0` read returned `0xffff`, each subsequent access
   stalled ~200 ms, and the CP4 sentinel read `0xffffffff`. The vendor performs
   this write **after** the `[pi+0x118]` AC-PHY anacore callback, which OpenBRCM
   does not have.

---

## 6. Radio-ID result

**No valid radio ID was obtained.** The only radio read that completed was the
core-only baseline, which returned the all-ones pair (`R0 = R1 = 0xffff`) and
decodes as invalid (`all_ones=1`, `valid=0`). The post-`anacore` radio read
(CP5) was never reached because the device was lost at CP4. The vendor-minimal
radio-ID milestone is therefore **NOT achieved**.

The vendor-decode arithmetic is implemented and host-tested
(`ob_diag_vendor_decode`, `tests/host/ob_diag_test.c`), so the moment the window
is live the ID will be decoded (`ID byte = R0 >> 4`).

---

## 7. Earliest divergence / failure

**Earliest divergence:** OpenBRCM issued the `wlc_phy_anacore` direct side
(`writew(0x0000, D11+0x3e6)`) **without the preceding `[pi+0x118]` AC-PHY
anacore callback** and the `wlc_phy_attach_acphy` state that installs it. The
vendor order is callback → `0x3e6`; issuing `0x3e6` alone wedged the D11/PHY
window (all-ones) → `DEVICE LOST`.

Secondary (earlier in the log, non-fatal) divergence: with only core prep, the
radio window reads all-ones, i.e. the frozen `radio_id_probe_only` design had an
incorrect premise.

**HYPOTHESIS:** the `0x3e6` write requires the AC-PHY analog-core callback (and
its register/clock setup) to be effective; without it the PHY interface becomes
inaccessible. **UNKNOWN:** the callback body (`pi+0x118`, vendor UNKNOWN #1) and
whether `0x3e6` is safe in any partial state.

---

## 8. Device-lost observations

- Repeated/settled all-ones reads were observed (`R0=R1=0xffff` at CP0, then
  `0x3e0=0xffff`, then `MACCONTROL=0xffffffff`).
- The central guard latched on the **trusted** `MACCONTROL` sentinel and then
  issued **zero** further MMIO (the CP4 checkpoint recorded `device_alive=0`
  and the code returned).
- **No platform reset / no AER / no sync flood.** Contrast with the M3.4D3B
  attempt, where teardown after device loss caused an AMD data-fabric sync flood
  and a platform reset. The fail-safe improved the outcome: the D11 loss was
  contained, the PCI device is still enumerated, and the machine remained up.
- The driver logged `dma-test: FATAL latch` on the rev42-diag path (the shared
  monotonic fatal latch is reused); this message is misleading for a
  non-DMA diagnostic and should be renamed. The module was pinned, as designed.
- **Per policy: do not rmmod, do not re-run, do not unbind; reboot to clear the
  latch.** No test was retried in this boot.

---

## 9. Confirmed working stages

- PCIe2 host up (`host_is_pcie2=1`).
- BCMA bind + D11 core enable (rev 42).
- FAST clock / `HAVEHT=1`.
- MAC/PHY clock gate (BCMA_IOCTL MPCLKE).
- D11 host-interface reads (MACCONTROL/MACINTSTATUS/MACINTMASK) and the trusted
  all-ones sentinel.
- Core-only radio-window read **mechanically** works (selector write + data
  read), returning the expected "not yet live" all-ones pair.
- Device-lost containment (no platform reset).

---

## 10. Remaining missing stages

1. **AC-PHY attach state** (`wlc_phy_attach_acphy`) and the **`[pi+0x118]`
   anacore callback body** — the immediate prerequisite of the radio-ID read;
   the top UNKNOWN.
2. A **safe PHY/radio prerequisite** that actually makes the radio window live
   (the direct `0x3e6` write alone is not safe).
3. The **radio ID read** itself (not yet obtained).
4. Production integration of the proven **D2A/D2B/D3A1**; a stable **D3B**
   successor (`M3.4D3+4`).
5. **D4** AC PHY init, radio config/PLL, channel, calibration, `EN_MAC`, TX, and
   the corrected RX/IRQ ordering.

---

## 11. Safe next integration step

**Do not write `D11+0x3e6` again, and do not re-run any radio probe, until the
`[pi+0x118]` AC-PHY anacore callback is recovered and its register effects are
understood.** That is a **static-only** task (zero hardware risk), and it is
vendor UNKNOWN #1 / E2 in `REV42-VENDOR-BRINGUP.md` §21/§22.

Recommended order:

1. **Static:** disassemble the `pi+0x118` callback installed by
   `wlc_phy_attach_acphy` (`0x080a198f`) and record its `phy_reg_write_array`
   table (vendor §12.2, §22 E2). This closes the last unexamined piece of the
   minimal radio-ID path.
2. **Safe read-only:** after a reboot, re-run `rev42_diag_probe_only=1` but with
   the `0x3e6` write **omitted** (or gated), to confirm the core-only baseline
   reproduces the all-ones pair without device loss, and to characterise
   `0x3e0`/`0x3fc`/`0x3fe` reads before any PHY write.
3. **Then** reproduce `wlc_phy_anacore(pi,1)` faithfully (callback + `0x3e6`)
   before attempting the radio-ID read again.

**Single next state transition to test (after reboot + static recovery):** the
`[pi+0x118]` AC-PHY anacore callback → `D11+0x3e6` transition, validated for
device survival *before* the radio read.

> **Operator action required now: reboot the machine.** The D11 window is lost
> (`MACCONTROL=0xffffffff`), the module is pinned and loaded, and no further MMIO
> may be issued this boot. Do not `rmmod`.

---

## Appendix A — files, tests, and reproduction

Changed/added (all additive; production path unchanged when the mode is off):

- `src/ob_diag.{c,h}` — new staged diagnostic mode, checkpoints, trace ops.
- `src/ob_ucode.h` — 8-way isolated-mode policy (`OB_ISOLATED_REV42_DIAG`).
- `src/ob_core.{c,h}` — `rev42_diag_probe_only` / `rev42_diag_full` params,
  dispatch, remove hook.
- `src/ob_initvals.{c,h}` — factored `ob_initvals_apply_common()` out of the
  proven D2B core (no behaviour change to `ob_initvals_run_d2b`).
- `Makefile` — `src/ob_diag.o`.
- `tests/host/ob_diag_test.c`, `tests/host/Makefile` — host tests (PASS).
- `scripts/rev42_diag_trace.py` — offline trace generator.
- `scripts/runtime-test.sh` — `rev42_diag_probe_only` / `rev42_diag_full` modes.
- `artifacts/rev42_probe_trace.json` — this run's trace.

Reproduce (after reboot):

```sh
make modules && ./scripts/sign.sh
sudo scripts/runtime-test.sh --mode rev42_diag_probe_only
# staged (only if the vendor-minimal run is clean and the device stays alive):
sudo scripts/runtime-test.sh --mode rev42_diag_full
python3 scripts/rev42_diag_trace.py --log <focused-rev42-diag-log> \
        --out artifacts/rev42_probe_trace.json
```

## Appendix B — claim classification

- **FACT:** production path lacks ucode/initvals/PHY/EN_MAC/TX and arms RX/IRQ
  early; vendor radio-ID needs only core+clock+anacore; core-only radio window
  reads all-ones; the direct `0x3e6` write preceded device loss at CP4.
- **HISTORICAL:** brcmsmac corerev 23/24 path; 15-region `bcm43xx-0.fw`;
  `phy_type==7` radio switch; `M_FIFOSIZE` block; `dma_txinit x6`.
- **HYPOTHESIS:** the `0x3e6` write is only safe after the `[pi+0x118]` callback;
  the callback configures clocks/anacore needed before `0x3e6`.
- **UNKNOWN:** the `[pi+0x118]` callback body; the board's radio ID; whether
  `0x3e6` is safe in any partial state; the exact D11 loss mechanism.
