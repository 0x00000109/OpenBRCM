# Milestones

Each milestone has a concrete, observable gate.

| M | Deliverable | Gate |
|---|---|---|
| **M0** | `bcma` probe, ChipCommon window, CHIPID | module loads; dmesg shows `chip 0x4352` |
| **M1** | `ob_si` bring-up (PMU/PLL/clock), external SPROM read | dmesg shows the validated SPROM rev/CRC |
| **M2** | mac80211 registration; wiphy bands/channels/rates | `iw phy` lists the 2.4 GHz HT 2×2 band |
| **M3** | dma64 rings, NAPI, `ieee80211_rx` | interface up; RX/TX counters move |
| **M4** | `.config`, `.bss_info_changed`, `.set_key`, scan | `iw scan`, association, encrypted link |
| **M5** | AMPDU, power save, runtime PM, LED/rfkill | stable traffic; suspend/resume |
| **M6** | second PHY family (nphy/htphy) | family module loads |

## M2.5b — eliminate the BCM4352 power-up Oops (historical)
Symptom: `BUG: kernel NULL pointer dereference, address 0x…0c` at
`bcma_core_pci_power_save+0x25` (`RAX=0`), called from `ob_si_powerup`.
- **Root cause:** `bcma_core_pci_power_save()` only guards
  `bus->hosttype != BCMA_HOSTTYPE_PCI`. On a PCIe **Gen2** bus `hosttype` is
  still `BCMA_HOSTTYPE_PCI`, but the legacy per-core state
  **`bus->drv_pci[0].core` is NULL** (Gen2 uses `bus->drv_pcie2`), so
  `pc->core->id.rev` faults at offset `0xc`.
- **Fix:** the legacy call is **removed**. Host bring-up now uses the public
  host abstraction **`bcma_host_pci_up(bus)`**, which checks `hosttype` and
  dispatches on `bus->host_is_pcie2` to `bcma_core_pcie2_up()` — the correct
  path for this bus (exported GPL; declared in `include/linux/bcma/bcma.h`).
- **Instrumentation:** read-only topology dump (`hosttype`, `host_is_pcie2`,
  `host_pci`, `drv_pci[0/1].core`, `drv_pcie2.core`, d11 `core_index/addr/wrap`)
  and separately labelled stages A (host up), B (d11 enable/reset), C (HT clock),
  D (OTP/SPROM). Bounded polling only.
- This does **not** claim to fix the SPROM shadow; that is a separate item.

## M2.5c — OTP diagnostic probe (historical; SUPERSEDED by M2.5f/g — OTP not used for the MAC)
Two earlier claims were **wrong** and are corrected here: "`si_pmu_otp_power` is a
no-op for 0x4352" (it is not), and "the OTP divisor comes from `readl(CC+0x10)`"
(that register is `BCMA_CC_OTPS`, OTP **status**, not geometry).

- **`si_pmu_otp_power` (0x12a8d), case 0x4352 → 0x12cb2**: mask `0x100`; power on
  = RMW `CC+0x618 |= 0x100`, `delay(1ms)`, bounded poll `CC+0x60c` bit `0x100`;
  power off = clear the bit. `si_pmu_is_otp_powered` checks `CC+0x60c` bit 8.
- **`srom_read` false branch** (`0x9284`) calls
  **`otp_read_region(sih, 1, buf, &size)` with `size = 0x180 = 384` words** —
  matching the 96×64×384 geometry. `otp_read_region` (0x33ff) calls `otp_init`
  then `ops->read_region`.
- **`otp_init` (0x329f)** selects one of two ops tables by `sih+0x14`:
  table A (`.data+0x60`, `>0x16` or `==0x15`) and table B (`.data+0xd0`,
  `==0x16` or `<=0x14`). **Table B's init (0x3bfe) derives the geometry from the
  ChipCommon CAP selector**: `sel = (readl(CC+0x4) & 0x380000) >> 19`, aborting
  if zero. This is the authoritative, non-circular source. (Table A's big inlined
  init at 0x3d6a instead seeds a divisor from `CC+0x10` — the legacy arm our
  earlier code mistakenly copied.)
- **Geometry (recovered):** the blob's per-chip switch (0x3d6a, OTPP[18:16]==0
  arm) writes, for selector 5, `rows=0x60(96)`, `cols=0x40(64)`,
  `words=0x180(384)` (0x3edf..0x3ef1); selectors 1/2/7 give 32×64, 64×64, 16×64.
  This **matches the historical Broadcom IPX OTP table** (independent
  corroboration). BCM4352 `CC_CAP=0x58680001` → selector 5.
- **OTP bit reader (0x38d3)**: `OTPC(0x14)=0`, `OTP_CFG(0xf4)=0`, read `OTPP(0x1c)`,
  `row = bitaddr/cols`, `col = bitaddr%cols`,
  `OTPD(0x18) = 0x80000000 | (row<<8) | (col&0xff)`; poll while bit31 (bounded),
  `READERR` = bit28, data = bit29. Writes only OTP control registers — never a
  programming-enable bit.
- **OTP init command** (blob 0x3fa3, non-OTPP[18:16]==1 arm): `OTPD=0x84000000`,
  poll while bit31 (bounded), issued before the first read.
- OTP power was already on at runtime (`was_powered=1`), so power is not the
  blocker; the traditional `0x840` shadow stays `ffffffff`, so the direct OTP
  read is the primary path.

`ob_si_otp_diag()` (opt-out via `otp_diag=0`) logs the before state, powers OTP,
re-reads the `0x840` shadow, derives the geometry from the CAP selector (no
circular word read), issues the init command, reads OTP words `0x26..0x28` with
`divisor=cols`, logs per-bit failure detail (bit offset, row, col, OTPD command,
OTPP/OTPD values, BUSY/READERR/VALUE/timeout), validates both byte orders with
`is_valid_ether_addr`, and does **not** replace the mac80211 address.

## M2.5a — instrumented power-up (previous)
Goal: obtain the real factory MAC and understand the HT capability value.
- **Implemented (provenance-backed):** instrumented power-up in `ob_si_powerup()`
  logging initial PMU/clock/OTP/SPROM state; PCIe/core wake + core reset via
  **bcma** (`bcma_core_pci_power_save`, `bcma_core_enable` — the kernel
  implementation of the recovered `ai_core_reset`); HT clock via
  `bcma_core_set_clockmode` (FORCEHT + HAVEHT poll); bounded shadow polling
  (500 ms, no infinite waits); raw MAC words + decoded MAC.
- **HT fix:** `rx_highest` corrected from 144 to **300 Mbps** — derived from our
  recovered formula (MCS7 × 2 streams × 40 MHz × SGI), consistent with the
  advertised MCS 0-15 / 2 streams / HT40. (Runtime previously reported 144.)
- **UNKNOWN / blocker:** the exact BCM4352 ChipCommon **OTP read FSM**
  (`otp_read_word` variants) and the 0x4352-specific PLL branch are not fully
  recovered. No guessed register writes are performed. See the project rule:
  "if a required bit/sequence is not provenance-backed, stop and mark UNKNOWN".
  Consequence: if the SPROM shadow is not populated by the chip's own power-on
  reset, the MAC is unavailable and remains `00:00:00:00:00:00`.

## Current state — M2.5 COMPLETE (runtime-proven)
On the real ASUS PCE-AC56 / BCM4352: bcma bind, PCIe2 host bring-up, D11 core
enable, FAST clock/HAVEHT, and mac80211 registration all work.
- The authoritative board-data source is the **external SPROM**; OTP is **not
  required** for the MAC on this board (`SROM_CONTROL` selects external,
  `OTPS.GU_PROG_HW=0`).
- External SPROM validates at **234 words, rev 11, CRC stored=calculated=0xc0**.
- Rev11 `IL0MAC` offset is **+0x90** (mainline bcma wrongly applies rev8 +0x8C).
- Permanent MAC = **2c:fd:a1:61:40:25**, exposed by both `iw dev` and
  `/sys/class/net/wlp33s0b1/address`.
- On-chip OTP is reachable only via the opt-in `otp_diag` (default off).
Next: **M3**, staged — M3.1 (DMA architecture report), M3.2 (software-side ring
allocation, runtime-validated) and M3.3 (interrupt infrastructure) are done;
M3.4 (RX enable) awaits M3.3 runtime validation.

## M2.5f — board-data source resolved: external SPROM
Runtime `SROM_CONTROL=0x23` (PRESENT|SIZE_4K|OTP_PRESENT, OTPSEL=0) and
`OTPS=0x9400` (GU_PROG_HW clear). Proven from the blob:
- `si_is_sprom_available()` (0x1f509) for ccrev>30 returns
  `cccaps & 0x40000000 ? (SROM_CONTROL & PRESENT) : 0`; true here.
- `srom_read()` region 1 calls the **external SPROM** reader (0x9142) when that
  flag is true and only uses the OTP fallback (0x9284) when it is false. So the
  blob does **not** fall back to OTP on this board.
- External SPROM/shadow base is **CC+0x800**; 0x840 is the OTP general-use-region
  mapping from OTPL, not the SPROM base.
This supersedes M2.5c/M2.5d direct-OTP attempts for the MAC. `otp_diag` is now
**off by default**; a read-only `sprom_diag` (16-bit reads exactly like
`bcma_sprom_read()`, with bcma CRC8 + revision 8..11 validation) is on and logs
`bus->sprom` that bcma already parsed.

## M2.5g — real MAC obtained and installed (runtime-verified)
External SPROM validates at **234 words, rev 11, CRC stored=calculated=0xc0**
(raw `2cfd a161 4025` at 0x90). bcma's `bus->sprom.il0mac` is malformed
(`00:00:00:00:2c:fd`) because mainline bcma has only `bcma_sprom_extract_r8()`
and applies rev8 `IL0MAC=+0x8C` to rev11 data; rev11 puts the first MAC at
**+0x90**. OpenBRCM:
- validates CRC + revision, then selects the offset by revision (rev8 +0x8C,
  rev11 +0x90) in `ob_si_read_mac()`;
- installs the validated MAC via `SET_IEEE80211_PERM_ADDR()` before
  `ieee80211_register_hw()`, never using `bus->sprom.il0mac`.
Runtime-verified: `iw dev` and `/sys/class/net/wlp33s0b1/address` both show
**2c:fd:a1:61:40:25** (OUI ASUSTeK). OTP remains untouched. M3 not started.

## M3.1 — DMA architecture (report)

Recovered the BCM4352 / D11 rev42 DMA64 model; see `docs/dma_architecture.md`.
No register writes. Key conclusions: 64-bit dma64 engine; FIFO map for
`corerev > 10` (TX0 0x200 / RX0 0x220 / TX1 0x240 / TX2 0x280 / TX3 0x2C0,
stride 0x40; RX ring = FIFO0, management TX = FIFO3); per-channel registers
`control 0x00 / ptr 0x04 / addrlow 0x08 / addrhigh 0x0C / status0 0x10 /
status1 0x14` (corrects the earlier "STATUS=0x04" artifact); 16-byte descriptor
`{ctrl1, ctrl2, addrlow, addrhigh}`; 8 KiB ring alignment; producer/consumer via
`ptr` + `status0`; D11 `macintstatus` 0x128 / `macintmask` 0x12C
(`I_RI`=1<<16, `I_XI`=1<<24); 38-byte hardware RX header.

## M3.2 — DMA ring allocation (software model only)

Implemented `src/ob_dma.{h,c}`. Software only: no D11 DMA register is touched,
no ring base is published, no engine is enabled and no IRQ is taken.
- Validates the real device capability with
  `dma_set_mask_and_coherent(core->dma_dev, DMA_BIT_MASK(64))` and fails probe
  cleanly (no silent DMA32 fallback) if 64-bit DMA is not accepted.
- Allocates one RX and one TX/control ring (future FIFO0 RX @0x220 / FIFO3 TX
  @0x2c0) from a `dma_pool` of 8 KiB blocks with 8 KiB alignment/boundary, and
  validates the recovered 8 KiB constraint on the **DMA address** only. The CPU
  virtual address is unrelated to the hardware and is only required to meet the
  natural alignment of `struct ob_dma_desc` for safe CPU access.
- Descriptor = 16 bytes `{ctrl1, ctrl2, addrlow, addrhigh}` with explicit
  masks/shifts (no bitfields); rings = 512 descriptors = 8192 bytes.
- Separate RX/TX index and per-slot ownership metadata (skb/dma/mapped) so a
  later mapping is unmapped and freed exactly once.
- Full unwind on every failure stage and in `ob_remove()`; no hardware reset is
  needed because DMA was never enabled.
- Host + KUnit tests cover descriptor encoding, ring arithmetic/wraparound,
  index bounds, descriptor offsets/aliasing and the EOT helper.

## M3.3 — interrupt infrastructure

Implemented `src/ob_irq.{h,c}`. Establishes and proves a safe IRQ path without
enabling any source, DMA or frame processing.
- **Transport:** the IRQ is the one `bcma` already recorded for the D11 core
  (`core->irq == bus->host_pci->irq`); OpenBRCM calls no `pci_alloc_irq_vectors()`
  and does not touch the PCI IRQ routing. Mode is reported from real PCI state
  (`pci_dev->msix_enabled`/`msi_enabled`, else INTx).
- **Registers:** `MACINTSTATUS=0x128`, `MACINTMASK=0x12C`, per-FIFO
  `intctrlregs` at 0x20. Corrects the M3.1 draft: `I_RI`/`I_XI` are per-FIFO
  bits, not `macintstatus` bits (see `docs/dma_architecture.md` §7).
- **Owned bits:** `MI_DMAINT` only; `OB_D11_IRQ_OWNED_MASK ⊆ KNOWN_MASK`, so the
  handler never acks an unproven bit. Owned sources are RMW-masked (unrelated
  bits preserved) before `request_irq`.
- **Handler:** read `MACINTSTATUS`; treat `0xffffffff` as invalid; if no owned
  bit pending return `IRQ_NONE`; otherwise rate-limited log, write back only the
  owned pending bits (write-1-to-clear), return `IRQ_HANDLED`. No loops, no
  sleeps, no allocation.
- **Teardown order:** mask owned bits → clear the handler gate →
  `synchronize_irq()` → `free_irq()` → log counters. `ob_remove()`:
  mac80211 → IRQ → DMA.
- Host + KUnit tests cover owned-mask subset, status validity, pending/ack,
  IRQ_NONE/HANDLED decisions, unknown-bit preservation and RMW masking.

## Previously
- M0/M1 code present (`src/ob_main.c`, `src/ob_core.c`, `src/ob_si.c`).
- **M2 present:** `src/ob_mac80211.c` registers with mac80211 and exposes the
  **2.4 GHz** band (channels, legacy rates, HT 20/40 2×2). Interface bring-up is
  intentionally disabled (`.start` returns `-EOPNOTSUPP`) until M3.
- Pure math present and tested (`ob_channel.c`, `ob_rate.c`; host + KUnit).

## M2 blockers / deliberately not exposed
- **5 GHz band is NOT advertised.** The exact BCM4352/acphy 5 GHz channel table
  has not been recovered/validated from the blob. The internal architecture is
  ready (`ob_channel` supports 5 GHz), but we do not advertise guessed
  capabilities. **TODO/blocker:** recover and validate the BCM4352/acphy 5 GHz
  channel list (and DFS/NO-IR flags) before enabling `NL80211_BAND_5GHZ`.
- **VHT / 80 MHz, LDPC, MAX_AMSDU, >2 spatial streams** are not advertised
  (no provenance yet; VHT depends on 5 GHz).


## Mapping to the RE work
- M1 ← RE Stages 4–5 (MMIO map, sequences/values).
- M2 ← RE Stage 6 (radio parameters).
- M3 ← RE Stage 6 (DMA) and Stage 8 (data-path flows).
- M4 ← RE Stages 7–8 (contract/dispatch, control flows, mac80211 mapping).
- M6 ← RE Stage 3 (pluggable PHY ops).
