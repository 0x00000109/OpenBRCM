# M3.4D4 — device-loss guard for the D3B → `wlc_phy_init` → radio/PHY/cal path

Blocker: **`D4-BLOCKER-DEV-LOST-PHY-PATH`**. Status:
**`ANALYSIS` + `IMPLEMENTED` / `STATIC TESTED`** (safety infrastructure only —
**no hardware, no MMIO, no driver execution, no PHY implementation expansion, no
PLL-value investigation, no new hardware candidate**). Machine-readable map:
[`dev_lost_phy_access_map.json`](dev_lost_phy_access_map.json).

## 0. Primary answer

> Can every D11/PHY/radio access reachable on the exact rev42 operational path
> after D3B be guaranteed to stop safely once device loss is detected?

**YES, at the access-primitive level.** Every D11 / PHY-indirect / radio /
SHM-OBJ / DMA / ChipCommon / AXI register access in the driver now goes through
one central guarded layer (`src/ob_guard.{c,h}`). Once `hw->dev_lost` is
latched, the layer issues **no MMIO**: reads return a dead sentinel and writes
are suppressed. Detection remains limited to **trusted direct 32-bit D11 reads
== `0xffffffff`**; the first such read on the path is the D3B postcondition
(read before any SHM/OBJ access). No new magic predicate is introduced.

## 1. Access map (Phase 1)

The path `sub_6656c → wlc_phy_init → wlc_phy_anacore → wlc_phy_switch_radio →
wlc_phy_switch_radio_acphy → wlc_phy_cals_acphy` was enumerated from the
existing re.db-derived inventory ([`dev_lost_access_map.json`](dev_lost_access_map.json)),
grouped by helper family (not per-site). Counts of vendor operations on the path:

| family | vendor helpers | sites | OpenBRCM guarded primitive |
| :--- | :--- | ---: | :--- |
| DIRECT_D11_READ | `osl_readl`/`osl_readw` | 3 | `ob_d11_read32/read16` |
| DIRECT_D11_WRITE | `osl_writel`/`osl_writew` | 3 | `ob_d11_write32/write16` |
| PHY_INDIRECT_READ | `phy_reg_read` | 5 | `ob_d11_read32` |
| PHY_INDIRECT_WRITE | `phy_reg_write`, `phy_reg_write_array`, `phy_reg_mod` | 45 | `ob_d11_write32/write16` |
| PHY_TABLE_WRITE | `wlc_phy_table_write_acphy` | 2 | `ob_d11_write32` |
| RADIO_READ | `read_radio_reg` | 5 | `ob_d11_read32` |
| RADIO_WRITE | `write_radio_reg`, `mod_radio_reg` | 57 | `ob_d11_write32/write16` |
| SHM/OBJ | `wlc_bmac_read_shm`/`write_shm` | — (D3B MHF via `ob_ucode_*`) | `ob_ucode_read_shm16`/`write_shm16` → `ob_d11_*` |
| DMA | tx/rx init/reset control+status | — | `ob_d11_*` + guarded D3A0 teardown |
| BCMA/CORE | host/core enable/reset | — | gated `bcma_*` in `ob_si`/`ob_ucode`/`ob_d3a0` |

The vendor PHY/radio helpers ultimately write the **D11 register window**
(`*(pi+0x148)`), so a single `ob_d11_*` guard covers direct, PHY-indirect and
radio accesses.

## 2. Unsafe gaps found (Phase 2) and how they are resolved

| gap class | before | after |
| :--- | :--- | :--- |
| A observer missing | direct `bcma_*` writes across d3a0/d3a1/d3b/rx/irq/ucode/si | all routed through `ob_guard` |
| B observer too late | D3B read postconditions already latched, but writes below them were unguarded | guard enforces at the primitive |
| C read detects but does not latch | only a few `ob_dev_lost_observe32` sites | `ob_d11_read32_trusted` latches at the first trusted all-ones |
| D latched but caller continues | teardown checked `dev_lost`; programming did not | guard suppresses the writes; callers see dead sentinels |
| E nested helper writes after latch | `ob_ucode` SHM/OBJ used raw bcma | nested helpers routed through the guard |
| F safe already | normal (live) path | unchanged (direct bcma pass-through when `dev_lost == false`) |

## 3. Centralization (Phase 3)

- `src/ob_guard.h` — pure decision layer (`ob_guard_do_read32/write32/read16/
  write16`, sentinels, `ob_guard_next_latch`) + backend abstraction, no kernel
  API, host-testable.
- `src/ob_guard.c` — bcma backend + the only place raw `bcma_read*/write*`
  remains; exposes `ob_d11_*`, `ob_d11_read32_trusted`, `ob_axi_*`, `ob_cc_*`.
- `struct ob_hw::dev_lost` stays the single monotonic latch; `struct
  ob_hw::guard_stat` records issued/blocked counters (diagnostics).
- Core/reset operations are gated: `ob_si_powerup`, `ob_ucode_prepare`,
  `ob_d3a0_core_contain`.
- The deterministic audit `tests/host/test_device_lost_guard.py` fails if any
  raw `bcma_read*/write*` reappears outside `ob_guard.c`, or if the trusted
  detector / core gates disappear.

Legacy helpers that cannot propagate an error keep the **containment strategy**:
writes are suppressed at the primitive (no MMIO) and reads return a dead
sentinel; the caller's existing `dev_lost` checks return `-EIO`.

## 4. All-ones detection (Phase 4)

Semantically valid only for trusted direct D11 window reads. Reused unchanged:

- `ob_d3a0_mmio_is_all_ones(v)` = `v == 0xffffffff`;
- vendor `wlc_hw_deviceremoved` predicate `ob_d3a0_maccontrol_present(m)` =
  `(m & 0x404) == 0x400`;
- arbitrary SHM/OBJDATA/table payloads are **never** classified as device loss
  (a 16-bit `0xffff` is a legitimate value). The guard only blocks access after
  a genuine trusted latch.

## 5. Tests (Phase 5) — no hardware

`tests/host/ob_guard_test.c` (cases 1–12 + negatives), run by `make hosttest`:

1. normal access before `dev_lost`; 2. trusted all-ones latches; 3. latch
monotonic (never clears); 4. direct D11 write suppressed after latch; 5.
PHY-indirect write suppressed; 6. radio write suppressed; 7. SHM/OBJ access
suppressed; 8. DMA teardown/reset suppressed; 9. nested caller sees the dead
sentinel and stops; 10. repeated removal/unload performs zero hardware access;
11. resources retained (latch never clears, free gate stays denied); 12. normal
non-`dev_lost` paths unchanged. Negatives: untrusted/16-bit all-ones payloads
never latch; writing all-ones is not an observation.

`tests/host/test_device_lost_guard.py` is the deterministic centralization
audit. `make modules` builds the guard into `openbrcm.ko`; the previously
hardware-proven normal path is a pass-through when `dev_lost == false`.

## 6. Revalidation of the exact path (Phase 6)

- **First possible loss observation** = the first trusted direct D11 read on the
  path: the D3B postcondition `MACCONTROL`/`MACINTMASK` reads (already present),
  and henceforth the first trusted read in a future PHY entry.
- **Latch** = `ob_d11_read32_trusted` → `ob_dev_lost_latch` (monotonic, sets the
  module-wide fatal latch, pins the module, no free).
- **Zero later hardware writes** = every subsequent write is a guarded write and
  is suppressed; the D3B/D3A1 failure paths return `-EIO` without teardown.

## 7. Scope guard (Phase 7)

This is **static safety infrastructure**. It does **not** change
`D4 IMPLEMENTATION GO` (still blocked by the remaining D4 blockers) and
**`HARDWARE TEST GO` remains NO**. No hardware candidate was prepared or run.

## 8. STOP boundary

No `insmod`/`rmmod`/`modprobe`; no MMIO execution; no PHY implementation; no
PLL-value work. The device-loss guard is `IMPLEMENTED` / `STATIC TESTED`
(`make modules`, `make hosttest`). Any hardware experiment remains forbidden
until the remaining D4 blockers are resolved.
