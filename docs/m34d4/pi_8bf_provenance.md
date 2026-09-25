# M3.4D4 — `pi+0x8bf` exact value and provenance resolved

Blocker: **`D4-BLOCKER-PI-8BF`**. Status: **`ANALYSIS ONLY`** — no hardware,
no MMIO, no driver execution. Machine-readable packet:
[`pi_8bf_provenance.json`](pi_8bf_provenance.json).

## 0. Result

- Read: `0xaa2b0  movzbl 0x8bf(%r13),%ecx`, `r13 = *(pi+0x138)`.
- **Value = `0x1a` (26).**
- Provenance: **deterministic constant** written by `sub_a7089` from a stack
  array `{0x19, 0x1a}` — *not* hardware-derived and *not* unknown.
- This supersedes the prior `no store found / COMPUTED_RUNTIME / UNKNOWN`.

## 1. Base-object correction

The field is **not** `pi+0x8bf`. The real base is `*(pi+0x138)`:

- `0xaa255  mov 0x138(%rbx),%r13` (and `0xaa311 …%r11`), where `rbx = pi`
  (passed as `rdi` to `wlc_phy_table_write_acphy`).
- `*(pi+0x138)` is a **0x920-byte** heap object:
  `wlc_phy_attach_acphy @0xa1997 mov [rbx+138h],rax` after
  `osl_malloc(*([pi+0x20]+0x10), 0x920)`, then
  `@0xa19ae osl_memset(rax, 0, 0x920)`; freed by `sub_97e2b`
  (`osl_mfree(osh, *(pi+0x138), 0x920)`, the installed `pi_fptr +0xc0`).

So the field is `(*(pi+0x138)) + 0x8bf`.

## 2. Exact read and containing function

| item | value |
| :--- | :--- |
| read site | `0xaa2b0  movzbl 0x8bf(%r13),%ecx` |
| base | `r13 = *(pi+0x138)` (loaded `0xaa255`) |
| containing fn | `sub_a7089` (unnamed discovered; Ghidra `FUN_001a7089`) |
| dead-end | `wlc_phy_stf_chain_temp_throttle_acphy` (`0xa479f`, size **200**) is a **separate** function; not the container |
| consumer | 4th arg (`ecx`) of `wlc_phy_table_write_acphy(pi, table=3, n=1, ecx, len=0x20, buf)` at `0xaa2ce` |

`sub_a7089` is called from `wlc_phy_switch_radio_acphy @0xab3f5` (first
radio-ON path) and from `sub_b018f`.

## 3. The writer (what the prior analysis missed)

`re field-writers --field 0x8bf` and a literal-displacement scan find **no**
store to `+0x8bf`. The store is an **incrementing-pointer** store:

```
0xaa2ef  movl $0x19,-0x70(%rbp)     ; arr1[0] = 0x19
0xaa2fc  movl $0x1a,-0x6c(%rbp)     ; arr1[1] = 0x1a
0xaa303  movl $0x7, -0x60(%rbp)     ; arr2[0] = 7
0xaa30a  movl $0x8, -0x5c(%rbp)     ; arr2[1] = 8
0xaa33f  mov %r11,%r15              ; r15 = *(pi+0x138)
0xaa342  xor %r13d,%r13d
loop:                                ; 2 iterations (r13 = 0,4 -> cmp $8)
0xaa345  mov (%rax,%r13,1),%edx      ; edx = arr1[i]
0xaa35b  lea (%r14,%rdx,1),%ecx
0xaa35f  test %edx,%edx
0xaa361  cmovs %ecx,%edx             ; only if arr1[i] < 0 (never for 0x19/0x1a)
0xaa364  mov %dl,0x8be(%r15)         ; i=0 -> +0x8be = 0x19 ; i=1 -> +0x8bf = 0x1a
...
0xaa39a  inc %r15
0xaa3c2  jne loop
0xaa3c4  movb $0x1,0x8bd(%r11)
```

Raw `objdump` confirms `aa364: mov %dl,0x8be(%r15)` with `aa39a: inc %r15`
and `aa3a9: cmp $0x8,%r13`. So one instruction writes **both** `+0x8be` and
`+0x8bf` via `r15`; results: `+0x8be = 0x19`, **`+0x8bf = 0x1a`**. The `cmovs`
negativity path does not apply to the positive constants, so the value is
independent of the band selector `r14`.

### Overlap / aliasing checks (all negative except the loop)

- No byte store at `0x8bf`, no word store at `0x8be`, no dword at `0x8bc`, no
  qword at `0x8b8` anywhere in `.text`.
- No 0x920-sized `memcpy`/`rep movs` into the sub-object (the several `0x920`
  constants are register/table offsets, not copy sizes).
- The only `+0x8bd` writers are `0xaa2d3` (=0) and `0xaa3c4` (=1), both in
  `sub_a7089`.
- `+0x8be`/`+0x8bf` are `osl_memset(0)` at attach and have no other writer.

## 4. Ordering and predicates

- Read gate: `0xaa27f cmpb $0x0,0x8bd(%r13); je 0xaa2db` — the read runs only
  when `+0x8bd != 0`.
- `+0x8bd` is set to `1` only by the producer (`0xaa3c4`), which runs only when
  `byte [pi+0x17e] == 0x0d` (`0xaa2db`, chanspec low byte = channel 13).
- Therefore the read at `0xaa2b0` executes on a **later invocation** than the
  producer; within one invocation the read precedes that invocation's producer
  store. The producer always writes the same constants, so **whenever the read
  executes, `+0x8bf == 0x1a`**.
- Outer gates: `[pi+0x164]==3`, `[*(pi+0x20)+0x69] & 0x20`,
  `[pi+0xc24] == 0x2625a00` (40,000,000).
- `pi+0x17e` is the **chanspec** (`wlc_phy_chanspec_radio_set` /
  `wlc_phy_chanspec_get`); its low byte is the channel, so the producer/consumer
  pair runs at channel 13. The factory-default first channel is
  `COMPUTED_RUNTIME` (`wlc_phy_chanspec_band_firstch` builds `channel|0x1800`),
  so channel 13 is not guaranteed on the very first bring-up.

## 5. Classification summary

| possibility | verdict |
| :--- | :--- |
| no direct store | true literally, but a store exists via `r15` increment |
| implicit zero from allocation | true until the producer runs (`osl_memset(0,0x920)`) |
| packed/word store covering `+0x8bf` | no; but an **incrementing-byte store** covers it |
| memcpy / structure copy | no |
| table-derived initialization | no |
| runtime hardware-derived | no |
| truly uninitialized/unknown | no |

**Value = `0x1a`; class = deterministic constant (stack-array immediate).**

## 6. Tooling gap

`re field-writers` / `re fields` do not model a store whose base register is
incremented in a loop (`mov %dl,0x8be(%r15)` + `inc %r15`), so the `+0x8bf`
writer was invisible. Filed as **T8 — incrementing-pointer store coverage**.
No tooling change made (a correct general fix needs value-range tracking of the
store base; not small, and this task is analysis-only).
