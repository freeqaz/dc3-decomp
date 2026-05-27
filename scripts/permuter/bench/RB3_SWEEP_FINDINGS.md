# RB3 Permuter Stress / Validation Sweep

Second-toolchain (RB3 / mwcceppc) validation of the DC3 permuter. The permuter
code lives in this repo; with `PERMUTER_PROJECT=rb3` it compiles RB3 variants
(mwcceppc) and diffs RB3 `.o` targets built at `../rb3/build/SZBE69_B8`. All
runs used `apply=False` (never mutates RB3 source) and a fresh/isolated score
cache per (function, mode). Sandbox disabled (the toolchain SIGSYS's otherwise).

Bench set: `bench_set_rb3.json` (16 high-band RB3 functions, 98.8-99.0%
baseline). Seed config: patterns=all (94), max_rounds=3, max_variants=40,
plateau_limit=2, compose=true, workers=6, PYTHONHASHSEED=0.

Drivers (committed): `rb3_ppab.py` (preprocess-cache 3-mode A/B),
`rb3_flag_ab.py` (single-flag A/B), throughput via `rb3_bench_driver.py
throughput` + `run.py`.

---

## 1. Preprocess-cache A/B — `PERMUTER_PREPROCESS_CACHE` (N = 185)

Three modes against the SAME variant list per function:
- **off** — cache unset (baseline full compile per variant).
- **on-strict** — cache + `STRICT=1` (byte-identical `.o` oracle; near-zero
  hits on mwcceppc because splicing shifts debug-info line numbers).
- **on-nonstrict** — cache on, validated by objdiff-score equivalence (the real
  default-on path; where speedup shows up).

Divergence = per-variant objdiff match% differs from `off` to 4 decimals.

Results (`rb3_ppab-merged.json` = full 16-fn run + fill-pass for functions that
hit transient source-lock contention at run start):

| Metric | Value |
|--------|-------|
| functions scored | 15 / 16 |
| **variants compared** | **185** (gate min 50: PASS) |
| **divergences (nonstrict)** | **0** |
| **divergences (strict)** | **0** |
| fast-hit rate (nonstrict) | 47.6% (91 hits / 191) |
| pooled compile-run ms (off → nonstrict) | 2785.7 → 2314.0 |
| **pooled nonstrict speedup** | **1.20x** |
| **median nonstrict speedup (per-fn)** | **1.18x** |
| pooled strict speedup | 1.08x |

The win splits sharply by **macro density** of the function body:

| Population | n | pooled nonstrict speedup |
|------------|---|--------------------------|
| macro-free bodies (fast path active) | 11 | **1.44x** (median 1.46x) |
| macro-heavy bodies (full fallback)   | 4  | **0.93x** |

Per-function (nonstrict speedup):
- Fast-path active: CalcMotion 1.92, CalculateValue 2.13, SetPitch 1.66,
  AddPhrasePoints 1.62, GetEnabledStateAt 1.56, GetRot 1.46, DrawLod 1.28,
  PlatformMgr 1.18, DeterminePhraseTimes 1.09. (MarkDownloaded/MarkDeleted
  nominally "active" but 1 hit / 13 fallbacks each → ~0.97x.)
- Full fallback (0 hits): ReloadMessages 0.98, UpdateNowBar 0.96,
  IsDoubleStrum 0.91, ShowNext 0.78.

### Why the gate result differs from the prelim 3-fn run

The prelim (3 fns / 34 variants) reported a 1.87x *median* — but that sample
happened to weight the two macro-free functions (2.2x, 1.8x) against one
fallback function. At N=185 the macro split is the dominant effect: ~half of
real RB3 functions reference a **live** macro (`MILO_ASSERT`, `FOREACH`,
`RELEASE`, etc.) in their body, which the macro-aware gate in
`preprocess_cache.py` correctly refuses to splice into the macro-free `.i` —
forcing a full-compile fallback. The fallback path is mildly **net-negative**
(0.93x pooled) because it pays for the one-time `-E` preprocess + per-variant
macro scan and then still does the full compile.

### VERDICT — default-on for RB3: NO (do not flip)

- **Correctness: clean.** 0 divergences over 185 variants in BOTH strict and
  nonstrict. The fast path never produced a wrong score. STRICT mode (the
  zero-divergence oracle) also showed 0 divergences on the functions where it
  built a cache.
- **Speed: below the ≥1.5x gate at scale.** Pooled 1.20x / median 1.18x. Only
  the macro-free subset (1.44x) approaches the bar, and even that is under 1.5x.
  Macro-heavy functions regress slightly.
- The DC3 path may still pass its own gate (DC3's MSVC `.i` splice is
  byte-identical and DC3 macro density differs) — that is the coordinator's
  cross-check. **On RB3 specifically the speedup does not justify default-on,**
  though it is safe to leave available as an opt-in (`PERMUTER_PREPROCESS_CACHE=1`)
  for macro-light functions where it delivers 1.4-2.1x with zero risk.

---

## 2. Throughput + stability sweep (16 RB3 functions)

Full permuter (`hill_climb`, 3 rounds, compose on, adaptive off) over the 16-fn
bench set, fresh isolated cache, profiling on (`rb3_throughput.json`).

| Metric | RB3 (mwcceppc) | DC3 baseline (for ref) |
|--------|----------------|------------------------|
| variants / second | **2.14** | 1.47 |
| total variants scored | 345 | 816 |
| wins / 100 attempts | **6.25** (1/16 improved) | 12.9 |
| functions reached 100% | 0 | 0 |
| overall wall | 160.9 s | 553.5 s |
| **crashes / exceptions / hangs** | **0** | — |

**Stability: clean.** 0 errors, 0 crashes, 0 hangs across all 16 functions and
345 variant compiles on the mwcceppc toolchain. The one win:
`VocalPart::AddPhrasePoints` 98.9344% → 99.0164% (+0.082%) via
`compose:argument_swap+declaration_reorder` — a real cross-toolchain win,
confirming the compose/beam machinery works identically on RB3.

Per-call subprocess cost (the A2/A4 input, contention-robust):
- **compile**: 258 calls, spawn 0.88ms + **run 2136ms** (mwcceppc parse is the
  cost — exactly what the preprocess-cache targets).
- **objdiff**: 158 calls, spawn 1.02ms + **run 25.6ms** (10x cheaper than DC3's
  263ms — RB3 `.o` are smaller / objdiff has less to compare).

Implication: on RB3 the compile-run dominates even harder than on DC3 (objdiff
is nearly free), so the preprocess-cache *should* be a bigger relative win here
— but it is gated by macro density (section 1). The lever that would lift RB3
above the 1.5x bar is widening fast-path coverage past macro-referencing bodies
(see section 4), not anything in the objdiff/diff path.

### Note on intermittent source-lock contention

Across the sweep runs, a handful of functions intermittently failed with
`RuntimeError: Source file locked by another permuter`. Root cause: the RB3
source tree is shared, and (a) a SIGKILL'd permuter's `flock` lingers until the
kernel reaps the fd, and (b) other agents occasionally touch `../rb3`. This is
the lock guard working **correctly** — it fails fast rather than letting two
permuters race the same `.cpp`. It is not engine instability: every function
that failed succeeded cleanly on a re-run against a quiet tree (section 1's fill
pass). `rb3_ppab.py` now has `--only` / `--start` / `--count` + `--checkpoint`
so a contended run can be resumed without losing completed work.

---

## 3. Flag A/B on RB3 (`rb3_flag_ab.py`, A = control, B = treatment)

Each flag: 2 sides x `hill_climb` (3 rounds, fresh cache) over the first 10
RB3 bench functions. "Win" = function improved above baseline at least once.
Stability = exceptions captured per side (never swallowed).

### `PERMUTER_HARD_FILTERS` (A=off, B=on, adaptive on)

| | A (off) | B (on) |
|---|---|---|
| wins | 1/10 | 1/10 |
| variants scored | 226 | 226 |
| variants/sec | 1.59 | 1.61 |
| errors | 0 | 0 |

**Verdict: SAFE but a no-op on RB3.** Identical wins and identical variant
counts — no RB3 function in the sample produced suppress signals strong enough
for the hard filter to cull anything. No regression, no crashes. Nothing to
gain or lose here on RB3; the DC3 B2 validation is the deciding data.

### `PERMUTER_PREDICTOR` (A=off, B=on with `PREDICTOR_BUDGET=8`, cull 0.5)

| | A (off) | B (on, budget 8) |
|---|---|---|
| wins | 1/10 | 1/10 |
| variants scored | 237 | **146 (-38.4%)** |
| wall seconds | 152.2 | **119.4 (-21.6%)** |
| errors | 0 | 0 |

**Verdict: SAFE and a throughput win on RB3.** With a tight budget (8 << the
40 max_variants) the predictor culled ~38% of the variant queue and cut wall
time ~22% while preserving the single win (it kept the winner even when it
compiled only 8/17 variants on that function). No regression, no crashes. This
is the predictor working as designed; the open question is whether a tighter
budget ever drops a real winner on a larger function set — not observed in this
sample. Worth the coordinator's consideration for a low-budget default, but the
budget is the risk knob, not the flag itself.

### `PERMUTER_C1_SOURCE_DIFF` (A=off, B=both)

| | A (off) | B (both) |
|---|---|---|
| wins | 1/10 | 1/10 |
| variants scored | 237 | 237 |
| errors | 0 | 0 |

**Verdict: SAFE, neutral on RB3.** Identical wins and variant counts — C1 is a
beam-ranking tie-break, so it reorders the queue but does not change the
discovered set on this 10-fn sample. No regression, no crashes. (C1 is already
default-on per the repo history; this run confirms it does no harm on RB3.)

---

## 4. Optimization findings — RB3 / mwcceppc path

1. **objdiff is nearly free on RB3** (25.6ms/call vs DC3's 263ms). RB3 `.o`
   targets are smaller, so any diff-side optimization (A2 daemon, etc.) has
   almost no headroom here. The entire per-variant cost is the **mwcceppc
   compile (2136ms run)**. Optimization effort on RB3 should target the
   compile, not the diff.

2. **The preprocess-cache is the right lever but macro-gated.** It directly
   attacks the 2136ms compile by skipping the `-E` re-parse, and where it
   fires it delivers 1.4-2.1x with zero divergences. The ceiling is the
   macro-aware gate: ~half of real RB3 functions reference a live macro
   (`MILO_ASSERT`, `FOREACH`, `RELEASE`, ...) in the body and fall back to a
   full compile. **The highest-value RB3 optimization is widening fast-path
   coverage past simple macro references** — e.g. splicing the *expanded* form
   of a known set of safe object-like macros, or pre-expanding the variant body
   against the cached live-macro map before the splice. That would move the
   macro-heavy population (currently 0.93x) onto the fast path and likely lift
   the pooled RB3 speedup above 1.5x.

3. **The fallback path is mildly net-negative** (0.93x pooled on macro-heavy
   fns). When the cache is enabled but the body can't be spliced, the run still
   pays the one-time `-E` preprocess + the per-variant macro scan and then does
   the full compile anyway. For a default-on world this argues for a per-
   function "is this body splice-eligible?" pre-check that disables the cache
   entirely (skipping even the `-E`) for macro-dense functions, so the fallback
   never costs more than plain OFF.

4. **The predictor is the cheapest throughput win on RB3** (-38% variants /
   -22% wall at budget 8, no win loss), and unlike the preprocess-cache it is
   compiler-agnostic (it culls before compile, so mwcceppc's high compile cost
   makes each culled variant worth more here than on DC3).

5. **Lock contention is real on a shared tree.** Running the permuter against
   `../rb3` while other agents touch it (or after a SIGKILL'd run) hits the
   `flock` guard. This is correct fail-fast behavior, but for batch sweeps the
   driver should clean stale `.permuter.lock` files at startup and retry locked
   functions once — the `--only` re-run path makes that a one-liner today.

---

## Summary — verdicts (RB3 only; coordinator cross-checks DC3)

| Flag / feature | RB3 verdict | Evidence |
|----------------|-------------|----------|
| `PERMUTER_PREPROCESS_CACHE` | **Do NOT default-on for RB3** (safe as opt-in) | N=185, 0 divergences, but pooled 1.20x / median 1.18x < 1.5x; macro-gated (1.44x active / 0.93x fallback) |
| `PERMUTER_HARD_FILTERS` | SAFE, no-op on RB3 | 1/1 wins, 226/226 variants, 0 err |
| `PERMUTER_PREDICTOR` (budget 8) | SAFE, throughput win | -38% variants, -22% wall, 1/1 wins, 0 err |
| `PERMUTER_C1_SOURCE_DIFF` | SAFE, neutral | 1/1 wins, 237/237 variants, 0 err |

**Engine stability on a second toolchain: solid.** Across all sweeps (ppab +
throughput + 3 flag A/Bs ≈ 1700+ variant compiles on mwcceppc): **0 genuine
crashes / exceptions / hangs.** The only failures were transient
source-`flock` contention on the shared RB3 tree (the lock guard working
correctly), all of which succeeded on re-run.



