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
- [`scripts/permuter/target_facts.py`](../../scripts/permuter/target_facts.py) — normalized evidence layer with 3 extractors

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

- **beam search** — default strategy in scan_and_permute.py (`--strategy beam`).
  Multi-state, diagnosis-guided, diversity-preserving. BeamState carries source,
  score, diagnosis, tags, provenance chain, failure history, guidance agreement.
  Config: width=8, depth=4, expand=24, escape=4, diversity=3.
- **greedy and evolutionary** baselines for comparison
- **diagnosis** from objdiff instruction data (clusters, regswaps, offsets,
  prologue deltas, noise ratio)
- **Ghidra, m2c, RB3, and ASM-guided** proposal inputs
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
- **compiler atlas** — 30 `AtlasEntry` records: 18 proven (diff-test), 3 inferred,
  9 negative (AT_LIMIT). Opcode-indexed for O(1) lookup. `boost_patterns()` API
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
- stable IL capture corpus with reusable fixtures
- parsed IL bundle schema across `.ex/.gl/.sy/.in/.db`
- PPC ASM -> IL-style lifting for a constrained opcode subset
- IL-guided constraints or facts consumable by the permuter
- selective compiler RE for COLOR register allocator internals

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
- Add `--validate` flag to show per-variant validation tiers in output
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

- define a fixed slice of 30 hard functions with symbol, unit, baseline %, and
  reason for inclusion
- add a reproducible runner or documented command for beam vs greedy comparison
- capture per-function metrics: final %, delta, proposals attempted, build
  failures, region improvements, atlas usage, validation tier distribution
- emit machine-readable output (JSON or CSV) suitable for before/after analysis
- summarize the result in a committed artifact under `docs/` or `msvc-src/results/`

Deliverables:

- fixed target list checked into the repo
- one reproducible benchmark command
- one machine-readable results artifact
- one short findings summary with wins, losses, and neutral outcomes

Exit criteria:

- beam and greedy can be compared on the same fixed slice
- attribution coverage and proposal counts are visible in the output
- the repo contains committed benchmark results, not only code

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

Status: Started — named bundle capture, manifest writing, and bundle inspection
CLI landed in `msvc-src/tools/il_parser.py`. First real fixture captured:
`il_type_control_cast_vs_and` with all five `_CL_*` files, manifest, and
normalized `bundle.json`.

Objective:
Make IL capture reproducible and reusable across many small source fixtures.

Required file targets:

- `msvc-src/tools/capture_il.py`
- `msvc-src/tools/il_parser.py`
- `msvc-src/analysis/il-fixtures/`
- `msvc-src/docs/IL_FORMAT.md`

Implementation tasks:

- preserve the full `_CL_*` file bundle instead of only opportunistic captures
- define a fixture manifest format with source path, flags, symbols, and notes
- capture representative fixtures for compare/cast, bool materialization,
  rlwinm-sensitive byte shifts, switch, and call/return
- capture the first `IL_TYPE_CONTROL.md` pair:
  - `u8()` / CAST-driven byte narrowing
  - `& 0xFF` / AND-driven local masking
- add a CLI mode that lists captured functions and bundle contents

Deliverables:

- committed fixture corpus under `msvc-src/analysis/il-fixtures/`
- documented bundle manifest format
- capture tool that can preserve and inspect a bundle deterministically

Progress so far:

- fixture source added:
  `msvc-src/analysis/il-fixtures/sources/il_type_control_cast_vs_and.cpp`
- first captured bundle committed:
  `msvc-src/analysis/il-fixtures/il_type_control_cast_vs_and/`
- second fixture source added:
  `msvc-src/analysis/il-fixtures/sources/il_bool_materialization.cpp`
- second captured bundle committed:
  `msvc-src/analysis/il-fixtures/il_bool_materialization/`
- verified the CAST-vs-AND distinction at the IL level:
  - `cast_shift` emits IL `CAST` around byte narrowing before `SHR`
  - `and_shift` emits IL `AND` before/after `SHR` with no equivalent early byte cast
  - `cast_xor` emits trailing `CAST`
  - `and_xor` emits `AND(255)` then `CAST`
- captured a bool-materialization fixture family covering zero-test, equality,
  inequality, signed-positive, and ordered comparisons
- `il_parser.py export-json` now emits normalized `bundle.json` for a bundle

Exit criteria:

- a new fixture can be captured and checked into the repo with one command
- fixture metadata is sufficient for another agent to reproduce the capture
- the corpus covers at least 5 distinct compiler behavior families

### WP7: Parse The Full IL Bundle Into A Normalized Schema

Priority: P1

Status: Started — normalized bundle JSON export exists, but cross-file schema
linking is still minimal.

Objective:
Turn the raw `_CL_*` files into one structured representation suitable for
diffing, testing, and later lifting work.

Required file targets:

- `msvc-src/tools/il_parser.py`
- `msvc-src/docs/IL_FORMAT.md`
- tests under `msvc-src/tools/` or repo test locations

Implementation tasks:

- define an `ILBundle` container spanning `.ex/.gl/.sy/.in/.db`
- parse symbol and type references across files
- emit normalized JSON for a fixture
- add fixture-based tests that lock down the schema
- document known unknowns instead of silently dropping bytes

Deliverables:

- normalized parsed representation for all five IL files
- fixture-backed tests for parser stability
- updated `IL_FORMAT.md` with per-file schema notes

Progress so far:

- `ILFile.to_dict()` provides normalized JSON export for bundle-level metadata,
  globals, symbols, imports, debug summaries, and parsed functions
- `export-json` CLI added to `msvc-src/tools/il_parser.py`
- fixture-based tests added for bundle manifests and JSON export
- real fixture export now resolves symbol names into function operations and
  exposes partial `.in` / `.db` summaries for downstream tooling

Exit criteria:

- parsing a fixture produces stable JSON across runs
- symbol/type tokens can be followed across file boundaries
- unknown bytes/records are surfaced explicitly in output

### WP8: Build A Constrained PPC->IL Lifter

Priority: P1

Status: Started — initial constrained lifter landed in
`msvc-src/tools/ppc_il_lifter.py` with tests for rlwinm-sensitive shapes and
compare-source CLI support.

Objective:
Lift a limited PPC subset into an IL-like representation that can be compared
to source-side IL.

Required file targets:

- new lifter module under `msvc-src/tools/` or `scripts/permuter/`
- `tools/compiler_trace/asm_diff.py` or attribution helpers if reused
- tests for lifting and comparison

Implementation tasks:

- define the lifted form and its intentional limits
- support only the opcode families needed by known synthesis patterns
- map PPC compare/branch, casts, shifts/masks, loads/stores, switch, and
  call/return into the lifted representation
- build one comparison tool that shows source IL vs lifted PPC for a function

Progress so far:

- `msvc-src/tools/ppc_il_lifter.py` now lifts a constrained PPC subset into
  normalized ops:
  - `extrwi` -> `FUSED_SHR_MASK`
  - `clrlslwi` -> `FUSED_SHL_MASK`
  - `clrlwi` -> `BYTE_MASK`
  - `srwi` / `slwi` -> `SHR` / `SHL`
  - compare/branch, load/store, basic ALU, call/return
  - bool carry chains: `addic`, `subfe`, `subfc`, `subfic`, `addze`, `adde`,
    `subfze`, `cntlzw`, `eqv`, `andc`, `neg`, `srawi`
- unsupported PPC instructions are surfaced explicitly, not silently dropped
- `compare-source` CLI now shows source IL from `_CL_*` capture beside lifted PPC
- the tool now derives higher-level shape facts:
  - `byte_fusion=fused_shr_mask|fused_shl_mask|separate_shift_and_mask`
  - `bool_materialization=zero_test|equality_nonzero|inequality_nonzero|signed_positive|unsigned_ordered|signed_ordered`
- verified real cases:
  - `cast_shift` -> `byte_fusion=fused_shr_mask`
  - `zero_test` -> `bool_materialization=zero_test`
  - `signed_positive` -> `bool_materialization=signed_positive`
- tests added for fused byte shift/mask lifting, bool carry chains, and unsupported-op reporting

Remaining work:

- add switch and dispatch-table lifting
- emit machine-readable shape deltas instead of only side-by-side output
- prove at least one derived fact can feed the permuter

Deliverables:

- constrained lifter module
- fixture-based lift tests
- side-by-side comparison output for at least a few real examples

Exit criteria:

- the lifter can explain at least one of: bool materialization, rlwinm fusion,
  branch polarity, or switch shape
- comparison output is usable by humans and scripts
- unsupported opcodes fail clearly instead of silently mis-lifting

### WP9: Feed IL-Derived Hints Into The Permuter

Priority: P2

Status: Started — the target-facts layer can now ingest derived PPC shape
facts, and the beam seed path requests them from baseline `/FAcs` listings.

Objective:
Use IL-level structure as another evidence source for search and ranking.

Required file targets:

- `scripts/permuter/target_facts.py`
- `scripts/permuter/beam_search.py`
- `scripts/permuter/types.py`
- any new `il_features.py` or similar helper

Implementation tasks:

- define a small set of IL-derived facts or hints
- expose them through `TargetFacts` or `RoundHints`
- boost or suppress patterns based on lifted/source IL comparisons
- add a debug mode to print the IL-derived reasoning for one function

Progress so far:

- `scripts/permuter/ppc_shape_facts.py` extracts derived shape facts from a
  PPC listing using the constrained lifter
- `Scorer.get_shape_facts()` now compiles a baseline `/FAcs` listing and derives
  shape facts for the target function
- `TargetFacts` now ingests `shape_facts` and converts them to `codegen_shape`
  facts with pattern routing:
  - `byte_fusion=separate_shift_and_mask` -> boost `u8_to_unsigned_long`
  - `byte_fusion=fused_*` -> suppress `u8_to_unsigned_long`
  - `bool_materialization=*` -> boost `bool_materialize`
  - signed/unsigned bool families also boost `signed_unsigned`
- routing is target-aware: shape-derived boosts/suppression only fire when the
  target-side diff opcodes agree with that direction
- `beam_search.py` and `hill_climber.py` now pass baseline shape facts into
  `extract_facts()`
- `TargetFacts.summary_lines()` now surfaces `codegen_shape` categories and
  boost/suppress routing at search startup for normal runs
- tests added for shape-fact extraction and target-fact routing

Remaining work:

- prove the new facts measurably change proposal ordering on real targets
- decide whether shape facts should remain target-facts-only or also become
  direct `RoundHints`
- expose the same fact summary in more result/reporting surfaces if needed

Deliverables:

- IL-derived hints visible in beam expansion or ranking
- tests for at least one hint affecting proposal priority
- roadmap status update describing what hints are in use

Exit criteria:

- the permuter can consume at least one IL-derived signal
- that signal changes proposal ordering in a measurable way

### WP10: Selective COLOR RE

Priority: P2

Objective:
Answer the remaining register-allocation questions that black-box testing has
not resolved.

Required file targets:

- `msvc-src/docs/`
- `msvc-src/tools/`
- any new `msvc-src/model/` helper created from the findings

Implementation tasks:

- create the dedicated c2.dll Ghidra project and name priority COLOR helpers
- decompile the top COLOR helpers by size and trace their roles
- identify the BSF trigger condition and spill cost formula
- cross-check the RE result against diff-test observations
- extract any actionable heuristic into code the permuter can consume

Deliverables:

- annotated RE notes committed under `msvc-src/docs/`
- at least one actionable heuristic or predictor stub in code
- explicit statement of what question was answered

Exit criteria:

- the RE result explains a real observed gap in DC3 behavior
- at least one finding is usable by the permuter, not just documented

## Next Sprint

The next sprint should execute these work packages in order:

1. WP1: measure beam vs greedy on a fixed hard slice
2. WP2: extend diff testing with the missing suites
3. WP6: build a stable IL capture corpus
4. WP7: parse the full IL bundle into a normalized schema

After that:

- WP3 expands atlas coverage from pattern docs
- WP4 broadens region-aware pattern coverage
- WP5 improves validator visibility
- WP8 and WP9 begin once there is a stable IL fixture corpus
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

Phases 1-5 are implemented:

1. **Attribution** — `/FAs` listing parser, mismatch join, region aggregation,
   scorer integration, 6 region-aware patterns. DONE.
2. **Differential testing** — core harness + 5 suites with proven decision maps.
   DONE. Extension suites deferred.
3. **Compiler atlas** — 72 entries (51 proven, 3 inferred, 18 negative), opcode-indexed
   lookup, beam boost/suppress wiring. Expanded from pattern docs. DONE.
4. **Target facts** — normalized evidence layer, 3 extractors, wired into
   BeamState ranking and proposal filtering. DONE.
5. **Validator ladder** — 6-level chain (parse→build→score→region→fact→semantic),
   wired into beam search and hill climber. DONE.
6. **Selective compiler RE** — pipeline mapped, pass groups identified. Targeted
   decompilation (COLOR internals) not started.

The remaining work is:
- **validation at scale** — run the full pipeline on 30+ hard targets to measure
  improvement
- **IL type control integration** — the proven `u8()` CAST vs `& 0xFF` AND finding
  ([IL_TYPE_CONTROL.md](IL_TYPE_CONTROL.md)) should be wired into the permuter as:
  (a) atlas entries for `extrwi`/`clrlslwi` detection/suppression,
  (b) a new `u8_to_unsigned_long` pattern that converts `u8` intermediate types to
  `unsigned long` + `& 0xFF`, and
  (c) IL capture fixtures to confirm the CAST vs AND mechanism.
  This single finding fixed 20+ functions and likely affects hundreds more.
- **extension suites** — FPR, template, scope nesting differential tests
- ~~**atlas expansion**~~ DONE — 72 entries harvested from pattern docs
- **selective RE** — only for COLOR questions diff testing can't answer
