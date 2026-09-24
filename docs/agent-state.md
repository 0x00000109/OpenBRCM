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
- `main` = `85d3013` — **PR #2 merged** (`Merge pull request #2 from
  0x00000109/m34d2a-ucode-upload`, merge commit `85d3013` with parents
  `854e398` + `f544af8`). `main` now contains M3.4D1 (`2029292`), the isolated
  modes `fw_validate_only=1` / `ucode_test_only=1`, `src/ob_ucode.{c,h}`,
  `docs/ucode_test.md`, and the M3.4D2A hardware record. The hardware-tested
  candidate `7265f9d` and implementation `47e0883` are reachable from `main`.
- Active branch `m34d2b-initvals-analysis` (from `main` @ `85d3013`) — M3.4D2B
  **analysis only**, Draft PR, not merged.
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

## Analysis-only facts (not hardware proven here)
- M3.4C/C.1: exact vendor rev42 images recovered from `wlc_hybrid.o_shipped`;
  vendor 8-byte IV record format (terminator `0xffff`), **not** b43 IV.
- M3.4D2A: see "Current milestone".

## Current milestone
**M3.4D2B — rev42 common initvals sequencing.**
Status: **`ANALYSIS ONLY` / NOT IMPLEMENTED / NOT HARDWARE PROVEN.**
- Branch `m34d2b-initvals-analysis` (Draft PR, not merged).
- Full report `docs/m34d2b_common_initvals.md`; machine-generated 610/73-record
  classification `docs/m34d2b/initvals_classification.{md,json}` via
  `scripts/analyze_initvals.py` (verifies vendor size + sha256, no hardware).
- Re-proven call order: ucode -> PSM start -> common `d11ac1initvals42`
  (`wlc_bmac_init` `0x68b98`, rev42+AC `0x687c9`) -> post setup -> band init
  `sub_6656c` (`d11ac1bsinitvals42` `0x66612` -> `wlc_phy_init` `0x669df`).
- Common table: 610 records (194 direct + 76 OBJADDR selectors + 340 OBJDATA
  data); windows SHM(56 auto-inc)+SCR(20) only; **no** `MACCONTROL`, **no** DMA,
  **no** PHY/radio, **no** interrupt-source enable (`MACINTMASK=0`); applied
  with `PSM_RUN=1`, `EN_MAC=0` (the D2A exit state).
- Follow-up blocker resolution + GO/NO-GO in `docs/m34d2b_common_initvals.md`
  (§F1–F14): `wlc_phy_cal_init` (`0x6834b`) is PHY **software-state only, 0
  MMIO**; omitted pre-steps classified (none required for the table); the
  **`SHM_EN` hard blocker is resolved** (not required for the `OBJADDR` object
  window; vendor applies with `SHM_EN=0`, upstream never sets it); SCR = PSM
  scratch pad; `IRQ ENABLE EFFECT = NONE`, `DMA ENABLE EFFECT = NONE`; formal
  decision **"CAN COMMON INITVALS BE ISOLATED SAFELY? YES"** (design only).
- Still **ANALYSIS ONLY / NOT IMPLEMENTED / NOT HARDWARE PROVEN**: the D2B test
  design (§F13) must not be implemented or run without explicit human approval.
- Last hardware-proven milestone remains **M3.4D2A** (see below).

## Last hardware-proven milestone
**M3.4D2A — D11 rev42 ucode upload + PSM start only.**
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
M3.4D2A isolated `ucode_test_only=1` **HARDWARE RUNTIME PROVEN on BCM4352**
(tested candidate `7265f9d`; see "Current milestone"). Prior: M3.4D1 isolated
`fw_validate_only=1` runtime PASS.

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
M3.4D2B follow-up analysis is recorded and returns **GO for *designing* an
isolated test** (see `docs/m34d2b_common_initvals.md` §F12–F13); the `SHM_EN`
hard blocker is resolved. Next: present the D2B test design for explicit human
approval, or close the residual UNKNOWNs (§F14). **No hardware action now:** do
not run `insmod`, do not repeat D2A, do not implement D2B hardware writes, and
do not apply `d11ac1initvals42`/`d11ac1bsinitvals42`. No PHY/radio/channel work.

## Post-test hardware state (risk)
After the successful D2A run the chip is intentionally left partial: `PSM_RUN=1`,
D11 core enabled, `EN_MAC=0`. No cleanup/recovery register writes are performed
(none is provenance-backed); a later load re-issues `OBJADDR` and rewrites the
image. Do **not** repeat the test automatically and do not invent cleanup writes.

## Exact STOP boundary
Analysis task: STOP after committing/pushing the M3.4D2B analysis document and
artifacts and opening a Draft PR. No `insmod`, no D2A repeat, no initvals write.

Last hardware STOP (M3.4D2A): after the bounded `MI_MACSSPNDD` poll and the
read-only SHM diagnostic, the D2A path returns. It must not apply
initvals/bsinitvals and must not touch PHY/radio/channel/DMA/IRQ/mac80211.

## Do NOT change blindly
- `src/ob_rx.c` / `ob_rx.h`: RX PTR model (`rcvptrbase=0`, `PTR=rxout*16`),
  `RX CONTROL=0x84d`, `addrhigh=0x80000000`, ring 256.
- `src/ob_dma.c`: `DMA_BIT_MASK(32)` (not 64).
- `src/ob_fw.{c,h}`: exact vendor firmware names/sizes/FNV guards.
- `src/ob_ucode.{c,h}`: recovered MACCONTROL/OBJADDR/poll constants.
- `MOC/` signing material (outside this repo): never modify/read the private key.

## Branch contents (`m34d2b-initvals-analysis`, Draft PR, not merged)
`docs/m34d2b_common_initvals.md`, `docs/m34d2b/initvals_classification.{md,json}`,
`scripts/analyze_initvals.py`, `docs/milestones.md`, `docs/agent-state.md`,
`README.md`. No `src/` or `tests/` change (analysis only).
Note: `scripts/runtime-test.sh` and `.opencode/` remain untracked local tooling.
