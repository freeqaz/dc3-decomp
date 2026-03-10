# Beam Solver Plan

This document proposes a new permuter search mode built around beam search
instead of the current greedy hill climber or the population-based
evolutionary loop.

This plan is a search-layer extension of the broader permuter roadmap in
`docs/plans/permuter/ARCHITECTURE_ROADMAP.md`. It is not a replacement for the
existing pattern, metadata, or cross-unit architecture work; it is the search
controller that should consume those capabilities more effectively.

## Goal

The goal of beam mode is:

- keep multiple reparsed source states alive at once
- spend limited build/score budget on the most promising next states
- allow neutral or slightly regressive intermediate rewrites when they open a
  better later path
- use diagnosis and auxiliary guidance (`ghidra`, `m2c`, `rb3`, future
  semantic summaries) to rank expansions rather than brute-force them

Put differently: beam mode should be the permuter's stateful multi-step search
mode for cases where single-incumbent hill climbing gets stuck but a generic
genetic algorithm is a poor fit.

## Non-Goals

Beam mode does not try to solve everything:

- It does not replace pattern design. Proposal quality still comes from
  patterns, constrained synthesis, and guidance adapters.
- It is not exhaustive search.
- It is not initially a cross-unit-by-default mode. Header-backed proposals
  should remain opt-in and separately budgeted.
- It does not require immediate removal of greedy mode. Greedy remains the
  cheap baseline and beam mode should prove its value before becoming default.
- It does not depend on GA-style crossover. If some limited recombination
  remains useful later, that is optional, not central.

## Why A New Mode

The current search options have opposite weaknesses:

- Greedy hill climbing is cheap and easy to reason about, but it only keeps one
  working state. It gets trapped when the next useful move is neutral or
  slightly regressive before a later recovery.
- The evolutionary mode keeps multiple states alive, but its crossover is weak
  for source-to-source decompilation work because most meaningful rewrites are
  not cleanly recombinable text edits.
- Constraint synthesis is currently a narrow round-1 prepass, not a true search
  strategy.

For this task, the search space is:

- expensive to evaluate
- highly discrete and structured
- rich in diagnosis/guidance signals
- poor for generic genetic crossover

That points to a beam-style search as a better fit.

## Success Criteria

The initial implementation should be judged by concrete outcomes:

- it improves more functions than greedy on a representative hard-target slice
- it does so with fewer wasted builds than evolutionary mode
- it preserves state integrity: reparsing, apply/restore, and cached context
  remain correct across many branching states
- it makes intermediate decisions inspectable enough that a failed run can be
  debugged from logs and saved state summaries

If beam mode cannot beat greedy on real plateaued targets with acceptable
runtime, it is not yet the right replacement.

## Core Idea

Keep the top `K` candidate states after each expansion round instead of a
single incumbent.

Each state is a reparsed function plus metadata:

- source bytes
- auxiliary file edits
- current score
- diagnosis
- structural tags
- provenance chain
- build-failure history
- available context (`ghidra`, `m2c`, `rb3`, `asm`)

At each step:

1. Expand each beam state with a bounded number of high-value proposals.
2. Score the resulting variants.
3. Keep only the top `K` survivors after deduplication and diversity checks.
4. Stop on perfect match, stagnation, or budget exhaustion.

This is not exhaustive search. It is a structured, diagnosis-guided best-first
search with bounded width.

The key architectural distinction is:

- proposal generation suggests possible next edits
- beam search decides which resulting source states are worth keeping alive

That separation matters because `--constrained` should become a proposal source,
not a separate search mode, and because new guidance sources should plug into
proposal ranking without rewriting the search loop.

## Proposed Search Loop

### 1. Seed

Build an initial beam from multiple proposal sources:

- diagnosis-weighted single-pattern variants
- structural-tag follow-up pairs
- cross-compose pairs
- constrained synthesis candidates
- Ghidra-guided variants
- m2c-guided variants

Phase-1 beam mode should remain single-function-first. Cross-unit/header-backed
variants should be disabled by default and only enter the seed when explicitly
requested.

The seed should not be limited to the single best score. Keep a small diverse
set such as:

- best overall
- best control-flow rewrite
- best register-allocation rewrite
- best Ghidra-guided
- best m2c-guided
- best composed/chain candidate

### 2. State Re-diagnosis

For each beam survivor, re-extract and re-diagnose the new source state.

This is critical. Search quality comes from updating the mismatch model after a
meaningful rewrite, not from continuing to use the original diagnosis forever.

### 3. Guided Expansion

Each state receives a proposal budget split across:

- direct pattern generation
- metadata-declared follow-ups
- structural-tag follow-ups
- constrained synthesis
- small repair/escape moves

Budgets should be rank-based, not uniform. Use:

- diagnosis relevance
- pattern/tag historical win rate
- build-failure suppression
- guidance agreement (`ghidra`, `m2c`, `rb3`)

Expansion should be state-local. Each beam state must expand from its own
reparsed source and its own refreshed diagnosis, not from the original
function's initial diagnosis.

### 4. Beam Selection

After scoring, keep the top `K` states with diversity protection.

Selection key should not be raw score alone. Prefer:

1. higher match %
2. lower build-failure risk
3. higher guidance agreement
4. structural diversity
5. shorter edit chain

Diversity rules should prevent the beam from collapsing into near-duplicates
that differ only by low-value syntactic noise.

### 5. Plateau Handling

When the beam stalls:

- widen the proposal mix temporarily
- allow neutral but diagnosis-shifting moves
- run a small escape budget of perturbation operators
- restart one or two beam slots from the best historical alternate branch

This is the practical replacement for generic GA mutation pressure.

## Why Beam Search Fits Better Than GA

Beam search matches the constraints of the permuter better:

- State transitions are meaningful and inspectable.
- Proposal generation is already diagnosis-guided.
- Good multi-step fixes usually look like short, coherent rewrite chains.
- Crossover has weak semantics because overlapping edits are common and many
  useful transformations are not text-independent.
- The scorer is expensive, so search should spend budget on guided successors,
  not broad random recombination.

## Proposed New Mode

Add a new mode, tentatively `--beam`.

Suggested initial flags:

- `--beam-width 8`
- `--beam-depth 4`
- `--beam-expand 24`
- `--beam-escape 4`
- `--beam-diversity 3`

These should be separate from `--chain-depth`. Chain depth is a proposal
generator detail; beam depth is a search budget detail.

Initial coexistence policy:

- keep `--beam` separate from `--evolutionary`
- treat greedy as the control/baseline mode
- only consider retirement of `--evolutionary` after beam mode has comparable
  logs, diagnostics, and measured win rate on hard targets

## State Model

Add a beam-state dataclass containing:

- `variant`
- `score`
- `diagnosis`
- `available_context`
- `tags`
- `applied_patterns`
- `generation`
- `stagnation_count`
- `build_fail_count`
- `guidance_agreement`

This should live beside `HillClimbResult` and `RoundHints`, not inside pattern
modules.

The state object should represent a real source state, not just a scored edit:

- reparsed `FunctionContext`
- cached guidance payloads for that state when needed
- enough provenance to explain why the state survived selection

## Proposal Sources

The beam mode should treat the following as interchangeable proposal sources:

- `generate_variants()`
- composed pairs
- adaptive chains
- constrained synthesis
- m2c-guided transforms
- cross-unit/header-backed candidates when explicitly enabled

That means constrained synthesis stops being a separate search mode and becomes
one producer in a broader proposal market.

This is the main reframe for `--constrained`: deterministic, guidance-backed
proposal extraction that can be invoked at seed time or on later beam states,
instead of a one-off round-1 special case.

## m2c Support

The permuter already benefits from Ghidra and RB3 context. m2c should be added
as a third structural guidance source.

Expected m2c value:

- more compiler-shaped call ordering
- better temp/live-range shape than Ghidra in some functions
- a second opinion on control-flow form
- better signal for tail-call ordering, guard structure, and call gating

Near-term m2c integration should provide:

- cached m2c text in `FunctionContext`
- `available_context` support for `m2c`
- lightweight extractors for:
  - last-call shape
  - condition structure
  - call list / call ordering
- search-time agreement scoring:
  - `ghidra + m2c agree`
  - `ghidra only`
  - `m2c only`
  - `conflict`

The beam selector should prefer candidates that improve score while also moving
source structure toward areas where Ghidra and m2c agree.

## Constrained Search Reframe

`--constrained` should stop meaning “run one special round-1 prepass.”

Instead it should mean:

- enable deterministic proposal extraction from guidance sources
- feed those proposals into the beam alongside ordinary pattern variants
- allow repeated constrained expansion after state re-diagnosis

This turns constrained synthesis into a reusable proposal generator rather than
an awkward special-case mode.

## Resolved Design Decisions

The following decisions should be treated as the phase-1 defaults.

### Survivor Ranking

Beam selection should use a lexicographic-style ranking, not a single opaque
score.

Primary ordering:

1. higher `match_percent`
2. lower `build_fail_count`
3. higher `guidance_agreement`
4. lower `stagnation_count`
5. shorter provenance chain

Tie-breaking should stay explainable. The implementation may internally convert
this to a weighted tuple, but logs should still print the component values
rather than only a collapsed scalar.

Initial `guidance_agreement` scoring:

- `+2`: Ghidra and m2c agree and the state moves toward that structure
- `+1`: only one guidance source supports the state
- `0`: no useful guidance signal
- `-1`: Ghidra/m2c conflict or the state moves away from the strongest signal

This is intentionally coarse for phase 1.

### Diversity Policy

Diversity should be structural-first, source-second.

The selector should enforce at least these buckets before filling remaining
slots by raw score:

- structural tags
- last applied pattern / proposal source
- guidance bucket (`agree`, `ghidra_only`, `m2c_only`, `none`, `conflict`)

Within a bucket, dedup and near-dedup should use:

- exact variant identity bytes first
- exact source bytes second
- cheap source-distance heuristics third

Byte-distance alone is not enough. We already know from composed-chain pruning
that edit-size diversity is a weak proxy for meaning.

### Guidance Refresh Policy

Guidance should be split into two classes:

- stable target-side guidance: original Ghidra/m2c/objdiff facts about the
  target function shape
- derived state-side summaries: facts extracted from the current source state

Phase-1 rule:

- always reuse stable target-side guidance for all descendants of the same root
  function
- always recompute derived state-side summaries after reparsing a surviving
  state
- never rerun full Ghidra or m2c decompilation for each child state

This keeps state-local ranking accurate without making beam search too
expensive.

### Mandatory Proposal Sources For Phase 1

Phase-1 beam mode should require only proposal sources that already exist in
the single-function architecture:

- direct pattern generation
- metadata/static follow-up pairs
- adaptive chains
- constrained synthesis
- existing Ghidra-guided variants

Optional in phase 1:

- m2c-guided proposal generators beyond lightweight ranking/support hints
- cross-unit/header-backed proposals
- any recombination/crossover logic

This keeps the first beam implementation narrow enough to compare fairly
against greedy mode.

### Build Failure Attribution

Build failures must be attributed per beam state and per base pattern.

Phase-1 rules:

- record failures on `(pattern_name, guidance_bucket)` within a state lineage
- suppress a pattern only for that lineage after repeated failures
- allow the same pattern to remain active in other lineages
- only elevate a pattern to run-wide suppression if it fails in every lineage
  that attempted it during the current depth

This is stricter than the current greedy `RoundHints.build_failed_patterns`
behavior and avoids over-poisoning patterns globally.

Constrained proposals should follow the same policy: a failing constrained
candidate should suppress its originating transform class in that lineage, not
disable synthesis entirely.

### Logging And State Capture

Beam mode should be debuggable by default.

Minimum required logging per depth:

- beam survivors and their ranking components
- proposal counts by source
- number of build failures by pattern/source
- dedup counts
- why a survivor displaced another candidate
- whether guidance agreement changed

Minimum retained state for the top survivors:

- source hash
- pattern/proposal provenance chain
- tags
- diagnosis summary
- ranking tuple
- build failure summary

This should be emitted in a compact human-readable form first. JSON export is
useful later, but it should not block the initial implementation.

## Deferred Questions

These are still real, but they should not block phase 1:

- How aggressively should beam width expand under plateau handling?
- Should neutral diagnosis-shifting states be guaranteed a survivor slot, or
  only when diversity would otherwise collapse?
- When cross-unit/header proposals are enabled, should they compete directly
  with local states or live in a separate sub-beam?

## Recommended Implementation Order

### Phase A: Infrastructure

- add a beam-state dataclass
- factor state re-diagnosis into a reusable helper
- factor proposal-source execution behind a shared interface
- finalize cached auxiliary context loading (`ghidra`, `m2c`, `rb3`) per state

### Phase B: Search Skeleton

- add `beam_search.py`
- seed beam from existing generators
- score and select top `K`
- add stagnation and diversity controls
- add logging/reporting that makes survivor decisions inspectable

### Phase C: Proposal Integration

- integrate constrained synthesis as a proposal source
- integrate adaptive chain generation per state
- integrate tag-history ranking per state
- make existing single-function proposal sources usable without rewriting them
  per search mode

### Phase D: Guidance Scoring

- add Ghidra/m2c agreement signals
- add m2c-guided tail-call/control-flow helpers
- use guidance agreement in beam ranking

### Phase E: Cross-Unit Extension

- allow optional header-backed beam proposals
- keep these behind explicit flags or separate budgets
