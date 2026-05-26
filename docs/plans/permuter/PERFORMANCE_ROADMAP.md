# Permuter Performance & Power Roadmap

Status: Active — opened 2026-05-26 · reviewed 2026-05-26 (staff design review)

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
| variants / second | 1.47 | — | 2× baseline |
| wins / 100 attempts | 12.9 (4/31 discovered) | — | +50% |
| wall-clock to first 100% | n/a (0/31; bench is AT_LIMIT) | — | −50% |

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

### A1 — Validate & default-on the preprocess cache (RB3) `[ ]`

The macro-aware preprocessed-splice fast path (`preprocess_cache.py`, toggle
`PERMUTER_PREPROCESS_CACHE` at `preprocess_cache.py:51`, shipped commit
a1a873e8) is off by default. It is mature in design (21 tests in
`tests/test_preprocess_cache.py`, conservative silent fallback) but young
(landed 2026-05-26). Defaulting on is a measurement problem, not a code problem.
Consumes the A0 harness.

- [ ] A/B via the A0 harness: run each bench function with cache off vs on, same
      seed/patterns. (Add `bench/preprocess_cache_ab.py` driver on top of A0.)
- [ ] Assert per-variant objdiff match% identical to 4 decimals; byte-identical
      `.obj` for the no-line-shift case.
- [ ] Record fallback rate (live-macro hits) per unit cluster, build-success
      parity, wall-clock delta under parallel load.
- [ ] Exercise `PERMUTER_PREPROCESS_CACHE_STRICT` (`scorer.py:472`) — the
      existing strict-mode companion that hard-fails on cache/full-compile
      divergence — as the validation oracle during the A/B.
- [ ] Gate: zero score divergence across N≥50, median speedup ≥1.5×.
- [ ] Flip default at `preprocess_cache.py:51`; keep env var as off-switch.

**Owner:** — · **Effort:** 1.5 days · **Risk:** low (silent fallback)

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

### B1 — Validate & flag the strategy-DB → priority path `[ ]`

**Re-scoped (2026-05-26): the path already exists.**
`strategy_db.recommend_patterns` (`strategy_db.py:483`) →
`apply_strategy_boosts` (`strategy_db.py:616`, called from `hill_climber.py:700`
and `:716`) → `RoundHints.atlas_boost_patterns` → `generator._pattern_priorities`
(`generator.py:63`). So strategy records *do* boost priorities today. What's
actually missing versus the original intent:

- [ ] It's keyed on a coarse `diag_cat` string, not a full diagnosis
      fingerprint. Decide (via A0 bench) whether finer keying wins.
- [ ] It's always-on with no A/B switch. Add `PERMUTER_STRATEGY_PRIORITIES` so
      the harness can measure win-rate delta with it on vs off.
- [ ] If coarse keying underperforms, extend `recommend_patterns` to accept a
      diagnosis fingerprint (note: its current signature is
      `recommend_patterns(unit_cat, diag_cat=None, top_k=10)` — changing it
      touches `apply_strategy_boosts` and `tests/test_strategy_db.py`).

Do **not** "add `recommend_patterns`" — it exists and is unit-tested.

**Owner:** — · **Effort:** 1 day · **Risk:** low

### B2 — diff-inspect signals as hard filters `[ ]`

`RoundHints` (`types.py:553`) re-weights *softly* today (`priority_floor`,
`suppression_factor`, `adaptive_priority_boost`) feeding
`generator._pattern_priorities`. Promote strong signals (e.g. "only register
swaps in cluster K") to **hard filters** that drop patterns outright.

- [ ] Source the strong signals from `target_facts.pattern_recommendations()`
      (`target_facts.py:79`, returns boost/suppress sets) and the fact-agreement
      check in `validator.check_fact_agreement` (`validator.py:221`) /
      `beam_search._compute_fact_agreement` (`beam_search.py:331`).
      (Note: there is **no** `target_facts.fact_agreement` symbol — the original
      draft's path was wrong; `fact_agreement` is a `BeamState` field at
      `types.py:789`.)
- [ ] Apply the hard drop in `generator._pattern_priorities`.

**Risk:** medium — could over-prune. The A0 harness is the regression gate;
require no win-rate regression on the bench set before merge.

**Owner:** — · **Effort:** 1 day · **Risk:** medium (could over-prune)

### B3 — Source canonicalization dedup `[ ]`

Many textually-different variants compile to identical preprocessed output.
Existing dedup (`scorer._variant_source_md5` at `scorer.py:203`, persistent
SQLite cache, plus `generator.py` byte-identity `seen_sources`) keys on **raw
bytes**. Add `_canonicalize(source)` (whitespace/spacing normalize,
deterministic decl-group sort), hash *that* before the existing source-md5
dedup, skip the compile. `_canonicalize` does not exist yet — genuine new work.

**Owner:** — · **Effort:** 1 day · **Risk:** low

### B4 — Variant outcome predictor `[-]`

Train a small classifier to rank the build queue and cut the bottom 50% under
tight budget. `scripts/permuter/predictor.py` (does not exist yet), called from
`generate_variants` (`generator.py:237`).

**Blocker — history is thin:** `climb_history.py` currently records only
`initial_pct`/`final_pct`/`delta`/`rounds_used` and a *set* of patterns. Of the
5 features the predictor wants, only ~2 exist; **diagnosis fingerprint, function
size, beam depth, and a per-variant pattern label (vs. today's pattern set) all
need new instrumentation in `record_climb` first.** **Deferred** — instrument
history during A1/B1, then revisit once it's wide enough.

**Owner:** — · **Effort:** ~1 week R&D (after history instrumentation) · **Risk:** medium

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

### C1 — Wire `ghidra_source_diff` into beam ranking `[ ]`

`ghidra_source_diff` is computed today but only printed as a diagnostic
(`hill_climber.py:828`). Structural diff between Ghidra decompilation and our
source is a strong "what to change next" signal. Plug it into
`BeamState.ranking_key` (`types.py:801`) alongside `fact_agreement`
(`types.py:789`) and the validator score; prioritize patterns that reduce the
structural diff.

**Owner:** — · **Effort:** 2–3 days · **Risk:** medium · **Do first in C**

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

### C3 — Expression-shape synthesis for FMA cluster `[ ]`

~51 known functions with FMA/algebraic-rewrite mismatches (projected ~80% hit
rate, per `docs/sessions/2026-03-05-ghidra-guided-permuter.md`).
`ghidra_expr_match` already exists **and is wired into the `fma_reorder`
pattern** (`fma_reorder.py:104`). This item extends that coverage: detect FMA
mismatch shape via diff-inspect, synthesize algebraic rewrites
(associate/commute/factor), emit ≤4 deterministic variants. Audit how far
`fma_reorder` already gets on the cluster before adding new rewrites.

**Owner:** — · **Effort:** ~1 week · **Risk:** medium

---

## Sequencing

```
Wk1  A0 bench harness + profiling + baselines (DONE)  ∥  A3 parallelize objdiff (0.5d)   → first real numbers
Wk2  A1 preprocess-cache A/B + flip default (1.5d) → B3 canonicalize dedup (1d) → B1 flag + measure strategy priorities (1d)
Wk3  B2 hard filters (1d, harness-gated)   [A2 objdiff daemon PARKED — A0 profiling: spawn is 0.4%]
Wk4  C1 source-diff into ranking (2–3d) → C2 investigate decl-order gap (0.5d: close or file)
Wk5+ C3 FMA expr synthesis (~1wk), B4 predictor (after history instrumentation)
     [A4 compile worker PARKED — A0 profiling: spawn is 0.17%; attack compile-run via A1/B3 instead]
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
