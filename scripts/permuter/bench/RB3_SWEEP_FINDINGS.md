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
