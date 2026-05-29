# Synthesis Engine Roadmap

This document defines the execution roadmap for the synthesis engine work.

The key update from earlier planning is simple:

- beam search is no longer hypothetical
- the synthesis engine should build on the beam search already in tree
- the next work is about better evidence, better proposal routing, and better validation

Relevant existing pieces:

Search & permuter:
- [`scripts/permuter/beam_search.py`](../../scripts/permuter/beam_search.py) — multi-state search loop (default strategy)
- [`scripts/permuter/hill_climber.py`](../../scripts/permuter/hill_climber.py) — greedy baseline + CLI wiring for `--beam`
- [`scripts/permuter/scan_and_permute.py`](../../scripts/permuter/scan_and_permute.py) — entry point, beam as default strategy
- [`scripts/permuter/types.py`](../../scripts/permuter/types.py) — `BeamState`, `BeamConfig`, `FunctionContext`, `Diagnosis`
- [`scripts/permuter/composer.py`](../../scripts/permuter/composer.py) — 2-stage + N-stage chains, adaptive follow-ups
- [`scripts/permuter/scorer.py`](../../scripts/permuter/scorer.py) — build pipeline, 3-layer dedup, parallel scoring

Attribution (Phase 1 implementation):
- [`scripts/permuter/attribution.py`](../../scripts/permuter/attribution.py) — `/FAs` listing parser, mismatch join, region aggregation

Compiler Atlas (Phase 3 implementation):
- [`scripts/permuter/compiler_atlas.py`](../../scripts/permuter/compiler_atlas.py) — 30 opcode→source-feature entries, lookup/boost API

Target Facts (Phase 4 implementation):
- [`scripts/permuter/target_facts.py`](../../scripts/permuter/target_facts.py) — normalized evidence layer with 4 extractors (diagnosis, atlas, guidance, shape facts)

Validator Ladder (Phase 5 implementation):
- [`scripts/permuter/validator.py`](../../scripts/permuter/validator.py) — 6-level validation chain (parse → build → score → region → fact → semantic)

Compiler trace & differential testing:
- [`tools/compiler_trace/invoker.py`](../../tools/compiler_trace/invoker.py) — wraps cl.exe with project flags, `/FAs` listing generation
- [`tools/compiler_trace/asm_diff.py`](../../tools/compiler_trace/asm_diff.py) — compile two variants, normalize, diff
- [`tools/compiler_trace/asm_regmap.py`](../../tools/compiler_trace/asm_regmap.py) — variable→register assignments from `/FAs`
- [`msvc-src/tools/diff_test.py`](../../msvc-src/tools/diff_test.py) — differential testing harness (regalloc, inline, peephole suites)
- [`msvc-src/results/FINDINGS_SUMMARY.md`](../../msvc-src/results/FINDINGS_SUMMARY.md) — proven codegen decision maps

Analysis & mining:
- [`scripts/analysis/mine_patterns.py`](../../scripts/analysis/mine_patterns.py) — commit-history pattern extraction
- [`scripts/analysis/diff_inspect.py`](../../scripts/analysis/diff_inspect.py) — deep mismatch analysis (diagnose, clusters, regswaps, attributed regions)
- [`scripts/analysis/reclassify_at_limit.py`](../../scripts/analysis/reclassify_at_limit.py) — bulk AT_LIMIT reclassification
- [`scripts/permuter/batch_triage.py`](../../scripts/permuter/batch_triage.py) — 6-category mismatch classification
- [`scripts/permuter/ghidra_preflight.py`](../../scripts/permuter/ghidra_preflight.py) — unfixable detection

MSVC RE:
- [`msvc-src/docs/PASSES.md`](../../msvc-src/docs/PASSES.md) — 35 named optimization passes
- [`msvc-src/docs/PIPELINE.md`](../../msvc-src/docs/PIPELINE.md) — compiler pipeline architecture
- [`msvc-src/docs/PASS_GROUPS.md`](../../msvc-src/docs/PASS_GROUPS.md) — binary patching results
- [`msvc-src/docs/IL_FORMAT.md`](../../msvc-src/docs/IL_FORMAT.md) — intermediate language format
- [`msvc-src/docs/PPC_IL_LIFTER.md`](../../msvc-src/docs/PPC_IL_LIFTER.md) — constrained PPC→IL lift for comparison and future hints

Data:
- `decomp.db` — function registry (symbol, unit, match%, verdict)
- `permuter_cache.db` — per-function scoring history
- `build/373307D9/report.json` — current objdiff report
- `build/373307D9/baselines/` — 25 commit-stamped baseline snapshots

Companion design docs:
- [INSTRUCTION_ATTRIBUTION.md](INSTRUCTION_ATTRIBUTION.md) — `/FAs` join design
- [COMPILER_ATLAS.md](COMPILER_ATLAS.md) — instruction-pattern → source-feature mappings
- [PATTERN_MINING.md](PATTERN_MINING.md) — cross-function transfer learning
- [TARGET_FACTS.md](TARGET_FACTS.md) — normalized evidence layer
- [DIFFERENTIAL_TESTING.md](DIFFERENTIAL_TESTING.md) — black-box codegen probing
- [IL_TYPE_CONTROL.md](IL_TYPE_CONTROL.md) — **NEW**: source types control IL opcodes, which control instruction selection (proven via ByteGrinder, 20+ functions)
- [MSVC_ROADMAP.md](MSVC_ROADMAP.md) — c2.dll RE plan
- [HEADER_LEVEL_CLUSTERING.md](HEADER_LEVEL_CLUSTERING.md) — **NEW**: header-level match% clustering methodology + historical catalog of 21 header fixes (1,500+ total function improvements), 6-category fix taxonomy, remaining opportunities
- [DEEP_ANALYSIS_PLAN.md](DEEP_ANALYSIS_PLAN.md) — detailed c2.dll analysis tracks

This roadmap is intentionally narrower than the earlier vision docs. It focuses
on the work that still needs to happen now that search infrastructure exists.

## Goal

Turn the current permuter + beam search stack into a target-guided
search-and-validation system that:

- spends build budget on the right regions of a function
- uses compiler behavior knowledge as structured evidence
- preserves and ranks promising partial progress
- remains inspectable enough to debug failed searches

This is not a plan to build a separate standalone engine first. The synthesis
engine should emerge inside the existing permuter stack.

## Current State

What already exists:

- **beam search** — default strategy everywhere (`--strategy beam` in batch,
  `--beam` in hill_climber). Multi-state, diagnosis-guided, diversity-preserving.
  BeamState carries source, score, diagnosis, tags, provenance chain, failure
  history, guidance agreement. Config: width=8, depth=4, expand=24, escape=4,
  diversity=3.
- **greedy and evolutionary** available via `--no-beam` / `--evolutionary`
- **diagnosis** from objdiff instruction data (clusters, regswaps, offsets,
  prologue deltas, noise ratio)
- **Ghidra, m2c, RB3, and ASM-guided** proposal inputs (all default-on)
- **standalone ASM-guided declaration reorder** — compiles with `/FAs`, parses
  var→register mapping, generates targeted swaps without BSF tracing. Runs
  automatically for any function with GPR swap pairs. Falls through to BSF-guided
  (also default-on) for deeper analysis.
- **79 patterns** with composition (2-stage + N-stage chains), adaptive
  follow-ups, tag-based selection. Includes `foreach_to_dowhile` (FOREACH
  macro → do-while with pre-guard, with optional scope narrowing variants).
- **`/FAs` listing parser** — `attribution.py` parses MSVC assembly listings,
  joins with objdiff instruction diffs, produces `AttributedMismatch` and
  `MismatchRegion` records. 19 unit tests. `FunctionContext.mismatch_regions`
  field added.
- **differential testing harness** — `msvc-src/tools/diff_test.py` with proven
  results for: register allocation order (linear, first-decl=highest),
  inlining threshold (~40 weighted cost units, branch=8x arithmetic),
  boolean materialization (comprehensive 6-category decision tree),
  NOR peephole, subf. fusion, branch polarity, float precision.
  See `msvc-src/results/FINDINGS_SUMMARY.md`.
- **MSVC pipeline mapped** — 35 named passes, 5 groups, G5P10 identified as the
  PPC code generator (not a separate peephole optimizer), G3P2 does record-form
  fusion, inliner cost model found. Binary patching confirmed.
- **cross-unit header variants** — header pattern bridge, multi-symbol scoring,
  blast-radius analysis, 8 header-backed patterns
- **header-level clustering** — match% clustering across TUs to identify shared
  header template bugs. Historical catalog of 21 header fixes totaling 1,500+
  function improvements. 6-category fix taxonomy (struct layout, vtable order,
  inlining control, template semantics, __forceinline, type qualifiers). Active
  cluster tracking from report.json + decomp.db. See [HEADER_LEVEL_CLUSTERING.md](HEADER_LEVEL_CLUSTERING.md).
- **compiler atlas** — 80 `AtlasEntry` records: proven (diff-test), inferred, and
  negative (AT_LIMIT). Opcode-indexed for O(1) lookup. `boost_patterns()` API
  for beam/generator integration. 20 unit tests.
- **target facts** — normalized evidence layer aggregating diagnosis, attribution,
  atlas, and guidance into queryable `TargetFact` records. 3 extractors, wired
  into BeamState (fact_agreement in ranking_key) and FunctionContext. 19 unit tests.
- **validator ladder** — 6-level validation chain from parse validity through
  semantic checks. `ValidationTier` enum used in `BeamState.ranking_key` for
  survivor selection. Advisory validation in hill_climber. 38 unit tests.

What is still missing:

- ~~attribution wired into scorer baseline~~ DONE (`Scorer.get_attribution()`)
- ~~attribution wired into pattern filtering~~ DONE (6 patterns use region filter)
- ~~a normalized target-facts layer~~ DONE (`TargetFacts` with 3 extractors)
- ~~a machine-readable compiler atlas~~ DONE (30 entries, opcode-indexed lookup)
- cross-function strategy database (mining infra exists, strategy records don't)
- ~~region-level improvement tracking in beam state~~ DONE (`BeamState.region_scores`)
- ~~validator ladder above "it built and scored better"~~ DONE (6-level chain)
- ~~stable IL capture corpus with reusable fixtures~~ DONE (6 fixture bundles, 37 functions)
- ~~parsed IL bundle schema across `.ex/.gl/.sy/.in/.db`~~ DONE (normalized JSON export, 43 fixture tests)
- ~~PPC ASM -> IL-style lifting for a constrained opcode subset~~ DONE (80+ PPC mnemonics, 12 shape categories, CFG, 173 tests)
- ~~IL-guided constraints or facts consumable by the permuter~~ DONE (4th extractor `extract_from_shape_facts()`, 10 shape-fact category handlers, target-aware routing)
- ~~selective compiler RE for COLOR register allocator internals~~ DONE (linear scan, not graph coloring — see `msvc-src/docs/COLOR_RE.md`)
- ~~automated header cluster detection~~ DONE — `scripts/analysis/header_cluster.py`:
  match% clustering, template pattern clustering, opportunity ranking with fixability
  scores. JSON output for downstream tooling. First run found ObjectDir::Find at
  99.7% across 38 TUs (78 functions) — fixed via Dir.h intermediate variable removal,
  +78 functions to 100%.
- TU-boundary inlining control catalog (which functions must NOT have header bodies)
- ~~`static const float` prologue fix pattern~~ DONE — `float_const_static` pattern
  (`scripts/permuter/patterns/float_const_static.py`): detects GPR↔FPR type conflict
  via `has_gpr_fpr_type_conflict` property, generates variants replacing inline float
  literals with `static const float` declarations. 23 unit tests.
- ~~`--validate` flag~~ DONE — default-on in hill_climber. Shows per-variant
  validation tier next to score, tier distribution summary at end of search.
  18 unit tests.

## Design Principle

Search is no longer the bottleneck. Evidence quality is.

Beam search gives us the search controller we needed. The remaining job is to
improve what the search sees, what it proposes, and how it decides that a
variant is truly useful.

That means the roadmap should prioritize:

1. attribution
2. target facts
3. atlas and diff-testing data
4. validator ladder
5. selective compiler RE

Notably, this means "implement beam search" is no longer a roadmap item.

## Phase 1: Attribution First

Status: In progress

Primary doc: `INSTRUCTION_ATTRIBUTION.md`

Objective:
Connect objdiff mismatches back to concrete source lines and source regions.

Why this comes first:

- it plugs directly into the existing permuter
- it makes current patterns more precise immediately
- it benefits both manual decomp work and automated search
- it is lower risk than new compiler RE

Deliverables:

- a robust `/FAs` listing parser that maps instructions to source lines
- join logic from objdiff instructions to source attribution
- `MismatchRegion` records stored on `FunctionContext`
- attributed output mode in `scripts/analysis/diff_inspect.py`
- region-scoped pattern filtering for a small high-ROI pattern set

Implemented so far:

- `/FAs` listing parser (`scripts/permuter/attribution.py`):
  - `parse_asm_listing()` — extracts function from PROC..ENDP, parses source
    comments, instruction lines, prologue helpers, file transitions
  - `AsmListing` / `AsmEntry` / `AsmInstruction` structured dataclasses
  - `source_line_for_index()` / `source_line_for_offset()` lookups
- Mismatch attribution join:
  - `attribute_mismatches()` — joins objdiff instruction diffs with /FAs source
    annotations, produces `AttributedMismatch` records with confidence scores
  - Supports opcode, register, insert/delete mismatch types
  - Interpolates from neighbors when direct attribution fails
- Region aggregation:
  - `aggregate_regions()` — merges attributed mismatches into contiguous source
    regions with configurable gap tolerance
  - `MismatchRegion` with match_ratio, impact, dominant_type properties
  - Sorted by impact for budget allocation
- Integration:
  - `FunctionContext.mismatch_regions` field added
  - `attribute_function()` convenience pipeline
- 19 unit tests covering parser, attribution, aggregation, and full pipeline

TODO:

- ~~Wire listing generation into scorer's baseline computation~~ DONE —
  `Scorer.get_attribution()` method compiles with /FAs, parses listing,
  joins with baseline objdiff data, returns `MismatchRegion` list.
- ~~Add `--attributed` mode to `scripts/analysis/diff_inspect.py`~~ DONE —
  `--symbol "..." --attributed` compiles with /FAs, runs objdiff, displays
  source-attributed mismatch regions with impact, local match ratio, and
  dominant type. Tested on real functions (DirLoader::SaveObjects: 5 regions).
- ~~Add region-aware filtering to high-ROI pattern generators~~ DONE —
  Added `node_in_mismatch_region()` and `line_in_mismatch_region()` helpers
  to `FunctionContext`. Wired into 15 patterns: variable_extraction,
  signed_unsigned, comparison_equivalence, comparison_flip, commutative_swap,
  inline_assignment, ternary_swap, bool_cast, guard_to_nested,
  early_return_merge, statement_reorder, return_call_merge,
  float_literal_pressure, assert_line_fix, declaration_reorder (fallback).
  Gracefully passes through when no regions available.
- ~~Add region-level improvement tracking to beam state~~ DONE —
  `BeamState.region_scores` dict maps `(start_line, end_line)` to local
  match ratio. `region_improvement_count()` compares against parent state.
- Test on 20-30 representative hard-target functions across different units

Success criteria:

- useful attribution on a representative hard-target slice
- fewer irrelevant pattern applications per round
- visible improvement in beam diversity by region, not only raw score

## Phase 2: Differential Testing Harness

Status: Core harness and first suites DONE. Extension suites not started.

Primary doc: [DIFFERENTIAL_TESTING.md](DIFFERENTIAL_TESTING.md)

Results: [`msvc-src/results/FINDINGS_SUMMARY.md`](../../msvc-src/results/FINDINGS_SUMMARY.md)

Objective:
Systematically map source features to codegen outcomes without decompiling
`c2.dll`.

Implemented:

- [`msvc-src/tools/diff_test.py`](../../msvc-src/tools/diff_test.py) — test
  harness using invoker + `/FAs` listing parser
- **regalloc_order suite** — declaration count (2-15 vars), order swap, mixed
  GPR/FPR, virtual calls, loops, conditionals. Finding: strictly
  first-declared=highest (linear scan) for ALL tested patterns up to N=15.
  Graph coloring (BSF) NOT triggered in any test. Compiler temporaries
  (loop counters, vtable lookups) consume callee-saved regs before user vars.
- **inline_threshold suite** — callee with N=1..50 statements. Finding: weighted
  cost model (~40 cost units). Arithmetic=weight 1, branch=weight 8.
  `inline` keyword = no effect with /Ox. `__forceinline` = unlimited.
- **peephole_trigger suite** — NOR (u8 XOR 0xFF → `not`), bool materialization
  (comprehensive 6-category decision tree by signedness+operator+constant),
  subf. fusion (`hi - lo >= 0` → `subf.`). Key finding: signedness is the
  primary differentiator for carry-chain instruction selection.
- **branch_polarity suite** — compiler ALWAYS inverts condition. `== 0` → `bne`,
  `!= 0` → `beq`. Applies to early return and nested if/else.
- **float_precision suite** — DOUBLETOSINGLE aggressively demotes all
  double→float assignments to `lfs`. `lfd` only for true double contexts.
- **pass identification via binary patching** — G5P10 is the PPC code generator
  (not a separate peephole optimizer). G3P2 does record-form fusion. G4P4
  disproved as G5_SPECIAL.

TODO (extension suites):

- **rlwinm fusion** — **PROVEN** (2026-03-10, 20+ ByteGrinder functions):
  Source type controls G5P10 rlwinm fusion via IL opcode choice.
  `u8()` cast → IL CAST(82 12 20) → fused `extrwi`/`clrlslwi`.
  `& 0xFF` → IL AND → separate `srwi`+`clrlwi` (matches target).
  Key finding: `u8()` CAST propagates backward through XOR/OR/ADD, masking
  all operands. `& 0xFF` AND stays local. See [IL_TYPE_CONTROL.md](IL_TYPE_CONTROL.md).
  **Remaining**: IL capture confirmation with `il_parser.py` (not yet done but
  codegen results are conclusive). Add atlas entries for extrwi/clrlslwi detection.
- **u8 mask placement** — **PROVEN** (2026-03-10, same ByteGrinder session):
  `u8 x = val;` → immediate `clrlwi` at assignment (early mask).
  `unsigned long x = val;` → no mask until `& 0xFF` at return (late mask).
  Target consistently defers mask to final computation. `unsigned long` intermediate
  types + `(int)((expr) & 0xFF)` return pattern matches target in all tested cases.
  See [IL_TYPE_CONTROL.md](IL_TYPE_CONTROL.md) for the full fix pattern.
- **FPR allocation interaction**: test GPR+FPR mixed declaration ordering,
  confirm FPR independence from GPR graph
- **Template instantiation**: test how template type parameters affect codegen
  decisions (signed vs unsigned template args)
- **Cross-call live range**: test how function calls between declarations affect
  register assignment vs temporaries
- **Scope nesting**: test how variable declarations inside loops/branches vs
  outer scope affect allocation order. Results feed the deferred
  `scope_narrowing` pattern (see Deferred Work).
- **Static local guard patterns**: test when `??_B` vs `$S` guard naming occurs
- Record negative results (source variations that did NOT change codegen) in
  structured JSON alongside positive results

Success criteria (for extension suites):

- decision maps that directly inform proposal ranking or suppression
- at least one new proven mapping that gets reused by the permuter
- explanation for the "~7 variable" BSF threshold seen in real DC3 functions

## Phase 3: Compiler Atlas Seed

Status: Core atlas DONE. Beam integration DONE. Atlas expansion DONE (72 entries).

Primary doc: [COMPILER_ATLAS.md](COMPILER_ATLAS.md)

Objective:
Create a machine-readable atlas of known instruction-pattern to source-feature
relationships.

This phase should start by harvesting what the repo already knows, not by
waiting for a perfect generator.

Existing raw material for harvest:

- [`msvc-src/results/FINDINGS_SUMMARY.md`](../../msvc-src/results/FINDINGS_SUMMARY.md) —
  30+ **proven** instruction→source mappings from differential testing:
  - 8 boolean materialization categories (addic/subfe, neg/andc/srwi, etc.)
  - NOR peephole trigger conditions
  - subf. fusion conditions
  - branch polarity rules
  - float precision behavior
  - register allocation ordering rules
  - inlining cost model
- [`docs/decomp/TECHNICAL_NOTES.md`](../decomp/TECHNICAL_NOTES.md) — 38
  hand-documented patterns (mix of proven and inferred)
- [`docs/decomp/patterns/*.md`](../decomp/patterns/) — 17 pattern docs with
  real examples and before/after percentages
- [`docs/decomp/patterns/unfixable-compiler.md`](../decomp/patterns/unfixable-compiler.md) —
  16 hard patterns with detection heuristics (negative atlas entries)
- MEMORY.md known patterns section — proven fixes with specific instructions

Deliverables:

- structured atlas entries in a queryable format (JSON or SQLite)
- confidence tagging: `proven` (diff-test confirmed) vs `inferred` (docs only)
  vs `negative` (confirmed no source-level fix)
- a lookup surface: given target opcode sequence, return matching entries
- pattern boost/suppress hooks for beam search

Implemented:

- [`scripts/permuter/compiler_atlas.py`](../../scripts/permuter/compiler_atlas.py):
  - `AtlasEntry` frozen dataclass with `Confidence` enum (PROVEN/INFERRED/NEGATIVE)
  - 30 entries harvested from FINDINGS_SUMMARY, TECHNICAL_NOTES, unfixable-compiler, MEMORY
  - Opcode index for O(1) lookup by target instruction
  - `lookup(target_opcodes)` — returns matching entries ranked by confidence then overlap
  - `boost_patterns(entries)` — extracts pattern boost/suppress sets from entries
  - `lookup_for_diagnosis()` — convenience wrapper mapping Diagnosis fields to lookups
  - Boolean materialization: 7 entries covering all 6 diff-test categories
  - Comparison patterns: unsigned zero, subf. fusion, branch polarity, division
  - Register allocation: declaration order, prologue mismatch
  - Float: literal precision, FMA fusion
  - Peephole: NOR, bit test materialization
  - Inlining threshold
  - Data layout: member bitwidth, empty/size, string signedness
  - Unfixable: 9 negative entries (volatile regswap, BSF locked, address reloc,
    static guard, scheduler fence, BSS zero-elision, vbtable recompute, mixed FMA,
    subic/subfe bool)
- 20 unit tests covering entries, lookup, boost, and diagnosis integration

TODO:

- ~~Define AtlasEntry schema~~ DONE
- ~~Harvest proven entries from FINDINGS_SUMMARY~~ DONE (~18 entries)
- ~~Harvest entries from TECHNICAL_NOTES~~ DONE (~5 entries)
- ~~Harvest negative entries from unfixable-compiler + MEMORY~~ DONE (~9 entries)
- ~~Build compiler_atlas.py with lookup/boost~~ DONE
- ~~Harvest patterns/*.md → additional entries with real examples~~ DONE (39 new
  entries from 8 pattern docs, bringing total from 33 to 72)
- ~~Wire lookup into beam expansion~~ DONE — `_expand_state()` in
  `beam_search.py` now calls `lookup_for_diagnosis()` and populates
  `round_hints.atlas_boost_patterns` / `atlas_suppress_patterns`.
  Priority multiplier: +0.3 for boosted, ×0.3 for suppressed.
- ~~Use atlas boost/suppress to influence pattern priority~~ DONE —
  `adaptive_priority_boost()` in `RoundHints` applies atlas multipliers.

Success criteria:

- ~~atlas entries are queryable~~ DONE
- ~~the 30 proven diff-test mappings are immediately queryable~~ DONE
- ~~negative entries identify known-unfixable instruction patterns~~ DONE
- ~~atlas lookups influence proposal ordering in beam search~~ DONE
- known patterns can be explained in atlas terms instead of only prose docs

## Phase 4: Target Facts MVP

Status: **DONE** — core module + beam integration + ranking hooks.

Primary doc: [TARGET_FACTS.md](TARGET_FACTS.md)

Objective:
Introduce a minimal normalized evidence layer above diagnosis, attribution,
atlas lookups, and guidance sources.

Important constraint:

The first version should be intentionally small. We do not need the full
long-term schema before the first consumers exist.

Phase-1 target facts should cover only:

- region identity
- fact kind
- payload
- confidence
- provenance

Initial fact kinds should stay narrow:

- `control_shape`
- `call_order`
- `type_shape`
- `register_pressure`
- `mismatch_class`
- `no_touch_zone`

Deliverables:

- `TargetFacts` object attached to a function state
- one extractor from diagnosis + attribution
- one extractor from atlas lookups
- one extractor from Ghidra/m2c agreement
- beam ranking hooks that consume facts
- proposal filters that consume facts

Implemented:

- [`scripts/permuter/target_facts.py`](../../scripts/permuter/target_facts.py):
  - `TargetFact` frozen dataclass (kind, region, payload, confidence, provenance)
  - `TargetFacts` container with `by_kind()`, `for_region()`, `high_confidence()`,
    `has_no_touch()`, and `pattern_recommendations()` queries
  - 3 extractors:
    1. `extract_from_diagnosis()` → register_pressure, mismatch_class, noise facts
    2. `extract_from_atlas()` → mismatch_class/no_touch_zone from atlas entries with
       boost/suppress pattern recommendations
    3. `extract_from_guidance()` → call_order, control_shape from Ghidra/RB3
  - `extract_facts()` convenience pipeline combining all sources
- 19 unit tests covering all dataclasses, queries, and extractors

TODO:

- ~~Define TargetFact dataclass~~ DONE
- ~~Define TargetFacts container~~ DONE
- ~~Build 3 extractors~~ DONE (diagnosis, atlas, guidance)
- ~~Wire into BeamState and FunctionContext~~ DONE —
  `FunctionContext.target_facts` and `BeamState.target_facts` fields added.
  `BeamState.fact_agreement` computed per child state via `_compute_fact_agreement()`.
- ~~Add beam ranking hook~~ DONE —
  `BeamState.ranking_key` includes `validation_tier` and `fact_agreement`
  in the lexicographic sort. States that satisfy more facts and pass higher
  validation tiers are preferred over raw-score-only equivalents.
- ~~Add proposal filter~~ DONE —
  `_expand_state()` feeds `target_facts.pattern_recommendations()` into
  `round_hints.atlas_boost_patterns` / `atlas_suppress_patterns`.
  Suppressed patterns get ×0.3 priority multiplier.

Success criteria:

- proposal code consumes normalized facts instead of raw text blobs
- conflicts are visible, not silently flattened
- the same facts can be reused by both ranking and filtering

## Phase 5: Validator Ladder

Status: **DONE** — 6-level chain implemented, wired into beam + hill climber.

Primary module: [`scripts/permuter/validator.py`](../../scripts/permuter/validator.py)

Objective:
Move acceptance beyond "objdiff score improved."

This does not mean full theorem proving. It means layering cheap checks before
spending expensive build budget or trusting a misleading score delta.

Validator ladder:

1. syntax and parse validity
2. build success
3. objdiff improvement
4. region-level improvement
5. fact agreement improvement
6. bounded semantic checks where practical

Implemented:

- [`scripts/permuter/validator.py`](../../scripts/permuter/validator.py):
  - `ValidationTier` enum (INVALID=0 through SEMANTIC_OK=6)
  - `ValidationResult` dataclass with per-level detail + `is_acceptable` /
    `is_high_quality` properties
  - 6 validation levels:
    1. `check_parse_validity()` — tree-sitter reparse, error collection
    2. `check_build_success()` — from ScoreResult
    3. `check_score_improved()` — score >= baseline with tolerance
    4. `check_region_improvement()` — per-region regression detection with
       configurable threshold
    5. `check_fact_agreement()` — pattern suppression, noise regression,
       no-touch zone checks
    6. `check_semantics()` — return-count, MILO_ASSERT-count, call-set
       preservation heuristics
  - `validate_variant()` — full ladder runner, stops at first failure
  - `validate_batch()` — batch validation for scoring pipelines
- `BeamState.validation_tier` field — set during child state creation
- `BeamState.ranking_key` — includes `validation_tier` in lexicographic sort
- Beam search: every child state gets validated, tier stored on state
- Hill climber: winner validated before apply, semantic warnings logged
- 38 unit tests covering all 6 levels + full ladder + tier ordering

TODO:

- ~~Implement validator chain~~ DONE (6 levels)
- ~~Wire into beam selection~~ DONE (`validation_tier` in `ranking_key`)
- ~~Wire into hill_climber~~ DONE (advisory validation on winners)
- ~~Add `--validate` flag~~ DONE — default-on in hill_climber CLI. Shows
  per-variant validation tier inline with score output, tier distribution
  summary at end of search. 18 unit tests.
- Add region-regression rejection mode (optional: reject variants that pass
  overall but regress specific regions)

Success criteria:

- fewer high-scoring but structurally wrong survivors
- better plateau behavior because neutral partial progress is separated from
  noisy regressions

## Phase 6: Selective Compiler RE And IL Modeling

Status: Initial exploration done (pipeline mapped, pass groups identified).
IL tooling exists in prototype form. Targeted decompilation and IL-guided
permuter integration not started.

Primary docs: [MSVC_ROADMAP.md](MSVC_ROADMAP.md), [DEEP_ANALYSIS_PLAN.md](DEEP_ANALYSIS_PLAN.md)

Existing RE results (see [`msvc-src/docs/`](../../msvc-src/docs/)):
- Pipeline fully traced from `InvokeCompilerPass` through `.obj` emission
- 35 named optimization passes cataloged ([`PASSES.md`](../../msvc-src/docs/PASSES.md))
- 5 pass groups with 37 unique pass functions ([`PASS_GROUPS.md`](../../msvc-src/docs/PASS_GROUPS.md))
- COLOR entry at `fcn.10bc6487` with 207 helper functions
- G5P10 identified as the PPC code generator — NOT a peephole optimizer
- G3P2 does record-form fusion (subf.)
- G4P4 disproved as G5_SPECIAL (zero peephole effect)
- Inliner cost model: weighted (~40 units), branch=8x arithmetic, `__forceinline`=unlimited
- IL format partially documented ([`IL_FORMAT.md`](../../msvc-src/docs/IL_FORMAT.md))
- IL type-control mechanism documented for byte operations
  ([`IL_TYPE_CONTROL.md`](IL_TYPE_CONTROL.md))
- Tools: [`extract_strings.py`](../../msvc-src/tools/extract_strings.py), [`capture_il.py`](../../msvc-src/tools/capture_il.py),
  [`il_parser.py`](../../msvc-src/tools/il_parser.py), [`il_diff.py`](../../msvc-src/tools/il_diff.py),
  [`il_annotate.py`](../../msvc-src/tools/il_annotate.py)

Objective:
Reverse-engineer `c2.dll` only where the black-box harness and atlas still
leave important unanswered questions.

This phase should be selective, not the default entry point.

Secondary objective:
Use the captured MSVC IL as a bridge layer between source-side structure and
target-side PPC assembly, so the permuter can reason about compiler-relevant
shapes instead of only raw source text and final asm.

Priority order:

1. IL capture, parsing, and lifting for the subset of operations that matter to
   current AT_LIMIT patterns:
   - casts and promotions
   - compare / branch structure
   - shift / mask / rlwinm-sensitive byte operations
   - bool materialization
   - switch dispatch
   - call/return shape
2. COLOR details that materially affect register-pressure proposals —
   specifically: what triggers BSF graph coloring in real DC3 functions (~7+
   vars)? The diff testing found linear allocation up to N=15 in simple
   patterns, so the trigger must involve overlapping live ranges, not just
   variable count.
3. ~~G5_SPECIAL details~~ **RESOLVED** — G5_SPECIAL is not a separate pass.
   All PPC patterns are instruction selection inside G5P10 (code generator).
4. Inliner cross-call budget effects — the diff testing found the threshold
   (~40 cost units) but not how caller context affects the budget.

Deliverables:

- normalized IL capture bundle format for `_CL_*` files
- fixture corpus of captured IL bundles tied to known source patterns
- parsed IL JSON or Python representation for `.ex/.gl/.sy/.in/.db`
- PPC ASM -> IL-style lifter for a constrained subset of opcodes
- comparison tool: source IL vs lifted PPC for one function
- annotated pseudocode for COLOR's callee-saved assignment loop
- BSF trigger condition (what makes allocation non-linear)
- spill cost formula
- validated inliner cross-function effects

TODO:

- Stabilize IL capture:
  - ~~add manifest-writing and bundle inspection support~~ DONE —
    `il_parser.py capture --bundle-name` now writes `manifest.json` and
    `list-bundle` inspects bundle contents/functions
  - make `capture_il.py` and/or `il_parser.py capture` preserve full `_CL_*`
    bundles reproducibly
  - store fixtures under `msvc-src/analysis/il-fixtures/` with metadata:
    source file, compiler flags, symbol list, capture date
  - verify capture works on small representative sources for compare/cast,
    bool materialization, rlwinm-sensitive byte ops, switch, and calls
- Parse the full IL bundle, not only `.ex`:
  - document and parse `.gl`, `.sy`, `.in`, and `.db`
  - map symbol/type tokens across files into one structured `ILBundle`
  - emit normalized JSON for diffing and downstream tooling
- Build an IL fixture corpus:
  - prioritize `IL_TYPE_CONTROL.md` cases first:
    `u8(expr)` / CAST vs `expr & 0xFF` / AND, plus backward propagation through
    XOR / OR / ADD
  - one fixture per proven compiler behavior from FINDINGS_SUMMARY
  - one pairwise fixture for "same asm class, different IL" cases
  - one pairwise fixture for "same source semantics, different IL and asm" cases
- Build a constrained PPC->IL lifter:
  - start with arithmetic, compare, branch, casts, shifts, masks, loads/stores,
    call/return, switch
  - do not attempt full PPC coverage initially
  - preserve typed operations where inferable (`u8`, `u16`, `u32`, signedness)
  - emit a lossy but comparable lifted form, not a full verifier IR
- Add IL comparison tooling:
  - compare source IL bundles against lifted PPC for one function
  - identify missing casts, widened temporaries, compare polarity, switch shape,
    and call ordering differences
- Turn IL findings into permuter inputs:
  - define `il_shape` / `cast_placement` / `branch_shape` / `switch_shape`
    facts or hints
  - boost patterns that move the candidate toward the desired lifted IL shape
  - add a lightweight CLI/debug surface to inspect IL deltas on one function
- Create Ghidra project for c2.dll (x86 PE, no PDB) — name key functions
  from call graph around COLOR entry (fcn.10bc6487). See DEEP_ANALYSIS_PLAN.md
  Track 1 for the priority function list.
- Decompile top COLOR helpers by size: fcn.10bc9550 (1752b), fcn.10bc9fda
  (725b), fcn.10bc69f1 (691b) — extract BSF trigger, spill cost formula
- Build runtime instrumentation prototype: hook COLOR entry via wibo
  LD_PRELOAD, dump the 1428-byte register state buffer before/after
- Cross-validate against diff-test regalloc findings
- Output actionable Python modules usable by permuter (register predictor)

Success criteria:

- reproducible IL capture on a representative source corpus
- parsed IL bundles are diffable and testable
- the PPC->IL lifter explains at least one existing pattern family better than
  raw asm alone
- at least one IL-derived fact or hint is consumable by the permuter
- BSF trigger condition explained (why ~7 vars in DC3 but not in diff tests)
- a register assignment predictor usable in beam proposal ranking
- RE outputs a practical rule, not only documentation

## Deferred Work

These are real ideas, but they should not block the near-term roadmap:

- full historical mining across all solved functions (see [PATTERN_MINING.md](PATTERN_MINING.md))
- large target-facts ontology expansion
- broad runtime instrumentation and DLL hooking (Track 6 in DEEP_ANALYSIS_PLAN.md)
- whole-function equality-saturation exploration
- heavyweight cross-unit search by default
- IL format deep exploration (partially started, see [`IL_FORMAT.md`](../../msvc-src/docs/IL_FORMAT.md))
- **scope_narrowing pattern** — a dedicated pattern that moves variable
  declarations into narrower scopes (e.g., from function scope into an
  `if`-guard or loop body). Changes when the compiler "sees" a variable,
  affecting register allocation timing and callee-saved assignment order.
  Proven effective in UIListDir::DrawWidgets (71.8→100%) where moving
  `bool isFocused` from before the loop into the pre-guard block fixed
  all 15 register swap instructions and the loop entry structure. The
  `foreach_to_dowhile` pattern already includes basic scope narrowing
  variants (moving 1-2 preceding statements into the guard). A standalone
  `scope_narrowing` pattern would generalize this to any scope boundary:
  if/else branches, loop bodies, switch cases, compound blocks. Requires
  dataflow analysis to verify no uses exist outside the target scope.
  Blocked on Phase 2's "scope nesting" differential test suite, which
  would provide the codegen decision map for how scope depth affects
  register assignment.

- **redundant_guard_elimination pattern** — when an `else if (A || B)`
  guard wraps inner conditions that are collectively exhaustive
  (e.g., `(A && !B)`, `(!A && B)`, `(A && B)`), the outer OR check
  generates redundant comparison + branch instructions. Replacing
  `else if (A || B) { ... }` with bare `else { ... }` eliminates
  the guard while preserving semantics (the inner conditions already
  handle all cases). Proven on HamListRibbon::StartFrame and
  HamListRibbon::EndFrame (both 93%→100%). The pattern should detect
  `else if (X || Y)` blocks where the inner branches exhaustively
  test X and Y, and propose replacing with `else`. Detection heuristic:
  look for insert clusters of 4-6 instructions (2× cmpwi+branch pairs)
  that re-test variables already checked in the inner conditions.
  Could also apply to `if (A || B)` at function scope when inner
  conditions are exhaustive.

- **accessor_outline pattern** — when an objdiff mismatch shows the
  target calling a small accessor via `bl` while our compiler inlines
  it (e.g., `lfs f0, 0x30, r30` vs `bl DisabledAlphaScale`), the
  accessor's inline body in the header is causing unwanted inlining.
  Moving the body from the header to the .cpp file makes our compiler
  emit a function call, matching the target. Proven on
  UIListSlot::Draw (96.6%→100%) by moving `DisabledAlphaScale()` and
  `ParentList()` from UIListWidget.h to UIListWidget.cpp. Detection:
  look for `replace` clusters where one side has `mr rN, rM` + `bl`
  (function call) and the other has a direct `lwz`/`lfs` load at the
  same member offset. The pattern should cross-reference the called
  symbol (may be ICF-merged) with known inline header accessors.
  Impact: potentially affects many AT_LIMIT functions where target
  doesn't inline accessors that our headers expose.

- **handler_inline pattern** — when a `HANDLE_ACTION` handler macro calls
  a wrapper function (e.g., `AppendNavItem()`), but the target binary
  inlines the wrapper body directly at the call site in the handler,
  the mismatch manifests as a delete cluster (5-10 instructions for
  the inlined ctor + push_back + dtor) plus a frame size difference
  (the inlined code needs stack space for temporaries). The fix is to
  replace the wrapper call in the handler with the wrapper's body:
  e.g., `HANDLE_ACTION(append_nav_item, mNavItems.push_back(NavItem()))`
  instead of `HANDLE_ACTION(append_nav_item, AppendNavItem())`.
  Proven on HamNavProvider::Handle (98.4%→100%) — the target inlined
  NavItem construction + push_back + destruction (5 extra instructions),
  requiring a 0x130 frame vs our 0x100, plus fixing an 11-group offset
  swap. Detection: look for delete clusters containing a constructor
  call (`??0`), a container method (`push_back`, `insert`), and a
  destructor call (`??1`) that correspond to a single `bl` wrapper call
  in the base. The wrapper function exists in the source but the target
  compiler decided to inline it at this specific call site. Related to
  the accessor_outline pattern but operates in the opposite direction
  (inlining INTO the caller rather than outlining FROM the header).
  Also applies to Message temporary construction — using
  `Message(_msg)` as a temporary expression vs `Message msg(_msg)` as
  a named variable changes destructor timing (end of full expression
  vs end of block scope), shifting Release calls relative to
  kDataUnhandled checks. Proven on Automator::Handle (98.8%→100%).

These can be revisited after attribution wiring + atlas seed + target-facts MVP
are proving value on real beam runs.

## Agent Execution Rules

Agents should treat this roadmap as an implementation queue, not as a research
wishlist.

For each work package below:

- make the code change
- add or update tests
- run the narrowest relevant verification
- update this roadmap status if the package meaningfully advances
- do not start selective compiler RE until the measurement package is done

When a package says "done", it means all of the following are true:

- the target files exist or are updated
- tests covering the new behavior exist
- the feature is wired into the real permuter path, not left as a standalone helper
- the outcome is measurable from logs, JSON output, or CLI behavior

## Agent Work Packages

These are the current actionable tasks. They are ordered by priority.

### WP1: Measure Beam Search On A Fixed Hard Slice

Priority: P0

Status: **DONE** — Beam search wins 3-0 vs greedy on 30-function AT_LIMIT slice.

Objective:
Prove that the implemented synthesis stack improves real search outcomes on a
stable target set.

Required file targets:

- `scripts/permuter/beam_search.py`
- `scripts/permuter/scan_and_permute.py`
- `scripts/permuter/hill_climber.py`
- `scripts/analysis/compare_progress.py` or a new analysis helper if needed
- `docs/plans/synthesis-engine/ROADMAP.md`

Implementation tasks:

- ~~define a fixed slice of 30 hard functions~~ — DONE: `scripts/permuter/tests/benchmark_targets.json`
  - 10 high (97-99.5%), 10 mid (90-97%), 10 low (80-90%), unit-diverse, size ≥ 100
- ~~add a reproducible runner~~ — DONE: `scripts/permuter/tests/benchmark_beam.py`
  - CLI with `--strategy`, `--bracket`, `--limit`, `--output`, beam/greedy settings
- ~~capture per-function metrics~~ — DONE: JSON output with delta, elapsed, rounds, stopped_reason, validation_tier
- ~~emit machine-readable output~~ — DONE: `scripts/permuter/tests/benchmark_beam_results.json`
- ~~summarize the result~~ — DONE: see results below

Results (beam_width=4, beam_depth=2, beam_expand=8, max_rounds=2, no Ghidra):

| Metric | Beam | Greedy |
|--------|------|--------|
| Improved | **3** | 0 |
| Mean delta | **+0.103%** | +0.000% |
| Sum delta | **+2.995%** | +0.000% |
| Max delta | **+1.617%** | +0.000% |
| Mean time | 22.8s | 18.5s |
| Total time | 662.5s | 554.6s |

Head-to-head (29 common targets): beam wins 3, greedy wins 0, ties 26.

Improved functions (all from low bracket):
- `Archive::Enumerate`: 85.56% → 86.52% (+0.97%)
- `HamUI::UpdateUIOverlay`: 88.34% → 89.95% (+1.62%)
- `DxRnd::SetDefaultRenderStates`: 86.41% → 86.82% (+0.41%)

Beam's multi-state search found improvements that greedy's single-state couldn't.
The improvement was concentrated in the "low" bracket (80-90%) — harder functions
where pattern chains have more room to explore.

Deliverables:

- ~~fixed target list checked into the repo~~ — PASS
- ~~one reproducible benchmark command~~ — PASS: `python -m decomp_synth.tests.benchmark_beam`
- ~~one machine-readable results artifact~~ — PASS
- ~~one short findings summary with wins, losses, and neutral outcomes~~ — PASS (above)

Exit criteria:

- ~~beam and greedy can be compared on the same fixed slice~~ — PASS
- ~~attribution coverage and proposal counts are visible in the output~~ — PASS
- ~~the repo contains committed benchmark results, not only code~~ — PASS

### WP2: Extend Diff Testing With The Missing Suites

Priority: P0

Status: **DONE** — 5 new suites added (13 total), all producing structured JSON output.

Objective:
Close the biggest remaining knowledge gaps in the black-box compiler model.

Required file targets:

- `msvc-src/tools/diff_test.py`
- `msvc-src/results/FINDINGS_SUMMARY.md`
- `scripts/permuter/tests/` or `tools/compiler_trace/tests/` for any helpers

Implementation tasks:

- ~~add the FPR allocation interaction suite~~ — DONE: `suite_fpr_allocation()`
- ~~add the template-instantiation signedness suite~~ — DONE: `suite_template_signedness()`
- ~~add the cross-call live-range suite~~ — DONE: `suite_cross_call_live_range()`
- ~~add the scope-nesting suite~~ — DONE: `suite_scope_nesting()`
- ~~add the static-local-guard suite~~ — DONE: `suite_static_local_guard()`
- ~~record negative results in structured output, not only human prose~~ — DONE: each suite outputs structured JSON with boolean flags and markers

Deliverables:

- ~~runnable new suites in `diff_test.py`~~ — 5 new suites, all invocable from CLI
- ~~updated findings summary with proven and negative results~~ — Sections 9-13 in FINDINGS_SUMMARY.md
- ~~tests for any parser/output/schema additions~~ — suites self-test via compile+parse

Key findings:
- FPR allocation: f31-first descending, independent of GPR, threshold at N=5 floats
- Template signedness: result type matters more than parameter type; sub-word promotion masks apply
- Cross-call live range: dead-after-call = no callee-saved; declaration order confirmed across calls
- Scope nesting: ZERO codegen effect (all depths produce identical hash) — **negative result**
- Static local guard: separate guard per static, `static const` with literal elides guard

Exit criteria:

- ~~each new suite can be invoked directly from the CLI~~ — PASS
- ~~at least one suite produces a new reusable decision map~~ — PASS (cross-call live range, FPR scaling)
- ~~negative results are represented in structured output, not lost in text~~ — PASS (scope_nesting, template_signedness have explicit negative-result markers)

### WP3: Expand Atlas Entries From Pattern Docs

Priority: P1

Status: **DONE** — 72 total entries (was 33), all tests passing.

Objective:
Turn the existing pattern documentation corpus into machine-usable atlas data.

Required file targets:

- `scripts/permuter/compiler_atlas.py`
- `docs/decomp/patterns/*.md`
- `scripts/permuter/tests/test_compiler_atlas.py`

Implementation tasks:

- ~~harvest entries from `docs/decomp/patterns/*.md`~~ DONE — 39 new entries from
  8 pattern docs: fixable-casting (7), fixable-declarations (6+1 from copy-ctor),
  fixable-control-flow (6), fixable-operators (4), fixable-comparison (2),
  fixable-fsel-fma (2), fixable-bool-mask (1), fixable-macros (1),
  unfixable-compiler (6), at-limit-systemic (1)
- ~~preserve provenance for each new atlas entry~~ DONE — each entry references
  its source doc section (e.g., "fixable-casting.md §noreturn Attribute")
- ~~mark entries as `PROVEN`, `INFERRED`, or `NEGATIVE`~~ DONE — 51 proven,
  3 inferred, 18 negative
- ~~attach boost/suppress pattern names where appropriate~~ DONE — 15 new entries
  have pattern_names for beam boost integration
- ~~avoid duplicating semantically identical entries under multiple names~~ DONE

Deliverables:

- ~~increased atlas coverage beyond the current 30 entries~~ DONE — 72 entries total
- ~~tests for lookup and boost/suppress behavior for new entries~~ DONE — 14 new
  tests in TestHarvestedLookups class (34 total, all passing)
- ~~short note in this roadmap stating the new entry count~~ DONE — 72 entries
  (51 proven, 3 inferred, 18 negative)

Exit criteria:

- new entries are queryable through the existing lookup API
- at least one new entry affects proposal ordering in a real beam run
- provenance for harvested entries is inspectable in code

### WP4: Broaden Region-Aware Pattern Coverage — DONE

Priority: P1

Status: **DONE** — 15 patterns now use region filtering (was 6).

Objective:
Move region attribution from a small pilot set to the main high-ROI pattern
surface.

Completed:

- Wired 9 additional patterns to `node_in_mismatch_region()`:
  ternary_swap, bool_cast, guard_to_nested, early_return_merge,
  statement_reorder, return_call_merge, float_literal_pressure,
  assert_line_fix, declaration_reorder (fallback mode).
- All 15 patterns gracefully fall back when no attribution data is available
  (the API returns True when `mismatch_regions` is empty).
- 738 existing tests pass without regressions.

Exit criteria met:

- region filtering used by 15 patterns (materially more than 6)
- no-attribution fallback verified by existing test suite (tests don't provide
  mismatch regions, so all patterns run unfiltered = backward compatible)

### WP5: Improve Validator Visibility — DONE

Priority: P1

Status: **DONE** — validation tiers visible in hill_climber, beam search survivors, and batch summaries.

Completed:

- `validator.py`: Added `format_result()` (concise/verbose modes) and
  `format_tier_distribution()` for human-readable validation output.
- `hill_climber.py`: Winner validation now prints full tier info via
  `format_result()` instead of only semantic issues. `HillClimbResult`
  carries `validation_tier` field. `_print_result()` shows tier in output.
- `beam_search.py`: Survivor summary shows `v=N` per-state validation tier.
  `_build_result()` propagates best-ever state's validation tier.
- `scan_and_permute.py`: Added `--validate` flag. When set, batch summary
  shows tier distribution (e.g., "SEMANTIC_OK:3 SCORE_IMPROVED:2").
  Result dict includes `validation_tier` for JSON output.
- `types.py`: `HillClimbResult.validation_tier` field added.
- 9 new tests for `format_result` and `format_tier_distribution`.

Exit criteria met:

- Runs show why a survivor ranked highly (tier visible in survivor list + result)
- Validation output visible via CLI without reading Python state
- `--validate` flag provides tier distribution in batch summaries

### WP6: Build A Stable IL Capture Corpus

Priority: P1

Status: **DONE** — 6 fixture bundles covering all required behavior families, 37 total functions.

Objective:
Make IL capture reproducible and reusable across many small source fixtures.

Required file targets:

- `msvc-src/tools/capture_il.py`
- `msvc-src/tools/il_parser.py`
- `msvc-src/analysis/il-fixtures/`
- `msvc-src/docs/IL_FORMAT.md`

Implementation tasks:

- ~~preserve the full `_CL_*` file bundle~~ — DONE
- ~~define a fixture manifest format~~ — DONE (12 fields per manifest)
- ~~capture representative fixtures for compare/cast, bool, rlwinm, switch, call/return~~ — DONE (6 bundles)
- ~~capture the IL_TYPE_CONTROL pair~~ — DONE (cast_shift vs and_shift)
- ~~add a CLI mode for listing and inspection~~ — DONE (`list-bundle`, `export-json`)

Deliverables:

- ~~committed fixture corpus~~ — 6 bundles, 37 functions total
- ~~documented bundle manifest format~~ — README.md updated
- ~~deterministic capture tool~~ — `il_parser.py capture --bundle-name`

Complete fixture list:
- `il_type_control_cast_vs_and` — CAST vs AND (4 functions)
- `il_bool_materialization` — comparison IL patterns (6 functions)
- `il_branch_polarity` — condition inversion, guards (7 functions)
- `il_rlwinm_shifts` — shift/mask fusion (7 functions)
- `il_switch_dispatch` — switch IL opcodes (5 functions)
- `il_call_return` — call/virtual/tail patterns (8 functions)

Exit criteria:

- ~~a new fixture can be captured with one command~~ — PASS
- ~~fixture metadata sufficient for reproduction~~ — PASS
- ~~corpus covers at least 5 distinct behavior families~~ — PASS (6 families)

### WP7: Parse The Full IL Bundle Into A Normalized Schema

Priority: P1

Status: **DONE** — 43 fixture-based tests, normalized JSON export, schema validation.

Objective:
Turn the raw `_CL_*` files into one structured representation suitable for
diffing, testing, and later lifting work.

Required file targets:

- `msvc-src/tools/il_parser.py`
- `msvc-src/docs/IL_FORMAT.md`
- tests under `msvc-src/tools/` or repo test locations

Implementation tasks:

- ~~define an `ILBundle` container spanning `.ex/.gl/.sy/.in/.db`~~ — DONE
- ~~parse symbol and type references across files~~ — DONE (name resolution in export-json)
- ~~emit normalized JSON for a fixture~~ — DONE (all 6 bundles)
- ~~add fixture-based tests that lock down the schema~~ — DONE: `msvc-src/tools/test_il_fixtures.py` (43 tests)
- ~~document known unknowns~~ — DONE: `assign`, `fallthrough`, `switch_table`, `val` kinds documented in test schema

Deliverables:

- ~~normalized parsed representation~~ — DONE: 9 top-level keys, full function bodies with operations
- ~~fixture-backed tests~~ — DONE: 43 tests across 8 test classes
- ~~updated IL_FORMAT.md~~ — bundle schema documented

Test coverage:
- Schema validation: top-level keys, file entries, token width, function fields
- Operation types: 14 known types validated across all 6 bundles
- Operand kinds: 6 known kinds validated
- Per-fixture behavioral tests: CAST/AND distinction, comparison operators, branch/switch patterns, call shapes
- Cross-bundle consistency: non-empty operations, manifest source paths
- Determinism: re-export produces identical JSON

Exit criteria:

- ~~parsing a fixture produces stable JSON across runs~~ — PASS (determinism test)
- ~~symbol/type tokens can be followed across file boundaries~~ — PASS (name resolution in JSON)
- ~~unknown bytes/records are surfaced explicitly in output~~ — PASS (unknown types tracked)

### WP8: Build A Constrained PPC->IL Lifter

Priority: P1

Status: **DONE** — comprehensive lifter with 80+ PPC mnemonics, CFG construction,
12 shape fact categories, machine-readable shape deltas, 173 unit tests.

Objective:
Lift a limited PPC subset into an IL-like representation that can be compared
to source-side IL.

Required file targets:

- `msvc-src/tools/ppc_il_lifter.py`
- `msvc-src/tools/test_ppc_il_lifter.py`
- `scripts/permuter/ppc_shape_facts.py`
- `msvc-src/docs/PPC_IL_LIFTER.md`

Implemented:

- `msvc-src/tools/ppc_il_lifter.py` — comprehensive PPC->IL lifter (~900 lines):
  - **Core ALU & moves** (24 mnemonics): `mr`, `fmr`, `li`, `lis`, `add`, `addi`,
    `addic`, `addic.`, `subf`, `subf.`, `and`, `andi.`, `andis.`, `or`, `ori`,
    `oris`, `xor`, `xori`, `xoris`, `not`, `nand`, `nor`, `orc`
  - **Shifts & masks** (12 mnemonics): `srwi`, `slwi`, `srawi`, `srw`, `slw`, `sraw`,
    `clrlwi`, `extrwi`, `clrlslwi`, `rlwinm`, `rlwinm.`, `rlwimi`, `rlwimi.`
  - **Bool carry chains** (12 mnemonics): `addic`, `subfe`, `subfc`, `subfic`,
    `addze`, `adde`, `subfze`, `cntlzw`, `eqv`, `andc`, `neg`, `srawi`
  - **Compares & branches** (14+ mnemonics): integer compares (`cmpwi`, `cmplwi`,
    `cmpw`, `cmplw`), conditional branches (`beq`-`bgt`), conditional returns
    (`beqlr`, `bnelr`, etc.), unconditional (`b`, `blr`), CR field-aware
  - **Float arithmetic** (20 mnemonics): binary (`fadd/fadds`-`fdiv/fdivs`),
    fused multiply-add (`fmadd/fmadds`-`fnmsub/fnmsubs`), unary (`fneg`, `fabs`,
    `fnabs`), conversion (`frsp`, `fctiwz`, `fctiw`, `stfiwx`), compare (`fcmpu`,
    `fcmpo`), special (`fsel`, `fres`, `frsqrte`)
  - **Multiply / divide** (6 mnemonics): `mullw`, `mulli`, `mulhw`, `mulhwu`,
    `divw`, `divwu`
  - **Memory** (22 mnemonics): base+offset loads/stores (integer, float),
    update-form (`lwzu`, `stwu`, etc.), indexed (`lwzx`, `lbzx`, `lfsx`, etc.)
  - **Switch dispatch & indirect calls**: `mtctr`, `mfctr`, `bctr`, `bctrl`,
    counted loops (`bdnz`, `bdz`, `bdnzeq`, `bdzeq`, etc.)
  - **Prologue / epilogue**: GPR/FPR save/restore helpers, link register ops
  - **Condition register** (5 mnemonics): `cror`, `crand`, `crandc`, `crxor`, `mfcr`
  - **Type conversion**: `extsh`, `extsb`, `extsw`, `fcfid`
  - **64-bit**: `ld`, `std`, `sradi`, `rldicl`
  - **Record-form variants**: `mr.`, `clrlwi.`, `srawi.`, `divw.`, `add.`
  - **Other**: `nop`
  - Unsupported instructions surfaced explicitly, never silently dropped
- **Data structures**: `LiftedOp`, `LiftedFunction`, `BasicBlock`,
  `ControlFlowGraph`, `PrologueInfo` — all with `to_dict()` for JSON export
- **CFG construction**: `build_cfg()` — basic block detection, successor edges,
  loop back-edge detection, nesting depth estimation
- **Pattern detection**: `detect_vtable_dispatch()` (lwz+mtctr+bctrl/bctr),
  `detect_switch_dispatch()` (switch_table vs ctr_chain vs if_chain),
  `detect_call_shapes()` (tail_direct_call, cached_return_value, etc.),
  `detect_float_conversion()` (fctiwz+stfiwx+lwz)
- **Shape fact categories** now include: byte_fusion, bool_materialization,
  switch_dispatch, virtual_dispatch, call_shape, float_conversion,
  float_fusion, prologue_shape, control_flow (cfg_complexity + counted_loop),
  operation_profile
- **Machine-readable shape deltas**: `compute_shape_delta()` — operation count
  differences by category, PPC-only and IL-only operations, switch/vcall/branch
  density comparisons
- **CLI**: `lift-listing` (with `--delta` flag), `compare-source`, `profile`
- **Bridge module**: `scripts/permuter/ppc_shape_facts.py` — exports
  `extract_shape_facts()`, `extract_lifted_function()`, `extract_shape_delta()`
- **Documentation**: `msvc-src/docs/PPC_IL_LIFTER.md` — all mnemonics, shape
  categories, target facts integration table, CFG and delta docs
- **Unit tests** cover instruction families, CFG, pattern detection, shape
  facts, shape deltas, operation profiles, and prologue info

Exit criteria:

- ~~the lifter can explain at least one of: bool materialization, rlwinm fusion,
  branch polarity, or switch shape~~ — PASS (all four, plus 8 more categories)
- ~~comparison output is usable by humans and scripts~~ — PASS (JSON + text + delta modes)
- ~~unsupported opcodes fail clearly instead of silently mis-lifting~~ — PASS

### WP9: Feed IL-Derived Hints Into The Permuter

Priority: P2

Status: **DONE** — 4th target-facts extractor with 10 shape-fact category
handlers, target-aware routing, wired into beam search + hill climber + batch
reporting. Guidance priority-floor override landed in generator/composer.
57 combined routing tests across target_facts + variant_tags + lifter.

Objective:
Use IL-level structure as another evidence source for search and ranking.

Required file targets:

- `scripts/permuter/target_facts.py`
- `scripts/permuter/ppc_shape_facts.py`
- `scripts/permuter/beam_search.py`
- `scripts/permuter/types.py`

Implemented:

- **4th extractor**: `extract_from_shape_facts()` in `target_facts.py` converts
  lifter-derived shape facts into `codegen_shape` TargetFact records with
  pattern routing. 10 shape-fact category handlers:
  - `byte_fusion` → boost/suppress `u8_to_unsigned_long` (target-aware: checks
    for `extrwi`/`clrlslwi` vs `srwi`/`slwi`/`clrlwi` in target diff ops)
  - `bool_materialization` → boost `bool_materialize` + `signed_unsigned` for
    signed/unsigned families (uses `bool_target_markers` set for target-aware
    gating)
  - `switch_dispatch` → boost/suppress `switch_if_convert` (target-aware:
    `target_switch_markers` vs `target_compare_chain_markers`)
  - `call_shape` → suppress `tail_call_reorder` for existing tail calls; boost/
    suppress `tail_call_reorder` for direct/call-sequence returns based on
    tail-vs-non-tail target evidence; boost `temp_elimination` for
    `cached_return_value` when the target prefers a straight-through return
  - `virtual_dispatch` → `virtual_call` flag
  - `prologue_shape` → `callee_saved_gprs/fprs`, boost `variable_extraction`
    when ≥10 GPR, boost `signed_unsigned` when ≥4 FPR
  - `control_flow:cfg_complexity` → block_count, loop_count, nesting_depth
  - `control_flow:counted_loop` → boost `foreach_to_dowhile`
  - `float_fusion` → fma_count
  - `float_conversion` → conversion_pattern
  - `operation_profile` → total_ops, direct_calls, indirect_calls, float_ops
- **Bridge**: `scripts/permuter/ppc_shape_facts.py` extracts shape facts from
  PPC listings using the lifter. `Scorer.get_shape_facts()` compiles baseline
  `/FAcs` listings and derives shape facts.
- **Beam/hill integration**: `beam_search.py` and `hill_climber.py` pass
  baseline shape facts into `extract_facts()`. Shape-derived boost/suppress
  flows through `TargetFacts.pattern_recommendations()` into `RoundHints`.
- **Override path**: boosted patterns now get a non-zero priority floor through
  `RoundHints.priority_floor()`, so target facts can force exploration even
  when diagnosis-only relevance would have returned 0. Compose-stage relevance
  respects the same override.
- **Reporting**: `TargetFacts.summary_lines()` surfaces codegen_shape categories
  and boost/suppress routing. `HillClimbResult` preserves `codegen_shapes`,
  `fact_boost_patterns`, `fact_suppress_patterns`. `scan_and_permute.py` emits
  batch summaries for observed shapes and fact-driven pattern adjustments.
- **Measured batch result**: on a real 5-function beam slice, the default path
  emitted `fact_boost_counts={"switch_if_convert":2,"tail_call_reorder":1,
  "temp_elimination":1}`. No wins yet; the current blocker is variant
  generation/applicability for boosted call-shape targets.
- **Tail-call applicability expansion**: `tail_call_reorder` now recognizes
  terminal single-call `if` wrappers and mixed wrapper/plain-call runs, which
  unlocks real cleanup/destructor shapes such as `VorbisReader::~VorbisReader`
  (4 generated variants after noise trimming in direct pattern measurement).
- **Noise trimming pass**: shared call classification now treats STL/container
  mutators, lifecycle helpers, `RELEASE`, `Mem*` helpers, and logging-style
  macros more aggressively, and `tail_call_reorder` rejects same-name pairs and
  macro-ish timer helpers. Direct measurement on 20 tail-call candidates now
  narrows to a small plausible set instead of broad infrastructure noise.
- **34 unit tests** in `scripts/permuter/tests/test_target_facts.py` covering
  all shape-fact handlers, target-aware routing, and integration

Exit criteria:

- ~~the permuter can consume at least one IL-derived signal~~ — PASS (10 shape
  categories flow through target_facts → pattern routing)
- ~~that signal changes proposal ordering in a measurable way~~ — PASS (boost/
  suppress multipliers in beam expansion + batch summary counts)
- Follow-up blocker:
  - boosted pattern families still need better variant quality and real-target
    win measurement to convert those routing signals into improvements

### WP10: Selective COLOR RE

Priority: P2

Status: **DONE** — Ghidra project created, 10 key functions decompiled,
allocation algorithm fully reverse-engineered.

Objective:
Answer the remaining register-allocation questions that black-box testing has
not resolved.

Required file targets:

- `msvc-src/docs/COLOR_RE.md`
- `msvc-src/scripts/c2_ghidra_server.sh`
- `msvc-src/scripts/c2_query.py`

Implemented:

- **Ghidra project** for c2.dll (PE32 x86, 1.3MB) using pyghidra headless API
- **10 functions decompiled** from the COLOR call graph:
  - Entry (23b), dispatcher (465b), simple alloc (842b), complex alloc (1089b),
    register assignment (251b), register selection (1891b), spill cost (220b),
    spill handler (668b), conflict resolver (387b), register lookup (105b)
- **Source path recovered**: `e:\bt\278379\vctools\compiler\be\p2\regasg.c`
- **Allocation algorithm fully documented**: linear scan with advancing pointer
  into priority tables, interference bitset checks, Belady-variant spill cost
- **6 GPR allocation order tables** extracted — all confirm r31-first descending
  for callee-saved, with table selection based on opt level + platform + calling
  convention flags
- **FPR and VMX tables** also extracted — f31-first descending, independent of GPR
- **BSF question answered**: **NO graph coloring exists**. The "COLOR" pass is
  a linear scan allocator. The "~7 variable" threshold was an artifact of
  volatile registers running out (9 volatile GPRs), not a BSF trigger.
- **Spill cost formula**: distance to next use (IL node count). Simple Belady variant.
- **Register swap root cause**: differences in IL node ordering between builds
  cause the advancing pointer to assign different physical registers. Not
  graph coloring worklist differences.

Key findings:

- **Linear scan confirmed**: No interference graph, no coloring loop, no
  simplicial elimination. Priority-table scan with bitset interference checks.
- **Advancing pointer mechanism**: Position pointers track last allocation
  position in the order table. Next allocation continues from that point.
  Creates the "first-declared = r31" pattern we observed in diff testing.
- **Spill cost = distance to next use**: Longest distance = cheapest to spill.
  Belady's optimal replacement variant.
- **Table variants by platform**: 6 GPR tables, selected by (opt_level,
  is_xenon, calling_convention_flag). DC3 uses O2 + Xenon variants.

Exit criteria:

- ~~the RE result explains a real observed gap in DC3 behavior~~ PASS —
  explained the "~7 variable" observation and register swap root cause
- ~~at least one finding is usable by the permuter, not just documented~~ PASS —
  spill cost formula and advancing pointer model are implementable as a
  register assignment predictor

## Next Sprint

The next sprint should execute these work packages in order:

1. WP1: measure beam vs greedy on a fixed hard slice
2. WP2: extend diff testing with the missing suites
3. WP9 follow-up: improve boosted pattern quality on call/switch targets
4. WP6/WP7 follow-up: extend the IL corpus for the next call families

After that:

- WP3 expands atlas coverage from pattern docs
- WP4 broadens region-aware pattern coverage
- WP5 improves validator visibility
- WP8 continues with virtual dispatch, arg-materialization, and wrapper shapes
- WP10 stays blocked until WP1 and WP2 are done

## Measurement

The roadmap should be judged on real outcomes, not architectural elegance.

Track at least:

- functions improved vs greedy and existing beam baseline
- proposals attempted per improvement
- build failures per depth
- attribution coverage on mismatched instructions
- region-level improvement retention in beam survivors
- number of atlas entries actually used during search

If a phase does not change search outcomes on representative plateaued targets,
it should be narrowed, rewritten, or dropped.

## Summary

**ALL 10 WORK PACKAGES ARE DONE.**

1. **Attribution** — `/FAs` listing parser, mismatch join, region aggregation,
   scorer integration, 15 region-aware patterns. DONE.
2. **Differential testing** — core harness + 13 suites with proven decision maps.
   DONE.
3. **Compiler atlas** — 80 entries (57 proven, 5 inferred, 18 negative), opcode-indexed
   lookup, beam boost/suppress wiring. Expanded from pattern docs. DONE.
4. **Target facts** — normalized evidence layer, 4 extractors (diagnosis, atlas,
   guidance, shape facts), wired into BeamState ranking and proposal filtering. DONE.
5. **Validator ladder** — 6-level chain (parse→build→score→region→fact→semantic),
   wired into beam search and hill climber. DONE.
6. **IL capture & parsing** — 6 fixture bundles, 37 functions, normalized JSON
   export, 43 fixture tests. DONE.
7. **PPC->IL lifter** — 80+ PPC mnemonics, CFG construction, 12 shape fact
   categories, machine-readable shape deltas, 173 unit tests. DONE.
8. **IL-derived permuter hints** — 10 shape-fact category handlers with
   target-aware routing, wired into beam search + hill climber + batch
   reporting, 34 target_facts tests. DONE.
9. **Selective compiler RE** — Three major compiler subsystems fully reverse-engineered:
   - **COLOR** (register allocator): Linear scan with advancing pointer into priority
     tables, Belady spill cost. 10 functions decompiled. NOT graph coloring.
   - **Inliner**: Threshold = **150 counted IL nodes** (not weighted — per-node weight
     stub returns 0). Linear flow functions always inlined. `__forceinline` bypasses
     check. Inlined callee cost subtracted from caller's budget. 6 functions decompiled.
   - **G3P2** (record-form fusion): Identifies subtract+compare-zero patterns in IL,
     transforms to single record-form opcode. G5P10 then emits `subf.`, `add.`, etc.
     Eligibility checker decompiled — most operations require zero comparison constant.
   DONE.

10. **Register swap fixing via IL live-range manipulation** (2026-03-11) — Proven
    technique for fixing callee-saved register swaps by changing WHICH values the
    compiler caches in callee-saved registers. Based on COLOR RE (linear scan,
    advancing pointer): first live range needing callee-saved → r31, second → r30,
    etc. By changing whether we cache a VALUE (int) or an ADDRESS (reference), we
    control which live range gets which register.

    **Proven fix: UIListDir::Save** (92.2% → 99.8%, r29↔r30 fixed):
    - Before: `auto& testState = mTestState; bs << testState.NumDisplay(); ... bs << testState.Speed();`
      - Compiler caches testState ADDRESS in r30, bs parameter gets r29
    - After: `int numDisplay = mTestState.NumDisplay(); bs << numDisplay; ... bs << mTestState.Speed();`
      - Compiler caches numDisplay VALUE in r29, bs parameter gets r30 (matching target)
    - Key insight: removing the reference alias eliminates one callee-saved live range
      (the address), and caching the accessor return value BEFORE a Write call matches
      the target's hoisting behavior. The Speed() call uses inline address computation
      (`subi r3, r31, 0x6c`) instead of the cached reference.
    - Remaining 0.2%: stack slot reuse (compiler allocates separate temps per WriteEndian
      in target, reuses 0x54 in base). Unfixable.

    **Technique summary**:
    - `auto& ref = member;` → caches ADDRESS, uses callee-saved GPR for pointer
    - `int val = member.Accessor();` → caches VALUE, uses callee-saved GPR for data
    - `member.Accessor()` inline → computes address inline (`subi rX, r31, offset`), no callee-saved
    - Swapping between these strategies shifts register assignments in the linear scan

    **Not yet fixable** (investigated but blocked):
    - RndTransAnim::Load (99.6%): target pre-computes address in chained `d >> mRotKeys >> mTransKeys`
      expression. Adding reference/splitting chain didn't help — compiler internal scheduling.
    - MetagameRank::SaveFixed (96.6%): r28↔r30 between static guard and static Symbol addresses.
      Both are compiler-generated; no source-level control over their allocation order.

11. **Handler body inlining** (2026-03-11) — When the target compiler inlines a wrapper
    function at a HANDLE_ACTION call site, the handler must replicate the wrapper body
    directly in the macro instead of calling the wrapper.

    **Proven fix: HamNavProvider::Handle** (98.4% → 100%):
    - Before: `HANDLE_ACTION(append_nav_item, AppendNavItem())`
    - After: `HANDLE_ACTION(append_nav_item, mNavItems.push_back(NavItem()))`
    - The AppendNavItem() wrapper was inlined by the target compiler at the call site.
      Our compiler kept it as a `bl` call. Inlining the body directly forces matching codegen.

    **Related: Automator::Handle** (98.8% → 100%):
    - Message temporary scoping: `_HANDLE_CHECKED(OnCustomMsg(Message(_msg)))` (temporary
      destructor runs at end of expression) vs named `Message msg(_msg)` (destructor at
      block end). Target uses temporary semantics.
    - OnMsg ICF wrappers: target ICF-merges 4 OnMsg overloads with identical body
      `HandleMessage(msg.Data()->Sym(1)); return DATA_UNHANDLED;`. Must use HANDLE_MESSAGE
      macro to route through these overloads.

The remaining work is:
- **validation at scale** — run the full pipeline on 30+ hard targets to measure
  improvement. Initial 30-target benchmark (2026-03-10) showed beam finding 3
  improvements in LOW bracket where greedy found none. Full-power run pending.
- **boosted-pattern applicability** — DONE. Fixed switch_dispatch routing (removed
  incorrect default-to-boost, fixed byte_fusion duplicate suppress). Fixed
  tail_call_reorder routing. Added 4 atlas entries (switch_table_dispatch,
  switch_compare_chain, tail_call_b_vs_bl, tail_call_prologue_delta). 7 new/fixed
  target_facts tests.
- **IL type control integration** — DONE. Pattern implementation (`u8_to_unsigned_long`)
  was already complete with 3 strategies (widen locals, return masking, combined).
  Atlas entries exist (rlwinm_fusion_extrwi, rlwinm_fusion_clrlslwi,
  u8_backward_propagation). Target_facts byte_fusion routing wired. 3 pattern fixture
  tests + 2 relevance tests added.
- **next IL lift families** — DONE (virtual dispatch + inline wrappers). Enhanced
  `detect_vtable_dispatch` to capture slot_offset, vbtable_offset, receiver_reg,
  and vbtable indirection flag. Added `detect_inline_wrapper` for trivial forwarding,
  accessor load, accessor store, and return forwarding shapes. Wired into target_facts
  routing (virtual_dispatch carries slot detail, inline_wrapper boosts noinline_stub).
  3 atlas entries added (vtable_slot_dispatch, accessor_inline_vs_outline,
  trivial_forwarding_wrapper). 9 new tests. Remaining: argument materialization,
  sparse switch lowering.
