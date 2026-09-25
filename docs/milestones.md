# Milestones

Each milestone has a concrete, observable gate.

| M | Deliverable | Gate |
|---|---|---|
| **M0** | `bcma` probe, ChipCommon window, CHIPID | module loads; dmesg shows `chip 0x4352` |
| **M1** | `ob_si` bring-up (PMU/PLL/clock), external SPROM read | dmesg shows the validated SPROM rev/CRC |
| **M2** | mac80211 registration; wiphy bands/channels/rates | `iw phy` lists the 2.4 GHz HT 2×2 band |
| **M3** | dma64 rings, NAPI, `ieee80211_rx` | interface up; RX/TX counters move |
| **M4** | `.config`, `.bss_info_changed`, `.set_key`, scan | `iw scan`, association, encrypted link |
| **M5** | AMPDU, power save, runtime PM, LED/rfkill | stable traffic; suspend/resume |
| **M6** | second PHY family (nphy/htphy) | family module loads |

## M3.4D1 — rev42 firmware acquisition + validation (DONE)
Gate: `insmod` logs `fw … validated` for `d11ucode42`, `ac1initvals42`,
`ac1bsinitvals42` and `all rev42 firmware validated; hardware untouched`; no
hardware writes.
- Source: exact vendor-blob images (provenance A) — see
  `docs/firmware_reconciliation.md`. No b43 IV conversion, no substitution.
- `src/ob_fw.{c,h}`: pure parser/iterator (vendor 8-byte records
  `{u16 offset, u16 width, u32 value}`, terminator `0xffff`) + kernel
  `ob_fw_probe()`; strict size + FNV-1a-64 + structure; dry run logs only the
  first/last 10 records/words. Present-but-invalid firmware fails probe.
- Recovered vendor order for D2: `MACCONTROL=0x04000404` (IHR_EN|PSM_JMP0|WAKE)
  → objaddr/objdata ucode upload → `macintstatus=-1`,
  `MACCONTROL=0x04020402` (PSM_RUN), poll `MI_MACSSPNDD` → common initvals →
  STOP before band init/PHY. bsinitvals deferred (applied with `wlc_phy_init`).

## M3.4D2A — D11 rev42 ucode upload + PSM start only (HARDWARE RUNTIME PROVEN on BCM4352)

Isolated mode `ucode_test_only=1` (mutually exclusive with `fw_validate_only`).
Gate (observed on BCM4352): upload `writes=10850`, PSM start, bounded poll
`MI_MACSSPNDD` PASS, then STOP before initvals/PHY/radio/DMA.
- Minimum prep only: `bcma_host_pci_up` + D11 `bcma_core_enable` + FAST clock
  (the proven M2.5 A/B/C subset), then the vendor upload sequence.
- `MACCONTROL=0x04000404` (IHR_EN|PSM_JMP0|WAKE) via masked RMW -> OBJADDR
  `0x03000000` (auto-inc) + 10850 raw LE OBJDATA writes with a write-count
  invariant -> `MACINTSTATUS=0xffffffff` -> `MACCONTROL=0x04020402`
  (IHR_EN|INFRA|PSM_RUN|WAKE, no EN_MAC) -> poll `MI_MACSSPNDD`
  (10 us x <=100000 => <=1.0 s). No initvals applier, no EN_MAC.
- SHM `M_FIFOSIZE0..3` (0x98..0x9e) are read with the vendor windowed
  `wlc_bmac_read_shm` access, logged only (the vendor reads them after common
  initvals). No revision string is invented (vendor reads none).
- Full call graph, MMIO chronology, unwind matrix and timing:
  `docs/ucode_test.md`. `remove()` has its own ucode_test_only guard and calls
  no RX/IRQ/DMA/mac80211 teardown. No cleanup register writes on any failure.

### M3.4D2A runtime evidence (BCM4352, chip rev 3, D11 rev 42)
Tested candidate `7265f9d`; implementation `47e0883`; base `854e398`.
`insmod` rc=0; image `brcm/bcm43xx-ucode.fw` `size=43400` `words=10850`;
`host_is_pcie2=1`, `clkctlst=070b0042`, HAVEHT=1, core_enabled=1; MACCONTROL
`00000000` -> upload `04000404`; `OBJADDR=30000000`; writes `10850/10850`; first
`0300104e 0001bc60 02f00e25 0003bfde`; last `02f00000 000002de 00000000
00000000`; PSM start `04020402`; poll `delay=10us max_iter=100000
max_total_us=1000000` PASS `iterations=11` `MACINTSTATUS=00000001`; SHM
`M_FIFOSIZE0..3=0000 0000 0000 0000` (diagnostic only — the vendor reads these
after the common-initvals applier, which D2A does not run); final
`PASS - stopped before initvals/PHY/radio/DMA`. No timeout/BUG/Oops/lockup/reset;
no DMA/IRQ/PHY/radio/channel init. Post-test state is intentionally partial
(`PSM_RUN=1`, D11 enabled, `EN_MAC=0`) with no cleanup writes.

## M3.4D2B — rev42 common initvals sequencing (`HARDWARE RUNTIME PROVEN` on BCM4352)

Canonical status (exact):
- M3.4D2A = HARDWARE RUNTIME PROVEN
- M3.4D2B = HARDWARE RUNTIME PROVEN
- M3.4D2B analysis = COMPLETE
- M3.4D2B implementation = IMPLEMENTED / STATIC TESTED / SIGNED

The decision `CAN COMMON INITVALS BE ISOLATED SAFELY? YES` cleared the design;
the implementation then passed on real BCM4352 hardware (evidence below). This
milestone proves **only** the isolated common-initvals sequence; it does **not**
prove bsinitvals, band init, AC PHY, radio, calibration, channel, RX or TX.

Full analysis: `docs/m34d2b_common_initvals.md`; machine-generated 610/73-record
classification in `docs/m34d2b/initvals_classification.{md,json}`
(`scripts/analyze_initvals.py`, verifies vendor size + sha256 first).
- Call order re-proven from the blob: ucode -> PSM start -> **common
  `d11ac1initvals42`** at `wlc_bmac_init` `0x68b98` (rev42+AC, `0x687c9`) ->
  post setup -> **band init `sub_6656c`** (`d11ac1bsinitvals42` `0x66612` ->
  `wlc_phy_init` `0x669df`).
- Consumer `sub_60f67` (`wlc_bmac_write_inits` `0x60fce`): 8-byte records
  `{u16 offset,u16 width,u32 value}`, terminator `0xffff`, base `*(dev+0xd0)`,
  width2 -> `writew`, width4 -> `writel`, straight-line, order-preserving.
- 610 records = 194 direct + 76 OBJADDR selectors + 340 OBJDATA data.
  Windows: 56 SHM auto-inc (320 writes) + 20 SCR (20 writes); **no** UCM/IHR/
  RCMTA windows. Categories: OBJ 416, TEMPLATE 77, IHR 80, SHM(direct) 33,
  MACINT 3, MAC_CORE 1.
- **No** `MACCONTROL` (`0x120`), **no** DMA (`0x200-0x3d7`), **no** PHY
  (`0x3e0-0x3fe`), **no** radio (`0x3d8-0x3db`), **no** interrupt-source
  enable (`MACINTMASK=0`). Applied with `PSM_RUN=1`, `EN_MAC=0` (exactly the
  D2A exit state); the table changes no MACCONTROL bit.
- All 610 applied unconditionally once selected (single call, constant symbol,
  no branch/patch inside the applier).
- Proven read-back gates for a future D2B test: `M_FIFOSIZE0..3` =
  `01c4/0000/0000/079e`, `MACINTMASK=0`, `MACINTSTATUS=0`.
- Proposed isolated boundary: D2A prep + ucode + PSM start -> common initvals ->
  read-only gates -> **STOP before `sub_6656c`** (no bsinitvals/PHY). Not safe
  to implement until the open UNKNOWNs (PHY `cal_init` ordering, skipped
  pre-steps, `SHM_EN`) are resolved.
- No PHY/radio/channel work in this milestone. Status vocabulary: analysis
  documented; **NOT IMPLEMENTED**, **NOT HARDWARE PROVEN**.

### M3.4D2B follow-up — blocker resolution + GO/NO-GO (still ANALYSIS ONLY)

See `docs/m34d2b_common_initvals.md` (§F1–F14).
- `wlc_phy_cal_init` (`0x6834b`) resolved: **0 MMIO**, PHY *software* state only
  (misleading name); not mandatory for common initvals; D2A succeeded without it.
- Omitted pre-steps classified: none required for the table besides the proven
  D11 clock/core state (coex/BTC/RF-LDO/PHY-sw steps are NOT REQUIRED FOR D2B).
- **`SHM_EN` HARD BLOCKER RESOLVED:** not required for the `OBJADDR` object
  window; vendor applies with `SHM_EN=0` and upstream brcmsmac never sets
  `MCTL_SHM_EN`. Entry state `MACCONTROL=0x04020402`, `PSM_RUN=1`, `EN_MAC=0`.
- Side-effect inventory reconciles exactly to 610; SCR transactions are the PSM
  scratch pad (`S_DOT11_CWMIN=0x1f`, `CWMAX=0x3ff`, `SRC=7`, `LRC=4`,
  `DTIM=0xffff`, ...).
- `IRQ ENABLE EFFECT = NONE`; `DMA ENABLE EFFECT = NONE`; no PHY/radio.
- Postconditions: `M_FIFOSIZE0..3 = 01c4/0000/0000/079e`, `MACINTMASK=0`,
  `MACCONTROL=0x04020402`, SHM `0x14=0xb4` (host `xmtfifo_sz` overwrite noted
  for the full path; isolated path keeps table values).
- Formal decision: **CAN COMMON INITVALS BE ISOLATED SAFELY? YES** — for
  *designing*/*implementing* an isolated test only; it is **not** permission to
  run hardware and **not** a hardware proof. Test design in §F13.

### M3.4D2B implementation + runtime evidence — isolated common-initvals test

`IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / **HARDWARE RUNTIME PROVEN** on
BCM4352. Tested candidate `f27286f6f7e817a58fd1ae6da311cc4281a10a0b`; signed
module SHA256 `1258290cb491ea551a9fb4c4e820ecf3450ae7c23957b41e5eeada14f1d98290`;
base `main` @ `65d61ce`; PR #6. Full detail: `docs/m34d2b_initvals_test.md`.

Runtime evidence (BCM4352, chip rev 3, D11 rev 42):
- `insmod rc=0`; ucode `brcm/bcm43xx-ucode.fw` `size=43400`, `words=10850`,
  upload `writes=10850`.
- D11 prep: `host_is_pcie2=1`, `clkctlst=070b0042`, `HAVEHT=1`, `core_enabled=1`;
  pre-upload `MACCONTROL 00000000 -> 04000404`; PSM start `04020402`.
- PSM poll: `delay=10us`, `max_iter=100000`, `max_total_us=1000000`, PASS
  `iterations=11`, `MACINTSTATUS=0x00000001`.
- Common initvals: `records=610`, `total=610`, `w16=113`, `w32=497`.
- Postconditions: `M_FIFOSIZE0..3=01c4/0000/0000/079e`, `MACINTMASK=00000000`,
  `MACCONTROL=04020402`, `SHM[0014]=000000b4`.
- Final `PASS - stopped before bsinitvals/PHY/radio/channel/DMA`; no
  timeout/BUG/Oops/lockup/reset, no RX/TX DMA, no IRQ bring-up, no PHY/radio/
  channel initialization.
- **Negative boundary (NOT proven):** `d11ac1bsinitvals42`, band
  initialization, AC PHY initialization, radio initialization, calibration,
  channel selection, RX frame reception, TX. Do not broaden this milestone.

Implementation notes (unchanged):
- New mode `initvals_test_only=1`; at most one isolated mode allowed
  (`fw_validate_only`/`ucode_test_only`/`initvals_test_only`); any combination
  returns `-EINVAL` before hardware access.
- Shared hardware-proven D2A core `ob_ucode_run_d2a()` (src/ob_ucode.c) used by
  both D2A and D2B — one copy of the register sequence, no silent divergence.
- Applies exactly the 610 `d11ac1initvals42` records (113 x 16-bit / 497 x
  32-bit), strict order, terminator excluded; write-count invariant
  `total=610 w16=113 w32=497` else `-EIO`.
- Read-only postconditions: `M_FIFOSIZE0..3=01c4/0000/0000/079e`,
  `MACINTMASK=0`, `MACCONTROL=0x04020402`, SHM `0x14=0xb4`; else `-EIO`.
- No `request_irq`, no host IRQ routing, no DMA (no `ob_dma_init`/`ob_rx_init`),
  no PHY/radio/channel, no bsinitvals/`sub_6656c`, no mac80211; dedicated
  `remove()` guard performs no teardown.
- Verified statically: `make` + `make hosttest` (8/8 PASS) +
  `scripts/docs-check.sh` PASS + checkpatch clean + signed as Broadcom Driver
  MOK (sha256). KUnit **NOT EXECUTED** (no runner).
- **HARDWARE RUNTIME PROVEN** on BCM4352 (see the runtime evidence above); the
  isolated test is **ONE SHOT** and must not be repeated.

## M3.4D3 — band-switch initvals + PHY boundary (`ANALYSIS ONLY`)

Status: **`ANALYSIS ONLY` / NOT IMPLEMENTED / NOT HARDWARE PROVEN.**
Last hardware-proven milestone remains **M3.4D2B**. Full report:
`docs/m34d3_bsinitvals.md`; machine-generated classification:
`docs/m34d3/bsinitvals_classification.{md,json}`
(`scripts/analyze_bsinitvals.py`, read-only, deterministic).

- Exact post-common vendor path re-proven in `wlc_bmac_init` (`0x6828a`):
  common applier `0x68b98` -> D11 setup tail (`0x68bab..0x695cb`) -> band-init
  helper `sub_6656c` (`0x695d8`) -> `d11ac1bsinitvals42` -> `wlc_phy_init`
  (`0x669df`).
- Consumer: `sub_60f67` (8-byte records, terminator `0xffff`, width2 ->
  `osl_writew`, width4 -> `osl_writel` at `D11 base + offset`). Table
  `d11ac1bsinitvals42` referenced at `0x66613`; selected when
  `[dev+0x84]==0x2A` (PHY rev 42) and `[[dev+0xE8]+0x1C]==0xB` (AC PHY type).
- Shape reconfirmed: 592 B, **73** records, terminator 73, **39 x 16-bit /
  34 x 32-bit**. All 73 are D11/MAC-side: 68 SHM (`OBJADDR 0x0001xxxx`, no
  auto-inc) + 5 direct IHR (`0x680/0x682/0x684/0x686` IFS, `0x700` NAV).
  Side-effect accounting reconciles to 73 (SHM state 34, selector 34, timing 4,
  NAV 1). `IRQ ENABLE EFFECT = NONE`; `DMA ENABLE EFFECT = NONE`; no PHY/radio.
- C3 (`brcmsmac/main.c brcms_c_ucode_bsinit`): band-switch initvals are the
  "band-specific ucode IHR, SHM, and SCR inits", applied with the band's MHF
  host flags immediately before `wlc_phy_init`; called on initial bring-up and
  on band switch (same table for 2.4/5 GHz; band values written separately).
- Common vs band-switch: SHM-only for bs; no direct-offset overlap; **3 shared
  SHM bytes overridden** by bs (`0x0010=0x14`, `0x001c=0x183`,
  `0x0094=0x1f4`).
- Real PHY boundary (corrected): `wlc_phy_init` (`0xbabf5`) ->
  `wlc_phy_anacore` (`0xbac84`, first PHY indirect write via `D11+0x3fc/0x3fe`)
  -> `wlc_phy_switch_radio` (`0xbad44`) -> `wlc_phy_switch_radio_acphy`
  (`0xaa782`, first radio-window writes via `D11+0x3d8/0x3da`) ->
  `call *[pi+0x28]` = `wlc_phy_init_aphy` (`0x8c3f9`, installed at `0x899dd`).
  **The earlier claim that the full vendor path calls `wlc_phy_switch_radio` at
  `0x69594` before band init was wrong for AC:** that call is inside
  `if ([[dev+0xE8]+0x1C] == 7)` (NPHY/HT only) and `wlc_bmac_mute` `0x6957b` is
  skipped (`wlc_bmac_init` arg#3 = 0).
- **Formal PHY/RF decision: `CAN A VENDOR-ORDERED BSINITVALS TEST STOP BEFORE
  REAL PHY/RF WRITES? YES`** for BCM4352/AC (no PHY-indirect/radio writes before
  bsinitvals). The isolated unit is the full vendor prefix, not the 73 records
  alone.
- **SECOND CORRECTION (Appendix B): the post-common tail is NOT D11-only.**
  For rev42 the legacy `xmtfifo_sz`/`M_FIFOSIZE`/TX-flush block is **skipped**
  (`phyrev <= 0x27`; the rev42 FIFO stage is `sub_67efd`, safe), but the tail
  also writes interrupt-source config (`intrcvlazy[0]`,
  `intctrlregs[0].intmask=I_RI`) and initializes DMA engines.
- **THIRD CORRECTION (Appendix C): the DMA/IRQ stage fully reversed.** Only
  **4** TX channels exist (`dma_txinit` x4, di[0..3]); TX enabled but **idle**
  (no descriptors posted). FIFO0 RX enabled (`control=0x84D`) with 64 buffers
  posted (idle). Host IRQ delivery **not possible** here (`macintmask=0`,
  `wl_intrsoff` active; `wl_intrson` only in `wlc_bmac_up_finish` after
  `wlc_phy_init`). Vendor quiesce = `wlc_coredisable` before freeing.
- **Formal decomposition A/B/C/D = YES** (report §C.18): D3A0 (vendor DMA/
  IRQ-source, host route off) -> D3A1 (remaining tail) -> D3B (band init + 73
  bsinitvals) -> D4 (PHY). PHY/RF boundary = YES.
- **D3A0 blocker closure (Appendix D):** 4-TX map (BK/BE/VI/VO @ 0x200/0x240/
  0x280/0x2C0); TX CONTROL = RMW `read(control) | XE | (PD?)`; `ddoffsethigh =
  dataoffsethigh = 0x80000000`; `intrcvlazy[0] = 0x01000000`; `dma_txreset
  0xf64a` / `dma_rxreset 0xf5ef`. **Vendor order: IRQ-source config
  (`intrcvlazy`→`macintstatus`→`intctrlregs[0].intmask=I_RI`) BEFORE DMA init.**
  Quiesce = per-channel reset **with verification** (`macintmask=0`, clear
  `I_RI`, `dma_rxreset`, `dma_txreset` per initialized channel); core reset is
  containment fallback and never authorizes a free after unverified reset.
  **`D3A0 IMPLEMENTATION GO: YES`** (report §D.19), still NOT IMPLEMENTED /
  NOT HARDWARE PROVEN.
- Resolved: `MACCONTROL` bit30 = `MCTL_DISCARD_PMQ` (`0x69047`
  `mctrl(mask=0x40060000, val=0x40020000)` -> `0x44020402` from the D2B state);
  `macphyclk_set` = D11 core cflags bit4 (`SICF_MPCLKE`); `switch_macfreq`
  writes D11 `0x62e/0x630` (TSF clock frac) from the PMU BB VCO.
- Smallest faithful boundary (design only, do not implement): D3A0 DMA/IRQ-source
  -> D3A1 tail -> `sub_6656c` through the 73 bsinitvals records, STOP before
  `wlc_phy_init`. OpenBRCM gaps: `ob_dma_quiesce`, 4-channel TX programming,
  out-of-band IRQ route. See report §17/§18 and Appendix C.

## M3.4D3A0 — isolated DMA lifecycle test (`HARDWARE RUNTIME PROVEN` on BCM4352)

**D3A0 TYPE: `ISOLATED DMA LIFECYCLE TEST`** — not a full vendor-prefix
reproduction; `sub_67efd` (TXE0/FIFO fixup) and the runtime NVRAM/BTC/rate/
power SHM tail are not pinned and are omitted (D3A1 integration content).

Canonical status (exact):
- M3.4D2A = HARDWARE RUNTIME PROVEN
- M3.4D2B = HARDWARE RUNTIME PROVEN
- M3.4D3 analysis = COMPLETE
- M3.4D3A0 = HARDWARE RUNTIME PROVEN

Status: **`IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE RUNTIME PROVEN`
on BCM4352** (candidate `4fa1b57`, runtime commit `7e68fe24`, signed module
SHA256 `0282d9b253b40ca13eba3420058b6314be629cdf50d540e549510f726cd6af08`,
kernel `7.0.0-34-generic`). The run proved the complete isolated lifecycle
allocate/map → program → hardware validation → verified stop → release:
`bring-up validation PASS`; `RX reset PASS` + `TX0..TX3 reset PASS` +
`all DMA engines stopped` + `rings released` + `PASS - bring-up + teardown
proven`; no BUG/Oops/WARNING/DMA-API error/lockup/reset timeout/FATAL. Module
param **`dma_test_only=1`**, mutually exclusive with the other isolated modes.
Files: `src/ob_d3a0.{c,h}`, `tests/host/ob_d3a0_test.c`,
`tests/kunit/ob_d3a0_kunit.c`. Evidence: `docs/m34d3a0_dma_test.md`.
- **Hard blocker found and fixed by the pre-hardware static audit:** the first
  cut ran only `ob_ucode_run_d2a()` (D2A) before the DMA prefix and skipped the
  610 common initvals. It now runs the shared `ob_initvals_run_d2b()` (proven
  D2A core + exactly 610 records, 113 w16 / 497 w32 + postcondition gate), then
  a live `ob_d3a0_check_d2b_exit` (`MACCONTROL=0x04020402`, `MACINTMASK=0`,
  FIFO=`0x01c4/0/0/0x079e`, `SHM[0x14]=0xb4`) before the first post-common
  write. No D2A-only path remains; `ob_ucode_run_d2a()` is not duplicated.
- Programs the pinned D11/clock/IRQ-source prerequisites before DMA
  (`MACCONTROL 0x04020402→0x44020402`), the four TX DMA channels
  (0x200/0x240/0x280/0x2c0; 512x16 B; 8192 align; `ADDRHIGH=0x80000000`;
  `control = read|XE|PD`; no ptr/descriptors → zero TX payload mappings) and
  FIFO0 RX (0x220; 256 desc; exactly 64 posted 2048-B buffers; `CONTROL=0x84d`;
  `PTR=0x400`).
- Host IRQ delivery impossible (`MACINTMASK=0`, no `request_irq`, no
  `bcma_host_pci_irq_ctl`, no `MI_DMAINT`); `EN_MAC=0`.
- **Fail-closed quiesce (corrected):** free only after EVERY programmed engine's
  own verified normal per-channel reset (`engines_stopped` + `free_allowed`).
  `bcma_core_disable` containment is attempted after a reset failure and its
  real `bcma_core_is_enabled()` readback recorded as `core_contained`, but it
  **never authorizes a free**: the path sets `fatal`, latches, records the
  retained rings, pins the module and keeps probe successful so the bound device
  retains the state; reboot required. `ob_remove()` honours this and never frees.
- STOPS before remaining D3A1 / band init / bsinitvals / `wlc_phy_init` / PHY /
  radio / channel / mac80211.

## M3.4D3A1 — vendor post-common / pre-PHY tail test

Canonical status (exact):
- M3.4D3A0 = HARDWARE RUNTIME PROVEN
- M3.4D3A1 = IMPLEMENTED / STATIC TESTED / SIGNED / HARDWARE RUNTIME PROVEN
  (isolated `d11_tail_test_only=1`; candidate `42d74b8`, module `6ba2d853…`;
  normal unload + DMA teardown + STOP boundary proven). **POSTCONDITION
  ERRATUM (2026-09):** the `tsf_cfpstart` (D11 `0x18c`) equality gate was
  invalid (write-only CFP-start programming register; readable value at
  `0x604/0x606`); it is removed and replaced by write-accounting + diagnostics.
  The other postconditions, the vendor sequence and the teardown remain proven.
  See `docs/m34d3a1_vendor_tail_test.md` §17.
- M3.4D3B = `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE ATTEMPTED` /
  NOT HARDWARE PROVEN / **platform CRASH** (isolated `bsinitvals_test_only=1`
  band init + `d11ac1bsinitvals42`); design `docs/m34d3b_band_init.md`,
  implementation `docs/m34d3b_band_init_test.md`, post-mortem
  `docs/m34d3b/d3b_crash_postmortem.md`. **Attempt 1 (frozen candidate
  `5fa5e5b`, module `3a10aff9…`)** reached D2A/D2B/T1/DMA/T2/`switch_macfreq`
  and the verified teardown, but failed the pre-D3B `tsf_cfpstart` postcondition;
  no bsinitvals record was applied. **Attempt 2 (corrected candidate
  `2917ca9`, module `88971029…`)** reached the D3B slice: MHF1..5 written and
  **all 73 `d11ac1bsinitvals42` records applied** (`73/39/34`), then ~4.84 s
  later every post-D3B D11 read returned `0xffffffff`; validation failed, the
  driver entered `ob_d3a0_teardown()` and the platform reset with an AMD
  **data-fabric sync flood** (`0x08000800`). The D3B postconditions were **not**
  proven; the D3A0 teardown is unsafe after MMIO loss. `HARDWARE RETEST: NO`.
  `D3B IMPLEMENTATION GO: YES` — `VALUE FULLY PROVEN`: all MHF write expressions,
  gate semantics and all five band-0 MHF values are resolved —
  `mhfs[0..4] = {0x0100, 0x0000, 0x0000, 0x0000, 0x0080}` (MHF1/MHF2/MHF4/MHF5
  closed earlier; **MHF3** closed by the 2026-09 read-only hardware SPROM
  capture; `docs/m34d3b_band_init.md` §3.5/§3.8). No value/provenance blocker
  remains, but a new **safety** blocker exists: device-loss handling.
  **BOUNDARY RETIRED (2026-09):** STOP-before-`wlc_phy_init` is **not a
  vendor-stable state** (vendor goes straight from `sub_60f67` at `0x669bd` to
  `wlc_phy_init` at `0x669df`; upstream `brcms_b_bsinit` does the same). D3B is
  retired as a standalone hardware milestone. It is replaced by the proposed
  **M3.4D3+4** (band init + minimal D4 prefix through `wlc_phy_init`). A
  central **device-lost fail-safe** (`hw->dev_lost`, monotonic) is now
  `IMPLEMENTED` / `STATIC TESTED`: once a trusted direct D11 read is all-ones,
  no further MMIO/free/retry occurs and reboot is required. See
  `docs/m34d3b/d3b_crash_postmortem.md` Part A/B.
- M3.4D3+4 (band init + minimal D4 prefix) = **PROPOSED / NOT STARTED /
  NOT HARDWARE PROVEN**; proposed STOP = after `wlc_phy_init` returns
  (`0x669e4`); exact scope and bounded teardown are not yet analysed.
- M3.4D4A (AC-PHY `wlc_phy_init` decomposition) = **`ANALYSIS ONLY`**
  (2026-09): `wlc_phy_init` (`0xbabf5`) decomposed into phases D4.0..D4.6 with
  callee table, first-op trace and sync points; artifact
  `docs/m34d4/wlc_phy_init_rev42_flow.json`, report
  `docs/m34d4/d4a_decomposition.md`. Earliest vendor-stable checkpoint = **after
  `wlc_phy_init` returns** (CP-F). Unknown-blocker write values remain (indirect
  PHY/radio opcode tables, `[pi+0x28]`/`[pi+0x118]` targets), so
  **`D4 IMPLEMENTATION GO: NO`** and `HARDWARE TEST GO: NO`. The `dev_lost`
  BAR-MMIO invariant was re-audited and holds.
- **RE TOOLING D4 ACCELERATION** (canonical `re` v3 + `re.db` schema v3,
  `iced/test/binary_analyzer`): whole-blob `re mmio` / `re imm`,
  `re field-writers`, `re indirect` (candidate + confidence
  EXACT/CONDITIONAL/UNRESOLVED), `re table` / `re regtables`,
  `re const`, `re phyops`, `re dump --json`, and `re regress`.
  `re regress` **PASS** (initvals 610/113/497, bsinitvals 73/39/34 exact;
  sub_67efd 0x530/0x540; switch_macfreq 0x62e/0x630; DMA/MAC facts).
  Status `IMPLEMENTED` / `STATIC TESTED` (db rebuild 3.5 s; db 17.1 MB).
- **M3.4D4A v2** (re-run with the new tooling) = **`ANALYSIS ONLY`**: the two
  previously-unresolved targets are located; `[phy+0x28]` is proven **zero for
  rev42 AC** (`wlc_phy_attach_acphy` `0xa3001` unconditional store), so the
  whole `wlc_phy_init` body is skipped at band init (`je 0xbaecE`), and
  `[phy+0x118]` is `UNRESOLVED` but unreachable on the AC path. Therefore
  `wlc_phy_init` is **not** the AC PHY-init point; the real AC init is
  `wlc_phy_attach_acphy` (+ acphy callees). CP-F is a stable return boundary but
  **not** an AC PHY-init checkpoint. `D4 IMPLEMENTATION GO: NO`; scope must be
  re-derived from `wlc_phy_attach_acphy`. Artifacts:
  `docs/m34d4/{wlc_phy_init_rev42_flow_v2,phy_operations_rev42,radio_operations_rev42}.json`,
  `docs/m34d4/d4_checkpoint_analysis_v2.md`.
- **M3.4D4B — AC-PHY initialization lineage** = **`ANALYSIS ONLY`** (2026-09,
  tool-first): `wlc_phy_attach_acphy` (`0xa194f`) is proven to be
  **software object construction + capability discovery + board/NVRAM/OTP
  parsing** (0 MMIO writes, 11 `phy_reg_read`, 238 field writes, 1 callback
  install `btc_adjust@+0xF8`); it zeroes the generic `+0x28` (init) and `+0x30`
  (cal) callbacks, so `wlc_phy_init`/`wlc_phy_cal_init` are **no-ops for rev42
  AC**. The AC hardware programming is in the caller `wlc_phy_attach`
  (`0xbe426`, called from `wlc_bmac_attach`) → `wlc_phy_anacore` (first write,
  D11 `0x3e6`) → direct `0x3d8/0x3f6` window writes → `wlc_phy_switch_radio`
  (`0xba395` AC branch) → `wlc_phy_switch_radio_acphy` (**62 literal radio
  RMW/write ops + delays + enable-MAC**, not a table/opcode stream). AC init
  runs **at probe/attach**, not at channel up. Earliest true checkpoint =
  **CP-A0** after `wlc_phy_attach_acphy` returns (pre-hardware, post-object);
  first "PHY initialised" = **CP-A3** after `wlc_phy_attach` returns.
  Unknown runtime-derived write values (`sub_a4adc`/`sub_9591e` `val=?`,
  `si_pmu_otp_power`) ⇒ **`D4 IMPLEMENTATION GO: NO`**, `HARDWARE TEST GO: NO`.
  Artifacts `docs/m34d4b/{acphy_attach_callgraph,acphy_function_table,acphy_hw_init_flow,acphy_phy_ops,acphy_radio_ops,acphy_tables}.json`,
  `docs/m34d4b/acphy_init_analysis.md`.
- **M3.4D4B Ghidra augmentation** = **`ANALYSIS ONLY`** (2026-09): Ghidra 12.1.3
  headless (decompiler + reference manager) applied only to the PARTIAL/
  CONDITIONAL/UNRESOLVED D4B facts. **Resolved the DMA TX base gap**: the bases
  are literal `dev+0x200/0x220/0x240/0x260` (`bmac rev>10` →
  `+0x240/0x280/0x2c0`) passed to `dma_attach`, and the per-engine writes are
  `sub_f947`/`sub_fa57` reached via the `dma64proc+0x08` vtable (the `re const`
  `0x240/0x280/0x2c0` were linear-scan artifacts). **Confirmed** (no change):
  `phy+0x118`/`+0x110` have no installer (null → fallback); `wlc_phy_init` is
  never installed as a callback; `wlc_phy_cals_acphy` is not on the attach path.
  **Corrected method**: `phy+0xF8` *is* `wlc_phy_btc_adjust_acphy` (the
  `movq $0,0xf8(%rbx)` look is the relocated `imm32`; `R_X86_64_32S` at
  `0xa3059`), and `+0x38/0x40/0xC0/0xC8/0xD0/0x100` are also zeroed. Ghidra is
  now **integrated into the persistent RE workflow**: `scripts/ghidra_headless.sh`
  + `scripts/ghidra/{Decompile,Refs,Vtable}.java`, documented in
  `docs/re-tooling.md` §1.1, checked by `scripts/re-bootstrap.sh`, and required
  by `AGENTS.md` §7 before manual disassembly. Artifacts
  `docs/m34d4b/ghidra_augmentation.{md,json}`; `D4 IMPLEMENTATION GO: NO`
  (unchanged), `HARDWARE TEST GO: NO`.
- **M3.4D4B Phase 0 — `re` false-positive fixes** (`ANALYSIS ONLY`, tooling
  commit `f334e21`): (A) **relocation-covered immediates** are no longer literal
  zero — `text_relocs()` + a `V::R` symbolic variant; `phy+0xF8` resolves to
  `&wlc_phy_btc_adjust_acphy` (`R_X86_64_32S` @`0xa3059`). (B) **linear-dataflow
  false exactness** removed — a value is `EXACT` only with a single dominating
  definition before any conditional branch, else `CONDITIONAL` + candidate set;
  `const_props` for `dma_attach` now has 0 `EXACT` rows. `re.db` schema **v4**
  (adds `confidence`/`candidates`); `re regress` PASS with the two new
  fixtures. Report `docs/m34d4b/re_false_positive_fixes.md`.
- **M3.4D4B resume — actual AC-PHY init lineage** = **`ANALYSIS ONLY`**:
  `wlc_phy_attach_acphy` = software+board+caps with **1** live callback
  (`+0xF8`→`wlc_phy_btc_adjust_acphy`, consumed by `wlc_phy_watchdog`); the
  generic vtable slots `+0x28/+0x30/+0x38/+0x40/+0xC0/+0xC8/+0xD0/+0x100` and
  `+0x110/+0x118` are **INTENTIONALLY_NULL** (construction-proven); **UNRESOLVED
  = 0**. Earliest substantial AC hardware entry = `wlc_phy_switch_radio_acphy`
  (`0xaa782`) via `wlc_phy_attach` → `wlc_phy_switch_radio` (radio **off** at
  attach); first hardware op = `wlc_phy_anacore` (`0xbabee`, D11 `0x3e6`). The
  post-bsinitvals continuation for AC is **band/MAC SHM** (`wlc_phy_init` is a
  no-op: `+0x28`=0 → `je 0xbaecE`), not PHY programming. Path counts (not
  whole-blob): PHY 186, RADIO 301, PHY_TABLE 3, `osl_delay` 17. Calibration is
  **not** on the attach/band path. Earliest stable state after mandatory setup =
  **CP-A3** (end of `wlc_phy_attach`, `STRONG proposed`, no live hardware
  postcondition yet); `dev_lost` coverage is structurally coverable by the
  existing monotonic invariant. `D4 IMPLEMENTATION GO: NO` (runtime-derived
  `val=?` on the executed callbacks), `HARDWARE TEST GO: NO`. Report
  `docs/m34d4b/d4b_resume_analysis.md`, updated
  `docs/m34d4b/acphy_function_table.json`.
- **M3.4D4B value-provenance closure + CP-A3 proof** = **`ANALYSIS ONLY`**:
  the three "UNKNOWN executed write values" are **not executed on the initial
  attach path**. `wlc_phy_attach` enters the **radio-OFF** branch of
  `wlc_phy_switch_radio_acphy` (`xor esi,esi` -> `wlc_phy_switch_radio(phy,0)`);
  `sub_9591e` and `sub_a4adc` (via `sub_a7089`/`sub_a04c2`, gated by
  `phy+0x32d`) live only in the **radio-ON** branch. `sub_a4adc` is
  `rx_farrow_tbl*`-table-driven; `sub_9591e` is a static-descriptor radio
  calibration loop; `si_pmu_otp_power` is a conditional RMW
  (`old|(0x100|v)` / `old&~(0x100|v)`). The executed attach write set is
  **12 PHY writes + 8 radio RMWs**, all `STATIC`; the NPHY-only `0x3d8` direct
  block is skipped for AC. `UNKNOWN = 0`. **CP-A3 = `LOGICALLY STABLE /
  NOT DIRECTLY OBSERVABLE`** (radio OFF, PSM not running, channel-independent);
  the operational state requires the later radio-ON checkpoint. `dev_lost`
  coverage complete structurally (wire the 1 `0x3e0` + 11 `phy_reg_read`
  sites). `D4 IMPLEMENTATION GO: NO`, `HARDWARE TEST GO: NO`. Report
  `docs/m34d4b/d4b_value_provenance_closure.md` +
  `docs/m34d4b/d4b_value_provenance.json`.
- M3.4D3B SPROM-evidence capture (branch `m34d3b-sprom-evidence`, PR #14)
  = `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / **`HARDWARE RUNTIME PROVEN`**
  (BCM4352, 2026-09, frozen candidate `739273c`, `openbrcm.ko` sha256
  `538588e2…`): the existing read-only SPROM path emits the already-read,
  CRC-validated 234-word rev11 image via the isolated `sprom_evidence_only=1`
  mode (`ob_si_emit_sprom11()`; zero extra MMIO). Decoded `antsel_type = 0`,
  **MHF3 = `0x0000`**. Evidence `docs/m34d3b/d3b_sprom_capture.json`,
  `docs/m34d3b_sprom_evidence.md`.
- M3.4D3B rev11 field map (branch `m34d3b-rev11-field-map`, PR #15) = tooling
  gap **T7 closed**: `srom_var_init 0x9704` walks a 24-byte descriptor table at
  `.rodata+0x1b00` (not `srom_parsecis`); tool `scripts/srom_var_table.py`,
  artifact `docs/m34d3b/rev11_sprom_fields.json`.
- M3.4D3B band-init implementation (branch `m34d3b-band-init-test`, PR #17,
  Draft) = `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE ATTEMPTED` /
  NOT HARDWARE PROVEN: isolated `bsinitvals_test_only=1` reuses the D3A1 prefix
  with the D3A0 DMA engines left live (`ob_d3a1_run_prefix()`), writes MHF1..5
  derived from the rev11 board fields, applies 73 `d11ac1bsinitvals42` records
  (39 x w2 + 34 x w4), validates the deterministic postconditions and performs
  the verified D3A0 teardown; STOPS before `wlc_phy_init`. **Failure history:
  attempt 1 stopped at the pre-D3B `tsf_cfpstart` postcondition (fixed); attempt
  2 applied all 73 records, then the D11 window read `0xffffffff` and the
  platform crashed during `ob_d3a0_teardown()` (AMD data-fabric sync flood).**
  Files `src/ob_d3b.{c,h}`; proof
  `docs/m34d3b_band_init_test.md`; post-mortem
  `docs/m34d3b/d3b_crash_postmortem.md`.

Reports: analysis `docs/m34d3a1_vendor_tail.md` (read-only RE of blob
`352a6e349f…`); implementation `docs/m34d3a1_vendor_tail_test.md`. Isolated
mode `d11_tail_test_only=1` reproduces the exact rev42 vendor sequence after
  the common initvals and STOPS before real PHY init. New code:
`src/ob_d3a1.{c,h}`, `tests/host/ob_d3a1_test.c`, `tests/kunit/ob_d3a1_kunit.c`.
Module built + MOK-signed and hardware-run for the D3A1 tail; the
`tsf_cfpstart` postcondition was corrected 2026-09 (§17).

**D3B attempt-2 candidate (corrected, NOT HARDWARE PROVEN, CRASHED):** commit
`ffa2a548911007b58605df51150e24d422d6d957` (PR #17 HEAD `2917ca9`), signed
`openbrcm.ko` sha256
`889710295f2e1c1c80088d333c244220c32127fda7cbc8644cca67c09ab2e1ed`; attempt 1
was frozen candidate `5fa5e5b` (module `3a10aff9…`). Both candidates are
retained in the frozen-candidate history; **do not retest** until the device-loss
mechanism is bounded (`docs/m34d3b/d3b_crash_postmortem.md`).
- **Ordering:** the vendor **interleaves** DMA inside the tail —
  `T1 (sub_67efd → MBURST/MAXANTCNT → intrcvlazy → MACCONTROL → TSF →
  intctrlregs → macphyclk → fastpwrup → MACHW_VER/CAP → SCR/SFBL/ifs) →
  DMA (4× txinit + rxinit + rxfill) → T2 (BTC/NVRAM SHM → 0x78c/0x78e/0x790 →
  switch_macfreq) → STOP before sub_6656c`. Design A (tail→DMA) and Design B
  (DMA→tail) are both non-vendor-faithful.
- `sub_67efd` re-proven for rev42: RXE block gated `phyrev>0x2a` (not executed);
  flush/cmd + 7-entry (42 writes) + 42-entry (168 writes) loops; no DMA/IRQ/PHY.
- `MACCONTROL` one transition `0x04020402 → 0x44020402`; IRQ source
  `INTRCVLAZY=0x01000000`, `MI_GP1` W1C, `intctrlregs[0].intmask=I_RI`,
  `macintmask` stays 0; first PHY op `wlc_phy_anacore` (`0xbac84`).
- D3A1 boundary: `sub_67efd` (`0x68bab`) … `wlc_bmac_switch_macfreq`
  (`0x695cb`), STOP before `sub_6656c` (`0x695d8`).
- **Blocker closure (§15):** the `0x78c/0x78e/0x790` six bytes are
  `wlc_pub+8` = `cur_etheraddr` (device MAC); `r13 = wlc_info`,
  `r13+0x20 = wlc_info->hw`; the SCR `0x24` write is a read-modify-write of
  SCR `0x24`, **skipped on the first init** (`wlc_info+0x718 == 1` set by
  `wlc_info_init`); `getvar` scans a `name=value` per-hw buffer then the global
  `nvram_get` list (`srom_var_init` + `nvram_init`/`nvram.txt`);
  `btc_params`/`btc_flags` absent on ASUS PCE-AC56 ⇒ **skip** (no zero-fill);
  `M_MAX_ANTCNT = 0x0a` = upstream vanilla `ANTCNT`.
- **Implementation:** exact `sub_67efd` (2 + 42 + 168 = 212 writes), exact T1
  order, DMA reused from D3A0 in vendor position, T2 (`btc_base` gate; absent
  `btc_params`/`btc_flags` ⇒ skip; MAC into `0x78c/0x78e/0x790` only inside the
  `btc_base != 0` gate), `switch_macfreq`, deterministic postconditions,
  fail-closed quiesce; `STOPPED BEFORE sub_6656c / bsinitvals / PHY`.
- Build/sign: `make` + `make signed` OK; module `ce9b7cc5…` signed
  (`Broadcom Driver MOK`), vermagic `7.0.0-34-generic`. Host tests 10/10 PASS.
- Audit corrections: the isolated dispatch prepares `hw->cc` + the validated
  external-SPROM MAC (`ob_si_prepare_board_data_for_d3a1`) before
  `ob_d3a1_test`; the `switch_macfreq` BB-VCO / 64-bit divide are exact verbatim
  ports (no best-effort), and an un-derivable VCO is an error, never a PASS.
- **First hardware attempt** (candidate `1186a9b`, module `ce9b7cc5…`):
  board-data/D2A/D2B PASS; `sub_67efd` `0x530` index 0 programmed `0x8007`,
  readback `0x0007`, aborted under the then-current fail-closed poll.
- **0x530/0x540 re-derivation**: the blob `0x530` predicate is the whole 16-bit
  word reading 0 (`test %ax,%ax`), NOT a `0x8000` mask; `0x540` is bit0 clear;
  both loops have **no error path** and continue on bound expiry. Corrected:
  expiry is non-fatal (logged), and `x532 = min(42-idx,3)` (the prior
  `(idx==0?1:0)` misread the decrementing loop counter). Corrected module
  `6ba2d853…` (4170009 B, srcversion `011D0C80396320496A86768`).
- Pre-freeze corrections: `switch_macfreq` checks `hw->cc` *before* the PLL
  read; `ob_si_read_mac()`'s errno is preserved by board-data preparation; the
  FIFO timeout logs include the final readback; and the first run records raw
  PLL2/PLL3, `d`, `den`, VCO, TSF fraction and `0x62e`/`0x630` (readback
  classified UNPROVEN, no equality gate).
- Only the symbolic name of the `0x78c/0x78e/0x790` SHM slots remains UNKNOWN
  (value/source proven; microcode-only consumer).
- **HARDWARE RUNTIME PROVEN** (candidate `42d74b8`, module `6ba2d853…`): one-shot
  isolated `d11_tail_test_only=1` on BCM4352 (kernel `7.0.0-34-generic`);
  `sub_67efd` ran with `0x530`/`0x540` expiry non-fatal, T1/DMA/T2 in vendor
  order, postconditions validated, **normal `rmmod` + verified DMA teardown +
  STOP before `sub_6656c`/bsinitvals/PHY**; no kernel fault. Proof:
  `docs/m34d3a1_vendor_tail_test.md` §14.1.
- **Next:** D3B (band init / `d11ac1bsinitvals42`) → D4 (AC PHY bring-up). D3B
  is analyzed in `docs/m34d3b_band_init.md`; the pre-bs helper `sub_62766` is
  newly pinned as `wlc_bmac_write_mhf` (MHF1..5). D3B
  `IMPLEMENTATION GO: CONDITIONAL` on the band-0 MHF values; D4 remains not
  started.

## M2.5b — eliminate the BCM4352 power-up Oops (historical)
Symptom: `BUG: kernel NULL pointer dereference, address 0x…0c` at
`bcma_core_pci_power_save+0x25` (`RAX=0`), called from `ob_si_powerup`.
- **Root cause:** `bcma_core_pci_power_save()` only guards
  `bus->hosttype != BCMA_HOSTTYPE_PCI`. On a PCIe **Gen2** bus `hosttype` is
  still `BCMA_HOSTTYPE_PCI`, but the legacy per-core state
  **`bus->drv_pci[0].core` is NULL** (Gen2 uses `bus->drv_pcie2`), so
  `pc->core->id.rev` faults at offset `0xc`.
- **Fix:** the legacy call is **removed**. Host bring-up now uses the public
  host abstraction **`bcma_host_pci_up(bus)`**, which checks `hosttype` and
  dispatches on `bus->host_is_pcie2` to `bcma_core_pcie2_up()` — the correct
  path for this bus (exported GPL; declared in `include/linux/bcma/bcma.h`).
- **Instrumentation:** read-only topology dump (`hosttype`, `host_is_pcie2`,
  `host_pci`, `drv_pci[0/1].core`, `drv_pcie2.core`, d11 `core_index/addr/wrap`)
  and separately labelled stages A (host up), B (d11 enable/reset), C (HT clock),
  D (OTP/SPROM). Bounded polling only.
- This does **not** claim to fix the SPROM shadow; that is a separate item.

## M2.5c — OTP diagnostic probe (historical; SUPERSEDED by M2.5f/g — OTP not used for the MAC)
Two earlier claims were **wrong** and are corrected here: "`si_pmu_otp_power` is a
no-op for 0x4352" (it is not), and "the OTP divisor comes from `readl(CC+0x10)`"
(that register is `BCMA_CC_OTPS`, OTP **status**, not geometry).

- **`si_pmu_otp_power` (0x12a8d), case 0x4352 → 0x12cb2**: mask `0x100`; power on
  = RMW `CC+0x618 |= 0x100`, `delay(1ms)`, bounded poll `CC+0x60c` bit `0x100`;
  power off = clear the bit. `si_pmu_is_otp_powered` checks `CC+0x60c` bit 8.
- **`srom_read` false branch** (`0x9284`) calls
  **`otp_read_region(sih, 1, buf, &size)` with `size = 0x180 = 384` words** —
  matching the 96×64×384 geometry. `otp_read_region` (0x33ff) calls `otp_init`
  then `ops->read_region`.
- **`otp_init` (0x329f)** selects one of two ops tables by `sih+0x14`:
  table A (`.data+0x60`, `>0x16` or `==0x15`) and table B (`.data+0xd0`,
  `==0x16` or `<=0x14`). **Table B's init (0x3bfe) derives the geometry from the
  ChipCommon CAP selector**: `sel = (readl(CC+0x4) & 0x380000) >> 19`, aborting
  if zero. This is the authoritative, non-circular source. (Table A's big inlined
  init at 0x3d6a instead seeds a divisor from `CC+0x10` — the legacy arm our
  earlier code mistakenly copied.)
- **Geometry (recovered):** the blob's per-chip switch (0x3d6a, OTPP[18:16]==0
  arm) writes, for selector 5, `rows=0x60(96)`, `cols=0x40(64)`,
  `words=0x180(384)` (0x3edf..0x3ef1); selectors 1/2/7 give 32×64, 64×64, 16×64.
  This **matches the historical Broadcom IPX OTP table** (independent
  corroboration). BCM4352 `CC_CAP=0x58680001` → selector 5.
- **OTP bit reader (0x38d3)**: `OTPC(0x14)=0`, `OTP_CFG(0xf4)=0`, read `OTPP(0x1c)`,
  `row = bitaddr/cols`, `col = bitaddr%cols`,
  `OTPD(0x18) = 0x80000000 | (row<<8) | (col&0xff)`; poll while bit31 (bounded),
  `READERR` = bit28, data = bit29. Writes only OTP control registers — never a
  programming-enable bit.
- **OTP init command** (blob 0x3fa3, non-OTPP[18:16]==1 arm): `OTPD=0x84000000`,
  poll while bit31 (bounded), issued before the first read.
- OTP power was already on at runtime (`was_powered=1`), so power is not the
  blocker; the traditional `0x840` shadow stays `ffffffff`, so the direct OTP
  read is the primary path.

`ob_si_otp_diag()` (opt-out via `otp_diag=0`) logs the before state, powers OTP,
re-reads the `0x840` shadow, derives the geometry from the CAP selector (no
circular word read), issues the init command, reads OTP words `0x26..0x28` with
`divisor=cols`, logs per-bit failure detail (bit offset, row, col, OTPD command,
OTPP/OTPD values, BUSY/READERR/VALUE/timeout), validates both byte orders with
`is_valid_ether_addr`, and does **not** replace the mac80211 address.

## M2.5a — instrumented power-up (previous)
Goal: obtain the real factory MAC and understand the HT capability value.
- **Implemented (provenance-backed):** instrumented power-up in `ob_si_powerup()`
  logging initial PMU/clock/OTP/SPROM state; PCIe/core wake + core reset via
  **bcma** (`bcma_core_pci_power_save`, `bcma_core_enable` — the kernel
  implementation of the recovered `ai_core_reset`); HT clock via
  `bcma_core_set_clockmode` (FORCEHT + HAVEHT poll); bounded shadow polling
  (500 ms, no infinite waits); raw MAC words + decoded MAC.
- **HT fix:** `rx_highest` corrected from 144 to **300 Mbps** — derived from our
  recovered formula (MCS7 × 2 streams × 40 MHz × SGI), consistent with the
  advertised MCS 0-15 / 2 streams / HT40. (Runtime previously reported 144.)
- **UNKNOWN / blocker:** the exact BCM4352 ChipCommon **OTP read FSM**
  (`otp_read_word` variants) and the 0x4352-specific PLL branch are not fully
  recovered. No guessed register writes are performed. See the project rule:
  "if a required bit/sequence is not provenance-backed, stop and mark UNKNOWN".
  Consequence: if the SPROM shadow is not populated by the chip's own power-on
  reset, the MAC is unavailable and remains `00:00:00:00:00:00`.

## Current state — M2.5 COMPLETE (runtime-proven)
On the real ASUS PCE-AC56 / BCM4352: bcma bind, PCIe2 host bring-up, D11 core
enable, FAST clock/HAVEHT, and mac80211 registration all work.
- The authoritative board-data source is the **external SPROM**; OTP is **not
  required** for the MAC on this board (`SROM_CONTROL` selects external,
  `OTPS.GU_PROG_HW=0`).
- External SPROM validates at **234 words, rev 11, CRC stored=calculated=0xc0**.
- Rev11 `IL0MAC` offset is **+0x90** (mainline bcma wrongly applies rev8 +0x8C).
- Permanent MAC = **2c:fd:a1:61:40:25**, exposed by both `iw dev` and
  `/sys/class/net/wlp33s0b1/address`.
- On-chip OTP is reachable only via the opt-in `otp_diag` (default off).
Next (historical; both corrections below were accepted and implemented in
M3.4B): **M3**, staged — M3.1/M3.2/M3.3 done; M3.4A (RX proof) done and STOPPED
pending acceptance of two corrections (`addrhigh=0x80000000`, RX ring=256)
before M3.4B (safe RX enable).

## M2.5f — board-data source resolved: external SPROM
Runtime `SROM_CONTROL=0x23` (PRESENT|SIZE_4K|OTP_PRESENT, OTPSEL=0) and
`OTPS=0x9400` (GU_PROG_HW clear). Proven from the blob:
- `si_is_sprom_available()` (0x1f509) for ccrev>30 returns
  `cccaps & 0x40000000 ? (SROM_CONTROL & PRESENT) : 0`; true here.
- `srom_read()` region 1 calls the **external SPROM** reader (0x9142) when that
  flag is true and only uses the OTP fallback (0x9284) when it is false. So the
  blob does **not** fall back to OTP on this board.
- External SPROM/shadow base is **CC+0x800**; 0x840 is the OTP general-use-region
  mapping from OTPL, not the SPROM base.
This supersedes M2.5c/M2.5d direct-OTP attempts for the MAC. `otp_diag` is now
**off by default**; a read-only `sprom_diag` (16-bit reads exactly like
`bcma_sprom_read()`, with bcma CRC8 + revision 8..11 validation) is on and logs
`bus->sprom` that bcma already parsed.

## M2.5g — real MAC obtained and installed (runtime-verified)
External SPROM validates at **234 words, rev 11, CRC stored=calculated=0xc0**
(raw `2cfd a161 4025` at 0x90). bcma's `bus->sprom.il0mac` is malformed
(`00:00:00:00:2c:fd`) because mainline bcma has only `bcma_sprom_extract_r8()`
and applies rev8 `IL0MAC=+0x8C` to rev11 data; rev11 puts the first MAC at
**+0x90**. OpenBRCM:
- validates CRC + revision, then selects the offset by revision (rev8 +0x8C,
  rev11 +0x90) in `ob_si_read_mac()`;
- installs the validated MAC via `SET_IEEE80211_PERM_ADDR()` before
  `ieee80211_register_hw()`, never using `bus->sprom.il0mac`.
Runtime-verified: `iw dev` and `/sys/class/net/wlp33s0b1/address` both show
**2c:fd:a1:61:40:25** (OUI ASUSTeK). OTP remains untouched. M3 not started.

## M3.1 — DMA architecture (report)

Recovered the BCM4352 / D11 rev42 DMA64 model; see `docs/dma_architecture.md`.
No register writes. Key conclusions: 64-bit dma64 engine; FIFO map for
`corerev > 10` (TX0 0x200 / RX0 0x220 / TX1 0x240 / TX2 0x280 / TX3 0x2C0,
stride 0x40; RX ring = FIFO0, management TX = FIFO3); per-channel registers
`control 0x00 / ptr 0x04 / addrlow 0x08 / addrhigh 0x0C / status0 0x10 /
status1 0x14` (corrects the earlier "STATUS=0x04" artifact); 16-byte descriptor
`{ctrl1, ctrl2, addrlow, addrhigh}`; 8 KiB ring alignment; producer/consumer via
`ptr` + `status0`; D11 `macintstatus` 0x128 / `macintmask` 0x12C
(`I_RI`=1<<16, `I_XI`=1<<24); 38-byte hardware RX header.

## M3.2 — DMA ring allocation (software model only)

Implemented `src/ob_dma.{h,c}`. Software only: no D11 DMA register is touched,
no ring base is published, no engine is enabled and no IRQ is taken.
- Validates the real device capability with
  `dma_set_mask_and_coherent(core->dma_dev, DMA_BIT_MASK(32))`. M3.4A proved the
  blob discards the DMA address high dword (writes `addrhigh=0x80000000`), so
  the device can only address a 32-bit host window; a 64-bit mask would allow
  addresses the device cannot represent. (M3.2 originally used 64-bit; corrected
  in M3.4A.)
- Allocates one RX and one TX/control ring (future FIFO0 RX @0x220 / FIFO3 TX
  @0x2c0) from a `dma_pool` of 8 KiB blocks with 8 KiB alignment/boundary, and
  validates the recovered 8 KiB constraint on the **DMA address** only. The CPU
  virtual address is unrelated to the hardware and is only required to meet the
  natural alignment of `struct ob_dma_desc` for safe CPU access.
- Descriptor = 16 bytes `{ctrl1, ctrl2, addrlow, addrhigh}` with explicit
  masks/shifts (no bitfields). M3.4A corrected the capacities to the blob's
  asymmetric values: **RX 256 descriptors (4096 B), TX 512 (8192 B)**, both from
  an 8 KiB `dma_pool` block with EOT on the last active descriptor (RX 255, TX
  511).
- Separate RX/TX index and per-slot ownership metadata (skb/dma/mapped) so a
  later mapping is unmapped and freed exactly once.
- Full unwind on every failure stage and in `ob_remove()`; no hardware reset is
  needed because DMA was never enabled.
- Host + KUnit tests cover descriptor encoding, ring arithmetic/wraparound,
  index bounds, descriptor offsets/aliasing and the EOT helper.

## M3.2.1 — 32-bit DMA window validation

Validates the corrected DMA address model without touching any DMA engine
register. Keeps `dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32))`. Both
coherent rings are additionally required to satisfy
`upper_32_bits(desc_dma) == 0` (`ob_dma_addr_in_window()`), failing probe with
`-ERANGE` otherwise — a defensive check independent of the mask. Diagnostics
report `dma: window = 32-bit` and per ring
`dma=<...> high32=<n> window=32-bit ... dma_aligned_8k=yes`. Host tests cover
the window helpers. Runtime: 5× insmod/rmmod, all addresses < 4 GiB, MAC
`2c:fd:a1:61:40:25`, no DMA-API/BUG/Oops/WARNING.

## M3.3 — interrupt infrastructure

Implemented `src/ob_irq.{h,c}`. Establishes and proves a safe IRQ path without
enabling any source, DMA or frame processing.
- **Transport:** the IRQ is the one `bcma` already recorded for the D11 core
  (`core->irq == bus->host_pci->irq`); OpenBRCM calls no `pci_alloc_irq_vectors()`
  and does not touch the PCI IRQ routing. Mode is reported from real PCI state
  (`pci_dev->msix_enabled`/`msi_enabled`, else INTx).
- **Registers:** `MACINTSTATUS=0x128`, `MACINTMASK=0x12C`, per-FIFO
  `intctrlregs` at 0x20. Corrects the M3.1 draft: `I_RI`/`I_XI` are per-FIFO
  bits, not `macintstatus` bits (see `docs/dma_architecture.md` §7).
- **Owned bits:** `MI_DMAINT` only; `OB_D11_IRQ_OWNED_MASK ⊆ KNOWN_MASK`, so the
  handler never acks an unproven bit. Owned sources are RMW-masked (unrelated
  bits preserved) before `request_irq`.
- **Handler:** read `MACINTSTATUS`; treat `0xffffffff` as invalid; if no owned
  bit pending return `IRQ_NONE`; otherwise rate-limited log, write back only the
  owned pending bits (write-1-to-clear), return `IRQ_HANDLED`. No loops, no
  sleeps, no allocation.
- **Teardown order:** mask owned bits → clear the handler gate →
  `synchronize_irq()` → `free_irq()` → log counters. `ob_remove()`:
  mac80211 → IRQ → DMA.
- Host + KUnit tests cover owned-mask subset, status validity, pending/ack,
  IRQ_NONE/HANDLED decisions, unknown-bit preservation and RMW masking.

## M3.4A — RX path proof (report only)

Recovered the exact BCM4352 / D11 rev42 RX DMA configuration from the blob; see
`docs/rx_path.md`. **No DMA register written, no engine enabled.** Resolved:
- RX control **`0x0000084D`** (`RE | PD | rxoffset<<1`, parity disabled, ROC
  disabled, preserved core bits 0) — `_dma_rxenable` 0xe5f0.
- `rxoffset = 38`, `rxbufsize = 2048`, no extra headroom; `nrxpost = 64`;
  **RX ring = 256 descriptors** (TX = 512) — `wlc_attach_malloc`/`wlc_bmac_attach`.
- Descriptor: `ctrl1 = EOT@255`, `ctrl2 = 0x0800`, `addrlow = (u32)pa`,
  **`addrhigh = 0x80000000`**.
- Ring base `addrlow = (u32)ring_dma`, `addrhigh = 0x80000000`;
  initial `PTR = 0x400` (rcvptrbase = 0, `PTR = rxout*16`; corrected in M3.4B).
- Completion `index = (status0 & 0x1fff) >> 4`; RX header = 38 bytes, frame at
  +38; RX FIFO0 intstatus/mask `0x20`/`0x24`, `I_RI = 1<<16`, `MI_DMAINT = 1<<15`.

Two **corrections to the accepted M3.1/M3.2 model** (blocking M3.4B):
`addrhigh = 0x80000000` (not 0), and RX ring = 256 (not 512). STOP for
acceptance before enabling DMA.

## M3.4B — FIFO0 RX engine bring-up (first RX)

Implemented `src/ob_rx.{h,c}`. Programs only the proven FIFO0 RX DMA block and
never hands frames to mac80211.
- Maps 64 RX buffers (`alloc_skb(2048)` + `dma_map_single(DMA_FROM_DEVICE)`),
  each required to satisfy `upper_32_bits(dma) == 0`; any violation unwinds all
  mappings and fails probe with no partial ring.
- Builds descriptors 0..63 (`ctrl1=0`, `ctrl2=0x0800`, `addrlow=(u32)pa`,
  `addrhigh=0x80000000`) and a structural EOT-only slot at 255 (address 0,
  length 0); `dma_wmb()` before publishing.
- Programs `addrlow`/`addrhigh`/`PTR=0x400` (rcvptrbase=0, rxout*16)/
  `control=0x0000084d`, reads
  back control/ptr/base/status0/status1 and aborts+disables on mismatch.
- Enables host routing (`bcma_host_pci_irq_ctl(true)`) then only
  `FIFO0 intmask |= I_RI` and `MACINTMASK |= MI_DMAINT`.
- Hard IRQ: read `MACINTSTATUS`, require `MI_DMAINT`, read FIFO0 `intstatus`,
  require `I_RI`, ack only `I_RI` and the owned MAC bit, mask RX, schedule a
  bounded tasklet. No ring walk in hard IRQ.
- Tasklet: `index = (status0 & 0x1fff) >> 4` (validated `< 256`, not past PTR,
  no wrap), unmap exactly once, validate `RxFrameSize`, log at most 5 frames
  (frame_control/type/subtype, 32 bytes) and "beacon detected"; no refill, stop
  when the posted set is drained. Failure containment masks/unroutes/disables.
- Teardown order: mask `I_RI` → mask `MI_DMAINT` → unroute → disable RX +
  bounded poll → `synchronize_irq()` → `tasklet_kill()` → `free_irq()` → unmap
  once → free skbs → free rings.

## Previously
- M0/M1 code present (`src/ob_main.c`, `src/ob_core.c`, `src/ob_si.c`).
- **M2 present:** `src/ob_mac80211.c` registers with mac80211 and exposes the
  **2.4 GHz** band (channels, legacy rates, HT 20/40 2×2). Interface bring-up is
  intentionally disabled (`.start` returns `-EOPNOTSUPP`) until M3.
- Pure math present and tested (`ob_channel.c`, `ob_rate.c`; host + KUnit).

## M2 blockers / deliberately not exposed
- **5 GHz band is NOT advertised.** The exact BCM4352/acphy 5 GHz channel table
  has not been recovered/validated from the blob. The internal architecture is
  ready (`ob_channel` supports 5 GHz), but we do not advertise guessed
  capabilities. **TODO/blocker:** recover and validate the BCM4352/acphy 5 GHz
  channel list (and DFS/NO-IR flags) before enabling `NL80211_BAND_5GHZ`.
- **VHT / 80 MHz, LDPC, MAX_AMSDU, >2 spatial streams** are not advertised
  (no provenance yet; VHT depends on 5 GHz).


## Mapping to the RE work
- M1 ← RE Stages 4–5 (MMIO map, sequences/values).
- M2 ← RE Stage 6 (radio parameters).
- M3 ← RE Stage 6 (DMA) and Stage 8 (data-path flows).
- M4 ← RE Stages 7–8 (contract/dispatch, control flows, mac80211 mapping).
- M6 ← RE Stage 3 (pluggable PHY ops).
