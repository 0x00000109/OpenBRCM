# Ghidra tier-2 RE scripts (OpenBRCM)

These are **reusable** Ghidra headless scripts invoked by
[`../ghidra_headless.sh`](../ghidra_headless.sh). They cover facts the canonical
Rust `re`/`re.db` cannot represent (see [`docs/re-tooling.md` §2.4](../../docs/re-tooling.md)).

Tier order (mandatory, see `AGENTS.md` §7):

1. `re` / `re.db` (indexed, deterministic) — always first.
2. **Ghidra headless** (decompiler, reference manager, CFG-aware value flow) —
   for indirect/vtable targets, interprocedural constant flow, struct-field
   aliasing, and loop/engine-array address resolution.
3. Manual `objdump`/`readelf`/`r2` — only if 1 and 2 cannot answer, or to settle
   a conflict between them (record the reason).

Read-only: the scripts never execute code, never touch hardware, never mutate
the blob. The vendor blob is **not** committed; the wrapper imports the
canonical blob at `$RE_BLOB`.

| script | purpose |
| :--- | :--- |
| `Decompile.java` | decompile one or more functions by name |
| `Refs.java` | list references (callers / pointer installers) to a symbol |
| `Vtable.java` | dump a function-pointer table/vtable with resolved targets |

## Examples

```sh
# who installs the PHY callbacks?
scripts/ghidra_headless.sh Refs.java wlc_phy_btc_adjust_acphy wlc_phy_init

# resolve the DMA engine vtable and its indirect targets
scripts/ghidra_headless.sh Vtable.java dma64proc

# decompile the AC anacore / radio dispatch
scripts/ghidra_headless.sh Decompile.java wlc_phy_anacore wlc_phy_switch_radio

# first run imports + analyzes the blob (~1 min); force a refresh:
scripts/ghidra_headless.sh --reimport Refs.java wlc_phy_init
```

Output is Ghidra's `INFO  <Script>.java>` prefixed stdout; pipe through
`sed -n '/===== /,$p'` to strip the launcher noise.

## Environment

- `GHIDRA_HOME` (default `/home/kartashoff/projects/ghidra`),
- `GHIDRA_PROJ` (default `/tmp/openbrcm_ghidra`),
- `RE_BLOB` (default the canonical `wlc_hybrid.o_shipped`).

Java GhidraScript is used (not `.py`) because headless PyGhidra requires the
Ghidra runtime to be started in PyGhidra mode; Java scripts work everywhere.
