# Tools Reference

Scripts, commands, and reference material for the DC3 decompilation project. For agent tool selection, see [INDEX.md](INDEX.md).

## Project Scripts

| Script | Description |
|--------|-------------|
| `tools/decompile.sh` | **Combined m2c decompilation workflow** (objdiff → m2c) |
| `tools/objdiff_to_m2c.py` | Convert objdiff JSON to m2c assembly format (with jump table resolution) |
| `tools/ghidra/export_types.py` | Export Ghidra types as m2c context headers |
| `tools/asm_to_m2c.py` | Convert DC3 dtk assembly to m2c-compatible format |
| `tools/decompctx.py` | Generate context files for decomp.me |
| `configure.py` | Generate build files (ninja) |
| `scripts/build/rebuild_jeff_link.sh` | Rebuild jeff (dtk), re-split XEX objects, link, show error summary |

## Symbol Lookup (Map File)

The linker map file `orig/373307D9/ham_xbox_r.map` contains all symbol names and addresses:

```bash
# Find function address by name
grep "FastSin\|Pool::Alloc" orig/373307D9/ham_xbox_r.map

# Example output:
# 0005:002027e8       ?FastSin@@YAMM@Z           825327e8 f   math:Trig.obj
#                     ^ mangled name              ^ address    ^ source file
```

## Merged Symbol Lookup (ICF)

When objdiff shows `LINKER_MERGED` patterns with `merged_<address>` symbols, use the merged-symbols tool to identify the actual symbol names:

```bash
# Look up what symbols are at a merged address
./bin/merged-symbols 82331360

# Also accepts the merged_ prefix from objdiff output
./bin/merged-symbols merged_82331448 -v

# See statistics on all merged symbols
./bin/merged-symbols --stats -e

# Output as JSON
./bin/merged-symbols 82331360 --json
```

ICF (Identical COMDAT Folding) merges functions with identical machine code to save space. Common patterns:
- `??_G` / `??_E`: Scalar and vector deleting destructors (identical code)
- Template instantiations like `ObjRefConcrete<T>::GetObj()` (same code for different T)

## Function Database (decomp.db)

SQLite database tracking all functions, patterns, and scoring:

```bash
# Find high-priority reachable targets
sqlite3 decomp.db "SELECT symbol, current_percent FROM functions WHERE reachable_100=1 AND current_percent < 100 ORDER BY priority_score DESC LIMIT 10"

# Query functions by pattern
sqlite3 decomp.db "SELECT symbol, current_percent FROM functions WHERE has_linker_merged=1 ORDER BY current_percent DESC LIMIT 10"
```

See [../reference/DATABASE_SCHEMA.md](../reference/DATABASE_SCHEMA.md) for full schema documentation.

### Trust caveats — read before believing a column

These apply equally to raw `sqlite3` queries and to
`mcp__orchestrator__query_functions`, which reads the same table. Treat the DB as
a **work-selection index**, not a measurement.

**`current_percent` drifts, sometimes badly.** The ninja `SYNC DB` step
(`scripts/ingest_report.py --build-safe`) deliberately does *not* write it — see
`ingest_report()` in `scripts/orchestrator/database.py`: *"Don't update
current_percent from report.json — it's unreliable. Only sync_objdiff.py should
set match%."* It updates `demangled`/`unit`/`size` and bumps `updated_at`. The
step is also best-effort: if the DB is locked by the live fleet it prints a
warning and exits 0, which on a box running several agents is the common case.

Two things follow. First, **`updated_at` is not a freshness signal for
`current_percent`** — it means "metadata touched by the last build". Second, the
percentages can be arbitrarily stale. Measured 2026-08-04, minutes after a sync:
**818 of 31,387 comparable rows disagreed with `report.json` by more than 0.5pp**,
the worst by ~65pp, in both directions. Re-measure it yourself:

```bash
sqlite3 decomp.db "SELECT symbol, current_percent FROM functions" # vs report.json fuzzy_match_percent
python3 scripts/sync_match_percent.py --build --promote   # rebuild report.json + resync (~2-3 min)
```

**`has_prologue_mismatch` is identically 0 for every row.** The detector never
populated it — `SELECT SUM(has_prologue_mismatch), COUNT(DISTINCT
has_prologue_mismatch) FROM functions` returns `0 | 1` over all 52,546 rows.
Filtering on it selects everything; filtering on `=1` selects nothing. It is not
evidence of anything.

**Always re-measure before acting.** Use `mcp__orchestrator__run_objdiff` and
**pass `project_dir`**. Omitting it silently measures the *main repo* instead of
your worktree, so your edits are invisible and the number looks frozen — the most
common way to waste an hour here.

For the decomp-triage side of DB trust (which verdicts and pattern flags survive
scrutiny, and how to triage what you find), see
[../decomp/patterns/INDEX.md](../decomp/patterns/INDEX.md).

## Progress Measurement

`scripts/measure_progress.sh` compares a baseline commit's `report.json` against
the current one. It builds the baseline in a throwaway worktree and caches the
result under `build/373307D9/baselines/`.

```bash
scripts/measure_progress.sh                          # HEAD vs HEAD~1
scripts/measure_progress.sh --functions --detailed HEAD~5
scripts/measure_progress.sh --current-dir /path/to/worktree HEAD
scripts/measure_progress.sh --authorable             # authorable-denominator metrics, no baseline
scripts/measure_progress.sh --refresh-baseline       # discard + rebuild the cached baseline
scripts/measure_progress.sh --allow-stale            # downgrade staleness/race errors to warnings
```

### The staleness gate, and why it exists

**Before this gate existed, a stale baseline silently produced wrong numbers.** A
`report.json` that lags its sources — or that another agent rebuilds underneath
you mid-comparison — shows up as a pile of *phantom regressions* that are
indistinguishable from real ones. There is no way to spot this from the output;
people acted on it.

Four guards now stand between you and that (all added in `e092d526`):

| Guard | What it does |
|-------|--------------|
| **Freshness gate** | Runs `ninja -n build/373307D9/report.json` in each side's directory. Anything other than "no work to do" means stale: the script rebuilds once, then re-checks and refuses if it is *still* stale (which means a concurrent build). |
| **Baseline provenance stamp** | Each cached baseline carries a `.meta` recording the commit, the dtk and objdiff-cli SHAs, and the git blob of every config input (`config.yml`, `symbols.txt`, `splits.txt`, `objects.json`, `link_order.txt`). Re-verified on reuse; a mismatch prints the diff and rebuilds. A pre-provenance cache entry is used but loudly flagged as unverifiable. |
| **Toolchain pinning** | The dtk and objdiff-cli actually used are read out of each side's generated `build.ninja` rather than assumed, and the baseline is built with the **current** side's binaries. dtk decides function boundaries from `symbols.txt` and objdiff-cli computes the percentages, so a version skew between the two sides invents differences that have nothing to do with the code. |
| **Mid-compare fingerprinting** | Both reports are fingerprinted (`inode:size:mtime:ctime`) before and after the diff. If either changed, the numbers came from a racing build and the run fails. |

**`--allow-stale` downgrades all of the above to warnings.** It exists for
diagnosis, not for getting past an inconvenient error. If the gate fires, the
right move is almost always to let it rebuild, or to use `--refresh-baseline`
when the config or toolchain has genuinely changed. Numbers produced under
`--allow-stale` are not trustworthy and should not be quoted anywhere.

The provenance inputs are exactly the files that decide *what dtk and objdiff
measure against*, which is why the jump-table split fix (see
[BUILD_SYSTEM.md](BUILD_SYSTEM.md#the-jump-table-split-bug-fixed-2026-08-04))
correctly invalidates every baseline cached before it: a bad `symbols.txt`
depressed scores by shipping wrong function extents to objdiff, so a
pre-fix-vs-post-fix comparison is measuring the splitter, not the code.

## Linking Tools

| Script | Description |
|--------|-------------|
| `scripts/build/link_test.py` | Standalone X360 link test (links split/hybrid .obj → PE) |
| `scripts/build/compare_pe.py` | Compare linked PE against original `ham_xbox_r.exe` |
| `scripts/build/fix_pdata.py` | Workaround for dtk .pdata splitting bug (integrated into `ninja link`) |

See [../sessions/2026-02-11-x360-linking-pipeline.md](../sessions/2026-02-11-x360-linking-pipeline.md) for full status and roadmap.

## Register Swap Patcher

Post-build tool that patches compiled `.obj` files to fix register allocation mismatches.
Uses objdiff's instruction-level diff as an oracle to identify register swaps, then
directly modifies the register fields in the PowerPC instructions.

**Not run by default** — must be invoked manually after `ninja`.

```bash
# Dry run: show what would be patched (no changes)
python3 scripts/obj_regswap_patcher.py --batch

# Apply patches to .obj files
python3 scripts/obj_regswap_patcher.py --batch --apply

# Regenerate report to see patched progress (without rebuilding)
build/tools/objdiff-cli report generate -o build/373307D9/report.json
python3 configure.py progress
```

Note: `ninja` will overwrite patched `.obj` files on the next rebuild, so the patcher
must be re-run after each build. The patcher auto-reverts any function where patching
causes a regression.

## objdiff MakeString Array-Size Normalization

Built into objdiff's `reloc_eq()` comparison (no separate tool needed). Automatically treats
`MakeString<char[N], int, char[M]>` template instantiations as equivalent regardless of N/M,
since arrays decay to pointers and produce identical machine code.

This resolves `bl` `diff_arg` mismatches caused by `__FILE__` string length differences
between the original build environment and ours. See
[../plans/MAKESTRING_ICF_EQUIVALENCE.md](../plans/MAKESTRING_ICF_EQUIVALENCE.md) for details.

**Impact:** +8.66pp fuzzy match (45.40% → 54.06%), +601 complete units.

## Quick Commands

```bash
# Build the project
ninja

# Generate progress report
ninja build/373307D9/report.json

# Link hybrid PE (requires wine)
ninja link

# Find near-match functions (90-99%)
objdiff-cli report query build/373307D9/report.json --functions --min-percent 90 --max-percent 99

# Check a specific function (markdown output is default)
objdiff-cli diff -p . "Game::Poll" --verdict

# Diff with context around mismatches
objdiff-cli diff -p . "Game::Poll" --verdict -C 3

# Check function info from report
objdiff-cli report function build/373307D9/report.json "Game::Poll"

# Quick m2c decompilation from target binary
tools/decompile.sh "CharClip::SetFlags"

# m2c with Ghidra type context
tools/decompile.sh "CharMirror::Load" --context

# Full analysis with m2c included
./bin/analyze-function "Game::Poll" --m2c

# Manual m2c pipeline (alternative, with jump table support)
./bin/objdiff-cli diff -p . "Foo::Bar" -f json --include-instructions | \
    python3 tools/objdiff_to_m2c.py --project-dir . | \
    python3 ~/code/milohax/m2c/m2c.py -t ppc -

# Generate decomp.me context
python3 tools/decompctx.py src/path/to/file.cpp -I include -I src
```

## Compiler Documentation

| Doc | Description |
|-----|-------------|
| [PRAGMA_INDEX.md](../decomp/PRAGMA_INDEX.md) | Xbox 360 compiler pragma documentation index |
| [PRAGMA_MATCHING_CHECKLIST.md](../decomp/PRAGMA_MATCHING_CHECKLIST.md) | Step-by-step guide for using pragmas to match functions |
| [PRAGMA_CODEGEN_SUMMARY.md](../decomp/PRAGMA_CODEGEN_SUMMARY.md) | Quick reference for pragma impact on code generation |
| [XBOX360_PRAGMA_REFERENCE.md](../decomp/XBOX360_PRAGMA_REFERENCE.md) | Complete technical reference for all code-generation pragmas |

**Key pragmas for matching:**
- `#pragma fp_contract(on|off)` - Controls fused multiply-add instruction generation (fmadds)
- `#pragma optimize("u", on|off)` - Controls prescheduling (instruction ordering)
- `#pragma bitfield_order(msb_to_lsb|lsb_to_msb)` - Controls bitfield packing order

## Archived Tools

| Tool | Description | Doc | Notes |
|------|-------------|-----|-------|
| decomp-permuter | Original C permutation fuzzer | [permuter.md](permuter.md) | C only, uses pycparser which doesn't support C++ |

## Projects

| Project | Description | Doc |
|---------|-------------|-----|
| VMX128 Ghidra Support | Adding Xbox 360 SIMD instruction support to Ghidra | [../vmx128/README.md](../vmx128/README.md) |

## External Resources

- [objdiff GUI](https://github.com/encounter/objdiff) - Visual diff tool
- [m2c online](https://simonsoftware.se/other/m2c.html) - Browser-based m2c
- [decomp.me](https://decomp.me) - Collaborative decompilation scratches
