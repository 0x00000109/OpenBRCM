# M3.4C.1 — rev42 firmware reconciliation (analysis only)

Scope: **no hardware writes, no firmware upload, no RX-DMA changes.**
Sources: upstream b43 (`/usr/src/linux-7.2.2-build/.../b43`, C3),
b43-fwcutter 019 (binary, C3), vendor blob `wlc_hybrid.o_shipped` (C2),
staged files in `/lib/firmware/brcm`.

## 0. Verdict

BCM4352 / D11 rev42 is **not** a firmware blind spot. Upstream b43 has an
explicit rev42 + AC-PHY path (`ucode42`, `ac1initvals42`, `ac1bsinitvals42`),
and our vendor blob contains those exact images for the very rev42 path we are
reverse-engineering. The three images are **already locally present**. What is
*not* true is byte-compatibility with b43: our files are in the **vendor native
8-byte record format**, not b43's on-disk IV format, and their sizes differ from
b43-fwcutter's canonical images (different driver version). Recorded, not
silently substituted.

---

## 1. Upstream b43 rev42 firmware selection (C3)

`b43_try_request_fw()` — `b43/main.c:2297`.

| item | value | source |
|---|---|---|
| condition | `rev == 42 && phy->type == B43_PHYTYPE_AC` | main.c:2309-2311 |
| `B43_PHYTYPE_AC` | `0x0b` | b43.h:429 |
| microcode | `ucode42` | main.c:2310-2311 |
| initvals | `ac1initvals42` | main.c:2446-2447 |
| bandswitch initvals | `ac1bsinitvals42` | main.c:2508-2509 |

- Filename resolution (`b43_do_request_fw`, main.c:2215-2229): proprietary →
  `"b43%s/%s.fw"` → `b43/ucode42.fw`, `b43/ac1initvals42.fw`,
  `b43/ac1bsinitvals42.fw`; opensource → `b43-open%s/%s.fw`.
- Container `struct b43_fw_header` (b43.h:635-644, `__packed`):
  `u8 type` (`'u'`/`'p'`/`'i'`), `u8 ver` (must be 1), `u8 pad[2]`,
  `__be32 size`. For ucode/PCM `size` = payload bytes and parser requires
  `size == blob_size - 8` (main.c:2267-2271); for IV `size` = **number of IVs**
  (main.c:2273-2275).
- IV record `struct b43_iv` (b43.h:646-655): `__be16 offset_size` where
  `B43_IV_OFFSET_MASK 0x7FFF`, `B43_IV_32BIT 0x8000`; union `{__be16, __be32}`.
  Parser `b43_write_initvals` (main.c:2763) rejects `offset >= 0x1000`, advances
  6 bytes (32-bit value) or 4 bytes (16-bit value), and requires exact length
  consumption.
- Upload functions: `b43_upload_microcode` (2616), `b43_upload_initvals`
  (2824), `b43_upload_initvals_band` (2839), `b43_write_initvals` (2763).
- Post-start validation (main.c:2683-2751): read SHM shared
  `B43_SHM_SH_UCODEREV/PATCH/DATE/TIME` (0x0000/0x0002/0x0004/0x0006); reject
  `rev <= 0x128`; pick `hdr_format` (598/410/351); opensource iff `date==0xFFFF`.

## 2. b43-fwcutter 019 extraction metadata (C3, from compiled fwlist)

`b43-fwcutter` v019 supports `ac1initvals42` / `ac1bsinitvals42` / `ucode42`
(strings confirm). Its supported-driver list does **not** include our
`wlc_hybrid.o_shipped` (identify: unknown MD5 `6889dbd24abf8006de5cc6eddd138518`);
newest supported proprietary driver is `wl_apsta.o` 784.2
(MD5 `29c8a47094fbae342902d84881a465ff`). Recovered compiled `fwlist` entries
(.rodata @ 0xb128, 24-byte records: `u32 type; const char *name; u32 off; u32 size`):

| name | type field | source offset | size | output header |
|---|---|---|---|---|
| `ucode42` | 3 | 0x0027e61c | 0x9c50 (40016) | b43 `'u'` hdr |
| `ac1initvals42` | 5 | 0x00273218 | 0x1310 (4880) | b43 `'i'` hdr |
| `ac1bsinitvals42` | 5 | 0x002cc668 | 0x0118 (280) | b43 `'i'` hdr |

Caveat: these offsets are in a b43-fwcutter-supported *driver file*, not in our
blob. Exact semantics of the `type` field (3/5) and whether `size` is the source
slice or the on-disk payload could not be fully confirmed locally (no
b43-fwcutter source available; no network).

## 3. Our `bcm4352-d11ac1initvals42.bin` (4888 bytes)

SHA256 `b5a2735d…983d938`. **Not** `8-byte b43 header + 0x1310 IV payload`.
- `0x1310 = 4880`, but the file is `4888 = 611 × 8`; byte 0 is `0x60`, not
  `'i'` (0x69). There is **no b43 header**.
- Parsed as the vendor native record format (see §8) it yields exactly
  **611 records = 610 data + 1 terminator**, all valid, terminator
  `off=0xffff` at record #610. Perfectly self-consistent.
- As b43 IV: first `offset_size = be16(60 01) = 0x6001` → `offset 0x2001 ≥ 0x1000`
  → b43 parser errors immediately. **Not b43 IV.**
- Conclusion: **OpenBRCM cannot feed this file to a b43 IV parser.** Either
  OpenBRCM implements the vendor 8-byte consumer (recommended, §8/§10) or the
  file must be converted to b43 IV. No header needs stripping — there is none.

`bcm4352-d11ac1bsinitvals42.bin`: SHA256 `e81a645c…014da`, 592 = 74×8 (73 data +
terminator), same vendor format.

`bcm43xx-ucode.fw`: SHA256 `22cf38bc…d1a73`, 43400 bytes raw vendor ucode.

## 4. Local rev42 image search (restricted)

| path | size | sha256 |
|---|---|---|
| `/lib/firmware/brcm/bcm4352-d11ac1initvals42.bin` | 4888 | `b5a2735d…` |
| `/lib/firmware/brcm/bcm4352-d11ac1bsinitvals42.bin` | 592 | `e81a645c…` |
| `/lib/firmware/brcm/bcm43xx-ucode.fw` | 43400 | `22cf38bc…` |

No `/lib/firmware/b43/`. No rev42 images in the OpenBRCM or OBDP trees (git-dir
name matches only). OpenBRCM currently declares only
`MODULE_FIRMWARE("brcm/bcm43xx-0.fw")` and `"brcm/bcm43xx_hdr-0.fw"`
(ob_main.c:50-51) and calls `request_firmware()` **nowhere**.

## 5. Vendor blob rev42 images (C2)

`.rodata` file base = 0x181170. Symbol section offsets and file offsets:

| symbol | .rodata off | file off | size | SHA256 |
|---|---|---|---|---|
| `d11ac1initvals42` | 0x98110 | 0x219280 | 4888 | `b5a2735d…` |
| `d11ac1bsinitvals42` | 0x99430 | 0x21A5A0 | 592 | `e81a645c…` |
| `d11ucode42` | 0xb1670 | 0x2327E0 | 43400 | `22cf38bc…` |

`d11ucode42sz` @ .rodata 0xbbff8 = 43400 (confirms size). Staged files are
**byte-exact slices** of these symbols (`blob slice == file` verified). Vendor
rev42 selection (C2):
- ucode: `wlc_bmac_mctrl+…` @0x607fd — `rev==0x2a && phy_type==0x0b(AC)` →
  loads `(d11ucode42sz, d11ucode42)`. **Matches b43's rev42+AC condition
  exactly.**
- initvals: `wlc_bmac_init` @0x687c9 — `rev==0x2a` → `d11ac1initvals42` →
  `wlc_bmac_write_inits` (0x60fce) → applier 0x60f67.
- bsinitvals: referenced @0x66613 (up-prep/reset path).

Adjacent images also present: `d11ac0initvals40` (4920), `d11ac0bsinitvals40`
(592), `d11ucode40` (43576), plus `d11ac2/3/6`.

## 6. Size/version comparison

| image | vendor blob (ours) | b43 canonical | note |
|---|---|---|---|
| `ucode42` | 43400 | 40016 | different version (ours larger) |
| `ac1initvals42` | 4888 (vendor 8B fmt) | 4880 | different encoding + version |
| `ac1bsinitvals42` | 592 | 280 | substantially different |

Not interchangeable. Recorded both; do not silently substitute.

## 7. b43 ucode upload algorithm (C3) vs blob (C2)

b43 `b43_upload_microcode` (main.c:2616-2761):
1. `macctl = R(MACCTL 0x120)`; WARN if `PSM_RUN`; set `PSM_JMP0`; write.
2. Zero 64 `SHM_SCRATCH` + 4096 `SHM_SHARED` 16-bit words.
3. `shm_control_word(B43_SHM_UCODE|B43_SHM_AUTOINC_W, 0)`.
4. For each `__be32` word: `W(SHM_DATA 0x164, be32_to_cpu(word)); udelay(10)`.
5. (rev≤10) PCM to `SHM_HW 0x01EA`, seed `0x00004000`.
6. `W(GEN_IRQ_REASON 0x128, 0xFFFFFFFF)`.
7. clear `PSM_JMP0`, set `PSM_RUN`.
8. poll `GEN_IRQ_REASON == B43_IRQ_MAC_SUSPENDED (0x1)`, 20×50 ms.
9. dummy read `GEN_IRQ_REASON`.
10. read `SHM_SH_UCODEREV/PATCH/DATE/TIME`; validate.

Blob `wlc_bmac_mctrl`-region helper @0x60744/0x60760: `r13 = *(dev+0xd0)` (D11
base); `W(D11+0x160, 0x03000000)` once; loop `i<n`: `W(D11+0x164, *(u32*)tab[i])`
**raw, no byteswap**; set `ucode_loaded=1` (`dev+0xae`). A PCM-style helper
@0x60c2f uses control `0x0003_01ea` then writes words to `D11+0x164`.

| step | mark |
|---|---|
| 1 PSM_JMP0 / MACCTL prep | B43 ONLY (blob via `wlc_bmac_mctrl`, unverified) |
| 2 zero scratch/shared | B43 ONLY |
| 3 SHM_CONTROL routing | **DIFFERENCE** (b43 `0x0100`, blob `0x0300`) |
| 4 32-bit writes to SHM_DATA | B43 + BLOB AGREE |
| 5 PCM | B43 ONLY (rev≤10, N/A rev42) |
| 6 IRQ clear | B43 ONLY |
| 7 PSM start | AGREE (blob start not fully decoded) |
| 8 poll MAC_SUSPENDED | B43 ONLY |
| 9–10 rev read/validate | B43 ONLY |

Item 4 is the substantive difference: b43 treats the on-disk ucode as
big-endian (`be32_to_cpu`), the blob writes its table words raw (native/LE).

Initvals ordering: b43 `b43_chip_init` (main.c:3256-3278) sets
`MACCTL = IHR_EN|SHM_EN|GMODE|INFRA`, then `upload_microcode`, gpio,
`upload_initvals`, `upload_initvals_band`, then analog on + `phy_init`. So
initvals are written **after the PSM is running**, before PHY init, with
`EN_MAC` still clear.

## 8. initvals format: canonical b43 vs vendor (C3 vs C2)

b43 on-disk IV (§1) is **not** our vendor format. Vendor applier `wlc_bmac_write_inits`
(0x60fce) → 0x60f67 loop (C2) reads **8-byte records**:

```
struct ob_iv { u16 offset; u16 size; u32 value; }   /* little-endian */
/* size ∈ {2,4}; offset < 0x1000; terminator: offset == 0xffff */
```
- addr = `*(dev+0xd0) + offset`; `size==2` → write16, `size==4` → write32.
- `d11ac1initvals42`: 611 records (610 data: 113×16-bit, 497×32-bit).
- `d11ac1bsinitvals42`: 74 records (73 data: 39×16-bit, 34×32-bit).
- Records are D11-register-relative (e.g. `0x160` SHM_CONTROL, `0x164`
  SHM_DATA, `0x124`/`0x128` MAC command/status), 16 vs 32-bit per record.

Our staged files are already exactly this form and need **no conversion** if
OpenBRCM implements this consumer; they are **not** valid b43 IV.

## 9. Architectural separation (unchanged)

Keep distinct: D11 PSM ucode `ucode42`; common AC init values
`ac1initvals42`; band-switch values `ac1bsinitvals42`; **and separately later**
AC PHY/radio tables/calibration. Loading ucode + initvals does not bring up
PHY/radio/channel.

## 10. Provenance decision

Preference **A — same vendor blob we are reverse-engineering** is satisfied: all
three images are present and are exactly what the blob's rev42/AC path consumes.
Therefore use A. B (canonical b43 images) differs in version and encoding and is
**not** to be substituted. Do not use brcmfmac FullMAC images.

Consequence: M3.4D should consume the **vendor 8-byte format** (proven consumer
at 0x60f67) and the raw vendor ucode (raw word writes), not b43's IV/BE format.
This is an explicit deviation from the b43 algorithm to preserve provenance A.

## 11. Proposed M3.4D — D11 firmware loader (define only, do NOT implement)

- request the three vendor files (add names to `MODULE_FIRMWARE`; currently only
  `brcm/bcm43xx-0.fw`/`brcm/bcm43xx_hdr-0.fw` are declared);
- validate sizes (4888/592/43400) and terminators;
- place D11 in the proven upload state (`PSM_JMP0`, zero scratch/shared);
- upload `ucode` raw words via SHM_UCODE+autoinc;
- apply common initvals (vendor 8-byte parser), then bsinitvals;
- start PSM (`PSM_RUN`), poll `GEN_IRQ_REASON == MAC_SUSPENDED`;
- verify ucode revision/heartbeat via SHM;
- **STOP** before PHY/radio/channel. No EN_MAC unless the proven start sequence
  requires it. No RX DMA, no TX.

**End of M3.4C.1 — analysis only; no hardware writes performed.**

---

# M3.4D1 — acquisition + validation implemented (no hardware writes)

## Verification of byte-exactness (re-confirmed)

| image | blob slice (file off) | size | blob==file | SHA256 |
|---|---|---|---|---|
| `d11ucode42` | 0x2327E0 | 43400 | yes | `22cf38bc…d1a73` |
| `d11ac1initvals42` | 0x219280 | 4888 | yes | `b5a2735d…983d938` |
| `d11ac1bsinitvals42` | 0x21A5A0 | 592 | yes | `e81a645c…d014da` |

Installed names used: `brcm/bcm4352-d11ucode42.bin` (primary) with documented
fallback `brcm/bcm43xx-ucode.fw`; `brcm/bcm4352-d11ac1initvals42.bin`;
`brcm/bcm4352-d11ac1bsinitvals42.bin`. FNV-1a-64 guards: `0x7d364f6207b3298b`,
`0xa7cfdfdbcc9d58f1`, `0xfa86a2510d2e0e3e`.

## Recovered vendor sequences (C2, brcmsmac C3 corroboration)

1. **Ucode upload registers** — applier `wlc_bmac_ucode_write` blob 0x60744
   (dispatch 0x607b0). `D11+0x160 = OBJADDR`, `D11+0x164 = OBJDATA`
   (brcmsmac d11.h:147-148). It writes `OBJADDR = 0x03000000` =
   `OBJADDR_AUTO_INC (0x03000000) | OBJADDR_UCM_SEL (0x0)`, **reads OBJADDR
   back** (barrier), then writes each 32-bit word to OBJDATA; auto-increment
   advances UCM (microcode) memory. Exact match: brcmsmac `brcms_ucode_write`
   (main.c:2214-2228). No byte swap — raw LE words (`le32_to_cpu` is identity).
   One write per word; final OBJADDR points past the image.
2. **Pre-upload MACCONTROL** — `wlc_bmac_init` @0x68353 calls
   `wlc_bmac_mctrl(dev, ~0, 0x04000404)` =
   `IHR_EN(0x400) | PSM_JMP_0(0x4) | WAKE(0x04000000)`; `PSM_RUN=0`,
   `EN_MAC=0`, `SHM_EN=0`. Exact match brcmsmac `brcms_b_coreinit`
   (main.c:3141).
3. **Scratch / SHM zeroing** — NOT performed by the vendor. The only
   64-iteration loop (0x686ef) calls `wlc_bmac_write_amt(dev, i, 0, 0)`,
   zeroing the 64-entry address-match table, not SHM/UCODE/SCRATCH. D2 must
   **not** add b43-style SHM zeroing.
4. **PSM start + validation** — blob fn 0x63828 (sym
   `wlc_bmac_wowlucode_start`, called from `wlc_bmac_init` @0x68501):
   `W(D11+0x128 macintstatus, 0xffffffff)`; then
   `wlc_bmac_mctrl(dev, ~0, 0x04020402)` =
   `IHR_EN | INFRA | PSM_RUN(0x2) | WAKE`; then polls
   `macintstatus & MI_MACSSPNDD(1<<0)` with 10 µs delays, timeout `0xF4249`
   (~1e6); success iff bit0 set. Exact match brcmsmac (main.c:3148-3157).
   b43's `GEN_IRQ_REASON` is the same register (0x128) and `MAC_SUSPENDED`
   the same bit (0x1). **No ucode revision string is read**; the run is
   verified by MI_MACSSPNDD self-suspend and by reading `M_FIFOSIZE0..3`
   (SHM 0x98/0x9a/0x9c/0x9e) for FIFO-size consistency (blob 0x68f88-0x68fae).
5. **initvals ordering** — common `d11ac1initvals42` applied in
   `wlc_bmac_init` @0x68b98, **after** ucode download (0x684c4) and PSM start
   (0x68501). bandswitch `d11ac1bsinitvals42` applied in discovered fn
   `sub_6656c` @0x669bd, which **immediately calls `wlc_phy_init` @0x669df**;
   `sub_6656c` is called from `wlc_bmac_init` @0x695d8 (initial 2.4 GHz path)
   and from `wlc_bmac_set_chanspec` @0x67bd0. Both tables are consumed at
   initial bring-up; order = **common then bandswitch**. bsinitvals belong to
   the band-init stage that is intertwined with PHY init.
6. **Open issue for D2** — the vendor calls `wlc_phy_cal_init` (0x6834c)
   *before* the ucode upload. The user's "stop before PHY/calibration" boundary
   therefore is not clean; D2 planning must decide whether that one-time cal
   call is required for ucode/initvals to take effect. Flagged, not guessed.

## D1 code

`src/ob_fw.{c,h}`: pure host-testable parser/iterator + kernel
`ob_fw_probe()`; called from `ob_probe()` right after `ob_si_probe()`.
Strict validation: exact size + FNV-1a-64 + structural parse; a present-but-
different file fails probe (`-EINVAL`/`-EILSEQ`). Absent files are logged and
do not disturb the validated SPROM/MAC/DMA/IRQ paths. Dry run logs only the
first/last 10 ucode words and IV records (`fw_dryrun` param, default on).
`MODULE_FIRMWARE()` now declares only the rev42 files. No register/SHM/IHR/
PSM/PHY/radio/RX/TX access anywhere in D1.

## Refined M3.4D2 boundary (define only, not implemented)

Apply, in vendor order: pre-ucode `MACCONTROL=0x04000404` → upload ucode via
OBJADDR/OBJDATA (raw words) → `W(macintstatus,-1)` + `MACCONTROL=0x04020402`
(PSM_RUN) + poll `MI_MACSSPNDD` → common `ac1initvals42` via the vendor 8-byte
applier → verify `MI_MACSSPNDD` and FIFO-size SHM (0x98..0x9e) → **STOP before
`sub_6656c`** (band init + `wlc_phy_init`), so **bsinitvals are NOT applied in
D2**. No EN_MAC, no RX/TX DMA, no mac80211 RX/TX. Resolve the `wlc_phy_cal_init`
ordering question first.

