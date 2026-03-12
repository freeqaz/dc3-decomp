# IL Permuter

## Goal

Use MSVC's front-end IL as a search boundary so the permuter spends less time
recompiling source variants that are compiler-equivalent.

The immediate target is **IL-aware dedup and bucketing**, not direct IL
mutation and not full `c2.dll` replay.

## Why This Exists

Our current source permuter is producing real wins, but it is expensive because
every candidate pays for:

1. front-end parse + typecheck
2. IL generation
3. `c2.dll` optimization + codegen
4. objdiff

The MSVC research already shows that the front-end writes a real typed IL bundle
(`_CL_*` `.ex/.gl/.sy/.in/.db`) and `c2.dll` reads it later. That means IL is a
natural boundary for:

- deduplicating source variants that compile to the same compiler-relevant shape
- ranking variants by how much they change compiler state
- eventually replaying the back-end without rerunning the full front-end

## Current Status

Already implemented elsewhere:

- full `_CL_*` bundle capture and normalized export in
  `msvc-src/tools/il_parser.py`
- persistent named IL fixtures under `msvc-src/analysis/il-fixtures/`
- PPC-to-IL-like lifting in `msvc-src/tools/ppc_il_lifter.py`
- target-fact routing from PPC-derived codegen shapes in the permuter
- traced `c2.dll` pipeline docs showing `InvokeCompilerPass -> IL LOAD -> optimize -> codegen`
- first-cut batch integration in `scripts/permuter/scorer.py`:
  removed after live validation showed canonical IL collisions at the object
  level for real call-shape variants
- safe follow-on integration in `scripts/permuter/beam_search.py`:
  analyze canonical IL for top scored variants, report bucket overlap, and use
  unique analyzed IL buckets as a light survivor tiebreak

What is missing:

- a canonical IL representation suitable for dedup and bucketing
- a stable hash contract for "compiler-relevant IL identity"
- a prototype proving that the fixture corpus can be grouped meaningfully by IL
- a concrete shipping plan for wiring IL buckets into the source permuter

## Scope

In scope for this document:

- canonical IL normalization and hashing
- IL-aware dedup/bucketing in the source permuter
- search heuristics that use IL distance/change magnitude
- backend replay as a later RE track

Not in scope for the first shipped version:

- arbitrary IL rewriting
- full direct invocation of `c2.dll`
- replacing source-level patterns with IL-level synthesis

## Working Hypotheses

### H1: IL is stable enough to use as a dedup key

Status: `validated for Phase 0`

Meaning:

- two source variants that matter to codegen should usually produce different
  canonical IL
- variants that differ only in source noise should often collapse to the same
  canonical IL

Current evidence:

- prototype canonicalizer implemented in `msvc-src/tools/il_permuter.py`
- validated on the current named fixture corpus:
  - 6 bundles
  - 37 functions
  - 37 unique per-function canonical hashes
- known divergent pairs remain distinct:
  - cast-vs-AND type control
  - tail-call vs plain-call call shape
  - switch lowering families
- fresh-capture self-compare on `il_type_control_cast_vs_and.cpp` produced
  identical canonical bundle hashes and zero canonical diffs
- but live batch validation found that identical canonical IL is **not**
  sufficient to skip compilation:
  - `UIList::SetProvider`
  - `SongSequence::OnSongLoaded`
  produced identical canonical IL hashes across two variants while still
  producing different `.obj` hashes

### H2: Canonical IL hashing should ship before backend replay

Status: `yes`

Reason:

- we already capture and parse full IL bundles today
- we do not yet know the exact `InvokeCompilerPass` contract
- an IL hash can cut compile volume immediately without waiting on more RE

### H3: Direct `c2.dll` replay is plausible but still an RE project

Status: `yes`

Evidence already in hand:

- `cl.exe` is a driver layer
- `/B2` can redirect backend loading to a wrapper DLL
- traced pipeline docs identify the IL load stage inside `c2.dll`
- export names are known (`InvokeCompilerPass`, `InvokeCompilerPassW`,
  `DllGetObjHandler`, `AbortCompilerPass`)

What remains unknown:

- exact parameter contract and object lifetimes around `InvokeCompilerPass`
- minimal environment needed to feed a captured IL bundle back into `c2.dll`
- whether a practical one-function replay path exists without extra process glue

## Architecture Options

### Option A: IL-Aware Dedup In The Existing Source Permuter

This is the first shipping path.

Flow:

1. generate source variants
2. compile only enough to capture normalized IL
3. hash each candidate's IL per function
4. keep one representative per IL bucket
5. send only bucket winners to full score/objdiff

Benefits:

- immediate reduction in expensive compiles and objdiff calls
- fits the existing source-pattern system
- no dependency on reverse engineering backend ingress

Costs:

- still pays some front-end work
- requires a policy for when IL capture is worth the extra step

### Option B: IL-Bucketed Search And Ranking

This is the second shipping path.

Use canonical IL deltas as a search signal:

- prefer candidates that actually move IL
- rank buckets by opcode/type/control-flow change magnitude
- learn which IL deltas correlate with score improvement for each pattern family

Benefits:

- fewer dead-end compiles
- more intelligent beam/hill prioritization

### Option C: Backend Replay / Backend Executor

This is a parallel research track, not the first ship.

Desired flow:

1. capture baseline IL once
2. feed IL directly to a long-lived backend path
3. emit `.obj` or assembly for objdiff
4. search over backend-relevant states without rerunning the front-end

This is likely the highest long-term leverage, but it is currently blocked on
RE work rather than implementation plumbing.

## Canonical IL Contract

The first canonical form should preserve **compiler-relevant structure** and
discard capture noise.

Preserve:

- function boundary
- opcode sequence
- operand kind
- operand type
- literal values
- result/return types
- switch/case values
- structural flags when present

Normalize away:

- token ids
- label ids
- symbol-table token numbering
- manifest/debug metadata
- source offsets
- raw bundle base names

Tentative rule:

- local tokens and labels are alpha-renamed in first-use order
- function names are excluded from per-function structure hash
- bundle hash includes function names so whole-TU identity is still stable

## Prototype

Prototype deliverable:

- `msvc-src/tools/il_permuter.py`

Prototype responsibilities:

- load `bundle.json`, bundle dirs, manifest paths, or raw `_CL_*` bases
- canonicalize functions into a stable structure form
- compute per-function and per-bundle hashes
- diff two bundles by canonical hash
- bucket a corpus to reveal collisions/equivalence classes

## Prototype Validation

Questions to answer with the prototype:

1. Do the existing fixtures hash stably from normalized bundle export?
2. Do known source distinctions survive canonicalization?
3. Are there accidental collisions across unrelated fixture functions?
4. Does the canonical form ignore token renumbering and label renumbering?

Expected fixture checks:

- `cast_shift` vs `and_shift` must differ
- tail-call and non-tail-call call-shape fixtures must differ
- switch chain and switch table style fixtures must differ
- synthetic alpha-renamed token variants must hash the same

Results:

- yes: alpha-renamed synthetic token variants collapse to the same canonical hash
- yes: cast-vs-AND functions stay distinct
- yes: tail-call and plain-call functions stay distinct
- yes: the current 37-function fixture corpus showed no accidental collisions
- yes: fresh capture through `il_parser.capture_il()` round-trips through the
  canonical comparator cleanly for self-compare
- no: live scorer validation does **not** support compile-skipping dedup yet

Interpretation:

- the current canonical form is good enough to use as a **screening key**
- it is not strong enough to use as a build-skipping equivalence key
- the next step should be ranking/analysis integration, not compile-skipping
  dedup

## Open Questions

### Q1: Should the first ship dedup on per-function hash or whole-bundle hash?

Current answer:

- per-function hash is the correct primary unit
- whole-bundle hash is still useful for capture integrity and TU identity

Reason:

- our search operates on one target function at a time
- helper changes elsewhere in the TU should not block dedup for the target

### Q2: Can we trust canonical IL as a compiler-equivalence key?

Current answer:

- trust it as a **screening key**, not as a final semantic guarantee

Meaning:

- use IL hash to skip redundant candidates before full scoring
- do not treat matching IL hash as proof that final `.obj` is identical until
  we have empirical confirmation on larger corpora

Current confidence:

- not high enough to ship for compile-skipping dedup
- not high enough to replace objdiff or semantic validation

Validated failure mode:

- same canonical IL can still lead to different backend/regalloc choices and
  different `.obj` output

### Q3: Should we mutate IL directly in phase 1?

Current answer:

- no

Reason:

- format safety and backend contract are still under-specified
- source-to-IL bucketing is already enough to reduce search cost

### Q4: Is backend replay blocked on missing research, or only implementation?

Current answer:

- still blocked on research

Concrete blockers:

- identify the exact IL ingress contract at `InvokeCompilerPass`
- determine whether `_CL_*` files are read directly or repackaged first
- determine minimum state required around the IL LOAD function traced in
  `msvc-src/docs/PIPELINE.md`

## Phases

### Phase 0: Canonical IL Hash Prototype

Objective:

- prove that we can turn captured IL into a stable dedup key

Deliverables:

- canonicalizer + hash tool
- per-function and per-bundle hash commands
- corpus bucketing command
- unit tests for alpha-renaming stability and fixture differentiation

Exit criteria:

- prototype runs on all current IL fixtures
- known divergent fixtures stay distinct
- synthetic token-renumbered inputs collapse correctly

Status:

- complete

### Phase 1: Shipping IL-Aware Dedup

Objective:

- use IL to reduce wasted search effort without assuming object equivalence

Deliverables:

- capture-or-load IL for candidate variants
- session cache keyed by `(symbol, canonical_il_hash)`
- skip scoring duplicate IL buckets
- reporting in scan/beam output for IL dedup hit rate

Exit criteria:

- measurable drop in full-score calls on a hard-target slice
- no regression in final best-of-run match rate

Recommended first cut:

1. add an optional IL capture stage only for shortlisted candidates
2. compute per-target-function canonical IL hash
3. use canonical IL for ranking, grouping, and reporting
4. do **not** skip compilation solely on canonical IL equality
5. measure whether IL grouping helps proposal ordering

Status:

- started

Validation outcome:

- a compile-skipping dedup spike was attempted and then removed
- live counterexample:
  - same canonical IL
  - different `.obj` hash
  - real functions: `UIList::SetProvider`, `SongSequence::OnSongLoaded`

Current direction:

- keep canonical IL in the toolchain
- use it for analysis/ranking first
- do not use it as an equivalence oracle yet

Live status:

- beam search now captures canonical IL for the top scored variants in a depth
  and reports bucket counts
- unique analyzed IL buckets get a small ranking tiebreak among otherwise
  similar survivors
- every candidate still goes through full build + objdiff
- IL analysis summary counts are now threaded into batch results and persisted
  in `improvement_runs` for later correlation work
- per-pattern IL pressure metrics (`analyzed_variants`, `unique_buckets`,
  `duplicate_buckets`) are now aggregated in batch summaries and stored in
  `improvement_runs.il_pattern_metrics` for future tuning/reporting

### Phase 2: IL-Bucketed Search

Objective:

- use IL change magnitude to rank source variants before expensive scoring

Deliverables:

- IL delta summary per candidate
- bucket representative selection policy
- beam/hill integration using IL delta features

Exit criteria:

- better proposal ordering on a measured call/switch/type-control slice

### Phase 3: Backend Replay Investigation

Objective:

- determine whether captured IL can drive `c2.dll` directly

Deliverables:

- `InvokeCompilerPass` contract notes
- `/B2` wrapper experiment plan
- minimum replay harness design
- one-function backend replay spike if feasible

Exit criteria:

- either a working replay spike or a concrete blocker document with next RE cuts

### Phase 4: Shipping Path Selection

Objective:

- decide whether the production path is:
  - source permuter + IL dedup
  - source permuter + IL dedup + IL ranking
  - backend replay executor

Deliverables:

- measurement report
- chosen integration path
- implementation backlog for agents

## Agent Work Packages

### WP1: Canonicalization Contract

Files:

- `msvc-src/tools/il_permuter.py`
- `docs/plans/synthesis-engine/IL_PERMUTER.md`

Tasks:

- define canonical function schema
- define which fields are ignored vs preserved
- document collision risk explicitly

Done when:

- hash output is deterministic and documented

### WP2: Fixture Corpus Validation

Files:

- `msvc-src/tools/il_permuter.py`
- `msvc-src/tools/test_il_permuter.py`

Tasks:

- run across all named fixtures
- record distinct-vs-collapsed cases
- identify any accidental collisions

Done when:

- fixture corpus results are written back into this doc

Status:

- complete for the current 6-bundle fixture corpus

### WP3: Permuter Integration Spike

Files:

- `scripts/permuter/scorer.py`
- `scripts/permuter/score_cache.py`
- `scripts/permuter/types.py`

Tasks:

- define a candidate IL dedup cache path
- choose when IL capture is invoked
- produce reporting for IL bucket hits

Done when:

- one measured slice shows reduced expensive score attempts

Status:

- in progress

Current implementation:

- canonical IL hashing prototype is complete
- compile-skipping dedup was tested and rejected by live validation
- ranking/reporting integration has started in beam search
- next step is to extend the same reporting to hill-climb/batch summaries

### WP4: Backend Replay RE

Files:

- `msvc-src/docs/ARCHITECTURE.md`
- `msvc-src/docs/PIPELINE.md`
- `msvc-src/tools/c2_decompile.py`

Tasks:

- trace `InvokeCompilerPass`
- understand the IL load handoff
- test `/B2` wrapper options

Done when:

- replay feasibility is either proven or cleanly blocked

## Initial Recommendation

Ship Phase 0 and Phase 1 before betting on backend replay.

That is the shortest path to value:

- it leverages research we already completed
- it should reduce redundant scoring even if backend replay takes longer
- it keeps the long-term `c2.dll` route open instead of blocking on it

Immediate next implementation step:

- integrate canonical IL hash dedup into the candidate scoring path behind a
  narrow first-cut policy:
  - only for shortlisted candidates
  - only for the target function
  - session-local cache first, persistent cache later

Current next step after implementation:

- wire canonical IL into:
  - hill-climb reporting
  - batch summary reporting
  - offline corpus analysis
- pursue backend replay separately if we want a stronger equivalence boundary
