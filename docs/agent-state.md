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

## Tooling bootstrap (read first)
- Deterministic RE tooling is canonical in
  `/media/kartashoff/Storage/opensource/iced/test`; see `docs/re-tooling.md`.
- `scripts/re-bootstrap.sh` (read-only) verifies the `re` binary, `re.db`, its
  indexed blob sha256, the gate scripts, the Git hook and the OpenCode
  integration. It must **PASS** before analysis; if `re.db` is stale it prints
  the rebuild command.
- OpenCode from this directory auto-loads the tool-first RE context, the
  `openbrcm-re` skill and the project plugins via `.opencode/opencode.json`.
- Rule: query `re.db`/`re` before manual `objdump`/`readelf`/`r2`/`grep`
  (`AGENTS.md` §7).

## Commit state (IMPORTANT)
- `main` = `5f6c3d2b64ecf8c7c4beaf9d0c61607d27431551` (**PR #13 merged**,
  branch `m34d3b-mhf-value-closure`); `main == origin/main`. Prior: PR #11
  (`06bbd60`, D3B band-init / MHF provenance analysis), PR #12 (`6eff1ea`,
  tool-first RE bootstrap + OpenCode integration).
- Merged PR history (oldest → newest): **#2** `85d3013` (M3.4D2A), **#5**
  `65d61ce` (M3.4D2B analysis), **#6** `4146cd8` (M3.4D2B test), **#7**
  `2a7ba1d` (M3.4D3 analysis), **#8** `3bdef76` (M3.4D3A0 test), **#9**
  `12c3e7a` (M3.4D3A1 vendor-tail analysis), **#10** `839f007` (M3.4D3A1
  isolated test + the first D3B analysis), **#11** `06bbd60` (M3.4D3B band-init
  / MHF provenance analysis), **#12** `6eff1ea` (tool-first RE bootstrap +
  OpenCode integration), **#13** `5f6c3d2` (M3.4D3B band-0 MHF value closure).
  `main` contains M3.4D1 (`2029292`), the
  isolated modes `fw_validate_only=1` / `ucode_test_only=1` /
  `initvals_test_only=1` / `dma_test_only=1` / `d11_tail_test_only=1`,
  `src/ob_ucode.{c,h}`, `src/ob_initvals.{c,h}`, `src/ob_d3a0.{c,h}`,
  `src/ob_d3a1.{c,h}`, `docs/ucode_test.md`, `docs/m34d3_bsinitvals.md`,
  `docs/m34d3a1_vendor_tail{,_test}.md`, `docs/m34d3b_band_init.md`,
  `docs/re-tooling.md`, `scripts/re-bootstrap.sh`, `scripts/re.sh` and
  `.opencode/`.
- **M3.4D2B** (`initvals_test_only=1`, candidate `f27286f`, module SHA256
  `1258290cb491ea551a9fb4c4e820ecf3450ae7c23957b41e5eeada14f1d98290`),
  **M3.4D3A0** (`dma_test_only=1`, candidate `4fa1b57`, runtime `7e68fe24`,
  signed module SHA256
  `0282d9b253b40ca13eba3420058b6314be629cdf50d540e549510f726cd6af08`) and
  **M3.4D3A1** (`d11_tail_test_only=1`, candidate `42d74b8`, module
  `6ba2d853…`) are all `HARDWARE RUNTIME PROVEN` on BCM4352.
- **D3B analysis merged** via PR #11 (`06bbd60`); **value closure merged** via
  PR #13 (`5f6c3d2`). **MHF3 input PROVEN** by the 2026-09 read-only SPROM
  capture (PR #14): `mhfs[0..4] = {0x0100, 0x0000, 0x0000, 0x0000, 0x0080}`
  (all five PROVEN), **`D3B IMPLEMENTATION GO: YES`**.
- **D3B SPROM-evidence branch** `m34d3b-sprom-evidence` (from `main` @
  `5f6c3d2`): read-only capture of the already-read 234-word rev11 SPROM image
  (`sprom_evidence_only=1` + `ob_si_emit_sprom11()`), the offline decoder
  `scripts/sprom11_decode.py` and host tests. `IMPLEMENTED` / `STATIC TESTED` /
  `SIGNED` / **`HARDWARE RUNTIME PROVEN` (BCM4352, 2026-09)** on frozen
  candidate `739273c` (`openbrcm.ko` sha256 `538588e2…`); `antsel_type = 0`,
  `MHF3 = 0x0000`. Evidence `docs/m34d3b/d3b_sprom_capture.json`.
- Analysis branch `m34d3a1-vendor-tail-analysis` — **M3.4D3A1 vendor-tail
  recovery, `ANALYSIS COMPLETE`**, `D3A1 IMPLEMENTATION GO: YES`; report
  `docs/m34d3a1_vendor_tail.md`; merged via PR #9.
- Pre-commit documentation-discipline hook is active (`.githooks/`);
  `scripts/re-bootstrap.sh` must PASS before analysis; see `AGENTS.md`.

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
- M3.4D3A0 (isolated mode, branch `m34d3a0-dma-test`): `dma_test_only=1`
  **HARDWARE RUNTIME PROVEN on BCM4352** — tested candidate `4fa1b57`, runtime
  commit `7e68fe24`, signed module SHA256
  `0282d9b253b40ca13eba3420058b6314be629cdf50d540e549510f726cd6af08`, kernel
  `7.0.0-34-generic`. `insmod` rc=0; D2A (10850 words, PSM PASS, 11 iters) +
  D2B 610 common initvals (`w16=113`, `w32=497`) + D2B exit gate; IRQ source
  only (`INTRCVLAZY=0x01000000`, `I_RI`, `MACINTMASK=0`); 4 TX rings
  (`0x200/0x240/0x280/0x2c0`, `ADDRHIGH=0x80000000`, `CONTROL 0->0x801`) with
  **0 TX payload mappings**; FIFO0 RX (`0x220`, `CONTROL=0x84d`, programmed PTR
  `0x400`, 64 mappings, IDLE); bring-up validation PASS; then
  `RX/TX0..TX3 reset PASS` + `all DMA engines stopped` + `rings released` +
  `PASS - bring-up + teardown proven`. No BUG/Oops/WARNING/DMA-API error/lockup/
  reset timeout/FATAL/reboot-required/AER. Scope: **isolated DMA lifecycle only**
  (does NOT prove the vendor post-common tail, `sub_67efd`, NVRAM/BTC/rate/power
  SHM tail, band init, bsinitvals, AC PHY, PHY tables, radio, synth/PLL, channel,
  calibration, real RX completion, TX frame completion, scan or association).
  Evidence: `docs/m34d3a0_dma_test.md`.
- M3.4D3A1 (isolated mode, branch `m34d3a1-vendor-tail-test`):
  `d11_tail_test_only=1` **HARDWARE RUNTIME PROVEN on BCM4352** — tested
  candidate `42d74b8`, signed module SHA256
  `6ba2d853adef9860213498c32c8968bdbb027e59ac17ffe8a2c2670503ff5abd`, kernel
  `7.0.0-34-generic`. `insmod` rc=0; D2A/D2B prefix PASS; exact rev42 tail
  `sub_67efd -> T1 -> DMA -> T2 -> switch_macfreq`; `0x530`/`0x540` bounded
  expiry handled vendor-**non-fatal**; postconditions validated; **normal
  `rmmod` + verified DMA teardown**; STOP before `sub_6656c`/bsinitvals/PHY;
  no BUG/Oops/lockup/reset. Scope: vendor post-common/pre-PHY D11 tail only
  (does NOT prove band init, bsinitvals, AC PHY, radio, calibration, channel,
  real RX/TX). Evidence: `docs/m34d3a1_vendor_tail_test.md` §14.1.

## Analysis-only facts (not hardware proven here)
- M3.4C/C.1: exact vendor rev42 images recovered from `wlc_hybrid.o_shipped`;
  vendor 8-byte IV record format (terminator `0xffff`), **not** b43 IV.
- **M3.4D3 (analysis, historical):** band-switch initvals (`d11ac1bsinitvals42`, 73 records)
  + PHY boundary. Report `docs/m34d3_bsinitvals.md`; classification
  `docs/m34d3/bsinitvals_classification.{md,json}` via
  `scripts/analyze_bsinitvals.py`. Decisions (corrected, Appendix B):
  `wlc_phy_switch_radio` does **not** precede bsinitvals on the BCM4352/AC path
  (the `0x69594` call is NPHY/HT-gated; `wlc_bmac_mute` is skipped), so a
  vendor-ordered test can stop before real PHY/RF writes: **YES**. The
  post-common tail is **not D11-only** (it inits 4 TX DMA engines + FIFO0 RX and
  writes interrupt-source masks); Appendix C reverses it and yields **A/B/C/D =
  YES** (D3A0 DMA/IRQ-source → D3A1 remaining tail → D3B), conditional on host
  IRQ route off and a core-reset/reboot quiesce; Appendix D closes the D3A0
  blockers and returns **`D3A0 IMPLEMENTATION GO: YES`**. The analysis proposed
  the full vendor prefix; the shipped D3A0 is deliberately **`ISOLATED DMA
  LIFECYCLE TEST`** (see below) because `sub_67efd` and the NVRAM/BTC/rate/power
  SHM tail are not pinned — they remain D3A1 integration content.
- M3.4D2A: see the historical M3.4D2B section above.

## Canonical milestone status
Stated exactly:
- M3.4D2A = HARDWARE RUNTIME PROVEN
- M3.4D2B = HARDWARE RUNTIME PROVEN
- M3.4D2B analysis = COMPLETE
- M3.4D2B implementation = IMPLEMENTED / STATIC TESTED / SIGNED /
  HARDWARE RUNTIME PROVEN
- M3.4D3 = ANALYSIS COMPLETE / NOT IMPLEMENTED / NOT HARDWARE PROVEN
- M3.4D3A0 design = `D3A0 IMPLEMENTATION GO: YES` (analysis, Appendix D)
- M3.4D3A0 = IMPLEMENTED / STATIC TESTED / SIGNED / HARDWARE RUNTIME PROVEN
  (isolated DMA lifecycle only; candidate `4fa1b57`)
- M3.4D3A1 = `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE RUNTIME
  PROVEN` (isolated `d11_tail_test_only=1` vendor post-common / pre-PHY tail;
  reuses the proven D3A0 DMA lifecycle in its vendor position; STOPS before
  `sub_6656c`; candidate `42d74b8`, module `6ba2d853…`; normal unload + DMA
  teardown proven)
- M3.4D3B = `ANALYSIS ONLY` / NOT IMPLEMENTED / NOT HARDWARE PROVEN (band init
  / `d11ac1bsinitvals42`; design `docs/m34d3b_band_init.md`; `D3B
  IMPLEMENTATION GO: YES` — MHF1..MHF5 all PROVEN; final vector
  `{0x0100, 0x0000, 0x0000, 0x0000, 0x0080}`; MHF3 PROVEN by the SPROM-evidence
  capture)
- M3.4D4 (AC PHY bring-up) = NOT STARTED / NOT HARDWARE PROVEN

The hardware-proven milestones are narrow (see below); the later
PHY/radio/channel stages remain **unproven**.

## M3.4D3A0 — isolated DMA lifecycle test (HARDWARE RUNTIME PROVEN)

**D3A0 TYPE: `ISOLATED DMA LIFECYCLE TEST`** — NOT a full vendor-prefix
reproduction. D3A0 starts from the proven D2B exit and programs only the
provenance-pinned D11/clock/IRQ-source prerequisites needed to exercise the
vendor DMA engines. Vendor-before-DMA stages that are not pinned (`sub_67efd`
TXE0/FIFO fixup; runtime NVRAM/BTC/rate/power SHM tail) are intentionally
omitted and MUST be restored by D3A1 integration before normal PHY bring-up.
See `docs/d3a0_dma_test_design.md` §0/§4.1.

Status: **`IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE RUNTIME PROVEN`
on BCM4352** (candidate `4fa1b57`, runtime commit `7e68fe24`, signed module
SHA256 `0282d9b253b40ca13eba3420058b6314be629cdf50d540e549510f726cd6af08`,
kernel `7.0.0-34-generic`). Proof: full isolated lifecycle
allocate/map → program → hardware validation → verified stop → release;
`PASS - bring-up + teardown proven`, no kernel fault. Module param
**`dma_test_only=1`**; mutually exclusive with the other isolated modes (any
conflict → `-EINVAL` before hardware). Files: `src/ob_d3a0.{c,h}`,
`tests/host/ob_d3a0_test.c`, `tests/kunit/ob_d3a0_kunit.c`. Full evidence:
`docs/m34d3a0_dma_test.md`.

- **Entry state = the full hardware-proven D2B exit**: runs the shared
  `ob_initvals_run_d2b()` (the proven `ob_ucode_run_d2a()` core plus EXACTLY the
  610 common-initvals records — 113 × 16-bit, 497 × 32-bit — and the
  postcondition gate). There is no D2A-only bypass; the D3A0 prefix is reached
  only after `ob_initvals_post_ok()` and a live re-read
  (`ob_d3a0_check_d2b_exit`: `MACCONTROL=0x04020402`, `MACINTMASK=0`,
  FIFO0..3=`0x01c4/0/0/0x079e`, `SHM[0x14]=0xb4`) pass. Only then:
- Pinned D11/clock/IRQ-source prerequisites (`intrcvlazy[0]=0x01000000` →
  `MACCONTROL` RMW `0x04020402→0x44020402` → `macintstatus` W1C `MI_GP1` →
  `intctrlregs[0].intmask=I_RI` → `macphyclk_set` ON → machwcap SHM caps), then
  DMA. These preserve the vendor register values and relative order, but are not
  the complete vendor prefix (see TYPE above).
- DMA: four TX channels (`0x200/0x240/0x280/0x2c0`, 512×16 B, 8192-aligned,
  `ADDRHIGH=0x80000000`, `control = read|XE|PD`, no ptr/descriptors → **zero TX
  payload mappings**) and FIFO0 RX (`0x220`, 256 desc, exactly 64 posted 2048-B
  `DMA_FROM_DEVICE` buffers, `CONTROL=0x84d`, `PTR=0x400`).
- Host IRQ impossible: `MACINTMASK` stays 0, no `request_irq`, no
  `bcma_host_pci_irq_ctl`, no `MI_DMAINT`; `EN_MAC` stays 0.
- Fail-closed quiesce: clear `I_RI`; `dma_rxreset`; `dma_txreset` per PROGRAMMED
  TX channel (bounded 10 ms polls); verify every PROGRAMMED engine stopped.
  **Only then** `engines_stopped=true`/`free_allowed=true` and a free may occur.
  If any per-channel reset fails, `bcma_core_disable` is attempted as
  **containment only** and its real `bcma_core_is_enabled()` readback recorded
  as `core_contained`; this **never** authorizes a free. In that case `fatal` is
  set, a **module-wide fatal latch** and a diagnostic record of the retained
  rings are kept, and the module is pinned (`__module_get`) so the state cannot
  disappear via rmmod/rebind; probe is kept successful so the bound device and
  its devres retain the state. Only a reboot clears it. `ob_remove()` honours
  this and never frees. Operator rule: **FATAL → do not unbind/rebind → reboot.**
- STOPS before remaining D3A1 tail / `sub_6656c` / bsinitvals / `wlc_phy_init`
  / PHY / radio / channel / mac80211.

## M3.4D3A1 — vendor post-common / pre-PHY tail test (HARDWARE RUNTIME PROVEN)
Status: **`M3.4D3A1 = IMPLEMENTED / STATIC TESTED / SIGNED / HARDWARE RUNTIME
PROVEN` on BCM4352** (candidate `42d74b8`, module `6ba2d853…`, kernel
`7.0.0-34-generic`).
Isolated mode `d11_tail_test_only=1` reproduces the exact rev42 vendor order
(`sub_67efd -> T1 -> DMA -> T2 -> switch_macfreq`) and STOPS before
`sub_6656c`; the DMA sub-lifecycle is the hardware-proven D3A0 one, reused in
its vendor position. New: `src/ob_d3a1.{c,h}`, `tests/host/ob_d3a1_test.c`,
`tests/kunit/ob_d3a1_kunit.c`. Report:
`docs/m34d3a1_vendor_tail.md`; proof `docs/m34d3a1_vendor_tail_test.md` §14.1.
Blob sha256 `352a6e349f…`; read-only RE.
- **Proof:** one-shot isolated `d11_tail_test_only=1`; `insmod` rc=0; full
  `sub_67efd` tail (`0x530`/`0x540` expiry non-fatal), T1/DMA/T2 in vendor
  order, postconditions validated, **normal `rmmod` + verified DMA teardown +
  STOP before `sub_6656c`/bsinitvals/PHY**; no BUG/Oops/lockup/reset.
- **Scope boundary (NOT proven):** band init, bsinitvals, `sub_6656c`,
  `wlc_phy_init`, AC PHY, radio, calibration, channel, real RX/TX, scan,
  association.
- **Key correction: the vendor interleaves DMA inside the tail** —
  `T1 (sub_67efd → MACCONTROL/macphyclk/SCR/SFBL/ifs) → DMA (4× txinit +
  rxinit + rxfill) → T2 (BTC/NVRAM SHM, 0x78c/0x78e/0x790, switch_macfreq) →
  STOP before sub_6656c`. Design A (tail→DMA) and Design B (DMA→tail) are both
  non-vendor-faithful; preserve the interleaving.
- Ordering (direct control flow, all proven): DMA is **after** `sub_67efd`,
  MACCONTROL `0x04020402→0x44020402`, and `macphyclk_set`; DMA is **before** the
  NVRAM/BTC SHM tail, bsinitvals (`0x669bd`) and `wlc_phy_init` (`0x669df`).
- `sub_67efd` (`0x67efd`): rev42 executes machwcap read + `0x542/0x540` FIFO
  flush/cmd + 7-entry loop (42 writes) + 42-entry loop (168 writes); the RXE
  block (`0x42c/0x42e/0x43a/0x43c/0x406`) is gated `phyrev>0x2a` and **not**
  executed for rev42. No DMA/IRQ/PHY/radio access; two bounded polls (0x540
  bit0 clear; 0x530 whole-word zero), both **non-fatal on expiry** in the blob
  (no error path). Mandatory on the vendor rev42 path; safe in an isolated
  no-PHY test.
- Omitted SHM groups: `M_MBURST_SIZE`(0x80)/`M_MAX_ANTCNT`(0x5c);
  `M_MACHW_VER`(0x16)/`M_MACHW_CAP_L/H`(0xc0/0xc2); SCR SRL/LRL + SFBL/LFBL;
  NVRAM `btc_params%d` (119) + 4 fixed BTC values (`0x7530/0x4e20/0x7530/0x753`)
  and `btc_flags`→`sub_62b79` (mhf ×5); plus the **UNKNOWN** six-byte group
  `0x78c/0x78e/0x790` sourced from `wlc+8..0xd`.
- IRQ source: `intrcvlazy[0]=0x01000000` → `macintstatus` W1C `MI_GP1=0x4000` →
  `intctrlregs[0].intmask=I_RI=0x10000`; `macintmask` stays 0; `MI_DMAINT` not
  set; host route off. First real PHY op = `wlc_phy_anacore` (`0xbac84`).
- D3A1 boundary: first included call `sub_67efd` (`0x68bab`), last included
  `wlc_bmac_switch_macfreq` (`0x695cb`), STOP before `sub_6656c` (`0x695d8`).
- Blocker closure (§15 of the report): the `0x78c/0x78e/0x790` six bytes are
  `wlc_pub+8` = `cur_etheraddr` (device MAC) — `wlc_bmac_attach` parses NVRAM
  `macaddr` → `wlc_hw+0x178`; `wlc_attach` `memcpy(wlc_pub+8, …)`. `r13 =
  wlc_info`, `r13+0x20 = wlc_info->hw`; the SCR `0x24` write is a
  read-modify-write of SCR `0x24` (read at `0x68773`), **skipped on the first
  init** because `wlc_info_init` sets `wlc_info+0x718 = 1`. `getvar` scans a
  NUL-separated `name=value` per-hw buffer then the global `nvram_get` list
  (`srom_var_init` SPROM vars + `nvram_init` `nvram.txt`). `btc_params`/
  `btc_flags` are absent on ASUS PCE-AC56 ⇒ skip (no zero-fill). `M_MAX_ANTCNT`
  `0x0a` = upstream vanilla `ANTCNT` (antenna swap threshold).
- Remaining non-blocking unknown: only the **symbolic name** of the SHM
  `0x78c/0x78e/0x790` slots (value/source proven; microcode-only consumer).

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
  B YES, C YES, D YES** (§C.18). `MACCONTROL` bit30 = `MCTL_DISCARD_PMQ`;
  `macphyclk_set` = D11 core cflags bit4 (`SICF_MPCLKE`); `switch_macfreq`
  writes D11 `0x62e/0x630` from PMU VCO. Report §0, §16/§17, Appendices B/C.
- D3A0 blocker closure (Appendix D): 4-TX map (BK/BE/VI/VO @ 0x200/0x240/0x280/
  0x2C0); TX CONTROL is RMW `read(control) | XE | (PD?)` (per-FIFO cap bits
  re-asserted, not a constant); `ddoffsethigh=dataoffsethigh=0x80000000`
  (bus core 0x83C + dma64); `intrcvlazy[0]=0x01000000` (constant, set in attach
  0x69faf);   `dma_txreset 0xf64a`, `dma_rxreset 0xf5ef` (bounded 10 ms polls);
  vendor order is **IRQ-source config before DMA init**
  (`intrcvlazy`→`macintstatus`→`intctrlregs[0].intmask=I_RI`, then
  `dma_txinit x4`→`dma_rxinit`→`dma_rxfill`). Quiesce = per-channel reset
  **with verification** (`macintmask=0`, clear `I_RI`, `dma_rxreset`,
  `dma_txreset` per initialized channel); `bcma_core_disable` is containment
  fallback only and never authorizes a free after unverified reset. **`D3A0
  IMPLEMENTATION GO: YES`** (§D.19), still NOT IMPLEMENTED / NOT HARDWARE
  PROVEN.

## Current milestone
**M3.4D3B — band init / `d11ac1bsinitvals42` (analysis/design).**
Status: **`ANALYSIS ONLY` / NOT IMPLEMENTED / NOT HARDWARE PROVEN**;
`D3B IMPLEMENTATION GO: YES` — `VALUE FULLY PROVEN`, no remaining blocker. The
band-0 MHF vector is `{0x0100, 0x0000, 0x0000, 0x0000, 0x0080}` with all five
words PROVEN (`docs/m34d3b_band_init.md` §3.8). MHF3 was closed by the 2026-09
read-only hardware SPROM capture (MHF4/MHF5/… PROVEN earlier). The last
hardware-proven milestone is **M3.4D3A1** (see "Last completed hardware test");
the D2B detail below is retained as the historical common-initvals result.

**D3B MHF3 SPROM-evidence capture** (`m34d3b-sprom-evidence`, from `main` @
`5f6c3d2`, PR #14): `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / **`HARDWARE
RUNTIME PROVEN` (BCM4352, 2026-09)** — the existing read-only SPROM path emits
the already-read, CRC-validated 234-word rev11 image (`sprom_evidence_only=1`,
`ob_si_emit_sprom11()`, zero extra MMIO). Frozen candidate `739273c`
(`openbrcm.ko` sha256 `538588e2…`), `insmod`/`rmmod` rc=0; `revision=11`,
`crc=calc=0xc0`, MAC anchor `2cfd a161 4025`. Decoded: `boardtype=0x85ba`,
`boardflags=0x10001000`, `aa2g=aa5g=7`, `antswitch=0` -> `antsel_type=0` ->
**MHF3 = `0x0000`**. Evidence: `docs/m34d3b/d3b_sprom_capture.json`,
`docs/m34d3b_sprom_evidence.md`.

### Historical — M3.4D2B (isolated rev42 common-initvals test, PROVEN)
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
**M3.4D3A1 — isolated vendor post-common / pre-PHY D11 tail test.**
Status: **`IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE RUNTIME
PROVEN` on BCM4352** (candidate `42d74b8`, module
`6ba2d853adef9860213498c32c8968bdbb027e59ac17ffe8a2c2670503ff5abd`). The
one-shot isolated `d11_tail_test_only=1` reproduced the exact rev42 tail
(`sub_67efd -> T1 -> DMA -> T2 -> switch_macfreq`), reused the proven D3A0 DMA
lifecycle in its vendor position, validated the deterministic postconditions,
ran the verified DMA teardown, unloaded cleanly (`rmmod`), and STOPPED before
`sub_6656c`. Scope: post-common / pre-PHY D11 tail only; band init, bsinitvals,
AC PHY, radio, calibration, channel and real RX/TX remain **unproven**.
Prior: **M3.4D3A0** (isolated DMA lifecycle, candidate `4fa1b57`) and
**M3.4D2B** (isolated rev42 common initvals, candidate `f27286f`) — both
`HARDWARE RUNTIME PROVEN`.
Prior historical: **M3.4D2A — D11 rev42 ucode upload + PSM start only.**
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
M3.4D3A1 isolated `d11_tail_test_only=1` **HARDWARE RUNTIME PROVEN on BCM4352**
(tested candidate `42d74b8`, module SHA256
`6ba2d853adef9860213498c32c8968bdbb027e59ac17ffe8a2c2670503ff5abd`; normal
`rmmod` + verified DMA teardown + STOP before `sub_6656c`). Prior: M3.4D3A0
`dma_test_only=1` HARDWARE RUNTIME PROVEN (candidate `4fa1b57`); M3.4D2B
`initvals_test_only=1` (candidate `f27286f`); M3.4D2A `ucode_test_only=1`
(candidate `7265f9d`); M3.4D1 `fw_validate_only=1` runtime PASS.

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
bsinitvals) → D4 (PHY). Appendix D closes the D3A0 blockers and returns
**`D3A0 IMPLEMENTATION GO: YES`** (4-TX map; TX CONTROL RMW; `0x80000000` high
word; `intrcvlazy[0]=0x01000000`; `dma_txreset 0xf64a`/`dma_rxreset 0xf5ef`;
quiesce = per-channel reset + `bcma_core_disable`).

**D3A0 is now HARDWARE RUNTIME PROVEN on BCM4352** on `m34d3a0-dma-test`
(candidate `4fa1b57`, runtime `7e68fe24`; PR #8, merged to `main` via a normal
merge commit). It was classified/exercised as an **`ISOLATED DMA
LIFECYCLE TEST`** (not a full vendor-prefix reproduction); the run proved the
complete isolated lifecycle allocate/map → program → hardware validation →
verified stop → release (`PASS - bring-up + teardown proven`, no kernel fault).
Evidence: `docs/m34d3a0_dma_test.md`.

**M3.4D3A1 is now HARDWARE RUNTIME PROVEN on BCM4352** (candidate `42d74b8`,
module `6ba2d853…`) on `m34d3a1-vendor-tail-test`; report
`docs/m34d3a1_vendor_tail_test.md` §14.1 (recovered ordering/source evidence in
`docs/m34d3a1_vendor_tail.md`). The one-shot isolated
`d11_tail_test_only=1` run executed the proven D2A/D2B core, the exact rev42
tail (`sub_67efd -> T1 -> DMA -> T2 -> switch_macfreq`) with the D3A0 DMA
lifecycle in its vendor position, validated the postconditions, ran the
verified DMA teardown, unloaded cleanly (`rmmod`), and **STOPPED before
`sub_6656c`**. No BUG/Oops/lockup/reset. See `docs/d3a0_dma_test_design.md`,
`docs/m34d3_bsinitvals.md` and `docs/m34d3a1_vendor_tail_test.md`.

**Next action — D3B unblocked (`GO: YES`); D4 after.** M3.4D3B (band init /
`d11ac1bsinitvals42`) is analyzed in
[`docs/m34d3b_band_init.md`](m34d3b_band_init.md): boundary = `sub_6656c`
entry (0x6656c) through the `sub_60f67` applier return (0x669c2), STOP before
`wlc_phy_init` (0x669df). The pre-bs helper `sub_62766` is
`wlc_bmac_write_mhf` (writes MHF1..5 to SHM `0x5e/0x60/0x62/0x78/0xd4` from
`band-0 mhfs[0..4]`). Value closure (§3.8): `mhfs[0..4] = {0x0100, 0x0000,
0x0000, 0x0000, 0x0080}` —
MHF1 `0x0100` (pub+0x54 init `0xffffffff`, no initial-up zeroer; the only
zeroer is the runtime iovar `wlc_doiovar`);
MHF2 `0x0000` (BCM4352 `bustype==1`, `buscoretype==0x83c`, so
`si_pci_war16165=0` => `wlc+0x60=0`);
MHF3 `0x0000` (hardware capture: `boardtype=0x85ba`, `boardflags & 0x8 = 0`,
`antswitch=0` => `antsel_type=0`);
MHF4 `0x0000` (4313-only site skipped);
MHF5 `0x0080` (band phytype `0x0b != 7` => `stf+0x59=1`).
No value/provenance/safety blocker remains; `D3B IMPLEMENTATION GO: YES`. D4
(AC PHY bring-up: `wlc_phy_init` -> `wlc_phy_anacore`, first PHY-indirect MMIO)
follows. Neither D3B nor D4 has code or hardware proof yet.

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
Current (D3B MHF3 SPROM-evidence capture, static-only): the one-shot read-only
`sprom_evidence_only=1` run is **DONE** (`HARDWARE RUNTIME PROVEN`); STOP after
the evidence/docs commit; **no D3B implementation**, no further
`insmod`/`rmmod`/`modprobe`, no hardware, no new D3B reverse engineering. The
MHF3 input is resolved; D3B implementation awaits its own planning branch.

Runtime STOP (last proven, M3.4D3A1): the isolated `d11_tail_test_only=1` path
returns after the vendor tail through `wlc_bmac_switch_macfreq`, after the
verified DMA teardown, and **before `sub_6656c`** / bsinitvals / PHY / radio /
channel / mac80211.

Next runtime STOP (D3B, **not implemented**): after the `sub_60f67` applier
return (`0x669c2`) and **before `wlc_phy_init`** (`0x669df`), i.e. before
`wlc_phy_anacore` and any PHY-indirect/radio window. `D3B IMPLEMENTATION GO:
YES`, but D3B may only be implemented in a dedicated (planning-first) milestone.

D2A/D2B historical STOP: neither earlier path reaches bsinitvals/`sub_6656c`/
PHY/radio/channel/DMA/IRQ/mac80211.

## Do NOT change blindly
- `src/ob_rx.c` / `ob_rx.h`: RX PTR model (`rcvptrbase=0`, `PTR=rxout*16`),
  `RX CONTROL=0x84d`, `addrhigh=0x80000000`, ring 256.
- `src/ob_dma.c`: `DMA_BIT_MASK(32)` (not 64).
- `src/ob_fw.{c,h}`: exact vendor firmware names/sizes/FNV guards.
- `src/ob_ucode.{c,h}`: recovered MACCONTROL/OBJADDR/poll constants and the
  shared `ob_ucode_run_d2a()` D2A core (do not fork it for D2B).
- `src/ob_initvals.{c,h}`: exact 610/113/497 shape + postcondition constants.
- `MOC/` signing material (outside this repo): never modify/read the private key.

## Historical branch contents (`m34d2b-initvals-test`, merged via PR #6)
Implementation: `src/ob_initvals.{c,h}`, `src/ob_ucode.{c,h}` (shared D2A core +
mode policy), `src/ob_core.{c,h}` (mode param/guards), `src/ob_fw.{c,h}`
(initvals request), `Makefile`. Tests: `tests/host/ob_initvals_test.c`,
`tests/host/ob_ucode_test.c`, `tests/kunit/ob_initvals_kunit.c`,
`tests/kunit/ob_ucode_kunit.c`, `tests/host/Makefile`. Docs:
`docs/m34d2b_initvals_test.md`, `docs/m34d2b_common_initvals.md`,
`docs/milestones.md`, `docs/agent-state.md`.
Note: `scripts/runtime-test.sh` and `.opencode/` remain untracked local tooling.
