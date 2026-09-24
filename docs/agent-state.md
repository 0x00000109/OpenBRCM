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
- `main` = `65d61ce` — **PR #2 merged** (`85d3013`) and **PR #5 merged**
  (`Merge pull request #5 from 0x00000109/m34d2b-initvals-analysis`, merge
  commit `65d61ce`, parents `85d3013` + `0bdec35`; normal merge commit, not
  squashed/rebase). `main` contains M3.4D1 (`2029292`), the isolated modes
  `fw_validate_only=1` / `ucode_test_only=1`, `src/ob_ucode.{c,h}`,
  `docs/ucode_test.md`, the M3.4D2A hardware record, and the M3.4D2B analysis
  (`137e290`, `ae25c68`, `0bdec35` all reachable from `main`). The
  hardware-tested candidate `7265f9d` and implementation `47e0883` are
  reachable from `main`.
- Active branch `m34d2b-initvals-test` (from `main` @ `65d61ce`) — holds the
  **M3.4D2B isolated common-initvals test** (`initvals_test_only=1`):
  `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / **NOT HARDWARE PROVEN**. Draft PR
  open; not merged. `main` does not yet contain the D2B implementation.
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

## Canonical milestone status
Stated exactly (an implementation or a formal analysis GO is never hardware proof):
- M3.4D2A = HARDWARE RUNTIME PROVEN
- M3.4D2B analysis = COMPLETE
- M3.4D2B implementation = IMPLEMENTED / STATIC TESTED / SIGNED
- M3.4D2B hardware status = NOT HARDWARE PROVEN

The decision `CAN COMMON INITVALS BE ISOLATED SAFELY? YES` cleared the design;
the code exists but has **not** run on hardware and is **not** a hardware proof.

## Current milestone
**M3.4D2B — isolated rev42 common-initvals test.**
Status: **`IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / NOT HARDWARE PROVEN.**
- Branch `m34d2b-initvals-test` (Draft PR, not merged); mode
  `initvals_test_only=1`, mutually exclusive with `fw_validate_only`/
  `ucode_test_only` (any conflict -> `-EINVAL`, no hardware access).
- Implementation doc `docs/m34d2b_initvals_test.md`; analysis report
  `docs/m34d2b_common_initvals.md`; classification
  `docs/m34d2b/initvals_classification.{md,json}`.
- Shared hardware-proven D2A core `ob_ucode_run_d2a()` (src/ob_ucode.c) is used
  by both D2A and D2B; only one copy of the register sequence exists.
- Applies exactly the 610 `d11ac1initvals42` records (113 x 16-bit, 497 x
  32-bit) in strict order, terminator excluded, then reads only
  `M_FIFOSIZE0..3=01c4/0000/0000/079e`, `MACINTMASK=0`,
  `MACCONTROL=0x04020402`, SHM `0x14=0xb4`. Stops before
  bsinitvals/`sub_6656c`/PHY/radio/channel/DMA/IRQ/mac80211.
- Static verification: `make` + `make hosttest` (8/8 PASS) +
  `scripts/docs-check.sh` PASS + checkpatch clean + `scripts/sign.sh`
  (Broadcom Driver MOK, sha256). KUnit NOT EXECUTED (no runner).
- NOT run on hardware. Last hardware-proven milestone remains **M3.4D2A**.

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
M3.4D2B implementation is **IMPLEMENTED / STATIC TESTED / SIGNED** on
`m34d2b-initvals-test` (Draft PR, not merged) and is **NOT HARDWARE PROVEN**.
Next task: review/merge the Draft PR, then — **only under explicit human
approval in a dedicated task** — run the isolated test
(`insmod openbrcm.ko initvals_test_only=1`). Until then: no `insmod`, no D2A
repeat, no initvals write, no PHY/radio/channel work. See
`docs/m34d2b_initvals_test.md` for the exact manual procedure and the
failure/residual-state matrix.

## M3.4D2B implementation boundary (as implemented)
The mode executes exactly this sequence, then STOP:
proven D2A preparation -> exact ucode upload -> proven PSM start /
`MI_MACSSPNDD` -> apply exactly the **610** `d11ac1initvals42` records ->
read deterministic postconditions -> STOP.

Postconditions preserved (provenance-backed only; no other equality check):
- `M_FIFOSIZE0 = 0x01c4`, `M_FIFOSIZE1 = 0x0000`, `M_FIFOSIZE2 = 0x0000`,
  `M_FIFOSIZE3 = 0x079e`;
- `MACINTMASK = 0`;
- `MACCONTROL = 0x04020402`;
- SHM `0x0014 = 0x000000b4`.

STOPs before: `sub_6656c`, bsinitvals, `wlc_phy_init`, PHY register programming,
radio programming, calibration, channel selection, RX DMA, TX DMA,
`request_irq`, host IRQ routing, mac80211 registration. Full design:
`docs/m34d2b_common_initvals.md` §F13; implementation:
`docs/m34d2b_initvals_test.md`.

## Post-test hardware state (risk)
After the successful D2A run the chip is intentionally left partial: `PSM_RUN=1`,
D11 core enabled, `EN_MAC=0`. No cleanup/recovery register writes are performed
(none is provenance-backed); a later load re-issues `OBJADDR` and rewrites the
image. Do **not** repeat the test automatically and do not invent cleanup writes.

## Exact STOP boundary
Implementation task: STOP after committing/pushing the D2B implementation and
tests and opening a Draft PR, keeping it Draft. No `insmod`, no D2A repeat, no
initvals write to hardware.

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
