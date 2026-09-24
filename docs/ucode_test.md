# M3.4D2A — D11 rev42 ucode upload + PSM start (isolated test mode)

Status: implemented, built, signed. **Not yet run on hardware.**
Mode: `ucode_test_only=1`. Mutually exclusive with `fw_validate_only=1`.

This milestone intentionally stops *before* common initvals, PHY, radio,
channel, RX/TX DMA, IRQ and mac80211. It is an isolated hardware test path, not
normal bring-up.

## 1. Mode isolation

| mode | fw_validate_only | ucode_test_only | probe branch |
|---|---|---|---|
| normal | 0 | 0 | full bring-up (unchanged) |
| validation | 1 | 0 | `ob_fw_probe()` only |
| ucode hardware test | 0 | 1 | `ob_ucode_test()` only |
| invalid | 1 | 1 | `-EINVAL` (never touches hardware) |

`ob_ucode_test()` never falls through into normal RX/DMA/mac80211 code, and
`ob_remove()` takes a dedicated `ucode_test_only` guard that performs no
RX/IRQ/DMA/mac80211 teardown.

## 2. Exact call graph

```
ob_probe()
  |   identity check (manuf/id) + mode-conflict check
  +-- ob_ucode_test(hw)                 [src/ob_ucode.c]
        +-- ob_fw_request_ucode()       acquire + size + FNV-1a-64 (no MMIO)
        +-- ob_ucode_prepare()
        |     +-- bcma_host_pci_up(bus)
        |     +-- bcma_core_is_enabled(); if false: bcma_core_enable(core, 0)
        |     +-- bcma_core_set_clockmode(core, BCMA_CLKMODE_FAST)
        |     +-- bcma_read32(CLKCTLST)              [read-only log]
        +-- ob_ucode_mctrl_update(~0, 0x04000404)     pre-upload MACCONTROL
        +-- bcma_write32(OBJADDR, 0x03000000); bcma_read32(OBJADDR)
        +-- 10850 x bcma_write32(OBJDATA, word[i])
        +-- bcma_write32(MACINTSTATUS, 0xffffffff)
        +-- ob_ucode_mctrl_update(~0, 0x04020402)     PSM start
        +-- bounded poll: bcma_read32(MACINTSTATUS) & MI_MACSSPNDD
        +-- ob_ucode_read_shm16(0x98/0x9a/0x9c/0x9e) [read-only diagnostic]
        +-- release_firmware()
```

## 3. Minimum BCMA/D11 preparation (only the proven subset)

Before implementation, the vendor path was re-examined. The minimum is three
existing, already runtime-proven operations (M2.5a/M2.5b `ob_si_powerup`
stages A/B/C); the rest of the normal bring-up (SPROM/OTP/D11 diag, DMA, IRQ,
RX, mac80211) is deliberately **not** run.

| # | operation | state affected | why required | provenance |
|---|---|---|---|---|
| 1 | `bcma_host_pci_up(bus)` | PCIe2 host/backplane access | D11 registers are unreachable until the PCIe2 host is up | `wlc_bmac_up_prep` si_pci_up; M2.5b (bcma public host path) |
| 2 | `bcma_core_enable(core, 0)` | BCMA `IOCTL`/`RESET_CTL` (+ core reset) | core must be out of reset before PSM/ucode registers work; kernel impl of recovered `ai_core_reset` | `wlc_bmac_corereset`; M2.5a |
| 3 | `bcma_core_set_clockmode(FAST)` | `CLKCTLST` FORCEHT/HAVEHT | D11 needs the HT clock for PSM/ucode | `wlc_bmac_corereset` core_phypll_ctl; M2.5a |
| 4 | MACCONTROL upload state | `MACCONTROL 0x120` | puts the MAC in the vendor pre-upload state | `wlc_bmac_init` @0x6836c |

Note ordering: `bcma_core_enable()` resets the D11, so MACCONTROL is written
only after prep completes.

## 4. Exact recovered values

- pre-upload `MACCONTROL = 0x04000404` = `IHR_EN(0x400) | PSM_JMP0(0x4) | WAKE(0x04000000)`;
  `PSM_RUN=0`, `EN_MAC=0`, `SHM_EN=0`.
  Provenance: `wlc_bmac_init` @0x68353 `wlc_bmac_mctrl(dev, ~0, 0x04000404)`.
- PSM start `MACCONTROL = 0x04020402` = `IHR_EN | INFRA(0x20000) | PSM_RUN(0x2) | WAKE`;
  `EN_MAC` is **not** set.
  Provenance: `wlc_bmac_wowlucode_start` @0x63854 `wlc_bmac_mctrl(dev, ~0, 0x04020402)`.
- Both are applied as a **masked** read-modify-write (`wlc_bmac_mctrl` 0x6060d:
  `(old & ~mask) | val`), with vendor mask `~0`.
- `MACINTSTATUS = 0xffffffff` written before PSM start.
  Provenance: `wlc_bmac_wowlucode_start` @0x6384c `osl_writel(0xffffffff, D11+0x128)`; doc
  also records the earlier `wlc_bmac_enable_mac` write of `1`.
- `OBJADDR (D11+0x160) = 0x03000000` = `AUTO_INC(0x03000000) | UCM_SEL(0)`, then read back.
  Provenance: ucode applier @0x60744 (`osl_writel(0x03000000, D11+0x160)`, `osl_readl`).
- `OBJDATA (D11+0x164)` receives each raw little-endian 32-bit word; one write per
  word, no byte swap. Same function, loop `0x60791..0x607a5`.

## 5. MMIO write chronology (OpenBRCM-explicit D11 writes)

| # | register | value | note |
|---|---|---|---|
| 1 | `MACCONTROL 0x120` | `0x04000404` | masked RMW (mask `~0`) |
| 2 | `OBJADDR 0x160` | `0x03000000` | then `0x160` read back (barrier) |
| 3 | `OBJDATA 0x164` | `word[0..10849]` | exactly 10850 writes, counted |
| 4 | `MACINTSTATUS 0x128` | `0xffffffff` | clear stale state, before PSM start |
| 5 | `MACCONTROL 0x120` | `0x04020402` | masked RMW (mask `~0`) |
| 6 | `OBJADDR 0x160` | `0x10000 \| (off>>2)` | once per SHM diagnostic (4x) |
| 7 | (SHM data is read, not written) | — | `read16(0x164 \| (off&2))` |

Indirect writes inside prep (bcma): `RESET_CTL`, `IOCTL`, `CLKCTLST` (FORCEHT)
plus PCIe `pcie_set_readrq`. No OpenBRCM DMA/IRQ/PHY/radio/channel register is
ever written.

## 6. PSM success poll

`wlc_bmac_wowlucode_start` @0x63828:

- `r13d = 0xF4249 (1000009)`, per iteration `osl_delay(10)` (→ `udelay(10)`),
  `r13d -= 10`, loop while `r13d != 9`.
- reads `macintstatus`; success iff `bit0` (`MI_MACSSPNDD`) set.
- => delay/iteration = **10 us**; max iterations = `0xF4249 / 10` = **100000**;
  max total = **1.000000 s**. No IRQ; every poll is bounded.

## 7. Post-start validation

- Success condition: `MACINTSTATUS & MI_MACSSPNDD (1<<0)`.
- No ucode revision/feature string is read: the vendor does **not** validate one
  (that is a b43-only step), so D2A does not invent one.
- SHM FIFO-size diagnostic: `M_FIFOSIZE0..3` at SHM byte offsets
  `0x98/0x9a/0x9c/0x9e`, accessed exactly as `wlc_bmac_read_shm` @0x61910 does:
  write `OBJADDR = 0x10000 | (off>>2)`, read back, then `read16(OBJDATA + (off&2))`.
  **Ordering caveat:** the vendor's own call sites (`0x68f88..0x68fae`) are
  *after* the common-initvals applier (`0x68b98`). D2A therefore only **logs**
  them as a read-only diagnostic and applies **no equality test**.

## 8. Failure / unwind matrix

No cleanup register writes are performed in any case; no `bcma_core_disable`/
reset is issued. Rationale: no DMA, IRQ, EN_MAC or mac80211 is active, so there
is no host resource to release; the ucode cannot execute (case A-C, `PSM_RUN=0`)
or is suspended/running exactly like the vendor's post-`wowlucode_start` state
(D/F); and the vendor itself does not reset on this failure. A later load
re-issues `OBJADDR` and rewrites the whole image.

| case | PSM_RUN | EN_MAC | core | reset/disable |
|---|---|---|---|---|
| A. before first ucode write | 0 | 0 | enabled | no |
| B. mid-upload (not reachable: loop always completes) | 0 | 0 | enabled | no |
| C. upload done, PSM not started | 0 | 0 | enabled | no |
| D. PSM start timeout | 1 | 0 | enabled | no; return `-ETIMEDOUT` |
| E. post-start validation fail | 0/1 | 0 | enabled | no; return `-EIO` |
| F. success then `rmmod` | 1 | 0 | enabled | no; remove logs and does nothing |

## 9. Bounded execution time (excluding `request_firmware` disk latency)

| segment | bound |
|---|---|
| host up (`pcie_set_readrq`) | negligible |
| core enable (`bcma_core_disable` wait) | <= 300 jiffies (early-return if already reset) + ~12 us |
| FAST clock (`usleep_range` + HAVEHT poll) | <= ~15.3 ms |
| MACCONTROL + OBJADDR + 10850 OBJDATA writes | ~tens of ms |
| PSM poll | <= 100000 x 10 us = 1.000000 s |
| SHM diagnostic | 4 windowed reads |
| **total worst case** | **~1.0 s + ~15 ms + <=300 jiffies (~2.2 s at HZ=250)** |

All loops are bounded: no unbounded core/clock/PSM/SHM wait.

## 10. Unreachable in this mode

initvals (`ob_fw_iv_parse`/appliers), PHY, radio, calibration, channel,
`ob_dma_init`, `ob_irq_init` (`request_irq`), `I_RI`/`MI_DMAINT`, RX DMA,
`ob_mac80211_register`/`ieee80211_register_hw`. `nm src/ob_ucode.o` references
no DMA/IRQ/RX/mac80211 symbol.
