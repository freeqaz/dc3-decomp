# Next Steps Plan

**Date:** 2026-02-27
**Status:** Active — phased plan toward DC3 decomp + boot

## Context: Where We Are

### Progress Snapshot
- **29,927 COMPLETE** (92.6%), **1,674 AT_LIMIT** (5.2%), **727 remaining** (2.2%)
- **368 Matching units** linked into hybrid binary
- **0 link errors**, `/FORCE:MULTIPLE` only (13,400 LNK4006 cosmetic warnings — see [FORCE_MULTIPLE_ELIMINATION.md](FORCE_MULTIPLE_ELIMINATION.md))
- **Decomp XEX boots** in Xenia headless (main loop, all imports resolved)
- **Normalized match%** available in objdiff fork (relocation-filtered alongside raw)

### Completed (Prior Sessions)
- Data stubs, ALTERNATENAME stubs, `/FORCE:UNRESOLVED` dropped (1017 → 0 errors)
- Anon namespace patcher + `WIBO_COMPUTER_NAME` + `WIBO_PATH_MAP` + `??_C@` string hashes
- Docs updated: LINKING_STATUS.md, BUILD_ROADMAP.md, CLEAN_LINK_PROJECT.md
- link_glue.cpp cleanup: 8 obsolete stubs removed (756 → 746 LNK4006)

---

## Phase 1: Fix Build Errors — DONE

Build errors in BustAMovePanel.cpp and HamDirector.cpp fixed. `ninja` completes cleanly.

---

## Phase 2: Batch Promotion Script

**Goal:** Systematically identify and promote functions that are effectively done, using Unicorn behavioral testing as the primary verdict driver and objdiff pattern analysis for supplementary context.

### Design Rationale

objdiff's analysis engine detects 14 mismatch patterns and assigns fixability verdicts using heuristic thresholds (e.g., "merged ratio >= 80% → AT_LIMIT"). These thresholds guess at behavioral equivalence without measuring it. We have Unicorn, which directly tests whether two implementations produce the same results. Unicorn is the ground truth for "does this function behave correctly?"

**Evidence from testing (2026-02-27):**

| Function | Raw Match | Mismatches | Unicorn |
|----------|-----------|-----------|---------|
| RndGenerator::Load | 98.9% | r10↔r11 regswap + MakeString const-ness | EQUIVALENT |
| CharCuff::Highlight | 98.9% | f29↔f30 regswap + float pool addresses | EQUIVALENT |
| RndTransformable::Copy | 98.6% | addi↔subi sign flip + merged calls | EQUIVALENT |

All three have <100% match but identical behavior. Register swaps, sign encoding differences, and constant pool address drift are cosmetic — they don't affect runtime behavior. Marking these COMPLETE is honest and correct for the goal of booting DC3.

### Decision Tree

Unicorn is the primary decision maker. objdiff patterns provide context for DIVERGENT cases.

```
For each non-COMPLETE function:

1. Run objdiff analysis
   → Get normalized_match_percent, raw_match_percent, patterns[], verdict

2. If normalized_match_percent == 100%:
   → COMPLETE
   Reason: all diffs are relocation noise (symbol addresses, const pool
   refs, branch offsets to equivalent targets). Normalized matching
   already filters these out. <100% raw is expected and acceptable.

3. If normalized_match_percent < 100%:
   → Run Unicorn behavioral test (dual-fixture: zero-fill + 0xCD-fill)

   3a. Unicorn EQUIVALENT:
       → COMPLETE
       Reason: behavior is identical despite assembly differences.
       These are cosmetic diffs (register allocation, instruction
       encoding, constant pool layout). Record both normalized and
       raw match% for transparency.

   3b. Unicorn DIVERGENT:
       Examine objdiff patterns to classify:

       i.  unicorn_class in {build_env, merged_call, merged_arg, fpr_precision}:
           → AT_LIMIT
           Reason: divergence is from build environment artifacts,
           not source code bugs.

       ii. Has ONLY unfixable patterns remaining:
           (LINKER_MERGED, DEAD_STORE_ELIMINATION,
            ANONYMOUS_NAMESPACE_HASH, STACK_SPILL_SCHEDULING)
           → AT_LIMIT with pattern evidence

       iii. Has fixable patterns:
            (BOOL_MASK, CONTROL_FLOW, COMPARISON_STYLE,
             STATIC_GUARD_COUNTER, DYNAMIC_CAST_MISMATCH,
             ALLOCA_MISMATCH, SCOPE_COUNTER_MISMATCH,
             PROLOGUE_MISMATCH, COMMUTATIVE_OP_ORDER, OFFSET_SWAP,
             REGISTER_SWAP)
            → stays workable, tagged with fix suggestions

       iv. unicorn_class == "logic" with no attributed patterns:
           → NEEDS_INVESTIGATION (real behavioral bug, unknown cause)

   3c. Unicorn SKIPPED or ERROR:
       → Fall back to objdiff verdict only (no auto-promote to COMPLETE,
         may promote to AT_LIMIT if objdiff verdict is AtLimit)
```

**Pattern fixability reference:**

| Pattern | Fixable? | Notes |
|---------|----------|-------|
| BOOL_MASK | **Yes** | Extract to local `bool` variable or add `(bool)` cast |
| CONTROL_FLOW | Yes | Branch polarity, if/else inversion, comparison operators |
| COMPARISON_STYLE | Yes | `> 0` vs `!= 0`, `>=` vs `>` |
| STATIC_GUARD_COUNTER | Yes | Reorder static local definitions in TU |
| DYNAMIC_CAST_MISMATCH | Yes | Use `GetObj<T>()` instead of `dynamic_cast<T*>()` |
| ALLOCA_MISMATCH | Yes | Use `_alloca` (intrinsic) not `alloca` (CRT wrapper) |
| SCOPE_COUNTER_MISMATCH | Yes | Remove extra braces around static locals |
| PROLOGUE_MISMATCH | Yes | Reorder variable declarations |
| COMMUTATIVE_OP_ORDER | Yes | Swap operand order in `a + b` → `b + a` |
| OFFSET_SWAP | Yes | Swap field access order |
| REGISTER_SWAP | Sometimes | Try declaration reorder (3-4 attempts max) |
| LINKER_MERGED | **No** | ICF linker artifact — call target equivalent |
| DEAD_STORE_ELIMINATION | **No** | Compiler optimization difference |
| ANONYMOUS_NAMESPACE_HASH | **No** | TU hash mismatch (mitigated by patcher) |

### Scope

**Include — re-check everything:**
- All ~727 remaining workable functions
- All ~1,674 AT_LIMIT functions (many stale — marked before `__FILE__` fix and anon namespace patcher)
- Any function not currently COMPLETE at 100% in the DB

**Exclude:**
- Functions already COMPLETE at 100% (nothing to gain)
- SDK/library functions (16,021 excluded in DB)

### Requirements

#### R1: CLI Interface

```
scripts/batch_promote.py [OPTIONS]

Modes:
  --dry-run           Default. Print report, don't modify DB.
  --apply             Write verdict changes to decomp.db.

Filtering:
  --unit PATTERN      Filter to specific unit glob (e.g., 'system/char/*')
  --min-pct N         Only process functions at or above N% (default: 0)
  --max-pct N         Only process functions at or below N% (default: 99.99)

Performance:
  --skip-unicorn      Skip Unicorn tests (faster, less accurate — uses
                      objdiff verdict only, will not promote to COMPLETE)
  --skip-build        Assume objects are already built (skip ninja)
  -j JOBS             Parallel workers for Unicorn (default: cpu_count)

Unicorn tuning:
  --no-coload         Disable callee co-loading (faster, less realistic)
  --no-typed          Disable typed object memory (use zero-fill only)
  --unicorn-timeout N Unicorn timeout in microseconds (default: 5000000)

Output:
  --verbose           Per-function detail (symbol, match%, patterns, decision)
  -o FILE             Write full JSON report to file
```

#### R2: Per-Function Pipeline

For each function in scope:

1. **Resolve paths**: Look up unit in `objdiff.json` to get `target_path` (decomp .obj) and `base_path` (original .obj).

2. **Build** (unless `--skip-build`): Run `ninja <target_path>` to ensure the decomp .obj is current.

3. **Run objdiff**: Invoke `objdiff-cli diff <SYMBOL> -p . -u <UNIT> --format json --verdict` and parse JSON output. Extract:
   - `normalized_match_percent` (relocation-filtered)
   - `raw_match_percent` (strict byte comparison)
   - `analysis.patterns[]` (detected pattern types + details)
   - `analysis.unattributed_mismatches` (mismatches not explained by any pattern)
   - `verdict.classification` (objdiff's heuristic verdict)

4. **Run Unicorn** (unless `--skip-unicorn` or normalized == 100%):
   Use the full Unicorn pipeline with all advanced features enabled by default.
   See R8 for Unicorn configuration details.

5. **Apply decision tree** (see above) to determine promotion verdict.

6. **Record result**: Accumulate per-function result with full evidence for reporting.

#### R3: DB Updates (--apply only)

Write to `decomp.db` `functions` table:

| Column | What to write | Notes |
|--------|--------------|-------|
| `verdict` | COMPLETE or AT_LIMIT | Never downgrade existing COMPLETE |
| `verdict_reason` | Evidence string | e.g., "unicorn_equivalent", "unfixable_linker_merged", "unicorn_build_env" |
| `current_percent` | `normalized_match_percent` from objdiff | Consistent with current MCP behavior |
| `unicorn_verdict` | EQUIVALENT / DIVERGENT / SKIPPED / ERROR | From live Unicorn run |
| `unicorn_class` | logic / build_env / regalloc / merged_call / etc. | Only if DIVERGENT |
| `unicorn_confidence` | high / input_sensitive / stable_divergent | From dual-fixture |
| `unicorn_reason` | Detailed divergence reason string | From `classify_divergence()` |
| `unicorn_tested_at` | Current timestamp | |
| `updated_at` | Current timestamp | |

**Safety rules:**
- Never downgrade COMPLETE → AT_LIMIT or COMPLETE → workable
- Never write a verdict without evidence (pattern or unicorn result)
- In dry-run mode, write nothing — only print what would change

#### R4: Output Format

**Summary (always printed):**
```
=== Batch Promotion Report ===
Scanned: N functions (M skipped: build error / excluded)

Promotions:
  → COMPLETE:  X
    - normalized 100% (relocation only):     A
    - unicorn equivalent (high confidence):   B
    - unicorn equivalent (fixture-sensitive):  C
  → AT_LIMIT:  Y
    - unfixable patterns only:  D
    - unicorn build_env:        E
    - unicorn merged_call:      F
    - unicorn fpr_precision:    G

Remaining workable: Z
  - fixable patterns found:  H  (tagged with suggestions)
  - needs investigation:     I  (no patterns, unicorn divergent/logic)

Unchanged: J  (already COMPLETE or no new info)
Errors:    K  (build failure / unicorn timeout)

Metrics (Work Stream 4C):
  Normalized: X.X% → Y.Y% (if applied)
  Raw:        X.X% → Y.Y% (if applied)

Unicorn confidence breakdown:
  High:              N  (both fixtures agree)
  Fixture-sensitive:  N  (fixtures disagree — weaker signal)
```

**Per-function detail (--verbose):**
```
[COMPLETE] RndGenerator::Load (system/rndobj/Gen)
  normalized=98.9% raw=98.9%
  unicorn=EQUIVALENT confidence=high (coloaded: 3 callees)
  patterns: REGISTER_SWAP(r10↔r11, 11 insns)
  reason: unicorn_equivalent

[WORKABLE] AppLabel::SetCreditsText (lazer/meta_ham/AppLabel)
  normalized=97.2% raw=97.2%
  unicorn=DIVERGENT class=logic confidence=stable_divergent
  patterns: CONTROL_FLOW(1 branch diff)
  suggestion: Try branch polarity steering (docs/decomp/patterns/fixable-control-flow.md)

[AT_LIMIT] SomeFunc (system/foo/Bar)
  normalized=97.5% raw=96.1%
  unicorn=DIVERGENT class=merged_call confidence=high
  patterns: LINKER_MERGED(2 calls to merged_82331360)
  reason: unicorn_merged_call
```

**JSON report (-o FILE):**
Array of per-function records with all fields for audit/analysis.

#### R5: Parallelism

- **Phase A — Build**: Run `ninja` once to build all objects (unless `--skip-build`)
- **Phase B — Analyze**: Process units sequentially. For each unit:
  1. Parse COFF files once (`COFFParser` for decomp + original)
  2. Create reusable `UnicornEngine` instance
  3. For each function in the unit:
     - Run `objdiff-cli diff` (subprocess)
     - Run Unicorn with shared engine + parsed COFFs (in-process)
  4. Collect results
- **Phase C — Report/Apply**: Aggregate results, print summary, write DB

Grouping by unit is critical: COFF parsing (~50ms each) and engine creation
(~50ms) are amortized across all functions in a unit. With `-j`, multiple
units can be processed in parallel via `ProcessPoolExecutor`.

#### R6: Error Handling

- Build failure for a unit → skip all functions in that unit, log warning
- objdiff crash for a function → skip, log warning, don't promote
- Unicorn timeout/error → fall back to objdiff verdict (decision tree step 3c)
- DB write failure → abort with error (don't partially apply)
- COFF parse failure → skip unit, log warning

#### R7: Idempotency

- Safe to re-run: already-COMPLETE functions are skipped
- Re-running after code changes picks up new match% and re-evaluates
- `--apply` followed by `--apply` is a no-op if nothing changed
- Unicorn results are always fresh (no stale cache — tests run live each time)

#### R8: Unicorn Configuration

Use all advanced Unicorn features by default for maximum accuracy:

| Feature | Default | Flag to disable | Why |
|---------|---------|-----------------|-----|
| **Dual-fixture** | ON | (always on) | Runs zero-fill AND 0xCD-fill, produces confidence score (`high` = both agree, `fixture_sensitive` = disagree). Essential for reliable verdicts. |
| **Coloading** | ON | `--no-coload` | Loads intra-TU callees via BFS so `bl` to local functions executes real code instead of hitting a trampoline stub. Makes comparison realistic — without this, any function that calls a local helper would appear to diverge on call args. |
| **Typed memory** | ON | `--no-typed` | Populates object memory region with randomized struct instances from `struct_db` instead of all-zeros. Exercises field access paths that zero-fill would skip (e.g., conditional branches on member values). |
| **Reusable engine** | ON | (always on) | Single `UnicornEngine()` per unit, reused across all functions. Saves ~50ms teardown/recreation per function. |

**Unicorn API calls (per function):**

```python
from scripts.unicorn_runner.run import (
    resolve_unit, run_dual_comparison_inner, _run_comparison_core
)
from scripts.unicorn_runner.coff import COFFParser
from scripts.unicorn_runner.comparator import classify_divergence
from scripts.unicorn_runner.engine import UnicornEngine

# Per-unit setup (amortized):
decomp_coff = COFFParser(decomp_obj_path)
orig_coff = COFFParser(orig_obj_path)
engine = UnicornEngine()

# Per-function:
exit_code, output = run_dual_comparison_inner(
    symbol,
    decomp_coff,
    orig_coff,
    coload=True,          # co-load intra-TU callees
    engine=engine,        # reuse engine
    verbose=False,
    timeout=5_000_000,
    json_output=True,     # structured output for parsing
)
# exit_code: 0=EQUIVALENT, 1=DIVERGENT, 2=ERROR, 3=SKIPPED

# For structured access to divergence class + confidence:
# Parse JSON output or use _run_comparison_core() for ComparisonBundle
```

**Unicorn divergence classes and their verdict mapping:**

| Unicorn Class | Meaning | Script Verdict |
|---------------|---------|---------------|
| (EQUIVALENT) | Behavior identical | **COMPLETE** |
| `build_env` | `__FILE__` strings, globals pointers | **AT_LIMIT** |
| `merged_call` | Call count mismatch at ICF-merged symbol | **AT_LIMIT** |
| `merged_arg` | Call arg mismatch at ICF-merged symbol | **AT_LIMIT** |
| `fpr_precision` | Float return differs (FMA/rounding) | **AT_LIMIT** |
| `regalloc` | Same calls, different register values only | **AT_LIMIT** |
| `stack_layout` | Call args differ in stack region | **AT_LIMIT** |
| `object_memory` | Memory diffs in object region | **workable** (logic error) |
| `call_count` | Call count mismatch (no merged indicators) | **workable** (real bug) |
| `call_arg` | Call arg mismatch (no merged/stack hints) | **workable** (real bug) |
| `return_value` | Integer return value mismatch | **workable** (real bug) |
| `error` | Execution error mismatch | **workable** (real bug) |
| `logic` | Unclassified divergence | **workable** (real bug) |

**Confidence interpretation:**

| Confidence | Meaning | Treatment |
|------------|---------|-----------|
| `high` | Both fixtures (zero + 0xCD) agree | Strong signal — trust verdict |
| `fixture_sensitive` | Fixtures disagree | Weaker signal — still promote if EQUIVALENT, but flag in report |
| `stable_divergent` | DIVERGENT in both fixtures | Strong divergence signal |

#### R9: Integration with diagnose.py

The existing `scripts/unicorn_runner/diagnose.py` module combines objdiff + Unicorn
into a unified recommendation. The batch_promote script should use `diagnose_single()`
as a reference implementation for the per-function pipeline, but extend it with:

1. **DB writes** (diagnose.py is output-only)
2. **Normalized match%** from objdiff fork (diagnose.py uses raw fuzzy_match_percent)
3. **Pattern-aware verdict mapping** (diagnose.py uses DONE/SKIP/FIX; we need COMPLETE/AT_LIMIT/workable with specific reasons)
4. **Batch-level aggregation** and reporting

The `diagnose_single()` function signature:
```python
diagnose_single(symbol, decomp_coff, orig_coff, verbose=False,
                coload=True, dual_fixture=True) → dict
# Returns: {symbol, demangled, match_pct, objdiff_class, unicorn_verdict,
#           unicorn_summary, recommendation, confidence, divergence_class}
```

Use this as the core analysis step, then layer our decision tree on top.

### Dependencies

| Tool | Role | Import/Invocation |
|------|------|-------------------|
| `objdiff-cli` (fork) | Pattern analysis + match% | Subprocess: `objdiff-cli diff SYMBOL -p . -u UNIT --format json --verdict` |
| `scripts/unicorn_runner/run.py` | Behavioral testing | Python import: `resolve_unit()`, `run_dual_comparison_inner()`, `_run_comparison_core()` |
| `scripts/unicorn_runner/diagnose.py` | Combined analysis | Python import: `diagnose_single()` (reference for pipeline) |
| `scripts/unicorn_runner/comparator.py` | Divergence classification | Python import: `classify_divergence()` |
| `scripts/unicorn_runner/coff.py` | COFF parsing | Python import: `COFFParser` (reuse per unit) |
| `scripts/unicorn_runner/engine.py` | Execution engine | Python import: `UnicornEngine` (reuse per unit) |
| `decomp.db` | Function database | sqlite3: read scope, write promotions + unicorn fields |
| `objdiff.json` | Unit → path mapping | JSON: resolve target_path/base_path |
| `ninja` | Build system | Subprocess: build objects before analysis |

### What This Script Does NOT Do

- Does not duplicate `batch_check` (MCP tool that marks 100% raw matches as COMPLETE)
- Does not replace manual decomp work — it only promotes functions that are already behaviorally correct
- Does not hide real bugs — DIVERGENT+logic functions stay workable with fix suggestions
- Does not inflate metrics — reports both normalized and raw match% side by side

---

## Phase 3: Push Remaining Functions to 100%

**Goal:** Decomp the ~727 remaining workable functions (fewer after Phase 2 promotions).

### Struct/Header Investigation Results (2026-02-27)

A deep investigation of all suspected struct layout issues found that **class layouts are correct
across the board**. The initial hypothesis that struct/header fixes were a major remaining
blocker was wrong. Every investigated class (FlowNode, CharClip, CharBonesSamples, HamDirector,
FontMap::Page, VorbisReader) had correct field offsets confirmed by Ghidra binary analysis and
m2c decompilation of the target.

Offset deltas that appeared to be struct errors were actually **stack frame size differences**
caused by `__FILE__` string length mismatches and compiler scheduling. See
`docs/sessions/2026-02-27-saturation-analysis.md` for the original hypotheses and
`docs/decomp/LOW_HANGING_FRUIT.md` for the corrected analysis.

### What Actually Blocks the Remaining Functions

Based on a 25-function sample across all match% ranges:

| Root Cause | Prevalence | Fixable? | Est. Functions |
|------------|-----------|----------|----------------|
| **Code logic bugs** (wrong impl) | ~25% | **Yes** | **~180** |
| **Register swaps** (GPR/FPR) | ~80% | Rarely | ~580 |
| **Symbol relocation noise** | ~60% | No | ~440 |
| **Control flow differences** | ~50% | Sometimes | ~360 |
| **`__FILE__` stack frame sizing** | ~25% | No | ~180 |
| **Anon namespace hash** | ~8% | No | ~60 |

Most functions have 2-3 issues simultaneously. The ~180 with fixable code logic bugs
are where effort should focus. The rest will largely end up AT_LIMIT.

### Priority Targets (by impact)

#### P0: Flow System (30 workable functions, 55-98%)

The single largest cluster of improvable functions. Issues are **code logic**, not struct layout.

**FlowPtr Copy Pattern** (affects 5-7 Copy functions):
All `Flow*::Copy` methods use `FlowPtr::operator=` which generates compound assignment.
The target does field-by-field copy (save mObjName/mState, call SetObjConcrete, store back).
Fixing this pattern improves: FlowCommand::Copy (53.3%), FlowDistance::Copy (58.7%),
FlowAnimate::Copy (77.3%), FlowRun::Copy (71.4%), FlowTrigger::Copy (80.6%),
FlowSetProperty::Copy (75.4%), FlowSound::Copy (87.2%).

**FlowSetProperty rewrites** (3 functions, 55-73%):
- `Load` (55.7%): Wrong `INIT_REVS(4,0)` should be `INIT_REVS(3,0)`, version branching
  logic needs rewrite with real `ReadEndian`/`DataNode::Load` calls
- `Execute` (55.3%): Missing ~10 FLOW_LOG/debug TextStream calls, `unk_0xE8` should
  reference `mEventsRegistered` in some places
- `PropertyTask::PropertyTask` (73.3%): Constructor logic

**FlowCommand::Load** (94.5%): Add null checks on `GetOwnerFlow()` before `->Dir()`.

#### P1: HamDirector Code Fixes (6 functions, 69-97%)

All struct offsets verified correct. Issues are source code bugs:

- `ReactToCollision` (87.6%): `ceil(beatSum / 4.0f)` should be
  `ceil(beatSum * 0.25f) * 4.0f` (round to nearest measure), plus const/non-const Node()
- `ClosestMove` (69.3%): Incomplete loop body — best-match tracking (numlower/i17 never
  update `out`)
- `FindNextDircut` (93.6%): Branch polarity inversion (bne vs beq)
- `UnloadMergers` (84.2%): Loop structure around TheHamWardrobe null checks

#### P2: Other High-Value Targets

| Unit | Functions | Range | Primary Issue |
|------|-----------|-------|---------------|
| system/rndobj/Text | 6 | 57-94% | Code logic (not FontMap::Page struct) |
| system/ui/UILabel | 6 | 67-94% | Register allocation, code structure |
| system/ui/UIFontImporter | 6 | 56-95% | Code logic |
| system/char/CharClip | 7 | 64-97% | `__FILE__` stack sizing + regswaps |
| system/char/CharLipSync | 6 | 87-97% | Inner class logic (Generator, PlayBack) |
| system/meta/StorePanel | 9 | varies | Untriaged |
| system/rndobj/HiResScreen | 8 | varies | Untriaged |

### Strategy
After Phase 2, the workable set will be smaller and tagged with specific fix suggestions.
Prioritize by:
1. **FlowPtr copy pattern** — one pattern fix, 5-7 functions improved 20-40pp each
2. **FlowSetProperty rewrite** — 3 functions, highest total pp gain
3. **HamDirector code bugs** — 4 fixable functions with specific known fixes
4. **Unit completions** — units with 1-2 remaining functions for quick 100%
5. **Bulk AT_LIMIT** — report ~400 functions that are effectively unfixable

### Parallel Agent Strategy
Use `docs/decomp/SUBAGENT_STRATEGY.md` for batch decomp work:
- Assign units to parallel worktree agents
- Each agent focuses on pushing one unit to 100%
- Use pattern docs to guide fixes

---

## Phase 4: Runtime Testing

**Goal:** Validate decomp correctness via Xenia headless execution.

### Current State
- Decomp XEX boots, enters main loop, all imports resolved
- Thread 6 stuck at `RtlEnterCriticalSection` (CRT init deadlock)
- Fake Kinect and save/load working

### Next Steps
- Profile runtime behavior — identify crash/hang points
- Compare execution traces between original and decomp XEX
- Fix any behavioral divergences found during testing

---

## Work Stream 4: Build-Env and Metrics Validation (Ongoing)

**Goal:** Adopt useful build-env and objdiff scoring improvements without masking fixable decomp work.

### 4A. Classify changes by risk before adoption (Required gate)

Use this split for every proposal:

- **Build-affecting** (can change link/runtime behavior): `WIBO_COMPUTER_NAME`, `WIBO_PATH_MAP`, anon namespace post-build patcher, data stubs.
- **Metrics-only** (reporting, not build output): `diff_arg` relocation-noise filtering, default omission of unfixable patterns, fuzzy/normalized scoring views.

Rule: never ship a metrics-only change as if it were a build fix.

### 4B. Build-safety verification checklist (for build-affecting changes)

For each build-affecting tweak:
1. Full rebuild (`ninja -k1`) and confirm link remains **0 LNK2001/2019** with only expected `/FORCE:MULTIPLE` duplicates.
2. Run anon patcher dry-run and apply-run; verify only `?A0x<HASH>@@` strings change (symbol-table name bytes), not `.text`.
3. Re-run link and boot smoke test; record pass/fail in session notes.

### 4C. Metrics guardrails (to avoid hiding real work)

Track and publish two metrics in docs/status output:

- **Raw fuzzy match**: current strict objdiff behavior.
- **Normalized fuzzy match**: relocation-only noise suppressed (LINKER_MERGED + symbol/hash/const-pool address drift).

Guardrails:
- Default triage may use normalized score for prioritization.
- AT_LIMIT decisions require raw diff evidence + pattern verification (especially LINKER_MERGED membership checks).
- Progress docs must include both values side-by-side so any masking is visible.

### 4D. LINKER_MERGED automation policy

Bulk AT_LIMIT is allowed only with verification:
1. recon/analyze shows `LINKER_MERGED [BLOCKED]`
2. merged address resolves to expected symbol set
3. no unexplained control-flow mismatch remains

Anything failing these checks stays manual-review, even if high match%.

---

## Deferred (Low Priority)

These are cosmetic improvements that don't advance the broader goal:

- **1B. Jeff EH metadata colocation** — 17 `__unwind$` stubs, ALTERNATENAME works fine
- **1C. Jeff SafeName ICF dedup** — 54 LNK4006 warnings, harmless
- **1D. Jeff cross-unit labels** — data stubs handle this
- **2B. Template duplicate warnings** — inherent to hybrid link, accept or `/IGNORE:4006`

---

## Execution Order

```
Phase 1  →  Phase 2  →  Phase 3  →  Phase 4
(build)     (promote)   (decomp)    (runtime)
 ~1hr        ~1 day      ongoing     ongoing
```

Phase 1 unblocks Phase 2. Phase 2 reduces Phase 3 scope. Phase 4 runs in parallel once XEX builds.
