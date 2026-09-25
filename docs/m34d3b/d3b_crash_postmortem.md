# M3.4D3B corrected hardware attempt — POST-MORTEM (platform-level crash)

**Status: `HARDWARE ATTEMPTED` / `NOT HARDWARE PROVEN` / `FAILED` — platform crash.**

No hardware, no `insmod`/`rmmod`/`modprobe`, no MMIO was performed while producing
this document. It is a pure read-only RE + source audit.

Tested candidate: PR #17 HEAD `2917ca9987ce9aedb58121b375e19208ebf1628c`
(runtime implementation `ffa2a548911007b58605df51150e24d422d6d957`), signed
`openbrcm.ko` sha256
`889710295f2e1c1c80088d333c244220c32127fda7cbc8644cca67c09ab2e1ed`.

Outcome: D2A/D2B/D3A1/DMA/T2/`switch_macfreq` and the D3B MHF + 73
`d11ac1bsinitvals42` writes all completed; ~4.84 s later every post-D3B MMIO
read returned `0xffffffff`; `ob_d3a1_validate`/`ob_d3b_validate` failed; the
driver entered `ob_d3a0_teardown()` (`dma-test: quiesce begin`) and the platform
then reset with `x86/amd: Previous system reset reason [0x08000800]: an
uncorrected error caused a data fabric sync flood event` (pstore empty).

---

## 1. Vendor execution immediately after the last bsinitvals write (Q1/Q2)

`sub_6656c(dev, chanspec, band)` (`0x6656c`, size 1490); `r12d = chanspec`,
`r13b = band` (`mov r13b,dl` at `0x66572`). The D3B initial-up call site is
`wlc_bmac_init 0x695d8 sub_6656c(dev, chanspec, band=0)`.

`re fn sub_6656c --asm`, the window after the applier call (`0x669bd`) and
before `wlc_phy_init` (`0x669df`):

```
000669bd  call sub_60f67                 ; bsinitvals42 applier (73 records)
000669c2  test r13b,r13b                 ; band
000669c5  je   0x669d0                    ; band==0 -> phy init
000669c7  cmp  dword ptr [rbx+84h],27h   ; (band!=0 path) phyrev
000669ce  ja   0x669e4                    ; rev>0x27 -> skip phy init
000669d0  mov  rax,[rbx+0E8h]
000669d7  movzx esi,r12w                 ; chanspec
000669db  mov  rdi,[rax+28h]             ; phy handle
000669df  call wlc_phy_init
```

Because the initial-up call passes `band=0`, control goes `0x669c5 -> 0x669d0`
and `wlc_phy_init` **is** called. Between the final table write and
`wlc_phy_init` the vendor performs **no** delay, MAC suspend/resume, PSM
handshake, SHM/object-window synchronization, core-clock transition, PCIe/core
readback, interrupt clear, posted-write flush, barrier or PHY-version read.
There is **no synchronization point**: the 73 writes are consumed by the
running PSM/ucode asynchronously while the host proceeds straight into the PHY.

## 2. The 73 `d11ac1bsinitvals42` records (execution semantics) (Q3)

Image: `/lib/firmware/brcm/bcm4352-d11ac1bsinitvals42.bin`, 592 B, sha256
`e81a645c79f55557c87f4662702d7c57599918c9b2340bc4e440ce1bdbd014da`, 73 records
+ terminator `0xffff` (not written). Re-derived directly from the image and
cross-checked against `docs/m34d3/bsinitvals_classification.json`.

Applier `sub_60f67` (`0x60f67`, size 103): stream of `{u16 off, u16 width, u32
val}`; `width==2 -> writew(D11+off)`, `width==4 -> writel(D11+off)`. No readback.

| class | count | offsets | notes |
| :--- | :--- | :--- | :--- |
| direct IHR (w2) | 5 | `0x680`, `0x682`, `0x684`, `0x686`, `0x700` | IFS/NAV timing |
| OBJADDR selector (w4) | 34 | `0x160` = `0x0001xxxx` | SHM window only (never SCR `0x20000`) |
| OBJDATA write (w2) | 34 | `0x164` (low) / `0x166` (high) | SHM words |

**No SCR record. No microcode/ucode instruction write. No PSM command register.
No DMA/IRQ/PHY/radio register.** The only "active" object is the shared-memory
state the PSM/ucode reads.

Direct IHR meanings (brcmsmac `d11.h`): `0x680 ifs_sifs_rx_tx_tx=0x3e3e`,
`0x682 ifs_sifs_nav_tx=0x23e`, `0x684 ifs_slot=0x212`, `0x686` = PAD/UNKNOWN
`0x9d0`, `0x700 nav_ctl=0x3c`.

SHM targets (byte offsets): `0x0c`, `0x10`, `0x1c`, `0x94`, `0x17d0`, `0x17d4`,
`0x990`, `0x99c`, `0x9a0`, `0x9a4`, `0x9b0`, `0x9b8`, `0x9c4`, `0x9cc`,
`0x9d8`, `0x9e0`, `0x9ec`, `0x9f4`, `0xa00`, `0xa08`, `0xa14`, `0xa1c`,
`0xa28` (each as low/high `0x164`/`0x166` halves; 3 bytes overridden vs the
common table: `0x10`, `0x1c`, `0x94`).

## 3. SHM consumers and asynchronous core/clock risk (Q4)

Upstream symbolic map (brcmsmac `d11.h`; b43 where applicable):

| SHM off | symbolic | written | relevance |
| :--- | :--- | :--- | :--- |
| `0x10` | `M_DOT11_SLOT` | `0x14` | MAC slot time |
| `0x1c` | `M_BCN_TXTSF_OFFSET` | `0x183` | beacon/TSF offset |
| `0x94` | `M_SYNTHPU_DLY` | `0x1f4` (=500) | **synth pre-wakeup delay** (clock/PLL wakeup timing) |
| `0x0c` | UNKNOWN (word 6) | `0x000a00c0` | PSM/ucode variable |
| `0x990..0xa28` | UNKNOWN (band/rate/power tables) | various | consumed by ucode; **not** in brcmsmac's named map |
| `0x17d0/0x17d4` | UNKNOWN (outside brcmsmac SHM 0x800-0xEFE) | `0x42b`/`0x100` | microcode-private / extended |

The state at the D3B slice is `MACCONTROL = 0x44020402` (`IHR_EN=1<<10`,
`PSM_RUN=1<<1`, `EN_MAC=0`) — the PSM is enabled and the ucode may run. The
vendor performs these writes with the ucode running too, but **only for the
microseconds until `wlc_phy_init`**; the PSM is expected to have a valid PHY by
the time it acts. `M_SYNTHPU_DLY` is the one value directly tied to a
clock/PLL (synth) wakeup decision. Whether any of these writes alone can drive
the ucode to change D11/core/clock state is **not proven**; the vendor never
synchronizes after them.

## 4. Final 10 records and state after record 73 (Q5)

| idx | off | w | val | selected SHM |
| :--- | :--- | :--- | :--- | :--- |
| 63 | `0x160` | 4 | `0x00010285` | select `0xa14` |
| 64 | `0x164` | 2 | `0x002c` | `0xa14` low |
| 65 | `0x160` | 4 | `0x00010285` | select `0xa14` |
| 66 | `0x166` | 2 | `0x0028` | `0xa14` high |
| 67 | `0x160` | 4 | `0x00010287` | select `0xa1c` |
| 68 | `0x166` | 2 | `0x0028` | `0xa1c` high |
| 69 | `0x160` | 4 | `0x0001028a` | select `0xa28` |
| 70 | `0x164` | 2 | `0x002c` | `0xa28` low |
| 71 | `0x160` | 4 | `0x0001028a` | select `0xa28` |
| 72 | `0x166` | 2 | `0x0028` | `0xa28` high |

Machine state immediately after record 73: `SHM[0xa14]=0x0028002c`,
`SHM[0xa1c]=xxxx0028`, `SHM[0xa28]=0x0028002c`; `OBJADDR` left on the SHM
window at/near word `0xa28` (auto-increment may advance it); the five direct IHR
values applied; `EN_MAC=0`, `PSM_RUN=1`. The **last physical write is
`D11+0x166 = 0x0028`** (OBJDATA high half). The host then begins read-back.

## 5. Vendor readback of the writes (Q6)

**None.** `sub_6656c` immediately enters `wlc_phy_init` (no reads of `0x160`,
`0x164`, `0x166`, `0x680..0x700`). This distinguishes:

- "write accepted by the CPU/MMIO bus" (the applier counted 73/39/34 and the
  bus fabric acknowledged), from
- "device remained accessible afterward" (never checked by the vendor at this
  point; the vendor checks device liveness elsewhere, see §8).

## 6. The ~4.84 s gap (Q7)

Between the logs `d3b-test: bsinitvals42 complete …` (`src/ob_d3b.c:163`) and
`d3b-test: post mhfs=…` (`src/ob_d3b.c:205`) the only code executed is
`ob_d3b_read_post()` (`src/ob_d3b.c:182-197`) followed by the start of
`ob_d3b_validate()`. `ob_d3b_read_post` has **no delay, loop, sleep or poll**:

```
5 x ob_ucode_read_shm16(mhf[i])      ; 0x5e,0x60,0x62,0x78,0xd4
3 x ob_d3b_read_shm32(0x10/0x1c/0x94) ; = 6 x ob_ucode_read_shm16
1 x bcma_read32(0x120 MACCONTROL)
1 x bcma_read32(0x12c MACINTMASK)
```

Each `ob_ucode_read_shm16` = `write32(0x160)` + `read32(0x160)` +
`read16(0x164+…)` = 3 MMIO. Total = 11×3 + 2 = **35 MMIO accesses**. There is
no software timer that could produce 4.84 s.

Therefore 4.84 s can only come from the bus/device side. Arithmetic:
`4.84 s / 35 ≈ 138 ms` per access — consistent with PCIe completion-timeout /
retry behaviour after the endpoint stopped completing reads (each read returns
`0xffffffff` once it times out). This is the leading explanation; a lower-bound
alternative is that the first ~4.8 s were spent in one long blocking access and
the rest returned immediately. Either way it is **bus/completion timeout, not an
intentional delay, not an OpenBRCM loop, not SHM-window software retry**.

## 7. Postcondition read path and the first bad access (Q8/Q9)

Access path (all through `bcma_host_pci_*` -> `ioread32/iowrite32` on
`bus->mmio`, no error conversion):

| value | path | physical access |
| :--- | :--- | :--- |
| MHF1..5 | `ob_ucode_read_shm16` | `write32(0x160, sel)`; `read32(0x160)`; `read16(0x164/0x166)` |
| SHM `0x10/0x1c/0x94` | `ob_d3b_read_shm32` -> 2× `ob_ucode_read_shm16` | as above |
| MACCONTROL | `bcma_read32(0x120)` | 1 read |
| MACINTMASK | `bcma_read32(0x12c)` | 1 read |

The **first physical MMIO access** in `ob_d3b_read_post` is the `write32(0x160)`
for MHF1; the **first access that can return `0xffffffff`** is the immediately
following `read32(0x160)` (OBJADDR readback). The logged `0xffffffff` for MHF
(via `0x164`) and for MACCONTROL/MACINTMASK means the whole D11 window returned
all-ones.

Can `0xffffffff` come from:

- **A. D11 core disabled/reset** — likely/possible. A core held in reset or
  clock-stopped returns all-ones across its window; every 0x18x/0x12x/objmem
  access returned all-ones simultaneously.
- **B. BCMA wrapper/core inaccessible** — possible (same observable as A; the
  wrapper maps the core window).
- **C. PCIe endpoint/link inaccessible** — likely/possible and best explains the
  ~138 ms/access and the subsequent fabric sync flood (link down -> completion
  timeout -> all-ones).
- **D. object/SHM window semantics** — rejected as the sole cause: MACCONTROL
  and MACINTMASK are direct registers, not SHM, and also read all-ones.
- **E. software sentinel/error conversion** — **rejected**: OpenBRCM and
  `bcma_host_pci_read32` do `ioread32` with no error-to-`0xffffffff` conversion
  (`drivers/bcma/host_pci.c`).

Conclusion: the D11 core / BCMA window / PCIe path became inaccessible; the
exact layer (core reset vs link down) is not yet distinguished.

## 8. Teardown after the failure (Q10) and vendor device-removed handling (Q11)

`ob_d3a0_teardown()` logs `dma-test: quiesce begin` and calls `ob_d3a0_quiesce()`.
First hardware access after that log:

```
bcma_read32(0x024 INTCONTROL0_MASK);            // RMW read  <-- FIRST access
bcma_write32(0x024, val_ffffffff & ~I_RI);      // = 0xfffeffff  (garbage write)
bcma_write32(0x12c MACINTMASK, 0);
ob_d3a0_rx_reset(): write32(0x220 RX_CONTROL,0); poll read32(0x220+STATUS0) ...
ob_d3a0_tx_reset(x4): write32(CONTROL,SE); poll STATUS0; write32(CONTROL,0); poll ...
(if any reset fails) ob_d3a0_core_contain(): bcma_core_disable -> bcma_core_wait_value(RESET_ST, ~0, 0, 300) + RESET_CTL/IOCTL writes
```

So the driver performs a **garbage RMW write to `0x24`** and then **DMA reset
writes/polls on an inaccessible core**. `bcma_core_wait_value(..., 300)` spins
with `udelay(10)+cpu_relax()` for up to 300 jiffies — a candidate for the
observed 100 % CPU before the reset.

Vendor contrast (`re` evidence): the vendor **always tests accessibility first**
and never touches the down/DMA path when the device is gone:

- `wlc_hw_deviceremoved` (`0x79847`): if clk present, `read32(D11+0x120
  maccontrol)`; removed iff `(val & 0x404) != 0x400`. On `0xffffffff` this is
  **removed=true** (identical to brcmsmac). Else `si_deviceremoved` (PCI config
  vendor id != `0x14e4`).
- `wlc_bmac_down_prep` (`0x66108`): calls `wlc_hw_deviceremoved` **first**
  (`0x66126`); if removed it skips `wl_intrsoff`/teardown writes entirely.
- `wlc_coredisable` (`0x6378d`): calls `wlc_hw_deviceremoved` **first**
  (`0x637a0`); if removed it skips `wlc_phy_switch_radio`, `wlc_phy_anacore`,
  clock and `si_core_disable`.
- `si_deviceremoved` (`0x1e50f`): PCI config vendor-id read.

OpenBRCM has **no** deviceremoved gate in `ob_d3a0_quiesce`/`ob_d3a0_teardown`.

## 9. Upstream corroboration (Q12)

`brcmsmac` (same generation) treats all-ones as device loss and aborts:

- `brcms_deviceremoved()` (`main.c:383`): `read32(maccontrol)`; removed iff
  `(macctrl & (MCTL_PSM_JMP_0|MCTL_IHR_EN)) != MCTL_IHR_EN` — i.e. `0xffffffff`
  is "removed".
- `ai_deviceremoved()` (`aiutils.c:692`): PCI config `PCI_VENDOR_ID` must be
  Broadcom; otherwise removed.
- `wlc_intstatus()` (`main.c:2487`): returns `-1` when `brcms_deviceremoved`,
  and returns `0` when `macintstatus == 0xffffffff` ("core still resetting") —
  it never proceeds blindly.
- `brcms_txstatus()` (`main.c:1004`): `s1 == 0xffffffff` -> `"dead chip"`,
  `*fatal = true`, return.

`bcma` itself does not convert MMIO errors (`host_pci.c` plain `ioread32`).

## 10. Should validation have recognised all-ones? (Q13/Q14)

Yes. The failed postcondition read set already contained a hard "device lost"
signal (`MACCONTROL=0xffffffff`), which is exactly `wlc_hw_deviceremoved`'s
removed condition and brcmsmac's "dead chip". OpenBRCM instead returned a
generic `-EIO` and proceeded into DMA quiesce.

**Designed fail-safe (NOT implemented; host-testable rule):**

> Once a trusted direct D11 register (MACCONTROL `0x120`, MACINTMASK `0x12c`,
> or any D11 register outside the SHM/OBJADDR semaphore) unexpectedly reads
> `0xffffffff`, set a module-wide `dev_lost` latch. While `dev_lost` is set, no
> D11 MMIO reset/quiesce write may be issued until accessibility is
> independently re-proven by `(read32(0x120) & 0x404) == 0x400` (the vendor
> `wlc_hw_deviceremoved` test) and, optionally, the PCI config vendor-id read.
> `ob_d3a0_teardown()` must then skip all DMA register writes and take the
> fatal/retain/no-free path.

Evaluation: had this rule been in force, the `0x24` RMW garbage write and the
RX/TX DMA reset writes on the inaccessible core would not have been issued; the
driver would have latched fatal and stopped. Whether those writes were the
*trigger* of the fabric flood is unknown, but the rule removes our contribution
and matches proven vendor/upstream behaviour. The rule is a pure predicate and
is host-testable; it is **not** to be implemented until the retest mechanism is
understood (Q19) and it must not weaken the existing fail-closed lifecycle.

## 11. D3A0 status split (Q15)

- **Normal accessible-device DMA lifecycle + teardown**: `HARDWARE RUNTIME
  PROVEN` (candidate `4fa1b57`, and re-exercised in the D3A1/D3B prefixes). These
  runs never observed all-ones; quiesce wrote only to a responsive core.
- **Teardown after device/MMIO loss**: **NOT PROVEN and now shown dangerous**
  (this event). The D3A0 proof does **not** cover an inaccessible core.

The fatal/free policy itself behaved conservatively (no free was reached;
`ob_d3a0_can_free` requires `free_allowed && engines_stopped && !fatal`), but
the *writes* that precede the fatal latch are the new hazard.

## 12. Causal timeline (Q16)

Only `137.594519` (BEGIN) and `143.668445` (quiesce begin) are given as exact
anchors; intermediate times are inferred/approximate and marked `~`.

| t (s) | event | D11 access |
| :--- | :--- | :--- |
| 137.594519 | `d3b-test: BEGIN` | — |
| ~137.6–138.2 | D2A upload (10850 writes), PSM poll (11 iters) | writes/read OK |
| ~138.2–138.5 | D2B 610 common initvals + postconditions | OK |
| ~138.5–138.8 | D3A1 T1, DMA 4×TX+FIFO0 RX, T2, `switch_macfreq` | OK |
| 138.8 (frozen run, before crash) | D3A1 postconditions PASS | **last known-good direct reads** |
| ~138.8 | D3B: pre-bs `0x3e0` read, MHF1..5 writes | OK |
| ~138.8 | `bsinitvals42 begin records=73` … `complete total=73 w16=39 w32=34` | last table write `0x166=0x0028` = **last known-good write** |
| ~138.83 → ~143.668 | `ob_d3b_read_post` (35 MMIO) blocks; every read returns `0xffffffff` | **first known-bad access** = `read32(0x160)` after the MHF1 selector write |
| 143.668445 | `d3b-test: validation FAIL ret=-5; tearing down`; `dma-test: quiesce begin` | — |
| >143.668445 | `read32(0x24)` (RMW) → write `0x24=0xfffeffff`; `0x12c=0`; RX/TX reset writes/polls; possibly `bcma_core_disable` spin | **dangerous writes on an inaccessible core** |
| shortly after | 100 % CPU / freeze / AMD data-fabric sync flood → reboot; no OpenBRCM line after `quiesce begin`; pstore empty | — |

`GAP(bsinitvals complete → bad reads) ≈ 4.84 s`; `GAP(BEGIN → quiesce begin) =
6.073926 s`.

## 13. Ranked root-cause hypotheses (Q17) — no certainty claimed

**H1 — PSM/ucode consumed band-init SHM with no PHY, destabilised the D11/PCIe
path (leading design hypothesis).**
- For: the vendor writes the same SHM but enters `wlc_phy_init` within µs; our
  test leaves the ucode with new band state and no PHY for ~4.84 s; `0x94 =
  M_SYNTHPU_DLY` is a clock/PLL pre-wakeup parameter; `PSM_RUN=1`.
- Against: not proven the ucode acts; EN_MAC=0; no vendor evidence that a
  missing PHY causes fabric faults.
- Needed to prove/refute: read `M_UCODE_DBGST` (SHM `0x40`) and D11 reset
  cause/`0x1e0` before/after; microcode trace or a test that programs
  `M_SYNTHPU_DLY` differently; a bounded experiment is required (not yet).

**H2 — The write itself (SHM `0x17d0`/`0x17d4` or a direct IHR) hit an
unmapped/undescribed object-memory location and wedged the core.**
- For: `0x17d0/0x17d4` lie outside brcmsmac's modelled SHM (`0x800-0xEFE`);
  direct IHR `0x686` is "PAD/UNKNOWN".
- Against: the vendor table writes them by design on this chip; the writes
  completed and counted.
- Needed: microcode SHM map / `OBJADDR` bounds; whether `0x17d0` aliases.

**H3 — The core/PCIe was already failing (thermal/power/SERDES) and the post
reads merely observed it; the quiesce writes then triggered the visible flood.**
- For: ~138 ms/access completion timeouts; AMD uncorrected-error sync flood;
  no OpenBRCM line after `quiesce begin`; no panic/pstore.
- Against: the same board passed D2B/D3A0/D3A1/D3B-prefix cleanly; no prior
  soft fault.
- Needed: AER/PCIe link-state and PMU/temperature telemetry from the crash;
  pstore is empty, so a future run needs a persistent console + `pcie` AER log.

**H4 — The dangerous quiesce writes on the inaccessible core caused the flood.**
- For: first action after `quiesce begin` is a garbage RMW to `0x24`, then DMA
  control writes + possible `bcma_core_disable`; the crash immediately follows;
  vendor/upstream explicitly never do this when removed.
- Against: a single MMIO write to an unreachable endpoint should be absorbed by
  the root complex, not cause a data-fabric flood; causality is unproven.
- Needed: a bounded, fenced retest with the fail-safe (abort on all-ones) that
  never writes after device loss — if the flood recurs with no writes, H4 is
  refuted.

**H5 — Rogue DMA from an engine left live while the core state was undefined.**
- For: 4 TX engines + FIFO0 RX were live; RX had 64 posted buffers; a corrupted
  ring pointer could DMA to a bad address.
- Against: TX had 0 descriptors, RX stayed IDLE at validation; the DMA base
  registers were valid; no rogue-DMA signature is recorded.
- Needed: IOMMU/AER fault records; DMA base/ring readback at the moment of loss.

## 14. Formal status (Q18)

| question | answer |
| :--- | :--- |
| D3B table writes reached? | **YES** — 73 records applied, counted `73/39/34`. |
| D3B table write loop completed? | **YES** — `bsinitvals42 complete`, terminator not written. |
| D3B postconditions proven? | **NO** — read-back returned `0xffffffff`; D3B itself never proved. |
| D11 MMIO accessibility after D3B? | **NO** — all post-D3B reads all-ones. |
| D3A0 teardown safe after MMIO loss? | **NO** — it wrote DMA reset registers on an inaccessible core. |
| Hardware retest GO? | **NO.** |

## 15. Retest gate (Q19)

`HARDWARE RETEST GO: NO.` A retest may only be considered once the all-ones
mechanism and the teardown interaction are understood well enough to bound the
risk. Minimum prerequisites:

1. Reproduce/attribute the all-ones layer (core-reset vs link-down vs endpoint
   failure) with persistent logging (netconsole/serial, not pstore) and AER.
2. Implement and host-test the §10 fail-safe: abort all D11 MMIO writes on the
   first trusted all-ones read; never enter DMA reset after device loss.
3. Prove the new STOP boundary cannot leave the PSM/ucode running with
   band-init SHM and no PHY for seconds (e.g., re-suspend PSM, or bound the
   read-back to a few accesses, or re-enter the vendor PHY path).
4. A quarantine/recovery story for the board and an explicit human go/no-go.

## 16. Scope / governance (Q20)

This document and the accompanying status updates are **evidence/docs only**. No
runtime or implementation change is made or merged. The frozen candidate/module
history (attempt `5fa5e5b`/`3a10aff9…`; corrected `2917ca9`/`88971029…`) is kept
intact. PR #17 stays Draft and unmerged.

### Tool-first evidence commands

```
scripts/re-bootstrap.sh
scripts/re.sh fn sub_6656c --asm
scripts/re.sh fn wlc_hw_deviceremoved --asm
scripts/re.sh fn wlc_bmac_down_prep --asm
scripts/re.sh fn wlc_coredisable --asm
scripts/re.sh fn si_deviceremoved --asm
scripts/re.sh card wlc_hw_deviceremoved
objdump -d wlc_hybrid.o_shipped > /tmp/blob_full.asm   # whole-blob immediate search (tooling gap)
python3 -c '<parse bcm4352-d11ac1bsinitvals42.bin>'    # 73 records, independent of re.db
```

Upstream corroboration (read-only): `drivers/net/wireless/broadcom/brcm80211/
brcmsmac/{main.c,aiutils.c,d11.h}` and `drivers/bcma/{host_pci.c,core.c}`.

### Tooling gaps recorded

- `re` cannot enumerate all references to an MMIO offset across the blob
  (whole-blob immediate search required `objdump`) — same gap as T7/T3.
- `re fn <fn> --asm` prints unresolved `? + 0x0` for some MMIO offsets
  (`wlc_bmac_init` `0x6906b`); the raw asm `lea rsi,[r12+18Ch]` is used.
