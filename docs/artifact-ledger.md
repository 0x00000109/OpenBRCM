# OpenBRCM artifact ledger

Persistence index for project knowledge. **Read this before re-deriving any
fact.** Machine-readable mirror: [`artifact-ledger.json`](artifact-ledger.json).
This ledger indexes reports; it does not duplicate them.

Generated 2026-09-25. Repository of record:
`/media/kartashoff/Storage/opensource/driver/OpenBRCM`.

## 0. Persistence status (important)

- **OpenBRCM HEAD** `7999966` on branch `m34d3b-band-init-test`, **15 commits
  ahead of `origin/main`** (`main` = `06d60d6`). **The D3B-implementation and
  the whole D4/D4B/D4C/lifecycle/D4A chain are UNMERGED**; the latest **8
  commits are UNPUSHED** (local only).
- **RE tooling** `iced/test` HEAD `f334e21` on `master`; `re.db` is
  **gitignored** (generated). Current `re.db` sha256
  `185f0bd84c2441379939ada95a4aba2e70c632f1175c41d759f9bd411b6ae449`
  (schema v4). Hashes cited in earlier docs (`44dae60d…`) are **stale**.
- Untracked local helper: `scripts/runtime-test.sh` (not a result artifact).

## 1. Hardware-proven milestones (merged to `main`)

| id | milestone | status | PR | artifact |
| :--- | :--- | :--- | :--- | :--- |
| M2–M3.4B | bind / DMA / IRQ / RX DMA L1 | HARDWARE RUNTIME PROVEN | — | `docs/milestones.md`, `docs/agent-state.md` |
| M3.4D1 | `fw_validate_only` | RUNTIME PASS | — | `docs/ucode_test.md` |
| M3.4D2A | ucode + PSM start | HARDWARE RUNTIME PROVEN | #2 | `docs/ucode_test.md`, `src/ob_ucode.*` |
| M3.4D2B | isolated common initvals | HARDWARE RUNTIME PROVEN | #6 | `docs/m34d2b_initvals_test.md`, `docs/m34d2b/`, `src/ob_initvals.*` |
| M3.4D3 | bsinitvals + PHY boundary | ANALYSIS ONLY | #7 | `docs/m34d3_bsinitvals.md`, `docs/m34d3/` |
| M3.4D3A0 | isolated DMA lifecycle | HARDWARE RUNTIME PROVEN | #8 | `docs/m34d3a0_dma_test.md`, `src/ob_d3a0.*` |
| M3.4D3A1 | vendor post-common tail | HARDWARE RUNTIME PROVEN | #9,#10 | `docs/m34d3a1_vendor_tail_test.md`, `src/ob_d3a1.*` |

## 2. D3B analysis evidence (merged)

| id | subject | merged | artifact |
| :--- | :--- | :--- | :--- |
| D3B MHF analysis | band-init / MHF provenance | #11 `06bbd60` | `docs/m34d3b_band_init.md` |
| D3B MHF closure | MHF1..MHF5 proven | #13 `5f6c3d2` | `docs/m34d3b_band_init.md`, `docs/m34d3b/evidence.jsonl` |
| SPROM field map | rev11 fields, T7 closed | #15 `29d8028` | `docs/m34d3b/rev11_sprom_fields.json`, `scripts/srom_var_table.py` |
| SPROM evidence | read-only capture, MHF3=0 | #14 `8b64b61` | `docs/m34d3b/d3b_sprom_capture.json`, `docs/m34d3b_sprom_evidence.md` |
| tooling bootstrap | OpenCode integration | #12 `6eff1ea` | `docs/re-tooling.md`, `scripts/re-bootstrap.sh`, `.opencode/` |

## 3. Unmerged work on `m34d3b-band-init-test`

| id | subject | commit | status | artifacts |
| :--- | :--- | :--- | :--- | :--- |
| D3B-IMPL | isolated band-init implementation | `b8bec51`…`2917ca9` | SIGNED / ATTEMPTED / NOT PROVEN | `src/ob_d3b.*`, `docs/m34d3b_band_init_test.md` |
| D3B-POSTMORTEM | platform crash post-mortem | `bcc53cf` | ANALYSIS / EVIDENCE | `docs/m34d3b/d3b_crash_postmortem.md` |
| DEV-LOST | device-lost fail-safe | `c244e98` | IMPLEMENTED / STATIC TESTED | `src/ob_d3a0.c`, `src/ob_d3a1.c`, postmortem |
| D4A-DECOMP | wlc_phy_init decomposition (superseded) | `21e83a0`, `95ccdff` | ANALYSIS | `docs/m34d4/d4a_decomposition.md` |
| D4B-RESUME | AC-PHY init lineage | `83e542b` | ANALYSIS | `docs/m34d4b/acphy_*`, `d4b_resume_analysis.md` |
| GHIDRA-AUG | tier-2 augmentation | `7d2daf9` | ANALYSIS / IMPL | `docs/m34d4b/ghidra_augmentation.*`, `scripts/ghidra*` |
| D4B-CLOSURE | value closure + CP-A3 | `84f9073` | ANALYSIS | `docs/m34d4b/d4b_value_provenance_closure.md`, `.json` |
| D4C | radio-OFF attach → operational | `e8946a1` | ANALYSIS | `docs/m34d4b/d4c_radio_on_transition.*` |
| LIFECYCLE | RPC caller + timeline | `00f22d8` | ANALYSIS | `docs/lifecycle/bcm4352_rev42_lifecycle.*` |
| D4A-REACHABILITY | actual rev42 reachability | `7999966` | ANALYSIS | `docs/m34d4/d4a_reachability_recovery.md`, `wlc_phy_init_bcm4352_reachable.json` |

## 4. Required knowledge → persistent location

| knowledge | location |
| :--- | :--- |
| D3B crash: all-ones MMIO, `0x08000800` sync flood, unsafe teardown | `docs/m34d3b/d3b_crash_postmortem.md` |
| device-lost invariant (monotonic latch, zero BAR writes, no-free) | `docs/m34d3b/d3b_crash_postmortem.md` Part A; `src/ob_d3a0.c`, `src/ob_d3a1.c` |
| `wlc_phy_attach_acphy` software role; real HW path | `docs/m34d4b/acphy_init_analysis.md`, `docs/m34d4/d4a_reachability_recovery.md` |
| 11 callback fields; `+0xf8` → `wlc_phy_btc_adjust_acphy`; 10 NULL | `docs/m34d4b/acphy_function_table.json` |
| re relocation + branch-aware EXACT/CONDITIONAL fixes (schema v4) | `docs/m34d4b/re_false_positive_fixes.md`; tooling `iced/test` `f334e21` |
| Ghidra TX DMA base correction; `+0xf8` corroboration | `docs/m34d4b/ghidra_augmentation.md` / `.json` |
| D4B closure: sub_a4adc/sub_9591e not on attach path; UNKNOWN=0 | `docs/m34d4b/d4b_value_provenance_closure.md` / `.json` |
| count correction (attach vs whole-family) | `docs/m34d4b/d4c_radio_on_transition.md`, `docs/milestones.md` |
| CP-A3 = LOGICALLY STABLE / RADIO OFF / NON-OPERATIONAL / NOT OBSERVABLE / NOT A MILESTONE | `docs/m34d4b/d4b_value_provenance_closure.md` |
| next direction (real radio-on / continuous timeline) | `docs/agent-state.md`, `docs/milestones.md`, `docs/lifecycle/` |

## 5. Superseded facts (do not quote as current)

| obsolete | current | correcting artifact |
| :--- | :--- | :--- |
| D4 attach = 186 PHY + 301 RADIO | attach on=0 = 12 PHY + 8 RADIO | `docs/m34d4b/d4c_radio_on_transition.md` |
| CP-A3 operational / hardware milestone | non-operational, radio OFF | `docs/m34d4b/d4b_value_provenance_closure.md` |
| sub_a4adc / sub_9591e attach blockers | radio-ON only | `docs/m34d4b/d4b_value_provenance_closure.md` |
| `phy+0xf8` literal/ambiguous | EXACT reloc to `wlc_phy_btc_adjust_acphy` | `docs/m34d4b/re_false_positive_fixes.md` |
| D4 follows D3B via `wlc_phy_init` | `wlc_phy_init` no-op for AC; path is `wlc_phy_cal_perical` | `docs/m34d4/d4a_reachability_recovery.md` |
| CP-O2 radio-enable checkpoint | RPC-only, no vendor-time CP-O2 | `docs/lifecycle/bcm4352_rev42_lifecycle.md` |

## 6. Open persistence risks

1. **Unmerged/unpushed work** — 15 commits on `m34d3b-band-init-test`; 8
   unpushed. A fresh agent on `main` cannot see the D4 chain. Push/PR required
   to make it durable.
2. **`re.db` is generated/ignored** — the v4 index is not in Git; must be
   rebuilt (`re db build`). Today's sha256 is recorded in the JSON ledger.
3. **Stale hashes in docs** — several D4B docs cite `44dae60d…`/`0c7cf875…`;
   the current build is `185f0bd8…`/`f6601944…`.
