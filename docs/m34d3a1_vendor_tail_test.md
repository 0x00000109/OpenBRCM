# M3.4D3A1 — isolated vendor post-common / pre-PHY D11 tail test

Status: **`M3.4D3A1 = IMPLEMENTED / STATIC TESTED / SIGNED`,
`NOT HARDWARE PROVEN`.**

This document records the isolated implementation of the vendor BCM4352 /
D11 rev42 post-common/pre-PHY tail recovered in
[`m34d3a1_vendor_tail.md`](m34d3a1_vendor_tail.md) (`M3.4D3A1 = ANALYSIS
COMPLETE`, `D3A1 IMPLEMENTATION GO: YES`). It is **not** a normal-bring-up
stage: it is a one-shot, isolated test that STOPS before `sub_6656c`.

**No hardware execution was performed.** The module is built and MOK-signed
but never loaded in this task.

## 1. Isolated mode

Module parameter (mutually exclusive with the other isolated modes):

```
d11_tail_test_only=1
```

Description: *"isolated D3A1 vendor-ordered post-common/pre-PHY D11 tail test;
includes proven DMA lifecycle; stops before bsinitvals/PHY"*.

Exactly one isolated mode may be active. The mode policy is the pure
`ob_isolated_mode_select5()`; any combination of `fw_validate_only`,
`ucode_test_only`, `initvals_test_only`, `dma_test_only` and
`d11_tail_test_only` fails probe before any hardware access. The normal driver
path is unchanged.

## 1.1 Minimal board-data preparation (audit correction)

The D3A1 mode is dispatched **before** the normal `ob_si_probe()` path, but
`ob_d3a1_test()` needs `hw->cc` (for the PMU PLL read) and `hw->mac_valid` /
`hw->mac` (for the MAC SHM words). An earlier revision therefore refused every
D3A1 run with "no validated MAC" and would have dereferenced a NULL `hw->cc`.

The dispatch now calls `ob_si_prepare_board_data_for_d3a1(hw)` first:

- `hw->cc = hw->bus->drv_cc.core`, verified non-NULL (else `-ENODEV`);
- `hw->mac_valid = (ob_si_read_mac(hw, hw->mac) == 0)` — the already
  hardware-proven READ-ONLY external-SPROM MAC path (parser reused, not
  duplicated).

It performs **no** D11/PMU/PLL/core writes, no `ob_si_powerup()`, no
`ob_si_d11_diag()`, no `otp_diag`/`sprom_diag`, and no normal DMA/IRQ/RX/
mac80211 initialization. The only PMU PLL accesses remain inside the exact
`switch_macfreq`. `ob_d3a1_test()` additionally refuses with `-ENODEV`
(no ChipCommon) or `-EINVAL` (no validated MAC) before any D3A1 write.

The helper preserves the exact `ob_si_read_mac()` errno: a bad CRC, an invalid
MAC or an allocation failure returns the original error from board-data
preparation **before** D2A/D2B, rather than flattening it into
`mac_valid=false` with a success return.

## 1.2 Final pre-freeze corrections

- **`switch_macfreq` entry guard** — the `!hw->cc` check now precedes
  `ob_d3a1_bb_vcofreq()` (which dereferences `hw->cc` via
  `ob_si_cc_read/write`). The `ob_d3a1_test()` entry invariant is kept as well.
- **`ob_si_read_mac()` error preserved** — see §1.1.
- **FIFO timeout diagnostics** — the `0x530` log now reports
  `index`, `programmed`, `last` (the final hardware readback) and `reads`; the
  `0x540` log reports `programmed`, `last` and `reads`.
- **PLL/VCO observability** — the first run records `pll2_raw`, `pll3_raw`
  (only when actually read), `pll3_read`, `bb_d=(PLL2>>4)&7`, `bb_den=PLL2>>7`,
  the derived VCO, the derived TSF fraction and the programmed `0x62e`/`0x630`
  values. No extra PMU reads are made for logging (the values come from the
  algorithm itself). `0x62e`/`0x630` are read back for diagnostics only and
  classified **UNPROVEN** (`tsf_frac_rb_proven=false`); no equality gate is
  invented because stable readback semantics are not vendor-proven.

## 2. Exact D3A1 call graph (implemented)

```
OB_ISOLATED_D3A1_TEST dispatch:
  ob_si_prepare_board_data_for_d3a1(hw) -> hw->cc + validated SPROM MAC
  ob_d3a1_test(hw):
    ob_d3a0_fatal_is_latched()          -> refuse after an unverified quiesce
    rev42 check; hw->cc check; mac_valid check
    ob_initvals_run_d2b("d3a1-test")    -> proven D2A core + 610 common initvals + gate
    ob_d3a0_d2b_state_ok(post)          -> D2B postconditions
    ob_d3a1_check_d2b_exit(hw)          -> LIVE re-check before first D3A1 write
    ob_d3a1_fifo_fixup(hw)              -> sub_67efd equivalent (verified polls)
    ob_d3a1_t1(hw)                      -> T1
    ob_d3a0_bringup(hw)                 -> DMA in vendor position (reused D3A0)
    ob_d3a1_t2(hw)                      -> T2 (BTC/MAC/switch_macfreq)
    ob_d3a1_validate(hw)                -> deterministic postconditions
    ob_d3a0_teardown(hw)                -> mandatory verified quiesce + free
    STOP  (log: STOPPED BEFORE sub_6656c / bsinitvals / PHY)
```

The vendor interleaving `T1 -> DMA -> T2` is preserved; DMA is **not** appended
before or after the tail. The DMA sub-lifecycle is the hardware-proven D3A0 one
(`ob_d3a0_bringup` / `ob_d3a0_teardown`), reused unchanged.

## 3. `sub_67efd` implementation scope (rev42 only)

Only the operations the blob executes for BCM4352 rev42 (`phyrev == 0x2a`):

- `machwcap = read32(D11+0x15c)`; `v = (machwcap >> 1) & 0xffc`; `0x542 = v`.
- `0x540 = 5` + bounded completion poll (`0x540` bit0 == 0; bound `0xd1`,
  step 10, ≤ 21 reads).
- The RXE block (`0x42c/0x42e/0x43a/0x43c/0x406`) is **not** executed (rev42
  gate `phyrev > 0x2a`).
- Exact 7-entry loop over the blob table `{7,0,1,2,3,4,5}`, 6 writes each
  (`0x54a/0x54c/0x520/0x54e/0x550/0x548`): entry `7` uses the `0x2a` group;
  rev42 entries use `rqpri = v - 0x2a`, `0x20`, `def = 0xb`, `0x54e =
  0x1216`, `0x550 = 0x740c`, `0x548 = entry | 0x10`. **42 writes.**
- Exact 42-entry loop (`idx 0..41`): `0x534 = idx`, `0x536 = min(idx+2,0x29)`,
  `0x532 = min(42 - idx, 3)` (the vendor's `min(idx+2,0x29) + r13d`, with the
  loop counter `r13d` initialised to 1 and `dec r13d` per iteration),
  `0x530 = (idx<<4)|0x8007`, bounded poll (`0x530 == 0`). **168 writes.**
- Total accounting: `2 + 42 + 168 = 212` writes, asserted at runtime
  (`ob_d3a1` counters). BCM4352/rev42-specific; no generalization.

### 3.1 Completion predicates and timeout semantics (proven from the blob)

Re-derived directly from the machine instructions (object file `wlc_hybrid.o`):

- `0x530` loop `0x6824d..0x6825e`:
  `call osl_readw` (`rdi = &0x530`), `66 85 c0` = `test %ax,%ax`,
  `74 06` = `je done`, `cmpl $0x9,counter`, `jne loop`. Completion is the
  **whole 16-bit word reading 0** (`read == 0`), not a `0x8000` mask.
- `0x540` loop `0x68033..0x68044`:
  `call osl_readw` (`rdi = &0x540`), `a8 01` = `test $0x1,%al`,
  `74 06` = `je done`, `cmp $0x9,%r14d`, `jne loop`. Completion is
  **bit0 clear** (`(read & 1) == 0`). The two registers do **not** share a
  predicate.
- Both loops initialise the counter to `0xd1` and decrement by `10`, exiting
  when it reaches `9` (21 reads). **The vendor has no error path**: on expiry
  it falls through and continues; `sub_67efd` always returns 0.

OpenBRCM reproduces the bound and the exact predicates, and treats expiry as a
recorded, **non-fatal** event (`dev_warn` + `fifo_poll_expired`), matching the
vendor control flow. `0x62e`/`0x630` readback is not involved here. There is
**no** `0x8000`-mask predicate and **no** `0x0007` special case.

### 3.2 First hardware attempt (historical provenance — superseded candidate)

- Candidate `1186a9b2479a185c1e438ebb43b4a2bd896fc5b7`, module
  `ce9b7cc5e7ed3564f402a3af37f4eda99eb41620c191ad309a5e0687233f27a0`.
- `d11_tail_test_only=1`; board-data preparation PASS (MAC
  `2c:fd:a1:61:40:25`); D2A PASS (ucode writes 10850, PSM poll PASS,
  `MI_MACSSPNDD=1`); D2B PASS (common initvals 610, w16 113, w32 497,
  `MACCONTROL=0x04020402`, `MACINTMASK=0`, `SHM[0x14]=0x00b4`).
- `sub_67efd` then reported: `0x530 index=0 programmed=0x8007
  final readback=0x0007 reads=21`, and the candidate aborted with
  `-ETIMEDOUT` before T1/DMA/T2.
- Root cause: the `0x530` predicate was already exact (`read == 0`); the
  candidate aborted on a poll that the vendor treats as a bounded,
  **non-fatal** wait. The low `0x0007` bits are simply not a state the vendor
  gate accepts, and the vendor proceeds anyway.
- This candidate is **historical evidence only**; it is not the frozen
  candidate and must not be reused. A corrected candidate gets a new commit and
  a new module SHA256.

## 4. Exact T1 write order (implemented)

`ob_d3a1_t1()` in vendor order:

1. SHM `M_MBURST_SIZE`(0x80) = 8; SHM `M_MAX_ANTCNT`(0x5c) = 0x0a.
2. `INTRCVLAZY[0]`(0x100) = 0x01000000.
3. MACCONTROL masked transition `0x04020402 -> 0x44020402` (mask
   `0x40060000`, value `0x40020000`), checked exactly.
4. `tsf_cfprep`(0x188) = 0x80000000; `tsf_cfpstart`(0x18c) = 0x02000000.
5. `macintstatus`(0x128) W1C `MI_GP1` = 0x4000.
6. `intctrlregs[0].intmask`(0x24) = `I_RI` = 0x10000.
7. `macphyclk_set(ON)` (SICF_MPCLKE, D11 core cflags bit 4).
8. fast-pwrup path (rev42): `si_clkctl_fast_pwrup_delay()` equivalent =
   `si_pmu_fast_pwrup_delay()` for 0x4352 = 1500 (`chiprev < 4`) / 3000
   (`chiprev >= 4`); `write16(D11+0x6a8, dly)`; the software copy
   (`dev+0x192` equivalent) then adds `sub_5fdca()` = 0x4b0 (phytype 0xb,
   chip != 0x4350). The register write uses the PMU value only (vendor order).
9. SHM `M_MACHW_VER`(0x16) = phyrev.
10. `machwcap`; SHM `M_MACHW_CAP_L/H`(0xc0/0xc2) = machwcap lo/hi.
11. SCR `SRL`(0x18) / `LRL`(0x1c) — **SCR window** (objmem selector 0x20000).
12. **SCR `0x24` is not written**: on the initial bring-up the vendor
    `wlc_info+0x718` first-init gate (`wlc_info_init` sets it to 1) clears it
    and skips the `copyto_objmem(0x24)` restore.
13. SHM `M_SFRMTXCNTFBRTHSD`(0x44) / `M_LFRMTXCNTFBRTHSD`(0x46).
14. rev42 IFS: `0x688 &= 0xfff`; `write16(0x69c, 1)`.

No reordering for code convenience.

## 5. DMA position

`ob_d3a0_bringup()` runs **after** T1 and **before** T2: four TX rings,
FIFO0 RX (64 × 2048 B), 32-bit Linux DMA window, `ADDRHIGH = 0x80000000`,
`RX CONTROL = 0x84d`, programmed producer PTR `0x400`, no TX payload
descriptors, no `MI_DMAINT`, no host IRQ route.

## 6. T2 behavior

`ob_d3a1_t2()`:

- `btc_base = 2 * read_shm(0x92)`.
- **If `btc_base == 0` the complete BTC block is skipped** — and, following the
  vendor branch at `0x69336` (jump to `0x694d8`), this also skips the six-byte
  MAC writes at `0x69466`. This is a correction to the earlier analysis, which
  treated the MAC writes as unconditional.
- If `btc_base != 0`:
  - `btc_params0..118`: no OpenBRCM NVRAM text-variable provider exists and the
    SPROM does not emit these keys, so every `getvar("btc_params%d")` is absent
    and the vendor behavior is to **skip the write** (never zero-fill). No write
    is issued.
  - `btc_flags`: absent ⇒ no `btc->flags` store and **no `wlc_bmac_mhf`
    writes**; no synthesized flags.
  - chip `0x4352`/`0xa8dc`: the 4 fixed extras `btc_base+2/0x10/0x12/0x2c` =
    `0x7530/0x4e20/0x7530/0x753`.
  - rev42 (`phyrev > 0x27`) with a validated MAC: SHM `0x78c/0x78e/0x790` =
    the MAC big-endian pairs `(mac[0]<<8)|mac[1]`, `(mac[2]<<8)|mac[3]`,
    `(mac[4]<<8)|mac[5]`, derived programmatically from `hw->mac` (never
    hardcoded).
- Read-only diagnostic of SHM `0x8e`.
- `switch_macfreq` for chip `{0xa9c4,0x4360,0xaa06,0x4352,0x4350}`.

## 7. MAC -> SHM 0x78c/0x78e/0x790

For the proven ASUS PCE-AC56 MAC `2c:fd:a1:61:40:25` the derived words are
`0x2cfd`, `0xa161`, `0x4025`. The implementation always derives them from
`hw->mac` via `ob_d3a1_mac_word()` and logs them.

## 8. `switch_macfreq` (BCM4352 rev3 path) — exact, no approximation

The earlier revision contained a documented "best-effort" PLL3 fold. That has
been replaced by **verbatim ports of the vendor machine code**:

- `ob_d3a1_muladd()` = blob `bcm_uint64_multiple_add` (0xac8f): 64-bit
  `a*b + c` (out_hi/out_lo). Verified against the extracted vendor machine code
  over **200000 random inputs**.
- `ob_d3a1_u64_divide()` = blob `bcm_uint64_divide` (0xad85): the vendor's
  32-bit-limb long division (NOT a plain 64-bit divide). Ported instruction by
  instruction and verified against the extracted vendor machine code over
  **288 directed + 50000 random vectors**. `b <= 1` writes nothing
  (`OB_D3A1_DIV_NO_WRITE`).
- `ob_d3a1_bb_vcofreq_from_pll(p2, p3)` = blob `si_pmu_get_bb_vcofreq`
  (0x14b7b), chip 0x4352 branch:
  `d=(p2>>4)&7`, `den=p2>>7`, `q=0x28*10000`, and, when `d != 0`,
  `esi=(lo>>24)|(hi<<8)` with `{hi,lo}=muladd(q,p3,0x800000)`; reject to 0 when
  `(s32)q > (s32)(~esi/den)`; else `esi + den*q`. `den == 0` returns 0.

`ob_d3a1_switch_macfreq()` reads PMU PLL2 (and PLL3 when `d != 0`) through
chipcommon `0x660/0x664` (requires a non-NULL `hw->cc`), then
`frac = ob_d3a1_u64_divide(0x3a9, 0x80000000, vco)` and writes
`0x62e = frac[15:0]`, `0x630 = frac[31:16]`.

`vco <= 1` is treated as an **ERROR** (`-EIO`), not a skip: the vendor
`bcm_uint64_divide` writes nothing for `div <= 1` and would publish an
uninitialized stack value. Our inability to derive a usable VCO must never
become a D3A1 PASS.

## 9. STOP boundary

The test stops immediately before `sub_6656c`. It never calls `sub_6656c`,
the five pre-bs `write_shm` calls, `sub_60f67(bsinitvals42)`, `wlc_phy_init`,
`wlc_phy_anacore`, radio, channel or calibration. The success log explicitly
states `STOPPED BEFORE sub_6656c / bsinitvals / PHY`.

## 10. Postconditions (validated before teardown)

`ob_d3a1_validate()` requires, on the live hardware:

- `MACCONTROL == 0x44020402`; `MACINTMASK == 0`; `INTRCVLAZY[0] == 0x01000000`;
  `intctrlregs[0].intmask & I_RI`; `EN_MAC == 0`; SICF_MPCLKE set.
- SHM `M_MBURST_SIZE == 8`, `M_MAX_ANTCNT == 0x0a`, `M_MACHW_VER == phyrev`,
  `M_MACHW_CAP_L/H == machwcap` lo/hi.
- **Exact** `tsf_cfprep == 0x80000000`, `tsf_cfpstart == 0x02000000`,
  `scc_fastpwrup_dly ==` the chiprev-derived value, SCR `SRL == 7`,
  SCR `LRL == 6`, SHM `SFBL == 3`, SHM `LFBL == 2`, `ifs_aifsn == 1`.
- **Masked** `ifs_ctl & ~0xfff == 0` (the vendor clears the high bits; the low
  12 bits are the pre-existing value).
- SHM `0x78c/0x78e/0x790 ==` the derived MAC words **when the vendor gate
  (`btc_base != 0`) would have written them**.
- DMA: `RX CONTROL == 0x84d`, `RX ADDRHIGH == 0x80000000`, RX state IDLE,
  `RX STATUS1` error bits 0 (the full TX/RX validation runs inside
  `ob_d3a0_bringup`).

No raw-equality test on hardware-current PTR/status pointer fields.

## 11. Teardown / fatal model

The proven D3A0 lifecycle is reused: the success path quiesces in the same run
(MACINTMASK = 0, clear `I_RI`, RX reset + bounded verify, TX0..TX3 reset +
bounded verify, verify all initialized engines stopped, unmap 64 RX buffers,
free skbs, release rings, destroy the DMA pool). Free is permitted **only**
after every programmed engine was verified stopped. A failed reset attempts
`bcma_core_disable` as containment only (never a free permit), latches a
module-wide fatal state, pins the module, retains the DMA memory and requires a
reboot; no retry. `ob_d3a1_remove()` refuses to free in the fatal state.

Every failure before DMA returns normally (no resources to release beyond the
reused prefix). After any DMA engine is programmed, the mandatory verified
teardown runs. No path frees a ring before its engine is verified stopped,
unmaps RX while RX may reference it, silently falls through into normal probe,
calls `sub_6656c`/PHY, or registers mac80211.

## 12. Regression protection

`fw_validate_only`, `ucode_test_only`, `initvals_test_only` and
`dma_test_only` are behaviourally unchanged. `dma_test_only` still runs the
isolated D3A0 DMA lifecycle test; the new D3A1 path is a separate mode. The
normal driver path is untouched; all pre-existing host tests still pass.

## 13. Tests

- `tests/host/ob_d3a1_test.c` (PASS): mode conflict (5-mode), D3A1 order model
  (DMA between T1 and T2, STOP before `sub_6656c`), `sub_67efd` rev42 branch,
  7-entry count (42), 42-entry count (168), total 212, MACCONTROL transition,
  MAC -> SHM word conversion, SCR `0x24` first-init skip, `btc_base == 0` skip,
  absent `btc_params`/`btc_flags` skip, DMA teardown/fatal lifecycle, and the
  new **exact** vectors: `ob_d3a1_muladd` (=`a*b+c`), `ob_d3a1_u64_divide`
  (0x80000000/0xbffffc57/0xa0000000/0x00999999/0xb6db7a11 + no-write),
  `ob_d3a1_bb_vcofreq_from_pll` (0x03072580/0x030725ef/0/0), exact
  `ob_d3a1_tsf_frac`, and the poll-bound predicate. It also pins the corrected
  `x532 = min(42-idx,3)` (3 at idx 0/1/2/3/39, 2 at 40, 1 at 41) and the exact
  per-register completion predicates: `ob_d3a1_fifo530_done` is whole-word
  zero (`0x0000` done, `0x8007`/`0x0007`/`0x8000` not done) and
  `ob_d3a1_fifo540_done` is bit0 clear (`0x0004`/`0x8000` done, `0x0001`/
  `0x0007` not done); `0x0004` is the value that proves the two predicates
  differ.
- Static regression guards in `scripts/docs-check.sh`: the D3A1 dispatch must
  call `ob_si_prepare_board_data_for_d3a1(hw)` before `ob_d3a1_test(hw)`; the
  prep must set `hw->cc` and call `ob_si_read_mac`; the prep body must contain
  no power-up/diag/register writes; `ob_d3a1_test` must refuse without `hw->cc`
  and without a validated MAC; and no `best-effort` may appear in D3A1.
- `tests/kunit/ob_d3a1_kunit.c`: mirror. **KUnit is not wired into the build in
  this environment** (no `kunit` Makefile target), so the mirror is not
  executed here.

## 14. Build / sign record

- `make clean && make`: success (kernel `7.0.0-34-generic`).
- `make hosttest`: PASS (10/10 suites).
- `scripts/docs-check.sh`: PASS.
- checkpatch on all changed C/H files: clean.
- `make signed`: module signed (`signer: Broadcom Driver MOK`,
  `sig_hashalgo: sha256`). **Not loaded.**

| field | value |
| :--- | :--- |
| module | `openbrcm.ko` |
| size | 4170009 B |
| sha256 | `6ba2d853adef9860213498c32c8968bdbb027e59ac17ffe8a2c2670503ff5abd` |
| vermagic | `7.0.0-34-generic SMP preempt mod_unload modversions` |
| srcversion | `011D0C80396320496A86768` |
| signer | `Broadcom Driver MOK` |
| depends | `mac80211,bcma` |

## 15. Known limitations (documented, non-blocking)

1. No OpenBRCM NVRAM text-variable provider: `btc_params`/`btc_flags` are
   absent and the vendor-equivalent path is the deterministic skip. If such a
   provider is added later, `ob_d3a1_t2()` can query it without further RE.
2. SRL/LRL/SFBL/LFBL use the vendor/upstream attach defaults (SRL 7, LRL 6,
   SFBL 3, LFBL 2) because the isolated test has no NVRAM retry-limit source.
3. The symbolic name of the SHM `0x78c/0x78e/0x790` slots remains UNKNOWN
   (value/source proven).

The TSF postcondition (`tsf_cfprep`/`tsf_cfpstart` equality) assumes those
registers read back the programmed value; if the first hardware run shows them
to be force/command registers that auto-clear, they will be downgraded to
diagnostic-only. All other postconditions are static.

D11 `0x62e`/`0x630` stable readback is **not** vendor-proven; the first run
logs the readback and classifies it as unproven (`tsf_frac_rb_proven=false`).
No equality postcondition is placed on them.

## 15.1 Failure matrix

| case | D3A1 writes | DMA live | action |
| :--- | :--- | :--- | :--- |
| A. board-data/MAC failure | none | no | return error (`-ENODEV`/`-EINVAL`) |
| B. D2A/D2B failure | none | no | existing proven policy (return error) |
| C. sub_67efd poll expiry | partial FIFO | no | vendor-non-fatal: log + continue; no abort |
| D. T1 failure before DMA | partial T1 | no | return error; residual D11 state documented |
| E. DMA/T2/validation failure | full tail | yes | mandatory verified D3A0 teardown |
| F. DMA reset unverifiable | full tail | yes | fatal latch, retain memory, pin module, reboot only |

## 16. Future hardware command sequence (PREPARED ONLY — DO NOT RUN)

Requires explicit human approval. No command in this section was executed.

```sh
# 0. build + sign (already done)
make signed
modinfo openbrcm.ko | grep -E 'vermagic|signer'

# 1. ensure no other openbrcm instance and the D11 core is free
lsmod | grep openbrcm || true

# 2. load the D3A1 isolated mode (exactly one isolated mode)
sudo insmod openbrcm.ko d11_tail_test_only=1

# 3. inspect the dmesg evidence (bring-up, sub_67efd accounting, T1, DMA,
#    T2, postconditions, verified teardown, STOP line)
sudo dmesg | grep -E 'd3a1-test|openbrcm'

# 4. unload (fail-closed teardown already ran on the success path)
sudo rmmod openbrcm

# 5. if the fatal latch is ever observed, DO NOT rmmod freely: reboot.
```
