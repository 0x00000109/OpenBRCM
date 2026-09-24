# M3.4A — BCM4352 / D11 rev42 RX path proof

Status: **report only — no DMA register written, no RX engine enabled.**
Blob: `wlc_hybrid.o_shipped` sha256 `352a6e34…4743` (Repo A).

Provenance: **C2** = binary-confirmed in our blob (addresses below), **C3** =
upstream structural (`brcmsmac`), **C4** = hardware-validated (none yet).

## Critical corrections to the accepted M3.1 / M3.2 model

These change previously-accepted facts and are the reason DMA is **not** enabled
yet:

1. **Descriptor/ring-base high word is `0x80000000`, not 0.** `dma_attach`
   (`0x10380`, at `0x10623`–`0x106c9`) sets, for `bustype==1 && buscoretype ∈
   {0x83C,0x820} && dma64`:
   `ddoffsetlow=0`, `ddoffsethigh=0x80000000`, `dataoffsetlow=0`,
   `dataoffsethigh=0x80000000`. BCM4352 has bus core `0x83C` (proven by
   `dma_addrwidth`), so both apply. The descriptor encoder `dma64_dd_upd`
   (`0xdcf8`) writes `addrhigh = dataoffsethigh = 0x80000000`; the ring-base
   writer (`~0xe69f`, `_dma_ddtable_init`) writes `addrhigh = ddoffsethigh =
   0x80000000`. M3.1's "PCI host: program absolute DMA addresses" is wrong — the
   low word stays `(u32)pa` but the high word must be the PCIe host marker.
2. **RX ring = 256 descriptors, not 512.** The DMA config block
   (`wlc_attach_malloc` `0x4ebbb`, struct at `[wlc+0x38]`) sets
   `[+0]=512`, `[+4]=256`, `[+8]=2048`, `[+0xC]=64`. `wlc_bmac_attach`
   (`0x6a37a`,`0x6a37e`) passes `[+4]` as `nrxd` and `[+0]` as `ntxd`;
   `dma64_alloc` (`sub_1010c`) uses `di+0xA4` (nrxd) for the RX direction
   (`0x101da`). So **RX=256 (4096 B), TX=512**. Our M3.2 RX ring is 512.

## 1. Exact RX control value — RESOLVED (C2)

Blob `_dma_rxenable` (`0xe5f0`):

```
control = (read(rx+0x00) & 0x30000)              /* preserve AE bits [17:16] */
        | 0x801                                  /* RE(0x1) | PD(0x800) if parity disabled */
        | (OC 0x400 only if dmactrlflags bit1)   /* ROC */
        | (di+0x10B << 24) | (di+0x105 << 18) | (di+0x10A << 21)  /* preserved core bits */
        | (rxoffset << 1)                        /* D64_RC_RO, bits [7:1] */
```

- `dmactrlflags` defaults to **0**: `dma_attach` calls the ctrlflags op with
  `mask=3, flags=0` (`0x1042a`: `esi=3, edx=0`) → Parity check **disabled** (PD
  set) and ROC **disabled** (no OC).
- `rxoffset = 38` (`di+0xF0`); preserved core bits are 0 on a fresh engine.
- **Final value = `0x0000084D`** = `RE(0x1) | PD(0x800) | (38 << 1)`. — C2.

## 2. Exact RX buffer size — RESOLVED (C2)

- Config `[wlc+0x38]+8 = 0x800 = 2048` (`wlc_attach_malloc` `0x4ec7e`), passed as
  `rxbufsize` (`wlc_bmac_attach` `0x6a376`).
- `rxextheadroom` for FIFO0 = **0** (`wlc_bmac_attach` `0x6a3cb`:
  `movl $0x0,0x10(%rsp)`), so `dma_attach` (`0x10482`) computes actual
  `di->rxbufsize = 2048` (no `BCMEXTRAHDROOM` subtraction; no `skb_pull`).
- **Descriptor byte count = 2048; DMA buffer allocation = 2048 bytes.**
- Buffer layout: 38-byte D11 header + up to 2010 bytes of 802.11 frame (FCS
  handling resolved in item 7). Max payload `= 2048 - 38`. — C2.

## 3. Exact RX post count — RESOLVED (C2)

- Config `[wlc+0x38]+0xC = 0x40 = 64`, passed as `nrxpost`
  (`wlc_bmac_attach` `0x6a372` → `dma_attach` `0x104a4` → `di+0xEC`).
- Refill model (C3 `dma_rxfill`): post `nrxpost - active`; ring capacity = `nrxd
  = 256`, so the ring never posts all descriptors (one slot reserved by the
  producer/consumer accounting). **Initial post = 64.**
- No evidence of a post-all-512/-256 behaviour. — C2/C3.

## 4. Exact RX descriptor encoding — RESOLVED (C2)

For one posted buffer at index `i`:

| field | value |
|---|---|
| `ctrl1` | `0`; `OB_DMA_CTRL1_EOT (0x10000000)` only when `i == nrxd-1` (255) |
| `ctrl2` | `rxbufsize & 0x7fff` = `2048 & 0x7fff` = `0x0800` |
| `addrlow` | `(u32)(pa + dataoffsetlow)` = `(u32)pa` |
| `addrhigh` | `dataoffsethigh` = **`0x80000000`** |

- No AE, no parity bit (parity disabled). AE only for 32-bit engines; our
  fast-path takes the `dataoffsetlow==0` branch (`dma64_dd_upd` `0xdd0a`).
- `ctrl1`/`ctrl2` written as `(0x10000000)` for EOT and `bufcount & 0x7fff`
  (`dma64_dd_upd` `0xdd00`, `0xdd56`–`0xdd5b`; EOT construction at `0xf300`).
- Ownership: the descriptor is host-owned until posted; the driver owns the
  mapped skb until it consumes the completion. — C2.

## 5. Exact producer publication — RESOLVED (C2/C3)

- Ring base registers (C2 `_dma_ddtable_init` `~0xe69f`):
  `write(rx+0x08, (u32)ring_dma)` … `write(rx+0x0C, 0x80000000)`.
- After posting `N` descriptors (`rxout` advanced by `N`), publish the last
  descriptor address (C3 `dma_rxfill`): `write(rx+0x04 (PTR),
  (u32)ring_dma + N*16)`.
- **Initial PTR after posting 64 = `(u32)ring_dma + 64*16 = ring_dma + 0x400`.**
- Ordering: all descriptor stores and `dma_map_single()`s must be visible before
  the PTR/`control` MMIO write. Use `dma_wmb()` then `wmb()` before the MMIO
  write (MMIO writes are not reordered past `dma_wmb`+`wmb`). — C3.

## 6. Exact completion / consumer semantics — RESOLVED (C3)

- `status0` (rx+0x10) low field `D64_RS0_CD_MASK = 0x1fff` holds the current
  descriptor address (ring base low bits + index*16). Because the ring is
  8 KiB-aligned (base low 13 bits = 0), `index = (status0 & 0x1fff) >> 4`.
- Generic form: `index = (((status0 & 0x1fff) - (u32)ring_dma) & 0x1fff) >> 4`.
- Completion condition: a posted descriptor `i` is complete when the engine's
  current index has advanced past `i` (`i != curr`), i.e. the C3 rule used by
  `dma64_getnextrxp`.
- Wrap: 13-bit masked difference handles wrap; valid indices stay `0..255`.
- `status1` (rx+0x14): RX error code in `D64_RS1_RE_MASK` (bits [31:28]); read
  only for failure containment, not for completion.
- **Do not infer completion from descriptor memory.** — C3 (accepted M3.1).

## 7. RX header — RESOLVED (C3)

- Hardware header = **38 bytes at the start of the DMA buffer**
  (`BRCMS_HWRXOFF 38`); the 802.11 frame starts at **offset 38**.
- On-wire little-endian layout (`struct d11rxhdr`): `RxFrameSize`@0 (u16, frame
  length), `PAD`@2, `PhyRxStatus_0..5`@4..15, `RxStatus1`@16, `RxStatus2`@18,
  `RxTSFTime`@20, `RxChan`@22.
- `RxFrameSize` is the 802.11 frame length (header excluded); total = 38 + len.
- FCS: kept in the frame for this path (frame length includes FCS); do not strip
  yet. RSSI/rate fields are **not** exposed in M3.4.
- Only `RxFrameSize` and the first bytes of the 802.11 header are consumed in
  M3.4. — C3.

## 8. Interrupt relationship — RESOLVED (C2/C3)

- Per-FIFO interrupt control at D11+`0x20` (`struct intctrlregs[8]`,
  `{intstatus,intmask}`): **FIFO0 (RX) intstatus `0x20`, intmask `0x24`**;
  FIFO3 (TX/control) `0x38`/`0x3C`.
- RX FIFO interrupt bit **`I_RI = 1<<16`**; TX `I_XI = 1<<24` (C3 `d11.h`).
  These are FIFO bits — **not** `macintstatus` bits (correcting M3.1).
- `macintstatus`/`macintmask` (`0x128`/`0x12C`) use the `MI_*` set;
  `MI_DMAINT = 1<<15` is the OR-summary of the FIFO DMA interrupts.
- Minimum RX path: set FIFO0 `intmask |= I_RI`; set `macintmask |= MI_DMAINT`;
  in the ISR read `macintstatus`, and on `MI_DMAINT` read FIFO0 `intstatus`,
  acknowledge `I_RI` (write-1-to-clear), then acknowledge the macintstatus bits.
- RX FIFO intstatus is write-1-to-clear; do not write `0xffffffff`.

## Deviations requiring acceptance before any DMA write

Even though every field above is resolved, two of them contradict accepted
M3.1/M3.2 facts and, if wrong, would make the device DMA to a wrong host
address. Per the project rule "no guessed writes / provenance first", M3.4B is
**not** started until these are acknowledged:

1. descriptors and ring base use `addrhigh = 0x80000000`;
2. the RX ring is 256 descriptors (4096 B), not 512.

## Proposed M3.4B (implemented only after the above is accepted)

- `struct ob_dma_ring` gets a per-role count; RX `n=256`, TX `n=512`; keep the
  8 KiB pool block.
- RX slots: `alloc_skb(2048)`, `dma_map_single(DMA_FROM_DEVICE)`, track
  `{skb,dma,mapped}` (already present).
- Post 64 descriptors with `ctrl1=EOT@255`, `ctrl2=0x800`,
  `addrlow=(u32)pa`, `addrhigh=0x80000000`; publish base (`addrlow/high`) and
  `PTR = ring_dma + 0x400`; write `control = 0x84D`.
- Read back `control`/`PTR`/`status0`/`status1`; abort + disable on mismatch.
- Enable FIFO0 `intmask |= I_RI`, `macintmask |= MI_DMAINT`.
- ISR: read `MACINTSTATUS`; on `MI_DMAINT` read FIFO0 intstatus, ack `I_RI`,
  ack owned macintstatus bits, schedule a bounded tasklet (no NAPI yet).
- Tasklet: bounded scan, `dma_unmap_single`, validate `RxFrameSize` + bounds,
  log first 5 frames (index, raw len, `RxFrameSize`, header flags, channel,
  first 32 bytes, frame-control subtype; report beacon), then free+repost. No
  `ieee80211_rx*()`.
- Teardown: mask RX int (`intmask &= ~I_RI`, `macintmask &= ~MI_DMAINT`), disable
  RX (`control=0`), bounded poll for `D64_RS0_RS_DISABLED`, `tasklet_kill`,
  `synchronize_irq`, then unmap/free every mapped buffer exactly once.

**End of M3.4A. No register written, no engine enabled. STOP for acceptance of
the two deviations above before M3.4B.**

---

# M3.4A addendum — final proof obligations

## 1. Exact DMA64 addrhigh formula — **A: `addrhigh = dataoffsethigh`**

`dma64_dd_upd` (blob `0xdcf8`), descriptor buffer address:

```
dcf9  mov  0xfc(%rdi),%r10d    ; r10d = dataoffsetlow
dd0a  test %r10d,%r10d
dd0d  je   dd19                ; dataoffsetlow==0 -> fast path (BCM4352)
dd1b  lea  (%r10,%rdx,1),%edx  ; addrlow = (u32)(dataoffsetlow + (u32)pa)
dd4d  mov  0x100(%rdi),%edx    ; edx = dataoffsethigh
dd53  mov  %edx,0xc(%rax)      ; desc.addrhigh = dataoffsethigh
```

No instruction reads pa bits [63:32]; the 32-bit `lea`/`mov` discard them.

`_dma_ddtable_init` (blob `~0xe69f`), ring base:

```
e6d2  add  %edx,%edi          ; edi = ddoffsetlow + (u32)pa   -> addrlow
e6dd  mov  0x50(%rbx),%rsi
e6e1  add  $0xc,%rsi
e6e5  mov  0xf8(%rbx),%edi    ; edi = ddoffsethigh
e6eb  jmp  writel             ; write(rx+0xC, ddoffsethigh)  -> addrhigh
```

`dma_attach` (`0x10645`) sets `ddoffsethigh = dataoffsethigh = 0x80000000` for
`buscoretype ∈ {0x83C,0x820} && dma64`. Therefore:

| address | expr |
|---|---|
| descriptor buffer | `addrlow = (u32)(dataoffsetlow + (u32)pa)`, `addrhigh = dataoffsethigh = 0x80000000` |
| RX ring base | `addrlow = (u32)(ddoffsetlow + (u32)ring)`, `addrhigh = ddoffsethigh = 0x80000000` |
| TX ring base | same as RX (TX regs) |

**The DMA address high dword is discarded.** The device addresses a 32-bit host
window at `0x80000000_<low32>`, so every DMA address must be < 4 GiB.
Implementation (M3.2 correction): `dma_set_mask_and_coherent(dev,
DMA_BIT_MASK(32))` was set so the DMA API cannot hand out >4 GiB addresses;
`hw->dma.h32 = OB_DMA_PCIE_H32` (0x80000000) is the descriptor/ring high word.

## 2. RX ring alignment — **8192 bytes required**

C3 `dma64_alloc`/`dma_attach`: when `aligndesc_4k` holds,
`dmadesc_align = D64RINGALIGN_BITS (13)` **unless** `(ntxd < D64MAXDD/2) &&
(nrxd < D64MAXDD/2)`; `D64MAXDD = 8192/16 = 512`, half = 256. FIFO0 has
`ntxd=512, nrxd=256`, so the condition is false → **align 13 = 8192**.
The blob's `sub_1010c` uses the same probe-derived alignment (`0x2000`).

- RX active descriptors = **256**
- RX active bytes = **4096**
- RX allocation bytes = **8192** (8 KiB `dma_pool` block; only 0..255 used)
- RX required alignment = **8192**
- TX active = 512 / 8192 (unchanged)

## 3. Corrected asymmetric software model (implemented, no hardware)

`src/ob_dma.h`: `OB_DMA_RING_DESC_COUNT_RX = 256`, `..._TX = 512`,
`OB_DMA_RING_ACTIVE_BYTES_RX = 4096`, `..._TX = 8192`, `OB_DMA_RING_BYTES =
8192`, `OB_DMA_RING_ALIGN = 8192`, `OB_DMA_RX_POST_INIT = 64`,
`OB_DMA_RX_BUFSZ = 2048`, `OB_DMA_PCIE_H32 = 0x80000000`; helpers
`ob_dma_ring_count()` / `ob_dma_ring_active_bytes()`; descriptor encode takes an
explicit high word. `src/ob_dma.c` uses the per-role count; the 8 KiB pool is
retained. Host tests assert `RX next(255)==0`, `TX next(511)==0`, RX offset max
`255*16=4080`, TX `511*16=8176`, `OB_DMA_RX_POST_INIT==64`, PTR offset
`64*16==0x400`, and RX EOT@255 / TX EOT@511.

## 4. Example RX descriptors

With `pa = 0x00000000fe0e6000`, `rxbufsize = 2048`, `h32 = 0x80000000`:

| idx | ctrl1 | ctrl2 | addrlow | addrhigh |
|---|---|---|---|---|
| 0 | `0x00000000` | `0x00000800` | `0xfe0e6000` | `0x80000000` |
| 255 | `0x10000000` (EOT) | `0x00000800` | `0xfe0e6000` | `0x80000000` |

Only descriptor 255 carries EOT. Not programmed to hardware yet.

## 5. Host IRQ routing enable ordering

`bcma` does not request the D11 IRQ; `brcmsmac` calls
`bcma_host_pci_irq_ctl(bus, d11core, true)` from the MAC bring-up
(`main.c:4904`) while `request_irq()` was already done at attach
(`mac80211_if.c:1172`). Ordering for M3.4B (handler always installed first, and
no route/source active before the ring is fully programmed):

1. `request_irq(irq, ..., IRQF_SHARED, ...)` — already done in M3.3.
2. Allocate/map RX buffers; program RX ring base + descriptors + PTR.
3. Enable RX DMA (`control = 0x84D`); read back.
4. `bcma_host_pci_irq_ctl(bus, d11core, true)`.
5. FIFO0 `intmask |= I_RI`.
6. `macintmask |= MI_DMAINT`.

Teardown is the reverse: mask `I_RI` + `MI_DMAINT` → disable RX (`control = 0`,
bounded poll `D64_RS0_RS_DISABLED`) → `tasklet_kill` → `synchronize_irq` →
`free_irq` → `bcma_host_pci_irq_ctl(bus, d11core, false)` → unmap/free buffers.

## 6. Revised M3.4B register-write sequence (proposed, NOT executed)

```
/* descriptors (memory, coherent): idx 0..63 posted, EOT only at 255 */
ctrl1 = (idx==255) ? 0x10000000 : 0
ctrl2 = 0x00000800
addrlow = (u32)pa ; addrhigh = 0x80000000
dma_wmb(); wmb();

write(D11+0x228, (u32)ring_dma)      /* RX addrlow  */
write(D11+0x22C, 0x80000000)         /* RX addrhigh */
write(D11+0x224, (u32)ring_dma + 64*16)  /* RX PTR = ring + 0x400 */
write(D11+0x220, 0x0000084D)         /* RX control  */
read back control/ptr/status0/status1; abort+disable on mismatch

bcma_host_pci_irq_ctl(bus, d11core, true)
write(D11+0x24,  read(D11+0x24)  | (1<<16))   /* FIFO0 intmask |= I_RI */
write(D11+0x12C, read(D11+0x12C) | (1<<15))   /* macintmask |= MI_DMAINT */
```

ISR: read `D11+0x128`; if `MI_DMAINT`, read `D11+0x20`, ack `I_RI`, ack owned
`macintstatus` bits, schedule the bounded tasklet. Teardown masks both,
disables the engine (bounded poll), kills the tasklet, then frees the IRQ.

**End of addendum. No DMA register written, no engine enabled.**

---

# M3.4B correction — aligned RX PTR model

The first M3.4B run aborted on a PTR equality check; the engine itself was
healthy. Blob re-proof:

`dma_rxfill` (`0xf39f`–`0xf3cf`):
```
f39f cmpb $0,0x40(%rbx)     ; dma64
f3b1 mov  0x50(%rbx),%rsi   ; RX reg base
f3b5 shl  $0x4,%edi         ; rxout*16
f3b8 add  0xe0(%rbx),%edi   ; + rcvptrbase
f3be add  $0x4,%rsi         ; PTR
f3cf call writel            ; write(PTR, rcvptrbase + rxout*16)
```
`_dma_ddtable_init` (`0xe683`): `cmpb $0,0x104(%rdi)` (aligndesc_4k) → when set
it **skips** assigning `rcvptrbase` (`di+0xe0`), so it stays 0. `dma_attach`
(`0x1076f`–`0x107c3`) sets `di+0x104 = (readback(addrlow after 0xff0) == 0)`;
BCM4352 takes the aligned path. Therefore **rcvptrbase = 0** and
**PTR = rxout*16 = 0x400** for rxout=64 — not `ring_dma + 0x400`.

PTR readback is a hardware-updated current/last descriptor register whose raw
value carries base/current bits; it is logged (`ptr_field = ptr & 0x1fff`) and
not compared for raw equality. Validation now uses only proven fields:
CONTROL.RE, ADDRLOW, ADDRHIGH, STATUS0 valid + not DISABLED, STATUS1 error bits.

Runtime STATUS0=0x2000e000 → RS=0x2 (IDLE), CD=0; STATUS1=0x0000e000 → RE=0,
AD=0xe000. RX was healthy and idle.
