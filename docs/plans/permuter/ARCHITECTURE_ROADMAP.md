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

## Phase 6: Better Semantic Context

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

Implemented so far:
- Shared statement effects now record call names and coarse call kinds
- The analyzer still behaves conservatively, but future reorder rules now have
  a tested place to hang call-side-effect heuristics
- `tail_call_reorder` now uses that call classification to reject obvious
  mutator/logging/guard call pairs instead of relying on a blanket
  `allow_call_pair=True` exception

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
