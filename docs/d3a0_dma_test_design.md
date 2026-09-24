# D3A0 — vendor pre-PHY DMA bring-up (design record)

**Status: `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `NOT HARDWARE PROVEN`.**
Implementation on branch `m34d3a0-dma-test`: `src/ob_d3a0.{c,h}`,
`tests/host/ob_d3a0_test.c`, `tests/kunit/ob_d3a0_kunit.c`, module param
`dma_test_only=1`. No hardware run was performed. Last hardware-proven
milestone: **M3.4D2B**.

Implementation notes / deviations from this design, all documented:
- `sub_67efd` (TXE0 FIFO fixup) and the runtime NVRAM/BTC/rate/power SHM tables
  are **not** implemented: their exact write sets are not pinned in the merged
  analysis, and they are not prerequisites for the DMA engine programming.
  They remain deferred pre-DMA/D3A1 content.
- The D3A0 prefix implements only the exactly-pinned operations: SHM
  `0x80=8`, `0x5c=0x0a`, `intrcvlazy[0]=0x01000000`, `MACCONTROL` RMW,
  `tsf_cfprep`/`tsf_cfpstart`, `macintstatus` W1C, `intctrlregs[0].intmask=I_RI`,
  `macphyclk` ON, `M_MACHW_VER`/`M_MACHW_CAP_L/H`.

This branch (`m34d3a0-dma-test`) records the exact future D3A0 implementation
boundary. It contains **design documentation only**; nothing here is
implemented. Source: `docs/m34d3_bsinitvals.md` Appendix D (D3A0 blocker closure,
GO = YES) and Appendix C (DMA/IRQ reversal).

## 1. Scope and non-goals

D3A0 reproduces the vendor post-common DMA/IRQ-source stage only:

- **No** PHY/radio.
- **No** `EN_MAC`.
- **No** host IRQ route (`bcma_host_pci_irq_ctl(true)` is not called).
- **No** `request_irq` requirement for this path.
- **No** mac80211.

`MACINTMASK` (`D11+0x12C`) stays 0 and the BCMA/PCI IRQ route stays disabled, so
CPU IRQ delivery is impossible at this stage (Appendix C.8/C.9).

## 2. Canonical vendor order (must not be reordered)

The recovered vendor write order configures the interrupt source **before** DMA
init. The implementation MUST preserve this order:

```
proven D2B preparation (ucode + common initvals)
  ->
required post-common D11 prefix up to IRQ-source configuration (D3A1 content,
    in vendor position)
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

Free permission requires a **verified** per-channel stop:

```
MACINTMASK = 0
clear/disable relevant per-FIFO sources (intctrlregs[0].intmask &= ~I_RI;
    ack owned macintstatus bits)
  -> dma_rxreset(di0)                       (control=0; poll status0 & 0xf0000000 == 0)
  -> dma_txreset(every initialized TX DMA channel)
  -> VERIFY every initialized engine is stopped/disabled (status0 state == 0)
```

Only **after** successful verification may the code `dma_unmap_single`, free
skbs, and free rings (`dma_pool_free`/`dma_free_coherent`).

- `bcma_core_disable`/core-reset is a **containment/fallback** only. It may
  authorize a free **only if its own postconditions are verified**; it must not
  be used to free after an unverified per-channel reset failure.
- If per-channel reset fails **and** core-reset postconditions cannot be
  verified: **do not** unmap/free any DMA-owned memory; keep resources and
  require reboot. The code must **never** free/unmap while hardware DMA access
  is possible.
- No invented PCIe flush mechanism. BCMA `bcma_core_disable` asserts core reset
  and waits `RESET_ST` with register readbacks; that is the containment
  guarantee, not a free-permission bypass.
- Track per-channel initialization state so partial-bring-up cleanup never
  resets/frees an uninitialized resource and never misses an initialized one.

`ob_dma_quiesce()` is mandatory before any `dma_unmap_single`, skb free, ring
free, or isolated-mode teardown.

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
- D3A0 `NOT IMPLEMENTED`; `NOT HARDWARE PROVEN`.
- M3.4D3 = `ANALYSIS ONLY`; last hardware-proven = **M3.4D2B**.
