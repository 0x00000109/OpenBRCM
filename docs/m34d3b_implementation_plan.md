# M3.4D3B — implementation plan (vendor-faithful band init + `d11ac1bsinitvals42`)

**Status: `ANALYSIS ONLY` (PLAN ONLY) — NOT IMPLEMENTED — NOT HARDWARE PROVEN.**

`D3B IMPLEMENTATION GO: YES`. All MHF inputs are PROVEN; the final band-0
vector is `mhfs[0..4] = {0x0100, 0x0000, 0x0000, 0x0000, 0x0080}` (MHF3 =
`0x0000`, closed by the 2026-09 read-only SPROM capture). This branch contains
the **plan only**; no D3B code and no hardware run.

Design source of truth: [`m34d3b_band_init.md`](m34d3b_band_init.md) (boundary,
table, postconditions, failure policy); MHF provenance §3.5/§3.8; capture
evidence [`m34d3b/d3b_sprom_capture.json`](m34d3b/d3b_sprom_capture.json).

## 1. Scope and boundary

- **Entry state:** the hardware-proven D3A1 exit — after
  `wlc_bmac_switch_macfreq` (`0x695cb`), DMA engines initialized, host IRQ
  route off.
- **Start:** `sub_6656c` entry (`0x6656c`, called from `wlc_bmac_init`
  `0x695d8`).
- **End (STOP):** the `sub_60f67` applier return (`0x669c2`), **before**
  `wlc_phy_init` (`0x669df`) / `wlc_phy_anacore` / any PHY-indirect or
  radio-window write.
- **New isolated mode:** `bsinitvals_test_only=1`, mutually exclusive with
  `fw_validate_only` / `ucode_test_only` / `initvals_test_only` /
  `dma_test_only` / `d11_tail_test_only` / `sprom_evidence_only` (conflict ->
  `-EINVAL` before any hardware access).
- **Reuse (do not fork):** the proven D3A1 prefix (`ob_d3a1_test()` /
  `ob_initvals_run_d2b()` core) and the D3A0 DMA lifecycle in its vendor
  position.

## 2. Operations (vendor order)

1. `sub_62766` = `wlc_bmac_write_mhf`: write MHF1..5 to SHM
   `0x5e/0x60/0x62/0x78/0xd4` from the band-0 vector
   `{0x0100, 0x0000, 0x0000, 0x0000, 0x0080}`.
2. `sub_60f67(dev, d11ac1bsinitvals42)`: apply the **73-record / 592 B** table
   (34 x OBJADDR w4 + 15 x OBJDATA-lo w2 + 19 x OBJDATA-hi w2 + 5 x direct IHR
   w2; same table for 2.4/5 GHz). No PHY/radio/IRQ/DMA-enable effect.
3. Read the deterministic postconditions and **STOP** before `wlc_phy_init`.

## 3. Postconditions and failure policy

- Postconditions exactly as recorded in `docs/m34d3b_band_init.md` (no invented
  checks); bounded polling only.
- Failure policy: the D3A1 **one-shot / reboot-or-full-reinit** rule — on any
  postcondition mismatch do not retry in the same boot; recover logs first.

## 4. Tests and guards (to add with the implementation)

- host + KUnit: table record count/shape, MHF vector, applier bounds, STOP
  boundary.
- deterministic `scripts/docs-check.sh` guard for the new mode and boundary.
- no `insmod`/hardware run in the planning milestone.

## 5. Explicit non-goals

`sub_6656c`->PHY boundary crossing, `wlc_phy_init`, AC PHY, radio, calibration,
channel, real RX/TX, scan/association. Those are D4+.

## 6. STOP boundary

Planning only. D3B code may be written only in a later task with explicit
approval; D3B remains NOT IMPLEMENTED / NOT HARDWARE PROVEN until then.
