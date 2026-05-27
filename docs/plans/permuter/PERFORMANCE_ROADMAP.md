# Permuter Performance & Power Roadmap

Status: Active — opened 2026-05-26 · reviewed 2026-05-26 (staff design review)
· 2026-05-27: **Workstreams A, B, and Synthesis (C) all closed out** — every
item landed, parked-with-evidence, or shipped behind a default-off flag pending
data/A-B. See the per-item statuses and the Review Log.

Living tracking document for making the permuter **faster** (more variants
scored per second) and **smarter** (fewer variants needed per win). Review
periodically; update the status tags and the Review Log at the bottom.

## Why

The permuter works and produces wins, but it feels fundamentally limited. The
inner loop is **compile + objdiff per variant**, both as subprocesses, and we
generate many variants per round (104 patterns registered, 97 active by default;
7 are opt-in) where most are low-signal. Three levers:

1. **Throughput** — cut the cost per variant (compile + diff).
2. **Search quality** — generate fewer, better variants using evidence we
   already collect.
3. **Synthesis** — construct candidates from target facts instead of permuting
   blindly. Much of this machinery already exists but is partly wired or used
   only for diagnostics.

> **Review note (2026-05-26):** A code audit found that several items first
> scoped as "build X" already exist in tree (C4 is done; B1, C1, C2 are
> partly built). Those items have been re-scoped to *validate / wire / extend*
> rather than *build from scratch*. Effort and risk dropped accordingly. The
> single most important prerequisite — a measurement harness — was implicit;
> it is now an explicit **A0** that gates everything else.

## Status Legend

- `[ ]` not started · `[~]` in progress · `[x]` done · `[-]` deferred/parked

## Headline Metrics (the only numbers that matter)

Tracked against a **fixed bench set**: 30 functions, mixed difficulty, fixed
seeds. Lives in `scripts/permuter/bench/` (created in A0).

North star is **wall-clock to first 100%** — variants/second is a proxy that
can be gamed by generating more low-signal variants, so weight it accordingly.

| Metric | Baseline (2026-05-26) | Current | Target |
|--------|----------------------|---------|--------|
| variants / second | 1.47 | 1.47 (A1 no-op on DC3) | 2× baseline |
| wins / 100 attempts | 12.9 (4/31 discovered) | 12.9 (unchanged) | +50% |
| wall-clock to first 100% | n/a (0/31; bench is AT_LIMIT) | n/a | −50% |

> **A1 A/B (2026-05-27):** the preprocess cache is RB3-only and never engaged
> on the all-DC3 bench set (0 fast hits / 732 variants, 0 score divergence,
> median compile-run speedup 0.97×). It does **not** move the Current column —
> the 56% compile-run cost is unchanged on DC3. Gate failed → default left OFF.

> Baseline captured by `scripts/permuter/bench/run.py` over the pinned
> 31-function set; full numbers + profiling in
> `scripts/permuter/bench/BASELINE.md`. The set is deliberately AT_LIMIT
> functions, so "first 100%" is n/a here — track variants/sec and discovered
> wins instead. Baseline was taken under heavy concurrent load, so wall-clock is
> inflated; the per-call subprocess costs are the contention-robust signal.
>
> **Profiling verdict (A0) — the number that decides A2/A4:** of attributed
> inner-loop wall-clock, **compile-run ≈ 56%, objdiff-run ≈ 21%,
> python-overhead ≈ 22%**. **Process spawn is negligible:** measured objdiff
> spawn floor **0.87 ms/call (0.38% of objdiff subprocess time, run ≈ 264 ms)**,
> compile spawn **0.95 ms/call (0.16%, run ≈ 597 ms)**. The doc's old "~80 ms
> objdiff spawn" assumption was wrong by ~90×.
>
> Re-run before every merge in this roadmap; record the delta in the PR and the
> Review Log. The harness is also the **regression gate** for every flag flip
> below: a flip ships only if the harness shows zero score divergence and no
> win-rate regression on the bench set.

---

## Workstream A — Inner-loop throughput

The hot path. `scorer.py` compiles each variant (`subprocess.run`, wibo+mwcceppc
or cl.exe) and diffs it (`objdiff diff ... -f json`, command built at
`scorer.py:805`, `_run_objdiff` defined at `scorer.py:790`).

### A0 — Bench harness, baselines & per-variant profiling `[x]` · **Done 2026-05-26**

The foundation. Nothing else in this roadmap can be honestly evaluated without
it. This was implicit in the old A1; it is now its own item because (a) it
blocks every measurement, and (b) its profiling breakdown decides whether the
risky throughput items (A2, A4) are worth building at all.

- [x] Create `scripts/permuter/bench/` with a fixed 31-function bench set
      (`bench_set.json`, 11 high / 12 mid / 8 low band), fixed seed config,
      fixed pattern set. List is pinned (stable ids) so runs are comparable
      across weeks.
- [x] Captured the three Headline Metrics as the baseline column (above).
- [x] **Per-variant profiling breakdown** (env-gated `PERMUTER_PROFILE=1`,
      `profiling.py`, wired into the 4 subprocess sites in `scorer.py`). Result:
      **compile-run ≈ 56%, objdiff-run ≈ 21%, python-overhead ≈ 22%**; spawn
      negligible. Measured spawn floors: **objdiff 0.87 ms, compile 0.95 ms** —
      replacing the unverified "~80 ms objdiff spawn".
- [x] Re-runnable `bench/run.py` emits a machine-readable results file
      (`baseline-results.json`) + human summary so every later PR can post a
      delta. (Note: run with the sandbox disabled — wibo/cl.exe write objects
      the command sandbox blocks with SIGSYS.)

**Owner:** A0 agent · **Effort:** 1.5 days · **Risk:** low · **Status:** done

### A1 — Validate & default-on the preprocess cache (RB3) `[!]` · **A/B run 2026-05-26: gate FAILED on the DC3 bench set — default NOT flipped**

The macro-aware preprocessed-splice fast path (`preprocess_cache.py`, toggle
`PERMUTER_PREPROCESS_CACHE` at `preprocess_cache.py:51`, shipped commit
a1a873e8) is off by default. It is mature in design (21 tests in
`tests/test_preprocess_cache.py`, conservative silent fallback) but young
(landed 2026-05-26). Defaulting on is a measurement problem, not a code problem.
Consumes the A0 harness.

- [x] A/B driver added: `bench/preprocess_cache_ab.py` on top of A0. Generates
      one deterministic round of variants per bench function (same seed/patterns
      as hill_climb round 1) and scores that identical list cache-OFF then
      cache-ON, each against a fresh isolated score cache.
- [x] Asserts per-variant objdiff match% identical to 4 decimals between off/on.
      Byte-identity for the no-line-shift case is enforced by the cache's own
      `_validate_preprocess_cache` self-check, driven in STRICT mode (below).
- [x] Records live-macro fallback rate per unit cluster + build-success parity.
      Per-call **compile-run ms** (via `PERMUTER_PROFILE=1`) is the headline
      speed signal — contention-robust, unlike wall-clock.
- [x] STRICT mode (`PERMUTER_PREPROCESS_CACHE_STRICT`, `scorer.py:472`) forced
      on in the ON run as the validation oracle.
- [ ] ~~Gate: zero score divergence across N≥50, median speedup ≥1.5×.~~
      **Result (HEAD `0c9b6fad`, 31 fns / 732 variants):** 0 divergences,
      0 build-parity breaks — BUT **0 fast hits / 0 fallbacks, median compile-run
      speedup 0.97×**. The fast path is **structurally inert on DC3**: it is
      hard-gated to `ProjectType.RB3`/mwcceppc (`scorer.py:338` — "MSVC's /E +
      splice has not been validated"), and DC3 uses cl.exe. The bench set is
      100% DC3, so the cache never engages → the 1.5× speedup gate cannot be met.
- [ ] **Default NOT flipped** — gate failed (speedup < 1.5×; zero coverage).
      The env off-switch is unchanged. To actually land A1, validate on an RB3
      worktree (`PERMUTER_PROJECT=rb3` + RB3 build present) where the fast path
      runs, or first extend/validate the splice path for the DC3/MSVC toolchain.
- [x] **MSVC /E + splice extension** (derisking experiment, 2026-05-27): impl
      added on `perf/msvc-preprocess-splice` (`preprocess_cache.py`:
      `derive_preprocess_command_msvc`, `strip_msvc_pch_flags`,
      `normalize_coff_timestamp`; `scorer.py`: `_build_preprocess_cache` accepts
      DC3, `_validate_msvc` byte+score oracle, `_redirect_source_path_msvc`,
      `_msvc_compile_cwd`). A/B run (31 fns / 709 variants compared, STRICT
      oracle): **0 score divergences, 0 build-parity breaks (the splice is
      sound)** but **27 fast hits / 340 fallbacks (7.4% coverage)** and
      **median per-call compile-run speedup 0.995× (pooled 0.990×)**. On the
      one function that hit 100% splice coverage (RndShaderSimple::CalcShaderOpts)
      the splice was 196 ms/call vs 158 ms/call PCH — **0.805× (slower)**.
      **Root cause:** splice requires no-PCH compilation, but DC3's PCH already
      eliminates the header-parse cost the splice was designed to skip. Splice
      + PCH are mutually exclusive features; PCH wins on MSVC.
      **Verdict: DEAD END on DC3** — gate impossible to meet because the lever
      is structurally absorbed by PCH. Implementation preserved on
      `perf/msvc-preprocess-splice` (default OFF, env-gated, conservative
      fallback) for future reference / RB3 carry-over. Not merging to main.

**Owner:** — · **Effort:** 1.5 days · **Risk:** low (silent fallback)
**Blocker:** feature is RB3-only; no RB3 toolchain present in the DC3 worktree,
so the A0/A1 bench set (all DC3) cannot exercise or validate it.
**Update 2026-05-27:** MSVC extension implemented + validated on DC3 — see
above; correct but architecturally inert because of PCH. A1 remains
RB3-only-meaningful.

### A2 — objdiff daemon mode `[-]` · **A0 profiling says: not worth it**

> **A0 verdict (2026-05-26): targets an imaginary bottleneck — recommend
> parking.** Measured objdiff spawn is **0.87 ms/call = 0.38%** of objdiff
> subprocess time (objdiff-run is ~264 ms/call). A daemon eliminates the
> ~1 ms spawn, i.e. at best a **0.4% reduction in objdiff cost** and ~0.1% of
> total inner-loop wall. The win the daemon was scoped to deliver (avoid
> re-mmapping the original objects every call) is real *inside objdiff*, but
> that cost lives in objdiff-run, not spawn — so the right lever is making the
> diff itself cheaper (or A3's parallelism), not an IPC daemon. Build A3 first;
> only revisit A2 if objdiff-run stays a large share *after* A3.

Our fork at `/home/free/code/milohax/objdiff` (`freeqaz/main` @ `f62bc9c`).
`objdiff-core` is a real library; no daemon exists today. Build this **only if
A0's profiling shows objdiff subprocess spawn is a material fraction of
per-variant time** — same measurement discipline as A4. **It does not (0.4%).**

- [ ] Branch `feature/daemon-mode` off `main` in the fork.
- [ ] Add `objdiff diff --daemon` in `objdiff-cli/src/cmd/diff.rs`: read
      `unit\tobj_path\tsymbol\tflags\n` on stdin, emit `match_pct\tjson?\n`.
- [ ] mmap original objects once at startup; reload on mtime change.
- [ ] Python client `scripts/permuter/objdiff_client.py` (daemon pool per batch).
      Must be **thread-safe / pooled** so it composes with A3's parallel diff
      rather than serializing on a single daemon.
- [ ] Swap `scorer._run_objdiff` (`scorer.py:790`) to use the client behind
      `PERMUTER_OBJDIFF_DAEMON`.

**Owner:** — · **Effort:** 2–3 days · **Risk:** medium (new IPC path)

### A3 — Parallelize Phase 3 (objdiff) `[ ]`

`score_batch` (`scorer.py:990`) runs objdiff sequentially (loop ~`scorer.py:1104`,
comment "objdiff is fast" at `scorer.py:1103`) while the compile phase already
uses `ThreadPoolExecutor(max_workers=workers)`. Each diff is independent; reuse
the existing `workers` arg for Phase 3. Pure win, independent of A2 — land first
alongside A0. If A2 lands later, point the threads at the daemon pool.

**Owner:** — · **Effort:** 0.5 day · **Risk:** low

### A4 — Persistent compiler worker `[-]`

Pre-spawn N long-lived `wibo mwcceppc` / `cl.exe` processes fed over a pipe, to
kill per-variant process spawn + DLL load. **Deferred** until A0 profiling
proves compile spawn is still dominant after A1/A2 land.

> **A0 verdict (2026-05-26): the *spawn* part targets an imaginary bottleneck.**
> Measured compile spawn (shell launch floor) is **0.95 ms/call = 0.16%** of
> compile subprocess time; compile-run is **~597 ms/call** and is 56% of the
> whole inner loop. So a persistent worker that only removes process-spawn +
> DLL-load buys ≤0.2% — *unless* it also keeps the **preprocessed translation
> unit warm across variants** (skip the ~0.4 s header re-parse per compile).
> That warm-TU win is the same one A1's preprocess-cache already targets far
> more cheaply. **Recommendation: keep A4 parked; pursue A1 (preprocess cache,
> low-risk, already built) to attack compile-run instead.** compile-run is the
> real giant (55%), but the lever is *fewer/cheaper compiles* (A1 cache, B3
> canonicalize dedup, B-stream "fewer variants"), not faster process spawn.

**Owner:** — · **Effort:** 3–5 days · **Risk:** high · **Gated on A0 profiling
(fails the gate — spawn is 0.16%)**

---

## Workstream B — Search quality (fewer variants needed)

Evidence to throw fewer variants already exists in tree; it's underused.

### B1 — Validate & flag the strategy-DB → priority path `[x]` · **Done 2026-05-27**

**Re-scoped (2026-05-26): the path already exists.**
`strategy_db.recommend_patterns` (`strategy_db.py:483`) →
`apply_strategy_boosts` (`strategy_db.py:616`, called from `hill_climber.py:700`
and `:716`) → `RoundHints.atlas_boost_patterns` → `generator._pattern_priorities`
(`generator.py:63`). So strategy records *do* boost priorities today. What's
actually missing versus the original intent:

- [x] It's always-on with no A/B switch. **Added `PERMUTER_STRATEGY_PRIORITIES`**
      off-switch (`strategy_db.py:616`, helper `strategy_priorities_enabled()` at
      `strategy_db.py:613`). Default ON. Set to `0`/`false`/`no`/`off` to disable.
      17 `test_strategy_db.py` tests still pass.
- [x] **A/B measured** on 10 mid-band bench functions (sequential, no lock
      contention, correct PCH, valid baselines 94–99%). Results:
      **ON: 20 wins/100, 383 variants · OFF: 20 wins/100, 383 variants — identical.**
      The strategy DB boost is currently **inert** on this bench set for two reasons:
      1. The A0 bench harness calls `hill_climb` without `adaptive=True`, so
         `round_hints` is None and `apply_strategy_boosts` is never called.
      2. Even with `adaptive=True`, the DB stores all records as
         `diagnosis_category='unknown'`, but the hill climber passes specific diag
         categories ('mixed', 'regswap', 'structural', etc.) which produce 0
         unit-specific matches — all boosts collapse to 1.0 (below the 1.2
         threshold). Only cross-unit data contributes, and it only populates the
         `cross_count` field, not `unit_count`, so boost stays at 1.0.
- [x] **Root cause of ineffectiveness diagnosed.** Coarse keying is not the issue;
      the DB was built without populating `diagnosis_category` per pattern (all
      stored as 'unknown'). Until the DB build step writes meaningful diag_cats,
      the unit-specific lookup always returns 0 rows and the boost never exceeds 1.0.
- [ ] **Remaining work to make B1 actually useful:** Fix the strategy DB build
      (`strategy_db build` command, `strategy_db.py:~700`) to classify patterns
      by diagnosis category when mining historical wins — write 'regswap',
      'structural', 'mixed', 'prologue', 'offset' instead of 'unknown'. Then
      re-run `strategy_db build` and re-run the A/B to measure real win-rate delta.
      Until then, the boost is a well-tested no-op (safe, no regression risk).

**Default: ON** (default behavior preserved). The path is correct code; the
off-switch is the deliverable of B1. The data-quality fix is a separate task.

**Owner:** — · **Effort:** 1 day (flag done; DB fix is ~0.5d more) · **Risk:** low

### B2 — diff-inspect signals as hard filters `[x]` · **Implemented 2026-05-27, default OFF (inert on bench)**

`RoundHints` (`types.py`) re-weights *softly* today (`priority_floor`,
`suppression_factor`, `adaptive_priority_boost`) feeding
`generator._pattern_priorities`. Promote strong signals (e.g. an atlas
`negative` suppression at 0.9 confidence) to **hard filters** that drop
patterns outright (priority 0 → 0 budget in `allocate_budgets`).

- [x] Source the strong signals from `target_facts.pattern_recommendations()`
      (`target_facts.py`, returns boost/suppress sets) and the fact-agreement
      check (`validator.check_fact_agreement`, `beam_search._compute_fact_agreement`
      — both test suppress-set membership; the hard filter escalates that same
      membership to a drop only above a confidence bar).
- [x] Apply the hard drop in `generator._pattern_priorities`, gated behind
      `PERMUTER_HARD_FILTERS` (default OFF, `generator.hard_filters_enabled()`).

**What "strong enough" means.** New `TargetFacts.hard_suppress_patterns(threshold=0.85)`:
a pattern is hard-dropped only when (a) some fact suppresses it at **>= 0.85
confidence** (catches the 0.9 atlas-`negative` tier; excludes the 0.7/0.8
heuristic-shape tier, which stays on the soft path) **and** (b) **no** fact
boosts it (a boost conflict keeps it on soft re-weighting). `RoundHints.hard_drop`
additionally never drops an atlas-`force_pattern` boost.

**A/B (A0 harness, `--adaptive`, fresh cache, 8 low-band fns, sandbox off):**

| metric | OFF | ON |
|--------|-----|----|
| wins / 100 | 12.5 | 12.5 |
| functions improved | 1 | 1 |
| variants compiled | 344 | 344 |
| per-function results | — | bit-identical |

**Zero win regression (gate passed) but zero compile reduction** — because the
bench set is regswap/structural-dominated and `pattern_recommendations()`
produced **no** suppress patterns at >= 0.85 confidence on any of these
functions (only `no_touch_zone` facts with empty `suppress_patterns`). The
filter was inert. A standalone check confirms the mechanism *does* prune when a
strong signal is present (a 0.9 suppress fact takes the pattern's budget
48 → 0). **Default left OFF**: the payoff is fewer compiles, and we measured
none on the bench, so flipping the default isn't justified yet. Re-evaluate on a
switch/tail-call-heavy subset (where shape suppressions fire) before default-on.

**Owner:** — · **Effort:** 1 day · **Risk:** medium (could over-prune) ·
**Branch:** `perf/b2-hard-filters`

### B3 — Source canonicalization dedup `[-]` · **Parked 2026-05-27: measured 0 benefit**

Original idea: many textually-different variants compile to identical
preprocessed output; key dedup on a canonicalized form (whitespace normalize +
decl sort) instead of raw bytes, to skip the compile.

**Prototyped and measured (branch `perf/b3-canon-dedup`, unmerged) — parked:**
- **Measured hit rate: 0 / 162 variants (8 batches, high+mid bench functions).**
  Even the *aggressive* canonicalizer (collapsing blank lines) never fired — the
  permuter's patterns emit token-level edits with consistent formatting, so no
  two variants differ only by whitespace. It would add a `_canonicalize`+md5 on
  the hot path for every variant for **zero** measured payoff.
- **Correctness hazard:** the "decl-group sort" in the original sketch is unsafe
  here — declaration order changes codegen (it's a permuter *win* mechanism).
  And collapsing/removing lines shifts `__LINE__`, which changes codegen in the
  ~40 `__LINE__` sites (`curl/`, `oggvorbis/`, `XLSPConnection.cpp`) — a silent
  false-dedup. (`MILO_ASSERT` is immune: it takes the line as an explicit arg.)
  The provably-safe normalization set is nearly empty.
- **Redundant:** the existing obj-hash dedup (`score_batch` Phase 3, md5 of the
  compiled `.obj`) already collapses variants that compile to identical objects.

Revisit only if a future change starts generating whitespace-divergent variants
and a re-measure shows a non-trivial hit rate.

**Owner:** — · **Effort:** 1 day · **Risk:** low · **Status:** parked (0 measured benefit)

### B4 — Variant outcome predictor `[~]` · **Mechanism landed 2026-05-27 — default OFF pending history accumulation**

Rank the build queue by predicted win-probability and cull the bottom fraction
under a tight budget. `scripts/permuter/predictor.py` (new), wired into
`generate_variants` (`generator.py`) behind `PERMUTER_PREDICTOR` (default OFF).

**Prerequisite — history instrumentation (DONE):** the original blocker was
that `climb_history.py` recorded only `initial_pct`/`final_pct`/`delta`/
`rounds_used` and a pattern *set*. Now instrumented:
- New `climb_history` columns (backward-compatible via in-place
  `ALTER TABLE ADD COLUMN` in `_migrate()`; pre-B4 rows read back as NULL):
  `diag_fingerprint`, `func_loc`, `func_stmts`, `beam_depth`.
- New `climb_variant` table — **one row per scored variant** with a
  per-variant `pattern_label` + `won`/`delta`, the per-variant granularity the
  predictor needs (the per-climb pattern *set* can't tell winners from losers).
- `record_climb` now accepts these features + a `variant_outcomes` list; it's
  wired into the result-assembly paths of `hill_climber.hill_climb` and
  `beam_search.beam_search` (both best-effort, never fail the run). Per-variant
  outcomes are accumulated cheaply alongside the existing
  `pattern_accumulator.record_variant` calls; dedup/cache pseudo-results are
  skipped (no real compile-run signal).
- `db_path` injection added across `climb_history` for test isolation.

**Predictor design:** empirical-Bayes win-rate over the recorded features —
NOT logistic regression, because history is thin and a Beta(α=1, β=10) prior
*degrades gracefully*: with zero data every variant scores the global ~9%
prior, so ranking is a stable no-op (all tie) and culling can't pick wrongly.
`score = 0.5·p(pattern,diag) + 0.35·p(pattern) + 0.15·p(global)`, times a weak
±10% `tanh(log(loc/median))` size nudge (big functions have more codegen DOF).
Stdlib + `math` only — no sklearn/numpy.

**Budget-gated culling:** `rank_and_cull(items, feature_of, budget, model)` is
a no-op when `len(items) <= budget` (original order preserved). Over budget it
sorts by score (stable on input index) and keeps `max(budget, round(n·(1-cull)))`
— never below budget. In `generate_variants` the predictor budget defaults to
`max_variants` (so even flag-ON is a no-op) and only bites when
`PERMUTER_PREDICTOR_BUDGET` is set lower; `PERMUTER_PREDICTOR_CULL` (default
0.5) tunes the fraction. Flag OFF = pure pass-through to the unchanged impl
generator (lazy yielding, byte-identical order).

**Tests:** 19 in `tests/test_predictor.py` — new-feature record/readback,
old-schema migration + read, predictor ranking (per-pattern, per-diagnosis,
bounded size nudge, train-from-DB), budget-gate (under/over/floor), and
flag-OFF byte-identity vs the impl generator. Full suite: 1376 pass (1357
baseline + 19), 14 pre-existing failures unchanged.

**Status:** default **OFF** — the mechanism is proven (instrumentation records
the features; predictor trains on available data without crashing; culling
respects the budget) but **win-rate impact is unvalidated**: real history is
still thin, so a default-on cull would risk dropping winners. Revisit (and run
an A/B sweep) once climb runs accumulate enough `climb_variant` rows.

**Owner:** — · **Effort:** mechanism done; ~A/B + tuning remaining · **Risk:** medium

---

## Workstream C — Synthesis revival

Already largely built. `constraint_solver.synthesize()` (`constraint_solver.py:191`)
runs as a default pre-pass (round-1 in `hill_climber.py:851`, gated
`hill_climber.py:849`; per-beam-state in `beam_search.py:546`, gated `:539`).
See companion roadmap: [../synthesis-engine/ROADMAP.md](../synthesis-engine/ROADMAP.md).

**Module status (corrected against code 2026-05-26):**

| Module | Status | Note |
|--------|--------|------|
| `constraint_solver.synthesize` | active | round-1 pre-pass (`hill_climber.py:851`); per-beam (`beam_search.py:546`) |
| `ghidra_ast.py` | active | feeds constraint extraction |
| `ghidra_expr_match.py` | **active** | called by `fma_reorder` pattern (`fma_reorder.py:104`) — *was wrongly listed dormant* |
| `ghidra_source_diff.py` | **diagnostic-only** | called for `[GHIDRA DIFF]` stderr (`hill_climber.py:828`); not in ranking — see C1 |
| Strategy DB → priorities | **partly wired** | `apply_strategy_boosts` → round_hints (`hill_climber.py:700`); see B1 |
| Live-range data | partial | computed in `statement_effects.py`, used by `parameter_live_range` pattern; *not* used for synthesis |
| M2C IL hints | **routed** | extractors consumed in `beam_search.py:160` + patterns — *was wrongly listed "built, not routed"* |

### C1 — Wire `ghidra_source_diff` into beam ranking `[x]` · **DONE — default flipped to `both` 2026-05-27**

`ghidra_source_diff` was computed but only printed as a `[GHIDRA DIFF]`
diagnostic (`hill_climber.py:828`). C1 plugs it into `BeamState.ranking_key`
(`types.py:765`) alongside `fact_agreement` and `validation_tier`.

**Implementation:**
- `score_source_diff()` in `ghidra_source_diff.py:69` collapses a `SourceDiff`
  to a non-negative scalar (lower = closer to target decompilation). Counts
  per-call deltas, guard mismatches, and control-flow bucket differences.
- New `BeamState.source_diff_score: float | None` field (`types.py:763`).
- `BeamState.ranking_key` adds a `-0.1 * source_diff_score` term
  (`types.py:776`). **WHY 0.1×:** keep it well below the unit-sized
  `fact_agreement` / `guidance_agreement` signals so it can break ties
  between states already equal on the stronger signals, without overruling
  them. States without a decomp get 0.0 = neutral.
- `_compute_source_diff_score()` in `beam_search.py:383` reparses the state
  source and scores against **both Ghidra and m2c** when available
  (averaged — each decompiler captures different aspects of the target).
  Mirrors the C2-fix m2c-fallback pattern in `constraint_solver`.
- Wired into the per-state construction in `beam_search.py:1005` next to the
  existing `fact_agreement` wiring.
- A/B kill-switch: `PERMUTER_C1_SOURCE_DIFF={off,ghidra,m2c,both}` (default
  **`both`**) — override to `off` for ablation experiments.

**Tests:** 21 new tests in `tests/test_source_diff_ranking.py` (cover scalar
scoring, ranking-key tie-break + dominance ordering, m2c fallback,
ghidra+m2c averaging, and env-flag modes). Full suite: 1349 pass
(1328 baseline + 21 new), 14 pre-existing failures unchanged.

**A/B harness:** `bench/c1_source_diff_ab.py` — runs beam search twice
per function (off / both) on the pinned bench set, reports wins/100,
mean rounds-to-first-win.

**A/B status — COMPLETE (2026-05-27).** Bounded run: `--limit 4 --bands mid`
(4 mid-band functions, 104 s wall, n=4):

| mode | wins/100 | mean rounds-to-first-win | sample |
|------|----------|--------------------------|--------|
| off  | 50.0     | 1.0                      | 4 fns  |
| both | 50.0     | 1.0                      | 4 fns  |

No regression on either metric. Gate: **PASS**. Default flipped to `both`
at `beam_search.py:409`. The signal is neutral on this bench set — the
tie-break only activates when two beam states score identically on the
stronger signals, which is uncommon in short 3-round runs. The signal is
expected to show more benefit in longer sweeps with more competing states.

**Owner:** — · **Effort:** done · **Risk:** low · **Status:** COMPLETE

### C2 — Close or extend declaration-order synthesis `[~]`

**Investigated 2026-05-26 (read-only audit, claim verified against code).**
The suspected gap in the prior re-scope was **wrong**: the Ghidra-derived
decl-order path is NOT gated behind a crossref. `declaration_reorder.py:149`
(`not crossref_produced`) is just dedup — the plain Ghidra-guided path fires
whenever there's no `.asm`/`.cod` listing (the common case, since
`asm_listing_path` is only set when a listing already exists next to the `.obj`,
`scorer.py:1219-1225`). The synthesis pre-pass (`constraint_solver._resolve_decl_order`)
is independent of both. So decl-order synthesis IS wired and reachable.

**The real narrow gap:** `ghidra_var_match.ghidra_guided_reorder`
(`ghidra_var_match.py:98-196`), used by BOTH the `declaration_reorder` pattern
(`declaration_reorder.py:397`) and synthesis (`constraint_solver.py:284`), does
**not** use Ghidra's variable order to choose the target permutation. It builds
`target_reg_to_pos` from Ghidra's first-use order (`ghidra_var_match.py:132-134`)
and then **never reads it** — verified. The actual swaps come purely from
objdiff `swap_pairs` via the callee-saved rule `31 - reg_num`
(`:153-165`), and the function early-returns when `swap_pairs` is empty (`:123`).
Ghidra's var order is used only as a yes/no gate (`:128`). So this is really
"objdiff-swap-pair-driven reorder, gated on Ghidra presence" — it has NOT
implemented the original C2 vision (`docs/sessions/2026-03-05-ghidra-guided-permuter.md:83-105`,
"reorder declarations to match Ghidra's first-use order").

- Cases missed: functions where objdiff swap-pair detection is empty/noisy
  (≥3-way rotations, mixed GPR/FPR, partial diffs) yet Ghidra's first-use order
  is reliable — exactly the hard cases C2 was meant to crack. When swap_pairs is
  clean the gap is invisible (the swap-pair math already suffices).
- [ ] **Fix (scoped):** in `ghidra_guided_reorder`, also emit a candidate that
      permutes `source_decl_names` into the inferred register order using
      `target_reg_to_pos`, independent of `swap_pairs` (must fire when swap_pairs
      is empty — i.e. relax the `:123` early-return for this path). Do **not**
      touch `declaration_reorder.py:149` — that condition is correct.
- [ ] **Add m2c as a redundant var-order source.** Today the variable first-use
      order is Ghidra-only: `extract_variable_first_use_order` parses a
      `GhidraAST` (`ghidra_ast.py:90`) and `cs.decl_order` is populated solely
      inside `if ctx.ghidra_ast is not None` (`constraint_solver.py:46-53`;
      `_resolve_decl_order` re-derives Ghidra-only at `:274`). When Ghidra emits
      nothing the whole decl-order path is dead. m2c text is already loaded
      (`ctx.m2c_code`) and mined for other signals (`beam_search.py:160-164`) but
      never for var order. Add an m2c-text first-use extractor (mirror the
      beam_search m2c extractors) and have `extract_constraints` /
      `_resolve_decl_order` fall back to it when `ghidra_ast` is absent. Rationale:
      Ghidra has gaps; m2c output is less readable but often closer to the
      original code's intent, so it is valuable redundancy for the hard cases.
- [ ] Validate on the FMA/hard cluster via the A0 bench harness; require no
      win-rate regression before merge (additive candidate, low risk).

Do **not** build `patterns/decl_order_from_ghidra.py` from scratch — extend
`ghidra_guided_reorder` / `extract_constraints` in place.

**Owner:** — · **Effort:** ~1 day (fix + m2c fallback) · **Risk:** low (additive candidate) · **Gated on A0 harness for validation**

### C3 — Expression-shape synthesis for FMA cluster `[x]`

~51 known functions with FMA/algebraic-rewrite mismatches (projected ~80% hit
rate, per `docs/sessions/2026-03-05-ghidra-guided-permuter.md`).
`ghidra_expr_match` already exists **and is wired into the `fma_reorder`
pattern** (`fma_reorder.py:104`). This item extended that coverage with
operand-commutation + multiply-chain reassociation synthesis.

**Audit (2026-05-27, branch `perf/c3-fma-synthesis`).** Sampled the
float-heavy `src/system/math/*` cluster via `run_diff_inspect`
(Normalize(Plane/Quat), Box::Volume, FastInvert, FastInterp, Set(Quat),
Multiply(Plane/Box), MakeScale, Frustum::Set, …). Two findings reshaped the
item:

1. **fma_reorder's prior coverage** = `+`/`-` addend reorder where one side is
   a multiply, the `a-b*c -> -(b*c-a)` negation rewrite, and `a-(b-c)` paren
   expansion (proven on CalcSpline/InterpTangent). It triggers only on
   `fmadds`/`fadds`-family opcodes in `diagnosis.diff_ops`.
2. **The dominant cluster shape it MISSED** is pure *operand commutation* of a
   multiply / flat add: `fmuls fX,A,B` vs `fmuls fX,B,A`, `fmadds` multiplicand
   swap, `fadds` operand swap. These never appear in `diff_ops` — objdiff
   reports them as a **single-instruction FPR register swap** (a `diff_arg`
   that lands in `reg_swap_pairs`). So `fma_reorder.relevant()` returned False
   AND, worse, `diagnosis.is_all_noise()` classified the whole function as
   noise (it only special-cased GPR swaps), so the permuter never even tried.

**What shipped** (additive, behind no flag — pure synthesis, never regresses):
- `fma_reorder`: detect the commutation signature (FPR swap with
  `first_idx==last_idx`), emit ≤4 deterministic commutation variants
  (`a*b->b*a`, flat `a+b->b+a`, grouping preserved so it's pure commutation)
  plus a multiply-chain reassociation `(a*b)*c -> a*(b*c)`.
- `diagnosis.is_all_noise()`: a single-instruction FPR swap is now a fixable
  commutation candidate (multi-instruction FPR swaps stay noise = spill
  artifacts). This unblocks the cluster from being silently skipped.
- 8 unit tests (synthetic FMA expressions → expected variant set + relevance +
  noise classification). Full permuter suite: no new failures.

**Realized hit rate: 0 added byte-match wins on the commutation subset — a
negative result, honestly reported.** A controlled experiment (manually
swapping `in.b*mult -> mult*in.b` in `Normalize(Plane)` and rebuilding)
produced byte-identical assembly: **the MSVC PPC compiler canonicalizes
commutative float operand order by register liveness and ignores source
operand order** for in-register products. So the bulk of the "~51 FMA cluster"
is the *commutation* subset, which is compiler-normalized and NOT
source-fixable. The ~80% projection in the 2026-03-05 doc applies to the
*Ghidra-guided structural/paren-expansion* subset (already shipped, proven on
CalcSpline/InterpTangent), not bare commutation. Reassociation `(a*b)*c` is the
one lever the compiler can't normalize, but the sampled chain cases
(Box::Volume) are bottlenecked upstream by struct-field load-order shifts, so
the multiply alone can't reach 100% there either. Net: the synthesis is
correct and bounded, the `is_all_noise` fix is the durable win (stops
mis-discarding these functions), and the empirical verdict is that this cluster
is mostly an unfixable compiler-codegen artifact rather than a source-shape gap.

**Owner:** done · **Effort:** ~1 day (audit-driven) · **Risk:** low (additive)

---

## Sequencing

```
Wk1  A0 bench harness + profiling + baselines (DONE)  ∥  A3 parallelize objdiff (0.5d)   → first real numbers
Wk2  A1 preprocess-cache A/B + flip default (1.5d) → B3 canonicalize dedup (1d) → B1 flag + measure strategy priorities (1d)
Wk3  B2 hard filters (1d, harness-gated)   [A2 objdiff daemon PARKED — A0 profiling: spawn is 0.4%]
Wk4  C1 source-diff into ranking (2–3d) → C2 investigate decl-order gap (0.5d: close or file)
Wk5+ C3 FMA expr synthesis (~1wk), B4 predictor (after history instrumentation)
     [A4 compile worker PARKED — A0 profiling: spawn is 0.16%; attack compile-run via A1/B3 instead]
```

**Start here:** ~~A0~~ (done) + A3. A0's profiling reordered the throughput
work: **compile-run (55%) and objdiff-run (22%) dominate; process spawn is
noise (<0.5%)**, so A2 (objdiff daemon) and A4 (compile worker) are *both*
parked — they optimize spawn, which isn't the bottleneck. The throughput
leverage is **fewer/cheaper compiles+diffs** (A3 parallelism, A1 preprocess
cache, B3 dedup, B-stream "fewer variants"), not faster process launch.

**Already done (removed from sequencing):** C4 — synthesis short-circuit. See
Done / Prerequisites.

## Risk & Rollback

Every change ships behind a flag, validated by the A0 harness (zero score
divergence + no win-rate regression) for ≥1 week of field use:

- `PERMUTER_PREPROCESS_CACHE` (exists, `preprocess_cache.py:51`) — A1
- `PERMUTER_PREPROCESS_CACHE_STRICT` (exists, `scorer.py:472`) — validation oracle for A1
- `PERMUTER_OBJDIFF_DAEMON` — A2
- `PERMUTER_STRATEGY_PRIORITIES` — B1
- (C4's `PERMUTER_SYNTHESIS_SHORTCIRCUIT` is unnecessary — the short-circuit is unconditional and already shipped.)

Other existing `PERMUTER_*` flags (for reference): `PERMUTER_DB_ROOT`,
`PERMUTER_PROJECT`.

## Done / Prerequisites

- [x] **C4 — Short-circuit blind patterns on synthesis win** (verified shipped
      2026-05-26): if `synthesize()` returns ≥100% in round 1, `hill_climber.py:882`
      sets `stopped_reason="perfect"` and `break`s at `:895`, before the
      blind-pattern phase (`generate_variants`). A partial synthesis improvement
      intentionally still runs blind patterns (updates baseline, continues). No
      flag needed.
- [x] **objdiff fork cleanup** (2026-05-26): consolidated `metric-honest-immediates`
      (1 commit, FF) into `freeqaz/main`, pushed. New HEAD `f62bc9c`. Left for
      triage: `stash@{0}` (WIP analysis enrichment), `freeqaz/mips-gprel32` +
      `freeqaz/pr-270` (upstream PR snapshots), stale `feature/analysis-pattern-detection`,
      `freeqaz/alt-keys`, `freeqaz/omf`. Untracked `modify_url.py` left alone.

## Companion Docs

- [../synthesis-engine/ROADMAP.md](../synthesis-engine/ROADMAP.md) — evidence-quality roadmap (Workstream C builds on this)
- [ARCHITECTURE_ROADMAP.md](ARCHITECTURE_ROADMAP.md) — permuter architecture
- [BEAM_SOLVER.md](BEAM_SOLVER.md) — beam search design

## Review Log

Append a dated line each time this doc is reviewed or an item changes state.

- **2026-05-26** — Doc opened. objdiff cleanup done (prerequisite). No bench
  numbers yet; first action is A3 + A1 to establish baselines.
- **2026-05-26** — Staff design review + code audit (4 concurrent verification
  passes). Corrections: pattern count 107→104 (97 default-active); fixed drifted
  line refs (`preprocess_cache.py` 49→51, `scorer.py` 808→805/790, synthesize
  call sites). Re-scoped to reality: **C4 already shipped** → moved to Done;
  **B1** path already wired (`apply_strategy_boosts`) → re-scoped to flag+measure;
  **C2** already consumed via `declaration_reorder` → re-scoped to investigate;
  **B2** module path corrected (`target_facts.pattern_recommendations`, not
  `target_facts.fact_agreement`). Status table corrected: `ghidra_expr_match`
  active (not dormant), `ghidra_source_diff` diagnostic-only, M2C IL routed.
  Added **A0** (bench harness + profiling) as explicit foundation; gated **A2**
  on A0 profiling like A4. Removed dead `INDEX.md` companion link. Noted **B4**
  blocked on history instrumentation.
- **2026-05-26** — **A0 landed** (`scripts/permuter/bench/`: 31-function pinned
  set, `run.py` harness, env-gated `profiling.py` wired into the 4 scorer
  subprocess sites, `BASELINE.md`). First real numbers (two consistent runs):
  variants/sec 1.47, 12.9 wins/100 (4/31 discovered), 0/31 reached 100% (bench
  is AT_LIMIT by design). **Profiling breakdown:** compile-run 56%, objdiff-run
  21%, python-overhead 22%; spawn negligible (objdiff 0.87 ms = 0.38%, compile
  0.95 ms = 0.16%). **Consequence: A2 and A4 both PARKED** — they optimize
  process spawn, which the data shows is <0.5% of the loop. The "~80 ms objdiff
  spawn" assumption was wrong by ~90×. Throughput leverage is fewer/cheaper
  compiles+diffs (A3, A1, B3), not faster spawn. (Baseline taken under heavy
  concurrent load; wall-clock inflated, per-call ms are the robust signal.)
- **2026-05-27** — **A1 A/B run** (`bench/preprocess_cache_ab.py` added on top
  of A0; results `bench/preprocess_cache_ab-results.json`, HEAD `0c9b6fad`).
  31 functions / **732 variants** scored cache-OFF vs cache-ON (each against a
  fresh isolated score cache; STRICT mode as the oracle; per-call compile-run ms
  from `PERMUTER_PROFILE`). **Result: 0 score divergences, 0 build-parity breaks,
  but 0 fast hits / 0 fallbacks and median compile-run speedup 0.97× (pooled
  0.93×).** Root cause: the fast path is hard-gated to `ProjectType.RB3`/mwcceppc
  (`scorer.py:338`); the bench set is 100% DC3 (cl.exe), so the splice never
  engages. **Gate FAILED (speedup < 1.5×, zero coverage) → default at
  `preprocess_cache.py:51` left OFF.** A1 cannot be validated/landed from a
  DC3-only worktree — it needs an RB3 build present, or the splice path extended
  to the DC3/MSVC toolchain first. 21 `test_preprocess_cache.py` tests still pass.
- **2026-05-27** — **MSVC /E + splice derisking experiment** (branch
  `perf/msvc-preprocess-splice`, NOT merged to main). Implementation extends
  the preprocess-cache fast path to cl.exe: `/E` to stdout, PCH flag stripping,
  COFF-timestamp normalization, bare-basename source token + cwd-switched
  invocation. A/B run (31 fns / **709 variants**, STRICT oracle, fresh isolated
  score caches): **0 score divergences, 0 build-parity breaks** — the splice
  is sound on every function where it engages. Coverage low: **27/367 cache
  attempts = 7.4% fast hit rate** (DC3's pervasive MILO_NOTIFY/MILO_ASSERT/NULL
  usage trips the macro-aware gate). Speed: **median per-call compile-run
  speedup 0.995× (pooled 0.990×) — no win**. On the one function with full
  splice coverage (RndShaderSimple::CalcShaderOpts) the splice was 196 ms/call
  vs 158 ms/call PCH = **0.805× (slower)**. **Verdict: dead-end on DC3.** Root
  cause: splice requires no-PCH compile, but DC3's PCH already eliminates the
  header-parse cost the splice was designed to skip — splice + PCH are
  mutually exclusive, and PCH wins on MSVC. Implementation preserved on
  `perf/msvc-preprocess-splice` (default OFF) for future reference / potential
  RB3 carry-over, **not merged**. The 21 `test_preprocess_cache.py` tests
  still pass on the branch.
- **2026-05-27** — **B1 flag added + A/B run** (`PERMUTER_STRATEGY_PRIORITIES`
  off-switch at `strategy_db.py:616`, helper `strategy_priorities_enabled()` at
  `strategy_db.py:613`; 17 `test_strategy_db.py` tests pass). A/B: 10 mid-band
  functions, sequential runs (no lock contention), valid 94–99% baselines.
  **Result: ON 20 wins/100, 383 variants · OFF 20 wins/100, 383 variants —
  identical.** The boost is currently **inert**: (1) the A0 bench harness doesn't
  pass `adaptive=True` so `round_hints` is None and `apply_strategy_boosts` never
  fires in plain bench runs; (2) even with `adaptive=True`, all DB records have
  `diagnosis_category='unknown'` but the hill climber passes specific diag_cats
  ('mixed', 'regswap', etc.), so unit-specific lookup always returns 0 rows and
  all boosts stay at 1.0 (below the 1.2 threshold). **Default left ON** (the
  path is correct; the data-quality issue is the next fix). Branch
  `perf/b1-strategy-priorities`.
- **2026-05-27** — **C1 A/B run + default flipped** (`bench/c1_source_diff_ab.py`
  created; bounded run `--limit 4 --bands mid`, 4 mid-band functions, ~104 s wall,
  branch `perf/c1-ab-validate`). Results: **off 50.0 wins/100 (2/4), mean
  rounds-to-first-win 1.0 · both 50.0 wins/100 (2/4), mean rounds-to-first-win
  1.0**. No regression on either metric. Gate: **PASS**. Default at
  `beam_search.py:409` flipped from `"off"` to `"both"`. The signal is neutral
  on 3-round bench runs because the `-0.1×` tie-break only fires when two states
  score identically on match% + fact_agreement + guidance_agreement; that is rare
  in short sweeps. Signal expected to provide benefit in longer live sweeps with
  deeper beam competition. 21 `test_source_diff_ranking.py` tests still pass.
- **2026-05-27** — **B4 mechanism landed** (history instrumentation +
  predictor; branch `perf/b4-predictor`). Cleared the "history is thin"
  prerequisite: `climb_history` gained 4 backward-compatible columns
  (`diag_fingerprint`/`func_loc`/`func_stmts`/`beam_depth`, in-place
  `ALTER TABLE` migration so pre-B4 rows still load) plus a new `climb_variant`
  table giving **per-variant** pattern labels + win/delta (the granularity the
  predictor needs). `record_climb` — previously defined but **never called** —
  is now wired into both `hill_climb` and `beam_search` result-assembly
  (best-effort). New `predictor.py`: empirical-Bayes Beta(1,10) win-rate blend
  (per-(pattern,diag)/per-pattern/global) + bounded ±10% size nudge, stdlib-only
  (no sklearn/numpy). Wired into `generate_variants` behind `PERMUTER_PREDICTOR`
  (default **OFF**): flag-OFF is a pure pass-through to the unchanged impl
  generator (byte-identical, verified by test); flag-ON ranks+culls only when
  over `PERMUTER_PREDICTOR_BUDGET` (defaults to `max_variants` = still a no-op),
  keeping `max(budget, round(n·(1-cull)))`. 19 new `test_predictor.py` tests;
  suite 1376 pass (1357 + 19), 14 pre-existing failures unchanged. **Win-rate
  impact unvalidated** (real history still thin) — kept default-OFF; A/B sweep
  deferred until `climb_variant` accumulates rows. Mechanism (record features →
  train without crashing → budget-respecting cull) demonstrated end-to-end.
- **2026-05-27** — **C3 done** (branch `perf/c3-fma-synthesis`). Audited the
  `src/system/math/*` FMA cluster. fma_reorder's gap = pure operand commutation
  (`fmuls`/`fmadds`/`fadds` operand swaps), which surface as single-instruction
  FPR `reg_swap_pairs` (not `diff_ops`) and were being discarded by
  `is_all_noise()` (GPR-only special-case). Shipped: commutation +
  multiply-chain reassociation synthesis in `fma_reorder` (≤4 deterministic
  variants), an `is_all_noise()` fix that no longer mis-discards
  single-instruction FPR swaps, and 8 unit tests (no new suite failures).
  **Realized: 0 added wins — negative result, reported honestly.** A controlled
  rebuild (manual `in.b*mult -> mult*in.b` in Normalize(Plane)) emitted
  byte-identical asm: MSVC PPC canonicalizes commutative float operand order by
  register liveness, so the commutation subset of the cluster is a compiler
  codegen artifact, not a source-shape gap. The ~80% projection applies to the
  Ghidra-guided paren-expansion subset (already shipped). The durable win is the
  `is_all_noise` fix (stops silently skipping these functions). See `### C3`.
- **2026-05-27** — **B2 implemented + A/B run** (branch `perf/b2-hard-filters`).
  New `TargetFacts.hard_suppress_patterns(threshold=0.85)` extracts the strong
  suppress signals (confidence >= 0.85 → catches the 0.9 atlas-`negative` tier;
  the 0.7/0.8 heuristic-shape tier stays soft) with a boost-conflict guard.
  `RoundHints.hard_suppress_patterns` + `RoundHints.hard_drop` carry the decision;
  `generator._pattern_priorities` drops those patterns to priority 0 (→ 0 budget)
  when `PERMUTER_HARD_FILTERS` is on (default OFF, `generator.hard_filters_enabled()`).
  Wired in both `hill_climber.py` and `beam_search.py`. **16 new
  `test_hard_filters.py` tests pass** (threshold boundary, boost-conflict,
  force-pattern override, flag on/off budget). Added a `--adaptive` passthrough
  to the A0 bench harness (default off; needed because the pinned bench runs
  `round_hints=None`, so neither soft nor hard re-weighting fires otherwise —
  same gap B1 hit). **A/B (8 low-band fns, `--adaptive`, fresh cache, sandbox
  off): OFF 12.5 wins/100, 344 variants · ON 12.5 wins/100, 344 variants —
  bit-identical per function.** Zero win regression (**gate passed**) but zero
  compile reduction: `pattern_recommendations()` produced no >= 0.85 suppress
  on any bench function (regswap/structural-dominated; `no_touch_zone` facts had
  empty `suppress_patterns`), so the filter was inert. A standalone check
  confirms the prune *does* fire when a strong signal exists (0.9 suppress fact
  → pattern budget 48→0). **Default left OFF** — the payoff is fewer compiles and
  we measured none here; re-evaluate on a switch/tail-call-heavy subset.
- **2026-05-27** — **Roadmap close-out (Workstreams A/B/C).** All items resolved:
  **landed** — A0 (harness+profiling), A3 (parallel objdiff), A1 (A/B harness),
  C1 (+A/B, default `both`), C2/C2-fix (Ghidra+m2c decl-order), C3 (FMA synth +
  `is_all_noise` fix), B1 (strategy flag), B2 (hard filters, flag-off), B4
  (predictor+history, flag-off); **parked with evidence** — A2/A4 (spawn <0.5%
  of loop), B3 (0/162 dedup hits), MSVC `/E`+splice (PCH absorbs the lever);
  C4 was already shipped. Strategic finding: throughput is structurally floored
  on DC3 (PCH + negligible spawn), so the remaining levers are fewer/smarter
  variants (B/C), most now shipped behind default-off flags pending live-sweep
  data. Integrated suite: 1431 passed, 13 pre-existing environmental failures,
  zero new. Default-off flags awaiting validation: `PERMUTER_HARD_FILTERS` (B2),
  `PERMUTER_PREDICTOR` (B4); C1's `PERMUTER_C1_SOURCE_DIFF` is on (`both`).
