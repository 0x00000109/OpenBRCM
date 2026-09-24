# M3.4D3A1 — vendor post-common-init / pre-PHY tail recovery (ANALYSIS COMPLETE)

Status: **`M3.4D3A1 = ANALYSIS COMPLETE`** / `NOT IMPLEMENTED` /
`NOT HARDWARE PROVEN`. This document is read-only reverse-engineering of
`wlc_hybrid.o_shipped` plus a read-only comparison with upstream `brcmsmac`
(performed under the `ANALYSIS ONLY` rule). **No hardware, no MMIO, no
`insmod`, no implementation.** Canonical status: `M3.4D3A0 = HARDWARE RUNTIME
PROVEN`, `M3.4D3A1 = ANALYSIS COMPLETE`.

Blocker-closure follow-up: §15 resolves all remaining value-source /
struct-field blockers (`wlc_info`/`wlc_pub` model, the MAC six bytes, the SCR
`0x24` read-modify-write + first-init gate, `getvar`/NVRAM origin,
`btc_params`/`btc_flags` absent behavior, `M_MAX_ANTCNT`). §16 returns
`D3A1 IMPLEMENTATION GO: YES` (still NOT IMPLEMENTED / NOT HARDWARE PROVEN).

Primary provenance: `wlc_hybrid.o_shipped`, sha256
`352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743`.
Upstream C3 (`/usr/src/linux-7.2.2-build/.../brcmsmac/`) is secondary
corroboration only. Addresses are `.text` offsets. Prior analysis is in
`docs/m34d3_bsinitvals.md` (Appendices A–D); this document **re-proves** the
D3A1-relevant parts directly from the blob and corrects two §B.4 details.

---

## 0. Starting state (proven input — not re-derived here)

- **D2A**: rev42 ucode uploaded (10850 writes), PSM running,
  `MI_MACSSPNDD` observed.
- **D2B**: all 610 common initvals applied (113 × 16-bit, 497 × 32-bit); known
  state `MACCONTROL=0x04020402`, `MACINTMASK=0`, `M_FIFOSIZE0..3 =
  01c4/0000/0000/079e`, `SHM[0x14]=0x00b4`.
- **D3A0 (hardware proven)**: four TX DMA engines and FIFO0 RX programmed,
  64 RX mappings, `RX CONTROL=0x84d`, programmed PTR `0x400`,
  `ADDRHIGH=0x80000000`, host IRQ route disabled, full lifecycle teardown
  proven.

The isolated D3A0 ordering is **not** assumed to be the vendor order. The
question this document answers is where the vendor actually puts DMA init
relative to the rest of the post-common tail.

---

## 1. Exact vendor post-common call graph

`wlc_bmac_init` (`0x6828a`, size 5071). The first D11 write is `wl_intrsoff`
(`0x682d6`). The post-common portion (proven from direct linear control flow in
`re fn wlc_bmac_init`, disassembly cross-checked with `objdump`):

```
0x68b98  sub_60f67(dev, d11ac1initvals42)          [M3.4D2B]
0x68b9d  if (phyrev > 0x27)                        ; BCM4352 0x2a -> taken
0x68bab      sub_67efd(dev)                        ; TXE0 FIFO fixup
0x68bb0      jmp 0x68fe2                            ; legacy FIFO block skipped
         else 0x68bb5..0x68fdf: xmtfifo_sz[]/host M_FIFOSIZE/TX-flush
             (NOT executed on BCM4352 rev42)

--- T1: pre-DMA D11/MAC tail (0x68fe2..0x69211) ---
0x68fef  wlc_bmac_write_shm(0x80, 8)               M_MBURST_SIZE
0x69001  wlc_bmac_write_shm(0x5c, 0x0a)            M_MAX_ANTCNT
0x6901a  osl_writel(D11+0x100, *(u32)(dev+0x1ac))  intrcvlazy[0]
0x6901f  if (phyrev==4) writel(D11+0x10c, 1<<24)   [SKIP rev42]
0x69047  wlc_bmac_mctrl(dev, 0x40060000, 0x40020000)
0x69059  osl_writel(D11+0x188, 0x80000000)         tsf_cfprep
0x6906b  osl_writel(D11+0x18c, 0x02000000)         tsf_cfpstart
0x6907d  osl_writel(D11+0x128, 0x4000)             macintstatus W1C (MI_GP1)
0x6908c  osl_writel(D11+0x24, 0x10000)             intctrlregs[0].intmask = I_RI
0x69091  if (phyrev==4) writel(D11+0x3c, 0x10000)  [SKIP rev42]
0x690b1  wlc_bmac_macphyclk_set(dev, 1)            SICF_MPCLKE
0x690b6  if (phyrev>4) { dly=si_clkctl_fast_pwrup_delay();
                         *(u16)(dev+0x192)=dly;
                         writew(D11+0x6a8, dly) }  scc_fastpwrup_dly   [rev42]
0x690e2  if (phyrev>0x28) *(u16)(dev+0x192) += sub_5fdca(dev)           [rev42]
0x690fa  write_shm(0x16, phyrev)                   M_MACHW_VER
0x6910e  if (phyrev>0x0c) { write_shm(0xc0, *(u16)(dev+0xa4));
                            write_shm(0xc2, *(u16)(dev+0xa6)); }
                                                   M_MACHW_CAP_L / _H
0x6913f  copyto_objmem(0x18, (dev+0x104), 2)       S_DOT11_SRC_LMT (SRL)
0x6915e  copyto_objmem(0x1c, (dev+0x106), 2)       S_DOT11_LRC_LMT (LRL)
0x6917d  if ([r13+0x718]==0) copyto_objmem(0x24, [r13+0x20], 4)
0x691ae  write_shm(0x44, *(u16)(dev+0x108))        M_SFRMTXCNTFBRTHSD (SFBL)
0x691c2  write_shm(0x46, *(u16)(dev+0x10a))        M_LFRMTXCNTFBRTHSD (LFBL)
0x691d6  if (phyrev>0x0f) { D11+0x688 &= 0xfff;
                            writew(D11+0x69c, 1) } ifs_ctl / ifs_aifsn   [rev42]
0x69211  [r13+0x68]=0

--- DMA (0x6921c..0x6924a) ---
0x6921c  for i in 0..5: if (dev->di[i]) di[i]->vfn[+0x08]()   ; 4x dma_txinit
0x69236  dev->di[0]->vfn[+0xa0]()                             ; dma_rxinit
0x69243  dev->di[0]->vfn[+0xd8]()                             ; dma_rxfill

--- T2: post-DMA D11/MAC tail (0x69250..0x695d8) ---
0x69250  if (phyrev==4) { di[3]->rxinit(); di[3]->rxfill(); }  [SKIP rev42]
0x69273  chip-TSF-clock switch; BCM4352 (0x4352) -> none       [SKIP]
0x6930e  btc_base = read_shm(0x92)*2
         if (btc_base) for i in 0..0x76: getvar("btc_params%d",i) ->
             write_shm(btc_base + 2*i)
0x693a2  if (chipid==0x4352 || 0xa8dc) 4 extra write_shm:
             btc_base+2=0x7530, +0x10=0x4e20, +0x12=0x7530, +0x2c=0x753
0x69426  getvar("btc_flags") -> *(u16)(dev+0xb0+8); sub_62b79 (wlc_bmac_mhf x5)
0x69466  if (phyrev>0x27) { write_shm(0x78c, wlc+8/9);
                            write_shm(0x78e, wlc+0xa/0xb);
                            write_shm(0x790, wlc+0xc/0xd); }    [rev42]
0x694d8  read_shm(0x8e); if (phyrev==0x21) {...}                [SKIP rev42]
0x6955b  if (arg#3) wlc_bmac_mute(dev,1,1)                      [SKIP: arg#3=0]
0x69562  *(u32)(dev+0x16c)=1
0x69580  if ([[dev+0xE8]+0x1C]==7) wlc_phy_switch_radio(...)    [SKIP: phytype=0xB]
0x69599  if (chipid in {0xa9c4,0x4360,0xaa06,0x4352,0x4350})
             wlc_bmac_switch_macfreq(dev, 0)                    [TAKEN 0x4352]
0x695d8  sub_6656c(dev, chanspec, band=0)          *** band init -> PHY ***
0x695ee  wl_intrsrestore
```

`sub_6656c` (`0x6656c`, band-init helper; two callers: `wlc_bmac_init 0x695d8`
and `wlc_bmac_set_chanspec 0x67bd0`) leads to the real PHY entry:

```
0x665d6  sub_62766                 ; 5 x write_shm (0x5e/0x60/0x62/0x78/0xd4)
0x6692c  si_core_sflags
0x66983  si_core_sflags
0x669bd  sub_60f67(dev, d11ac1bsinitvals42)        ; 73 records (D3B)
0x669df  wlc_phy_init(pi, chanspec)                *** real PHY entry ***
           -> 0xbac31 wlc_phy_chanspec_shm_set
           -> 0xbac44 osl_readl(D11+0x120)         ; maccontrol read only
           -> 0xbac84 wlc_phy_anacore             *** first PHY indirect MMIO ***
0x669e7  sub_62684
0x669fa/0x66a0d wlc_bmac_set_cwmin / set_cwmax
0x66a2d  sub_62716
0x66a45/0x66a5d write_shm
0x66a65  sub_62403
0x66a8e  write_shm
0x66a96  sub_627c9
0x66ac9  wlc_bmac_mhf
0x66ad1  sub_6106b                 ; GPIO
0x66b28  sub_62766
0x66b30  wlc_bmac_set_extlna_pwrsave_shmem
```

**First real PHY/RF operation** = `wlc_phy_anacore` (`0xbac84`) inside
`wlc_phy_init`, reached only after the bsinitvals applier.

### 1.1 Corrections to prior text

- §B.4 item 4 (`0x69035`, `writel 0x01000000`) is **gated `phyrev==4`** and
  wrote `D11+0x10c`, not an unconditional `intrcvlazy` sibling. Not executed on
  rev42.
- §B.4 item 8 implied a single `intctrlregs` write; the blob writes
  `D11+0x24 = I_RI` unconditionally and a second `D11+0x3c = 0x10000` gated
  `phyrev==4` (SKIP rev42).

---

## 2. `sub_67efd` fully reversed (BCM4352/rev42)

`sub_67efd` (`0x67efd`, size 909). This is the real rev42 FIFO stage; the C3
equivalent is `brcms_b_corerev_fifofixup()`. Entry `rdi = dev`.

### 2.1 Operations in execution order

1. `machwcap = osl_readl(D11+0x15c)`; `v = (machwcap >> 1) & 0xffc`; stored to
   driver globals `.bss+0xb34` and `.data+0x16c`. **read-only.**
2. `phyrev = dev->[0x84]` (`0x67f2d`/`0x67f53`).
3. **RXE gate** (`0x67f59`): `if (phyrev == 0x2c || phyrev <= 0x2a)` skip the
   RXE block; else run it. **rev42 (0x2a) skips it.**
   - RXE (not rev42): `writew(0x42c,0x1500)`, `writew(0x42e,0x28ff)`,
     `writew(0x43a,0x2900)`, `writew(0x43c,0x3cff)`, `writew(0x406,0x101)`,
     `writew(0x406,0x1)`.
4. `writew(0x542, <machwcap-derived>)` (`xmtfifoflush`);
   `writew(0x540, 5)` (`xmtfifocmd`);
   poll `readw(0x540) & 1` until 0, bound `0xd1` decrement 10 (~20 iters).
5. **7-entry loop** over table `.rodata+0x284740` = `{7,0,1,2,3,4,5}`. Per entry:
   - if entry == 7: `di = cx = def = 0x2a`;
   - else: `di = <machwcap global>`, `cx = <per-phyrev table word>`,
     `def = (phyrev in {0x2c,0x29,0x2d,0x2e,0x2f}) ? 6 : 0xb` (rev42 → `0xb`);
   - `writew(0x54a, di)` `xmtfiforqpri`,
     `writew(0x54c, cx)` `xmttplatetxptr`,
     `writew(0x520, def)` `xmtfifodef`,
     `writew(0x54e, ((edx-4)<<8)|edx)`,
     `writew(0x550, 0x740c)` `xmttplateptr`,
     `writew(0x548, entry|0x10)` `xmtfifoprirdy`.
     ⇒ 7 × 6 = 42 writes.
6. **42-entry loop** `idx = 0..41`. Per entry:
   `writew(0x534, idx)`, `writew(0x536, min(idx+2,0x29))`,
   `writew(0x532, min(idx+2,0x29) + (idx==0 ? 1 : 0))`,
   `writew(0x530, (idx<<4)|0x8007)`;
   poll `readw(0x530) == 0`, bound `0xd1` decrement 10.
   ⇒ 42 × 4 = 168 writes.

Total on rev42: **1 readl, 2 + 42 + 168 = 212 writew**, two bounded polls, and
no other access. Per-rev parameter tables: `.rodata+0x284740` (entry list) and
`.rodata+0x2846e0` (per-fifo parameter words).

### 2.2 Side-effect classification

| class | present? | detail |
| :--- | :--- | :--- |
| PASSIVE CONFIGURATION | yes | `machwcap` read; FIFO parameter writes |
| FIFO RESET/CONTROL | yes | `0x540/0x542` flush/cmd; `0x520/0x548/0x54a/0x54c/0x54e/0x550`; `0x530..0x536` |
| DMA-AFFECTING | **no** | no access to `0x200..0x37F` |
| IRQ-AFFECTING | **no** | no `intctrlregs`/`macintstatus`/`macintmask` |
| PHY/RADIO-AFFECTING | **no** | no `0x3fc/0x3fe`, no radio window |
| ASYNC/SELF-TRIGGERING | **no** | two synchronous FIFO-ack polls, both bounded |

### 2.3 Direct answers

- Mandatory for rev42? **Yes** — taken unconditionally for `phyrev > 0x27`
  (`0x68b9d`); C3 calls the equivalent when `fifosz_fixup=true`.
- Required before DMA initialization? **Yes** (vendor places it before the DMA
  loop; it defines/reset the TX FIFO allocation the TX engines later use).
- Required before bsinitvals? It precedes bsinitvals in vendor order. The
  bsinitvals writes are D11 SHM/IHR and do not read the TXE0 FIFO block, so it
  is not a data dependency, but it is a fixed vendor ordering requirement.
- Required before PHY init? It precedes band init/PHY; no PHY dependency.
- Safe to execute in an isolated no-PHY test? **Yes** (no DMA armed, no IRQ,
  no PHY/radio, `EN_MAC=0`, bounded polls). It does mutate TXE0 FIFO state, so
  the enclosing test must be one-shot/vendor-ordered, not a free-form probe.

---

## 3. Omitted runtime SHM / NVRAM / rate / power tail

Groups omitted from D3A0's prefix, in vendor order. Cross-checked against C3
`d11.h` names where an equivalent exists.

| # | write | SHM off | value source | class | NVRAM? | pre-PHY consumed | DMA impact | fw impact |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `M_MBURST_SIZE` | `0x80` | const `8` | constant | no (C3 const) | yes (ucode) | no | frameburst |
| 2 | `M_MAX_ANTCNT` | `0x5c` | const `0x0a` | constant | no | yes | no | antenna swap |
| 3 | `M_MACHW_VER` | `0x16` | `phyrev` | runtime | no | yes | no | ucode MAC version |
| 4 | `M_MACHW_CAP_L/_H` | `0xc0/0xc2` | `machwcap` lo/hi | runtime | no | yes | no | ucode MAC caps |
| 5 | SCR SRL/LRL | objmem `0x18/0x1c` | `dev+0x104/0x106` | runtime/config | yes (derived) | yes | no | retry limits |
| 6 | SCR 4-byte | objmem `0x24` | read-modify-write of SCR `0x24`; **skipped on first init** | runtime | no | yes | no | rate/tpc |
| 7 | `M_SFRMTXCNTFBRTHSD` | `0x44` | `dev+0x108` | config | yes | yes | no | rate fallback |
| 8 | `M_LFRMTXCNTFBRTHSD` | `0x46` | `dev+0x10a` | config | yes | yes | no | rate fallback |
| 9 | BTC params | `btc_base+2i` | NVRAM `btc_params%d` (119) | NVRAM/board | **yes** | yes | no | coexistence |
| 10 | BTC extras | `btc_base+2/0x10/0x12/0x2c` | const `0x7530/0x4e20/0x7530/0x753` | const (4352) | no | yes | no | coexistence |
| 11 | BTC flags | MHF (SHM) | NVRAM `btc_flags` → `sub_62b79` (`wlc_bmac_mhf` ×5) | NVRAM/board | **yes** | yes | no | coexistence/MHF |
| 12 | MAC 6 bytes | `0x78c/0x78e/0x790` | `wlc_pub+8..+0xd` = `cur_etheraddr` (SPROM/NVRAM `macaddr`) | runtime | yes | yes | no | device MAC (name of SHM slot UNKNOWN) |

Notes:

- `btc_base = read_shm(0x92) * 2` (`0x6930e`); if zero the 119-write loop **and**
  the `btc_flags` block are skipped. The 4 extra writes (row 10) are
  unconditional for chip `0x4352` once `btc_base != 0`. A `btc_params` key that
  is absent is **skipped, not zero-filled** (`getvar == NULL`).
- Row 5/7/8 (`SRL/LRL`, `SFBL/LFBL`) are the NVRAM-derived retry limits; C3
  names them and writes them from `wlc_hw->SRL/LRL/SFBL/LFBL`.
- Row 12 source is **`*wlc_info + 8`**, the 6-byte `cur_etheraddr` MAC; see §15.2
  for the full `wlc_bmac_attach`(macaddr) → `wlc_attach`(memcpy) writer chain.
  The SHM target slot name is **UNKNOWN** (microcode-only consumer), but the
  value/source is proven and this is non-blocking.
- **Values OpenBRCM can already obtain:** `phyrev`, `machwcap`, SRL/LRL/SFBL/
  LFBL analogues, the validated SPROM MAC (`ob_si_read_mac`, rev11 IL0MAC
  `+0x90`). **Not available (and not required):** NVRAM-text `btc_params%d` /
  `btc_flags`; on the ASUS PCE-AC56 they are absent and the vendor skips them.
- None of these writes touch the DMA register block, PHY, or radio.

---

## 4. Exact DMA ordering (canonical BCM4352 rev42)

Vendor linear sequence (single function, no branch reorders it on rev42):

```
common initvals (D2B)
  -> sub_67efd
  -> M_MBURST_SIZE / M_MAX_ANTCNT
  -> intrcvlazy[0]
  -> MACCONTROL transition
  -> tsf_cfprep / tsf_cfpstart / macintstatus / intctrlregs[0].intmask
  -> macphyclk_set
  -> fast_pwrup_delay / sub_5fdca
  -> M_MACHW_VER / M_MACHW_CAP_L/H
  -> SCR SRL/LRL/rate + SFBL/LFBL
  -> ifs_ctl / ifs_aifsn
  -> 4x dma_txinit + dma_rxinit + dma_rxfill          <-- DMA
  -> BTC params/flags (NVRAM) + 0x78c/0x78e/0x790
  -> switch_macfreq
  -> sub_6656c -> bsinitvals -> wlc_phy_init (first PHY op)
```

Answers:

| Q | answer | evidence |
| :--- | :--- | :--- |
| A. DMA before/after `sub_67efd`? | **after** | `0x68bab` < `0x6921c` |
| B. DMA before/after runtime SHM tail? | **before** the BTC/NVRAM tail (`0x6921c` < `0x6930e`); **after** the SHM writes in T1 | direct order |
| C. DMA before/after MACCONTROL `0x…` transition? | **after** | `0x69047` < `0x6921c` |
| D. DMA before/after `macphyclk_set`? | **after** | `0x690b1` < `0x6921c` |
| E. DMA before `bsinitvals`? | **before** | `0x6921c` < `0x669bd` (via `0x695d8`) |
| F. DMA before `wl_phy_init`? | **before** | `0x6921c` < `0x669df` |
| G. Proven vs inferred? | **all proven from direct control flow** | linear body, only gated sub-blocks are conditional; their position is fixed |

**The vendor interleaves the DMA init inside the tail: `T1 → DMA → T2`.**

---

## 5. IRQ-source ordering

| order | addr | register | value | width |
| :--- | :--- | :--- | :--- | :--- |
| 1 | `0x6901a` | `D11+0x100 intrcvlazy[0]` | `0x01000000` (`1<<IRL_FC_SHIFT`, 24) | 32 |
| 2 | `0x6907d` | `D11+0x128 macintstatus` | `0x4000` (W1C `MI_GP1`) | 32 |
| 3 | `0x6908c` | `D11+0x24 intctrlregs[0].intmask` | `I_RI=0x10000` | 32 |
| – | never | `D11+0x12c macintmask` | held 0 by `wl_intrsoff` (`0x682d6`) | 32 |

- `intrcvlazy[0]` is written **before** the MACCONTROL transition and **before**
  DMA init.
- `I_RI` (per-FIFO RX source, `1<<16`) is armed before the RX engine is enabled.
- `MI_DMAINT` (`1<<15`, MAC aggregate) is **not** written.
- BCMA/PCI host route is controlled by `bcma_host_pci_irq_ctl` via
  `wl_intrsoff`/`wl_intrsrestore`; it is **off** across the whole window.

Pre-PHY vendor state: source armed? **yes**. Aggregate MAC interrupt enabled?
**no**. BCMA host route enabled? **no**. CPU delivery possible? **no**.

---

## 6. MACCONTROL state machine (D2B exit → before `wl_phy_init`)

`wlc_bmac_mctrl` (`0x6066d`) is shadow-cached at `dev+0x168`; write to
`D11+0x120` only when changed. Exactly one transition in the tail:

- at `0x69047`: `old = 0x04020402`, `mask = 0x40060000`,
  `val = 0x40020000`, `new = (old & ~mask) | val = **0x44020402**`.
- semantic bits: `MCTL_DISCARD_PMQ(1<<30)` set; `MCTL_AP(1<<18)` cleared;
  `MCTL_INFRA(1<<17)` set; `MCTL_PSM_RUN(1<<1)` kept; `MCTL_EN_MAC(1<<0)` 0;
  SHM_EN bit8 0.
- Persistence: `macphyclk_set` writes core cflags; `switch_macfreq` writes
  `0x62e/0x630`; the bsinitvals applier writes `0x160/0x164/0x166/0x680..0x700`.
  **MACCONTROL stays `0x44020402` from `0x69047` until `wlc_phy_init`** (which
  only reads it at `0xbac44`). Bit30 semantics: `MCTL_DISCARD_PMQ`; MCTL_AP is
  cleared for the initial BSS-STA station role. Unknown bits remain UNKNOWN by
  name; operation and safety are known.

---

## 7. `wlc_bmac_macphyclk_set`

`wlc_bmac_macphyclk_set(dev,on)` (`0x65006`) =
`si_core_cflags(D11 si, SICF_MPCLKE=0x10, on?0x10:0)` (`0x65024`). It is a D11
**core cflags** gate, fully reversible; **no PHY/radio MMIO, no PLL/synth, no
asynchronous activity**. It only lets the MAC dynamically gate the PHY clock.
Vendor calls it ON at `0x690b1`. Belongs in the **D3A1 pre-PHY tail** (already
present in D3A0).

## 8. `wlc_bmac_switch_macfreq`

`wlc_bmac_switch_macfreq` (`0x64cbf`), spurmode `0`. For BCM4352 (`0x64f14`):

```
eax = si_pmu_get_bb_vcofreq(si, 0x28)          ; PMU read
bcm_uint64_divide(&out, eax, 0x80000000, 0x3a9)
writew(D11+0x62e, out.lo)                      ; tsf_clk_frac_l
writew(D11+0x630, out.hi)                      ; tsf_clk_frac_h
```

No PHY/radio write, no PLL/synth write, no state-machine start; values are
runtime PMU-VCO-derived, not band/channel dependent. Readback equality is
meaningful only if the driver derives the same PMU value; otherwise treat as
`DERIVED/runtime`. Belongs in the **D3A1 pre-PHY tail** (it is the last call
before `sub_6656c`).

---

## 9. D3A1 boundary

Requirements: no PHY indirect writes, no radio, no channel, no calibration, no
`EN_MAC`, no host IRQ delivery, no TX payload, no mac80211; DMA only with proven
D3A0 semantics; vendor ordering preserved.

- **first included instruction/call:** `sub_67efd` at `0x68bab`.
- **last included instruction/call:** `wlc_bmac_switch_macfreq` at `0x695cb`.
- **exact STOP point:** immediately before the call at `0x695d8`.
- **first forbidden next call:** `sub_6656c` (`0x695d8`), which runs
  `bsinitvals` (`0x669bd`) and then `wlc_phy_init` (`0x669df`,
  first PHY op `wlc_phy_anacore 0xbac84`).

D3A1 therefore includes the vendor DMA init **in its vendor position** (reusing
the D3A0-proven DMA lifecycle), not as a separate post-hoc stage.

---

## 10. Design A vs Design B

- **Design A** (tail → DMA): **NOT vendor-faithful** — it would move the
  post-DMA tail (BTC/NVRAM, `0x78c..`, `switch_macfreq`) before DMA.
- **Design B** (DMA → tail): **NOT vendor-faithful** — it would move
  `sub_67efd`, MBURST/MAXANTCNT, MACCONTROL, macphyclk and the SCR/SFBL SHM
  writes after DMA.
- **Correct design:** preserve the vendor **interleaving**:
  `T1 → DMA (reuse D3A0) → T2 → STOP before sub_6656c`.
  D3A0 remains the isolated proof of the DMA sub-lifecycle; D3A1 is the full
  vendor-ordered post-common tail with that sub-lifecycle embedded.

---

## 11. Deterministic postconditions (proposed D3A1)

| item | expected | class |
| :--- | :--- | :--- |
| `MACCONTROL` (`0x120`) | `0x44020402` | exact (derived from D2B) |
| `MACINTMASK` (`0x12c`) | `0` | exact |
| `INTRCVLAZY[0]` (`0x100`) | `0x01000000` | exact |
| `intctrlregs[0].intmask` (`0x24`) | `0x10000` (masked `& I_RI`) | masked |
| `tsf_cfprep` (`0x188`) | `0x80000000` | exact |
| `tsf_cfpstart` (`0x18c`) | `0x02000000` | exact |
| `M_FIFOSIZE0..3` | `01c4/0000/0000/079e` | exact (unchanged) |
| SHM `M_MBURST_SIZE` (`0x80`) | `0x0008` | exact |
| SHM `M_MAX_ANTCNT` (`0x5c`) | `0x000a` | exact |
| SHM `M_MACHW_VER` (`0x16`) | `phyrev` | exact |
| SHM `M_MACHW_CAP_L/H` (`0xc0/0xc2`) | `machwcap` lo/hi | exact |
| `tsf_clk_frac_l/h` (`0x62e/0x630`) | PMU-VCO-derived | state/diagnostic |
| TX0..3 `control` bit0 (XE) | `1` | exact |
| TX0..3 `control` cap bits | equal to attach-time read | derived (RMW) |
| TX0..3 `addrhigh` | `0x80000000` | exact |
| TX0..3 `addrlow` | ring base | derived |
| TX0..3 `status0` state | not DISABLED | state |
| RX `control` | `0x0000084d` | exact |
| RX `addrhigh` | `0x80000000` | exact |
| RX programmed PTR | `0x400` | exact (programmed value only) |
| RX `status0` | RS=IDLE (`0x2`) | state |
| RX `status1` error bits | `0` | exact |
| PSM state | `MI_MACSSPNDD`/PSM_RUN as inherited | state |
| `EN_MAC` | `0` | exact |
| D11 core cflags `SICF_MPCLKE` (bit4) | `1` | exact |
| host IRQ route | disabled (`macintmask=0`, no `bcma_host_pci_irq_ctl`) | exact |

Do **not** require equality on hardware-current pointer/status fields (raw RX
`ptr`/`status0` field bits); use state fields and the programmed PTR value.

---

## 12. Failure / teardown model

D3A1 reuses the proven D3A0 quiesce lifecycle. Per-stage matrix:

| stage | failure return | DMA started? | teardown | per-channel reset sufficient? | containment | retry safe? | reboot? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `sub_67efd` | error | no | none | n/a | n/a | not proven | no |
| T1 SHM/MACCONTROL/macphyclk | error | no | none | n/a | n/a | unknown | no |
| DMA `dma_txinit/rxinit/rxfill` | error | **yes** | required | yes (verified) | fallback only | **no** | if reset unverified |
| T2 BTC/NVRAM/SHM | error | yes | required | yes (verified) | fallback only | **no** | if reset unverified |
| `switch_macfreq` | error | yes | required | yes (verified) | fallback only | **no** | if reset unverified |
| validation mismatch | error | yes | required | yes (verified) | fallback only | **no** | if reset unverified |

Rules (locked to D3A0/D.7/D.8): free/unmap **only** after every initialized
engine is verified stopped (`status0` state 0); `bcma_core_disable` is
containment only and never authorizes a free; on unverified reset, latch fatal,
retain memory, keep probe bound, require reboot. No DMA-backed memory is freed
while a hardware engine may reference it.

---

## 13. Vendor vs upstream brcmsmac (secondary)

| vendor behavior | upstream equivalent | verdict | reason |
| :--- | :--- | :--- | :--- |
| `sub_67efd` TXE0 FIFO fixup | `brcms_b_corerev_fifofixup` (`main.c:2036`) | **PARTIAL** | same purpose/reset; C3 uses `xmtfifo_sz[]`; vendor uses `machwcap` + per-rev tables + 7/42 loops |
| MBURST/MAXANTCNT/intrcvlazy/mctrl/tsf/macintstatus/intctrlregs/macphyclk/fastpwrup | `brcms_b_coreinit` (`main.c:3127`) | **MATCH** | same constants/offsets and order |
| `M_MACHW_VER`/`M_MACHW_CAP_L/H`, SCR SRL/LRL, SFBL/LFBL, ifs | `brcms_b_coreinit` | **MATCH** | same SHM/objmem semantics |
| 4x `dma_txinit` + `dma_rxinit` + `dma_rxfill` | `brcms_b_coreinit:3288-3296` | **MATCH** | same order and channel count (D3A0-proven) |
| BTC params/flags NVRAM, `0x78c..0x790` | not in C3 `brcms_b_coreinit` | **DIFFERENT** | vendor-specific/newer tail; C3 lacks it |
| `wlc_bmac_switch_macfreq` 0x4352 path | `brcms_b_switch_macfreq` (`main.c:2096`) | **PARTIAL/DIFFERENT** | C3 handles only 43224/43225/LCN; vendor 0x4352 uses PMU-VCO-derived frac |
| bsinitvals then `wlc_phy_init` then cwmin/cwmax/timing/phytype | `brcms_b_bsinit` (`main.c:1659`) + `brcms_c_ucode_bsinit` (`main.c:680`) | **MATCH** | same shape |

Upstream is used only to corroborate blob-derived behavior; blob remains
primary and is never replaced by upstream.

---

## 14. D3A1 implementation plan (NOT implemented)

Reuse: `ob_d3a0_*` DMA allocate/program/post/validate/quiesce/free (proven),
`ob_dma` rings, `ob_initvals_run_d2b()`, the D3A0 fatal/quiesce model.

New (decomposition suggestion, names non-binding):

1. `ob_d11_fifo_fixup()` — `sub_67efd` equivalent (the 7/42 loops + machwcap).
2. `ob_d11_mac_tail_pre_dma()` — T1: MBURST/MAXANTCNT, intrcvlazy, mctrl,
   tsf, macintstatus, intctrlregs, macphyclk, fastpwrup, machwver/cap, SCR
   retry, SFBL/LFBL, ifs. (D3A0's prefix is a subset; refactor it into this
   helper and call from D3A0 to avoid duplication.)
3. `ob_dma_*` — reuse D3A0.
4. `ob_d11_mac_tail_post_dma()` — T2: BTC params/flags (needs NVRAM parsing),
   `0x78c/0x78e/0x790`, `switch_macfreq`.
5. `ob_d3a1_validate()` — §11 postconditions.
6. `ob_d3a1_quiesce()` — reuse D3A0 quiesce.
7. new isolated mode `d11_tail_test_only=1` (mutually exclusive with the others).

New prerequisites: NVRAM `btc_params%d` / `btc_flags` parsing; a source for the
`wlc+8..+0xd` six bytes or an explicit `UNKNOWN` skip with documented reasoning.

## 15. Blocker closure — value-source / struct-field analysis

This section closes the value-source / struct-field blockers formerly listed in
§15 of the first revision. All findings are read-only RE of the blob
(`re`/`objdump`, `.rela.text`); upstream is corroboration only. The prior
`sub_67efd`, ordering and STOP analysis is unchanged and not re-derived here.

### 15.1 `wlc` object model (`wlc_info` vs `wlc_pub`)

The address the earlier draft called `wlc` is **two dereferences below the
`wlc_bmac_init` argument**; the naming was imprecise. Correct object graph
(proven from `wlc_attach_malloc 0x4ebbb`, `wlc_info_init 0x24d9b`,
`wlc_attach 0x37d10`, `wlc_bmac_init 0x6828a`):

| name | how reached | size | constructor | notes |
| :--- | :--- | :--- | :--- | :--- |
| `wlc_hw` (`dev`) | `rdi` of `wlc_bmac_init`; `rbx` | — | `wlc_hw_attach 0x798ff` | D11 hardware object |
| `wlc_info` (outer) | `r13 = *dev = dev->wlc` | `0x838` | `wlc_info_init 0x24d9b` | `wlc_attach` returns this |
| `wlc_pub` (`P`) | `P = *wlc_info` (field `+0x00`) | `0x348` | fields seeded by `wlc_info_init` | `*(*dev)` |

`wlc_info` (r13) field map (`0x00..0x40`, verified writers/readers):

| off | width | field | evidence |
| :--- | :--- | :--- | :--- |
| `+0x00` | ptr | `pub` (`P`) | `wlc_attach_malloc 0x4eddd` `*(r14)=r15`; `wlc_attach 0x37dda` `r12=*(rbx)` |
| `+0x08` | ptr | `osh`/handle | `wlc_attach 0x37ddd` `*(r14+8)=r14` (2nd attach arg) |
| `+0x20` | ptr | `hw` (== `dev`) | `wlc_attach 0x380fb`/`0x38625` pass `*(wlc+0x20)` as `wlc_hw` to `wlc_bmac_retrylimit_upd`/`wlc_bmac_hw_etheraddr` |
| `+0x68` | u32 | scratch | `wlc_bmac_init 0x69211` `*(r13+0x68)=0` |
| `+0x718` | u8 | first-init gate | `wlc_info_init 0x24f5f` `=1`; read/cleared in `wlc_bmac_init` |

`wlc_pub` (`P`) field map (nearby, verified):

| off | width | field | evidence |
| :--- | :--- | :--- | :--- |
| `+0x07` | u8 | `=3` | `wlc_info_init 0x24dd6` |
| `+0x08` | 6×u8 | `cur_etheraddr` (MAC) | `wlc_ampdu_macaddr_upd 0x4f511`; `wlc_attach 0x38641` |
| `+0x14` | u32 | arch/rev cached | `wlc_set_mac 0x3a120` compares to `0x27` |
| `+0x54` | u32 | `=-1` | `wlc_info_init 0x24e99` |
| `+0x80` | u32 | `=0` | `wlc_info_init 0x24f04` |
| `+0x100` | ptr | `si`/bus | `wlc_attach 0x37de7`; read `wlc_bmac_attach 0x69908` |
| `+0x108` | ptr | per-hw NVRAM vars buffer | `wlc_attach 0x37df3`; `wlc_bmac_attach 0x6991a` |
| `+0x110` | u32 | vars length | `wlc_attach 0x37e06`; `wlc_bmac_attach 0x69932` |
| `+0x2F8` | ptr | primary owner back-pointer | `wlc_set_mac 0x3a109` |

So the six bytes are at **`wlc_pub+0x08` = `cur_etheraddr`**, i.e. in the object
`*(wlc_info)`, not in `wlc_info` itself.

### 15.2 The six bytes at `wlc_pub+8..+0xd` — complete writer chain

Class: **a 6-byte IEEE MAC address** (six u8; also consumed as three big-endian
u16 pairs). Not rate/power/antenna/timing/coexistence.

Backward dataflow (attach path):

1. `wlc_bmac_attach 0x6a94a`: `getvar(*(dev+0xC0), "macaddr")`; if `NULL` →
   attach error `0x15`. Else `bcm_ether_atoe(value, dev+0x178)` parses the text
   MAC into `wlc_hw->hw_etheraddr` (`0x6a96e..0x6a978`), with a zero/`0xff`
   sanity check.
2. `wlc_attach 0x3861e..0x38641`: `wlc_bmac_hw_etheraddr(dev, buf)` reads
   `dev+0x178` (6 bytes); then `osl_memcpy(wlc_pub+8, buf, 6)` — **the writer**.
3. Later interface/`cur_etheraddr` iovars rewrite `dev+0x178`
   (`wlc_bmac_set_hw_etheraddr 0x611f2`, called from `wlc_doiovar 0x4823a` et
   al.); those are runtime BSS changes, not the initial bring-up value.

`wlc_ampdu_macaddr_upd` independently proves the field is the MAC: it does
`osl_memcpy(tmp, wlc_pub+8, 6)` then programs the 8-byte A-MPDU MAC template.

Dependency summary: **board/SPROM/NVRAM dependent, not band/channel/PHY
dependent**; constant during D3A1 (mac80211 is not up yet). Initialization site =
`wlc_attach`; constructor path = `wlc_bmac_attach` (`macaddr` parse); later
updates = `cur_etheraddr` iovars. No other writer exists on the D3A1 path.

### 15.3 `r13` identity and `r13+0x20` (SCR `0x24` source)

- `r13 = *dev = dev->wlc = wlc_info` (`wlc_bmac_init 0x6835b` `mov (%rbx),%r13`).
- `r13+0x20 = wlc_info->hw = wlc_hw` (see 15.1). **It is a pointer to `dev`; it
  is the first argument to `wlc_bmac_copyto_objmem`, not the data source.** The
  earlier draft's `copyto_objmem(0x24, [r13+0x20], 4)` mis-read the ABI.
- Real ABI: `wlc_bmac_copyto_objmem(wlc_hw=r12, off=esi, src=rdx, len=ecx,
  flags=r8d)` (proven from the callee `0x62d48`).
- Real `0x24` source is the **4-byte stack local `-0x34(%rbp)`**, which is:
  - zero-initialized at `0x6835e` `movl $0,-0x34(%rbp)`; and
  - loaded by `wlc_bmac_copyfrom_objmem(dev, 0x24, &local, 4, 0x20000)` at
    `0x68773` early in the same function.

  So the SCR `0x24` operation is a **read-modify-write / restore** of the
  current SCR `0x24` content (read at `0x68773`, conditionally written back at
  `0x69191`), not a fresh derived value. `wlc_hw` has no `SRL`-style source
  needed for it.

### 15.4 The `r13+0x718` gate — exact meaning and initial value

`wlc_info+0x718` is a **u8 "first-init" flag**:

- only writer of the value `1`: `wlc_info_init 0x24f5f` (`movb $1,0x718(%rdi)`),
  called once from `wlc_attach 0x37e74`;
- `wlc_bmac_init 0x6917d` reads it: `if (flag != 0) { flag = 0; skip the SCR
  0x24 write; } else { copyto_objmem(0x24, local, 4); }`.

Therefore on the **initial BCM4352 bring-up path** (attach → `wlc_info_init` →
`wlc_init` → `wlc_bmac_init`, flag == 1) the SCR `0x24` write is
**ALWAYS SKIPPED** on the first `wlc_bmac_init`, then executed on any later
re-init. It is not configuration-dependent within D3A1.

### 15.5 `getvar` implementation and the variable-table source

`getvar(vars, name)` (`0xb807`) — first argument is the **variable table
pointer**, second the key:

1. `if (name == NULL || strlen(name) == 0) return NULL`.
2. Walk the `vars` buffer as **NUL-separated `name=value` strings**: return
   `value` on `strncmp(key)==0 && key[len]=='='`.
3. If not found → return `nvram_get(name)` (global list fallback).

`getintvar(vars, name)` (`0xba1a`) = `getvar()` then `bcm_strtoul(value, NULL,
0)` (auto base); returns **0** when `getvar` returns `NULL`.

`nvram_get` (`0x19916`) walks a global singly-linked list of blocks
(`{next@0x00, len@0x0C, data@0x10}`; data = NUL-separated `name=value`). The
list is built by:

- `nvram_init 0x19a5f` — allocates 4 KiB, `osl_os_open_image("nvram.txt")`
  (`.rodata.str1.1+0x17e4`), reads blocks, normalizes `\t \r \n NUL` to string
  terminators, `nvram_append`;
- `srom_var_init 0x9704` — parses the **SPROM** (`srom_read`/`srom_parsecis`)
  and appends vars (`sromrev`, `ccode`, `leddc`, `pa%d`, `pd%d`, `pdh%d`,
  `pdl%d`, `gcr%d`, MAC, …).

Both are called from the SI attach path `sub_22b10` (`0x230b6`, `0x230d1`).
Representation = text `name=value` table; lifetime = driver attach→detach;
lookup = linear scan; conversion = `bcm_strtoul(..., 0)`; missing = `NULL`
(→ `getintvar` = 0); default = none. Origin is **SROM-derived vars + a
platform NVRAM text image**, not PCI config/OTP/driver module params.

### 15.6 `btc_params` / `btc_flags` availability and absent behavior

At `wlc_bmac_init 0x6930e`:

- `btc = *(dev+0xB0)` (0x20-byte alloc in `wlc_hw_attach 0x79956`);
  `btc+0x1A (u16) = btc_base_words = read_shm(0x92) * 2`.
- `if (btc_base == 0) goto 0x694d8` — **the whole T2 BTC block (119 params + 4
  extras + `btc_flags`) is skipped**.
- else for `i = 0..0x76`: `snprintf(buf,"btc_params%d",i)`;
  `if (getvar(vars,buf) == NULL) continue;` — **absent ⇒ the write is skipped
  entirely** (it does *not* write zero); present ⇒
  `write_shm(btc_base+2*i, getintvar(vars,buf))`.
- `0x4352` (and `0xa8dc`): 4 unconditional constant writes (same gate):
  `btc_base+0x2=0x7530`, `+0x10=0x4e20`, `+0x12=0x7530`, `+0x2c=0x753`.
- `btc_flags`: `if (getvar(vars,"btc_flags") == NULL) skip;` else
  `btc->flags (btc+8) = getintvar(...)` and call `0x62b79` (BTC MHF applier,
  `wlc_bmac_mhf` ×N). **Absent ⇒ no `btc->flags`, no MHF writes.**

`srom_var_init` generates **no** `btc_params`/`btc_flags` keys (its format
strings are `pa%d`/`pd%d`/`pdh%d`/`pdl%d`/`gcr%d`/`ccode`/`leddc`/`sromrev`/MAC
only). Those keys can therefore only come from the platform `nvram.txt`.

### 15.7 Relationship to the rev11 external SPROM

- The **MAC** is available from the SPROM: OpenBRCM `ob_si_read_mac()`
  (`src/ob_si.c`) validates the CRC/revision and reads rev11 IL0MAC at
  `+0x90` into `hw->mac[6]` (`mac_valid`). That is the same datum the vendor
  parses from `macaddr`.
- `btc_params`/`btc_flags` are **NVRAM text variables, not raw SPROM fields**;
  `srom_var_init` never emits them. **External SPROM alone is insufficient** to
  derive them.

### 15.8 BTC functional requirement for the ASUS PCE-AC56 (BCM4352, no BT)

BTC is **not required** on our board and is **not a value blocker**:

- the whole T2 BTC block is gated by `btc_base = read_shm(0x92)*2` (ucode/PSM
  state produced by the same D2A/D2B prefix); if 0 it is skipped;
- even when non-zero, `btc_params`/`btc_flags` writes happen only if the keys
  exist — with no `nvram.txt` provider and no SPROM keys they are absent, so the
  vendor behavior is skip (15.6);
- the 4 fixed `0x4352` extras are constants and are reproduced verbatim when
  `btc_base != 0`.

Classification: **REQUIRED ONLY IF `btc_base != 0` AND the key is present;
otherwise OPTIONAL/NO-OP**. For a BT-less board it is functionally a no-op, but
the branch/value reproduction is exact and cheap.

### 15.9 `M_MAX_ANTCNT = 0x0a`

SHM `0x5c` = `(0x02e * 2)`, upstream `d11.h` `M_MAX_ANTCNT` = **"antenna swap
threshold"**, and upstream `main.c:131` defines `ANTCNT 10 /* vanilla
M_MAX_ANTCNT val */`, written at `brcms_b_coreinit` (`main.c:3233`). It is a
raw threshold count, not a bitfield/policy, and does **not** vary with chain
count. **OpenBRCM may use exactly `0x000a`.** (Vendor constant == upstream.)

### 15.10 Consumers of SHM `0x78c/0x78e/0x790`

Whole-image scan of the immediates: the **only** references are the three
`wlc_bmac_init` writes (`0x69472/0x694a2/0x694c3`). There is **no host-side
reader/writer**; consumption is microcode-internal. Semantic name of the target
SHM slots is **UNKNOWN**, but the written datum is fully proven: the 6-byte
device MAC, big-endian pairs
(`0x78c=(mac0<<8)|mac1`, `0x78e=(mac2<<8)|mac3`, `0x790=(mac4<<8)|mac5`).
Per the task rule, an unknown symbolic name with a proven value/source is
sufficient for implementation. (The `0x790` hits in `*sslpnphy*` are unrelated
PHY-table offsets.)

### 15.11 Blocker classification

| blocker | class | resolved? |
| :--- | :--- | :--- |
| MAC six bytes / SHM `0x78c/78e/790` | B (source known, name unknown) | **yes** |
| SCR objmem `0x24` / `[r13+0x20]` | C (source known; skipped on first init) | **yes** |
| `[r13+0x718]` gate | C | **yes** |
| `getvar` / NVRAM table origin | C | **yes** |
| `btc_params`/`btc_flags` presence | C (deterministic absent ⇒ skip) | **yes** |
| rev11 SPROM for BTC keys | C (insufficient by design) | **yes** |
| `M_MAX_ANTCNT` | C | **yes** |
| SHM `0x78c` semantic name | A→B (name only) | **non-blocking** |

No **VALUE UNKNOWN** (class A) blocker remains on the BCM4352 rev42 path.

### 15.12 Concrete BCM4352 rev42 values/formulas

| write | value / derivation (target path) |
| :--- | :--- |
| SCR objmem `0x24` | **skip** (first-init gate `wlc+0x718 == 1`); else restore the value read from SCR `0x24` |
| SHM `0x78c` | `(hw->mac[0]<<8) | hw->mac[1]` |
| SHM `0x78e` | `(hw->mac[2]<<8) | hw->mac[3]` |
| SHM `0x790` | `(hw->mac[4]<<8) | hw->mac[5]` |
| `btc_base` | `read_shm(0x92) * 2`; if `0` skip all BTC writes |
| `btc_params%d` (i=0..118) | present ⇒ `write_shm(btc_base+2i, value)`; absent ⇒ skip |
| BTC `0x4352` extras | `0x7530/0x4e20/0x7530/0x753` at `btc_base+2/0x10/0x12/0x2c` |
| `btc_flags` | present ⇒ `btc+8=value` + MHF applier; absent ⇒ skip |
| `M_MAX_ANTCNT` | `0x000a` |

`hw->mac[6]`: from the external SPROM via `ob_si_read_mac()` (`mac_valid`), or an
equivalent validated board MAC.

## 16. GO decision

```
D3A1 IMPLEMENTATION GO: YES
```

Justification against the acceptance rule:

- vendor ordering and STOP boundary already proven (unchanged);
- every D3A1 write now has an exact constant, a runtime read from a named
  source, or a proven deterministic skip:
  - MAC six bytes ← `hw->mac` (SPROM/NVRAM `macaddr`);
  - SCR `0x24` ← skipped on the first init (gate `wlc_info+0x718`);
  - `btc_base` ← `read_shm(0x92)*2` (reproduce the runtime read);
  - `btc_params`/`btc_flags` ← absent ⇒ skip (vendor-identical);
  - `M_MAX_ANTCNT` ← constant `0x0a`;
  - `switch_macfreq` ← PMU-VCO-derived (`si_pmu_get_bb_vcofreq`) — unchanged
    from the accepted D3A1 boundary;
- all BCM4352 rev42 branches resolved; no value-unknown write remains;
- D3A0 DMA lifecycle is reused unchanged, in its vendor position;
- STOP stays before `sub_6656c`/bsinitvals/`wlc_phy_init`.

Conditions carried into implementation (not blockers):

1. Require `mac_valid`; abort (as the vendor does) if no validated MAC exists.
2. Reproduce the `btc_base != 0` branch by reading SHM `0x92` at runtime; if the
   project later gains a NVRAM text provider, `btc_params`/`btc_flags` become
   readable without any further RE.
3. The two SHM target names remain semantically UNKNOWN (value proven).

Standing constraints for this task only: analysis/docs; **no implementation, no
hardware, no `insmod`/`rmmod`/`modprobe`, no runtime source changes**, PR #9
stays **Draft**.
