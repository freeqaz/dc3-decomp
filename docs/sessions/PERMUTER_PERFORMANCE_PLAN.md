# Permuter Performance Optimization Plan

**Date**: 2026-03-04
**Status**: Phase 1+2 implemented (dedup + cache + wibo FS cache + PCH + direct cl.exe)

## Measured Performance

Machine: AMD Ryzen 9 7950X (16C/32T)

### Single compile benchmarks

| Configuration | Wall time | Speedup |
|--------------|-----------|---------|
| Baseline (no cache) | 3.7s | 1x |
| **Wibo FS cache** | **1.15s** | **3.2x** |
| **FS cache + PCH** | **~0.9s** | **~4.1x** |
| From preprocessed .i | 0.9s | 4.1x |

### Infrastructure improvements

| Step | Wall time | Notes |
|------|-----------|-------|
| Direct wibo cl.exe (cached) | 1.15s | Scorer bypasses ninja, invokes cl.exe directly |
| objdiff single symbol | 0.45s | JSON output |
| objdiff --build (compile+diff) | ~1.6s | Combined with FS cache |
| objdiff --batch (3 symbols) | 0.6s | Amortized .obj load |

### Parallel compile scaling (with FS cache)

Pre-cache scaling was limited by filesystem contention (22K getdents64 per compile × N workers). With FS cache reducing syscalls by 84%, the sweet spot shifts from 4-8 to **8-16 workers**.

**Per-round cost (100 variants, sequential, with FS cache)**:
- 1 baseline build+objdiff: ~1.6s
- 100 variant builds: 100 × 1.15s = **115s**
- 100 objdiff calls: 100 × 0.45s = **45s**
- **Total: ~162s = 2.7 min per round** (was 6.3 min)

---

## IMPLEMENTED: Score Deduplication & Persistent Cache

**Files**: `scripts/permuter/score_cache.py`, `scripts/permuter/scorer.py`
**Tests**: `scripts/permuter/tests/test_score_cache.py` (11 tests)

Three dedup layers execute before expensive build+objdiff work:

### Layer 1: Source Dedup
If `md5(variant.source) == md5(baseline_source)`, skip everything. Returns baseline score immediately. Catches patterns that matched nothing (e.g., `alloca_intrinsic` on code with no `alloca` calls). Cost: ~1μs (one MD5 hash).

### Layer 2: Persistent Cache (SQLite)
Lookup `(symbol, source_md5)` in `permuter_cache.db`. If found, return cached `(match_pct, build_ok)`. Persists across sessions — if `batch_auto` retries a function, previously-tried variants are free. Cache DB uses WAL mode for concurrent access. Cost: ~100μs (SQLite lookup).

### Layer 3: Obj Hash Dedup
After building, `md5(.obj)` is checked against a session-local cache. Different source text can produce identical object code (e.g., `0` vs `0.0f` where compiler treats them the same). When the obj hash matches a previous variant, we skip objdiff entirely and reuse the cached score. Cost: ~50μs (file hash, ~36KB obj files).

### Stats Reporting
On scorer exit, cache stats are printed:
```
cache: 23/50 hits (source=5, obj=8, persistent=10, builds=27)
```
This tells you how many builds were avoided. In the example above, 23 of 50 variants were free.

### Architecture
```
Scorer.score(variant)
  ├── source_md5 == baseline_md5?  → return baseline_pct  [Layer 1]
  ├── ScoreCache.lookup_source()   → return cached score   [Layer 2]
  ├── build()                      → write + ninja
  ├── md5(.obj) in session cache?  → return cached score   [Layer 3]
  ├── objdiff()                    → full scoring
  └── ScoreCache.store()           → persist for next time
```

### Expected Impact
Depends heavily on function and pattern mix. Conservative estimates:
- **Source dedup**: ~10-20% of variants produce identical source (cheap patterns that don't match)
- **Obj hash dedup**: ~5-15% of variants produce identical .obj (semantic no-ops)
- **Persistent cache**: 100% hit rate on re-runs of the same function
- **Combined first-run savings**: ~15-30% fewer builds per round
- **Combined re-run savings**: up to 100% (all variants cached)

---

## IMPLEMENTED: Direct Compiler Invocation + Wibo FS Cache

### Direct cl.exe invocation

The scorer now extracts the compile command from `ninja -t commands` and invokes cl.exe directly, bypassing ninja's dependency checking. This saves ~50ms per variant.

**Implementation**: `Scorer._extract_compile_cmd()` parses the `cd ... && wibo ... cl.exe ...` command, then `_build()` runs it directly via `subprocess.run(shell=True)`.

### Wibo FS cache (`WIBO_FS_CACHE=1`)

Three caches added to wibo, gated by `WIBO_FS_CACHE=1` env var (set in build.ninja):

| Cache | Location | Hits/compile | Impact |
|-------|----------|-------------|--------|
| `pathFromWindows()` | `wibo/src/files.cpp` | Eliminates 97% of dir listings | **-2.5s** |
| `GetFileAttributesA` stat | `wibo/dll/kernel32/fileapi.cpp` | 9,055 hits / 1,676 misses | Part of overall |
| `canonicalPath()` | `wibo/src/files.cpp` | Dedup `weakly_canonical()` | Part of overall |
| `collectDirectoryMatches` dir listing | `wibo/dll/kernel32/fileapi.cpp` | 0 calls (unused path) | None |

**Key discovery**: The original profiling identified `collectDirectoryMatches` as the bottleneck, but it was never called. The real culprit was `resolveCaseInsensitive()` in `pathFromWindows()`, which does `directory_iterator` for case-insensitive file lookup on every Windows path resolution.

**Syscall reduction**: 121K → 19K total (getdents64: 22K→3K, newfstatat: 99K→16K)

**Debug stats**: Set `WIBO_FS_CACHE_STATS=1` to see hit/miss counts at process exit.

## Optimization 1: Parallel Variant Scoring (HIGH impact)

### Design

`Scorer.score_batch()` compiles multiple variants in parallel using `ThreadPoolExecutor`. Each worker writes source, compiles to a temp .obj via direct cl.exe invocation, then objdiff scores sequentially.

### Expected Impact (updated with FS cache)

| Metric | Before (3.7s/compile) | After (1.15s/compile + 8 workers) | Speedup |
|--------|--------|---------------------------|---------|
| 100 variants/round (cold) | 379s | ~25s | **15×** |
| 100 variants/round (warm cache) | 379s | ~15s | **25×** |
| 5-round hill climb | 32 min | ~2 min | **16×** |

---

## Optimization 2: Adaptive Pattern Selection (HIGH impact)

### Problem Statement

With 27+ patterns, the generator produces many variants that have no realistic chance of improving the score. The current `relevant()` filter is binary (yes/no) and the budget allocation uses static global win rates. Neither adapts to the specific mismatch profile.

### Approach A: Priority-Scored Relevance (hand-coded)

Replace binary `relevant() -> bool` with `priority(diagnosis) -> float` (0.0 = skip, 1.0 = definitely try). Budget allocation uses priorities instead of historical win rates.

```python
# Current: binary
def relevant(self, diagnosis): return bool(diagnosis.clusters)

# Proposed: scored
def priority(self, diagnosis) -> float:
    score = 0.0
    for d in diagnosis.diff_ops:
        if d.target_opcode == "fneg": score += 0.4  # strong signal
    if diagnosis.clusters: score += 0.1  # weak signal
    return min(score, 1.0)
```

Example mappings:
| Diagnosis Signal | High-priority Patterns | Low-priority Patterns |
|------------------|----------------------|---------------------|
| `beq↔bne` single, no clusters | `branch_polarity` (0.9) | `and_split` (0.1) |
| `beq↔bne` + clusters size 3+ | `and_split` (0.8), `single_return` (0.6) | `branch_polarity` (0.2) |
| `srwi↔srawi` | `sizeof_signed_cast` (0.9), `signed_unsigned` (0.8) | everything else (0.05) |
| `bl` mismatch to Max/Min | `max_to_conditional` (0.95) | most others (0.02) |
| `fneg↔frsp` scheduling | `negation_split` (0.9) | everything else (0.05) |

This is a hand-coded decision tree. Works immediately, no training data needed.

### Approach B: Contextual Bandit (learned)

Multi-armed bandit that learns per-diagnosis-profile which patterns win:

```python
# Context = frozenset of diagnosis features:
#   {"has_branch_diff", "has_clusters", "has_fneg", "cluster_size>3", ...}
#
# For each (context_hash, pattern_name), track wins/pulls in SQLite:
#   ucb(p) = wins[p]/pulls[p] + C * sqrt(ln(total_pulls) / pulls[p])
#
# Always try the pattern with highest UCB score.
# sqrt term = exploration bonus (try under-explored patterns)
```

Storage: SQLite table `(context_hash, pattern_name, wins, pulls)`. ~30 lines of code on top of the existing cache infrastructure.

**Cold start**: First ~50 functions use uniform exploration. After that, the bandit dominates static win rates.

**Learning signal**: After scoring a variant, if `score > baseline`, record `win=1` for that (context, pattern) pair. Otherwise `win=0`.

### Approach C: Combined (recommended)

1. Start with hand-coded priorities (Approach A) — immediate improvement, no training data
2. Log (context, pattern, outcome) tuples to the persistent cache
3. After accumulating ~100 function-runs of data, switch to bandit (Approach B)
4. The bandit overrides hand-coded priorities where it has sufficient data (>10 pulls)

### Framing: Stochastic Combinatorial Optimization

The search space maps to combinatorial optimization:
- **State**: C++ source text for a function
- **Objective**: match% (0-100), discrete jumps, non-smooth
- **Evaluation cost**: ~1-3s per point (build + objdiff)
- **Moves**: each pattern is an axis in a discrete, sparse landscape

Key property: **no gradients**. Most moves score identically to baseline. A few jump +0.5-5%. Very rarely one jumps +20%. This rules out gradient descent / continuous optimization.

Closest analogues:
- **Multi-armed bandits** (pattern selection)
- **Genetic programming** (source-level mutations)
- **Bayesian optimization** (expensive evaluations, surrogate model)
- **Simulated annealing** (accepting worse solutions to escape local optima)

The bandit approach is most practical because:
1. Evaluation is expensive → minimize total evaluations
2. Arms (patterns) have stable payoff distributions per context → exploitable
3. The context (diagnosis) is a strong prior → contextual bandits fit perfectly

---

## Optimization 3: objdiff --build Integration (MEDIUM impact)

### Problem

The scorer makes two subprocess calls per variant: `ninja build` then `objdiff-cli diff`. Each subprocess spawn adds ~10-20ms overhead, and objdiff already has `--build` + `--incremental` flags that combine both steps.

### Design

Replace the two-step `_build()` + `_run_objdiff()` with a single `objdiff-cli diff --build --incremental` call.

### Measured overhead

- Two calls: 3.3s + 0.45s = 3.75s
- Single `--build`: 3.7s
- **Saves ~50ms per variant** (subprocess spawn overhead)
- 100 variants × 50ms = **5s per round** — modest but free

### Compatibility Note

`objdiff --build --incremental` invokes ninja internally. If we move to direct compiler invocation (Optimization 1), this becomes irrelevant for the parallel path. But it's still useful as a quick win for the sequential fallback path.

---

## Optimization 4: objdiff Batch Scoring (MEDIUM impact)

### Problem

After parallel compilation produces N .obj files, we still call objdiff N times (once per symbol). Each call loads the base .obj, disassembles both sides, and diffs — but the base .obj is the same every time.

### Current Batch Mode

`objdiff-cli diff --batch` reads symbols from stdin, groups by unit, loads each .obj pair once, and diffs all symbols. Used by `sync_objdiff.py`. But this operates in project mode (`-p .`) where it finds .obj paths from `objdiff.json`.

### Design: Batch Scoring for Variants

Since all variants compile the same TU, all produced .obj files contain the same symbol. We need to diff the same symbol from N different target .obj files against the same base .obj.

**Recommendation**: Start with sequential objdiff after parallel compile. The compile step dominates; objdiff is already fast enough that batch optimization is lower priority. Revisit if compile parallelism makes objdiff the new bottleneck.

---

## Optimization 5: Composition Reparse Caching (LOW impact)

### Problem

The composer calls `reparse_variant(ctx, a_variant.source)` for each stage-A variant. This re-parses the entire source file through tree-sitter (~50-100ms per call). With 10 stage-A variants × 3 composition pairs = 30 reparses = ~3s overhead.

### Fix

Cache by source hash in the composer:
```python
_reparse_cache: dict[int, FunctionContext] = {}

def _cached_reparse(ctx, source):
    key = hash(source)
    if key not in _reparse_cache:
        _reparse_cache[key] = reparse_variant(ctx, source)
    return _reparse_cache[key]
```

### Impact

Saves ~3s per round. Negligible compared to compile time but essentially free to implement.

---

## Optimization 6: batch_auto.py Parallelism (HIGH impact for batch runs)

### Problem

`batch_auto.py` processes functions sequentially. `batch_sweep.py` already has `ProcessPoolExecutor` parallelism grouped by source file. `batch_auto` should use the same pattern.

### Fix

Port the `ProcessPoolExecutor` pattern from `batch_sweep.py` into `batch_auto.py`:
- Group candidates by source file (already done)
- Submit each source-file group as a parallel task
- Within each group, process functions sequentially (shared source file)
- Add `--jobs N` flag (default: 4)

### Impact

With `--jobs 4` on a 50-function batch: **~4× speedup** (10 hours → 2.5 hours).

Combined with per-function parallel scoring (Opt 1): **~16× total** (10 hours → ~37 minutes).

---

## Optimization 7: Build System Speedups (HIGH leverage)

### Ninja Overhead

Measured: ninja adds **~0ms** to a single .obj build — the tool startup is dominated by wibo+cl.exe at 3.3s. No optimization needed here.

### Compiler Warm-up / Caching

wibo runs MSVC's X360 cl.exe through Wine syscall translation. The 3.3s compile time breaks down roughly as:
- wibo startup + Wine init: ~0.3s
- cl.exe loading + preprocessing: ~1.5s
- Optimization + codegen: ~1.0s
- Object file writing: ~0.5s

**Possible speedups**:
1. **Precompiled headers (PCH)**: MSVC `/Yu` flag. Would cache preprocessed headers across compiles. Estimated savings: 0.5-1.0s per compile. **Risk**: PCH under wibo is untested; MSVC PCH requires exact flag compatibility.

2. **ccache-equivalent**: Hash source + flags → skip compile if .obj exists. Useless for the permuter (every variant is unique) but helpful for batch rebuilds.

3. **wibo persistent mode**: Keep a wibo process alive to avoid repeated Wine init. Would save ~0.3s per compile. **Risk**: wibo doesn't support this natively; would need forking it.

4. **Faster preprocessing**: The biggest chunk is preprocessing includes. A "slim build" mode that precomputes the preprocessed output and then only re-preprocesses the changed function body could save significant time. **Very high complexity.**

### Direct .obj Diffing

For the permuter's use case, we don't need the full project infrastructure. `objdiff-cli diff -1 target.obj -2 base.obj` with explicit paths avoids project config scanning. Measured: saves ~50ms per call.

---

## Implementation Priority

| # | Optimization | Impact | Effort | Status |
|---|-------------|--------|--------|--------|
| — | Score dedup + persistent cache | **15-30% fewer builds + 100% re-run** | Easy | **DONE** |
| — | Wibo FS cache | **3.7s → 1.15s/compile (3.2×)** | Medium | **DONE** |
| — | Direct cl.exe invocation | **-50ms/variant** | Easy | **DONE** |
| — | PCH build rules | **~0.25s/compile** | Easy | **DONE** |
| 1 | Parallel variant scoring | **8-16× per round** | Medium (2-3 sessions) | Partially done (`score_batch`) |
| 2 | Adaptive pattern selection | **2-5× fewer wasted variants** | Medium (1-2 sessions) | Planned |
| 3 | objdiff --build integration | **~5s/round** | Easy (30 min) | Planned |
| 4 | objdiff batch scoring | **~20s/round** | Medium (1 session) | Planned |
| 5 | Composition reparse cache | **~3s/round** | Easy (30 min) | Planned |
| 6 | batch_auto parallelism | **4× for batch runs** | Easy (1 session) | Planned |
| 7 | Preprocessed .i compilation | **-0.25s/variant** | Medium | Deferred |

**Recommended next steps**: 1 (parallel scoring) → 2 (adaptive selection) → 6 (batch parallelism)

The wibo FS cache + direct cl.exe invocation provide the foundation. Parallel scoring is now dramatically more effective since per-compile cost dropped from 3.7s to 1.15s and filesystem contention is eliminated.

---

## Codebase Audit Results (2026-03-04)

### Per-Unit Sweep

| Unit | Candidates | Wins | Best Win | Notes |
|------|-----------|------|----------|-------|
| system/obj/DataFunc | 89 | 0 | N/A | DEF_DATA_FUNC macro limitation |
| system/char/CharEyes | 46 | 3 | +6.22% ProceduralBlinkUpdate | |
| system/rndobj/Utl | 47 | 4 | +7.05% GetNormalMapTextures | Composed variable_extraction+declaration_reorder |
| system/rndobj/Text | 70 | 3 | +0.61% FontMap::AllocateMeshes | |

### Known Limitations

**DEF_DATA_FUNC macro extraction**: Functions defined via `DEF_DATA_FUNC(DataAdd, ...)` macros in DataFunc.cpp cannot be extracted by tree-sitter. The parser sees `DEF_DATA_FUNC` as the function name rather than the expanded C++ definition. This affects ~89 functions in DataFunc.cpp alone. Workaround: none (fundamental tree-sitter limitation with unexpanded macros).

**Concurrency model**: Within each function, `score_batch()` uses `ThreadPoolExecutor` with `os.cpu_count()` workers for parallel compilation. The unit-level runner processes functions sequentially. Cross-function parallelism could be added but within-function parallelism already saturates CPU cores during compilation.

---

## Appendix: Key File Locations

- `scripts/permuter/score_cache.py` — Persistent SQLite cache + dedup layers
- `scripts/permuter/scorer.py` — Scoring engine with 3-layer dedup
- `scripts/permuter/composer.py` — Two-step composition with reparse
- `scripts/permuter/hill_climber.py` — Iterative improvement loop
- `scripts/permuter/batch_auto.py` — Sequential batch processor
- `scripts/permuter/batch_sweep.py` — Parallel batch processor (reference)
- `scripts/permuter/generator.py` — Variant generation with budget allocation
- `scripts/permuter/tests/test_score_cache.py` — Cache layer tests
- `configure.py` — Build system generation (compile commands)
- `build.ninja` — Generated ninja rules
- `permuter_cache.db` — Persistent score cache (gitignored, auto-created)

## Appendix: Compile Command Template

```bash
cd $in_dir && wibo \
  WIBO_COMPUTER_NAME='9QVZU3' \
  WIBO_FS_CACHE='1' \
  WIBO_PATH_MAP='e:/lazer_build_gmc1/system/src/=$abs_src_system;...' \
  $cl_exe \
  /I e:/lazer_build_gmc1/system/src/stlport \
  /I $src/xdk/LIBCMT \
  /I e:/lazer_build_gmc1/system/src \
  /I e:/lazer_build_gmc1/lazer/src \
  /I e:/lazer_build_gmc1/system/src/oggvorbis \
  /I e:/lazer_build_gmc1/system/src/synth/tomcrypt \
  /I e:/lazer_build_gmc1/system/src/net/curl/include \
  /I $src \
  /nologo /wd4355 /wd4164 /c /GR /O1 /Oi /EHsc /TP \
  /Fo$output_obj $source_file
```

Per-target variables come from `build.ninja` (generated by `configure.py`). The parallel scorer needs to extract these for the target .obj and substitute `/Fo` with a temp path.

## Appendix: Cache Schema

```sql
CREATE TABLE IF NOT EXISTS score_cache (
    symbol      TEXT NOT NULL,
    source_md5  TEXT NOT NULL,
    obj_md5     TEXT,
    match_pct   REAL NOT NULL,
    build_ok    INTEGER NOT NULL DEFAULT 1,
    timestamp   REAL NOT NULL,
    PRIMARY KEY (symbol, source_md5)
);
```

Future extension for bandit learning:
```sql
CREATE TABLE IF NOT EXISTS bandit_outcomes (
    context_hash  TEXT NOT NULL,    -- hash of diagnosis feature set
    pattern_name  TEXT NOT NULL,
    wins          INTEGER NOT NULL DEFAULT 0,
    pulls         INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (context_hash, pattern_name)
);
```
