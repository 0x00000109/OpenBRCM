# Milestones

Each milestone has a concrete, observable gate.

| M | Deliverable | Gate |
|---|---|---|
| **M0** | `bcma` probe, ChipCommon window, CHIPID | module loads; dmesg shows `chip 0x4352` |
| **M1** | `ob_si` bring-up (PMU/PLL/clock), OTP→SPROM | dmesg `SROM MAC xx:xx:...` (not the fallback) |
| **M2** | mac80211 registration; wiphy bands/channels/rates | `iw phy` lists 2.4/5 GHz and VHT rates |
| **M3** | dma64 rings, NAPI, `ieee80211_rx` | interface up; RX/TX counters move |
| **M4** | `.config`, `.bss_info_changed`, `.set_key`, scan | `iw scan`, association, encrypted link |
| **M5** | AMPDU, power save, runtime PM, LED/rfkill | stable traffic; suspend/resume |
| **M6** | second PHY family (nphy/htphy) | family module loads |

## M2.5 (current) — power-up / OTP / SPROM / MAC
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

## Current state
M0/M1/M2 complete on hardware (bcma binds, PHY registers). M2.5 instrumented;
MAC recovery pending the OTP-FSM provenance item above.

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
