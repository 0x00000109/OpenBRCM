# M3.1 — BCM4352 / D11 rev 42 DMA architecture report

Status: **report only — no hardware register writes performed.**
Blob: `wlc_hybrid.o_shipped` sha256 `352a6e34…4743` (Repo A). This document
recovers the DMA model from (a) the blob itself and (b) Linux
`brcmsmac`/`bcma` used strictly as structural corroboration.

## Provenance legend
- **C2** — binary-confirmed in our blob (`dma_attach` 0x10380, `dma_addrwidth`
  0xffbc, `wlc_bmac_attach` 0x6984f, `wlc_intrson` 0x7a720, `wlc_intrsoff`
  0x7a6b5, `wlc_isr` 0x7a8e4, `wlc_bmac_enable_mac` 0x60d3d, `sub_7a769`).
- **C3** — upstream structural (`brcmsmac/dma.c`, `dma.h`, `d11.h`; `bcma`).
- **C4** — hardware-validated (none yet; that is M3.2+).

## 1. DMA32 vs DMA64
**Conclusion: 64-bit DMA engine (dma64) — C2/C3, predicate to be evaluated at
runtime in M3.2 before any write.**

- BCM4352 `CC_CAP = 0x58680001` has `BCMA_CC_CAP_64BIT (0x08000000)` set →
  64-bit silicon backplane.
- Blob `dma_attach` sets `di+0x40 = (si_core_sflags(sih,0,0) >> 12) & 1` and
  branches the descriptor alignment on it (`di+0x68 = 0x0d/0x0c` for the 64-bit
  path vs `0x04` for the 32-bit path). `di+0x40` is the 64-bit-addressing flag.
- Blob `dma_addrwidth(sih, di)` returns **0x40** when the backplane-64 flag +
  `si_backplane64()` hold and `bustype==1 && buscoretype ∈ {0x820,0x83C}`;
  otherwise 0x20 (32-bit) or 0x1E (30-bit). BCM4352 is PCIe (`bustype=1`,
  bus core `0x83C`).
- Per-fifo registers use the **dma64 6-register block** and a 16-byte descriptor
  with separate `addrlow`/`addrhigh`.

## 2. Channels / rings (D11 FIFO map) — C2 (blob) + C3
`wlc_bmac_attach` calls `dma_attach` four times with register bases relative to
the D11 core (`[wlc+0xD0]`). For `corerev > 10` (ours = 42) it uses the
`fifo64regs[]` layout (0x40-byte stride, RX = TX + 0x20):

| FIFO (ring) | TX base | RX base | Role |
| ---: | ---: | ---: | :--- |
| 0 | 0x200 | 0x220 | TX AC_BK (background data) **+ RX frames** |
| 1 | 0x240 | — | TX AC_BE (best-effort data) |
| 2 | 0x280 | — | TX AC_VI (video) |
| 3 | 0x2C0 | — | TX AC_VO / **control & management** |

- Older cores (`corerev <= 10`) use different bases (0x200/0x210/0x220/0x240/
  0x260/0x280/0x2C0) — the blob selects by `[wlc+0x84] > 0xA`.
- **RX channel/ring = FIFO0 RX (base 0x220).**
- **TX selection:** data uses FIFO0..2 by AC; **management/control = FIFO3**
  (`TX_CTL_FIFO = TX_AC_VO_FIFO`). M3.6's first controlled management TX should
  target FIFO3.
- The layout matches `struct fifo64 { dma64regs dmaxmt; pio4regs piotx;
  dma64regs dmarcv; pio4regs piorx; }` (`d11.h`), stride 0x40 (`pio4regs`=8 B).

## 3. Register map

### 3a. Per-channel dma64 block (0x18 bytes) — C3, partially C2
| off | name | semantics |
| ---: | :--- | :--- |
| 0x00 | `control` | engine enable + flags (see 3c) |
| 0x04 | `ptr` | **last descriptor posted** by the driver (ring-base + index*16) |
| 0x08 | `addrlow` | descriptor-ring base, low 32 bits (8 KiB aligned) |
| 0x0C | `addrhigh` | descriptor-ring base, bits 63:32 |
| 0x10 | `status0` | engine state + **current descriptor index** (hw) |
| 0x14 | `status1` | active descriptor + error code |

C2 evidence: the blob reads `base+0x00`, `base+0x04`, and writes `base+0x08=0xff0`
(ring-size/alignment probe, matching brcmsmac `_dma_descriptor_align`).
`status0/status1` at 0x10/0x14 are C3 (brcmsmac `struct dma64regs`); **the earlier
`dma_regs.json` label "STATUS 0x04" is wrong** (0x04 is `ptr`).

### 3b. D11 global registers — C2/C3
| off | name | note |
| ---: | :--- | :--- |
| 0x100 | `intrcvlazy[0]` | RX interrupt lazy timer (blob writes here in `wlc_bmac_init`) |
| 0x120 | `maccontrol` | MAC enable (`wlc_bmac_enable_mac`) |
| 0x124 | `maccommand` | MAC command |
| 0x128 | `macintstatus` | interrupt status (read in `sub_7a769`) |
| 0x12C | `macintmask` | interrupt mask (`wlc_intrson/off`) |

### 3c. Control-register bits
TX (`control`): `XE`=0x1 (enable), `SE`=0x2 (suspend), `LE`=0x4, `FL`=0x10,
`PD`=0x800 (parity disable), `AE`=0x30000 (bits[17:16] addr extension). — C3.
RX (`control`): `RE`=0x1 (enable), `RO`=0xfe (rx-offset, bits[7:1]),
`FM`=0x100, `SH`=0x200, `OC`=0x400, `PD`=0x800, `AE`=0x30000. — C3.

## 4. Descriptor format — C3 (structure), C2 (size/align)
16-byte descriptor (`struct dma64desc`), two 64-bit words:
```
struct ob_dma_desc {        /* little-endian on the wire */
    __le32 ctrl1;           /* flags + buffer count            */
    __le32 ctrl2;           /* byte count (len) + AE + parity  */
    __le32 addrlow;         /* buffer DMA address [31:0]       */
    __le32 addrhigh;        /* buffer DMA address [63:32]      */
};
```
`ctrl1`: `EOT`=1<<28, `IOC`=1<<29, `EOF`=1<<30, `SOF`=1<<31; bits[27:20]
core-specific. `ctrl2`: `BC_MASK`=0x7fff (buffer/byte count), `AE`=0x30000
(<<16), `PARITY`=0x40000.
- **TX descriptor:** `ctrl1 = SOF|EOF|IOC` (+`EOT` on last ring slot);
  `ctrl2 = len & 0x7fff`.
- **RX descriptor:** `ctrl1 = 0` (+`EOT` on last slot); `ctrl2 = rxbufsize`.
- Descriptor size **16 B** and ring alignment **8 KiB** are also C2 via
  `dma_regs.json`/`brcm_dma.h` and the blob's `0xff0` probe.

## 5. Producer / consumer semantics
- **TX (producer = host):** software keeps `tin`/`tout`. Fill `desc[txout]`,
  `txout = (txout+1) % ntxd`, then **write `ptr = ringbase + txout*16`** to
  publish. Hardware consumes old→new; `status0 & XS0_CD_MASK` = current
  descriptor. Ring is full when `next(txout) == txin`; completion is read from
  `status0`.
- **RX (consumer = host):** software keeps `rin`/`rout`. Post buffers by filling
  `desc[rout]`, `rout = (rout+1) % nrxd`, then **write `ptr = ringbase +
  rout*16`** to publish. Hardware fills descriptors; `status0 & RS0_CD_MASK`
  is the descriptor the engine is currently on, so frames `[rout, that index)`
  are complete. Host consumes by advancing `rout`.
- `ptr`/`status0` index fields are 13-bit masks (`XS0_CD_MASK`/`RS0_CD_MASK`
  0x1fff), which is why the descriptor ring must be 8 KiB-aligned and ≤ 8192 B
  (`D64MAXRINGSZ = 1<<13` → max 512 descriptors).

## 6. RX path details
- **Hardware RX header = 38 bytes** (`BRCMS_HWRXOFF`), written by the DMA at the
  **start of each RX buffer**, followed by the 802.11 frame. Header
  (`struct d11rxhdr`, on the wire little-endian `d11rxhdr_le`):
  `RxFrameSize`@0, `PAD`@2, `PhyRxStatus_0..5`@4..15, `RxStatus1`@16,
  `RxStatus2`@18, `RxTSFTime`@20, `RxChan`@22. `RxFrameSize` is the 802.11 frame
  length; total buffer used = header + frame.
- The header size is communicated to the engine via the **RX `control` bits[7:1]
  (`RO`)** (`rxoffset << 1`). Exact value (38) to be confirmed at M3.4.
- The channel `status0` (engine current index) — **not** the descriptor — is what
  bounds how many RX buffers the host may reap. Frame length must additionally
  be sanity-checked against `RxFrameSize` and the buffer size.

## 7. Interrupt relationship — C2
> **Correction (M3.3).** An earlier draft of this section attributed `I_RI`
> (1<<16) / `I_XI` (1<<24) to `macintstatus`. That is wrong: those are the
> per-FIFO interrupt bits (`intctrlregs[i]`, D11+0x20) for the RX/TX FIFOs.
> `macintstatus`/`macintmask` (0x128/0x12C) use the **`MI_*`** bit set.

- Single D11 IRQ. ISR reads **`macintstatus` (0x128)**, keeps
  `raw & macintmask`, and acknowledges by writing the owned bits back
  (write-1-to-clear). A read of `0xffffffff` means the core is in reset / the
  device was removed and must not be acknowledged.
- **`macintstatus`/`macintmask` bits (C3 `d11.h`, `MI_*`)**: `MI_TBTT`(2),
  `MI_MACTXERR`(9), `MI_PHYTXERR`(11), **`MI_DMAINT`(15, OR of the per-FIFO DMA
  interrupts)**, `MI_TXSTOP`(16), `MI_TFS`(29), `MI_PHYCHANGED`(30),
  `MI_TO`(31), plus PSM/PMQ/beacon/gp bits (0-21, 28-31).
- **Per-FIFO DMA interrupt control at D11+0x20** (`intctrlregs[8]`,
  `{intstatus, intmask}`): `RX_FIFO = 0` (0x20/0x24) uses `I_RI = 1<<16`;
  `TX_AC_VO_FIFO = TX_CTL_FIFO = 3` (0x38/0x3C) uses `I_XI = 1<<24`; error bits
  `I_PC`(10) `I_PD`(11) `I_DE`(12) `I_RU`(13) `I_RO`(14) `I_XU`(15). These
  assert `MI_DMAINT` at the top level; per-FIFO `intstatus` is
  write-1-to-clear. RX/TX completion is also gated by per-descriptor `IOC`
  (bit 29).
- `wlc_intrson`: caches mask at `[wlc+0x9c]`, writes `macintmask`. `wlc_intrsoff`:
  writes 0 then reads back with a 1 µs delay. `sub_7a769` reads status and does
  `macintstatus = read & [wlc+0x1c4]` (masked acknowledge). **Never write
  0xffffffff blindly.**
- **PCI transport (C3):** `bcma` records the core IRQ as `core->irq =
  bus->host_pci->irq` but does not request it. The per-core routing is
  `bcma_host_pci_irq_ctl(bus, core, enable)`, which sets the core's bit in the
  PCI `IRQMASK` config register (`bcma_regs.h:59`). `brcmsmac` uses
  `request_irq(pdev->irq, ..., IRQF_SHARED, ...)` and never allocates its own
  vectors.

## 8. Reset / enable ordering (derived, C3 + C2 hooks)
1. DMA device ready (`bcma` host up — already done in `ob_si_powerup`).
2. **TX reset:** set `control = SE` (suspend), poll `status0.XS` until
   disabled/idle/stopped (bounded); `control = 0`; poll disabled; `udelay(~300)`.
3. **RX reset:** `control = 0`; poll `status0.RS == disabled` (bounded).
4. Program descriptor-ring base: `addrlow = (u32)ring_pa`; `addrhigh = 0x80000000`
   for `buscoretype 0x83C && dma64` (BCM4352) — see §11.
5. Zero the descriptor ring **before** enabling (or after, if 4K-aligned);
   publish `ptr` only with valid descriptors.
6. **TX enable:** `control |= XE` (`| PD` if parity disabled).
   **RX enable:** `control = RE | (rxoffset<<1) [| PD][| OC]`.
7. MAC interrupts: leave `macintmask = 0` until the IRQ handler is installed;
   then unmask only `I_RI` first (M3.3/M3.4).

## 9. bcma plumbing (this is how M3.2/M3.3 get their handles) — C2 (kernel)
- **DMA device:** `core->dma_dev`. For `BCMA_HOSTTYPE_PCI` (ours),
  `bcma` sets `core->dma_dev = bus->dev` (the PCIe device). Use it for
  `dma_alloc_coherent()` / `dma_map_single()`.
- **IRQ:** `core->irq = bus->host_pci->irq`. `bcma` does **not** request MSI;
  the line is the PCI-assigned IRQ. M3.3 must confirm MSI/MSI-X/INTx from the
  `pci_dev` state, and register with `IRQF_SHARED` (bcma may already have a
  handler for the PCIe core on the same line).
- Register access continues through `bcma_read32/16` on the D11 core window.

## 10. Relation to the D11 revision (rev 42)
- The only rev-sensitive DMA detail observed is the **FIFO register stride**
  (`corerev > 10` → the `fifo64regs[]` 0x40-stride map above; RX base 0x220).
- `dma_attach` special-cases core ids `0x829`/`0x834` (other 802.11 cores) for
  address-extension; our D11 `0x812` takes the general path.

## 11. Translation / PCIe DMA offset — corrected (M3.4A)
- PCI host: `ddoffsetlow = dataoffsetlow = 0` but **`ddoffsethigh =
  dataoffsethigh = 0x80000000`** for `buscoretype ∈ {0x83C,0x820} && dma64`
  (BCM4352 is `0x83C`). Blob `dma_attach` (`0x10380`, `0x10645`) and the
  descriptor/ring writers (`dma64_dd_upd` `0xdcf8`, `_dma_ddtable_init`
  `~0xe69f`) prove `addrlow = (u32)pa` and **`addrhigh = 0x80000000`** for both
  buffer descriptors and the ring base. This supersedes the earlier "no offset"
  statement; see `docs/rx_path.md`.
- The `AE` (address-extension) mechanism (`ctrl2` bits[17:16], control bits
  [17:16]) is only for **32-bit** engines placing buffers above 1 GiB. For a
  64-bit engine it is not required; M3.2 will still probe AE support as the blob
  does.

## 12. Unresolved fields (do not guess; resolve in M3.2/M3.4)
- Exact value of RX `control` `RO` (rxoffset) on this part (expected 38 = header).
- `status0`/`status1` layout is C3, not yet observed in our blob (inlined
  reset/rx/tx functions were not symbolised). Confirm by read-only dump first.
- The blob `dma_attach` reads core-specific control bits at [25:18]
  (`0x1c0000/0xe00000/0x3000000`) into `di+0x105/0x10a/0x10b`; meaning unknown.
- `[di+0xF4] = 0x80000000` (PCIe 0x820/0x83C) / `0x40000000` (others) — likely a
  "DMA core flags" default; semantics unresolved.
- Whether the RX engine writes per-descriptor status back into `ctrl1/ctrl2`
  (we rely on the 38-byte header + channel `status0`).
- Exact `rxbufsize`/`nrxpost` used by the Windows-style stack for this PCIe part
  (brcmsmac uses `RXBUFSZ`, `NRXBUFPOST=32`); we will choose our own, bounded.

## 13. Proposed minimal M3.2 plan (allocation only; engine stays disabled)
1. Add `struct ob_dma_desc` (16 B, above) and `struct ob_dma_ring`
   { `desc[]` (coherent), `dma_addr_t ring_dma`, `u16 n`, `u16 head`, `u16 tail`,
   `void **buf`/`dma_addr_t *buf_dma`, `bool ready`, `enum {TX,RX}` }.
2. Add `struct ob_dma` in `od_hw`: one RX ring (FIFO0-RX base 0x220) and one
   control TX ring (FIFO3 base 0x2C0); RX descriptors only, no mapping of skbs
   yet beyond coherent buffers.
3. Ring rules: n ≤ 512, ring bytes 8 KiB-aligned via `dma_alloc_coherent` +
   manual align (like brcmsmac) or kmalloc+dma_map_single; publish `ptr` only
   when valid; wrap arithmetic `(i+1) % n`.
4. Use `dma_alloc_coherent(core->dma_dev, …)`, check every failure, keep a full
   cleanup path in `ob_remove`.
5. **Do not touch `control`/`addrlow`/`ptr` yet** — allocate, initialise
   descriptors in memory, and log the computed bases only.
6. Host/KUnit tests: wraparound, full/empty, descriptor init, index arithmetic.
7. STOP after allocation/free is clean; no hardware enable, no IRQ, no RX/TX.

## 14. Exact provenance per conclusion
| # | Conclusion | Confidence | Source |
| :-- | :-- | :-- | :-- |
| 1 | 64-bit DMA engine; predicate | C2/C3 | `dma_addrwidth` 0xffbc, `dma_attach` di+0x40; `brcmsmac/dma.h` |
| 2 | FIFO map, RX=FIFO0@0x220, mgmt=FIFO3@0x2C0 | C2 | `wlc_bmac_attach` 0x6984f dma_attach call sites; `brcmsmac/d11.h` |
| 3 | dma64 reg layout control/ptr/addrlow/addrhigh/status0/status1 | C3(+C2 partial) | `brcmsmac/dma.h`; blob reads +0/+4, writes +8 |
| 4 | D11 `macintstatus` 0x128 / `macintmask` 0x12C | C2 | `wlc_intrson/off` 0x7a720/0x7a6b5, `sub_7a769` |
| 5 | descriptor 16 B, ctrl bits, 8 KiB ring | C3/C2 | `brcmsmac/dma.c`; `dma_regs.json`, blob `0xff0` probe |
| 6 | RX hw header 38 B + fields | C3 | `brcmsmac/d11.h` `d11rxhdr`, `BRCMS_HWRXOFF` |
| 7 | IRQ/data plumbing via bcma `core->irq`/`core->dma_dev` | C2 | `drivers/bcma/main.c:257` |
| 8 | reset/enable ordering | C3 | `brcmsmac/dma.c` `dma_txreset/rxreset/txinit/rxinit` |
| 9 | PCI host: `addrlow=(u32)pa`, `addrhigh=0x80000000` (dma64, 0x83C) | C2 | `dma_attach` 0x10645; `dma64_dd_upd` 0xdcf8 |

**End of M3.1. No register writes. Awaiting approval before M3.2.**

---

## M3.2 implementation note (software model only)

`src/ob_dma.{h,c}` implements the model above without touching hardware.

- **Allocation API:** `dma_pool_create("ob-dma-ring", core->dma_dev,
  OB_DMA_RING_BYTES=8192, OB_DMA_RING_ALIGN=8192, 8192)`, then
  `dma_pool_alloc()` per ring. A pool with `size == align == boundary == 8192`
  carves each block on an 8 KiB boundary, which a plain
  `dma_alloc_coherent(8192)` does not guarantee. The recovered 8 KiB constraint
  is validated on the **DMA address** only (`IS_ALIGNED(desc_dma, 8192)`), since
  that is what the hardware receives; the CPU virtual address only has to meet
  the natural alignment of `struct ob_dma_desc`. An allocation is rejected with
  `-EINVAL` if the DMA address is not 8 KiB aligned.
- **Diagnostics** report the two address spaces separately:
  `dma_aligned_8k=<yes|no> cpu_desc_aligned=<yes|no>`.
- **Real DMA device:** `core->dma_dev`, which `bcma` sets to `bus->dev` (the
  PCIe device, `drivers/bcma/main.c:250`). **SUPERSEDED (M3.4A/M3.2.1):** the
  device discards the high address dword, so the window is constrained to
  32-bit with `dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32))` and an explicit
  `upper_32_bits(desc_dma)==0` check. The original M3.2 plan validated a 64-bit
  mask; that is kept as HISTORICAL context only, not current behaviour.
- **Descriptor:** `struct ob_dma_desc { __le32 ctrl1, ctrl2, addrlow, addrhigh; }`
  with `_Static_assert(sizeof(...) == 16)`; encoded via `cpu_to_le32` helpers.
- **Ring:** `struct ob_dma_ring` keeps `desc_cpu`/`desc_dma`, the pool block
  base handles, `n`, `head`, `tail`, a per-slot ownership array and an
  `allocated` flag, with independent RX and TX instances.
- **Not done (by design):** writing 0x200/0x220/0x240/0x280/0x2C0, `control`,
  `addrlow`, `addrhigh`, `ptr`, enabling an engine, or taking an IRQ. Descriptors
  are zero-initialised; no buffer is mapped and no EOT is written to the ring.


## M3.4D3A0 — isolated vendor pre-PHY DMA lifecycle (`dma_test_only`)

The accepted M3.4D3 analysis (Appendices C/D of `docs/m34d3_bsinitvals.md`)
adds a vendor-faithful DMA stage in `dma_test_only`. It is deliberately kept
separate from the M3.2/M3.4B path so the proven normal and isolated modes are
untouched. Lifecycle stages (`src/ob_d3a0.c`):

| stage | function | registers / resources |
| :--- | :--- | :--- |
| D2B entry | `ob_initvals_run_d2b` | D2A core + exactly 610 common initvals (113 w16 / 497 w32) + postcondition gate |
| D2B re-read | `ob_d3a0_check_d2b_exit` | live `MACCONTROL=0x04020402`, `MACINTMASK=0`, FIFO=`0x01c4/0/0/0x079e`, `SHM14=0xb4` |
| prefix | `ob_d3a0_prefix` | pinned D11: `intrcvlazy`, `MACCONTROL` RMW, `tsf`, `macintstatus`, `intctrlregs[0]=I_RI`, `macphyclk`, machwcap SHM |
| alloc | `ob_d3a0_ring_alloc` | 4 TX + 1 RX 8 KiB-aligned coherent rings |
| TX program | `ob_d3a0_tx_program` | `addrlow`, `addrhigh=0x80000000`, `control = read\|XE\|PD`; no `ptr`, no descriptors (zero TX payload mappings) |
| RX map/desc | `ob_d3a0_rx_map`, `ob_d3a0_rx_build_desc` | exactly 64 `DMA_FROM_DEVICE` buffers, 16-B descriptors, EOT@255 |
| RX program | `ob_d3a0_rx_program` | `control=0x84d`, `ptr=0x400`, `addrhigh=0x80000000` |
| validate | `ob_d3a0_validate` | addrlow/addrhigh/control/status0(state)/status1; `macintmask=0` |
| quiesce | `ob_d3a0_quiesce` | clear `I_RI`; `dma_rxreset`; `dma_txreset` per initialized channel (bounded 10 ms); verify; `bcma_core_disable` containment only if its `bcma_core_is_enabled()` readback is false |
| free | `ob_d3a0_free_mem` | only after `ob_d3a0_can_free()`; unmap/free buffers and rings |

Fail-closed invariant: DMA memory is never freed while hardware may still
consume it (`ob_d3a0_can_free`); an unverified quiesce sets a fatal,
reboot-required state, latches a module-wide re-entry block, records the
retained ring addresses and pins the module (`__module_get`), and
`ob_d3a0_remove` refuses to free. Probe is kept successful so the bound device
retains the state; only a reboot clears it. `MACINTMASK` stays 0 and the host
BCMA/PCI IRQ route is never enabled. The four TX channels are `0x200` AC_BK,
`0x240` AC_BE, `0x280` AC_VI, `0x2C0` AC_VO/CTL; FIFO0 RX is `0x220`.
