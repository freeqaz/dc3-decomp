# Permuter Architecture Roadmap

This roadmap captures the next architectural layer for the C++ permuter after
pattern registration, basic composition, diagnosis-guided prioritization, and
the recent control-flow rule improvements.

## Goals

1. Make safety-critical pattern logic reusable instead of reimplemented per rule
2. Raise the ceiling on control-flow and reorder-style fixes without bloating
   each individual pattern
3. Separate single-function permutation from cross-unit/header-wide workflows
4. Improve search quality by carrying more structural information through the
   permuter pipeline

## Phase 1: Shared Statement-Effect Analysis

Status: Completed

Problem:
- Reorder-oriented patterns each reimplement their own read/write/control-flow
  checks
- Safety logic is duplicated across `statement_reorder`, `tail_call_reorder`,
  and future reorder patterns
- Pattern-local heuristics drift over time and are hard to test independently

Deliverables:
- Add a shared analyzer that computes per-statement:
  - reads
  - writes
  - control-flow presence
  - call presence
  - pointer dereference bases
  - assertion/guard-like markers
- Use the analyzer in:
  - `statement_reorder`
  - `tail_call_reorder`
- Add executable tests for the analyzer and the migrated patterns

Why this first:
- It improves multiple existing patterns immediately
- It is local to the current single-function architecture
- It creates a reusable substrate for future control-flow and movement rules

## Phase 2: Shared Control-Flow Helpers

Status: Completed

Problem:
- Several patterns inspect `if`/`switch`/loop tails independently
- Terminal-path reasoning is currently heuristic and duplicated

Deliverables:
- Reusable helpers for:
  - terminal block discovery
  - bare-return detection
  - trailing statement-run extraction
  - side-effect-safe path-local rewrites
- Migrate:
  - `tail_call_reorder`
  - `switch_if_convert`
  - `return_call_merge`
  - future loop/control-flow patterns

Expected value:
- Better coverage for end-of-path transformations
- Less ad hoc AST walking in individual patterns

## Phase 3: Pattern Metadata and Registry Safety

Status: In progress

Problem:
- Pattern modules can exist on disk without participating in the registry
- Follow-up wiring is partly static and partly tribal knowledge

Deliverables:
- Add tests that fail if pattern files are present but unregistered
- Let patterns declare:
  - safety tier
  - structural domain
  - likely follow-ups
  - whether they depend on Ghidra, RB3, or other auxiliary context
- Generate or validate the follow-up map from metadata where possible

Expected value:
- Less registry drift
- Lower maintenance cost as the rule set grows

Implemented so far:
- Registry tests now fail if pattern files exist on disk without registration
- Patterns can declare lightweight metadata:
  - `safety_tier`
  - `structural_domain`
  - `follow_ups`
  - `requires_context`
- Composer follow-up discovery now merges declared follow-ups with the static map
- Registry tests validate that declared follow-ups point at real registered patterns
- Generator budget allocation now uses `safety_tier` as a small tie-breaker,
  preferring more conservative patterns when other signals are equal

## Phase 4: Structural Search Layer

Status: In progress

Problem:
- The composer mostly chains named patterns using a curated follow-up map
- Search still reasons in terms of pattern names instead of structural effects

Deliverables:
- Add postcondition tags to variants such as:
  - `introduced_temp`
  - `reordered_tail_calls`
  - `converted_if_to_switch`
  - `reduced_live_range`
- Use those tags to drive smarter follow-on pattern selection
- Bias budgets using historical “mismatch profile -> winning tag/pattern” data

Implemented so far:
- Variants now carry structural tags through compose, chain, merge, and mutation
- Cross-compose uses tag-aware follow-up selection
- Round history records winning/promising tags
- Adaptive compose-pair and chain construction now uses tag history before scoring
- Core movement/register-allocation patterns now emit structural tags too:
  - `moved_declaration`
  - `reordered_declarations`
  - `reordered_assignments`

Expected value:
- Better budget use than static pair lists alone
- Stronger multi-step fixes without hand-encoding every combination

## Phase 5: Cross-Unit/Header-Aware Tier

Status: Completed

Problem:
- Some high-value fixes, especially tail-call reorder in header inlines, are
  fundamentally multi-symbol
- The current permuter assumes a single source edit only affects the current
  function score

Deliverables:
- Separate tooling for header-inline and cross-unit variants
- Multi-symbol scoring and blast-radius accounting
- Safe application flow for shared-header edits

Expected value:
- Unlock fixes the single-function permuter will never see
- Keep risky cross-unit edits out of the normal hot path

Implemented so far:
- Shared header-impact analysis now computes:
  - direct includers
  - transitive affected translation units
  - include-depth-derived risk tiers
- Variants can now carry auxiliary file edits in addition to the primary
  function source edit
- Scoring and apply/restore flows now understand multi-file variants, including:
  - cache keys that distinguish header edits from same-source variants
  - safe restoration of touched auxiliary files after scoring
  - final apply/verification paths that preserve or roll back auxiliary edits
- `noinline_stub` can now emit conservative direct-header variants by:
  - resolving directly included headers
  - finding trivial inline callees in those headers
  - skipping high-risk headers by blast-radius tier
  - emitting auxiliary header edits instead of pretending the change is local
- Cross-unit source-to-symbol lookup now exists, so header impact can be turned
  into concrete functions to score rather than just a list of affected files
- A reusable header-variant scorer now exists for:
  - looking up affected functions
  - rebuilding only the affected object targets
  - skipping unchanged objects via obj hash comparison
  - rescoring changed symbols with objdiff batch
  - computing net improvement/regression/perfect-loss summaries
- A standalone header-variant workflow now exists for the first consumer path:
  - discover header-backed variants from a function/pattern
  - score them with the multi-symbol scorer
  - report ranked results
  - optionally apply the best accepted variant
- Header-inline tail-call discovery now exists as a second cross-unit path:
  - starts from the current caller
  - resolves directly included inline header callees
  - finds safe trailing-call swaps using the shared tail-call safety logic
  - emits auxiliary header edits that flow through the same multi-symbol scorer
- Additional cross-unit header bridges now exist for selected local patterns:
  - `header_return_call_merge`
  - `header_switch_if_convert`
  - `header_early_return_merge`
  - `header_branch_polarity`
  - `header_single_return`
  - `header_guard_to_nested`
  - `header_statement_reorder`
  - `header_variable_extraction`
  These reuse existing proven single-function transforms on directly included
  inline headers and feed the same cross-unit scoring workflow
- Header bridge support is now metadata-driven via pattern declarations rather
  than a separate hardcoded allowlist, which keeps future cross-unit opt-ins
  aligned with registry/test coverage

Follow-on work:
- Still-broader cross-unit patterns beyond the current header-backed set
- Search policies that gate or prioritize cross-unit edits using the new risk
  tier and include graph data
- Worktree-backed execution for isolated cross-unit rebuild/scoring loops

## Phase 6: Better Semantic Context

Status: Complete

Problem:
- Many patterns still use syntax-only checks where semantic summaries would
  permit more targeted and safer rewrites

Deliverables:
- Alias-aware local effect summaries
- Better call classification (pure/read/write/unknown)
- Lightweight CFG summaries for terminal-path transforms
- More precise Ghidra/RB3 guidance adapters

Expected value:
- Broader safe rewrite space
- Fewer false-positive variants

Implemented:
- Shared statement effects now record call names and coarse call kinds
- The analyzer still behaves conservatively, but future reorder rules now have
  a tested place to hang call-side-effect heuristics
- `tail_call_reorder` now uses that call classification to reject obvious
  mutator/logging/guard call pairs instead of relying on a blanket
  `allow_call_pair=True` exception
- m2c structural extractors now available for pattern guidance:
  - `extract_nesting_depth()` — maximum nesting depth from m2c output
  - `extract_guard_count()` — count of guard-return patterns
  - `extract_return_pattern()` — classifies return structure (merged_var, split_calls, guard_chain, single)
  - `extract_call_order()` — function call names in order of appearance
- m2c guidance now wired into 4 patterns:
  - `guard_to_nested` — uses nesting depth + guard count to prefer direction
  - `return_call_merge` — uses return pattern to prefer merge vs split
  - `early_return_merge` — uses return pattern + guard count to filter directions
  - `statement_reorder` — uses call order to prioritize swaps toward target order
- Alias-aware statement effects (`AliasInfo`) now detect reference and pointer
  bindings (`auto& ref = obj.member`, `Type* p = &obj`) and track them per
  statement, enabling alias-aware safety checks in reorder rules
- Def-use chain construction (`DefUseChains`) across statement sequences:
  - Per-variable definition-to-use tracking with live-in support for parameters
  - Live-range computation (first_def, last_use) per variable
  - `can_move_past(stmt_idx, target_idx)` for safe multi-step movement
  - `is_live_between(var, start, end)` for liveness queries
  - `statement_reorder` now uses def-use chains for multi-step statement moves
    instead of only pairwise independence checks
- Lightweight CFG module (`cfg.py`) for function bodies:
  - `BasicBlock` / `CFG` dataclasses with successor/predecessor edges
  - `build_cfg()` — splits at if/switch/loop/return boundaries
  - `is_terminal_block()` — recursive all-successors-terminal check
  - `reaches_exit()` / `dominates()` — reachability and dominance queries
  - `stmt_is_in_terminal_position()` — primary API for tail-call analysis
  - `live_variables_at_block_entry()` — backward liveness using statement effects
  - `tail_call_reorder` now uses CFG terminal-block discovery as a fourth
    strategy for finding swap sites, complementing the AST-walk approaches
- 27 new tests covering CFG construction, terminal detection, dominance,
  liveness, alias detection, and def-use chain safety

## Phase 7: Worktree-Aware Cross-Unit Execution

Status: Planned

Problem:
- Cross-unit/header scoring mutates shared files and can trigger multi-object
  rebuilds, which is a poor fit for the normal single-tree hot path
- We already have orchestrator worktree-pool machinery for isolation, commit
  sync, and DB-backed leasing, but the permuter does not use it yet

Deliverables:
- Add an optional worktree-backed execution mode for cross-unit scoring flows
- Reuse the existing worktree pool's:
  - DB locking / session leasing
  - sync-to-main-commit behavior before a worktree is handed out
  - patch extraction / cleanup lifecycle
- Let header-wide scorers and future cross-unit CLIs run in isolated worktrees
  instead of mutating the main checkout

Expected value:
- Safer cross-unit experimentation
- Natural parallelism for expensive rebuild/score loops
- Reuse proven infrastructure instead of inventing a second isolation path

## Phase 8: Beam Search

Status: Complete (Phases A-E all implemented)

Problem:
- Greedy hill climbing keeps only one working state and gets trapped on plateaus
- Evolutionary mode's crossover is weak for structured source-to-source rewrites

Deliverables:
- Multi-state search that keeps the top K candidates alive at each depth
- Multi-criteria ranking: score, build reliability, guidance agreement, diversity
- State re-diagnosis between expansion rounds
- Integration with all existing proposal sources

Implemented so far:
- `BeamState` dataclass with lexicographic `ranking_key` property
- `BeamConfig` dataclass for width, depth, expand, escape, diversity
- `beam_search.py` — full search loop (seed → expand → score → select)
- `_expand_state()` — per-state proposal generation via standard patterns
- `_select_survivors()` — diversity-aware selection
- `_rediagnose_state()` — reparse + re-diagnose for next depth
- Stagnation detection across all beam survivors
- Best-ever tracking with guaranteed beam inclusion
- `--beam` flag in hill_climber CLI + standalone `beam_search.py` CLI
- Constrained synthesis integrated as per-state proposal source (not just
  round-1 prepass) — runs at every beam depth when Ghidra AST available
- Real guidance agreement scoring using structural extractors:
  - Compares source guard count, nesting depth, and return pattern against
    both m2c and Ghidra targets
  - Returns +2 (both agree), +1 (one agrees), 0 (neutral), -1 (diverges)
- 25 unit tests for ranking, selection, dedup, guidance, config, escape
- Cross-unit beam proposals via `--cross-unit` flag (Phase E)

See `docs/plans/permuter/BEAM_SOLVER.md` for the full design.

## Immediate Implementation Track

The first implementation slice from this roadmap is:

1. Introduce shared statement-effect analysis in permuter core
2. Migrate reorder-style patterns to that API
3. Add tests that exercise the shared logic directly
4. Use the new layer as the basis for future control-flow helpers
5. Add structural tags to variants and start using them in composer follow-up selection

## Non-Goals for This Slice

- No header-wide permutation engine yet
- No full CFG builder yet
- No pattern metadata schema redesign yet
- No historical ranking database changes yet

## Success Criteria

- Reorder-style patterns share one tested safety model
- No behavior regressions in focused tests for existing reorder rules
- Future reorder/control-flow patterns can build on the same analyzer instead of
  reimplementing read/write checks
