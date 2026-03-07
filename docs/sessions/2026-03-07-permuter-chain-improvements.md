# Session: Permuter Chain/Composition System Improvements

Date: 2026-03-07

## What we did

Implemented the initial chain/composition improvements per the plan:
- Expanded `_FOLLOW_UP_MAP` from 18 to 52 entries
- Added recursive BFS chain walker (`_walk_followups`)
- Dynamic compose pairs per round via `get_compose_pairs()`
- Chain wins now credited in `_query_effective_pairs` DB query
- Component credit in `mark_winner` for compose/chain winners
- FPR-specific diagnosis chains
- Priority field on `ChainSpec`, sort before truncation
- Raised `max_chains` 5 -> 10, budget split 60/20/20 -> 50/20/30
- Round-1 diagnosis-relevant pairwise combos (Layer 2.5)
- Updated `/permute` skill to pass aggressive single-function flags

Commits: `fb82e750e`, `995d4c996`

## Batch test results (200 functions, 50-90% range)

- 11,676 variants scored across 200 functions in 688.6s
- 27 "NEW BEST" improvements found (--no-apply mode)
- 11 of 27 wins (41%) came from compose pairs -- validating the new system
- Winning compose pairs: `signunsign+cmpflip`, `brpol+cmpflip`, `varext+declreorder`, `boolcast+cmpflip`, `stmt_reorder+declreorder`, `tmpelim+varext`

## Five categories of waste identified

### 1. Build failures: 1,798 of 11,676 variants (15.4%)

Worst offenders: `argswap_0` (65), `deepbind_0` (57), `boolcast_0` (55), `hoist_0` (51), `const_0` (45).

### 2. Round 2 wasted: 160 all-cache rounds

Deterministic patterns on unchanged source = 100% cache hits.

### 3. Compose/chain duplication

Same pair in both Phase 2 and Phase 3 = double compile cost.

### 4. Zero 3-stage chain output

3-stage chains built but beam search produces nothing. Intermediate relevance checks or reparse failures kill the chain.

### 5. Chain budget = second compose phase

30% chain budget produces only 2-stage chains identical to compose pairs.

---

## Phase A: Quick wins — DONE

Commit `5ebee74ae`. Implemented all three Tier 1 fixes:

### 1. Source-level dedup ✅
- `seen_sources: set[int]` hash set in `generate_variants()` spanning all phases
- Includes baseline hash to catch no-op variants
- Logged dedup count to stderr

### 2. Fix 3-stage chains ✅
- **Diagnosis suppression**: At intermediate stages (>0, non-final), `beam_ctx.diagnosis = None` prevents pattern relevance filtering from killing the beam
- **Relaxed _prune_beam**: `max(3, diff_count // 10)` instead of fixed `5`
- **Partial chain fallback**: When beam dies at stage N, yields stage N-1 candidates as shorter chains
- **Scaled beam_width**: `max(5, len(chain.stages) * 3)`
- **Metadata propagation**: `reparse_variant()` now copies `symbol`, `ghidra_code`, `ghidra_ast`, `target_var_order`, `target_gpr_saves`, `asm_listing_path`

### 3. Build failure suppression ✅
- `_collect_build_failed_patterns()` identifies base patterns with 100% build failure
- `RoundHints.build_failed_patterns` populated after each round
- `generate_variants(failed_patterns=...)` suppresses from compose/chain first-stage
- Also: `_split_pattern_name()` now handles `crosscompose:` prefix

---

## Subagent research findings (Phase B-C specs)

Six subagents investigated the remaining items. Key discoveries that changed or refined the original plan:

### Cross-variant composition (#4) — ready to implement
- Phase 4 runs **after** Phase 1 scoring, **inside** the Scorer context — NOT inside `generate_variants()`
- Trigger: `len(improvers) >= 2` (need at least 2 improving variants to cross-compose)
- Companion pattern selection: union of (independently-improving patterns) + (_FOLLOW_UP_MAP for winner)
- Budget is **additional** (not subtracted from Phase 1-3) — `min(30, max_variants // 3)`
- Existing 3-layer scorer dedup (source_dedup, cache_hit, obj_dedup) handles Phase 4 overlap with Phase 2/3 transparently
- New functions: `select_improvers()`, `_select_companion_patterns()`, `cross_compose_variants()` in `composer.py`

### Build failure suppression DB (#2 Level 2) — ready to implement
- `query_build_failure_rates(min_runs=5, recent_days=14)` in `pattern_stats.py`
- Tiered multiplier: 0-20%→1.0x, 20-40%→0.75x, 40-60%→0.5x, 60-80%→0.25x, 80%+→0.1x
- Wire into `allocate_budgets()` as `build_failure_rates` param
- Query once before the round loop, not per-round
- `pattern_runs` table already has `build_failures` and `variants_generated` columns — no schema changes needed

### Smart round 2 (#5) — key insight: source dedup is 80% of the fix
- The subagent discovered that after `--apply`, round 2 starts from different base source (winner applied), so variant `source_md5` differs from round 1. Most "waste" actually hits **obj dedup** (same .obj from slightly different source), not source dedup.
- Real remaining waste: redundant *compiles* that produce identical objects.
- Three-strategy model: `_determine_round_strategy()` → "normal", "winner_followup", "exploration"
- **winner_followup**: Halve independent budget, boost compose pairs involving winner via `_boost_winner_pairs()`
- **exploration** (≥2 rounds no improvement): Zero budget for all failed patterns, redistribute to non-failed. Chain-only mode.
- New `RoundHints` fields: `no_improvement_rounds`, `failed_pattern_names`, `exclude_variant_names`

### Multi-variant merge (#6) — simpler than expected
- `extract_edit_spans()` uses prefix/suffix matching (O(n), fast) — no need for difflib
- Most patterns make single localized edits → 1 EditSpan per variant
- Overlap check is O(1) for typical single-edit variants
- `find_merge_candidates()`: top-8 improvers, pairwise non-overlapping = up to 28 pairs, capped at 15
- Adds ~15s wall-clock per round (15 extra compiles at 1s each)
- Shared code with evolutionary crossover — implement merge first, reuse in evolutionary

### Genetic/evolutionary (#7) — depends on merge infrastructure
- Core insight: crossover = merge.py's non-overlapping edit combination. Build merge first.
- `extract_edits()` (post-hoc diff) is preferred over modifying all patterns to report edits
- Population lifecycle: EVALUATE → SELECT → CROSSOVER → MUTATE → FILL → ASSEMBLE
- Budget: 300 evals (20 population × 15 generations), ~75s with 6 workers
- **More budget-efficient** than greedy hill climbing (300 vs 1000 evaluations)
- The `Variant.edits` field exists but is **never populated** — needs post-hoc diff to fill

### Score gradient analysis (#8) — low-cost tiebreaker, high-cost feedback loop
- Per-instruction data already available from objdiff `--include-instructions` (used by diagnosis)
- **Cheap win**: Selective re-scoring. Only top-5 candidates (within 1% of best) get `--include-instructions` re-run. ~150-350ms extra per round (<5% overhead)
- `MismatchFingerprint`: frozenset of `MismatchEntry(index, match_type, category, opcodes, detail_hash)`
- Tiebreaker formula: `trajectory_score = (structural_fixed * 3.0 + regswap_fixed * 1.0 + structural_introduced * -2.0 + ...) * 0.05`
- Tier 1 (match%) still dominates; gradient only breaks ties within 0.01%
- **Phased rollout**: Phase 1 (fingerprint infra, no behavior change) → Phase 2 (selective re-scoring) → Phase 3 (tiebreaker) → Phase 4 (category-targeted chains)

### Pattern parameterization (#9) — largest architectural change
- Post-generation merge pass (recommended over modifying Pattern.generate())
- New `EditGrouper` class with `CorrelationIndex` mapping variables → edit sites
- `generate_sites()` optional method on Pattern ABC — only `signed_unsigned`, `comparison_equivalence`, `comparison_flip` need it initially
- Strongest correlation: `signed_unsigned` + `comparison_equivalence` on same variable
- **50-95% fewer variants** for correlated pairs (3 grouped per-variable vs 170 ungrouped)
- Goes between Phase 1 and Phase 2 as "Phase 1.5"
- Budget split: 45% independent / 15% grouped / 15% composed / 25% chains

### Backtracking hill climb (#10) — clean separation from hill_climb()
- New `beam_search_climb()` function (not a refactor of existing)
- Per-beam Scorer context is fine — `ninja -t commands` is <10ms
- Budget split mode: `per_beam_budget = max(20, max_variants // beam_width)`
- Source dedup across beams via MD5 prevents identical beam entries
- Stale beam eviction after `plateau_limit` rounds without improvement
- New stopping criterion: beam convergence (all beams same score within 0.01%)

---

## Phase B: Core improvements — implementation order

Based on subagent research, the recommended order is:

### B1. Multi-variant merge (#6) — implement first
- **Why first**: Shared infrastructure (`EditSpan`, `extract_edit_spans`, `edits_overlap`) is reused by evolutionary (#7) and potentially cross-compose (#4)
- **Files**: New `merge.py` (~150 lines), `hill_climber.py` (~20 lines insert)
- **Effort**: Small-medium. Self-contained new module.

### B2. Cross-variant composition (#4) — highest immediate ROI
- **Why second**: Directly exploits Phase 1 scoring results. The 41% compose win rate from batch testing suggests cross-composition will find even more combinations.
- **Files**: `composer.py` (3 new functions), `hill_climber.py` (~30 lines insert)
- **Effort**: Medium. Architectural decision (Phase 4 after scoring, not inside generate_variants) is locked in.

### B3. Build failure suppression DB (#2 Level 2) — persistent learning
- **Why third**: Amplifies Phase A's per-round suppression with cross-session learning
- **Files**: `pattern_stats.py` (new query), `generator.py` (add param to allocate_budgets)
- **Effort**: Small. Schema already supports it.

### B4. Smart round 2 (#5) — eliminates round 2 waste
- **Why fourth**: Depends on understanding actual round 2 behavior. Source dedup from Phase A already helps.
- **Files**: `types.py` (new RoundHints fields), `generator.py` (strategy selection), `hill_climber.py` (winner info passing)
- **Effort**: Medium. The three-strategy model is well-specified.

## Phase C: Exploratory — pick one

| Feature | Budget | Risk | Payoff |
|---------|--------|------|--------|
| Score gradient (#8) | Low (Phase 1-2 only) | Low | Tiebreaker + better chain targeting |
| Evolutionary (#7) | Medium (~400 lines) | Medium | Escapes local optima |
| Backtracking (#10) | Medium (~150 lines) | Low | Parallel paths, no new data structures |
| Pattern parameterization (#9) | High (~300 lines + per-pattern changes) | Medium | Fewer, higher-quality variants |

**Recommendation**: Start with Score gradient Phase 1-2 (fingerprint infra + selective re-scoring) — it's low-risk, adds data collection that informs all other features, and the tiebreaker gives immediate value.

## Key files reference

| File | Role |
|------|------|
| `scripts/permuter/generator.py` | Variant generation, budget allocation, phase orchestration |
| `scripts/permuter/composer.py` | Compose pairs, chain variants, follow-up map, beam search |
| `scripts/permuter/hill_climber.py` | Round loop, scoring, improvement application |
| `scripts/permuter/types.py` | All data structures (Variant, Diagnosis, RoundHints, ChainSpec) |
| `scripts/permuter/scorer.py` | Build, objdiff, dedup, batch scoring |
| `scripts/permuter/pattern_stats.py` | DB tracking, build failure rates |
| `scripts/permuter/extractor.py` | Function extraction, reparse_variant |
| `scripts/permuter/patterns/base.py` | Pattern ABC (generate, relevant, priority) |
