# REV42-GAP-ANALYSIS — BCM4352 / D11 corerev 42 bring-up gap analysis

> ## ⛔ HISTORICAL ONLY — NOT A CURRENT PROCEDURE, NOT A CURRENT FACT
>
> **Canonical replacement for every address, mode and status claim in this
> file:**
>
> | topic | canonical source |
> | :--- | :--- |
> | D11 address model, `core->addr`, `core->wrap`, superseded models | [`docs/m34d4/addr_provenance.md`](docs/m34d4/addr_provenance.md) |
> | the RUN 1 measurement and `0x0FFC` semantics | [`docs/m34d4/d11_addr_u1_measured.md`](docs/m34d4/d11_addr_u1_measured.md) |
> | the RUN 2 execution and the core-enable proof | [`docs/m34d4/rev42_ai_enable_freeze.md`](docs/m34d4/rev42_ai_enable_freeze.md) |
> | the five rev42 probe modes and their per-mode hardware status | [`docs/m34d4/rev42_diag_modes.md`](docs/m34d4/rev42_diag_modes.md) |
> | live project state and STOP boundary | [`docs/agent-state.md`](docs/agent-state.md) |
>
> **What changed after this file was written (2026-09-27 M3.4D reconciliation):**
>
> - `core->addr` (D11 core functional) was **measured** as **`0x18001000`** and
>   `core->wrap` (BCMA wrapper) as **`0x18101000`**. These are
>   `PROVEN_HARDWARE` (RUN 1, marker `openbrcm-test-20260927-014301-8836`).
>   Any statement here that a window base is "UNKNOWN", or that proposes
>   `0x18000000` / `0x18001000` / `0x18100000` as the base, is **SUPERSEDED**.
> - The `+0x1000` (ROOT+slot) and "two-root" relations are **RETRACTED**. The two
>   window addresses are independent eROM-derived values in two independent PCI
>   config registers (`0x80` and `0x70` on PCIe2); no arithmetic relates them.
> - **No positive role is claimed for `0x18000000`** — specifically, it is *not*
>   claimed to be ChipCommon. The only canonical statements about it are
>   negative.
> - Raw `PCI_CFG_WRITE` of `0x80` / `0x70` / `0xAC` is **withdrawn**. The driver
>   performs no window-register write; BCMA owns the window switch.
> - `AI_WRAPPER_UNREACHABLE` and any "all-ones proves a wrong window" rule are
>   **RETRACTED**. All-ones is an observation only.
> - `0x0FFC` is a core-window **probe offset of UNKNOWN register identity**. It
>   is **not** a readability discriminator, **not** a liveness discriminator and
>   **not** evidence of a wrong window. Hardware measured `0xffffffff` there both
>   with the core down (RUN 1) and with `ai_iscoreup = 1` (RUN 2, marker
>   `openbrcm-test-20260927-100501-6116`) — which is exactly why it discriminates
>   nothing.
> - `rev42_ai_probe_only` (RUN 1) and `rev42_ai_enable_only` (RUN 2) have each
>   been executed **once**. Both candidates are **spent**; neither is to be
>   re-run merely for confirmation, and this file authorises no execution.
> - **The `HAVEHT=1` / "CLKCTLST bit17 CONFIRMED" claims below are RETRACTED
>   (2026-09-27, M3.4E `ANALYSIS ONLY`).** `0x01E0` is **two registers in two
>   windows**: `CLKCTLST` (`FORCEHT` bit1, `HAVEHT` bit17) is the **ChipCommon**
>   register; the **D11-window** `0x1e0` is a different register of `UNKNOWN`
>   identity, vendor-written with bit1 (`sub_64887`, mac clock enable), bit5
>   (`wlc_bmac_radio_hw`) and bits 8|9 (`wlc_bmac_core_phypll_ctl`) — never
>   bit17. The `clkctlst=070b0042` figure was read from the **D11** window
>   (`artifacts/rev42_probe_trace.json` seq 3, `block: "D11"`, `offset: 480`), so
>   it is **not** a `HAVEHT` observation. The raw value is
>   `PROVEN_HARDWARE` and unchanged; only the conclusion is retracted. Evidence:
>   [`docs/m34d4/m34e_d11_mac_accessibility.md`](docs/m34d4/m34e_d11_mac_accessibility.md).
>
> The run records and analysis **inside this file are kept verbatim** as
> historical evidence. Nothing here should be read as a current fact or as an
> instruction to touch hardware.


Status: **ANALYSIS ONLY. No driver code modified. No hardware executed.**
Target: Broadcom BCM4352 PCIe, chip rev 3, D11 core `0x812` rev `0x2A` (42), AC class.
Authoritative firmware handoff: `/media/kartashoff/Storage/mrbin/FIRMWARE-TO-DRIVER-HANDOFF.md`
(+ `artifacts/firmware_handoff/`).

Evidence vocabulary (this document):

- **FACT** — corroborated by two independent sources or one authoritative + gate.
- **HISTORICAL REFERENCE** — true of the old brcmsmac corerev 23/24 path; not a rev42 result.
- **HYPOTHESIS** — plausible, not proven.
- **UNKNOWN** — not recovered; do not fill with a guess.

Confidence tags used for the handoff: CONFIRMED, STRONGLY_SUPPORTED, PROBABLE,
POSSIBLE, UNKNOWN, DISPROVED.

---

## 0. Handoff reconciliation (read first)

The handoff was produced against **OpenBRCM `main` @ `06d60d6`** ("87 commits",
cloned to `/tmp/OpenBRCM`). The working tree here is branch
`m34d3b-band-init-test` @ `ab5d37c`, **31 commits ahead of `origin/main`**, and it
contains the D3A0/D3A1/D3B/D4 vendor-blob reverse-engineering chain that the
handoff did not see. Consequences:

| Handoff statement | Reconciliation |
|---|---|
| "The old brcmsmac firmware path is NOT a BCM4352/rev42 implementation" | **FACT — confirmed and central.** This branch already treats the 15-region `bcm43xx-0.fw`/`bcm43xx_hdr-0.fw` package as corerev 23/24-only and never loads it (`src/ob_fw.h` uses `brcm/bcm4352-d11ucode42.bin` + `brcm/bcm43xx-ucode.fw`). `grep` confirms 0 references to the 15-region package in `src/`. |
| "rev42 evidence is UNKNOWN / no rev42 reference driver" | **Partially superseded.** No *upstream/vendor source* rev42 reference exists, but this branch has an authoritative **proprietary vendor blob** (`wlc_hybrid.o_shipped`, sha256 `352a6e349f…`) reverse-engineered with the deterministic `re`/`re.db` index and Ghidra. The real rev42 path (`wlc_bmac_init` → `sub_6656c` → `wlc_phy_init` → cal) is recovered in `docs/m34d3*.md`, `docs/m34d4*.md`. |
| "Initval record is `{u16 offset;u16 width;u32 value}`" | **FACT.** `src/ob_fw.h:47-56` parses exactly this, terminator `0xffff` last, `offset ≤ 0xfff`, `width ∈ {2,4}`; applied as `bcma_write16/32(core, offset, value)` (`src/ob_initvals.c:32-38`). |
| "OpenBRCM's ucode upload matches brcmsmac; word-0 divergence DISPROVED" | **FACT + independently corroborated.** `re fn 0x60744` (vendor ucode applier) writes `*(a0+0xd0)+0x160 = 0x3000000` then streams `*(a1+0x0)` to `+0x164`. The vendor rev42 blob itself uses the same `OBJADDR=AUTO_INC|UCM_SEL` + `OBJDATA` stream as brcmsmac and OpenBRCM. No OpenBRCM-specific defect. |
| "OpenBRCM ucode upload is a faithful port of `brcms_ucode_write`" | **FACT**, and the vendor blob corroborates it, so it is generic D11 object-memory semantics (not brcmsmac-specific). |
| "The 15-region package is corerev 23/24" | **FACT**, matches this branch. |
| "OpenBRCM commit 06d60d6" | The handoff did not see D3A0 (HW PROVEN), D3A1 (HW PROVEN), D3B (HW ATTEMPTED → platform crash), or the D4 reachability work. Those are the current failure boundary (§9). |

**Net:** the handoff's architectural warning is correct and already implemented;
the handoff's "rev42 unknown" is **superseded** by this branch's vendor-blob RE;
the handoff's firmware-format corrections are consistent with the code.

---

## 1. Current OpenBRCM bring-up sequence

Source of truth: `src/ob_core.c`, `src/ob_si.c`, `src/ob_fw.c`, `src/ob_dma.c`,
`src/ob_irq.c`, `src/ob_rx.c`, `src/ob_mac80211.c`. Entry point `ob_probe()`
(`src/ob_core.c:146`), registered as the bcma driver probe.

```
ob_probe(core)
├─ core identity gate: manuf==BCM && id==BCMA_CORE_80211 (0x812)     [CONFIRMED]
├─ ob_isolated_mode_select7(...)  -> isolated modes; else normal path
│
├─ ISOLATED MODES (bypass normal bring-up; never reach mac80211):
│   fw_validate_only        : request_firmware + size/FNV validate + dry-run trace only
│   ucode_test_only   (D2A) : ob_ucode_run_d2a()  [HARDWARE PROVEN]
│   initvals_test_only(D2B) : ob_ucode_run_d2a + 610 common initvals  [HARDWARE PROVEN]
│   dma_test_only     (D3A0): D2B + pinned IRQ-source prereqs + 4 TX + FIFO0 RX DMA
│                            lifecycle + verified quiesce  [HARDWARE PROVEN]
│   d11_tail_test_only(D3A1): D2A/D2B + vendor tail sub_67efd->T1->DMA->T2->
│                            switch_macfreq, STOP before sub_6656c  [HARDWARE PROVEN]
│   bsinitvals_test_only(D3B): D3A1 prefix (DMA live) + MHF1..5 + d11ac1bsinitvals42
│                            (73 rec) + postcheck + teardown  [HW ATTEMPTED → CRASH]
│   radio_id_probe_only     : min core prep + 2 radio reg0/1 reads  [FROZEN, NOT RUN]
│   sprom_evidence_only     : read-only 234-word rev11 SPROM capture [HARDWARE PROVEN]
│
└─ NORMAL PATH (production):
    ob_si_probe(core)
    ├─ hw->cc = bus->drv_cc.core                          [ChipCommon window]
    └─ ob_si_powerup()
       ├─ Stage A: bcma_host_pci_up(bus)                  [PCIe2 host up]
       ├─ Stage B: bcma_core_enable(D11) if !enabled      [D11 core enable/reset]
       ├─ Stage C: bcma_core_set_clockmode(D11, FAST)     [FAST/HT clock]
       └─ board data from external SPROM (no PMU PLL writes for rev42)
    ├─ ob_si_d11_diag()          : read-only MACCONTROL/…/phyversion dump
    ├─ ob_si_dump_bus_sprom()    : bcma-parsed SPROM (logged, NOT used for MAC)
    ├─ ob_si_read_mac()          : rev11 external SPROM, CRC8 + rev validate -> hw->mac
    ├─ sprom_diag                : read-only full SPROM dump (+ rev11 board fields)
    ├─ ob_fw_probe()             : request_firmware(ucode42/initvals42/bsinitvals42),
    │                              exact size + FNV-1a-64 + shape validate, DRY-RUN
    │                              trace ONLY.  <-- NO UPLOAD
    ├─ ob_dma_init()             : 32-bit DMA mask; 8 KiB-aligned dma_pool rings
    │                              RX(256 desc) + TX(512 desc) allocated (SW only,
    │                              no D11 DMA register touched)
    ├─ ob_irq_init()             : request_irq(shared); mask owned sources (RMW);
    │                              handler installed; NO source enabled
    ├─ ob_rx_init()              : map 64 RX buffers, program FIFO0 RX (0x220)
    │                              ring+CONTROL=0x84d+PTR=0x400, enable host IRQ
    │                              route + FIFO I_RI + MACINTMASK MI_DMAINT
    └─ ob_mac80211_register()    : 2.4 GHz HT 2x2, ieee80211_register_hw()
                                  start() -> -EOPNOTSUPP; tx() -> drops frame
```

**Hard fact:** the production path performs **no ucode upload, no common
initvals, no bsinitvals, no post-common D11 tail, no `wlc_phy_init`/PHY/radio,
no channel, no `EN_MAC`, and no TX**. `ucode_test_only`/`initvals_test_only`/
`d11_tail_test_only`/`bsinitvals_test_only` are **isolated tests**, not wired
into `ob_probe`'s normal chain.

Stage detail table:

| # | Stage | Function(s) | File | HW block | Registers | Expected postcondition | Known postcondition | Conf |
|---|---|---|---|---|---|---|---|---|
| 1 | bcma bind / core discovery | `ob_probe` | ob_core.c:146 | backplane | — | D11 0x812 rev42 bound | proven | CONFIRMED |
| 2 | ChipCommon window | `ob_si_probe` | ob_si.c:942 | CC/PMU/SPROM | CC+0x0/0x4/0x190/0x800 | CC present | proven | CONFIRMED |
| 3 | PCIe2 host up | `bcma_host_pci_up` | bcma | PCIe2 | PCIe2 CFG | host up | proven | CONFIRMED |
| 4 | D11 core enable/reset | `bcma_core_enable` | bcma | D11 | core ctrl/cflags | core enabled | proven | CONFIRMED |
| 5 | FAST clock | `bcma_core_set_clockmode` | bcma | ~~D11~~ **ChipCommon** | CLKCTLST bit17 | HAVEHT=1 | proven | CONFIRMED — **window CORRECTED 2026-09-27, see note below** |
| 6 | PMU/PLL (board) | *(none for rev42)* | — | PMU | — | vendor `si_pmu_*` not run | **absent** | DIVERGENCE |
| 7 | OTP / SPROM | `ob_si_read_mac` | ob_si.c:135 | CC+0x800 | SPROM words | rev11 CRC valid, MAC | proven | CONFIRMED |
| 8 | D11 object memory / ucode | *(none in production)* | — | D11+0x160/0x164 | OBJADDR/OBJDATA | PSM suspended | **absent** (isolated only) | MISSING |
| 9 | Common initvals | *(none in production)* | ob_initvals.c (isolated) | D11 0x000..0xfff | 610 records | FIFO0..3=01c4/0/0/079e | absent | MISSING |
| 10 | bsinitvals / band init | *(none in production)* | ob_d3b.c (isolated) | D11 SHM/IHR | 73 records | overrides 0x10/0x1c/0x94 | absent | MISSING |
| 11 | Post-common D11 tail | *(none in production)* | ob_d3a1.c (isolated) | D11/TXE0/DMA/IRQ | 0x100/0x120/0x128/0x24/0x54x/0x62e… | MACCONTROL=0x44020402 | absent | MISSING |
| 12 | PHY/radio init | *(none)* | — | PHY/radio windows | 0x3d8/0x3da/0x3e0/0x3fc/0x3fe | radio ON, PHY init | **absent** | MISSING |
| 13 | Calibration | *(none)* | — | PHY/radio | — | `wlc_phy_cals_acphy` | absent | MISSING |
| 14 | Channel/chanspec | *(none)* | — | PHY/radio | — | channel programmed | absent | MISSING |
| 15 | DMA rings | `ob_dma_init` (SW) + `ob_rx_init` (FIFO0) | ob_dma.c, ob_rx.c | D11 0x200..0x2ff | CONTROL/PTR/ADDR | RX IDLE | **RX armed before ucode/PHY** | DIVERGENCE |
| 16 | Interrupts | `ob_irq_init`+`ob_rx_enable_irq` | ob_irq.c, ob_rx.c | D11 IRQ masks | 0x24/0x12c/0x128 | host route off until up | **host route + MI_DMAINT enabled early** | DIVERGENCE |
| 17 | `EN_MAC` | *(none)* | — | D11+0x120 | EN_MAC bit0 | EN_MAC=1 after PHY | **absent** | MISSING |
| 18 | TX path | *(none; `ob_ops_tx` drops)* | ob_mac80211.c:116 | D11 TX | — | first TX frame | **absent** | MISSING |
| 19 | RX frame receive | *(engine armed, no ucode/PHY)* | ob_rx.c | D11 FIFO0 | — | first RX frame | **absent** | MISSING |

---

## 2. Confirmed generic D11 mechanisms (reusable)

These are safe to rely on for rev42 because **two independent sources agree**
(brcmsmac corerev-agnostic code and the rev42 vendor blob):

1. **Object-memory programming** — `OBJADDR (D11+0x0160)` selector
   (`AUTO_INC=0x03000000`, `UCM_SEL=0`, `SHM_SEL=0x00010000`, `SCR_SEL=0x00020000`)
   + `OBJDATA (0x0164/0x0166)` data port. Concrete ucode stream:
   `write32(0x160, 0x03000000)` → `read32(0x160)` barrier → one 32-bit
   `write32(0x164, word)` per word. **Vendor blob `sub_60744` does exactly this**
   (`re fn 0x60744`), brcmsmac `brcms_ucode_write` does exactly this, OpenBRCM
   `ob_ucode_run_d2a` does exactly this.
2. **Initval record format** `{u16 offset;u16 width;u32 value}` 8 B LE,
   terminator `0xffff` last, applied as a direct D11 `write16/32(offset,value)`.
   Corroborated by `re regtables` (610/113/497) and the vendor applier
   `sub_60f67`.
3. **PSM states** — upload `MACCONTROL=0x04000404` (IHR_EN|PSM_JMP0|WAKE, no
   PSM_RUN/EN_MAC/SHM_EN); start `MACCONTROL=0x04020402`
   (IHR_EN|INFRA|PSM_RUN|WAKE).
4. **`MI_MACSSPNDD` poll** — bounded, 10 µs step, ≤100000 iterations.
5. **initvals after self-suspend**, strict table order.
6. **SHM read/write window** — `OBJADDR=SHM_SEL|(off>>2)`, barrier, then
   `OBJDATA + (off&2)` halfword.
7. **`MACCONTROL` shadowed masked update** (`wlc_bmac_mctrl`).
8. **D11 DMA64 32-bit window** — `addrhigh=0x80000000`, 8 KiB-aligned rings,
   FIFO0 RX @0x220, TX @0x200/0x240/0x280/0x2c0.

---

## 3. Historical brcmsmac-only behavior (do NOT port to rev42)

1. `brcms_ucode_download` corerev **23 (NPHY)** / **24 (LCNPHY)** dispatch; no
   17/42 branch. It uploads nothing on a BCM4352 and reports
   `unsupported corerev 42`.
2. The 15-region `bcm43xx-0.fw` + `bcm43xx_hdr-0.fw` packaging
   (`struct firmware_hdr`), region ids 10/12 ucode, 11/13 lengths, 14/15 BOM.
   **Superseded packaging**; do not add a loader.
3. The assumption that rev23/24 init ordering is rev42 ordering — **HISTORICAL
   REFERENCE only**; unverified for rev42.
4. `bcma_sprom_get()` rev8 MAC extraction (`0x8C`) is wrong for rev11; already
   corrected in OpenBRCM (`0x90`).

**Already-corrected, do not reintroduce:** the word-0 "off-by-0x104e" OpenBRCM
bug claim is **DISPROVED**.

---

## 4. Known rev42-specific evidence

The following are **rev42-specific** (recovered from the vendor blob and/or
verified hardware):

| Evidence | Source | Confidence |
|---|---|---|
| rev42 per-file images: `d11ucode42` (43400 B, 10850 words), `d11ac1initvals42` (4888 B, 610 = 113×w2+497×w4), `d11ac1bsinitvals42` (592 B, 73 = 39×w2+34×w4) | vendor blob slices; byte-identical to corpus; FNV/SHA reproduce | CONFIRMED |
| Ucode upload mechanism = generic OBJADDR/OBJDATA stream | vendor `sub_60744`; brcmsmac | CONFIRMED |
| `sub_6656c` band-init slice, selected at `phyrev==0x2a && phytype==0xb` | vendor blob | STRONGLY_SUPPORTED (analysis) |
| Band-0 MHF vector `{0x0100,0x0000,0x0000,0x0000,0x0080}` | vendor gates + hardware SPROM capture | PROVEN (analysis) |
| bsinitvals = band-specific ucode IHR/SHM/SCR init, applied once per band init, immediately **before** `wlc_phy_init`; 5 direct IHR writes + 68 SHM | vendor blob | STRONGLY_SUPPORTED |
| The `0x69594` `wlc_phy_switch_radio` and `0x6957b` `wlc_bmac_mute` are **not** on the AC initial path (NPHY/arg-3 gated) | vendor blob | PROVEN |
| Real AC PHY entry = `sub_6656c@0x669df → wlc_phy_init@0xbabf5`; `pi+0x28` = `sub_b018f` (installed by `wlc_phy_attach_acphy`) | vendor blob, reloc-covered imm32 | PROVEN |
| First radio-ON = `wlc_phy_init → wlc_phy_switch_radio(ON) → wlc_phy_switch_radio_acphy`; PLL branch A/B selected by `pi+0x16e = radio_rev>>4` | vendor blob | PROVEN except the runtime selector value |
| Calibration = `wlc_phy_cal_perical(pi,6) → wlc_phy_cals_acphy` (synchronous) | vendor blob | STRONGLY_SUPPORTED |
| `wlc_bmac_init` reachable indirects `0x6923d→dma64_rxinit`, `0x6924a→dma64_rxfill` | `re` base-aware indirect | PROVEN |
| DMA/IRQ tail: 4 TX channels, TX idle, FIFO0 RX 64 buffers, host IRQ route off until `wlc_bmac_up_finish` | vendor blob | PROVEN |

---

## 5. Missing rev42 behavior (to reach first TX/RX)

**Implementation gaps (production path):**

1. **Production ucode upload + PSM** — the proven D2A core exists only as an
   isolated mode; not integrated.
2. **Production common initvals** — proven D2B, not integrated.
3. **Production vendor tail (D3A1)** — proven isolated; not integrated. Also
   needed because the vendor interleaves DMA inside the tail
   (`sub_67efd → T1 → DMA → T2 → switch_macfreq`).
4. **Band init / bsinitvals (D3B)** — isolated only, and it **crashed** on
   hardware; no stable implementation.
5. **AC PHY init (D4)** — `wlc_phy_init` (`anacore → switch_radio(ON) →
   sub_b018f`). **Not implemented. `D4 IMPLEMENTATION GO: NO`.**
6. **PHY table loading / radio / channel / synth (D5/D6)** — not implemented.
7. **Calibration (D7)** — not implemented.
8. **`EN_MAC` + `wl_intrson`** — not implemented.
9. **TX path** — `ob_ops_tx` drops the frame; no TX ring/descriptors are posted.
10. **Correct ordering** — production enables FIFO0 RX + host IRQ + `MI_DMAINT`
    before ucode/initvals/PHY, inverting the vendor order (RX enable belongs
    after PHY/channel/`EN_MAC`).

**Value/evidence gaps blocking the above:**

11. **PLL branch selector** `pi+0x16e` = BCM2069 `radio_rev >> 4`, read from
    radio reg 0 bits[11:4] via the D11 radio-indirect pair. **HARDWARE-REQUIRED**
    (no NVRAM/SPROM override). Blocks D4 and therefore channel/TX/RX.
    The read-only probe is designed and frozen (`radio_id_probe_only=1`), **not
    executed**.
12. **Initial band/chanspec** — `wlc_init` computes it from locale/SPROM/NVRAM
    (`COMPUTED_RUNTIME`); no static constant.
13. **dev_lost latch not wired** to the D4 PHY/radio/table accesses (safety
    prerequisite, not a value).
14. **Runtime write masks** `pi+0xa7` (`COMPUTED_RUNTIME`), Farrow coefficient
    arrays (static `.data` tables, runtime-compacted).

---

## 6. OpenBRCM vs reference comparison

Reference = (a) old brcmsmac corerev 23/24 path, (b) the rev42 vendor blob.

| Operation | brcmsmac reference | rev42 vendor blob | OpenBRCM | Same? |
|---|---|---|---|---|
| D11 object-memory ucode stream | `OBJADDR=0x03000000` + `OBJDATA` words | `sub_60744` same | `ob_ucode_run_d2a` same | **MATCH** |
| PSM upload/start | `0x04000404` / `0x04020402` | `wlc_bmac_mctrl` same | same | **MATCH** |
| `MI_MACSSPNDD` poll | SPINWAIT | `wlc_bmac_wowlucode_start` | bounded poll | **MATCH** |
| Common initvals | `brcms_c_write_inits` (rev23 only) | `sub_60f67(d11ac1initvals42)` | `ob_initvals_run_d2b` (610) | **MATCH** (rev42 count) |
| bsinitvals | requested, never applied | `sub_60f67(d11ac1bsinitvals42)` at band init | isolated D3B only; **crashed** | **DIVERGENCE** |
| MACCONTROL tail update | `MCTL_INFRA\|MCTL_DISCARD_PMQ` | `0x04020402 → 0x44020402` | D3A1 `0x44020402` | **MATCH** |
| DMA/IRQ ordering | DMA in `wlc_bmac_init` tail, host IRQ off until up_finish | 4 TX + FIFO0 RX, `macintmask=0`, route off | production enables RX + host IRQ early | **DIVERGENCE** |
| `wlc_phy_init` | n/a (rev23/24 different PHY) | real AC entry, first radio-ON | not implemented | **MISSING** |
| corerev dispatch | 23/24 only | rev42 direct | rev42 direct (correct) | **DIVERGENCE (intentional, correct)** |

---

## 7. Registers / object-memory interfaces involved

- D11 host interface: `0x0120` MACCONTROL, `0x0124` MACCOMMAND, `0x0128`
  MACINTSTATUS (W1C), `0x012C` MACINTMASK, `0x0154` PSMDEBUG, `0x0158`
  PHYDEBUG, `0x015C` machwcap, `0x0180/0x0184` TSF, `0x0188` tsf_cfprep,
  `0x018C` tsf_cfpstart, `0x01E0` ~~CLKCTLST~~ **(D11-window identity UNKNOWN;
  CLKCTLST is the ChipCommon register of this offset — corrected 2026-09-27,
  see `docs/m34d4/m34e_d11_mac_accessibility.md`)**, `0x03E0` phyversion.
- Object memory: `0x0160` OBJADDR, `0x0164` OBJDATA lo, `0x0166` OBJDATA hi;
  selectors UCM `0x00000000`, SHM `0x00010000`, SCR `0x00020000`, AUTO_INC
  `0x03000000`.
- Post-common tail: `0x0100` intrcvlazy, `0x0024` intctrlregs[0].intmask,
  `0x0068` ifs_ctl (`0x0688`), `0x069C` ifs_aifsn, `0x06A8` fast-pwrup,
  `0x062E/0x0630` MAC clock divisors, TXE0 `0x0520/0x0530/0x0532/0x0534/
  0x0536/0x0540/0x0542/0x0548/0x054A/0x054C/0x054E/0x0550`.
- DMA: TX FIFO3 BK `0x200`, BE `0x240`, VI `0x280`, VO/CTL `0x2C0`; RX FIFO0
  `0x220` (CONTROL `0x84D`, PTR `0x400`, `addrhigh=0x80000000`).
- PHY indirect: `0x03E0` (status), `0x03FC` (addr|data), `0x03FE` (data),
  `0x03E6` (anacore); radio indirect: `0x03D8` (address latch), `0x03DA`
  (16-bit data).
- SHM config written by bsinitvals: bytes `0x000C`, `0x0010`, `0x001C`,
  `0x0094`, `0x0990..0x0A28`, `0x17D0/0x17D4`; MHF1..5 at `0x5E/0x60/0x62/
  0x78/0xD4`.

---

## 8. Ucode / initvals / bsinitvals state

- **Ucode**: rev42 image acquired and FNV-validated in production (`ob_fw_probe`)
  but **not uploaded**. Upload is proven only in the isolated D2A mode
  (10850 OBJDATA writes, PSM start, poll PASS `iterations=11`
  `MACINTSTATUS=0x1`). The upload mechanism is generic D11 and vendor-blob
  corroborated. Word 0 = `0x0300104e` is streamed as data by brcmsmac, the
  vendor applier `sub_60744`, and OpenBRCM; the `0x104e` start-address reading
  is a **HYPOTHESIS / open hardware question**, not a code defect.
- **Initvals**: common table shape proven (610/113/497); applied in isolated D2B
  with exact postconditions. Correct **for the isolated state**; not integrated;
  and the vendor's exact D2B→band-init entry state differs (extra tail writes,
  MACCONTROL `0x04020402→0x44020402`).
- **bsinitvals**: table (73 records) recovered and classified; applied only in
  the isolated D3B mode. **Required rev42 behavior**: the vendor blob applies it
  at band init via `sub_6656c@0x669bd`, before `wlc_phy_init`. Sources conflict
  on the *old* brcmsmac path — the handoff states brcmsmac "requests but never
  applies" bsinitvals in `wlc_coreinit`, while this branch's `brcmsmac`
  corroboration notes `brcms_c_ucode_bsinit`/`brcms_b_bsinit` on the set-band and
  initial-up paths (`docs/m34d3_bsinitvals.md` §1). This document does **not**
  resolve that conflict; it relies only on the vendor blob (rev42). OpenBRCM's
  isolated D3B attempt applied all 73 records then the platform crashed (§9).

---

## 9. Current failure boundary

Two distinct boundaries:

**(A) Production functional boundary (no crash).** The earliest point where the
expected vendor state is not reached is **stage 8/9**: after `ob_fw_probe` the
vendor would upload ucode + apply common initvals; OpenBRCM does neither. The
first *ordering* divergence after that is `ob_rx_init` arming FIFO0 RX and
`MI_DMAINT` **before** ucode/initvals/PHY (vendor enables RX only after
`wlc_bmac_up_finish`). No frame can ever be received: the MAC has no ucode, the
PHY/radio are uninitialised, and mac80211 `start()` returns `-EOPNOTSUPP`.

**(B) Observed hardware-state mismatch (isolated D3B, 2026-09).** From the
proven D3A1 exit, `bsinitvals_test_only=1` wrote MHF `{0x0100,0,0,0,0x0080}`
and all 73 `d11ac1bsinitvals42` records (`complete total=73 w16=39 w32=34`).
~4.84 s later **every post-D3B D11 read returned `0xffffffff`**
(`mhfs=ffff…`, `SHM[0x10/0x1c/0x94]=ffffffff`, `MACCONTROL=ffffffff`,
`MACINTMASK=ffffffff`). Validation failed; the driver entered
`ob_d3a0_teardown()` (`dma-test: quiesce begin`); the platform then reset with
an AMD **data-fabric sync flood** (`0x08000800`). No later OpenBRCM line, no
visible panic. This is the **earliest observed mismatch**: expected
post-D3B D11 window alive with the override values, observed all-ones (window
dead) followed by an unsafe teardown. Root cause **UNKNOWN**; post-mortem
`docs/m34d3b/d3b_crash_postmortem.md`. `HARDWARE RETEST: NO`.

> Note: D3B attempt 1 failed earlier on a *readback of a write-only register*
> (`tsf_cfpstart 0x18C`), which was an invalid equality gate, not a hardware
> mismatch; it was corrected and is not the boundary.

---

## 10. Highest-value experiments (ranked)

Ranked by (information gained × risk⁻¹) ÷ effort; all read-only unless stated.

| # | Experiment | Information | Risk | Effort |
|---|---|---|---|---|
| 1 | **Isolated radio identity probe** (`radio_id_probe_only=1`, frozen): min core prep + 2 selector writes + 2 reg0/1 reads → decode `radio_rev = reg0>>4` → PLL sequence A/B/SKIP. | Resolves the **hardware-required** `pi+0x16e` selector; unblocks D4 (AC PHY/radio), hence channel/TX/RX. | **Low** — read-only + 2 non-destructive selector writes; dev_lost-safe; decoder rejects ambiguity. | **Low** — candidate frozen & host-tested (`706b3407…`); one owner-approved `insmod`. |
| 2 | **Object-memory readback after D2A** (handoff exp. 1): in `ucode_test_only`, read UCM at object addr `0` and `0x104e`, then repeat with `OBJADDR=AUTO_INC\|UCM_SEL\|0x104e`. | Settles whether word 0 `0x0300104e` is metadata or data; validates the object-memory model. | **None** — read-only. | **Low** — ~2 lines after `src/ob_ucode.c:162`. |
| 3 | **Successor band-init test `M3.4D3+4`**: band init + bsinitvals then continue through `wlc_phy_init` (no STOP before it), with dev_lost wired and same-run quiesce. | Removes the non-vendor-stable STOP; tests whether STOP-before-`wlc_phy_init` is the crash trigger. | **Medium-high** — previous run reset the platform. | **High** — needs D4 value closure first. |
| 4 | **D3B retest with dev_lost wired + no teardown-on-loss.** | Distinguishes "bsinitvals bad" from "teardown-after-loss bad". | Medium-high. | Medium. |
| 5 | **Apply region id 1/7/8 of the 15-region package to compare SHM** (handoff exp. 2/3). | Narrows the id1–9 distinction. | Low (core stopped). | Low. |
| 6 | **`0x0130/0x0134` second address/data port probe** (handoff exp. 4). | Names a second port. | Unknown — unnamed registers. | Low. |

---

## 11. Safe implementation candidates

Only changes supported strongly enough to implement *now* (still no hardware
without owner approval):

1. **Integrate the proven isolated D2A + D2B into the production path** as the
   first real bring-up stages (ucode upload + common initvals), gated so the
   normal path can be enabled stage-by-stage. Evidence: D2A/D2B HARDWARE PROVEN;
   generic D11 mechanism corroborated.
2. **Read-only object-memory diagnostic** (experiment 2) added to the isolated
   `ucode_test_only` path. Evidence: read-only, no new writes.
3. **Fix the production RX/IRQ ordering** so FIFO0 RX + host IRQ + `MI_DMAINT`
   are not armed before ucode/initvals/PHY (or keep them behind the
   bring-up gate). Evidence: vendor order (`wl_intrson` only after
   `wlc_phy_init`).
4. **`dev_lost` observer wiring** for the new D4-path D11/PHY/radio accesses
   (safety only; no value guessing). Evidence: central guard design exists.

Explicitly **not** safe yet: D4 PHY/radio code, TX path, channel programming —
blocked by the `pi+0x16e` selector and value closure.

---

## 12. Unknowns that must remain UNKNOWN

1. `pi+0x16e` / BCM2069 radio revision numeric value (HARDWARE_REQUIRED; no
   default).
2. Whether object address `0x104e` is a start address (hypothesis).
3. Initial band/channel/chanspec for the first up (COMPUTED_RUNTIME).
4. Exact runtime write masks `pi+0xa7`, runtime-compacted Farrow coefficients.
5. Semantic names of the 5 direct IFS/NAV IHR fields (`0x680/0x682/0x684/
   0x686`, `0x700`) — values proven, names unknown.
6. Root cause of the D3B all-ones + teardown crash.
7. Whether the `sub_6656c` `osl_readw(D11+0x3e0)` result gates anything.
8. ISA of the ucode body (fails all 9 ISAs tested) — irrelevant to loading.
9. Distinction among 15-region ids 1–9 (historical packaging; do not name).

---

## Final report

1. **Exact current OpenBRCM bring-up sequence** — §1. Normal path:
   bcma bind → ChipCommon → PCIe2 host up → D11 core enable → FAST clock →
   external rev11 SPROM MAC/board → firmware *validation only* → DMA rings (SW)
   → IRQ registration → FIFO0 RX engine armed + host IRQ/MI_DMAINT → mac80211.
   No ucode/initvals/bsinitvals/PHY/channel/EN_MAC/TX in production. The
   rev42 vendor stages exist only as isolated tests (D2A/D2B/D3A0/D3A1 proven;
   D3B crashed; D4 analysis-only).

2. **Earliest confirmed divergence** — **Production:** after `ob_fw_probe` the
   vendor uploads ucode + common initvals; OpenBRCM does neither, and arms RX/IRQ
   early (stage 8/9; §9-A). **Hardware mismatch:** isolated D3B — post-73-record
   D11 window returned all-ones, then teardown → platform reset (§9-B).

3. **brcmsmac parts reusable as generic D11 semantics** — §2: object-memory
   OBJADDR/OBJDATA streaming, PSM MACCONTROL states, `MI_MACSSPNDD` poll,
   initvals-after-suspend, SHM window, masked `MACCONTROL`, DMA64 32-bit rings.

4. **Historical-only parts not to port** — §3: the corerev 23/24 dispatch, the
   15-region `bcm43xx-0.fw`/`bcm43xx_hdr-0.fw` package, and any inference that
   rev23/24 ordering is rev42 ordering.

5. **Missing rev42 behavior** — §5: production ucode+initvals integration; the
   vendor D3A1 tail; stable band-init/bsinitvals (D3B); AC PHY init (D4,
   `wlc_phy_init`); PHY tables/radio/channel/cal (D5–D7); `EN_MAC`; TX; and the
   corrected RX/IRQ ordering.

6. **Confirmed defect in the current ucode upload path** — **None.** The upload
   matches brcmsmac *and* the vendor blob `sub_60744`; the word-0 suspicion is
   DISPROVED. The only open item is a hardware question (does `0x104e` mean
   something), not a code defect.

7. **Are initvals correct for rev42?** The **format and the 610-record common
   table are correct** and hardware-proven in isolation (D2B postconditions
   exact). They are **not integrated** into production, and the isolated D2B
   exit is not the vendor's exact band-init entry state (missing tail +
   MACCONTROL `0x44020402`). The **bsinitvals (73-record) application is not
   proven** (D3B crashed).

8. **bsinitvals status** — Recovered, classified and isolatable (applied at
   `sub_6656c@0x669bd`, before `wlc_phy_init`); required rev42 behavior; one
   isolated hardware attempt applied all 73 records and then the platform
   crashed. **Not proven; retest NO-GO.**

9. **Five highest-value next experiments** — §10: (1) isolated radio-id probe;
   (2) object-memory readback at `0` vs `0x104e`; (3) successor band-init test
   through `wlc_phy_init` (D3+4); (4) D3B retest with dev_lost wired and no
   teardown-on-loss; (5) region id1/7/8 SHM comparison.

10. **Single experiment most likely to move OpenBRCM toward first TX/RX** — the
    **isolated BCM2069 radio identity probe** (`radio_id_probe_only=1`, already
    frozen `706b3407…`, decoder + harness ready). It resolves the
    hardware-required `pi+0x16e` PLL selector that blocks D4 (AC PHY/radio init),
    which in turn gates channel selection, `EN_MAC` and TX/RX. The handoff's
    `0x104e` readback (experiment 2) is the best *low-cost* firmware-model check
    but does not by itself unblock TX/RX.

---

### Separation of claims

**FACT** — vendor blob uses `OBJADDR/OBJDATA` ucode streaming; 610/73 record
shapes; D2A/D2B/D3A0/D3A1 hardware-proven; production has no ucode/initvals/PHY;
rev42 per-file images byte-identical to corpus.

**HISTORICAL REFERENCE** — brcmsmac 23/24 dispatch and 15-region packaging.

**HYPOTHESIS** — `0x104e` is a start address; D3B crash root cause; the STOP
before `wlc_phy_init` is non-vendor-stable and may relate to the crash.

**UNKNOWN** — §12 (radio revision, initial chanspec, runtime masks, IFS/NAV
names, crash mechanism).
