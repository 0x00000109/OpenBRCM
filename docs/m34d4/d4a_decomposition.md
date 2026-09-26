> **SUPERSEDED (2026-09):** `wlc_phy_init` is a **no-op for BCM4352 AC**
> (`[pi+0x28]==0`); the checkpoint/CP-F model here and any "186 PHY" scope are
> withdrawn. The real post-D3B path is `wlc_phy_cal_perical` →
> `wlc_phy_cals_acphy`. See
> [`d4a_reachability_recovery.md`](d4a_reachability_recovery.md) and
> [`../artifact-ledger.md`](../artifact-ledger.md) §5.

# M3.4D4A — BCM4352 rev42 AC-PHY `wlc_phy_init` decomposition (PURE RE)

**Status: `ANALYSIS ONLY`.** No hardware, no MMIO, no `insmod`, no implementation,
no hardware candidate. Tool-first `re`/`re.db`; raw `objdump` only where `re`
cannot represent a fact (gaps recorded in §K).

Machine-readable artifact: [`wlc_phy_init_rev42_flow.json`](wlc_phy_init_rev42_flow.json).

## Tooling record

| item | value |
| :--- | :--- |
| `re` sha256 | `6b91079459fdd281729314f97ea1c177fac294b47fcce3ff98bc3874cd4cf4fc` |
| `re.db` sha256 | `7812fec92c2e46fa3702e2560a1521eb5984fe73ca2c430c222bb4bc0d3f78f0` |
| blob sha256 | `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743` |
| functions / calls | 5095 / 51193 |
| reloc-site mismatches | 0 |
| `scripts/re-bootstrap.sh` | PASS |
| verification gates | present (bootstrap PASS; `re verify` shape: 0 mismatches) |

Every claim below cites the `re` query in the artifact's `tooling.queries`.

---

## A. Re-audit of the `dev_lost` invariant

Narrow claim to prove: after `hw->dev_lost` is latched, **no D11/BCMA BAR MMIO
is reachable** from the isolated-mode flows.

Access classes reachable after DMA bring-up (source audit of `src/ob_d3a0.c`,
`ob_d3a1.c`, `ob_d3b.c`, `ob_ucode.c`, `ob_initvals.c`):

- **A/B D11 BAR reads/writes** — live in `ob_d3a0.c`, `ob_d3a1.c`, `ob_d3b.c`,
  `ob_ucode.c` (`ob_ucode_read/write_shm16`), `ob_initvals.c`,
  `ob_d3a0_*`/`ob_d3a1_*` helpers.
- **C SHM/OBJ indirect** — `ob_ucode_read_shm16`/`ob_ucode_write_shm16`
  (`OBJADDR 0x160` + `OBJDATA 0x164/0x166`), used by `ob_initvals_apply`.
- **D DMA registers** — `ob_d3a0_{rx,tx}_reset`, `ob_d3a0_quiesce`,
  `ob_d3a0_bringup`.
- **E BCMA wrapper/core MMIO** — `bcma_core_disable`/`bcma_core_is_enabled`
  (`ob_d3a0_core_contain`).
- **F PCI config-space** — **none** in the OpenBRCM latch path
  (`bcma_host_pci_irq_ctl` is not called post-latch; the vendor `si_deviceremoved`
  PCI-config probe is not implemented).
- **G pure software** — postcondition predicates/state only.

Proof of the invariant (guard + dispatch, `c244e98`):

1. Latching points are the *last* access of a phase and are immediately followed
   by a return: `ob_d3b_read_post` reads MACCONTROL/MACINTMASK **first** and
   returns before any SHM/OBJ access; `ob_d3a1_validate` returns -EIO right after
   the observed all-ones; `ob_d3a0_validate`/`ob_d3a0_rx_program` latch on
   `status0==0xffffffff`.
2. Post-latch dispatch is guarded: `ob_d3a0_teardown`, `ob_d3a0_quiesce`,
   `ob_d3a0_rx_reset`, `ob_d3a0_tx_reset` each `return -EIO` when `dev_lost`;
   `ob_d3b_test::fail_after_dma` returns before `ob_d3a0_teardown`;
   `ob_d3b_remove`/`ob_d3a1_remove`/`ob_d3a0_remove` take the `fatal` branch
   (no teardown/free); `ob_d3a0_free_mem` is blocked by `ob_d3a0_can_free`.
3. Therefore every other accessor lives behind those guards or in the normal
   (non-isolated) driver path, where `dev_lost` is never latched.

**Result: the invariant is literally true for D11/BCMA BAR accesses reachable
from the isolated-mode flows.** No hole found; **no new functionality added**.
PCI config-space is **not used** post-latch (no exception needed today); if a
future passive `pci_read_config_dword` probe is added it must be documented as a
separate non-BAR exception.

---

## B/C. `wlc_phy_init` call graph and semantic phases

Entry: `sub_6656c` -> `sub_60f67(d11ac1bsinitvals42)` (`0x669bd`) ->
`wlc_phy_init` (`0x669df`, `band=0`). `wlc_phy_init` is **not atomic**; it is
15 direct calls plus two indirect targets, grouped into phases derived from
control flow (full table in the JSON):

| phase | name | key operations | sync | AC taken |
| :--- | :--- | :--- | :--- | :--- |
| D4.0 | entry / chanspec SHM | re-entry guard; `wlc_phy_chanspec_shm_set` -> **SHM `0xa0`**; read **MACCONTROL `0x120`** | none | yes |
| D4.1 | anacore | `wlc_phy_anacore(pi,1)`; AC -> `[pi+0x118]` fn ptr or `writew(D11+0x3e6)` | none proven | yes (generic branch) |
| D4.2 | BW / PHY reset | `wlapi_bmac_bw_set` -> `wlc_bmac_bw_set` (reads `0x120`; reset; may re-init) | inside reset | conditional |
| D4.3 | AC/chip tweaks | `[pi+0x32d]=0`, `[pi+0xf88]=0` for 4352/4360 | none | yes |
| D4.4 | radio switch/init | `wlc_phy_switch_radio(pi,1)` -> **`wlc_phy_switch_radio_acphy`** (`0xaa782`, 4266 B): **`wlapi_suspend_mac_and_wait` first**, then `mod_radio_reg`/`read_radio_reg` + `osl_delay`; `call [pi+0x28]` | MAC suspend + fixed delays | yes |
| D4.5 | dummy TX | `wlc_phy_do_dummy_tx` (phytype `0x0b/0x0c`): template RAM + many `osl_writew` | unknown | yes |
| D4.6 | status/txpower/ant/ACI | `phy_reg_read` (phytype 2 only), `wlc_phy_txpower_update_shm` (phytype 0/2 only), `wlc_phy_ant_rxdiv_set` (may suspend MAC), `sub_b740d` (phytype 4/7/`0x0b`), final `wlapi_bmac_read_shm(0x92)` | MAC suspend in ant | partly |
| D4.7 | caller band tail (outside `wlc_phy_init`) | `sub_62684` (txant), cwmin/cwmax, `sub_62716`, SHM `0x52/0x50`, `sub_62403`, SHM `0x3c`, `sub_627c9`, mhf, `sub_6106b`, `sub_62766` | — | yes |

For each phase, ENTRY/WRITES/ASYNC/WAIT/EXIT/NEXT are in the JSON `phases`.

For the AC (0x0b) path, note which branches are **not taken**: `aci_sw_reset_nphy`
(4), `_htphy` (7), `phy_reg_read` status reads (2), `wlc_phy_txpower_update_shm`
(0/2), `wlc_iovar_txpwrindex_set_lcncommon` (0x0a). `wlc_phy_anacore` for 0x0b
takes the generic branch.

---

## D/E. First hardware-visible operations across the boundary

Recovered ordered trace (last 10 bsinitvals writes → first `wlc_phy_init`
operations); `off` is D11 byte offset. The full 100-op expansion is bounded by
the unresolved indirect targets (`[pi+0x28]`, `[pi+0x118]`, acphy opcodes).

| # | function | PC | class | base/off | w | value/source | semantic |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| -5..-1 | `sub_60f67` | — | direct IHR | `0x680/0x682/0x684/0x686/0x700` | w2 | `0x3e3e/0x23e/0x212/0x9d0/0x3c` | IFS SIFS/slot/PAD, NAV |
| -3 | `sub_60f67` | — | OBJADDR | `0x160` | w4 | `0x00010287` | select SHM `0xa1c` |
| -2 | `sub_60f67` | — | OBJDATA | `0x166` | w2 | `0x0028` | SHM `0xa1c` high |
| -1 | `sub_60f67` | — | OBJADDR | `0x160` | w4 | `0x0001028a` | select SHM `0xa28` |
| 1 | `sub_60f67` | — | OBJDATA | `0x164` | w2 | `0x002c` | SHM `0xa28` low |
| 2 | `sub_60f67` | — | OBJADDR | `0x160` | w4 | `0x0001028a` | select SHM `0xa28` |
| 3 | `sub_60f67` | — | OBJDATA | `0x166` | w2 | `0x0028` | **last write** (SHM `0xa28` high) |
| 4 | `wlc_phy_init` | `0xbac05` | read | `[pi+0x186]` | — | software | re-entry guard |
| 5 | `wlc_phy_init` | `0xbac31` | call | — | — | — | `wlc_phy_chanspec_shm_set` |
| 6 | `wlc_phy_chanspec_shm_set` | `0xb5d3c` | SHM write | OBJ `0xa0` | w2 | chanspec | chanspec to ucode |
| 7 | `wlc_phy_init` | `0xbac44` | **D11 read** | `0x120` | 32 | `[pi+0x148]` | MACCONTROL sample |
| 8 | `wlc_phy_init` | `0xbac84` | call | — | — | — | `wlc_phy_anacore(pi,1)` |
| 9 | `wlc_phy_anacore` | `0xbabee` | **D11 write** | `0x3e6` | 16 | `0xf4`/`0x0000` | PHY0 (fallback; AC likely `[pi+0x118]` fn) |
| 10 | `wlapi_bmac_bw_set` | `0xbaca9` | call | — | — | — | BW set / PHY reset |
| 11 | `wlc_bmac_bw_set` | `0x660b5` | D11 read | `0x120` | 32 | — | MACCONTROL |
| 12 | `wlc_phy_switch_radio` | `0xbad44` | call | — | — | — | radio |
| 13 | `wlc_phy_switch_radio_acphy` | `0xaa7bb` | call | — | — | — | **MAC suspend + wait** |
| 14 | `wlc_bmac_suspend_mac_and_wait` | `0x63a6b` | D11 read | `0x120` | 32 | — | device-gone check |
| 15 | `wlc_bmac_suspend_mac_and_wait` | `0x63a7f` | D11 read | `0x128` | 32 | — | MACINTSTATUS |
| 16 | `wlc_bmac_suspend_mac_and_wait` | `0x63a92` | mctrl | `0x120` | RMW | suspend | MAC suspend |
| 17 | `wlc_bmac_suspend_mac_and_wait` | `0x63a9f` | delay+read | `0x128` | poll | bit0 | MI_MACSSPNDD |
| 18+ | `wlc_phy_switch_radio_acphy` | `0xaa80f..` | radio | radio win | w* | `mod_radio_reg` | radio PLL/synth writes (+`osl_delay`) |

**First PHY-indirect write**: `wlc_phy_anacore` (`0xbaad1`) via `[pi+0x118]`
(for AC) or the fallback `osl_writew(D11+0x3e6, 0xf4)`.
**First radio write**: `wlc_phy_switch_radio_acphy` `mod_radio_reg` at `0xaa80f`.
**First MAC handshake**: `wlc_bmac_suspend_mac_and_wait` (MACCONTROL RMW +
`MACINTSTATUS` bit0 poll) inside `wlc_phy_switch_radio_acphy`.

`wlc_phy_anacore` for AC: generic branch (`0xbabb7`): if `[pi+0x118]` non-NULL
call it; else `osl_writew(D11+0x3e6, 1?0x0000:0x00f4)`. It changes the analog
core / PHY0 register; **clocks are not demonstrably changed here**, and it
**requires immediate continuation** (radio/BW/table init follows). Not stable.

---

## F. Candidate synchronization points

| id | mechanism | vendor reaches? | quiescent? | async outstanding? | DMA live safe? | IRQ off? | teardown? | deterministic postcondition? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| SYNC1 `wlc_bmac_suspend_mac_and_wait` `0x63a04` | MAC suspend RMW + bounded poll `MACINTSTATUS` bit0 (~8289×10 µs) | yes (switch_radio_acphy, ant_rxdiv_set) | no (radio init follows) | yes (radio/synth) | yes | yes | hard | MI_MACSSPNDD, but released by resume |
| SYNC2 acphy `osl_delay` | fixed delays | yes | no | yes | yes | yes | hard | none |
| SYNC3 `phy_reg_read` `0xb2f34` | `0x3fc` write / `0x3fe` read | yes | partial | yes | yes | yes | possible | PHY reg value |
| SYNC4 final `read_shm(0x92)` `0xbae6e` | SHM read at end | yes | near-complete | caller tail remains | yes | yes | yes | SHM `0x92` |
| CP-F `wlc_phy_init` return `0x669e4` | function boundary | yes | **yes** | none inside PHY | yes | yes | yes | SHM/PHY config complete; MAC resumed |

`wlapi_suspend_mac_and_wait` also performs its own device-gone check (all-ones
MACCONTROL/MACINTSTATUS -> abort), corroborating the Part A rule.

---

## G. Why the old D3B stop was unstable (hypotheses, no causation)

- **H1 (band SHM consumed by running PSM; PHY init needed to complete transition).**
  *For:* `M_DOT11_SLOT`, `M_BCN_TXTSF_OFFSET`, `M_SYNTHPU_DLY`, rate/power tables
  are written then the vendor immediately runs PHY init; upstream
  `brcms_c_ucode_bsinit` -> `wlc_phy_init` immediate. *Against:* no proof the
  ucode acts on them while `EN_MAC=0`. *Missing:* D11 reset-cause/`M_UCODE_DBGST`
  telemetry across the window.
- **H2 (`M_SYNTHPU_DLY`/synth expects PHY continuation).** *For:* `0x94`=500 is
  the synth pre-wakeup delay and upstream rewrites it *after* PHY init. *Against:*
  the value is a vendor table value, not proven to trigger anything. *Missing:*
  microcode SHM consumer map.
- **H3 (one specific bsinitvals record invalid).** *For:* `0x17d0/0x17d4` are
  outside brcmsmac's modelled SHM; `0x686` is PAD. *Against:* the table is the
  vendor table for this chip and was applied by the vendor path. *Missing:* the
  microcode SHM map.
- **H4 (earlier OpenBRCM D3A1/T1/DMA/T2 state differs from vendor).** *For:* the
  D3A0 sequence is an *isolated* reconstruction, not the full vendor prefix
  (`sub_67efd`, NVRAM/BTC/rate/power tail). *Against:* D3A1/D3A0 were hardware
  proven in isolation; D3B ran the D3A1 prefix. *Missing:* a bit-for-bit prefix
  diff against a vendor trace.
- **H5 (device alive until OpenBRCM's own post-read; teardown only caused the
  flood).** *For:* the ~4.84 s is `ob_d3b_read_post` (35 MMIO, no software loop)
  -> completion timeouts; the crash followed `quiesce begin`. *Against:* the
  vendor never lets that interval happen. *Missing:* which layer went all-ones
  (core vs link) with persistent logging + AER.

No percentage confidences. The Part A fail-safe makes a future bounded run able
to distinguish H5/H4 from H1/H2/H3.

---

## H. Upstream `brcmsmac` corroboration

- `brcms_c_ucode_bsinit` (`main.c:680`) = `brcms_c_write_mhf` + band-specific
  IHR/SHM/SCR inits (`d11n0bsinitvals16` / `d11lcn0bsinitvals24`). Equivalent to
  the vendor `sub_62766` + `sub_60f67`.
- `brcms_b_bsinit` (`main.c:1659`) calls `brcms_c_ucode_bsinit` then
  **immediately** `wlc_phy_init`, then `brcms_c_ucode_txant_set`, cwmin/cwmax,
  slot timing, `M_PHYTYPE`/`M_PHYVER`, ofdm pctl1, `brcms_b_upd_synthpu`
  (which **rewrites** `M_SYNTHPU_DLY`).
- **No natural checkpoint exists between bsinitvals and PHY init** upstream.
- Symbolic mappings usable (do not import blindly): `M_DOT11_SLOT`, `M_SYNTHPU_DLY`,
  `M_BCN_TXTSF_OFFSET`, `M_EDCF_*`; the AC PHY symbol family
  (`wlc_phy_*_acphy`, `wlc_phy_anacore`, `wlc_phy_switch_radio_acphy`,
  `wlc_phy_attach_acphy`) exists in the vendor blob symbol table, but the AC
  table/opcode semantics are not in brcmsmac.

---

## I. Earliest valid new milestone checkpoint

| checkpoint | after | verdict | why |
| :--- | :--- | :--- | :--- |
| A | `wlc_phy_chanspec_shm_set` (D4.0) | INVALID | PHY/radio untouched |
| B | `wlc_phy_anacore` (D4.1) | INVALID | analog changed; radio/BW/table outstanding |
| C | `wlc_bmac_bw_set` (D4.2) | INVALID | possible PHY reset; radio not switched |
| D | `wlc_phy_switch_radio[_acphy]` (D4.4) | INVALID | MAC suspend released; dummy TX/txpower/ant/ACI + caller tail outstanding |
| E | before final `read_shm(0x92)` (D4.6) | POSSIBLE-WEAK | only the final read + caller tail remain |
| **F** | **`wlc_phy_init` return (`0x669e4`)** | **STRONG** | PHY/radio/synth init complete; MAC resumed; deterministic postconditions |
| G | full `sub_6656c` tail (`0x66b3a`) | STRONG-LATE | whole band-unit init complete |

**Earliest vendor-stable checkpoint = F (after `wlc_phy_init` returns).**
It is the first point at which (1) the vendor has finished the entire PHY
initialisation it started immediately after bsinitvals, (2) no interior MAC
suspend is held, (3) the PSM cannot be left holding band SHM with an
uninitialised PHY, and (4) an observable postcondition exists (PHY/SHM config +
MAC state). Checkpoints A–D are interior points that the vendor immediately
continues past; they cannot be shown stable.

---

## J. Scope estimate for checkpoint F

| dependency | rough size | status |
| :--- | :--- | :--- |
| new functions (porcelain) | 4–7 (`ob_d4_*`) | DERIVED |
| direct D11 MMIO writes | ~5–12 | PROVEN |
| indirect PHY writes | **hundreds** (`switch_radio_acphy` 4266 B, `do_dummy_tx`, `sub_b740d`) | **UNKNOWN BLOCKER** (AC opcodes/tables) |
| PHY table entries | hundreds (acphy tables) | **UNKNOWN BLOCKER** |
| radio writes | many (`mod_radio_reg`) | **UNKNOWN BLOCKER** (values/order) |
| polls | `wlc_bmac_suspend_mac_and_wait` (bounded) + acphy waits | DERIVED |
| delays | acphy `osl_delay` set | DERIVED (values in blobs) |
| firmware/board data | acphy tables, initvals | **UNKNOWN BLOCKER** |
| new teardown | needed for partial PHY state | UNKNOWN BLOCKER |
| unresolved constants | `call [pi+0x28]`, `[pi+0x118]`, acphy opcode tables | **UNKNOWN BLOCKER** |

**There are UNKNOWN BLOCKER write values up to checkpoint F.** Per the rule
("do not implement while any write value is UNKNOWN BLOCKER"), D4 implementation
is **NO GO** at this time. The indirect PHY/radio opcode tables and the two
indirect call targets must be recovered first (a D4B RE task).

---

## K. Tooling feedback / gaps

1. **Anonymous table-driven / indirect-call resolution.** `re card wlc_phy_init`
   shows `call [pi+0x28]` and `wlc_phy_anacore`'s `[pi+0x118]` only as struct
   offsets; the targets (AC PHY op fns) are unresolved. Desired:
   `re fields --struct <phy> --fnptrs` / indirect-target provenance. Closest:
   `re fields`; missing: base-pointer + struct-type resolution.
2. **Whole-blob MMIO-offset / immediate search.** Enumerating all `0x18c` refs
   earlier required `objdump`. Desired: `re refs --mmio 0x18c --width 32`.
3. **`re fn --asm` MMIO base resolution.** `wlc_phy_anacore` prints
   `osl_writew 16 ? + 0x0 = 0xf4`; desired resolution to `D11+0x3e6`.
4. **PHY-window semantic extraction.** `phy_reg_read`/`phy_reg_write` show the
   0x3fc/0x3fe window but not the AC PHY register names/values encoded in the
   callers' tables. Desired: `re phy --acphy` name/value extraction.
5. **Cross-function struct-field writer enumeration** (T1 from earlier) still
   needed to map `[pi+0x118]`/`[pi+0x28]` writers (`wlc_phy_attach_acphy`).
6. **Constant propagation through calls.** `wlc_phy_init`'s immediate `0x0b`
   phytype gates are visible, but propagated constants into callees are not.

No Rust tooling was modified in this task.

---

## L. Status

Docs/evidence only. `scripts/re-bootstrap.sh` PASS; `make hosttest` PASS;
`scripts/docs-check.sh` PASS. No Part A source change was needed, so no
rebuild/sign was required (build was already signed at `c244e98`).
