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

**The `has_*` pattern flags come from a reloc-BLIND pass** (fixed 2026-08-19).
`sync_objdiff.py` runs objdiff with `-c functionRelocDiffs=none` — the canonical
ruler — which masks relocation differences. Four detectors read exactly those, so
`has_linker_merged`, `has_prologue_mismatch`, `has_scope_counter_mismatch` and
`has_makestring_mismatch` were `0` on all 52,547 rows while `verdict_reason` on
708 of them said `LINKER_MERGED`. Re-running the same 3,000 functions with
`functionRelocDiffs=all` gave 119 / 20 / 9 / 7. Over the full 31,446 authorable
functions: **1,310 / 221 / 81 / 63**. Refresh with
`python3 scripts/backfill_reloc_patterns.py --apply`; `sync_objdiff` no longer
writes `has_linker_merged` so it cannot zero the result. `has_assert_revs` and
`has_ltcg_pooling` were **dropped** — always 0, no writer, no detector.
`has_alloca_mismatch` / `has_dynamic_cast_mismatch` are still 0, but that is now a
*measured* zero under the permissive config, not an unmeasured one.

**There is no `decomp.db` in a git worktree, deliberately.** A worktree `ninja`
used to create a shadow one — every row, **0 verdicts and 0 percentages** — and
anything defaulting to `--db decomp.db` answered out of it. Identical queries,
2026-08-19: AT_LIMIT certs `0` vs `3,796` in main; near-misses `0` vs `89`; the
80-95 band `0` vs `325`. Worse than empty, actually: `query_functions` treats a
NULL percent as passing a range filter, so an 80-95 query returned **20 rows
with no percentages at all**. Since 2026-08-19 the worktree carries a *tripwire*
file at `decomp.db` that is not a valid SQLite database, `orchestrator.database`
raises `ShadowDatabaseError` naming both paths, and the ninja sync edge skips.
Pass the main repo's path explicitly. MCP tools are unaffected — they resolve
the DB against the server's project root, not your cwd.

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

## objdiff-cli through MCP: what maps to what

`CLAUDE.md` tells agents to use the `mcp__orchestrator__` tools rather than the
raw CLI. That rule was unfollowable until 2026-08-19. A sweep of 474 session
transcripts found 483 tool calls mentioning `objdiff-cli`, of which **296
actually invoke it — 259 against DC3 or a DC3 worktree** (the rest are
`rb3-xenon`/`rb3`, where MCP refuses by design, or the objdiff fork's own tests).
By flag, those 259 DC3 invocations break down as:

| flag reached for | n | now |
|---|---|---|
| `-f json` (parse it myself) | 181 | `output_format="json"` |
| `--include-data` (vtables, RTTI) | 88 | `include_data=true` |
| `--batch` (bulk) | 49 | `run_symbol_sweep` |
| `-c functionRelocDiffs=…` | 31 | `diff_mode=` |
| `--full-listing` | 28 | already existed (`full_listing`) |
| `-1/-2` object pair | 25 | mostly `build=false`; true arbitrary pairs stay CLI |
| `report generate`/`query` | 12 | infrastructure — stays CLI |
| `--map-file` | 10 | automatic |
| `doc-links` | 3 | infrastructure — stays CLI |

Use this table before reaching for the binary.

| Raw invocation | Sanctioned equivalent |
|---|---|
| `diff <sym> -f markdown --verdict` | `run_objdiff(symbol, project_dir)` |
| `diff <sym> -f json --include-instructions` | `run_objdiff(..., output_format="json")` |
| `diff <sym> --include-data` (vtables, RTTI, string pools) | `run_objdiff(..., include_data=true, unit="…")` |
| `-c functionRelocDiffs=all` (count relocations) | `run_objdiff(..., diff_mode="raw")` |
| `-c functionRelocDiffs=name_check` (report.json's ruler) | `run_objdiff(..., diff_mode="name_check")` |
| `diff -1 <target.obj> -2 <base.obj> <sym>` merely to skip the build | `run_objdiff(..., build=false)` |
| `diff <sym> -C 5` / `--full-listing` | `run_objdiff(..., context=5)` / `full_listing=true` |
| `diff -u <unit> <sym>` (disambiguate) | `run_objdiff(..., unit="…")` |
| a shell loop over many symbols | `run_symbol_sweep(kind="functions", symbols=[…])` |
| a shell loop over every `??_7` in every unit | `run_symbol_sweep(kind="vtable_slots")` |
| `--map-file` for ICF equivalence | automatic — objdiff loads `build/373307D9/icf_aliases.map` from `objdiff.json`, and `run_symbol_sweep` reads it again for address adjudication |

**Still legitimate direct CLI use** (do not route these through MCP):

* the ninja `report generate` rule, `tools/none_guard.py`, `tools/project.py`,
  `scripts/measure_progress.sh`, `scripts/sync_match_percent.py` — these *are*
  the measurement layer;
* `-1/-2` where the two objects genuinely are **not** this project's
  target/base pair for one unit — e.g. `-1 $TGT -2 $TGT` to read the target's
  own listing, or comparing a saved candidate `.obj`. (If you only wanted to
  avoid a rebuild, use `build=false`.)
* `doc-links`, `report query`/`report function` — no MCP surface, and none is
  needed for ordinary decomp work.
* **Anything in another repo.** `run_objdiff` raises `CrossProjectError` for a
  foreign `project_dir` on purpose; 19 of the 296 transcript invocations were
  `rb3-xenon` work correctly routed around MCP. Prefer that repo's own
  orchestrator when one exists.

### The relocation ruler: three rulers, and which one to reach for

All counts below are **non-equal instruction rows** from `objdiff-cli
--include-instructions`, not headline percentages. Measured on this fork
(objdiff-cli 4.2.3, `?Load@CamShot@@UAAXAAVBinStream@@@Z`, 2026-08-19), with
the ICF alias map held constant via `--map-file` so the project option is the
only variable:

| ruler | fuzzy% | non-equal rows | reloc_ignored |
|---|---|---|---|
| **objdiff-cli's own built-in default** (`DataValue`) | 99.68568 | **147** | 15 |
| *(flag omitted, under this repo's `-p`)* | 99.85558 | 119 | 22 |
| `none` | 99.85558 | 119 | 22 |
| `name_check` | 99.85558 | 119 | 22 |
| `all` | 99.66141 | **151** | 11 |

**Why omitting `-c` is not raw.** Not because "the fork's default is already
normalized" — that was the original explanation here and it is **false**. The
fork's built-in CLI default is `FunctionRelocDiffs::DataValue`
(`objdiff-cli/src/cmd/diff.rs`, `build_config_from_args`), a *third* ruler
with a third row count: **147**. Omitting the flag lands on `name_check` only
because **this repo's `objdiff.json`** carries

```json
"options": { "functionRelocDiffs": "name_check" }
```

which `apply_project_options` (`objdiff-core/src/config/mod.rs`) stamps over the
built-in default whenever `-p` loads the project.

That distinction is load-bearing, not pedantry: **`bin/objdiff-cli` is a symlink
shared with `../rb3` and `../rb3-xenon`**, whose `objdiff.json` may set a
different value. "The fork normalizes" predicts the same row counts in all three
trees. It travels with the **project config**, not with the binary.

A second contingency worth knowing: the `none == name_check == 119` coincidence
holds only while the ICF alias map is loaded. Drop `--map-file` and `name_check`
rises to **130** while `none` stays at **119** — the two rulers genuinely
differ; they agree here only because `icf_aliases.map` already proves the folds
`name_check` would otherwise charge.

#### What `raw` actually was: mislabelled, not blind

`run_diff_inspect` shipped `diff_mode="raw"` as *omit `-c`*, which under this
repo's `-p` returned the **`name_check`** answer under a "raw" label. It was
**mislabelled, not inert-and-blind**: `name_check` charges relocation *name*
mismatches, which is precisely the wrong-callee / wrong-vtable-slot plane. An
earlier draft of this note claimed an agent hunting a wrong-slot bug "would have
concluded there was none"; that is **overstated and retracted**.

**`name_check` is the wrong-callee ruler — prefer it.**
`?Poll@KinectShareConnection@@QAAXXZ` scores 100.0% with **0** mismatch rows
under `none`, and `name_check` finds exactly **1**, a genuine wrong callee:

```
bl ??$MakeString@E@@YAPBDPBDABE@Z   (target, unsigned char)
bl ??$MakeString@D@@YAPBDPBDABD@Z   (ours,   char)
```

**`raw`/`all` is the addend view, and it is noisier rather than more capable**
for that class. Over a 14-function sample, `all` added **997** rows on top of
`name_check`:

| what the added row was | rows | share |
|---|---|---|
| same symbol on both sides (pure address/addend noise) | 531 | 53.3% |
| `lbl_*` vs a named static | 352 | 35.3% |
| register-only, no symbol either side | 112 | 11.2% |
| **an actual name divergence** | **2** | **0.2%** |

i.e. **99.8% noise**. `?Handle@CampaignPerformer@@…` goes from **0** rows under
`name_check` to **605** under `all`. Use `all` when you care about relocation
*addends*; use `name_check` when you are hunting a wrong callee. Both tools now
route through one `RELOC_RULER` table in `mcp_server.py`, and the `mismatches`
renderer tags same-symbol rows `addr_reloc` so the noise is visible as noise.

Anything concluded from `run_diff_inspect(diff_mode="raw")` **before
2026-08-19** was measured with `name_check`, not `all` — so it is a *sharper*
ruler than `none` for wrong-callee work, but it is not what the label said and
it never counted addends.

#### `diff_mode` still reaches only 3 of `run_diff_inspect`'s 11 modes

`mismatches`, `compare` and `save_baseline` build their objdiff command in
`mcp_server.py` and honour the ruler. The other eight do not:

* `diagnose`, `clusters`, `regswaps`, `offsets`, `replaces`, `attributed` —
  delegate to `scripts/analysis/diff_inspect.py`, which builds its *own* command
  and exposes no ruler switch. **That file is owned by another lane and was
  deliberately not modified here.**
* `stack-layout` — same, via `scripts/analysis/stack_layout.py`.
* `asm_listing` — a `/FAs` compiler listing; there is no objdiff run to rule.

All eight **print a banner saying the ruler was ignored** rather than returning a
normalized report under a raw label. If you need a relocation-aware answer, use
`run_objdiff(diff_mode="name_check")` or `run_diff_inspect(mode="mismatches")`.

> **Not affected:** the ubiquitous `Match: 100.0% normalized (99.8% raw)`
> strings. That "raw" is `raw_match_percent`, a different axis entirely, and
> none of the above changes it.

### `run_symbol_sweep`

The bulk shape. Three kinds:

* `vtable_slots` (default) — every `??_7` symbol **defined** in every target
  split object, one-shot `--include-data` diffed, keeping relocation rows where
  both sides name a symbol and the two names resolve to **different** addresses
  across `orig/373307D9/ham_xbox_r.map` + `build/373307D9/icf_aliases.map`.
  Equal addresses are a proven ICF fold and benign. `insert`/`delete` rows —
  where only one side has a slot at all — are a separate **length** tier.
  ~2,900 diffs, ~3 min at 16 workers.
* `data_symbols` — same engine, your own `symbol_glob` (`??_R4*`, `??_C@*`, …).
* `functions` — batch-diff a supplied symbol list through objdiff `--batch`
  (one process, not N). `--batch` refuses `--include-data`; that is an
  objdiff-side restriction, not a wrapper omission.

Every sweep leads with a COVERAGE block naming its **universe**, how many rows it
examined, and every drop reason. `max_symbols` truncation is labelled
`TRUNCATED`. This is the same contract `scripts/analysis/coverage.py` enforces;
`symbol_sweep` imports that module when present and falls back to a
same-shaped local implementation otherwise.

Sweeps are read-only: they diff already-built objects, never run ninja, never
write `decomp.db`. Safe to run alongside the build/permuter fleet.

CLI form, for scripts: `python3 -m scripts.orchestrator.symbol_sweep --project .
--kind vtable_slots --format json --out /tmp/x.json` (exit 3 = truncated,
4 = unaccounted rows).

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
