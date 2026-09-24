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
- Real PHY boundary: `wlc_phy_init` (`0xbabf5`) -> `wlc_phy_anacore`
  (`0xbac84`, first PHY indirect MMIO via `D11+0x3e0/0x3fc/0x3fe`) ->
  `wlc_phy_switch_radio` (`0xbad44`) -> `call *[pi+0x28]` = `wlc_phy_init_aphy`
  (`0x8c3f9`, installed at `0x899dd`). Note the full vendor path already calls
  `wlc_phy_switch_radio` at `0x69594` **before** band init.
- **Formal decision: `CAN BSINITVALS BE ISOLATED SAFELY BEFORE REAL PHY INIT?
  NOT YET.** Blockers: post-common entry state not isolated (incl. `MACCONTROL`
  change at `0x69047`), band/MHF dependency, rev/type selection, no clean PHY
  separation, unproven postconditions.
- Proposed smallest safe boundary (design only, do not implement): reproduce
  the post-common D11 setup tail through band init, stopping before
  `wlc_phy_switch_radio`/`wlc_phy_init`. See report §17/§18 for the AC PHY
  follow-on roadmap.

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
