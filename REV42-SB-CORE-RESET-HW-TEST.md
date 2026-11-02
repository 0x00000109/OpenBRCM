# REV42-SB-CORE-RESET-HW-TEST — vendor sb_core_reset cold-path hardware probe

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
>
> The run records and analysis **inside this file are kept verbatim** as
> historical evidence. Nothing here should be read as a current fact or as an
> instruction to touch hardware.


> ### ⚠ HISTORICAL RUN RECORD — the terminology correction below still stands
>
> The SB-block/core-window versus wrapper-window distinction below is correct and
> remains current; see `docs/m34d4/addr_provenance.md` §4c. The run record
> itself is history. `rev42_sb_reset_only` has **never been executed** as a
> current candidate, and this file authorises no execution.

> ## ⚠ HISTORICAL RUN RECORD — TERMINOLOGY CORRECTED
>
> This mode touches **only SB-block registers (0x0F00–0x0FFF) inside the D11
> core functional window `core->addr`**, reached with `ob_d11_read32()` /
> `ob_d11_write32()`. The **BCMA wrapper window `core->wrap`** (the AI/ARM agent
> window) is **never** touched by this mode. The two are different things and
> must not be conflated.
>
> Consequently two labels in the record below have been renamed in the driver:
> the trace block is now `SB_CORE` (was `WRAPPER`) and the terminal
> classification is now `SB_BLOCK_UNREACHABLE` (was `WRAPPER UNREACHABLE`).
> `scripts/runtime-test.sh` was updated to match; the historical text is left
> untouched.

**Status: RUN EXECUTED. The wrapper/SB register block read `0xffffffff` at R0, so
the probe STOPPED BEFORE ANY WRITE. Classification: `WRAPPER_UNREACHABLE`
(vendor brief R-6). No D11 functional read was performed. No DEVICE_LOST. Module
`rmmod` clean; reboot not required for safety.**

- Spec (read in full): `/media/kartashoff/Storage/mrbin/REV42-SB-CORE-RESET.md`
  and `/media/kartashoff/Storage/mrbin/artifacts/rev42_sb_core_reset.yaml`.
- Mode: new isolated `rev42_sb_reset_only=1` (R0..R7), vendor sequence only.
- Tested module: `openbrcm.ko` sha256
  `26eb3d7d98a93abd795f2b475a4ce9c597ad087a48cf196167d9d96b5da1edb4` (signed).
- Trace: `artifacts/rev42_sb_core_reset_probe.json`.

## 0. Preconditions

| Check | Result |
| --- | --- |
| BCM4352 `14e4:43b1` present | YES |
| Conflicting Wi-Fi driver | NONE |
| No DEVICE_LOST this boot | YES (prior run ended in a soft stop, no latch) |
| Diagnostic uses the new vendor-reset implementation | YES (`ob_sb_reset_test`, wrapper-only) |
| Normal production bring-up disabled | YES (isolated mode only) |
| **Fresh reboot** | **NO — same boot `19:50:42` as the previous run.** The stale module was `rmmod`'d before this run. Noted as a caveat (see §5). |

## 1. Executed sequence (authoritative log)

```
R0: read 0x0F98=0xffffffff 0x0F8=0xffffffff 0x0F90=0xffffffff 0x0F9C=0xffffffff
    -> wrapper_reachable = 0
    -> WRAPPER UNREACHABLE (R-6): STOPPING BEFORE WRITES
    -> no branch, no write, no D11 functional read
```

All four wrapper registers read all-ones on the first access. The vendor
sequence's `0x0F98` writes (`0x00070003/0x00040003/0x00070001/0x00070000/
0x00050000`) and the `0x0F8/0x0F90/0x0F9C` conditionals were therefore **not**
executed, per the brief's fail-safe ("wrapper/SB access itself becomes invalid →
STOP MMIO").

## 2. Required report

| # | Item | Result |
| --- | --- | --- |
| 1 | raw initial `SB+0x0F98` | **`0xffffffff`** |
| 2 | initial bit2 state | **SET** (0xffffffff; also bit0=1, bit1=1, bit16=1) |
| 3 | every conditional branch taken | **none** — the run stopped at R0 before any branch or write |
| 4 | raw `0x0F98` after each state transition | **n/a** — no state transitions were performed |
| 5 | terminal `0x0F98` value | **n/a** — no writes |
| 6 | final `si_iscoreup` | **n/a** (intermediate: `0xffffffff & 0x10007 = 0x10007 != 0x10000` → false, but the stop reason is wrapper-unreachable, not "core down") |
| 7 | first D11 functional access attempted? | **NO** |
| 8 | first MACCONTROL value | **n/a** |
| 9 | final classification | **`WRAPPER_UNREACHABLE` (R-6)** — distinct from `CORE_DOWN`; not `DEVICE_LOST` (PCI enumerated, no AER, `rmmod` clean) |
| 10 | exact first divergence | YAML seq **7** (R0): the vendor's `sb_read32(si->(0xb8)+0x0F98)` expects a boot-ROM/wrapper value whose bit0/bit2/bit16 are meaningful; the hardware returned `0xffffffff` (all four wrapper registers) — the wrapper/SB block is not reachable at the accessed address |
| 11 | may the next experiment proceed to PHY reset / SITE A? | **NO** — the core never reached `si_iscoreup`, so SITE A must not be attempted. The wrapper access must be resolved first |

## 3. Static basis for the accessor (why this is a real R-6, not a wrong base)

- Vendor: `si->(0xb8)` is produced by `sb_setcoreidx` (`elf 0x1c69a`) →
  `sub_1c5aa` (`elf 0x1c5aa`). For `bustype == 1` (PCI) `sub_1c5aa` writes the
  core's base address to **PCI config offset `0x80`** (`BCMA_PCI_BAR0_WIN`,
  "Backplane address space 0") and returns the BAR0-mapped core base. So
  `SB+0x0F98` = the BAR0 core-window mapping at `0x0F98`.
- Linux bcma: `bcma_host_pci_read32` = `ioread32(bus->mmio + offset)` after
  `bcma_host_pci_switch_core(core)` (which likewise programs the backplane
  window). `bcma_read32(core, off)` is therefore the **same BAR0 core window**
  the vendor's `si->(0xb8)` selects.
- OpenBRCM `ob_d11_read32(hw, 0x0F98)` = `bcma_read32(core, 0x0F98)` = exactly
  that mapping. It is the correct analogue of `si->(0xb8)+0x0F98`.
- (`bcma_aread32` / `ob_axi_read32` is a *different* window — `BAR0 + 0x1000`,
  the agent — used by bcma for `BCMA_IOCTL`/`BCMA_RESET_CTL`. It is recorded
  here as an alternative to test in the next phase, but the vendor's `si->(0xb8)`
  model points at the core window, not the agent window.)

Conclusion: the all-ones read is the brief's anticipated **R-6** condition
("backplane address-window programming for the D11 core ... nothing in
`wlc_bmac_attach`/`wlc_bmac_corereset` writes them; they are programmed by the
external `si_doattach`/`wlc_hw_attach`"). Reaching BAR0 directly through the
isolated probe does not reproduce that outer setup.

## 4. Evidence / reproduction

```
traces: /tmp/openbrcm-rev42diag-openbrcm-test-20260926-202307-26450.log
dmesg : /tmp/openbrcm-runtime-openbrcm-test-20260926-202307-26450.log
```
`insmod rc=0`; probe returned `-EIO` (soft stop, no latch); the harness
classified the run as a clean stop and `rmmod openbrcm` succeeded. PCI device
still enumerated; no AER/bcma errors.

## 5. Caveat

The machine was **not** freshly rebooted for this run (same boot `19:50:42` as
the previous failed probe; the stale module was unloaded manually). The wrapper
read is expected to be unaffected by prior D11 state (bcma never writes
`0x0F98`), but a fresh-boot re-run should be part of the next phase to be
certain.

## 6. Next single step (per the brief)

**Do not invent writes.** The next phase is to reconstruct the outer
attach/window setup that makes the wrapper/SB block reachable, i.e.:

1. determine the exact BAR0 window value / core base that `si_doattach` /
   `wlc_hw_attach` programs before `wlc_bmac_corereset` (R-6), and whether
   BCMA/PCIe needs it explicitly;
2. test whether the wrapper is reachable in the agent window
   (`ob_axi_read32(0x0F98)`) vs the core window (`ob_d11_read32(0x0F98)`);
3. only then re-run the vendor `sb_core_reset` sequence.

No ucode, initvals, bsinitvals, DMA, RX, TX, host IRQ or mac80211 were used.
No production bring-up was modified.
