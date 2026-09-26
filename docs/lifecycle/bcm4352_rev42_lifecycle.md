# BCM4352 rev42 AC lifecycle reconstruction

**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no driver code, no candidate.
Baseline `re` v4 (`0c7cf875…`), `re.db` `44dae60d…`. Corpora: `wlc_hybrid.o_shipped`,
`wl.ko` (unstripped, identical addresses), `wl7-64.sys`, `wl10-64.sys`.
Machine-readable: [`bcm4352_rev42_lifecycle.json`](bcm4352_rev42_lifecycle.json).

## 1. CP-O2 temporal contradiction — resolved

The D4C report was internally inconsistent: it placed the radio-ON
(`wlc_bmac_radio_hw`) in the attach phase (before D2/D3) yet described CP-O2 as
"PSM running (D2A), DMA initialized (D3A0)".

**Resolution:** `wlc_bmac_radio_hw` is an **RPC-dispatched function**
(`WLRPC_WLC_BMAC_RADIO_HW_ID`) with **no caller anywhere in the corpus**. It is
not executed by the vendor Linux attach/init path, so there is no vendor-time
CP-O2 to order. The D4C CP-O2 state was an unjustified inference.

| state | meaning |
| :--- | :--- |
| **A. vendor entry state** | `N/A / UNKNOWN` — no vendor caller, no defined phase |
| **B. vendor exit state** | `N/A / UNKNOWN` |
| **C. proposed OpenBRCM post-D3 invocation** | PSM running (D2A), DMA engines initialized (D3A0), MAC per initvals, then radio ON |
| **D. vendor-faithful?** | **No** — C is hypothetical; the vendor never invokes it in-corpus |

## 2. Where `wlc_bmac_radio_hw` sits

- The function is `FUNC LOCAL`, 290 B, at `0x63e2a`; unique symbol, no alias.
- It is **not** called by `wlc_bmac_attach`, nor by any function in
  `wlc_hybrid.o_shipped` or `wl.ko`.
- It calls `si_pmu_radio_enable` → `wlc_phy_anacore(1)` →
  `wlc_phy_switch_radio(1)` (AC ON). Its own `on` arg is `arg1` (`r13b=sil`).
- So it is part of the **hardware-layer API surface**, not a call inside attach.

## 3. External caller

| corpus | method | result |
| :--- | :--- | :--- |
| `wlc_hybrid.o_shipped` | `re card`, Ghidra Refs | 0 callers / no refs |
| `wl.ko` | `readelf -r`, symbol/alias scan | no relocation, unique LOCAL symbol |
| `wl7-64.sys` / `wl10-64.sys` | strings | `WLRPC_WLC_BMAC_RADIO_HW_ID` present |

`WLRPC_WLC_BMAC_RADIO_HW_ID` proves the function is an **RPC target** in the
offload (`bcm_ol`) architecture. The dispatcher / peer is not in the available
corpora. The caller layer is therefore the **offload/RPC layer (or peer
firmware)** — not the wl wrapper, not the hardware attach layer, not the PHY
module.

`wlc_init` (the D2/D3 phase entry) is called from `wl_init` (`wl.ko 0x184a60`,
`R_X86_64_PLT32`), i.e. the OS adaptation layer.

## 4. API contract

`wlc_bmac_radio_hw(void *hw, bool on, bool arg2)`

- arg0 `hw`: `sih @+0xb8`, `d11 @+0xd0`, `band @+0xe8`, common `@+0x20`.
- arg1 `on`: radio enable/disable.
- arg2: skip `wlc_phy_anacore` when true (`0x63e9e`).
- callsites in corpus: **0**. Frequency / attach / init / band / suspend /
  down-up: **none proven** (RPC-driven).

## 5. Chronological lifecycle

```
L01 PCI probe / module init .............. UNKNOWN (outside corpus)
L02 SI/BCMA attach ........................ KNOWN (M2)
L03 SPROM/NVRAM parse ..................... KNOWN (M2)
L04 PHY object attach wlc_phy_attach(0) ... KNOWN
L05 band structure attach ................. KNOWN
L06 CP-A3 (radio OFF) ..................... KNOWN
L07 dma_attach (allocation) ............... KNOWN
L08 wlc_init -> wlc_bmac_init ............. KNOWN
L09 chanspec/band set ..................... KNOWN (0x6833b)
L10 wlc_phy_cal_init (AC no-op) ........... KNOWN
L11 ucode upload / PSM start (D2A) ........ KNOWN
L12 common initvals (D2B) ................. KNOWN
L13 D11 tail sub_67efd (D3A1) ............. KNOWN
L14 DMA + NVRAM/BTC tail (D3A0) ........... KNOWN
L15 switch_macfreq ........................ KNOWN
L16 MHF + bsinitvals (D3B) ................ KNOWN
L17 wlc_phy_init (AC no-op) ............... KNOWN
L18 wlc_up / up_prep / hw_up / up_finish .. KNOWN
L19 IRQ enable ............................ KNOWN
L20 wlc_bmac_radio_hw(1) radio enable ..... EXTERNAL_UNKNOWN (RPC; not orderable)
L21 channel programming (runtime) ......... KNOWN
L22 RX/TX operational ..................... UNKNOWN
```

Only L20 is not orderable; everything else is host-code ordered.

## 6. H_NEW

**Not supported.** There is no vendor attach/init radio call to omit: the only
AC on=1 site is an RPC target with no in-corpus caller, and `wlc_phy_init`'s
on=1 is AC-unreachable. The D3B crash cannot be attributed to a missing early
radio init on this evidence.

- FOR: the radio-ON branch code is large and radio-only.
- AGAINST: zero callers in the hybrid object and the linked module; no
  relocation; unique LOCAL symbol; `WLRPC_WLC_BMAC_RADIO_HW_ID` shows RPC.
- MISSING: the `bcm_ol` RPC dispatcher object / peer firmware lifecycle.

## 7. Early-radio persistent state

None. No early radio-on executes in-corpus, so categories A–E are all empty; no
later read/branch consumes state produced by `wlc_bmac_radio_hw`.

## 8. Is early radio init required on BCM4352?

No dependency evidence exists. For every "before X" question (ucode, PSM,
initvals, DMA, bsinitvals, `wlc_phy_init`) the answer is `UNKNOWN`, and ordering
alone is not a dependency.

## 9. MAC state

`wlc_bmac_radio_hw` does not enable the MAC in-corpus (not called). EN_MAC,
PSM_RUN and suspend/awake belong to `wlc_bmac_init`/`wlc_bmac_up_*`. "MAC
enabled" at CP-O2 is withdrawn.

## 10. DMA state

At CP-A3: `dma_attach` has **allocated** rings, engines are **not initialized**;
engine init (`dma_txinit`/`dma_rxinit`) is in the D3A0 tail of `wlc_bmac_init`.
CP-O2's "DMA initialized" is withdrawn (there is no CP-O2).

## 11. CP-O2 reassessment

`EXTERNAL-API CHECKPOINT / NOT ORDERABLE FROM CURRENT CORPUS`. Not
vendor-reachable, not observable, not a usable milestone.

## 12. Implementation consequences

- **MODEL A** (board → D2 → D3 → later PHY) is the evidence-supported in-corpus
  host lifecycle.
- **MODEL B** (early PHY/radio before D2) is **not supported**; the vendor makes
  no such call.
- Smallest pre-D2 isolated milestone: **none**.

## 13. Tooling

`re`/`re.db` cannot represent external/cross-binary/RPC-dispatch call edges. A
fact as important as "this function is an RPC target with no in-object caller"
has no DB representation. Recorded as a reusable gap (no DB change made; not
required to answer this milestone). `bcm4352_recover.py` remains the tool for
wl.ko↔hybrid symbol recovery.

## Erratum to D4C

See the erratum appended to
[`../m34d4b/d4c_radio_on_transition.md`](../m34d4b/d4c_radio_on_transition.md):
CP-O2 is withdrawn; the radio-ON branch is code-reachable but not
vendor-executed in this corpus.
