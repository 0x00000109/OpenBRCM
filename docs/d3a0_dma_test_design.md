# D3A0 — isolated DMA lifecycle test (design record)

**D3A0 TYPE: `ISOLATED DMA LIFECYCLE TEST`. This is NOT a full vendor-prefix
reproduction.** D3A0 starts from the hardware-proven D2B exit state and programs
**only** the provenance-pinned D11/clock/IRQ-source prerequisites required to
exercise the vendor DMA engines (4 TX + FIFO0 RX) independently, then validates
and tears them down. It is a primitive/safety test, not vendor bring-up.

**Status: `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `NOT HARDWARE PROVEN`.**
Implementation on branch `m34d3a0-dma-test`: `src/ob_d3a0.{c,h}`,
`tests/host/ob_d3a0_test.c`, `tests/kunit/ob_d3a0_kunit.c`, module param
`dma_test_only=1`. No hardware run was performed. Last hardware-proven
milestone: **M3.4D2B**.

## 0. Scope classification (must not be overstated)

D3A0 intentionally omits the vendor stages that lie between the D2B exit and
`dma_txinit`/`dma_rxinit` and are **not sufficiently pinned for implementation**:

- `sub_67efd` (TXE0/FIFO fixup).
- the runtime NVRAM/BTC/rate/power SHM tail.

Consequences, stated explicitly:

- D3A0 does **not** claim to reproduce the complete vendor post-common prefix
  and must never be described as "the exact vendor pre-PHY prefix".
- The omitted stages are not prerequisites for **programming/resetting** the DMA
  engines (which is all this test does), so the isolated lifecycle test is
  meaningful on its own.
- **Final normal-driver integration MUST restore the complete vendor ordering**
  (including `sub_67efd` and the SHM/rate/power tail, in vendor position) before
  band init / bsinitvals / PHY bring-up. **D3A1 is that integration task**, not
  a runtime step simply appended after the DMA engines are armed.

Implementation notes / deviations from this design, all documented:
- **D2B entry (audit fix).** The implementation runs the shared
  `ob_initvals_run_d2b()` (D2A + 610 common initvals + postconditions) before the
  D3A0 prerequisite stage and re-checks the live D2B exit state. The first cut
  skipped the 610 common initvals (D2A-only) — a real bug caught by the static
  pre-hardware audit.
- **Containment is not free authorization (audit fix).** A per-channel reset
  failure followed by a verified core disable records `core_contained` but sets
  `fatal`; it never authorizes a free. Only every programmed engine's own
  verified normal stop sets `engines_stopped`/`free_allowed`.
- The D3A0 prerequisite stage implements only the exactly-pinned operations: SHM
  `0x80=8`, `0x5c=0x0a`, `intrcvlazy[0]=0x01000000`, `MACCONTROL` RMW,
  `tsf_cfprep`/`tsf_cfpstart`, `macintstatus` W1C, `intctrlregs[0].intmask=I_RI`,
  `macphyclk` ON, `M_MACHW_VER`/`M_MACHW_CAP_L/H`.
- Naming: `ob_d3a0_rx_map()` maps exactly 64 RX `DMA_FROM_DEVICE` buffers; the
  TX rings publish base+CONTROL only, so there are **zero TX payload mappings**.
  (`test_tx_reg_map` / `ob_d3a0_tx_reg_map_test` test the TX *register* map, not
  DMA mappings.)

This branch (`m34d3a0-dma-test`) records the exact D3A0 implementation and its
design. Source: `docs/m34d3_bsinitvals.md` Appendix D (D3A0 blocker closure,
GO = YES) and Appendix C (DMA/IRQ reversal).

## 1. Scope and non-goals

D3A0 exercises the vendor DMA/IRQ-source engines from the D2B state using only
pinned prerequisites (see §0 for the omitted, not-pinned vendor stages):

- **No** PHY/radio.
- **No** `EN_MAC`.
- **No** host IRQ route (`bcma_host_pci_irq_ctl(true)` is not called).
- **No** `request_irq` requirement for this path.
- **No** mac80211.
- **No** claim of full vendor-prefix ordering.

`MACINTMASK` (`D11+0x12C`) stays 0 and the BCMA/PCI IRQ route stays disabled, so
CPU IRQ delivery is impossible at this stage (Appendix C.8/C.9).

## 2. Order of the pinned prerequisites (must not be reordered)

D3A0 is an isolated lifecycle test, but it preserves the order that the
recovered vendor code uses for the operations it *does* implement (interrupt
source before DMA init). The omitted vendor stages of §0 are NOT inserted here:

```
proven D2B preparation = ob_ucode_run_d2a() + EXACTLY 610 common initvals
    (113 w16 / 497 w32) + postcondition gate   [ob_initvals_run_d2b]
  ->
live re-read of the D2B exit state            [ob_d3a0_check_d2b_exit]
    (MACCONTROL=0x04020402, MACINTMASK=0, FIFO=0x01c4/0/0/0x079e, SHM14=0xb4)
  ->
pinned D11/clock/IRQ-source prerequisites needed for independent DMA
    programming (intrcvlazy / macintstatus / intctrlregs[0] / macphyclk /
    machwcap SHM)
  ->
INTRCVLAZY[0] (D11+0x100) = 0x01000000
  ->
MACINTSTATUS (D11+0x128) W1C = MI_GP1 (0x4000)
  ->
per-FIFO INTCONTROL[0].intmask (D11+0x24) = I_RI (0x10000)
  ->
MACINTMASK (D11+0x12C) remains 0
  ->
host BCMA/PCI IRQ route remains disabled
  ->
allocate / initialize DMA resources (8 KiB align; 32-bit DMA window)
  ->
program TX DMA channels 0..3
  ->
program RX DMA
  ->
post 64 RX buffers
  ->
validate deterministic DMA state
  ->
ob_dma_quiesce (per-channel reset -> verify every engine stopped)
  ->
only after verified stop: dma_unmap/free Linux DMA resources
  ->
STOP
```

Vendor anchors (`.text` offsets in `wlc_hybrid.o_shipped`, sha256
`352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743`):
`0x69006` `intrcvlazy[0]`; `0x69070` `macintstatus`; `0x69082`
`intctrlregs[0].intmask`; `0x6921c` `dma_txinit` x4; `0x69236` `dma_rxinit`;
`0x69243` `dma_rxfill`.

## 3. Exact DMA facts to preserve

### TX (4 channels)

| ch | logical FIFO | D11 base |
|---|---|---|
| TX0 | `TX_AC_BK_FIFO` | `0x200` |
| TX1 | `TX_AC_BE_FIFO` | `0x240` |
| TX2 | `TX_AC_VI_FIFO` | `0x280` |
| TX3 | `TX_AC_VO`/`TX_CTL_FIFO` | `0x2C0` |

- `ntxd = 512`; descriptor = 16 bytes; ring bytes = 8192; alignment = 8192.
- `ADDRHIGH = 0x80000000`; `addrlow = ring_pa` (32-bit window).
- `control = RMW(read(control) | XE(0x1) | (PD(0x800) iff parity not enabled))`
  — preserve the per-channel capability fields; do **not** hard-code one
  synthetic TX CONTROL constant.
- `txin = txout = 0`; `txavail = ntxd - 1`.
- **No TX payload mappings; no TX descriptor payloads posted.**
- `ptr` is not programmed by TX init.

### RX (FIFO0)

- block = `0x220`, `nrxd = 256`, descriptor = 16 bytes, alignment = 8192.
- `rxpost = 64`, buffer size = 2048, `rxoffset = 38`.
- `CONTROL = 0x0000084D`; `PTR = 0x400`; `ADDRHIGH = 0x80000000`.
- DMA addresses constrained to the 32-bit window; expected state = `IDLE`.

### Interrupt source

- `INTRCVLAZY = 0x01000000` (`1<<24`).
- `I_RI = 0x00010000`; `I_RI != MI_DMAINT` (`MI_DMAINT = 1<<15`).
- `MACINTMASK` stays 0; host IRQ route stays disabled.

## 4. Quiesce and cleanup contract (mandatory)

Free permission requires **every programmed engine to have its own verified
normal per-channel stop**:

```
MACINTMASK = 0
clear/disable relevant per-FIFO sources (intctrlregs[0].intmask &= ~I_RI;
    ack owned macintstatus bits)
  -> dma_rxreset(di0)                       (control=0; poll status0 & 0xf0000000 == 0)
  -> dma_txreset(every PROGRAMMED TX DMA channel)
  -> VERIFY every PROGRAMMED engine is stopped/disabled (status0 state == 0)
  -> engines_stopped = true; free_allowed = true
```

Only after that may the code `dma_unmap_single`, free skbs, and free rings
(`dma_pool_free`/`dma_free_coherent`).

Explicitly separated lifecycle flags:

| flag | meaning | authorizes free? |
| :--- | :--- | :--- |
| `engines_stopped` | every programmed engine had its own verified normal stop | no (necessary) |
| `core_contained` | after a reset failure, core reset observed disabled | **never** |
| `free_allowed` | set only together with `engines_stopped` | yes (with `engines_stopped`, `!fatal`) |
| `fatal` | any programmed engine could not be verified stopped | never |

Invariant: `free_allowed => every programmed DMA engine has a verified normal
stop`. `ob_d3a0_can_free()` enforces `!fatal && free_allowed && engines_stopped
&& !hw_active`.

- `bcma_core_disable`/core-reset is **containment only** and **never authorizes a
  free**. If any per-channel reset fails, the code records `core_contained`
  (informational) if the core disable verifies, then sets `fatal` and retains
  all DMA memory.
- No invented PCIe flush mechanism. BCMA `bcma_core_disable` asserts core reset
  and waits `RESET_ST` with register readbacks. The accepted containment check is
  the **real readback** `bcma_core_is_enabled() == false` (reads `BCMA_IOCTL`
  clock bits and `BCMA_RESET_CTL`), not a cached software flag. Honest caveat:
  **the core reset does NOT by itself prove the PCIe bridge has drained
  already-outstanding reads/writes**, which is exactly why it cannot authorize a
  free. The retained DMA memory makes any late/stray transaction harmless.
- Track per-channel initialization state so partial-bring-up cleanup never
  resets/frees an uninitialized resource and never misses an initialized one.
- **Fatal retention rule (cases B/C/D).** If any per-channel reset fails — with
  or without verified containment — set `lc->fatal`, set a **module-wide
  latch**, record the retained ring DMA addresses + mapped count in a static
  diagnostic record, and `__module_get(THIS_MODULE)` so rmmod cannot unload the
  owning text. `ob_probe` keeps the probe **successful** in this state so the
  device stays bound and `@hw` (devres) is retained; the latch also blocks any
  D3A0 re-entry for the module's lifetime. The invariant: an unverified live DMA
  engine can never outlive the memory it references, and the fatal state cannot
  silently disappear through failed probe/unbind/rebind. Only a reboot clears
  it.

`ob_dma_quiesce()` is mandatory before any `dma_unmap_single`, skb free, ring
free, or isolated-mode teardown.

### 4.1 Vendor-before-DMA stages intentionally omitted

Because D3A0 is an isolated lifecycle test (§0), it does **not** execute the
vendor stages that precede `dma_txinit`/`dma_rxinit` but are not pinned:
`sub_67efd` (TXE0/FIFO fixup) and the runtime NVRAM/BTC/rate/power SHM tail.
Normal-driver integration (D3A1) MUST restore them, in vendor order, before band
init/bsinitvals/PHY bring-up.

### 4.2 Fatal state and manual sysfs unbind (operator rule)

The module pin set by `ob_d3a0_latch_fatal()` blocks `rmmod`, but it cannot block
a manual `echo <bdf> > /sys/.../unbind` (or driver-core removal). If the BCMA
device is unbound while the fatal latch is set:

- `ob_remove()` → `ob_d3a0_remove()` runs, sees `lc.fatal`, logs the retained
  ring addresses, sets drvdata to NULL and returns **without freeing**.
- the `devm`-allocated `@hw` is then released, so the in-`@hw` ring pointers are
  gone, but the **raw** `dma_pool` pages and RX skbs are not `devm`-managed and
  remain allocated (leaked). The mappings remain valid and are never unmapped.
- because the device is no longer bound, another driver could subsequently bind
  the D11 core; any driver that resets/reprograms the core would also stop the
  engines, and the retained (leaked) memory keeps any late PCIe transaction
  harmless.

There is **no simple repository-supported BCMA mechanism** to suppress manual
bind/unbind for just the isolated test mode without touching normal
architecture, so none is added. The exact operator rule is therefore:

> **FATAL DMA STATE → DO NOT UNBIND / DO NOT REBIND / REBOOT ONLY.**

The module pin enforces "no `rmmod`"; the operator must not bypass it with sysfs
unbind/rebind. A reboot is the only correct recovery.

## 5. Deterministic D3A0 postconditions

TX0..TX3: `control` XE=1 (capability bits = attach-time read); `addrlow` = ring
base; `addrhigh = 0x80000000`; `status0` not DISABLED.
RX: `control = 0x84D`; `ptr = 0x400`; `addrhigh = 0x80000000`; `status0` RS=IDLE;
`status1` error bits 0.
Global: `macintmask = 0`; `intctrlregs[0].intmask = 0x10000`;
`intrcvlazy[0] = 0x01000000`; `maccontrol = 0x44020402`.
Use field/state checks, not raw RX `ptr` equality.

## 6. Bring-up and teardown in one run

The first D3A0 hardware test must prove **both** bring-up and safe teardown in a
single run. If teardown cannot be verified, revert to reboot-only and do not
free DMA resources.

## 7. Formal status

- D3A0 `IMPLEMENTATION GO = YES` (analysis, `docs/m34d3_bsinitvals.md` §D.19).
- D3A0 = `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `NOT HARDWARE PROVEN`.
- A static pre-hardware audit found that the first implementation skipped the 610
  common initvals (D2A-only); it now runs the shared D2B applier first.
- M3.4D3 = `ANALYSIS ONLY`; last hardware-proven = **M3.4D2B**.

## 8. Prepared future hardware procedure (NOT executed)

Prepared but intentionally **not run**. Requires explicit human approval and a
quiet machine (no other openbrcm activity). Uses only `dma_test_only=1`; no
`runtime-test.sh`, no combined isolated modes.

Frozen artifacts (this branch). Earlier candidates `c214f5eb…` (commits
`8a59bf1`+`b33e7e3`) and `76d6ec29…` (commit `90a5ae0`) are **SUPERSEDED**: the
first lacked the D2B common-initvals entry and the second treated core-reset
containment as free authorization. Neither must ever be used.
- implementation + docs commit: the commit that contains this document
- built + signed module: `openbrcm.ko`
  SHA256 `0282d9b253b40ca13eba3420058b6314be629cdf50d540e549510f726cd6af08`
  (`signer: Broadcom Driver MOK`, `sig_hashalgo: sha256`)

```
# 0. verify the frozen module hash
sha256sum openbrcm.ko
#   expect 0282d9b253b40ca13eba3420058b6314be629cdf50d540e549510f726cd6af08

# 1. ensure no stale module is loaded
lsmod | grep -c '^openbrcm '    # expect 0

# 2. load only the D3A0 isolated mode
sudo insmod openbrcm.ko dma_test_only=1

# 3. capture the D3A0 evidence (bring-up validation + quiesce + PASS)
sudo dmesg | grep -E 'dma-test:|openbrcm:'

# 4. unload
sudo rmmod openbrcm
```

Expected dmesg milestones: `dma-test: BEGIN`; `common initvals begin records=610`;
`common initvals complete total=610 w16=113 w32=497`;
`D2B exit verified ...`; `D2B prefix complete common_records=610`;
`TX0..TX3 programmed`; `RX buffers mapped=64`; `RX buffers posted=64`;
`bring-up validation PASS`; `quiesce begin`; `RX reset PASS`;
`TX0..TX3 reset PASS`; `all DMA engines stopped`; `rings released`;
`PASS - bring-up + teardown proven`; `STOP before remaining D3A1/band/PHY`.
A missing/failed quiesce logs `core containment observed ... free still
forbidden` (if containment verified) followed by `quiesce NOT verified; ...
reboot required`, sets the fatal latch, pins the module and must be treated as
a failure (reboot), not PASS — containment is never a free permit.
