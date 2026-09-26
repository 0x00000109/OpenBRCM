# M3.4D4D — PLL/synth selector provenance (`pi+0x16e`)

Status: **ANALYSIS COMPLETE / `HARDWARE_REQUIRED`** (no hardware, no MMIO, no
driver change). Blocker `d4.pll_synth.rev42.branch`
(task alias `D4-BLOCKER-PLL-SYNTH-REV42-SELECTOR`).

Machine-readable: [`pll_selector_provenance.json`](pll_selector_provenance.json),
[`radio_identity_map.json`](radio_identity_map.json),
[`pll_decision_matrix.json`](pll_decision_matrix.json),
[`checkpoint_candidates.json`](checkpoint_candidates.json).

## 1. Result

**Classification C — HARDWARE_REQUIRED.** The first-radio-ON PLL/synth branch
(`wlc_phy_switch_radio_acphy`) is selected by `pi+0x16e`, which is the
**2069 radio revision class = `radio_rev >> 4`**, decoded from a read-only
radio identity register. No vendor constant, board field or NVRAM/SPROM value
pins it. The two branches are now mapped exactly; only the board's numeric 2069
revision is missing.

| selector | 2069 rev | branch |
|---|---|---|
| `pi+0x16e == 1` | 16..31 | **sequence A** (`0x80b` family) |
| `pi+0x16e == 2` | 32..47 | **sequence B** (`0x60c` family) |
| other (0 / 15) | 0..15 / 254 | both skipped, converge at `sub_9fb72` |

**No guessed default is recorded.**

## 2. Semantic identity (corrected name)

`pi+0x16e` is **not** a standalone "radio revision". In `wlc_phy_attach` (AC
path, `phytype==0x0B`):

```
osl_writew(pi+0x148 + 0x3d8, 0); r13 = osl_readw(pi+0x148 + 0x3da); // radio reg 0
osl_writew(pi+0x148 + 0x3d8, 1); r14 = osl_readw(pi+0x148 + 0x3da); // radio reg 1
edx = (r13 << 16) | r14;
pi+0x16a = edx & 0xffff;            // == r14 = radio id (0x2069)
pi+0x16c = (edx >> 16) & 0xff;      // == r13 & 0xff = full 2069 revision byte R
pi+0x16e = (edx >> 20) & 0xff;      // == (r13 >> 4) & 0xff = R >> 4   <-- selector
pi+0x16f = (edx >> 16) & 0x0f;      // low revision nibble
```

`0x3d8` is the radio indirect address latch and `0x3da` the 16-bit data port
(exactly what `read_radio_reg` uses; the alternate window `0x3f6/0x3f8/0x3fa`
is not taken on this board because `[[pi+0x20]+0x28]` is the D11/PHY rev 42,
not `0x1B`/`<=0x17`). Radio reg 1 holds the radio id (`0x2069`); radio reg 0
holds the revision byte `R`, so `pi+0x16e = R >> 4`.

Corroboration that this is the 2069 revision: `sub_9fb72` compares
`pi+0x16a == 0x2069` and then indexes a jump table with `pi+0x16c - 3` to pick
`prefregs_2069_rev3/4/16/17/18/23/24/25/26/33_37`; per-class branches on
`pi+0x16e` drive `wlc_phy_radio_override_acphy` (`ovr_regs_2069_rev2/16/32`),
`wlc_phy_attach_acphy` (OTP fallback), tempsense, temp-throttle, RX-IQ,
TX-tone and spur-mode. The accepted AC radio ids are `{0x030B, 0x2069}`.

## 3. Producer / overwrite / order

- **Producer:** `wlc_phy_attach @0xbeff8` (single AC store).
- **Other store:** `wlc_phy_periodic_cal_lpphy` (16-bit, LPPHY only).
- **radio reg 0 writes:** only `sub_10a6a0 @0x10a6dc` (non-AC `>=0x100000`);
  on the AC path radio reg 0 is hardware identity.
- **Order:** pi memset-zero → `phytype/phyrev` from PHY reg `0x3e0` →
  radio reg 0/1 read → radio-id/rev validation (mismatch ⇒ `switch_radio(0)` +
  attach error) → `wlc_phy_switch_radio(pi,0)` → … → `wlc_phy_init` →
  `wlc_phy_switch_radio(ON=1)` reads the byte.

## 4. Why not statically closable

- `pi+0x16e` is read from the radio die, not assigned by any board input.
- No `radiorev`/`radioid` NVRAM or SPROM override string exists in the blob.
- The validation path **fails attach** on a bogus id/rev (no default fill).
- Therefore the numeric rev (and hence A vs B) is **HARDWARE_REQUIRED**.

## 5. Read-only probe (designed, NOT implemented/executed)

```
osl_writew(pi+0x148 + 0x3d8, 0); v0 = osl_readw(pi+0x148 + 0x3da);
osl_writew(pi+0x148 + 0x3d8, 1); v1 = osl_readw(pi+0x148 + 0x3da);
selector = (v0 >> 4) & 0xff;   rev = v0 & 0xff;   radio_id = v1;
```

- 2 writes + 2 reads, no polling.
- The `0x3d8` address-latch write is unavoidable (the data port is only
  meaningful for the latched register); it does not change radio config.
  A strictly write-free observation is **impossible**.
- Earliest already-proven safe point: after the D2B common-initvals exit (or the
  D3A1 tail). Needs D11 core enabled + PHY clock (both already proven).
- Guard integration: route through `ob_d11_read16/write16`; a `0xffff`/`0xffff`
  pair is device-loss, not a revision.

## 6. Tooling gaps filed

`R-G1` cross-function struct-field **reader** enumeration (only a whole-`.text`
`objdump` scan answered it) — HIGH_REUSE. `R-G2` hardware-bitfield → object-field
provenance / revision decision matrix — HIGH_REUSE. `R-G3`/`R-G4` overlapping
ELF symbols and st_size-truncated table sizes — LOW_REUSE. None blocked the
analysis; no tooling was changed.
