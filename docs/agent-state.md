# OpenBRCM agent state

Authoritative handoff. Read with `AGENTS.md`. Repository + Git history are the
source of truth; this file records the live working-tree state on top of HEAD.

## Target hardware
- Broadcom BCM4352 PCI ID `14e4:43b1`
- ASUS PCE-AC56 subsystem `1043:85ba`, chip revision 3
- D11 core ID `0x812`, revision `0x2A` / 42 (AC class)
- PCIe Gen2 BCMA core `0x83C`

## Architecture
- BCMA for the Linux backplane infrastructure
- OpenBRCM `ob_si` for recovered chip-specific behaviour
- mac80211 SoftMAC integration

## Commit state (IMPORTANT)
- `main` = `4146cd8` — **PR #2 merged** (`85d3013`), **PR #5 merged**
  (`65d61ce`, normal merge commit, not squashed/rebase), and **PR #6 merged**
  (`4146cd8`, parents `65d61ce` + `21107ef`). `main` contains M3.4D1
  (`2029292`), the isolated modes `fw_validate_only=1` / `ucode_test_only=1`,
  `src/ob_ucode.{c,h}`, `docs/ucode_test.md`, the M3.4D2A hardware record, the
  M3.4D2B analysis (`137e290`, `ae25c68`, `0bdec35`), and **M3.4D2B**
  (`initvals_test_only=1`, `src/ob_initvals.{c,h}`, shared
  `ob_ucode_run_d2a()`), HARDWARE RUNTIME PROVEN on BCM4352 (candidate
  `f27286f`, module SHA256
  `1258290cb491ea551a9fb4c4e820ecf3450ae7c23957b41e5eeada14f1d98290`). The
  hardware-tested candidate `7265f9d` and implementation `47e0883` are
  reachable from `main`.
- Active branch `m34d3-bsinitvals-analysis` (from `main` @ `4146cd8`) — M3.4D3
  **analysis only** (`ANALYSIS ONLY` / NOT IMPLEMENTED / NOT HARDWARE PROVEN):
  `d11ac1bsinitvals42` + entry into AC PHY init. Draft PR; not merged.
- Pre-commit documentation-discipline hook is active (`.githooks/`); see
  `AGENTS.md`.

## Hardware-proven facts (HARDWARE PROVEN)
- M2: bcma bind, external SPROM rev11 MAC `2c:fd:a1:61:40:25`, HT 2x2
  (`iw phy`), `ieee80211_hw` registration.
- M2.5: PCIe2 host bring-up, D11 core enable, FAST clock/HAVEHT.
- M3.2/M3.2.1: DMA64 8 KiB-aligned rings; **32-bit** DMA window enforced
  (`upper_32_bits(dma)==0`); descriptor `addrhigh=0x80000000`.
- M3.3: IRQ registration/teardown only (no source enabled).
- M3.4B: FIFO0 RX engine bring-up, **RX DMA Level-1 PASS** — ring 256
  descriptors (TX 512), `rxbufsize=2048`, `rxoffset=38`, descriptor
  `ctrl2=0x0800`, `addrhigh=0x80000000`, `rcvptrbase=0`, initial
  `PTR=0x400`, `RX CONTROL=0x84d`, engine reached IDLE, host IRQ route + masks
  programmed. No real RX completion/frame proven yet.
- M3.4D1 (isolated mode, branch `m34d2a-ucode-upload`): `fw_validate_only=1`
  **runtime PASS** —
  primary `brcm/bcm4352-d11ucode42.bin` absent, fallback
  `brcm/bcm43xx-ucode.fw` used (`size=43400`, `words=10850`); common initvals
  `4888`/`610`; band-switch initvals `592`/`73`; all validated; "hardware
  untouched"; `rmmod` clean ("nothing to tear down").
- M3.4D2A (isolated mode, branch `m34d2a-ucode-upload`): `ucode_test_only=1`
  **HARDWARE RUNTIME PROVEN on BCM4352** — tested candidate `7265f9d`,
  implementation `47e0883`, base `854e398`. `insmod` rc=0; ucode
  `brcm/bcm43xx-ucode.fw` `size=43400`/`words=10850`; upload `writes=10850`;
  PSM start `MACCONTROL=0x04020402`; bounded poll PASS `iterations=11`
  `MACINTSTATUS=0x00000001`, then STOP. Scope: ucode upload + PSM start only.
- M3.4D2B (isolated mode, branch `m34d2b-initvals-test`): `initvals_test_only=1`
  **HARDWARE RUNTIME PROVEN on BCM4352** — tested candidate `f27286f`, module
  SHA256 `1258290cb491ea551a9fb4c4e820ecf3450ae7c23957b41e5eeada14f1d98290`,
  base `65d61ce`. `insmod` rc=0; ucode `brcm/bcm43xx-ucode.fw` `43400`/`10850`
  writes; PSM PASS `iterations=11` `MACINTSTATUS=0x00000001`; common initvals
  `610/610` (`w16=113`, `w32=497`); postconditions
  `M_FIFOSIZE0..3=01c4/0000/0000/079e`, `MACINTMASK=0`, `MACCONTROL=0x04020402`,
  SHM `0x14=0xb4`; STOP before bsinitvals/PHY/radio/channel/DMA. No
  timeout/BUG/Oops/lockup/reset. Scope: **common initvals only** (does NOT prove
  bsinitvals, band init, AC PHY, radio, calibration, channel, RX or TX).

## Analysis-only facts (not hardware proven here)
- M3.4C/C.1: exact vendor rev42 images recovered from `wlc_hybrid.o_shipped`;
  vendor 8-byte IV record format (terminator `0xffff`), **not** b43 IV.
- **M3.4D3 (current):** band-switch initvals (`d11ac1bsinitvals42`, 73 records)
  + PHY boundary. Report `docs/m34d3_bsinitvals.md`; classification
  `docs/m34d3/bsinitvals_classification.{md,json}` via
  `scripts/analyze_bsinitvals.py`. Decisions (corrected, Appendix B):
  `wlc_phy_switch_radio` does **not** precede bsinitvals on the BCM4352/AC path
  (the `0x69594` call is NPHY/HT-gated; `wlc_bmac_mute` is skipped), so a
  vendor-ordered test can stop before real PHY/RF writes: **YES**. The
  post-common tail is **not D11-only** (it inits 4 TX DMA engines + FIFO0 RX and
  writes interrupt-source masks); Appendix C reverses it and yields **A/B/C/D =
  YES** (D3A0 DMA/IRQ-source → D3A1 remaining tail → D3B), conditional on host
  IRQ route off and a core-reset/reboot quiesce. The isolated unit is the full
  vendor prefix, not the 73 records alone.
- M3.4D2A: see "Current milestone".

## Canonical milestone status
Stated exactly:
- M3.4D2A = HARDWARE RUNTIME PROVEN
- M3.4D2B = HARDWARE RUNTIME PROVEN
- M3.4D2B analysis = COMPLETE
- M3.4D2B implementation = IMPLEMENTED / STATIC TESTED / SIGNED
- M3.4D3 = ANALYSIS ONLY / NOT IMPLEMENTED / NOT HARDWARE PROVEN

The hardware-proven milestones are narrow (see below); the later
PHY/radio/channel stages remain **unproven**.

## M3.4D3 — band-switch initvals + PHY boundary (ANALYSIS ONLY)
Status: **`ANALYSIS ONLY` / NOT IMPLEMENTED / NOT HARDWARE PROVEN.**
- Consumer: `sub_60f67(dev, d11ac1bsinitvals42)` at `sub_6656c` `0x669bd`,
  selected when `[dev+0x84]==0x2A` (PHY rev 42) and
  `[[dev+0xE8]+0x1C]==0xB` (AC phy type); `sub_6656c` is called from
  `wlc_bmac_init` `0x695d8` (initial up) and `wlc_bmac_set_chanspec` `0x67bd0`.
- Table: 592 B, **73** records (34 x OBJADDR w4 + 15 x OBJDATA-lo w2 +
  19 x OBJDATA-hi w2 + 5 x direct IHR w2); 68 SHM + 5 direct IHR; `IRQ ENABLE
  EFFECT = NONE`, `DMA ENABLE EFFECT = NONE`, no PHY/radio (for the table
  itself). C3: "band-specific ucode IHR, SHM, and SCR inits", applied before
  `wlc_phy_init`; called on up and band switch; same table for 2.4/5 GHz.
- Common vs bs: SHM-only for bs; no direct-offset overlap; 3 SHM bytes
  overridden (`0x0010=0x14`, `0x001c=0x183`, `0x0094=0x1f4`).
- PHY boundary (corrected): `wlc_phy_init (0xbabf5)` -> `wlc_phy_anacore
  (0xbac84)` (first PHY indirect write, `D11+0x3fc/0x3fe`) ->
  `wlc_phy_switch_radio (0xbad44)` -> `wlc_phy_switch_radio_acphy (0xaa782)`
  (first radio-window writes, `D11+0x3d8/0x3da`) -> `wlc_phy_init_aphy
  (0x8c3f9)` via `call *[pi+0x28]`. The `wlc_bmac_init` `0x69594`
  `wlc_phy_switch_radio` is `phy_type==7` (NPHY/HT) gated and **not taken for
  AC**; `wlc_bmac_mute` `0x6957b` is skipped (`wlc_bmac_init` arg#3=0).
- Tail reversal (Appendix B): for rev42 the legacy
  `xmtfifo_sz`/`M_FIFOSIZE`/TX-flush block is **skipped** (`phyrev <= 0x27`
  gate); the rev42 FIFO stage is `sub_67efd` (TXE0 fixup, safe). The tail also
  writes `intrcvlazy[0]`/`intctrlregs[0].intmask=I_RI` and inits DMA engines.
- DMA/IRQ reversal (Appendix C, NEW): **4** TX channels only (`dma_txinit` x4,
  di[0..3]; FIFO0 RX `0x220`, BK `0x200`, BE `0x240`, VI `0x280`, VO/CTL
  `0x2C0`). TX engines enabled but **idle** (no descriptors posted → no
  transmission). FIFO0 RX enabled (`control=0x84D`) with 64 buffers posted
  (idle). Host IRQ delivery **not possible** (`macintmask=0`, `wl_intrsoff`
  active; `wl_intrson` only in `wlc_bmac_up_finish` after `wlc_phy_init`).
  Vendor quiesce = `wlc_coredisable` before `dma_detach` frees memory.
- Decisions: PHY/RF boundary **YES**; formal decomposition **A YES (D3A0),
  B YES, C YES, D YES** (§C.18), conditional on host IRQ route disabled and an
  `ob_dma_quiesce`/reboot policy. `MACCONTROL` bit30 = `MCTL_DISCARD_PMQ`;
  `macphyclk_set` = D11 core cflags bit4 (`SICF_MPCLKE`); `switch_macfreq`
  writes D11 `0x62e/0x630` from PMU VCO. Report §0, §16/§17, Appendices B/C. No
  implementation.

## Current milestone (just proven)
**M3.4D2B — isolated rev42 common-initvals test.**
Status: **`HARDWARE RUNTIME PROVEN` on BCM4352.**
- Tested candidate `f27286f6f7e817a58fd1ae6da311cc4281a10a0b`; module SHA256
  `1258290cb491ea551a9fb4c4e820ecf3450ae7c23957b41e5eeada14f1d98290`; base
  `main` @ `65d61ce`; PR #6.
- Runtime: `insmod` rc=0; ucode `brcm/bcm43xx-ucode.fw` `43400` bytes / **10850
  writes**; pre-upload `MACCONTROL 00000000 -> 04000404`; PSM start
  `04020402`; poll `iterations=11` `MACINTSTATUS=0x00000001`; common initvals
  **`610/610`** (`w16=113`, `w32=497`); postconditions all seven exact
  (`01c4/0000/0000/079e`, `MACINTMASK=0`, `MACCONTROL=0x04020402`,
  `SHM[0x14]=0xb4`); final `PASS - stopped before
  bsinitvals/PHY/radio/channel/DMA`. No timeout/BUG/Oops/lockup/reset.
- Mode `initvals_test_only=1`, mutually exclusive with `fw_validate_only`/
  `ucode_test_only` (conflict -> `-EINVAL`, no hardware access).
- Shared D2A core `ob_ucode_run_d2a()` (src/ob_ucode.c) used by both D2A and D2B.
- Evidence: `docs/m34d2b_initvals_test.md`; report
  `docs/m34d2b_common_initvals.md`; classification
  `docs/m34d2b/initvals_classification.{md,json}`.
- **Negative boundary (NOT proven):** `d11ac1bsinitvals42`, band initialization,
  AC PHY initialization, radio, calibration, channel selection, RX frame
  reception, TX. Do not broaden this milestone.

## Last hardware-proven milestone
**M3.4D2B — isolated rev42 common-initvals test (see "Current milestone").**
Prior: **M3.4D2A — D11 rev42 ucode upload + PSM start only.**
Status: **`HARDWARE PROVEN` — HARDWARE RUNTIME PROVEN on BCM4352** (tested
candidate `7265f9d`, implementation `47e0883`, base `854e398`), now merged to
`main` (`85d3013`). This proves the ucode upload + PSM start **only**;
initvals/PHY/radio/channel/RX/TX remain **unproven**.
- Isolated mode `ucode_test_only=1`; mutually exclusive with `fw_validate_only=1`
  (conflict -> `-EINVAL`, no hardware access).
- Minimum prep (`bcma_host_pci_up` + D11 `bcma_core_enable` + FAST clock) ->
  `MACCONTROL=0x04000404` masked RMW -> `OBJADDR=0x03000000` + exactly 10850
  raw LE `OBJDATA` writes (write-count invariant) -> `MACINTSTATUS=0xffffffff`
  -> `MACCONTROL=0x04020402` (no `EN_MAC`) -> bounded `MI_MACSSPNDD` poll
  (10 us, `0xF4249`/10 = <=100000 iters = <=1.0 s) -> read-only SHM
  `M_FIFOSIZE0..3` (0x98..0x9e) -> STOP.
- Must NOT reach: common initvals, bsinitvals, PHY, radio, channel, DMA, IRQ,
  mac80211. Dedicated `remove()` guard performs no RX/IRQ/DMA/mac80211 teardown.
- Details: `docs/ucode_test.md`.

Runtime evidence (BCM4352, chip rev 3, D11 rev 42):
`clkctlst=070b0042` HAVEHT=1 core_enabled=1; MACCONTROL `00000000` -> upload
`04000404`; `OBJADDR=30000000`; writes `10850`; first `0300104e 0001bc60
02f00e25 0003bfde`; last `02f00000 000002de 00000000 00000000`; PSM start
`04020402`; poll `delay=10us max_iter=100000 max_total_us=1000000` PASS
`iterations=11` `MACINTSTATUS=00000001`; SHM `M_FIFOSIZE0..3=0000 0000 0000
0000` (diagnostic only — vendor reads occur after common initvals); final
`PASS - stopped before initvals/PHY/radio/DMA`. No timeout/BUG/Oops/lockup/reset,
no DMA/IRQ/PHY/radio/channel init.

## Last completed hardware test
M3.4D2B isolated `initvals_test_only=1` **HARDWARE RUNTIME PROVEN on BCM4352**
(tested candidate `f27286f`, module SHA256 `1258290cb491ea551a9fb4c4e820ecf3450ae7c23957b41e5eeada14f1d98290`;
see "Current milestone"). Prior: M3.4D2A isolated `ucode_test_only=1` HARDWARE
RUNTIME PROVEN (candidate `7265f9d`); M3.4D1 `fw_validate_only=1` runtime PASS.

## Last failure / reset event
A test that used `fw_dryrun=1` believing it isolated hardware caused a long
hang followed by a **hard system reset**. `fw_dryrun` is only a parser log
option. `fw_validate_only` was introduced as the true isolated early-return
mode; M3.4D1 then passed. Do not repeat the combined normal-probe test.

## Active safety constraints
- No hardware test without explicit human approval in the current task.
- No `insmod`/`rmmod`/`modprobe` from an agent by default.
- Never invent register writes; provenance or stop.
- Bounded polling only.
- Do not commit the proprietary blob or firmware images.

## Current next action
M3.4D3 analysis is recorded (Appendix A ordering, Appendix B tail, Appendix C
DMA/IRQ reversal of `docs/m34d3_bsinitvals.md`). The DMA/IRQ content of the
rev42 tail is now fully reversed: **4** TX channels (not 6), TX enabled/idle,
FIFO0 RX enabled with 64 buffers, host IRQ route off, vendor quiesce = core
reset/disable. Formal decomposition **A/B/C/D = YES**: D3A0 (vendor DMA/
IRQ-source, host route off) → D3A1 (remaining tail) → D3B (band init + 73
bsinitvals) → D4 (PHY). Next (analysis/design only): design `ob_dma_alloc/
desc_init/program/post_rx`, an out-of-band `ob_irq_route`, and `ob_dma_quiesce`
(core reset/disable); until the quiesce exists D3A0 is reboot-required. **No
hardware action:** no `insmod`, no initvals/bsinitvals write, no PHY/radio/
channel/DMA/IRQ/mac80211. See `docs/m34d3_bsinitvals.md` and, for D2B runtime
evidence, `docs/m34d2b_initvals_test.md`.

## M3.4D2B boundary and evidence (PROVEN)
Executed sequence (candidate `f27286f`, module SHA256
`1258290cb491ea551a9fb4c4e820ecf3450ae7c23957b41e5eeada14f1d98290`):
proven D2A preparation -> exact ucode upload (`10850` writes) -> PSM start /
`MI_MACSSPNDD` (`iterations=11`, `MACINTSTATUS=0x00000001`) -> apply exactly the
**610** `d11ac1initvals42` records (`total=610`, `w16=113`, `w32=497`) -> read
the deterministic postconditions -> STOP.

Postconditions observed exact (provenance-backed only; no other check):
- `M_FIFOSIZE0 = 0x01c4`, `M_FIFOSIZE1 = 0x0000`, `M_FIFOSIZE2 = 0x0000`,
  `M_FIFOSIZE3 = 0x079e`;
- `MACINTMASK = 0`;
- `MACCONTROL = 0x04020402`;
- SHM `0x0014 = 0x000000b4`.

STOPPED before: `sub_6656c`, bsinitvals, `wlc_phy_init`, PHY register
programming, radio programming, calibration, channel selection, RX DMA, TX DMA,
`request_irq`, host IRQ routing, mac80211 registration. Design:
`docs/m34d2b_common_initvals.md` §F13; implementation + evidence:
`docs/m34d2b_initvals_test.md`.

## Post-test hardware state (risk)
After the successful D2B run the chip is intentionally left partial: `PSM_RUN=1`,
D11 core enabled, `EN_MAC=0`, and the 610 common-initvals records applied. No
cleanup/recovery register writes are performed (none is provenance-backed); a
later load re-issues `OBJADDR` and rewrites the image. The D2B test is **ONE
SHOT**: do **not** repeat it and do not invent cleanup writes; after a
FAIL/timeout/reset, recover logs and analyze before any further action.

## Exact STOP boundary
Documentation task for the runtime result: STOP after committing the
evidence-only update, marking PR #6 Ready and merging via a normal merge commit.
No `insmod`, no D2A/D2B repeat, no initvals write to hardware.

Runtime STOP (both D2A/D2B): the code returns after the shared D2A core (D2A
also reads the SHM diagnostic; D2B applies the 610 records and reads the
postconditions). Neither path may reach bsinitvals/`sub_6656c`/PHY/radio/
channel/DMA/IRQ/mac80211.

## Do NOT change blindly
- `src/ob_rx.c` / `ob_rx.h`: RX PTR model (`rcvptrbase=0`, `PTR=rxout*16`),
  `RX CONTROL=0x84d`, `addrhigh=0x80000000`, ring 256.
- `src/ob_dma.c`: `DMA_BIT_MASK(32)` (not 64).
- `src/ob_fw.{c,h}`: exact vendor firmware names/sizes/FNV guards.
- `src/ob_ucode.{c,h}`: recovered MACCONTROL/OBJADDR/poll constants and the
  shared `ob_ucode_run_d2a()` D2A core (do not fork it for D2B).
- `src/ob_initvals.{c,h}`: exact 610/113/497 shape + postcondition constants.
- `MOC/` signing material (outside this repo): never modify/read the private key.

## Branch contents (`m34d2b-initvals-test`, Draft PR, not merged)
Implementation: `src/ob_initvals.{c,h}`, `src/ob_ucode.{c,h}` (shared D2A core +
mode policy), `src/ob_core.{c,h}` (mode param/guards), `src/ob_fw.{c,h}`
(initvals request), `Makefile`. Tests: `tests/host/ob_initvals_test.c`,
`tests/host/ob_ucode_test.c`, `tests/kunit/ob_initvals_kunit.c`,
`tests/kunit/ob_ucode_kunit.c`, `tests/host/Makefile`. Docs:
`docs/m34d2b_initvals_test.md`, `docs/m34d2b_common_initvals.md`,
`docs/milestones.md`, `docs/agent-state.md`.
Note: `scripts/runtime-test.sh` and `.opencode/` remain untracked local tooling.
