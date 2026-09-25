# M3.4D4 — `wlc_bmac_init` indirect ops-table dispatch resolved

Blocker: **`d4.indirect.wlc_bmac_init.vtable`** — the two rev42-reachable
indirect calls:

```
0x6923d: call qword ptr [rax+0A0h]
0x6924a: call qword ptr [rax+0D8h]
```

Status: **`ANALYSIS ONLY`**, **resolved** (no hardware, no MMIO, no driver
execution). Machine-readable packet:
[`indirect_ops_table_resolution.json`](indirect_ops_table_resolution.json).

## 1. Object at `*(wlc_hw + 0x20)`

`0x69236 mov rdi,[rbx+0x20]; 0x6923a mov rax,[rdi]` → `rax = *(*(wlc_hw+0x20))`.

- `wlc_hw + 0x20` is the **`di[0]` DMA-channel pointer**, written by
  `wlc_hw_set_di @0x796a7` (`mov [rdi+rsi*8+0x20],rdx`), called from
  `wlc_bmac_attach @0x6a458` with `rsi=0`, `rdx` = first `dma_attach` return.
- `di[0]` is allocated by **`dma_attach @0x10380`** (`osl_malloc(0x130)`), and
  is the **FIFO0 combined channel** (TX `TX_AC_BK_FIFO` + RX `RX_FIFO`), the
  same mapping as upstream `brcms_b_attach_dmapio` (`di[0]`, first FIFO).
- `dma_attach` stores the ops table into **object word 0**:
  `re refs dma64proc` → `dma_attach @0x1040b`, and `re fn dma_attach --asm`
  shows `0x10408 mov qword ptr [r13],<imm>` with relocation
  `R_X86_64_32S dma64proc` at `0x1040c`. So `*(di[0]) = di[0]->ops`.
- The table is selected by `(si_core_sflags >> 12) & 1 == SISF_DMA64 (0x1000)`:
  set → `dma64proc`; clear → the anonymous `.rodata+0x27d7b0` table. **Both
  tables carry the same `+0xa0`/`+0xd8` entries**, so the targets are
  unambiguous regardless. BCM4352 D11 is DMA64.

## 2. Table `dma64proc`

`.rodata+0x27d620`, size 400 B, 46 pointer slots, populated statically by
`R_X86_64_64` relocations in `.rela.rodata` (not runtime-patched).

| slot | target | symbolic | identity |
| :--- | :--- | :--- | :--- |
| `+0x08` | `.text@0xf947` | `sub_f947` | `dma64_txinit` (loop at `0x6921c`) |
| **`+0xa0`** | **`.text@0xf897`** | **`sub_f897`** | **`dma64_rxinit`** |
| **`+0xd8`** | **`.text@0xf14d`** | **`sub_f14d`** | **`dma64_rxfill`** |

`re ptrtable dma64proc` resolves every slot; `readelf -r` independently
confirms `00000027d6c0 R_X86_64_64 .text + f897` and
`00000027d6f8 R_X86_64_64 .text + f14d`.

## 3. Semantic confirmation (independent)

Modern `brcmsmac` `brcms_b_init` (`main.c:3289-3296`) has exactly:

```c
	for (i = 0; i < NFIFO; i++)
		if (wlc_hw->di[i])
			dma_txinit(wlc_hw->di[i]);
	dma_rxinit(wlc_hw->di[RX_FIFO]);
	dma_rxfill(wlc_hw->di[RX_FIFO]);   /* RX_FIFO == 0 */
```

The vendor sequence at `0x6921c` (loop, `ops+0x08`), `0x6923d` (`ops+0xa0`) and
`0x6924a` (`ops+0xd8`) is identical. `sub_f897` matches `dma_rxinit` (zero
`rxin`/`rxout`, clear the RX descriptor ring, `_dma_rxenable` /
`_dma_ddtable_init(dir=DMA_RX)`); `sub_f14d` matches `dma_rxfill` (`osl_pktget`,
`osl_dma_map`, post buffers, update RX `ptr`).

## 4. Reachability

Both calls are **unconditional** on the main `wlc_bmac_init` path, before the
`cmp dword ptr [rbx+84h],4` (`phyrev==4`) gate that guards the legacy `di[3]`
calls. BCM4352 rev42 (`phyrev=42`) reaches both `0x6923d` and `0x6924a`.
`di[0]` is non-null for rev42 (allocated by `wlc_bmac_attach`).

## 5. Result

- `0x6923d` → **`sub_f897` = `dma64_rxinit`** — **EXACT**
- `0x6924a` → **`sub_f14d` = `dma64_rxfill`** — **EXACT**

No unresolved provenance edge remains for the two call targets.

## 6. Tooling

- `re`: `refs dma64proc`, `fn dma_attach/wlc_hw_set_di/wlc_bmac_init --asm`,
  `ptrtable dma64proc`, `packet --fn wlc_bmac_init --indirect` — canonical.
- Ghidra (`Decompile.java sub_f897 sub_f14d`): **negative** — the ET_REL blob is
  under-segmented so the discovered functions are not resolvable there; `re`
  and the relocations are authoritative.
- Manual `readelf -r` used only as independent verification.
- **No tooling change required:** V5-G1 (multi-level pointer provenance) and
  V5-G2 (automatic call-site → runtime ops-table chain) remain filed as
  non-blocking gaps. The required facts are already obtainable from
  `re refs` (installer) + `re ptrtable` (slot targets), so no general fix was
  added merely for this case.
