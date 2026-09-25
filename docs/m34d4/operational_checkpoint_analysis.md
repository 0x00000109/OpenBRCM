# M3.4D4 Phase 7 — earliest STRONG operational checkpoint (post-D3B)

**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no driver code, no candidate.

A checkpoint is a place where an isolated bring-up may STOP and still be
vendor-faithful with deterministic postconditions. Classification:
`INVALID` / `WEAK` / `STRONG`.

## Candidate checkpoints

| id | point | radio | PHY | PLL | cal | pending op | executed-value closure | DMA/PSM/MAC | verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| CP-D4.0 | entry `wlc_phy_init` | OFF (attach) | attach-only | not set | not done | none yet | n/a | PSM run (D2A), DMA (D3A0 is a separate isolated run) | **INVALID** |
| CP-D4.1 | after `wlc_phy_init` returns (end of `sub_6656c`'s PHY init) | ON | AC init callback `sub_b018f` done | set | radio RX cal (`sub_9591e`) done; **perical not done** | none | `pi+0x16e` branch, masks `pi+0xa7/0x8be/0x8bf` runtime | MAC resumed | **WEAK** |
| CP-F | after `wlc_phy_cal_perical` returns | ON | initialized | set | perical + `wlc_phy_cals_acphy` done | none | same runtime values | MAC/PSM/DMA understood | **WEAK (best candidate)** |
| CP-O2 | `wlc_bmac_radio_hw` return | ON | n/a | n/a | n/a | n/a | n/a | n/a | **INVALID** (RPC-only, not vendor-executed) |

## Why no STRONG checkpoint exists yet

Per the STRONG criteria, a candidate must have:

1. no pending async PHY/radio operation — satisfied at CP-D4.1/CP-F;
2. no incomplete calibration — only CP-F satisfies (CP-D4.1 leaves perical out);
3. **no unresolved executed write value** — **FAILS**: masks `pi+0xa7`, `pi+0x8be`,
   `pi+0x8bf`, the `pi+0x16e` PLL branch, farrow coefficient arrays and the
   `pi+0xf9c`/`pi+0xf89` states are `COMPUTED_RUNTIME`/`UNKNOWN`;
4. no unknown indirect callback — **PARTIAL FAIL**: `pi+0x28`/`pi+0x30`/`pi+0xc0`
   are now resolved (sub_b018f/sub_8e77a/sub_97e2b), but `wlc_bmac_init`
   0x6923d `[rax+0xA0]` and 0x6924a `[rax+0xD8]` remain `UNRESOLVED`;
5. MAC/PSM state understood — PSM run (D2A), MAC resumed by `wlapi_enable_mac`;
6. DMA state understood — D3A0 isolated; not part of this path's teardown;
7. safe dev_lost handling — **NOT WIRED** for the new direct D11 reads;
8. bounded teardown/recovery — the D3B crash shows teardown-on-device-loss is
   unsafe.

## Conclusion

- **EARLIEST STRONG OPERATIONAL CHECKPOINT: NONE.**
- Best candidate = **CP-F** (after `wlc_phy_cal_perical` returns), classified
  **WEAK**: vendor-faithful and synchronous, but fail-closed value closure and
  dev_lost wiring are not complete, and no deterministic observable
  postcondition is proven for rev42 AC.
- A STOP after `wlc_phy_init` (CP-D4.1) is **not** vendor-stable: the vendor
  continues to `wlc_set_home_chanspec` and `wlc_phy_cal_perical`, and the D3B
  crash already showed that stopping inside the D11/PHY boundary is unsafe.
