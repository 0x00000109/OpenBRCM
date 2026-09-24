# M3.4C — BCM4352 / D11 rev42 RX prerequisite audit

Status: **analysis only + read-only diagnostic. No new bring-up stage
implemented. RX DMA left untouched.** Blob: `wlc_hybrid.o_shipped` (Repo A).

Provenance: **C2** = binary-confirmed, **C3** = upstream structural
(`brcmsmac`/`bcma`), **UNKNOWN** = not yet recovered.

## 1. Vendor bring-up call graph (C2 symbols + C3 ordering)

Attach-time (A):
- `wlc_attach` (0x37d10) → `wlc_bmac_attach` (0x6984f): dma_attach per FIFO
  (0x10380), PHY attach via `wlc_phy_attach` (0xbe426) →
  `wlc_phy_attach_acphy` (0xa194f) for BCM4352 acphy.

Interface-up (B), from `wlc_up` (0x3aac3):
- `wlc_bmac_up_prep` (0x6646a): `wlc_bmac_xtal(ON)`, `si_clkctl_init`,
  `si_pci_setup`, `si_setcore`, radio-hw-disabled check, `si_pci_up`,
  `wlc_bmac_corereset` (0x65e12) → `wlc_bmac_mctrl`, `si_core_reset`,
  `wlc_bmac_write_ihr`, `wlc_bmac_phy_reset`, `wlc_bmac_core_phypll_ctl`.
- `wlc_bmac_init` (0x6828a): `wl_intrsoff`, `si_pmu_rfldo`,
  `wlc_phy_chanspec_radio_set`, `wlc_phy_cal_init`, `wlc_bmac_mctrl`,
  `wlc_bmac_mhf`, SHM writes, `wlc_bmac_wowlucode_start`, IHR/objmem copies.
- `wlc_bmac_enable_mac` (0x60d3d): `wlc_bmac_mctrl` (EN_MAC),
  `wlc_ucode_wake_override_clear`; the blob writes `macintstatus (0x128) = 1`.
- `wlc_bmac_up_finish` (0x66409): `wlc_phy_hw_state_upd`, `wl_intrson`.

Channel (C): `wlc_phy_chanspec_set` (0xb5d48) → `wlc_phy_chanspec_radio_set`
(0xb1c8a) → radio20xx setup; `wlc_c_set_chanspec`.

RX DMA (D): `dma_rxinit` (blob 0xf8c0 raises→aligned) → `_dma_rxenable`
(0xe5f0) → `dma_rxfill` (PTR write 0xf3b8).

Ucode upload: written to D11 IHR/object memory (`wlc_bmac_write_ihr` during
`corereset`; `wlc_bmac_copyto_objmem` 0x62d48) and started through
`maccontrol` PSM; the exact rev42 entrypoint is **UNKNOWN** (no symbol).

## 2. Ordered prerequisite sequence before first RX

1. (A) D11 core enable/reset, FAST clock, PHY attach, DMA attach.
2. (B) xtal on + clock init + host IRQ routing + core reset.
3. (B) **D11 ucode download** + **initvals** upload; start PSM.
4. (B) `MACCONTROL`: `IHR_EN` (+`WAKE`), later `EN_MAC`.
5. (B) **PHY init/cal** + **radio init**.
6. (C) **channel/chanspec** select (2.4 GHz).
7. (B/C) RX filters / MHF / SHM setup.
8. (D) RX DMA ring + buffers + `_dma_rxenable`.
9. (B) `EN_MAC`.
10. (B) interrupt enable (`wl_intrson`, I_RI + MI_DMAINT).

## 3. Current OpenBRCM state vs each prerequisite

| # | stage | state |
|---|---|---|
| 1 | core/clock/PHY-attach/DMA-attach | IMPLEMENTED + runtime proven (M2/M3.2) — except PHY attach |
| 2 | clock + IRQ route + core reset | PARTIAL (FAST clock M2, IRQ route M3.4B, bcma core enable) |
| 3 | **D11 ucode + initvals** | **NOT IMPLEMENTED** (no `request_firmware` call at all) |
| 4 | MACCONTROL IHR_EN/EN_MAC | **NOT IMPLEMENTED** (never written) |
| 5 | PHY init/cal + radio init | **NOT IMPLEMENTED** |
| 6 | hardware channel/chanspec | **NOT IMPLEMENTED** |
| 7 | RX filters / MHF / SHM | **NOT IMPLEMENTED** |
| 8 | RX DMA ring/control | IMPLEMENTED + runtime LEVEL-1 proven (M3.4B) |
| 9 | EN_MAC | **NOT IMPLEMENTED** |
| 10 | interrupt enable (I_RI + MI_DMAINT) | PARTIAL (enabled, but no traffic) |

`ob_si_d11_diag()` (M3.4C) now logs `maccontrol`/`maccommand`/`macintstatus`/
`macintmask`/`psmdebug`/`phydebug`/`phyversion`/`tsf` to confirm at runtime.

## 4. Microcode blobs required

- OpenBRCM currently declares `brcm/bcm43xx-0.fw` + `brcm/bcm43xx_hdr-0.fw`
  (C3: brcmsmac `brcms_ucode_download` uses these only for D11 corerev 17/23/24)
  and **does not call `request_firmware()` at all**.
- Those files **do not cover D11 rev42 / acphy**. BCM4352 D11 softmac ucode +
  initvals are not in mainline linux-firmware (it ships *fullmac* PCIe firmware
  for 4352, a different datapath).
- `bcm4352-d11ac1initvals42.bin` is an **initvals** image (PHY/radio tables),
  not D11 ucode. The exact rev42 ucode image and its source are **UNKNOWN** and
  must be recovered from the vendor package / blob.

## 5. initvals role (partial)

Initvals are `(address, value)` register tables applied after ucode download,
targeting PHY/radio/SHM/MAC address spaces. Band-specific variants
("bsinitvals") exist. Applying requires PHY clocks and normally a suspended
MAC. The exact rev42 encoding/order and whether MAC suspend is required are
**UNKNOWN** (blob data tables `d11a0g0*` are for older revs; acphy rev42
tables not yet located in the blob).

## 6. MACCONTROL (D11+0x120) — C3 d11.h

`EN_MAC(0) PSM_RUN(1) PSM_JMP_0(2) SHM_EN(8) SHM_UPPER(9) IHR_EN(10)
INFRA(17) AP(18) LOCK_RADIO(19) BCNS_PROMISC(20) PHYLOCK(21) KEEPCONTROL(22)
KEEPBADFCS(23) PROMISC(24) HPS(25) WAKE(26) TBTTHOLD(28) DISCARD_PMQ(30)
GMODE(31)`. `maccommand` 0x124, `macintstatus` 0x128, `macintmask` 0x12C,
`psmdebug` 0x154, `phydebug` 0x158, `phyversion` 0x3E0 (u16), TSF 0x180/0x184.
`ucode_running` is derived from `PSM_RUN & IHR_EN`; `mac_enabled` from `EN_MAC`.
Do **not** set `EN_MAC` until the full prerequisite sequence is proven.

## 7. PHY / radio identification

BCM4352 uses the **acphy** (`wlc_phy_attach_acphy` 0xa194f); radio is a
2057/2056-class ac radio. Radio ID/revision and the exact acphy rev42 tables
must be read at runtime (requires PHY register access, not yet implemented).
Entrypoints: PHY `wlc_phy_attach`/`wlc_phy_cal_init`; radio setup is inside the
PHY cal/chanspec path.

## 8. Channel-1 programming path (2.4 GHz, 2412 MHz)

`chanspec` (2.4 GHz ch1, 20 MHz) → `wlc_c_set_chanspec` → suspend MAC →
`wlc_phy_chanspec_set` (0xb5d48) → `wlc_phy_chanspec_radio2057_setup`
(and acphy channel tables) → radio synth/PLL programming → resume MAC. Tables
and exact registers are **UNKNOWN** for rev42. Not implemented.

## 9. Read-only runtime diagnostic added

`ob_si_d11_diag()` logs, at probe (after power-up, before any new bring-up):
```
bringup: maccontrol=… maccommand=… macintstatus=… macintmask=…
bringup: psmdebug=… phydebug=… phyversion=… tsf=…
bringup: en_mac=… psm_run=… ihr_en=… infra=… promisc=… wake=…
bringup: ucode_running=… mac_enabled=…
```
No register is written.

## 10. Decision tree / first missing prerequisite

Dependency-ordered classification:
- **A. D11 microcode not loaded/running — MISSING (root).**
- **B. initvals missing/incomplete — MISSING (gated by A).**
- **F. MAC `EN_MAC`/`IHR_EN` not set — MISSING (gated by A/B).**
- **C. PHY not initialized — MISSING.**
- **D. radio not initialized — MISSING.**
- **E. no hardware channel selected — MISSING.**
- **G. RX filter/SHM setup missing — MISSING.**
- H. RX DMA — DONE.

**First missing prerequisite to implement next: A (D11 rev42 microcode), then B
(initvals), then F (MAC enable), then C/D/E (PHY/radio/channel).** The very next
actionable step is to **recover/obtain the BCM4352 D11 rev42 ucode + initvals
images and the exact upload/start sequence**, since nothing downstream (MAC
enable, PHY, radio, channel) can run without them. If those images are
unavailable, that is a hard blocker and must be resolved before further RX
bring-up.

**End of M3.4C. No bring-up stage implemented; RX DMA untouched.**
